/* MOSAIK HIL bench - node state machine
 *
 * Deliberately free of any platform dependency: the caller supplies a
 * monotonic millisecond clock and a transmit callback. This is what makes
 * the logic testable on a host without hardware, and identical to what runs
 * on the target.
 *
 * Requirements exercised (see docs/TEST-PLAN.md):
 *   REQ-002  exactly one active leader; split-brain prohibited
 *   REQ-004  leader election completes < 1000 ms after leader loss
 *   REQ-005  SAFE mode entered < 10 ms after a critical invariant violation
 *
 * Leadership lease (Lot 2A): a leader must maintain evidence of majority
 * connectivity. The nominal lease is 500 ms. If a leader cannot refresh
 * its lease within this interval, its leadership authority becomes invalid.
 * This prevents an isolated old leader from retaining authority indefinitely
 * during a 2+1 network partition.
 *
 * Stale/replay immunity (Lot 2B): the protocol rejects messages that would
 * improperly restore obsolete leadership authority, invalidate a newer term,
 * or destabilize valid cluster state. Rejection is based on term monotonicity,
 * lease validity, and sender state, using available protocol fields.
 * No cryptographic anti-replay is claimed; sequence numbers are not present
 * in the current frame format. See LOT2B_STALE_REPLAY_REPORT.md.
 */
#ifndef MOSAIK_NODE_H
#define MOSAIK_NODE_H

#include "mosaik_proto.h"

typedef void (*mosaik_tx_fn)(const mosaik_frame_t *frame, void *user);

typedef struct {
    uint16_t heartbeat_period_ms;
    uint16_t election_timeout_min_ms;
    uint16_t election_timeout_max_ms;
    uint16_t vote_timeout_ms;
    uint8_t  cluster_size;         /* Legacy boot parameter. Since Lot 5 it is
                                    * NOT the membership: quorum is derived from
                                    * the committed membership mask (see
                                    * mosaik_config_store_t). Retained for
                                    * backward-compatible configuration only. */
    uint8_t  max_failed_elections; /* before declaring SAFE / NO_QUORUM */
    /* Candidate retry backoff span (Lot 2D, C2-a). When a candidate's vote
     * timeout expires without quorum and SAFE is not reached, the node waits
     * an additional (rng % span) milliseconds, drawn from its own per-node
     * RNG, before starting the next election. A span of S gives exactly S
     * possible deterministic waits, 0..S-1 ms. A span of 1 gives a wait of
     * 0 ms on every node, i.e. no desynchronisation. 0 is treated as 1.
     * The first election attempt is unaffected. */
    uint16_t candidate_retry_backoff_span_ms;
} mosaik_config_t;

/* Architecture-level defaults from MOSAIK-ADD-0001. */
void mosaik_config_default(mosaik_config_t *cfg);

/* Leadership lease duration in simulated milliseconds. A leader that cannot
 * demonstrate majority connectivity within this interval loses valid
 * leadership authority. This is a host-model parameter; it does not validate
 * physical CAN-FD timing. */
#define MOSAIK_LEADERSHIP_LEASE_MS 500u

/* Lot 5: committed configuration and its host-model persistence.
 *
 * Membership is an explicit 3-bit voter mask (bit n-1 = node n) identified
 * by (config_epoch, mask). The configuration epoch is a membership epoch,
 * independent of the leadership term. A node changes its committed
 * configuration only through the CONFIG transaction (PROPOSE, ACCEPT,
 * COMMIT/ANNOUNCE) and only to epoch + 1.
 *
 * HOST-MODEL PERSISTENCE ONLY. The store is memory owned by the caller
 * that survives the modelled crash and cold restart of one node. It holds
 * exactly what the node itself wrote before crashing: its committed
 * (epoch, mask) and its single acceptance binding for the next epoch.
 * No FRAM/NVM hardware, no persistence of term, vote or SAFE is claimed
 * or modelled. A node without a store (NULL) is volatile and boots with
 * the default configuration. */
