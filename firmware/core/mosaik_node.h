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
    uint8_t  cluster_size;
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

#endif /* MOSAIK_NODE_H */
