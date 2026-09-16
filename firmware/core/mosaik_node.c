#include "mosaik_node.h"

static mosaik_reject_reason_t g_last_reject_reason = MOSAIK_REJECT_NONE;

void mosaik_config_default(mosaik_config_t *cfg)
{
    cfg->heartbeat_period_ms     = 100u;  /* MOSAIK-ADD-0001 */
    cfg->election_timeout_min_ms = 300u;  /* 3 missed heartbeats */
    cfg->election_timeout_max_ms = 500u;  /* 5 missed heartbeats */
    cfg->vote_timeout_ms         = 150u;
    cfg->cluster_size            = 3u;
    cfg->max_failed_elections    = 3u;
    cfg->candidate_retry_backoff_span_ms = 50u; /* Lot 2D, C2-a */
}

mosaik_reject_reason_t mosaik_get_last_reject_reason(const mosaik_node_t *node)
{
    (void)node;
    return g_last_reject_reason;
}

static uint8_t quorum(const mosaik_node_t *n)
{
    return (uint8_t)((n->cfg.cluster_size / 2u) + 1u);
}

static uint8_t popcount8(uint8_t v)
{
    uint8_t c = 0u;
    while (v) { c = (uint8_t)(c + (v & 1u)); v = (uint8_t)(v >> 1); }
    return c;
}

/* xorshift32, seeded per node. Deterministic, so test runs are reproducible. */
static uint32_t rng_next(mosaik_node_t *n)
{
    uint32_t x = n->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    n->rng = x;
    return x;
}

static uint32_t election_timeout(mosaik_node_t *n)
{
    uint32_t span = (uint32_t)(n->cfg.election_timeout_max_ms -
                               n->cfg.election_timeout_min_ms + 1u);
    return n->cfg.election_timeout_min_ms + (rng_next(n) % span);
}

static void emit(mosaik_node_t *n, mosaik_msg_type_t type, uint8_t arg)
{
    mosaik_msg_t msg;
    mosaik_frame_t frame;

    msg.type    = type;
    msg.src     = n->id;
    msg.version = MOSAIK_PROTO_VERSION;
    msg.role    = n->role;
    msg.state   = n->state;
    msg.term    = n->term;
    msg.arg     = arg;

    mosaik_encode(&frame, &msg);
    n->last_tx_ms = n->now_ms;
    if (n->tx) {
        n->tx(&frame, n->user);
    }
}

/* REQ-005. Entered synchronously from the detection point. */
static void enter_safe(mosaik_node_t *n, mosaik_safe_cause_t cause, uint32_t trigger_ms)
{
    if (n->state == MOSAIK_STATE_SAFE) {
        return;
    }
    n->state           = MOSAIK_STATE_SAFE;
    n->role            = MOSAIK_ROLE_FOLLOWER;
    n->leader_id       = 0u;
    n->safe_cause      = (uint8_t)cause;
    n->safe_trigger_ms = trigger_ms;
    n->safe_entry_ms   = n->now_ms;
    emit(n, MOSAIK_MSG_SAFE, (uint8_t)cause);
}

/* Lot 2B: Stale/replay detection helpers. */

/* Check if a heartbeat message is stale (term too old, duplicate seq, or
 * authority expired). Returns rejection reason or MOSAIK_REJECT_NONE if OK. */
static mosaik_reject_reason_t check_heartbeat_stale(const mosaik_node_t *n,
                                                     const mosaik_msg_t *msg)
{
    uint8_t src = msg->src;
    if (src >= 4u) {
        return MOSAIK_REJECT_INVALID_SENDER;
    }

    /* Term monotonicity: reject messages from older terms. */
    if (msg->term < n->term) {
        return MOSAIK_REJECT_STALE_TERM;
    }

    /* Same-term authority: if we are leader in this term and lease is valid,
     * another heartbeat in same term is split-brain (handled elsewhere).
     * If we are follower, check if the sender's authority is stale. */
    if (msg->term == n->term) {
        uint8_t src_idx = src - 1u;
        /* Duplicate sequence number from same source. */
        if (msg->arg == n->last_hb_seq[src_idx] && n->last_hb_seq[src_idx] != 0u) {
            return MOSAIK_REJECT_DUPLICATE_SEQ;
        }
        /* Stale authority: if sender claims leader but our lease from them
         * would have expired (we track per-sender lease via term). */
        if (msg->role == MOSAIK_ROLE_LEADER) {
            /* If we have a newer term recorded for this source, it's stale. */
            if (n->last_hb_term[src_idx] > msg->term) {
                return MOSAIK_REJECT_STALE_AUTHORITY;
            }
        }
    }

    return MOSAIK_REJECT_NONE;
}

