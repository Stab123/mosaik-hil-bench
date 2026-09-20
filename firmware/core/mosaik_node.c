#include <stddef.h>
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

static uint8_t popcount8(uint8_t v)
{
    uint8_t c = 0u;
    while (v) { c = (uint8_t)(c + (v & 1u)); v = (uint8_t)(v >> 1); }
    return c;
}

/* Lot 5: quorum of an explicit membership mask: floor(|mask| / 2) + 1.
 * cfg.cluster_size no longer takes part. */
static uint8_t quorum_of(uint8_t mask)
{
    return (uint8_t)((popcount8(mask) / 2u) + 1u);
}

static bool in_mask(uint8_t mask, uint8_t id)
{
    return id != 0u && id <= MOSAIK_MAX_NODES && (mask & (uint8_t)(1u << (id - 1u))) != 0u;
}

static uint8_t bit_of(uint8_t id)
{
    return (uint8_t)(1u << (id - 1u));
}

/* A membership mask a node may commit to: non-empty subset of the three
 * nodes with at least two members. A one-node membership can never hold
 * authority in this model and is refused. */
static bool mask_valid(uint8_t mask)
{
    return mask != 0u && (mask & (uint8_t)~MOSAIK_MEMBERSHIP_ALL) == 0u && popcount8(mask) >= 2u;
}

static bool promise_pending(const mosaik_node_t *n)
{
    return n->accepted_epoch == (uint16_t)(n->committed_epoch + 1u);
}

/* Joint participation set. While a successor has been accepted but not
 * committed, every node of C(n) OR C(n+1) takes part: a node needed by
 * either quorum must be able to vote and to acknowledge. The intersection
 * would be wrong here: for a transition such as {1,2} -> {1,3} it is the
 * single node 1, which would silence the very nodes both quorums need.
 * Safety comes from the quorum test below, not from this set. */
uint8_t mosaik_effective_members(const mosaik_node_t *n)
{
    uint8_t e = n->committed_mask;
    if (promise_pending(n)) {
        e = (uint8_t)(e | n->accepted_mask);
    }
    return e;
}

bool mosaik_is_member(const mosaik_node_t *n)
{
    return in_mask(n->committed_mask, n->id);
}

/* Joint rule: a candidate holds a quorum only of the committed membership
 * and, while a successor is accepted but not committed, of that successor
 * as well. Votes outside the committed membership never count. */
static bool election_quorum_reached(const mosaik_node_t *n)
{
    uint8_t v = n->vote_mask;
    bool ok = popcount8((uint8_t)(v & n->committed_mask)) >= quorum_of(n->committed_mask);
    if (promise_pending(n)) {
        ok = ok && popcount8((uint8_t)(v & n->accepted_mask)) >= quorum_of(n->accepted_mask);
    }
    return ok;
}

bool mosaik_has_quorum_ack_evidence(const mosaik_node_t *n)
{
    uint8_t fresh = bit_of(n->id), i;
    bool ok;
    if (n->role != MOSAIK_ROLE_LEADER) { return false; }
    for (i = 1u; i <= MOSAIK_MAX_NODES; i++) {
        if (i == n->id) { continue; }
        if (n->last_ack_term[i - 1u] == n->term &&
            (uint32_t)(n->now_ms - n->last_ack_rx_ms[i - 1u]) < (uint32_t)n->cfg.heartbeat_period_ms) {
            fresh |= bit_of(i);
        }
    }
    ok = popcount8((uint8_t)(fresh & n->committed_mask)) >= quorum_of(n->committed_mask);
    if (promise_pending(n)) {
        ok = ok && popcount8((uint8_t)(fresh & n->accepted_mask)) >= quorum_of(n->accepted_mask);
    }
    return ok;
}

/* Lot 5 host-model persistence: write what the node itself decided. */
static void config_persist(mosaik_node_t *n)
{
    if (n->store != NULL) {
        n->store->committed_epoch = n->committed_epoch;
        n->store->committed_mask  = n->committed_mask;
        n->store->accepted_epoch  = n->accepted_epoch;
        n->store->accepted_mask   = n->accepted_mask;
    }
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
    msg.cfg_stage = (uint8_t)MOSAIK_CFG_NONE;
    msg.cfg_mask  = 0u;

    mosaik_encode(&frame, &msg);
    n->last_tx_ms = n->now_ms;
    if (n->tx) {
        n->tx(&frame, n->user);
    }
}