#define MOSAIK_CONFIG_EPOCH_INITIAL 1u
#define MOSAIK_MEMBERSHIP_ALL       0x07u

typedef struct {
    uint16_t committed_epoch;   /* 0 = empty store */
    uint8_t  committed_mask;
    uint16_t accepted_epoch;    /* acceptance binding: one successor per epoch */
    uint8_t  accepted_mask;
} mosaik_config_store_t;

/* LOT 6A: software-facing transport status (PRE-HARDWARE SPECIFICATION).
 *
 * This is the smallest local input that lets a node distinguish "I cannot
 * hear anyone" (a partition, already modelled since Lot 2C) from "I myself
 * cannot speak" (a transmitter fault, which a partition never tells a node
 * about). It is LOCAL EVIDENCE ABOUT THIS NODE ONLY. It carries no
 * information about any peer, about topology, about who is leader, or about
 * whether any frame was delivered anywhere. A platform reports it from its
 * own controller; nothing derives it from the bus.
 *
 * The four values are the software-facing abstraction of the CAN error-
 * confinement states, which exist in the CAN standard itself and are
 * therefore not invented here:
 *
 *   UP          error-active: the controller transmits and acknowledges
 *               normally.
 *   DEGRADED    error-passive: the controller still transmits, but its
 *               error counters show a persistent fault. Transmission is
 *               still possible, so this is not muteness.
 *   BUS_OFF     the controller has removed itself from the bus. It cannot
 *               transmit at all. This is LOCAL KNOWLEDGE OF OWN MUTENESS.
 *   RECOVERING  bus-off recovery is in progress and not complete. The
 *               controller is still mute.
 *
 * HARDWARE DETECTION AND RECOVERY TIMING ARE NOT SPECIFIED HERE and are not
 * validated: they are LOT 6B, and no hardware exists. LOT 6A specifies only
 * what the protocol core must do with a status that some platform reports.
 *
 * LOT 6A RED BASELINE: the status is recorded and can be read back, and it
 * is NOT read by any protocol decision. Transmission, leadership authority,
 * lease and mode semantics ignore it entirely. The tests TC-092, TC-093,
 * TC-096, TC-097 and TC-098 fail because of that, by design. */
typedef enum {
    MOSAIK_TRANSPORT_UP         = 0,
    MOSAIK_TRANSPORT_DEGRADED   = 1,
    MOSAIK_TRANSPORT_BUS_OFF    = 2,
    MOSAIK_TRANSPORT_RECOVERING = 3
} mosaik_transport_status_t;

/* Message rejection reason codes for test instrumentation. */
typedef enum {
    MOSAIK_REJECT_NONE = 0,
    MOSAIK_REJECT_STALE_TERM,        /* term < local term */
    MOSAIK_REJECT_STALE_LEASE,       /* term == local term but lease expired */
    MOSAIK_REJECT_DUPLICATE_SEQ,     /* duplicate sequence number from same src */
    MOSAIK_REJECT_STALE_AUTHORITY,   /* authority-bearing msg from expired leader */
    MOSAIK_REJECT_INVALID_SENDER     /* sender state invalid for msg type */
} mosaik_reject_reason_t;