static void become_follower(mosaik_node_t *n, uint16_t term)
{
    n->role      = MOSAIK_ROLE_FOLLOWER;
    n->term      = term;
    /* Vote memory (voted_for, voted_term) is deliberately NOT erased here.
     * One vote per term must hold across role changes within the same term
     * (LOT 2 erratum, TC-050). Eligibility to vote in a strictly higher term
     * follows from voted_term differing from that term. */
    n->vote_mask = 0u;
    n->deadline_ms = n->now_ms + election_timeout(n);
}

static void start_election(mosaik_node_t *n)
{
    n->term       = (uint16_t)(n->term + 1u);
    n->role       = MOSAIK_ROLE_CANDIDATE;
    n->voted_for  = n->id;
    n->voted_term = n->term;
    n->vote_mask  = (uint8_t)(1u << (n->id - 1u));
    n->leader_id  = 0u;
    n->deadline_ms = n->now_ms + n->cfg.vote_timeout_ms;
    if (n->state == MOSAIK_STATE_INIT) {
        n->state = MOSAIK_STATE_DEGRADED;
    }
    emit(n, MOSAIK_MSG_VOTE_REQ, 0u);
}

static void become_leader(mosaik_node_t *n)
{
    n->role                     = MOSAIK_ROLE_LEADER;
    n->leader_id                = n->id;
    n->state                    = MOSAIK_STATE_NOMINAL;
    n->failed_elections         = 0u;
    n->became_leader_ms         = n->now_ms;
    n->last_quorum_contact_ms   = n->now_ms;
    n->lease_expiry_ms          = n->now_ms + MOSAIK_LEADERSHIP_LEASE_MS;
    emit(n, MOSAIK_MSG_HEARTBEAT, n->seq++);
}

void mosaik_init(mosaik_node_t *node, uint8_t id, const mosaik_config_t *cfg,
                 mosaik_tx_fn tx, void *user, uint32_t now_ms)
{
    node->id  = id;
    node->cfg = *cfg;
    node->role  = MOSAIK_ROLE_FOLLOWER;
    node->state = MOSAIK_STATE_INIT;
    node->term  = 0u;
    node->voted_for = 0u;
    node->voted_term = 0u;
    node->vote_mask = 0u;
    node->leader_id = 0u;
    node->failed_elections = 0u;
    node->safe_cause = (uint8_t)MOSAIK_SAFE_NONE;
    node->seq = 0u;
    node->now_ms = now_ms;
    node->last_hb_rx_ms = now_ms;
    node->last_tx_ms = now_ms;
    node->rng = 0x9E3779B9u ^ ((uint32_t)id * 2654435761u);
    node->became_leader_ms = 0u;
    node->safe_entry_ms = 0u;
    node->safe_trigger_ms = 0u;
    node->decode_errors = 0u;
    node->last_quorum_contact_ms = now_ms;
    node->lease_expiry_ms = now_ms;
    node->stale_term_rejections = 0u;
    node->stale_lease_rejections = 0u;
    node->duplicate_seq_rejections = 0u;
    node->stale_authority_rejections = 0u;
    node->invalid_sender_rejections = 0u;
    for (int i = 0; i < 4; i++) {
        node->last_hb_seq[i] = 0u;
        node->last_hb_term[i] = 0u;
        node->last_ack_seq[i] = 0u;
        node->last_ack_term[i] = 0u;
    }
    node->tx = tx;
    node->user = user;
    node->deadline_ms = now_ms + election_timeout(node);
}

bool mosaik_is_leader(const mosaik_node_t *node)
{
    return node->role == MOSAIK_ROLE_LEADER;
}