/* Lot 5: CONFIG frames carry the membership epoch in the term field and
 * do not touch last_tx_ms, so they never shift heartbeat or SAFE timing. */
static void emit_config(mosaik_node_t *n, mosaik_cfg_stage_t stage, uint16_t epoch, uint8_t mask, uint8_t arg)
{
    mosaik_msg_t msg;
    mosaik_frame_t frame;

    msg.type      = MOSAIK_MSG_CONFIG;
    msg.src       = n->id;
    msg.version   = MOSAIK_PROTO_VERSION;
    msg.role      = MOSAIK_ROLE_FOLLOWER;
    msg.state     = MOSAIK_STATE_INIT;
    msg.term      = epoch;
    msg.arg       = arg;
    msg.cfg_stage = (uint8_t)stage;
    msg.cfg_mask  = mask;

    mosaik_encode(&frame, &msg);
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

/* Lot 3: true while at least one SAFE frame actually received from a peer is
 * still fresh. The window is derived from the heartbeat period (SAFE nodes
 * re-announce every heartbeat period): 3 periods. Wrap-safe unsigned elapsed
 * time. Uses only local state and the local clock. */
static bool peer_safe_evidence_fresh(const mosaik_node_t *n)
{
    uint32_t window = 3u * (uint32_t)n->cfg.heartbeat_period_ms;
    uint8_t i;
    for (i = 0u; i < 4u; i++) {
        if ((n->safe_evidence_mask & (uint8_t)(1u << i)) != 0u &&
            (uint32_t)(n->now_ms - n->last_safe_rx_ms[i]) < window) {
            return true;
        }
    }
    return false;
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
    /* Lot 4 (C1): starting an election is consensus activity, not FDIR
     * degradation. The state is left untouched: a healthy candidate may
     * remain INIT. DEGRADED arises only from received peer SAFE evidence
     * (Lot 3); NOMINAL arises only from leadership or an accepted
     * heartbeat. */
    emit(n, MOSAIK_MSG_VOTE_REQ, 0u);
}

static void become_leader(mosaik_node_t *n)
{
    n->role                     = MOSAIK_ROLE_LEADER;
    n->leader_id                = n->id;
    /* Lot 3: a leader stays DEGRADED while received peer SAFE evidence is
     * fresh. Authority is unaffected by DEGRADED. */
    n->state                    = peer_safe_evidence_fresh(n) ? MOSAIK_STATE_DEGRADED
                                                              : MOSAIK_STATE_NOMINAL;
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

    /* LOT 6A. A cold restart re-initialises the CAN controller, which
     * re-enters error-active, so UP is the correct boot value. */
    node->transport_status         = (uint8_t)MOSAIK_TRANSPORT_UP;
    node->transport_status_ms      = now_ms;
    node->transport_status_changes = 0u;
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
        node->last_safe_rx_ms[i] = 0u;
        node->last_ack_rx_ms[i] = 0u;
    }
    node->safe_evidence_mask = 0u;
    /* Lot 5: default configuration until a store is loaded. */
    node->committed_epoch = (uint16_t)MOSAIK_CONFIG_EPOCH_INITIAL;
    node->committed_mask  = (uint8_t)MOSAIK_MEMBERSHIP_ALL;
    node->accepted_epoch  = 0u;
    node->accepted_mask   = 0u;
    node->proposing       = false;
    node->proposal_epoch  = 0u;
    node->proposal_mask   = 0u;
    node->accept_set      = 0u;
    node->proposal_start_ms = now_ms;
    node->last_propose_tx_ms = now_ms;
    node->last_cfg_announce_ms = now_ms;
    node->store = NULL;
    node->nonmember_rejections = 0u;
    node->config_stale_rejections = 0u;
    node->config_future_rejections = 0u;
    node->config_conflict_rejections = 0u;
    node->config_duplicates = 0u;
    node->config_mismatch_observed = 0u;
    node->config_commits = 0u;
    node->tx = tx;
    node->user = user;
    node->deadline_ms = now_ms + election_timeout(node);
}