typedef struct {
    uint8_t          id;
    mosaik_config_t  cfg;

    mosaik_role_t    role;
    mosaik_state_t   state;
    uint16_t         term;

    uint8_t          voted_for;   /* node voted for in voted_term; 0 = none.
                                    * Never erased by a role change: a vote is
                                    * recorded once per term. Any term other
                                    * than voted_term has no vote yet. */
    uint16_t         voted_term;
    uint8_t          vote_mask;   /* bit (n-1) set when node n granted */

    uint8_t          leader_id;   /* 0 = unknown */
    uint8_t          failed_elections;
    uint8_t          safe_cause;
    uint8_t          seq;

    uint32_t         now_ms;
    uint32_t         last_hb_rx_ms;
    uint32_t         last_tx_ms;
    uint32_t         deadline_ms;
    uint32_t         rng;

    /* Leadership lease tracking. */
    uint32_t         last_quorum_contact_ms; /* last time majority was reachable */
    uint32_t         lease_expiry_ms;        /* when current lease expires */

    /* Stale/replay detection (Lot 2B). */
    uint8_t          last_hb_seq[4];         /* last seen HB seq per src (1..3) */
    uint16_t         last_hb_term[4];        /* last seen HB term per src */
    uint8_t          last_ack_seq[4];        /* last seen ACK seq per src (1..3) */
    uint16_t         last_ack_term[4];       /* last seen ACK term per src */

    /* Peer SAFE evidence (Lot 3). Bit (src-1) of the mask is set once at least
     * one SAFE frame has actually been received from that peer;
     * last_safe_rx_ms[src-1] is the local time of the latest one. Evidence is
     * fresh while (now_ms - last_safe_rx_ms) < 3 * heartbeat_period_ms. */
    uint32_t         last_safe_rx_ms[4];
    uint8_t          safe_evidence_mask;

    /* Lot 5: per-peer time of the last accepted current-term ACK (own
     * clock), the evidence the leadership-lease quorum predicate reads. */
    uint32_t         last_ack_rx_ms[4];

    /* Lot 5: committed configuration, acceptance binding and the
     * proposer's volatile transaction state. */
    uint16_t         committed_epoch;
    uint8_t          committed_mask;
    uint16_t         accepted_epoch;     /* == committed_epoch + 1 while a promise is pending */
    uint8_t          accepted_mask;
    bool             proposing;
    uint16_t         proposal_epoch;
    uint8_t          proposal_mask;
    uint8_t          accept_set;         /* members whose ACCEPT was actually received (incl. self) */
    uint32_t         proposal_start_ms;
    uint32_t         last_propose_tx_ms;
    uint32_t         last_cfg_announce_ms;
    mosaik_config_store_t *store;        /* host-model persistent store, may be NULL */

    /* Lot 5 rejection / observation counters (test instrumentation). */
    uint32_t         nonmember_rejections;      /* consensus frame from/for a non-member */
    uint32_t         config_stale_rejections;   /* CONFIG epoch below the committed one */
    uint32_t         config_future_rejections;  /* CONFIG epoch beyond committed + 1 */
    uint32_t         config_conflict_rejections;/* second successor for the same epoch */
    uint32_t         config_duplicates;         /* idempotent repeats */
    uint32_t         config_mismatch_observed;  /* same epoch, different mask (inconsistency) */
    uint32_t         config_commits;

    /* LOT 6A: locally reported transport status, the time it was last set on
     * this node's own clock, and how many times it changed. SPECIFICATION
     * ONLY in the RED baseline: written by mosaik_set_transport_status(),
     * read by no protocol decision. */
    uint8_t          transport_status;
    uint32_t         transport_status_ms;
    uint32_t         transport_status_changes;

    /* Observability, for the test bench and for the on-target trace. */
    uint32_t         became_leader_ms;
    uint32_t         safe_entry_ms;
    uint32_t         safe_trigger_ms;
    uint32_t         decode_errors;

    /* Rejection counters (Lot 2B test instrumentation). */
    uint32_t         stale_term_rejections;
    uint32_t         stale_lease_rejections;
    uint32_t         duplicate_seq_rejections;
    uint32_t         stale_authority_rejections;
    uint32_t         invalid_sender_rejections;

    mosaik_tx_fn     tx;
    void            *user;
} mosaik_node_t;

void mosaik_init(mosaik_node_t *node, uint8_t id, const mosaik_config_t *cfg,
                 mosaik_tx_fn tx, void *user, uint32_t now_ms);