void mosaik_on_rx(mosaik_node_t *node, uint32_t now_ms, const mosaik_frame_t *frame)
{
    mosaik_msg_t msg;

    node->now_ms = now_ms;

    if (!mosaik_decode(frame, &msg)) {
        node->decode_errors++;
        return;
    }
    if (msg.src == node->id) {
        return; /* own frame echoed back on the bus */
    }
    if (node->state == MOSAIK_STATE_SAFE) {
        return; /* SAFE is latched; recovery requires ground arbitration */
    }

    /* A higher term always wins: step down and adopt it. */
    if (msg.term > node->term) {
        become_follower(node, msg.term);
    }

    switch (msg.type) {

    case MOSAIK_MSG_HEARTBEAT:
        /* Lot 2B: Stale/replay rejection for heartbeats. */
        {
            mosaik_reject_reason_t reject = check_heartbeat_stale(node, &msg);
            if (reject != MOSAIK_REJECT_NONE) {
                g_last_reject_reason = reject;
                switch (reject) {
                    case MOSAIK_REJECT_STALE_TERM:
                        node->stale_term_rejections++;
                        break;
                    case MOSAIK_REJECT_DUPLICATE_SEQ:
                        node->duplicate_seq_rejections++;
                        break;
                    case MOSAIK_REJECT_STALE_AUTHORITY:
                        node->stale_authority_rejections++;
                        break;
                    default:
                        break;
                }
                return; /* reject stale/replayed heartbeat */
            }
            g_last_reject_reason = MOSAIK_REJECT_NONE;
        }

        if (msg.term < node->term) {
            return; /* stale leader, ignore */
        }
        /* REQ-002 invariant: two leaders in the same term must never coexist.
         * Observing one is a critical violation, not a condition to resolve. */
        if (node->role == MOSAIK_ROLE_LEADER && msg.term == node->term) {
            enter_safe(node, MOSAIK_SAFE_SPLIT_BRAIN, now_ms);
            return;
        }
        node->role             = MOSAIK_ROLE_FOLLOWER;
        node->leader_id        = msg.src;
        node->state            = MOSAIK_STATE_NOMINAL;
        node->failed_elections = 0u;
        node->last_hb_rx_ms    = now_ms;
        /* Track last seen heartbeat sequence and term per source for
         * duplicate/replay detection (Lot 2B). 0-indexed peer. */
        node->last_hb_seq[msg.src - 1u] = msg.arg;
        node->last_hb_term[msg.src - 1u] = msg.term;
        /* Leadership lease: followers must not challenge the leader until the
         * lease expires (500 ms). This enforces INV-LEADER-UNIQUE during
         * network partitions. A small random jitter is added after the lease
         * to prevent synchronized elections if the partition heals. */
        node->deadline_ms      = now_ms + MOSAIK_LEADERSHIP_LEASE_MS +
                                 (rng_next(node) % 50u);
        /* Lot 2C: Send ACK for valid heartbeat (follower -> leader).
         * Echo the heartbeat's sequence number (msg.arg) so the leader can
         * match the ACK to a specific heartbeat. */
        if (node->role == MOSAIK_ROLE_FOLLOWER) {
            emit(node, MOSAIK_MSG_ACK, msg.arg);
        }
        break;

    case MOSAIK_MSG_VOTE_REQ:
        /* Lot 2B: Stale vote request rejection. */
        if (msg.term < node->term) {
            g_last_reject_reason = MOSAIK_REJECT_STALE_TERM;
            node->stale_term_rejections++;
            return;
        }
        g_last_reject_reason = MOSAIK_REJECT_NONE;
        if (node->voted_term == msg.term && node->voted_for != 0u &&
            node->voted_for != msg.src) {
            return; /* one vote per term - this is what forbids split-brain */
        }
        node->voted_for  = msg.src;
        node->voted_term = msg.term;
        node->deadline_ms = now_ms + election_timeout(node);
        emit(node, MOSAIK_MSG_VOTE_GRANT, msg.src);
        break;

    case MOSAIK_MSG_VOTE_GRANT:
        /* Lot 2B: Stale vote grant rejection. */
        if (msg.term < node->term) {
            g_last_reject_reason = MOSAIK_REJECT_STALE_TERM;
            node->stale_term_rejections++;
            return;
        }
        g_last_reject_reason = MOSAIK_REJECT_NONE;
        if (node->role != MOSAIK_ROLE_CANDIDATE) { return; }
        if (msg.term != node->term)               { return; }
        if (msg.arg  != node->id)                 { return; }
        node->vote_mask |= (uint8_t)(1u << (msg.src - 1u));
        if (popcount8(node->vote_mask) >= quorum(node)) {
            become_leader(node);
        }
        break;

    case MOSAIK_MSG_ACK:
        /* Lot 2C: ACK message handling. */
        if (msg.term < node->term) {
            g_last_reject_reason = MOSAIK_REJECT_STALE_TERM;
            node->stale_term_rejections++;
            return;
        }
        g_last_reject_reason = MOSAIK_REJECT_NONE;
        
        if (msg.src >= 4u) return;  /* source validation */
        
        if (msg.term == node->term) {
            /* Record inbound ACK as per-peer quorum evidence (0-indexed peer). */
            node->last_ack_seq[msg.src - 1u] = msg.arg;
            node->last_ack_term[msg.src - 1u] = msg.term;
            /* Do NOT directly update last_quorum_contact_ms here. */
            /* Per-peer evidence is recorded; quorum evaluation is separate. */
        }
        break;
    case MOSAIK_MSG_SAFE:
        /* A peer has latched SAFE. The cluster continues degraded. */
        if (node->state == MOSAIK_STATE_NOMINAL) {
            node->state = MOSAIK_STATE_DEGRADED;
        }
        break;

    default:
        break;
    }
}