void mosaik_load_config_store(mosaik_node_t *node, mosaik_config_store_t *store)
{
    node->store = store;
    if (store == NULL) { return; }
    if (store->committed_epoch == 0u || !mask_valid(store->committed_mask)) {
        store->committed_epoch = (uint16_t)MOSAIK_CONFIG_EPOCH_INITIAL;
        store->committed_mask  = (uint8_t)MOSAIK_MEMBERSHIP_ALL;
        store->accepted_epoch  = 0u;
        store->accepted_mask   = 0u;
    }
    node->committed_epoch = store->committed_epoch;
    node->committed_mask  = store->committed_mask;
    node->accepted_epoch  = store->accepted_epoch;
    node->accepted_mask   = store->accepted_mask;
}

/* Lot 5: install a committed configuration. Only ever epoch + 1, only from
 * the transaction. A node that leaves the membership drops any candidacy
 * without touching its term or vote memory; a leader is never removed
 * because a proposal must include its proposer. */
static void config_commit(mosaik_node_t *n, uint16_t epoch, uint8_t mask)
{
    bool took_part = in_mask(mosaik_effective_members(n), n->id);

    n->committed_epoch = epoch;
    n->committed_mask  = mask;
    n->config_commits++;
    if (n->proposing && n->proposal_epoch == epoch) {
        n->proposing = false;
    }
    config_persist(n);

    if (!in_mask(mask, n->id)) {
        /* This node is no longer a voter. It drops any candidacy and any
         * leadership immediately: changing membership must never leave
         * authority behind in a configuration that excludes its holder
         * (INV-RECONFIG-AUTHORITY). Term and vote memory are untouched,
         * and the node is NOT forced into SAFE: exclusion is not FDIR. */
        if (n->role == MOSAIK_ROLE_LEADER) {
            n->lease_expiry_ms = n->now_ms;   /* authority invalid from now on */
        }
        if (n->role != MOSAIK_ROLE_FOLLOWER) {
            n->role = MOSAIK_ROLE_FOLLOWER;
        }
        n->vote_mask = 0u;
    } else if (!took_part) {
        /* Re-admitted. Its election deadline lapsed while it was passive;
         * rearm it locally so it waits for the cluster's heartbeat instead
         * of opening an election at a term it knows to be stale. */
        n->deadline_ms = n->now_ms + election_timeout(n);
    }
}

bool mosaik_request_reconfiguration(mosaik_node_t *n, uint8_t target_mask)
{
    uint16_t next = (uint16_t)(n->committed_epoch + 1u);
    if (n->state == MOSAIK_STATE_SAFE)                    { return false; }
    if (n->committed_epoch == 0xFFFFu)                    { return false; } /* no epoch wraparound */
    if (!mosaik_is_member(n))                             { return false; }
    if (!mosaik_has_valid_leadership_authority(n))        { return false; }
    if (n->proposing)                                     { return false; }
    if (!mask_valid(target_mask))                         { return false; }
    if (!in_mask(target_mask, n->id))                     { return false; }
    if (target_mask == n->committed_mask)                 { return false; }
    if (n->accepted_epoch == next && n->accepted_mask != target_mask) { return false; }
    n->proposing         = true;
    n->proposal_epoch    = next;
    n->proposal_mask     = target_mask;
    n->accept_set        = bit_of(n->id);
    n->accepted_epoch    = next;          /* the proposer's own binding */
    n->accepted_mask     = target_mask;
    n->proposal_start_ms = n->now_ms;
    n->last_propose_tx_ms = n->now_ms;
    config_persist(n);
    emit_config(n, MOSAIK_CFG_PROPOSE, next, target_mask, 0u);
    return true;
}

/* Lot 5: CONFIG frame handling. Never touches term, role (except a removed
 * candidate), vote memory, lease or election timing. Epochs are compared
 * explicitly; only epoch + 1 from a committed member can commit. */
