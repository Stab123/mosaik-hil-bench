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
} mosaik_config_t;

/* Architecture-level defaults from MOSAIK-ADD-0001. */
void mosaik_config_default(mosaik_config_t *cfg);

/* Leadership lease duration in simulated milliseconds. A leader that cannot
 * demonstrate majority connectivity within this interval loses valid
 * leadership authority. This is a host-model parameter; it does not validate
 * physical CAN-FD timing. */
#define MOSAIK_LEADERSHIP_LEASE_MS 500u

typedef struct {
    uint8_t          id;
    mosaik_config_t  cfg;

    mosaik_role_t    role;
    mosaik_state_t   state;
    uint16_t         term;

    uint8_t          voted_for;   /* 0 = no vote cast in voted_term */
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

    /* Observability, for the test bench and for the on-target trace. */
    uint32_t         became_leader_ms;
    uint32_t         safe_entry_ms;
    uint32_t         safe_trigger_ms;
    uint32_t         decode_errors;

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

#endif /* MOSAIK_NODE_H */