void mosaik_tick(mosaik_node_t *node, uint32_t now_ms)
{
    node->now_ms = now_ms;

    if (node->state == MOSAIK_STATE_SAFE) {
        if ((now_ms - node->last_tx_ms) >= node->cfg.heartbeat_period_ms) {
            emit(node, MOSAIK_MSG_SAFE, node->safe_cause);
        }
        return;
    }

    if (node->role == MOSAIK_ROLE_LEADER) {
        /* Leadership lease check: if lease has expired, the leader loses
         * valid authority and steps down to follower. It will start a new
         * election after its election timeout. This ensures INV-LEADER-UNIQUE:
         * at most one node holds valid leadership authority at any time. */
        if (now_ms >= node->lease_expiry_ms) {
            become_follower(node, node->term);
        } else {
            if ((now_ms - node->last_tx_ms) >= node->cfg.heartbeat_period_ms) {
                emit(node, MOSAIK_MSG_HEARTBEAT, node->seq++);
            }
        }
        return;
    }

    if ((int32_t)(now_ms - node->deadline_ms) < 0) {
        return;
    }

    if (node->role == MOSAIK_ROLE_CANDIDATE) {
        uint32_t span;
        node->failed_elections++;
        if (node->failed_elections >= node->cfg.max_failed_elections) {
            enter_safe(node, MOSAIK_SAFE_NO_QUORUM, now_ms);
            return;
        }
        /* Lot 2D, C2-a: candidate retry backoff. A failed candidate does
         * not retry at the timeout instant; it waits a locally drawn random
         * backoff first, so two candidates that collided do not collide
         * again in lock-step. The wait is not another failed election.
         * Only state that belonged to the failed attempt is cleared: the
         * collected votes. Term, voted_for and voted_term are preserved, so
         * the one-vote-per-term rule still holds during the wait. The next
         * election advances the term through start_election() when the
         * backoff deadline expires on the follower path below. */
        span = node->cfg.candidate_retry_backoff_span_ms;
        if (span == 0u) {
            span = 1u;
        }
        node->role        = MOSAIK_ROLE_FOLLOWER;
        node->vote_mask   = 0u;
        node->deadline_ms = now_ms + (rng_next(node) % span);
        return;
    }
    start_election(node);
}

bool mosaik_has_valid_leadership_authority(const mosaik_node_t *node)
{
    if (node->role != MOSAIK_ROLE_LEADER) {
        return false;
    }
    return node->now_ms < node->lease_expiry_ms;
}