static void handle_config(mosaik_node_t *n, const mosaik_msg_t *m)
{
    uint16_t next = (uint16_t)(n->committed_epoch + 1u);
    bool src_member = in_mask(n->committed_mask, m->src);

    switch ((mosaik_cfg_stage_t)m->cfg_stage) {

    case MOSAIK_CFG_PROPOSE:
        /* The proposer must belong to the configuration this node has
         * committed. A node that is itself a member additionally requires
         * the proposer to be the leader it currently follows; a node that
         * is not (one this configuration removed, or one being re-admitted)
         * has no meaningful leader and accepts from any member. */
        if (!src_member)                             { n->nonmember_rejections++; return; }
        if (mosaik_is_member(n) && m->src != n->leader_id) { n->nonmember_rejections++; return; }
        if (m->term < next)                          { n->config_stale_rejections++; return; }
        if (m->term > next)                          { n->config_future_rejections++; return; }
        if (!mask_valid(m->cfg_mask) || !in_mask(m->cfg_mask, m->src)) { n->config_mismatch_observed++; return; }
        if (n->accepted_epoch == next) {
            if (n->accepted_mask != m->cfg_mask)     { n->config_conflict_rejections++; return; }
            n->config_duplicates++;                  /* idempotent: re-send the same acceptance */
        } else {
            n->accepted_epoch = next;
            n->accepted_mask  = m->cfg_mask;
            config_persist(n);
        }
        emit_config(n, MOSAIK_CFG_ACCEPT, next, m->cfg_mask, m->src);
        return;

    case MOSAIK_CFG_ACCEPT:
        if (!n->proposing)                           { return; }
        /* An acceptance counts when it comes from a node of either
         * configuration of this transaction. A node that the proposal ADDS
         * is not yet in the committed membership, yet its acceptance is
         * exactly what the new quorum needs; it can never contribute to the
         * old quorum, because the test below masks the set by each
         * membership separately. */
        if (!src_member && !in_mask(n->proposal_mask, m->src)) {
            n->nonmember_rejections++; return;
        }
        if (m->term != n->proposal_epoch || m->cfg_mask != n->proposal_mask || m->arg != n->id) {
            if (m->term < n->proposal_epoch) { n->config_stale_rejections++; }
            return;
        }
        if ((n->accept_set & bit_of(m->src)) != 0u)  { n->config_duplicates++; return; }
        n->accept_set |= bit_of(m->src);
        /* Authorisation of C(n+1) requires a quorum of C(n) AND a quorum of
         * C(n+1) among the acceptances actually received. A quorum of C(n)
         * alone is NOT sufficient: it lets the proposer install the new
         * configuration, and therefore the new lease rule, while the nodes
         * that must still be counted under C(n) have promised nothing. The
         * conjunction makes every acceptor of a committed configuration a
         * member of both quorums, so no later leader can satisfy one
         * configuration without the other (INV-RECONFIG-TRANSITION). */
        if (popcount8((uint8_t)(n->accept_set & n->committed_mask)) >= quorum_of(n->committed_mask) &&
            popcount8((uint8_t)(n->accept_set & n->proposal_mask)) >= quorum_of(n->proposal_mask)) {
            uint16_t e = n->proposal_epoch;
            uint8_t  k = n->proposal_mask;
            config_commit(n, e, k);
            emit_config(n, MOSAIK_CFG_COMMIT, e, k, 0u);
        }
        return;

    case MOSAIK_CFG_COMMIT:
    case MOSAIK_CFG_ANNOUNCE:
        if (m->term == n->committed_epoch) {
            if (m->cfg_mask == n->committed_mask) { n->config_duplicates++; }
            else                                  { n->config_mismatch_observed++; }
            return;
        }
        if (m->term < n->committed_epoch)            { n->config_stale_rejections++; return; }
        if (m->term > next)                          { n->config_future_rejections++; return; }
        /* epoch + 1: admissible only from a member of the configuration
         * being superseded, for a valid mask that keeps the sender. */
        if (!src_member)                             { n->nonmember_rejections++; return; }
        if (!mask_valid(m->cfg_mask) || !in_mask(m->cfg_mask, m->src)) { n->config_mismatch_observed++; return; }
        config_commit(n, m->term, m->cfg_mask);
        return;

    default:
        return;
    }
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

    /* Lot 5: membership traffic is handled on its own, before any term
     * processing: its epoch is a membership epoch, not a leadership term. */
    if (msg.type == MOSAIK_MSG_CONFIG) {
        handle_config(node, &msg);
        return;
    }

    /* Lot 5 membership gate: consensus frames (HEARTBEAT, VOTE_REQ,
     * VOTE_GRANT, ACK) from a source outside this node's committed
     * membership, or received by a node that is itself no longer a member,
     * carry no consensus evidence: no term adoption, no vote, no lease
     * evidence, no leader evidence. SAFE frames remain FDIR evidence
     * regardless of membership (Lot 4 discipline: FDIR is not consensus). */
    if (msg.type != MOSAIK_MSG_SAFE &&
        (!in_mask(node->committed_mask, msg.src) || !mosaik_is_member(node))) {
        node->nonmember_rejections++;
        return;
    }

    /* A higher term always wins: step down and adopt it.
     * Lot 4 (C2): only for message types whose term is consensus evidence
     * of a leadership epoch (HEARTBEAT, VOTE_REQ, VOTE_GRANT, ACK). A SAFE
     * announcement is FDIR evidence only: its term is not adopted, so a
     * latched peer can never step a valid leader down or force an
     * election. The SAFE handler below records the evidence. */
    if (msg.type != MOSAIK_MSG_SAFE && msg.term > node->term) {
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
        /* Lot 3: an accepted heartbeat does not clear DEGRADED while received
         * peer SAFE evidence is fresh (REQ-SAFE-0004). */
        node->state            = peer_safe_evidence_fresh(node) ? MOSAIK_STATE_DEGRADED
                                                                : MOSAIK_STATE_NOMINAL;
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
        /* Lot 5: grant only within the effective membership (joint rule
         * while a successor is accepted but not committed). */
        if (!in_mask(mosaik_effective_members(node), msg.src) ||
            !in_mask(mosaik_effective_members(node), node->id)) {
            node->nonmember_rejections++;
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
        if (!in_mask(mosaik_effective_members(node), msg.src)) {
            node->nonmember_rejections++;         /* Lot 5: a non-member's vote never counts */
            return;
        }
        node->vote_mask |= (uint8_t)(1u << (msg.src - 1u));
        if (election_quorum_reached(node)) {
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
        if (!in_mask(mosaik_effective_members(node), msg.src)) {
            node->nonmember_rejections++;         /* Lot 5: no lease evidence from a non-member */
            return;
        }

        if (msg.term == node->term) {
            /* Record inbound ACK as per-peer quorum evidence (0-indexed peer). */
            node->last_ack_seq[msg.src - 1u] = msg.arg;
            node->last_ack_term[msg.src - 1u] = msg.term;
            node->last_ack_rx_ms[msg.src - 1u] = now_ms;
            /* Do NOT directly update last_quorum_contact_ms here. */
            /* Per-peer evidence is recorded; quorum evaluation is separate. */
        }
        break;
    case MOSAIK_MSG_SAFE:
        /* A peer has latched SAFE. The cluster continues degraded.
         * Lot 3: record per-peer SAFE evidence from the received frame only.
         * Own frames were already discarded above; the source id was
         * validated by the decoder, and is range-checked again here before
         * indexing. SAFE is never propagated: no role, term, vote, authority
         * or lease change happens here. */
        if (msg.src != 0u && msg.src <= MOSAIK_MAX_NODES && msg.src != node->id) {
            node->last_safe_rx_ms[msg.src - 1u] = now_ms;
            node->safe_evidence_mask |= (uint8_t)(1u << (msg.src - 1u));
        }
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

    /* Lot 3: DEGRADED exit. Return to NOMINAL only when no received peer SAFE
     * evidence is fresh AND legitimate leader evidence exists locally: a
     * leader with valid authority, or a follower that knows its leader and
     * whose heartbeat deadline still lies in the future. A candidate, or a
     * follower in retry backoff (leader_id == 0), never clears DEGRADED here.
     * This touches state only; lease, election and backoff timing below are
     * unchanged. */
    if (node->state == MOSAIK_STATE_DEGRADED && !peer_safe_evidence_fresh(node)) {
        bool leader_evidence = false;
        if (node->role == MOSAIK_ROLE_LEADER) {
            leader_evidence = mosaik_has_valid_leadership_authority(node);
        } else if (node->role == MOSAIK_ROLE_FOLLOWER) {
            leader_evidence = (node->leader_id != 0u) &&
                              ((int32_t)(now_ms - node->deadline_ms) < 0);
        }
        if (leader_evidence) {
            node->state = MOSAIK_STATE_NOMINAL;
        }
    }

    /* Lot 5: periodic announcement of the committed configuration (every
     * five heartbeat periods). It repairs a COMMIT that did not reach a
     * member and lets peers observe an inconsistent configuration. It
     * carries no consensus evidence and never shifts heartbeat timing. */
    if ((uint32_t)(now_ms - node->last_cfg_announce_ms) >= 5u * (uint32_t)node->cfg.heartbeat_period_ms) {
        node->last_cfg_announce_ms = now_ms;
        emit_config(node, MOSAIK_CFG_ANNOUNCE, node->committed_epoch, node->committed_mask, 0u);
    }

    /* Lot 5: proposer maintenance. A proposal is re-sent every heartbeat
     * period while pending and abandoned after ten periods, or as soon as
     * the proposer no longer holds valid authority. The acceptance binding
     * is kept: the same successor may be proposed again for this epoch. */
    if (node->proposing) {
        if (!mosaik_has_valid_leadership_authority(node) ||
            (uint32_t)(now_ms - node->proposal_start_ms) >= 10u * (uint32_t)node->cfg.heartbeat_period_ms) {
            node->proposing = false;
        } else if ((uint32_t)(now_ms - node->last_propose_tx_ms) >= (uint32_t)node->cfg.heartbeat_period_ms) {
            node->last_propose_tx_ms = now_ms;
            emit_config(node, MOSAIK_CFG_PROPOSE, node->proposal_epoch, node->proposal_mask, 0u);
        }
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

    /* Lot 5: taking part in consensus and standing for election are two
     * different things. A node takes part (votes, acknowledges, is counted)
     * whenever it belongs to EITHER configuration of a pending transaction,
     * which is what mosaik_effective_members() returns. It may only STAND
     * FOR ELECTION while it belongs to every configuration that might
     * currently be in force: its committed membership and, while an
     * acceptance is pending, the successor it has promised. A node the
     * successor removes could otherwise spend its whole election budget on
     * attempts that the pending commit would immediately undo, and latch
     * SAFE for want of a quorum it was never entitled to assemble.
     * It is not forced into SAFE: exclusion is not FDIR. It keeps its
     * timers, term and vote memory, and stands again once a configuration
     * that includes it is in force. */
    if (!mosaik_is_member(node)) {
        return;
    }
    if (promise_pending(node) && !in_mask(node->accepted_mask, node->id)) {
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

/* LOT 6A: local transport status reporting.
 *
 * RED BASELINE. These three functions are the complete LOT 6A transport
 * interface and they are deliberately INERT: the status is recorded and can
 * be read back, and NOTHING in emit(), emit_config(), mosaik_on_rx(),
 * mosaik_tick() or mosaik_has_valid_leadership_authority() consults it.
 * A bus-off node therefore still hands frames to its transmit callback and
 * still reports valid leadership authority until its lease expires. That is
 * the defect TC-092, TC-093, TC-096, TC-097 and TC-098 capture. Correcting
 * it is LOT 6A GREEN and is NOT part of this commit. */
void mosaik_set_transport_status(mosaik_node_t *node, uint32_t now_ms,
                                 mosaik_transport_status_t status)
{
    if (node == NULL) {
        return;
    }
    node->now_ms = now_ms;
    if (node->transport_status != (uint8_t)status) {
        node->transport_status_changes++;
    }
    node->transport_status    = (uint8_t)status;
    node->transport_status_ms = now_ms;
}

mosaik_transport_status_t mosaik_get_transport_status(const mosaik_node_t *node)
{
    return (mosaik_transport_status_t)node->transport_status;
}

bool mosaik_transport_can_transmit(const mosaik_node_t *node)
{
    return node->transport_status != (uint8_t)MOSAIK_TRANSPORT_BUS_OFF &&
           node->transport_status != (uint8_t)MOSAIK_TRANSPORT_RECOVERING;
}

bool mosaik_has_valid_leadership_authority(const mosaik_node_t *node)
{
    if (node->role != MOSAIK_ROLE_LEADER) {
        return false;
    }
    return node->now_ms < node->lease_expiry_ms;
}
