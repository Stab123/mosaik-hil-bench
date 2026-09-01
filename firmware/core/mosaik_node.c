#include "mosaik_node.h"

void mosaik_config_default(mosaik_config_t *cfg)
{
    cfg->heartbeat_period_ms     = 100u;  /* MOSAIK-ADD-0001 */
    cfg->election_timeout_min_ms = 300u;  /* 3 missed heartbeats */
    cfg->election_timeout_max_ms = 500u;  /* 5 missed heartbeats */
    cfg->vote_timeout_ms         = 150u;
    cfg->cluster_size            = 3u;
    cfg->max_failed_elections    = 3u;
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

static void become_follower(mosaik_node_t *n, uint16_t term)
{
    n->role      = MOSAIK_ROLE_FOLLOWER;
    n->term      = term;
    n->voted_for = 0u;
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
    n->role             = MOSAIK_ROLE_LEADER;
    n->leader_id        = n->id;
    n->state            = MOSAIK_STATE_NOMINAL;
    n->failed_elections = 0u;
    n->became_leader_ms = n->now_ms;
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
        node->deadline_ms      = now_ms + election_timeout(node);
        break;

    case MOSAIK_MSG_VOTE_REQ:
        if (msg.term < node->term) {
            return;
        }
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
        if (node->role != MOSAIK_ROLE_CANDIDATE) { return; }
        if (msg.term != node->term)               { return; }
        if (msg.arg  != node->id)                 { return; }
        node->vote_mask |= (uint8_t)(1u << (msg.src - 1u));
        if (popcount8(node->vote_mask) >= quorum(node)) {
            become_leader(node);
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
        if ((now_ms - node->last_tx_ms) >= node->cfg.heartbeat_period_ms) {
            emit(node, MOSAIK_MSG_HEARTBEAT, node->seq++);
        }
        return;
    }

    if ((int32_t)(now_ms - node->deadline_ms) < 0) {
        return;
    }

    if (node->role == MOSAIK_ROLE_CANDIDATE) {
        node->failed_elections++;
        if (node->failed_elections >= node->cfg.max_failed_elections) {
            enter_safe(node, MOSAIK_SAFE_NO_QUORUM, now_ms);
            return;
        }
    }
    start_election(node);
}