/* Feed one received CAN frame. Safety transitions happen inside this call,
 * so the detection-to-SAFE latency is bounded by the receive path itself. */
void mosaik_on_rx(mosaik_node_t *node, uint32_t now_ms, const mosaik_frame_t *frame);

/* Periodic service. Call at least every heartbeat_period_ms / 4. */
void mosaik_tick(mosaik_node_t *node, uint32_t now_ms);

/* True while the node holds the leader role. This does NOT imply valid
 * leadership authority; use mosaik_has_valid_leadership_authority() for
 * safety-critical decisions. */
bool mosaik_is_leader(const mosaik_node_t *node);

/* True if the node holds the leader role AND its leadership lease is valid.
 * A leader must maintain majority connectivity evidence within the lease
 * interval (500 ms nominal). This is the correct predicate for
 * leader-dependent safety decisions. */
bool mosaik_has_valid_leadership_authority(const mosaik_node_t *node);

/* Get the last rejection reason (for test instrumentation). */
mosaik_reject_reason_t mosaik_get_last_reject_reason(const mosaik_node_t *node);

/* Lot 5: attach the node's own host-model configuration store and load the
 * committed configuration and acceptance binding it holds. An empty store
 * is initialised to the default configuration (all nodes, epoch 1). Call
 * once after mosaik_init(). */
void mosaik_load_config_store(mosaik_node_t *node, mosaik_config_store_t *store);

/* Lot 5: external command starting a membership transaction towards
 * target_mask. Accepted only from a committed member holding valid
 * leadership authority with no transaction pending, for a mask that is a
 * non-empty subset of the three nodes with at least two members, includes
 * the requester, differs from the committed mask and does not conflict
 * with an acceptance already bound for the next epoch. Returns whether the
 * PROPOSE was issued; it never commits anything by itself. */
bool mosaik_request_reconfiguration(mosaik_node_t *node, uint8_t target_mask);

/* Lot 5: true while this node belongs to its committed membership. */
bool mosaik_is_member(const mosaik_node_t *node);

/* Lot 5: the nodes that take part in consensus from this node's point of
 * view: its committed membership, joined with the accepted successor while
 * an acceptance is pending (joint participation set). Quorum is NOT taken
 * over this set; see mosaik_has_quorum_ack_evidence(). */
uint8_t mosaik_effective_members(const mosaik_node_t *node);

/* Lot 5: true when this leader holds fresh (within one heartbeat period),
 * current-term acknowledgements actually received from a quorum of its
 * committed membership AND, while an acceptance is pending, from a quorum
 * of the accepted successor, the leader itself included. Used by the
 * caller's lease renewal; outbound traffic never counts. */
bool mosaik_has_quorum_ack_evidence(const mosaik_node_t *node);

/* LOT 6A: report this node's own transport status. The only legitimate
 * caller is the platform layer that owns this node's CAN controller, or the
 * host bench standing in for it. Passing another node's status would be
 * magical information and is forbidden by INV-TRANSPORT-LOCAL-EVIDENCE.
 * A cold restart through mosaik_init() resets the status to UP, matching a
 * controller that re-enters error-active when it is re-initialised. */
void mosaik_set_transport_status(mosaik_node_t *node, uint32_t now_ms,
                                 mosaik_transport_status_t status);

/* LOT 6A: the status this node last reported about itself. */
mosaik_transport_status_t mosaik_get_transport_status(const mosaik_node_t *node);

/* LOT 6A: whether this node's own transport is able to transmit at all.
 * False exactly for BUS_OFF and RECOVERING, which are the two states in
 * which the controller has removed itself from the bus. DEGRADED
 * (error-passive) still transmits and is therefore not muteness.
 *
 * RED BASELINE: this predicate is correct and no protocol path consults it.
 * Making the protocol honour it is LOT 6A GREEN, not this commit. */
bool mosaik_transport_can_transmit(const mosaik_node_t *node);

#endif /* MOSAIK_NODE_H */
