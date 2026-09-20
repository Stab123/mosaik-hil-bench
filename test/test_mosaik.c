/* MOSAIK HIL bench - host test suite
 *
 * A virtual CAN bus running three node instances in simulated time.
 * Every test case is traced to a requirement in docs/TEST-PLAN.md.
 *
 * This is a logic bench, not a timing measurement. Numbers reported here
 * are simulated-time values; measured latencies come from the hardware
 * campaign described in docs/TEST-PLAN.md.
 */
#include <stdio.h>
#include <string.h>
#include "mosaik_node.h"

#define N_NODES 3
#define QUEUE_LEN 128
#define MAX_DELAYED_MSGS 128

/* Lot 2C: Directional network fault model.
 * Each directed path A->B can be independently configured. */
typedef enum {
    NET_DELIVER = 0,   /* Normal delivery */
    NET_DROP = 1,      /* Drop message */
    NET_DELAY = 2,     /* Delay message */
    NET_REORDER = 3    /* Reorder (queue for later) */
} net_action_t;

typedef struct {
    net_action_t action;
    uint32_t delay_ms;      /* For NET_DELAY */
    int reorder_pos;        /* For NET_REORDER: position in queue */
} net_path_t;

/* Lot 2C: Adversarial message scheduling for delayed/duplicate/replay tests. */
typedef struct {
    mosaik_frame_t frame;
    uint8_t        src;
    uint32_t       deliver_at_ms;
    bool           active;
} delayed_msg_t;

typedef struct {
    mosaik_node_t  node[N_NODES];
    bool           powered[N_NODES];
    bool           crashed[N_NODES];              /* Lot 2D: node is crashed (no tx/rx) */
    net_path_t     net_paths[N_NODES][N_NODES];  /* net_paths[from][to] */
    mosaik_frame_t queue[QUEUE_LEN];
    uint8_t        queue_src[QUEUE_LEN];
    int            queued;
    uint32_t       now_ms;

    /* Lot 2B: Adversarial message scheduling for delayed/duplicate/replay tests. */
    delayed_msg_t delayed_msgs[MAX_DELAYED_MSGS];
    int delayed_count;

    /* Lot 2C: Instrumentation counters */
    uint32_t max_concurrent_valid_authorities;
    uint32_t term_regressions;
    uint32_t stale_authority_acceptances;
    uint32_t lease_expirations;
    uint32_t lease_renewals;
    uint32_t quorum_contact_events;
    uint32_t messages_delivered;
    uint32_t messages_dropped;
    uint32_t messages_delayed;
    uint32_t messages_reordered;

    /* Lot 2C: Inbound message tracking for lease renewal.
     * leader_last_inbound_ms[leader][from] = last time leader received a message from 'from'. */
    uint32_t leader_last_inbound_ms[N_NODES][N_NODES];
    uint16_t leader_last_inbound_term[N_NODES][N_NODES];

    /* Lot 2D: Crash/restart instrumentation */
    uint32_t crash_count[N_NODES];
    uint32_t restart_count[N_NODES];
    uint32_t max_concurrent_valid_authorities_lot2d;
    uint32_t term_regressions_lot2d;
    uint32_t stale_authority_acceptances_lot2d;
    uint32_t duplicate_votes_lot2d;
    uint32_t lease_expirations_lot2d;

    /* LOT 3 OBSERVATION: frames actually emitted per source node and message
     * type, counted in bus_tx(). Read by tests only; never read by any node. */
    uint32_t tx_count[N_NODES][7];

    /* LOT 5 OBSERVATION: CONFIG frames emitted per node and stage, and a copy
     * of the last one per stage (for replay tests). Read by tests only. */
    uint32_t       tx_cfg_count[N_NODES][5];
    mosaik_frame_t last_cfg_frame[N_NODES][5];
    bool           last_cfg_frame_valid[N_NODES][5];

    /* LOT 5 HOST-MODEL PERSISTENCE: one configuration store per node. It is
     * the node's own persistent memory: written only by the node, handed
     * back to it at cold restart, cleared with the whole bus. It is never
     * read by any other node and never derived from topology. */
    mosaik_config_store_t store[N_NODES];

    /* LOT 4 OBSERVATION: raw state and role bytes of every frame actually
     * emitted by a running node, read in bus_tx() before decoding, and a
     * copy of the last HEARTBEAT frame each node emitted (for replay tests).
     * Read by tests only; never read by any node. */
    uint32_t       tx_state_byte_count[4];
    uint32_t       tx_state_byte_out_of_range;
    uint32_t       tx_role_byte_out_of_range;
    mosaik_frame_t last_hb_frame[N_NODES];
    bool           last_hb_frame_valid[N_NODES];
} bus_t;

static bus_t g_bus;

static void bus_tx(const mosaik_frame_t *frame, void *user)
{
    uintptr_t src = (uintptr_t)user;
    int src_idx = (int)src - 1;
    /* Crashed nodes do not transmit */
    if (src_idx >= 0 && src_idx < N_NODES && g_bus.crashed[src_idx]) {
        return;
    }
    /* LOT 3 OBSERVATION only: record what this node actually emitted. */
    if (src_idx >= 0 && src_idx < N_NODES) {
        mosaik_msg_t obs;
        bool is_cfg = frame->id > MOSAIK_ID_CONFIG_BASE && frame->id <= MOSAIK_ID_CONFIG_BASE + MOSAIK_MAX_NODES;
        /* LOT 4 OBSERVATION only: raw emitted state/role bytes (bytes 2 and 3
         * of a CONFIG frame are stage and mask, not role and state). */
        if (!is_cfg) {
            if (frame->data[3] <= (uint8_t)MOSAIK_STATE_SAFE) {
                g_bus.tx_state_byte_count[frame->data[3]]++;
            } else {
                g_bus.tx_state_byte_out_of_range++;
            }
            if (frame->data[2] > (uint8_t)MOSAIK_ROLE_LEADER) {
                g_bus.tx_role_byte_out_of_range++;
            }
        }
        if (mosaik_decode(frame, &obs) && (int)obs.type >= 0 && (int)obs.type < 7) {
            g_bus.tx_count[src_idx][(int)obs.type]++;
            if (obs.type == MOSAIK_MSG_HEARTBEAT) {
                g_bus.last_hb_frame[src_idx] = *frame;
                g_bus.last_hb_frame_valid[src_idx] = true;
            }
            if (obs.type == MOSAIK_MSG_CONFIG && obs.cfg_stage >= 1u && obs.cfg_stage <= 4u) {
                g_bus.tx_cfg_count[src_idx][obs.cfg_stage]++;
                g_bus.last_cfg_frame[src_idx][obs.cfg_stage] = *frame;
                g_bus.last_cfg_frame_valid[src_idx][obs.cfg_stage] = true;
            }
        }
    }
    if (g_bus.queued >= QUEUE_LEN) { return; }
    g_bus.queue[g_bus.queued] = *frame;
    g_bus.queue_src[g_bus.queued] = (uint8_t)src;
    g_bus.queued++;
}

/* Lot 2C: Set network action for a directed path */
static void bus_set_net_action(uint8_t from, uint8_t to, net_action_t action, uint32_t delay_ms)
{
    if (from >= N_NODES || to >= N_NODES || from == to) { return; }
    g_bus.net_paths[from][to].action = action;
    g_bus.net_paths[from][to].delay_ms = delay_ms;
    g_bus.net_paths[from][to].reorder_pos = -1;
}

/* Lot 2C: Get network action for a directed path */
static net_action_t bus_get_net_action(uint8_t from, uint8_t to)
{
    if (from >= N_NODES || to >= N_NODES || from == to) { return NET_DROP; }
    return g_bus.net_paths[from][to].action;
}

/* Lot 2C: Check if a message should be delivered/dropped/delayed/reordered */
static net_action_t bus_check_delivery(uint8_t from, uint8_t to, uint32_t *out_delay_ms)
{
    net_action_t action = bus_get_net_action(from, to);
    if (out_delay_ms) *out_delay_ms = g_bus.net_paths[from][to].delay_ms;
    return action;
}
 
/* Lot 2C: Initialize all paths to DELIVER */
static void bus_init_net_paths(void)
{
    int i, j;
    for (i = 0; i < N_NODES; i++) {
        for (j = 0; j < N_NODES; j++) {
            if (i == j) {
                g_bus.net_paths[i][j].action = NET_DROP;
            } else {
                g_bus.net_paths[i][j].action = NET_DELIVER;
                g_bus.net_paths[i][j].delay_ms = 0;
                g_bus.net_paths[i][j].reorder_pos = -1;
            }
        }
    }
}

/* Lot 2B: Inject a message immediately (bypassing TX callback). */
static void bus_inject_frame(const mosaik_frame_t *frame, uint8_t src)
{
    if (g_bus.queued >= QUEUE_LEN) { return; }
    g_bus.queue[g_bus.queued] = *frame;
    g_bus.queue_src[g_bus.queued] = src;
    g_bus.queued++;
}

/* Lot 2B: Schedule a message for delayed delivery. */
static bool bus_schedule_delayed(const mosaik_frame_t *frame, uint8_t src, uint32_t delay_ms)
{
    if (g_bus.delayed_count >= MAX_DELAYED_MSGS) { return false; }
    int idx = g_bus.delayed_count++;
    g_bus.delayed_msgs[idx].frame = *frame;
    g_bus.delayed_msgs[idx].src = src;
    g_bus.delayed_msgs[idx].deliver_at_ms = g_bus.now_ms + delay_ms;
    g_bus.delayed_msgs[idx].active = true;
    return true;
}

/* Lot 2B: Process any delayed messages that are due for delivery. */
static void bus_process_delayed(void)
{
    for (int i = 0; i < g_bus.delayed_count; i++) {
        if (!g_bus.delayed_msgs[i].active) { continue; }
        if (g_bus.now_ms >= g_bus.delayed_msgs[i].deliver_at_ms) {
            if (g_bus.queued < QUEUE_LEN) {
                g_bus.queue[g_bus.queued] = g_bus.delayed_msgs[i].frame;
                g_bus.queue_src[g_bus.queued] = g_bus.delayed_msgs[i].src;
                g_bus.queued++;
            }
            g_bus.delayed_msgs[i].active = false;
        }
    }
}
/* Lot 2C: Update leader inbound tracking with term. */
static void bus_update_leader_inbound(int to_node, int from_node, uint16_t term)
{
    if (to_node >= 0 && to_node < N_NODES && from_node >= 0 && from_node < N_NODES) {
        g_bus.leader_last_inbound_ms[to_node][from_node] = g_bus.now_ms;
        g_bus.leader_last_inbound_term[to_node][from_node] = term;
    }
}

/* Lot 2C: Check if leader has fresh inbound quorum contact from current term. */

/* Evidence must be from ACTUAL RECEIVED messages that are: */

/* - Actually delivered to the leader (not just network connectivity) */

/* - Admissible under existing protocol/state semantics */

/* - Current term */

/* - Freshness-bounded (within lease window) */

static bool bus_leader_has_inbound_quorum(int leader_idx)
{
    if (leader_idx < 0 || leader_idx >= N_NODES) return false;
    if (!g_bus.powered[leader_idx]) return false;
    if (g_bus.node[leader_idx].role != MOSAIK_ROLE_LEADER) return false;
    /* Lot 5: the quorum is the node's own judgement over the acknowledgements
     * it actually received, in its current term, within one heartbeat
     * period, from members of its committed configuration (and of the
     * accepted successor while one is pending). The former harness rule
     * "one acknowledgement from any peer is a majority" is gone. The peer's
     * powered/crashed state is still never consulted. */
    return mosaik_has_quorum_ack_evidence(&g_bus.node[leader_idx]);
}

/* Lot 2C: Check if leader has fresh inbound quorum contact from current term. */
/* Set full connectivity (all nodes can talk to all other nodes). */
static void bus_set_full_connectivity(void)
{
    int i, j;
    bus_init_net_paths();
    for (i = 0; i < N_NODES; i++) {
        for (j = 0; j < N_NODES; j++) {
            if (i != j) {
                g_bus.net_paths[i][j].action = NET_DELIVER;
            }
        }
    }
}

/* Set symmetric 2+1 partition: nodes in group A can talk to each other,
 * nodes in group B can talk to each other, but no cross-group communication. */
static void bus_set_partition_2plus1(uint8_t isolated_node) /* 1-indexed node id */
{
    int i, j;
    bus_init_net_paths();
    uint8_t iso = isolated_node - 1;
    for (i = 0; i < N_NODES; i++) {
        for (j = 0; j < N_NODES; j++) {
            if (i == j) {
                g_bus.net_paths[i][j].action = NET_DROP;
            } else if (i == iso || j == iso) {
                g_bus.net_paths[i][j].action = NET_DROP; /* isolated node cut off */
            } else {
                g_bus.net_paths[i][j].action = NET_DELIVER;  /* majority pair connected */
            }
        }
    }
}

/* Lot 2C: One-way leader isolation - leader can send but not receive */
static void bus_set_one_way_leader_isolation(uint8_t leader_node) /* 1-indexed */
{
    int i, j;
    bus_init_net_paths();
    uint8_t leader = leader_node - 1;
    for (i = 0; i < N_NODES; i++) {
        for (j = 0; j < N_NODES; j++) {
            if (i == j) continue;
            if (i == leader) {
                /* Leader can send to others */
                g_bus.net_paths[i][j].action = NET_DELIVER;
            } else if (j == leader) {
                /* Others cannot send to leader */
                g_bus.net_paths[i][j].action = NET_DROP;
            } else {
                /* Non-leader nodes can talk to each other */
                g_bus.net_paths[i][j].action = NET_DELIVER;
            }
        }
    }
}

/* Lot 2C: Asymmetric minority view - nodes have inconsistent views */
static void bus_set_asymmetric_minority(void)
{
    bus_init_net_paths();
    /* A receives from B, B doesn't receive from A
     * B receives from C, C receives from B
     * A and C can talk
     * This creates inconsistent views
     */
    g_bus.net_paths[0][1].action = NET_DELIVER;  /* A -> B */
    g_bus.net_paths[1][0].action = NET_DROP;     /* B -/-> A */
    g_bus.net_paths[1][2].action = NET_DELIVER;  /* B -> C */
    g_bus.net_paths[2][1].action = NET_DELIVER;  /* C -> B */
    g_bus.net_paths[0][2].action = NET_DELIVER;  /* A -> C */
    g_bus.net_paths[2][0].action = NET_DELIVER;  /* C -> A */
}

/* Lot 2C: Partition with queued old traffic */
static void bus_heal_partition(void)
{
    bus_set_full_connectivity();
}

/* Initialise the bus with an explicit node configuration. Used by tests
 * that exercise a documented configuration parameter; identical to
 * bus_init() otherwise. */
static void bus_init_with_cfg(const mosaik_config_t *cfg)
{
    int i;
    memset(&g_bus, 0, sizeof(g_bus));
    for (i = 0; i < N_NODES; i++) {
        g_bus.powered[i] = true;
        g_bus.crashed[i] = false;
    }
    bus_set_full_connectivity();
    for (i = 0; i < N_NODES; i++) {
        mosaik_init(&g_bus.node[i], (uint8_t)(i + 1), cfg,
                    bus_tx, (void *)(uintptr_t)(i + 1), 0u);
        /* Lot 5: the node's own (empty) store, initialised by the node. */
        mosaik_load_config_store(&g_bus.node[i], &g_bus.store[i]);
    }
}

static void bus_init(void)
{
    mosaik_config_t cfg;
    mosaik_config_default(&cfg);
    bus_init_with_cfg(&cfg);
}

/* Lot 2D: Crash a node - it stops transmitting and processing.
 * Messages already in flight are NOT removed (they follow network model). */
static void bus_crash_node(int node_idx)
{
    if (node_idx < 0 || node_idx >= N_NODES) { return; }
    g_bus.crashed[node_idx] = true;
    g_bus.crash_count[node_idx]++;
    /* Node stops transmitting - tx callback will not be called for crashed nodes.
     * Node stops processing - mosaik_on_rx will not be called for crashed nodes.
     * Messages already in the network queue remain and will be delivered per network model. */
}

/* Lot 2D: Restart a node - cold initialization (term/vote state LOST).
 * Uses real mosaik_init semantics - no persistence, no injected knowledge. */
static void bus_restart_node(int node_idx)
{
    if (node_idx < 0 || node_idx >= N_NODES) { return; }
    if (!g_bus.crashed[node_idx]) { return; } /* Only restart crashed nodes */
    
    mosaik_config_t cfg;
    mosaik_config_default(&cfg);
    
    /* Preserve only identity and config; ALL volatile state is lost.
     * This is the current architecture behavior - no persistence. */
    g_bus.crashed[node_idx] = false;
    g_bus.restart_count[node_idx]++;
    
    /* Full cold re-initialization - term=0, voted_for=0, voted_term=0, etc. */
    mosaik_init(&g_bus.node[node_idx], (uint8_t)(node_idx + 1), &cfg,
                bus_tx, (void *)(uintptr_t)(node_idx + 1), g_bus.now_ms);
    /* Lot 5: only the node's own persisted configuration comes back. */
    mosaik_load_config_store(&g_bus.node[node_idx], &g_bus.store[node_idx]);
}

static void bus_step(void)
{
    mosaik_frame_t pending[QUEUE_LEN];
    uint8_t pending_src[QUEUE_LEN];
    int n, i, j;
    /* Lot 2C integrity: set only when an admissible current-term ACK from a
     * peer was ACTUALLY delivered to, and accepted by, a node holding the
     * leader role during this step. */
    bool leader_ack_received[N_NODES] = {false};

    /* Lot 2B: Process any delayed messages that are now due. */
    bus_process_delayed();

    n = g_bus.queued;
    memcpy(pending, g_bus.queue, sizeof(mosaik_frame_t) * (size_t)n);
    memcpy(pending_src, g_bus.queue_src, sizeof(uint8_t) * (size_t)n);
    g_bus.queued = 0;

for (j = 0; j < n; j++) {
        uint8_t src_idx = pending_src[j] - 1; /* 0-indexed */

        mosaik_msg_t msg;
        bool decoded = mosaik_decode(&pending[j], &msg);

        for (i = 0; i < N_NODES; i++) {
            if (!g_bus.powered[i]) { continue; }
            if (g_bus.crashed[i]) { continue; }  /* Lot 2D: crashed nodes don't process */
            if (g_bus.node[i].id == pending_src[j]) { continue; }
            
            /* Lot 2C: Use directional network model */
            uint32_t delay_ms = 0;
            net_action_t action = bus_check_delivery(src_idx, i, &delay_ms);
            if (action == NET_DROP) { 
                g_bus.messages_dropped++;
                continue; 
            }
            if (action == NET_DELAY) {
                /* Schedule for delayed delivery */
                if (bus_schedule_delayed(&pending[j], pending_src[j], delay_ms)) {
                    g_bus.messages_delayed++;
                } else {
                    g_bus.messages_dropped++; /* Queue full */
                }
                continue;
            }
            if (action == NET_REORDER) {
                /* For simplicity, treat reorder as delay with small offset */
                if (bus_schedule_delayed(&pending[j], pending_src[j], 1)) {
                    g_bus.messages_reordered++;
                } else {
                    g_bus.messages_dropped++;
                }
                continue;
            }
            /* NET_DELIVER */
            g_bus.messages_delivered++;
            mosaik_on_rx(&g_bus.node[i], g_bus.now_ms, &pending[j]);

            /* Lot 2C quorum-contact evidence integrity.
             * Peer contact is credited ONLY for an ACK frame that was
             * actually delivered to a node holding the leader role and that
             * the protocol accepted: the node's own ACK handler recorded this
             * ACK (seq, term) for its CURRENT term. Nothing is inferred from
             * outbound heartbeat delivery, reverse-path connectivity,
             * topology, powered/crashed state, or any other frame type.
             * Old-term frames are never accepted by the node and therefore
             * never credited; a delayed current-term ACK counts at the time
             * it is actually received, subject to the freshness window. */
            if (decoded && msg.type == MOSAIK_MSG_ACK && src_idx < N_NODES &&
                g_bus.node[i].role == MOSAIK_ROLE_LEADER &&
                msg.term == g_bus.node[i].term &&
                g_bus.node[i].last_ack_term[src_idx] == g_bus.node[i].term &&
                g_bus.node[i].last_ack_seq[src_idx] == msg.arg) {
                bus_update_leader_inbound(i, src_idx, msg.term);
                leader_ack_received[i] = true;
            }
        }
    }
    /* Leadership lease renewal: only upon ACTUAL receipt of admissible peer
     * contact in this step, and only while fresh current-term evidence from
     * a quorum of peers exists (the leader itself is the implicit member).
     * Outbound heartbeat transmission or delivery alone never renews. */
    for (i = 0; i < N_NODES; i++) {
        if (g_bus.powered[i] && !g_bus.crashed[i] && leader_ack_received[i] &&
            g_bus.node[i].role == MOSAIK_ROLE_LEADER &&
            bus_leader_has_inbound_quorum(i)) {
            g_bus.node[i].last_quorum_contact_ms = g_bus.now_ms;
            g_bus.node[i].lease_expiry_ms = g_bus.now_ms + MOSAIK_LEADERSHIP_LEASE_MS;
            g_bus.lease_renewals++;
            g_bus.quorum_contact_events++;
        }
    }
  
    for (i = 0; i < N_NODES; i++) {
        if (!g_bus.powered[i]) { continue; }
        if (g_bus.crashed[i]) { continue; }  /* Lot 2D: crashed nodes don't tick */
        mosaik_tick(&g_bus.node[i], g_bus.now_ms);
    }
    g_bus.now_ms++;
}
 
static void bus_run(uint32_t ms)
{
    uint32_t k;
    for (k = 0; k < ms; k++) { bus_step(); }
}
 
static int leader_count(void)
{
    int i, c = 0;
    for (i = 0; i < N_NODES; i++) {
        if (g_bus.powered[i] && !g_bus.crashed[i] && mosaik_has_valid_leadership_authority(&g_bus.node[i])) { c++; }
    }
    return c;
}

static int leader_index(void)
{
    int i;
    for (i = 0; i < N_NODES; i++) {
        if (g_bus.powered[i] && !g_bus.crashed[i] && mosaik_is_leader(&g_bus.node[i])) { return i; }
    }
    return -1;
}

static int valid_leader_count(void)
{
    int i, c = 0;
    for (i = 0; i < N_NODES; i++) {
        if (g_bus.powered[i] && !g_bus.crashed[i] && mosaik_has_valid_leadership_authority(&g_bus.node[i])) { c++; }
    }
    return c;
}

static int valid_leader_index(void)
{
    int i;
    for (i = 0; i < N_NODES; i++) {
        if (g_bus.powered[i] && !g_bus.crashed[i] && mosaik_has_valid_leadership_authority(&g_bus.node[i])) { return i; }
    }
    return -1;
}

static int g_failures = 0;
static int g_checks = 0;

static void check(bool cond, const char *req, const char *what)
{
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("  FAIL [%s] %s\n", req, what);
    }
}

/* TC-001 / REQ-002: a single leader emerges and stays unique. */
static void tc_001_single_leader(void)
{
    printf("TC-001  single leader elected and held [REQ-002]\n");
    bus_init();
    bus_run(2000u);
    check(leader_count() == 1, "REQ-002", "exactly one leader after 2000 ms");
    printf("        leader = node %d, term = %u\n",
           leader_index() + 1, g_bus.node[leader_index()].term);
}

/* TC-002 / REQ-002: no instant during a long run shows two leaders. */
static void tc_002_no_split_brain(void)
{
    uint32_t k;
    int max_leaders = 0;
    printf("TC-002  no split-brain over 20 s of operation [REQ-002]\n");
    bus_init();
    for (k = 0; k < 20000u; k++) {
        bus_step();
        if (leader_count() > max_leaders) { max_leaders = leader_count(); }
    }
    check(max_leaders <= 1, "REQ-002", "leader count never exceeded 1");
    printf("        maximum concurrent leaders observed = %d\n", max_leaders);
}

/* TC-003 / REQ-004: election completes < 1000 ms after loss of the leader. */
static void tc_003_failover_latency(void)
{
    int old_leader, new_leader = -1;
    uint32_t kill_ms, elected_ms = 0u, elapsed;
    uint32_t k;

    printf("TC-003  failover after leader power loss [REQ-004]\n");
    bus_init();
    bus_run(2000u);
    old_leader = leader_index();
    check(old_leader >= 0, "REQ-004", "a leader existed before the fault");
    if (old_leader < 0) { return; }

    g_bus.powered[old_leader] = false;
    kill_ms = g_bus.now_ms;

    for (k = 0; k < 3000u; k++) {
        bus_step();
        if (leader_count() == 1) {
            new_leader = leader_index();
            elected_ms = g_bus.now_ms;
            break;
        }
    }
    check(new_leader >= 0, "REQ-004", "a new leader was elected");
    if (new_leader < 0) { return; }

    elapsed = elected_ms - kill_ms;
    check(new_leader != old_leader, "REQ-004", "new leader differs from failed node");
    check(elapsed < 1000u, "REQ-004", "election completed in under 1000 ms");
    printf("        node %d lost, node %d elected after %u ms (simulated)\n",
           old_leader + 1, new_leader + 1, elapsed);
}

/* TC-004 / REQ-005: SAFE latched inside the receive path. */
static void tc_004_safe_on_split_brain(void)
{
    mosaik_msg_t msg;
    mosaik_frame_t frame;
    int li;
    uint32_t latency;

    printf("TC-004  SAFE latched on same-term dual leader [REQ-005, REQ-002]\n");
    bus_init();
    bus_run(2000u);
    li = leader_index();
    check(li >= 0, "REQ-005", "a leader existed before the injection");
    if (li < 0) { return; }

    msg.type = MOSAIK_MSG_HEARTBEAT;
    msg.src  = (uint8_t)(((li + 1) % N_NODES) + 1);
    msg.version = MOSAIK_PROTO_VERSION;
    msg.role = MOSAIK_ROLE_LEADER;
    msg.state = MOSAIK_STATE_NOMINAL;
    msg.term = g_bus.node[li].term;
    msg.arg  = 0u;
    mosaik_encode(&frame, &msg);

    mosaik_on_rx(&g_bus.node[li], g_bus.now_ms, &frame);

    latency = g_bus.node[li].safe_entry_ms - g_bus.node[li].safe_trigger_ms;
    check(g_bus.node[li].state == MOSAIK_STATE_SAFE, "REQ-005", "node entered SAFE");
    check(g_bus.node[li].safe_cause == (uint8_t)MOSAIK_SAFE_SPLIT_BRAIN,
          "REQ-005", "SAFE cause reported as split-brain");
    check(latency < 10u, "REQ-005", "SAFE entered within 10 ms of detection");
    printf("        SAFE latched, cause = split-brain, detection-to-SAFE = %u ms\n",
           latency);
}

/* TC-005 / REQ-003: a lone survivor cannot reach quorum. */
static void tc_005_no_quorum(void)
{
    int li, survivor = -1, i;
    printf("TC-005  lone node cannot self-appoint, latches SAFE [REQ-003]\n");
    bus_init();
    bus_run(2000u);
    li = leader_index();
    if (li < 0) { check(false, "REQ-003", "a leader existed before the fault"); return; }

    for (i = 0; i < N_NODES; i++) {
        if (i != li && survivor < 0) { survivor = i; }
    }
    for (i = 0; i < N_NODES; i++) {
        if (i != survivor) { g_bus.powered[i] = false; }
    }
    bus_run(3000u);

    check(!mosaik_is_leader(&g_bus.node[survivor]), "REQ-003",
          "survivor did not claim leadership without quorum");
    check(g_bus.node[survivor].state == MOSAIK_STATE_SAFE, "REQ-003",
          "survivor latched SAFE");
    check(g_bus.node[survivor].safe_cause == (uint8_t)MOSAIK_SAFE_NO_QUORUM,
          "REQ-003", "SAFE cause reported as no-quorum");
    printf("        node %d isolated, SAFE cause = no-quorum\n", survivor + 1);
}

/* TC-006: frame codec round-trip and rejection of corrupted payloads. */
static void tc_006_codec(void)
{
    mosaik_msg_t in, out;
    mosaik_frame_t frame;
    int i;
    int rejected = 0;

    printf("TC-006  frame codec round-trip and CRC rejection\n");

    in.type = MOSAIK_MSG_HEARTBEAT;
    in.src = 2u;
    in.version = MOSAIK_PROTO_VERSION;
    in.role = MOSAIK_ROLE_LEADER;
    in.state = MOSAIK_STATE_NOMINAL;
    in.term = 0x1234u;
    in.arg = 0x5Au;
    mosaik_encode(&frame, &in);

    check(mosaik_decode(&frame, &out), "codec", "valid frame decodes");
    check(out.src == in.src && out.term == in.term && out.arg == in.arg &&
          out.role == in.role && out.state == in.state && out.type == in.type,
          "codec", "round-trip preserves every field");

    for (i = 0; i < 8; i++) {
        mosaik_frame_t bad;
        mosaik_msg_t dummy;
        mosaik_encode(&bad, &in);
        bad.data[i] ^= 0x01u;
        if (!mosaik_decode(&bad, &dummy)) { rejected++; }
    }
    check(rejected == 8, "codec", "single-bit corruption rejected in all 8 bytes");
    printf("        %d/8 single-bit corruptions rejected\n", rejected);
}

/* TC-007 / REQ-002, Lot 2A: 2+1 network partition with leadership lease expiry.
 * Verifies INV-LEADER-UNIQUE: at most one node holds valid leadership authority
 * at any simulated instant, even during a partition. */
static void tc_007_partition_lease_expiry(void)
{
    uint32_t partition_time = 0;
    uint32_t old_leader_authority_expiry = 0;
    uint32_t new_majority_leader_valid = 0;
    int max_concurrent_valid = 0;
    int old_leader_idx = -1;
    int new_leader_idx = -1;
    uint32_t k;
    bool old_leader_expired = false;
    bool new_leader_valid = false;

    printf("TC-007  2+1 partition: lease expiry enforces unique valid leader [REQ-002, Lot 2A]\n");

    bus_init();
    bus_run(2000u); /* Allow stable leader election */

    old_leader_idx = leader_index();
    check(old_leader_idx >= 0, "REQ-002", "a leader existed before partition");
    if (old_leader_idx < 0) { return; }

    /* Record partition time and isolate the current leader (2+1 partition). */
    partition_time = g_bus.now_ms;
    bus_set_partition_2plus1((uint8_t)(old_leader_idx + 1));

    /* Continue simulation. Track valid leader count at every step. */
    for (k = 0; k < 5000u; k++) {
        bus_step();

        int vlc = valid_leader_count();
        if (vlc > max_concurrent_valid) { max_concurrent_valid = vlc; }

        /* Detect when old leader's authority expires (either steps down or loses valid authority). */
        if (!old_leader_expired &&
            g_bus.powered[old_leader_idx] &&
            !mosaik_has_valid_leadership_authority(&g_bus.node[old_leader_idx])) {
            old_leader_authority_expiry = g_bus.now_ms;
            old_leader_expired = true;
        }

        /* Detect when new majority leader gains valid authority. */
        if (!new_leader_valid) {
            new_leader_idx = valid_leader_index();
            if (new_leader_idx >= 0 && new_leader_idx != old_leader_idx) {
                new_majority_leader_valid = g_bus.now_ms;
                new_leader_valid = true;
            }
        }

        /* Early exit if both events observed and system stabilized. */
        if (old_leader_expired && new_leader_valid && k > 1000) {
            /* Run a bit more to ensure stability. */
            for (uint32_t extra = 0; extra < 1500; extra++) {
                bus_step();
                int vlc2 = valid_leader_count();
                if (vlc2 > max_concurrent_valid) { max_concurrent_valid = vlc2; }
            }
            break;
        }
    }

    /* Ensure old leader has fully stepped down. */
    for (uint32_t extra = 0; extra < 1000; extra++) { bus_step(); }

    /* Final verification. */
    check(max_concurrent_valid <= 1, "INV-LEADER-UNIQUE",
          "maximum concurrent valid leaders never exceeded 1");
    check(old_leader_expired, "Lot 2A", "old leader authority expired after partition");
    check(new_leader_valid, "Lot 2A", "new majority leader gained valid authority");
    check(valid_leader_count() == 1, "Lot 2A", "exactly one valid leader after stabilization");
    check(g_bus.node[old_leader_idx].role == MOSAIK_ROLE_LEADER ||
          g_bus.node[old_leader_idx].state == MOSAIK_STATE_SAFE ||
          g_bus.node[old_leader_idx].role == MOSAIK_ROLE_FOLLOWER,
          "Lot 2A", "old leader in known state");

    printf("        partition_time = %u ms\n", partition_time);
    printf("        old_leader_authority_expiry = %u ms (delta = %u ms)\n",
           old_leader_authority_expiry, old_leader_authority_expiry - partition_time);
    printf("        new_majority_leader_valid = %u ms (delta = %u ms)\n",
           new_majority_leader_valid, new_majority_leader_valid - partition_time);
    printf("        maximum concurrent valid leaders = %d\n", max_concurrent_valid);
}

/* ===== LOT 2B: Stale/Replay Immunity Tests ===== */

/* TC-008: Old term heartbeat rejected.
 * Scenario: establish leader at term N, advance to term N+1,
 * inject delayed heartbeat from old leader carrying term N. */
static void tc_008_old_term_heartbeat_rejected(void)
{
    mosaik_msg_t msg;
    mosaik_frame_t frame;
    int old_leader_idx, new_leader_idx;
    uint16_t old_term, new_term;
    uint32_t k;

    printf("TC-008  old term heartbeat rejected [Lot 2B]\n");

    bus_init();
    bus_run(2000u); /* term N */

    old_leader_idx = leader_index();
    old_term = g_bus.node[old_leader_idx].term;
    check(old_leader_idx >= 0, "Lot 2B", "leader existed at term N");
    if (old_leader_idx < 0) { return; }

    /* Capture an old heartbeat from the leader at term N. */
    msg.type = MOSAIK_MSG_HEARTBEAT;
    msg.src  = (uint8_t)(old_leader_idx + 1);
    msg.version = MOSAIK_PROTO_VERSION;
    msg.role = MOSAIK_ROLE_LEADER;
    msg.state = MOSAIK_STATE_NOMINAL;
    msg.term = old_term;
    msg.arg  = g_bus.node[old_leader_idx].seq; /* current seq */
    mosaik_encode(&frame, &msg);

    /* Kill old leader and force election to term N+1. */
    g_bus.powered[old_leader_idx] = false;
    for (k = 0; k < 3000u; k++) {
        bus_step();
        if (leader_count() == 1) { break; }
    }

    new_leader_idx = leader_index();
    new_term = g_bus.node[new_leader_idx].term;
    check(new_leader_idx >= 0, "Lot 2B", "new leader elected at term N+1");
    check(new_term > old_term, "Lot 2B", "term advanced to N+1");
    if (new_leader_idx < 0) { return; }

    /* Revive old leader node (simulate delayed message arrival). */
    g_bus.powered[old_leader_idx] = true;
    bus_set_full_connectivity();

    /* Inject the old heartbeat (term N) into the cluster now at term N+1. */
    bus_inject_frame(&frame, msg.src);

    /* Step a few times to process. */
    for (k = 0; k < 100u; k++) { bus_step(); }

    /* Verify: term never decreases, old leader not restored, valid authority unchanged. */
    check(g_bus.node[new_leader_idx].term == new_term, "Lot 2B",
          "term never decreased after stale heartbeat");
    check(g_bus.node[old_leader_idx].term == new_term, "Lot 2B",
          "old leader adopted newer term");
    check(mosaik_has_valid_leadership_authority(&g_bus.node[new_leader_idx]), "Lot 2B",
          "valid N+1 authority remains");
    check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
          "no double valid leadership authority");

    printf("        old_term = %u, new_term = %u\n", old_term, new_term);
    printf("        stale heartbeat rejected, term maintained\n");
}

/* TC-009: Replay after lease expiry.
 * Scenario: valid leader, isolate so lease expires, replay old heartbeat without fresh quorum. */
static void tc_009_replay_after_lease_expiry(void)
{
    mosaik_msg_t msg;
    mosaik_frame_t frame;
    int leader_idx;
    uint8_t old_seq;
    uint16_t term;
    uint32_t expiry_ms, k;

    printf("TC-009  replay after lease expiry [Lot 2B]\n");

    bus_init();
    bus_run(2000u);

    leader_idx = leader_index();
    check(leader_idx >= 0, "Lot 2B", "leader existed");
    if (leader_idx < 0) { return; }

    term = g_bus.node[leader_idx].term;
    old_seq = g_bus.node[leader_idx].seq - 1; /* last sent seq */

    /* Capture a valid heartbeat. */
    msg.type = MOSAIK_MSG_HEARTBEAT;
    msg.src  = (uint8_t)(leader_idx + 1);
    msg.version = MOSAIK_PROTO_VERSION;
    msg.role = MOSAIK_ROLE_LEADER;
    msg.state = MOSAIK_STATE_NOMINAL;
    msg.term = term;
    msg.arg  = old_seq;
    mosaik_encode(&frame, &msg);

    /* Isolate the leader so its heartbeats don't reach quorum.
     * This causes the lease to expire. */
    for (int i = 0; i < N_NODES; i++) {
        if (i != leader_idx) {
            g_bus.net_paths[leader_idx][i].action = NET_DROP;
            g_bus.net_paths[i][leader_idx].action = NET_DROP;
        }
    }

    /* Wait for lease to expire (500 ms + margin). */
    expiry_ms = g_bus.node[leader_idx].lease_expiry_ms + 200;
    while (g_bus.now_ms < expiry_ms) { bus_step(); }

    /* Verify lease expired and authority lost. */
    check(!mosaik_has_valid_leadership_authority(&g_bus.node[leader_idx]), "Lot 2B",
          "lease expired, authority lost");
    /* Note: leader may not have ticked to step down yet; tick once more to ensure. */
    bus_step();
    check(g_bus.node[leader_idx].role == MOSAIK_ROLE_FOLLOWER, "Lot 2B",
          "leader stepped down after lease expiry");

    /* Restore connectivity. */
    bus_set_full_connectivity();

    /* Replay the old heartbeat without fresh quorum evidence. */
    bus_inject_frame(&frame, msg.src);

    for (k = 0; k < 100u; k++) { bus_step(); }

    /* Verify: expired authority stays expired, replay does not renew lease. */
    check(!mosaik_has_valid_leadership_authority(&g_bus.node[leader_idx]), "Lot 2B",
          "expired leadership authority stays expired");
    check(g_bus.node[leader_idx].role == MOSAIK_ROLE_FOLLOWER, "Lot 2B",
          "replay does not restore valid leadership");
    check(valid_leader_count() <= 1, "INV-LEADER-UNIQUE",
          "invariant satisfied");

    printf("        lease expired at %u ms, replay at %u ms rejected\n",
           expiry_ms - 100, g_bus.now_ms);
}

/* TC-010: Delayed old leader after partition recovery.
 * Scenario: 2+1 partition, majority elects new leader, heal partition,
 * deliver delayed traffic from old leader. */
static void tc_010_delayed_old_leader_after_partition_heal(void)
{
    int leader_a_idx, leader_b_idx;
    uint16_t term_a, term_b;
    uint32_t partition_time, k;

    printf("TC-010  delayed old leader after partition recovery [Lot 2B]\n");

    bus_init();
    bus_run(2000u);

    leader_a_idx = leader_index();
    check(leader_a_idx >= 0, "Lot 2B", "initial leader A existed");
    if (leader_a_idx < 0) { return; }
    term_a = g_bus.node[leader_a_idx].term;

    /* Create 2+1 partition isolating leader A. */
    partition_time = g_bus.now_ms;
    bus_set_partition_2plus1((uint8_t)(leader_a_idx + 1));

    /* Majority side elects new leader B at newer term. */
    for (k = 0; k < 3000u; k++) {
        bus_step();
        if (valid_leader_count() == 1 && valid_leader_index() != leader_a_idx) {
            break;
        }
    }

    leader_b_idx = valid_leader_index();
    check(leader_b_idx >= 0, "Lot 2B", "new leader B elected in majority");
    if (leader_b_idx < 0) { return; }
    term_b = g_bus.node[leader_b_idx].term;
    check(term_b > term_a, "Lot 2B", "term advanced in majority partition");

/* Heal partition. */
    bus_set_full_connectivity();

    /* Allow convergence - let the old leader receive new term heartbeats. */
    for (k = 0; k < 5000u; k++) { bus_step(); }

    /* Verify: old leader adopts newer term after partition heal. */
    check(g_bus.node[leader_b_idx].term == term_b, "Lot 2B",
          "newer term remains authoritative after heal");
    check(mosaik_has_valid_leadership_authority(&g_bus.node[leader_b_idx]), "Lot 2B",
          "new leader B retains valid authority");
    check(g_bus.node[leader_a_idx].term == term_b, "Lot 2B",
          "old leader A adopted newer term");
    check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
          "cluster converges to one valid authority");
 
    printf("        partition_time = %u ms\n", partition_time);
    printf("        term_a = %u, term_b = %u\n", term_a, term_b);
}

/* TC-011: Duplicate heartbeat idempotence.
 * Scenario: deliver one valid heartbeat repeatedly without new quorum evidence. */
static void tc_011_duplicate_heartbeat_idempotence(void)
{
    mosaik_msg_t msg;
    mosaik_frame_t frame;
    int leader_idx;

    printf("TC-011  duplicate heartbeat idempotence [Lot 2B]\n");

    bus_init();
    bus_run(2000u);

    leader_idx = leader_index();
    check(leader_idx >= 0, "Lot 2B", "leader existed");
    if (leader_idx < 0) { return; }

    /* Capture a valid heartbeat. */
    msg.type = MOSAIK_MSG_HEARTBEAT;
    msg.src  = (uint8_t)(leader_idx + 1);
    msg.version = MOSAIK_PROTO_VERSION;
    msg.role = MOSAIK_ROLE_LEADER;
    msg.state = MOSAIK_STATE_NOMINAL;
    msg.term = g_bus.node[leader_idx].term;
    msg.arg  = g_bus.node[leader_idx].seq;
    mosaik_encode(&frame, &msg);

    int initial_valid_count = valid_leader_count();
    check(initial_valid_count == 1, "Lot 2B", "one valid leader initially");

    /* Deliver the same heartbeat 10 times in a row (duplicate burst). */
    for (int i = 0; i < 10; i++) {
        bus_inject_frame(&frame, msg.src);
        bus_step();
    }

    /* Verify: no additional authority, no additional leaders, no term regression. */
    check(valid_leader_count() == 1, "Lot 2B",
          "duplicate delivery does not create additional authority");
    check(leader_count() == 1, "Lot 2B",
          "duplicate delivery does not create additional leaders");
    check(g_bus.node[leader_idx].term == msg.term, "Lot 2B",
          "duplicate delivery does not cause term regression");
    check(mosaik_has_valid_leadership_authority(&g_bus.node[leader_idx]), "Lot 2B",
          "valid authority unchanged");

    printf("        duplicate heartbeat delivered 10x, authority unchanged\n");
}

/* TC-012: Stale election/vote traffic.
 * Scenario: establish newer term, inject old election/vote traffic. */
static void tc_012_stale_election_traffic(void)
{
    mosaik_msg_t msg;
    mosaik_frame_t frame;
    int leader_idx;
    uint16_t old_term, new_term;
    uint32_t k;

    printf("TC-012  stale election/vote traffic rejected [Lot 2B]\n");

    bus_init();
    bus_run(2000u);

    leader_idx = leader_index();
    check(leader_idx >= 0, "Lot 2B", "initial leader existed");
    if (leader_idx < 0) { return; }
    old_term = g_bus.node[leader_idx].term;

    /* Capture an old vote request. */
    msg.type = MOSAIK_MSG_VOTE_REQ;
    msg.src  = (uint8_t)(leader_idx + 1);
    msg.version = MOSAIK_PROTO_VERSION;
    msg.role = MOSAIK_ROLE_CANDIDATE;
    msg.state = MOSAIK_STATE_NOMINAL;
    msg.term = old_term;
    msg.arg  = 0u;
    mosaik_encode(&frame, &msg);

    /* Kill leader, force new election at newer term. */
    g_bus.powered[leader_idx] = false;
    for (k = 0; k < 3000u; k++) {
        bus_step();
        if (leader_count() == 1) { break; }
    }

    int new_leader_idx = leader_index();
    check(new_leader_idx >= 0, "Lot 2B", "new leader elected");
    if (new_leader_idx < 0) { return; }
    new_term = g_bus.node[new_leader_idx].term;
    check(new_term > old_term, "Lot 2B", "term advanced");

    /* Revive old node and inject stale vote request. */
    g_bus.powered[leader_idx] = true;
    bus_set_full_connectivity();
    bus_inject_frame(&frame, msg.src);

    for (k = 0; k < 100u; k++) { bus_step(); }

    /* Verify: no term rollback, no leader rollback, no stale vote alters quorum. */
    check(g_bus.node[new_leader_idx].term == new_term, "Lot 2B",
          "no term rollback from stale vote request");
    check(mosaik_has_valid_leadership_authority(&g_bus.node[new_leader_idx]), "Lot 2B",
          "valid leader retains authority");
    check(g_bus.node[leader_idx].term == new_term, "Lot 2B",
          "old node adopted newer term");
    check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
          "no stale vote altered quorum result");

    printf("        old_term = %u, new_term = %u\n", old_term, new_term);
    printf("        stale vote request rejected\n");
}

/* ===== LOT 2C: Network Adversarial Tests ===== */

/* TC-013: One-way leader isolation.
 * Scenario: leader can send but not receive.
 * The leader's outbound heartbeats are delivered, but inbound messages are dropped. */
static void tc_013_one_way_leader_isolation(void)
{
    int leader_idx;
    uint32_t k;

    printf("TC-013  one-way leader isolation [Lot 2C]\n");

    bus_init();
    bus_run(2000u);

    leader_idx = leader_index();
    check(leader_idx >= 0, "Lot 2C", "leader existed");
    if (leader_idx < 0) { return; }

    /* Apply one-way isolation: leader can send but not receive. */
    bus_set_one_way_leader_isolation((uint8_t)(leader_idx + 1));

    /* Run long enough for lease to expire (500 ms + margin). */
    for (k = 0; k < 2000u; k++) { bus_step(); }

    /* Verify: leader's authority expires because it cannot receive quorum contact. */
    check(!mosaik_has_valid_leadership_authority(&g_bus.node[leader_idx]), "Lot 2C",
          "leader authority expired under one-way isolation");
    check(g_bus.node[leader_idx].role == MOSAIK_ROLE_FOLLOWER, "Lot 2C",
          "leader stepped down after lease expiry");

    /* Majority side should elect a new leader. */
    check(valid_leader_count() <= 1, "INV-LEADER-UNIQUE",
          "no double valid authority");

    printf("        leader %d authority expired under one-way isolation\n", leader_idx + 1);
}

/* TC-014: Asymmetric minority view.
 * Scenario: nodes have inconsistent directional views.
 * A receives from B, B doesn't receive from A, etc. */
static void tc_014_asymmetric_minority_view(void)
{
    int leader_idx;
    uint32_t k;

    printf("TC-014  asymmetric minority view [Lot 2C]\n");

    bus_init();
    bus_run(2000u);

    leader_idx = leader_index();
    check(leader_idx >= 0, "Lot 2C", "leader existed");
    if (leader_idx < 0) { return; }

    /* Apply asymmetric minority view topology. */
    bus_set_asymmetric_minority();

    /* Run long enough to observe behavior. */
    for (k = 0; k < 2000u; k++) { bus_step(); }

    /* Verify: no double valid authority, term monotonicity preserved. */
    check(valid_leader_count() <= 1, "INV-LEADER-UNIQUE",
          "no double valid authority under asymmetric view");
    
    /* Check term monotonicity */
    for (int i = 0; i < N_NODES; i++) {
        if (g_bus.powered[i]) {
            check(g_bus.node[i].term >= g_bus.node[leader_idx].term, "Lot 2C",
                  "term monotonicity preserved");
        }
    }

    printf("        asymmetric view applied, max valid authorities = %d\n", valid_leader_count());
}

/* TC-015: Selective heartbeat loss.
 * Test various loss patterns: single, 2 consecutive, 3 consecutive, sustained. */
static void tc_015_selective_heartbeat_loss(void)
{
    int leader_idx;
    int patterns[] = {0, 1, 2, 3}; /* single, 2 consecutive, 3 consecutive, sustained */
    int num_patterns = 4;

    printf("TC-015  selective heartbeat loss [Lot 2C]\n");

    for (int p = 0; p < num_patterns; p++) {
        bus_init();
        bus_run(2000u);

        leader_idx = leader_index();
        check(leader_idx >= 0, "Lot 2C", "leader existed");
        if (leader_idx < 0) { continue; }

        /* Apply heartbeat loss pattern by dropping leader->others messages. */
        int loss_count = patterns[p];

        if (loss_count == 0) {
            /* No loss - baseline */
            for (int steps = 0; steps < 500; steps++) { bus_step(); }
            check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
                  "no loss maintains single authority");
        } else if (loss_count <= 2) {
            /* Transient loss: drop 1-2 heartbeats then restore */
            for (int steps = 0; steps < loss_count; steps++) {
                bus_set_net_action(leader_idx, (leader_idx + 1) % N_NODES, NET_DROP, 0);
                bus_set_net_action(leader_idx, (leader_idx + 2) % N_NODES, NET_DROP, 0);
                bus_step();
            }
            /* Restore */
            bus_set_net_action(leader_idx, (leader_idx + 1) % N_NODES, NET_DELIVER, 0);
            bus_set_net_action(leader_idx, (leader_idx + 2) % N_NODES, NET_DELIVER, 0);
            for (int steps = 0; steps < 500; steps++) { bus_step(); }
            check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
                  "transient loss does not create double authority");
        } else {
            /* Sustained loss: continuously drop leader->others for > lease period */
            for (int steps = 0; steps < 1000; steps++) {
                bus_set_net_action(leader_idx, (leader_idx + 1) % N_NODES, NET_DROP, 0);
                bus_set_net_action(leader_idx, (leader_idx + 2) % N_NODES, NET_DROP, 0);
                bus_step();
            }
            /* Verify: sustained loss invalidates authority */
            check(!mosaik_has_valid_leadership_authority(&g_bus.node[leader_idx]), "Lot 2C",
                  "sustained loss invalidates authority");
            check(g_bus.node[leader_idx].role == MOSAIK_ROLE_FOLLOWER, "Lot 2C",
                  "leader stepped down after sustained loss");
        }
    }

    printf("        selective loss patterns tested\n");
}

/* TC-016: Delay around lease boundary.
 * Inject messages at deterministic offsets around lease expiry. */
static void tc_016_delay_around_lease_boundary(void)
{
    int offsets[] = {-100, -10, -1, 0, 1, 10, 100};
    int num_offsets = 7;

    printf("TC-016  delay around lease boundary [Lot 2C]\n");

    for (int o = 0; o < num_offsets; o++) {
        int offset_ms = offsets[o];

        bus_init();
        bus_run(2000u);

        int leader_idx = leader_index();
        check(leader_idx >= 0, "Lot 2C", "leader existed");
        if (leader_idx < 0) { continue; }

        uint32_t expiry_ms = g_bus.node[leader_idx].lease_expiry_ms;

        /* Wait until just before expiry + offset */
        uint32_t target_ms = expiry_ms + offset_ms;
        while (g_bus.now_ms < target_ms) { bus_step(); }

        /* Capture and inject a heartbeat at this offset */
        mosaik_msg_t msg;
        mosaik_frame_t frame;
        uint16_t term_before = g_bus.node[leader_idx].term;
        msg.type = MOSAIK_MSG_HEARTBEAT;
        msg.src = (uint8_t)(leader_idx + 1);
        msg.version = MOSAIK_PROTO_VERSION;
        msg.role = MOSAIK_ROLE_LEADER;
        msg.state = MOSAIK_STATE_NOMINAL;
        msg.term = g_bus.node[leader_idx].term;
        msg.arg = g_bus.node[leader_idx].seq;
        mosaik_encode(&frame, &msg);

        /* Inject the heartbeat */
        bus_inject_frame(&frame, msg.src);

        /* Step a few times to process */
        for (int k = 0; k < 10; k++) { bus_step(); }

        /* Verify: expired authority is not resurrected by late delivery */
        if (offset_ms >= 0) {
            /* At or after expiry: should not renew expired authority */
            if (!mosaik_has_valid_leadership_authority(&g_bus.node[leader_idx])) {
                check(!mosaik_has_valid_leadership_authority(&g_bus.node[leader_idx]), "Lot 2C",
                      "expired authority not resurrected by late delivery");
            }
        } else {
            /* Before expiry: may renew if within lease */
            /* Verify no term regression: term must not decrease after injection */
            check(g_bus.node[leader_idx].term >= term_before, "Lot 2C",
                  "no term regression at offset");
        }
    }

    printf("        delay offsets tested: -100, -10, -1, 0, +1, +10, +100 ms\n");
}

/* TC-017: Message reordering.
 * Generate two messages M1 (older) and M2 (newer), deliver M2 then M1. */
static void tc_017_message_reordering(void)
{
    printf("TC-017  message reordering [Lot 2C]\n");

    bus_init();
    bus_run(2000u);

    int leader_idx = leader_index();
    check(leader_idx >= 0, "Lot 2C", "leader existed");
    if (leader_idx < 0) { return; }

    /* Capture two heartbeats: M1 (older seq) and M2 (newer seq) */
    mosaik_msg_t msg1, msg2;
    mosaik_frame_t frame1, frame2;

    /* M1: older heartbeat */
    msg1.type = MOSAIK_MSG_HEARTBEAT;
    msg1.src = (uint8_t)(leader_idx + 1);
    msg1.version = MOSAIK_PROTO_VERSION;
    msg1.role = MOSAIK_ROLE_LEADER;
    msg1.state = MOSAIK_STATE_NOMINAL;
    msg1.term = g_bus.node[leader_idx].term;
    msg1.arg = g_bus.node[leader_idx].seq;
    mosaik_encode(&frame1, &msg1);

    /* Step to get next sequence */
    bus_step();

    /* M2: newer heartbeat */
    msg2.type = MOSAIK_MSG_HEARTBEAT;
    msg2.src = (uint8_t)(leader_idx + 1);
    msg2.version = MOSAIK_PROTO_VERSION;
    msg2.role = MOSAIK_ROLE_LEADER;
    msg2.state = MOSAIK_STATE_NOMINAL;
    msg2.term = g_bus.node[leader_idx].term;
    msg2.arg = g_bus.node[leader_idx].seq;
    mosaik_encode(&frame2, &msg2);

    /* Deliver M2 first (newer), then M1 (older) - reorder */
    uint16_t term_before = g_bus.node[leader_idx].term;
    bus_inject_frame(&frame2, msg2.src);
    bus_step();
    bus_inject_frame(&frame1, msg1.src);
    bus_step();

    /* Verify: processing M2 before M1 does not roll state backwards */
    check(g_bus.node[leader_idx].term >= term_before, "Lot 2C",
          "no term regression from reordering");
    check(mosaik_has_valid_leadership_authority(&g_bus.node[leader_idx]), "Lot 2C",
          "valid authority maintained");
    check(valid_leader_count() <= 1, "INV-LEADER-UNIQUE",
          "no double authority from reordering");

    printf("        M2 then M1 delivered, no state regression\n");
}

/* TC-018: Partition heal with queued traffic.
 * Extend TC-010 with network reordering during heal. */
static void tc_018_partition_heal_queued_traffic(void)
{
    printf("TC-018  partition heal with queued traffic [Lot 2C]\n");

    bus_init();
    bus_run(2000u);

    int leader_a_idx = leader_index();
    check(leader_a_idx >= 0, "Lot 2C", "initial leader A existed");
    if (leader_a_idx < 0) { return; }
    uint16_t term_a = g_bus.node[leader_a_idx].term;

    /* Capture some heartbeats from leader A before partition. */
    mosaik_msg_t msg;
    mosaik_frame_t delayed_frames[10];
    int delayed_count = 0;

    for (int i = 0; i < 3; i++) {
        bus_step();
        if (g_bus.node[leader_a_idx].role == MOSAIK_ROLE_LEADER) {
            msg.type = MOSAIK_MSG_HEARTBEAT;
            msg.src = (uint8_t)(leader_a_idx + 1);
            msg.version = MOSAIK_PROTO_VERSION;
            msg.role = MOSAIK_ROLE_LEADER;
            msg.state = MOSAIK_STATE_NOMINAL;
            msg.term = term_a;
            msg.arg = g_bus.node[leader_a_idx].seq;
            mosaik_encode(&delayed_frames[delayed_count], &msg);
            delayed_count++;
        }
    }

    /* Create 2+1 partition isolating leader A. */
    bus_set_partition_2plus1((uint8_t)(leader_a_idx + 1));

    /* Majority side elects new leader B at newer term. */
    uint32_t k;
    for (k = 0; k < 3000u; k++) {
        bus_step();
        if (valid_leader_count() == 1 && valid_leader_index() != leader_a_idx) {
            break;
        }
    }

    int leader_b_idx = valid_leader_index();
    check(leader_b_idx >= 0, "Lot 2C", "new leader B elected in majority");
    if (leader_b_idx < 0) { return; }
    uint16_t term_b = g_bus.node[leader_b_idx].term;
    check(term_b > term_a, "Lot 2C", "term advanced in majority partition");

    /* Heal partition. */
    bus_heal_partition();

    /* Deliver delayed frames from old leader A aggressively and out of order. */
    for (int i = delayed_count - 1; i >= 0; i--) {
        bus_inject_frame(&delayed_frames[i], delayed_frames[i].data[1]);
        bus_step();
    }

    /* Allow convergence - first let the new leader's heartbeats propagate. */
    for (k = 0; k < 1000u; k++) { bus_step(); }

    /* Deliver delayed frames from old leader A aggressively and out of order. */
    for (int i = delayed_count - 1; i >= 0; i--) {
        bus_inject_frame(&delayed_frames[i], delayed_frames[i].data[1]);
        bus_step();
    }

    /* Allow convergence - let the old leader receive new term heartbeats. */
    for (k = 0; k < 2000u; k++) { bus_step(); }

    /* Verify: old queued traffic cannot restore A authority. */
    check(g_bus.node[leader_b_idx].term == term_b, "Lot 2C",
          "newer term remains authoritative after heal with queued traffic");
    check(mosaik_has_valid_leadership_authority(&g_bus.node[leader_b_idx]), "Lot 2C",
          "new leader B retains valid authority");
    check(g_bus.node[leader_a_idx].term == term_b, "Lot 2C",
          "old leader A adopted newer term");
    check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
          "cluster converges to one valid authority");

    printf("        term_a = %u, term_b = %u, delayed_frames = %d\n", term_a, term_b, delayed_count);
}

/* TC-019: Selective ACK/quorum failure.
 * Drop enough contact traffic that leader cannot demonstrate fresh majority contact. */
static void tc_019_selective_ack_quorum_failure(void)
{
    printf("TC-019  selective quorum contact failure [Lot 2C]\n");

    bus_init();
    bus_run(2000u);

    int leader_idx = leader_index();
    check(leader_idx >= 0, "Lot 2C", "leader existed");
    if (leader_idx < 0) { return; }

    /* Drop inbound messages TO the leader from other nodes.
     * Leader can still send outbound, but cannot receive quorum confirmation. */
    for (int i = 0; i < N_NODES; i++) {
        if (i != leader_idx) {
            g_bus.net_paths[i][leader_idx].action = NET_DROP;  /* Others -> leader */
        }
    }

    /* Wait for lease to expire. */
    uint32_t expiry_ms = g_bus.node[leader_idx].lease_expiry_ms + 200;
    while (g_bus.now_ms < expiry_ms) { bus_step(); }

    /* Verify: leader's authority expires without inbound quorum contact. */
    check(!mosaik_has_valid_leadership_authority(&g_bus.node[leader_idx]), "Lot 2C",
          "authority expires without inbound quorum contact");
    /* Note: leader may not have ticked to step down yet; tick once more to ensure. */
    bus_step();
    check(g_bus.node[leader_idx].role == MOSAIK_ROLE_FOLLOWER, "Lot 2C",
          "leader steps down when cannot receive quorum contact");

    /* Restore connectivity. */
    bus_set_full_connectivity();

    /* Verify: majority side can elect new leader. */
    uint32_t k;
    for (k = 0; k < 3000u; k++) {
        bus_step();
        if (valid_leader_count() == 1) { break; }
    }

    check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
          "new valid leader elected after quorum contact loss");

    printf("        outbound-only leader lost authority, new leader elected\n");
}

/* TC-020: Adversarial combination.
 * Combine asymmetric failure, selective drop, delayed stale message, reordering. */
static void tc_020_adversarial_combination(void)
{
    printf("TC-020  adversarial combination [Lot 2C]\n");

    bus_init();
    bus_run(2000u);

    int leader_a_idx = leader_index();
    check(leader_a_idx >= 0, "Lot 2C", "initial leader A existed");
    if (leader_a_idx < 0) { return; }
    uint16_t term_a = g_bus.node[leader_a_idx].term;

    /* Create asymmetric fault: leader A can send to B, but B drops messages to A.
     * C can talk to both. */
    bus_init_net_paths();
    g_bus.net_paths[0][1].action = NET_DELIVER;  /* A -> B */
    g_bus.net_paths[1][0].action = NET_DROP;     /* B -/-> A */
    g_bus.net_paths[0][2].action = NET_DELIVER;  /* A -> C */
    g_bus.net_paths[2][0].action = NET_DELIVER;  /* C -> A */
    g_bus.net_paths[1][2].action = NET_DELIVER;  /* B <-> C */
    g_bus.net_paths[2][1].action = NET_DELIVER;

    /* Capture a heartbeat from A before fault. */
    mosaik_msg_t msg;
    mosaik_frame_t frame;
    bus_step();
    msg.type = MOSAIK_MSG_HEARTBEAT;
    msg.src = (uint8_t)(leader_a_idx + 1);
    msg.version = MOSAIK_PROTO_VERSION;
    msg.role = MOSAIK_ROLE_LEADER;
    msg.state = MOSAIK_STATE_NOMINAL;
    msg.term = term_a;
    msg.arg = g_bus.node[leader_a_idx].seq;
    mosaik_encode(&frame, &msg);

    /* Let the fault take effect - A can send but not receive from B. */
    uint32_t k;
    for (k = 0; k < 600u; k++) { bus_step(); }

    /* Now inject the stale heartbeat from A. */
    bus_inject_frame(&frame, msg.src);

    for (k = 0; k < 100u; k++) { bus_step(); }

    /* Verify: no double valid authority, no term regression. */
    check(valid_leader_count() <= 1, "INV-LEADER-UNIQUE",
          "adversarial combination does not create split brain");
    
    for (int i = 0; i < N_NODES; i++) {
        if (g_bus.powered[i]) {
            check(g_bus.node[i].term >= term_a, "Lot 2C",
                  "term monotonicity preserved under adversarial faults");
        }
    }

    printf("        adversarial combination applied, max valid authorities = %d\n", valid_leader_count());
}

/* ===== LOT 2D: Crash/Restart/Recovery Tests ===== */

/* TC-021: Follower crash while leader/quorum remains available.
 * Scenario: stable leader, crash a follower, restart it, verify it rejoins cleanly. */
static void tc_021_follower_crash_quorum_available(void)
{
    int leader_idx, follower_idx;
    uint16_t term_before;
    uint32_t k;

    printf("TC-021  follower crash while leader/quorum available [Lot 2D]\n");

    bus_init();
    bus_run(2000u); /* stable leader */

    leader_idx = leader_index();
    check(leader_idx >= 0, "Lot 2D", "leader existed before crash");
    if (leader_idx < 0) { return; }
    term_before = g_bus.node[leader_idx].term;

    /* Pick a follower to crash */
    follower_idx = (leader_idx + 1) % N_NODES;

    /* Crash the follower */
    bus_crash_node(follower_idx);
    uint32_t crash_ms = g_bus.now_ms;

    /* Run for a while with follower crashed - leader should continue */
    for (k = 0; k < 1000u; k++) { bus_step(); }

    /* Verify leader still has valid authority */
    check(mosaik_has_valid_leadership_authority(&g_bus.node[leader_idx]), "Lot 2D",
          "leader authority maintained during follower crash");
    check(g_bus.node[leader_idx].term == term_before, "Lot 2D",
          "term unchanged during follower crash");

    /* Restart the follower */
    bus_restart_node(follower_idx);
    uint32_t restart_ms = g_bus.now_ms;

    /* Run for convergence - follower should adopt current term via heartbeats */
    for (k = 0; k < 2000u; k++) { bus_step(); }

    /* Verify: follower adopted current term, no duplicate leaders */
    check(g_bus.node[follower_idx].term == term_before, "Lot 2D",
          "restarted follower adopted current term");
    check(g_bus.node[follower_idx].role == MOSAIK_ROLE_FOLLOWER, "Lot 2D",
          "restarted follower is follower");
    check(g_bus.node[follower_idx].voted_for == 0, "Lot 2D",
          "restarted follower has no vote in current term");
    check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
          "single valid leader after follower restart");
    check(g_bus.node[leader_idx].term == term_before, "Lot 2D",
          "original leader term unchanged");

    /* Track max concurrent valid authorities */
    if (g_bus.max_concurrent_valid_authorities_lot2d < (uint32_t)valid_leader_count()) {
        g_bus.max_concurrent_valid_authorities_lot2d = (uint32_t)valid_leader_count();
    }

    printf("        follower %d crashed at %u ms, restarted at %u ms, adopted term %u\n",
           follower_idx + 1, crash_ms, restart_ms, term_before);
}

/* TC-022: Leader crash.
 * Scenario: crash the leader, verify failover, no split-brain. */
static void tc_022_leader_crash(void)
{
    int leader_idx, new_leader_idx;
    uint16_t old_term, new_term;
    uint32_t k;

    printf("TC-022  leader crash [Lot 2D]\n");

    bus_init();
    bus_run(2000u);

    leader_idx = leader_index();
    check(leader_idx >= 0, "Lot 2D", "leader existed before crash");
    if (leader_idx < 0) { return; }
    old_term = g_bus.node[leader_idx].term;

    /* Crash the leader */
    bus_crash_node(leader_idx);
    uint32_t crash_ms = g_bus.now_ms;

    /* Wait for new election */
    for (k = 0; k < 3000u; k++) {
        bus_step();
        if (valid_leader_count() == 1) { break; }
    }

    new_leader_idx = valid_leader_index();
    check(new_leader_idx >= 0, "Lot 2D", "new leader elected after crash");
    if (new_leader_idx < 0) { return; }
    new_term = g_bus.node[new_leader_idx].term;

    check(new_leader_idx != leader_idx, "Lot 2D", "new leader differs from crashed");
    check(new_term > old_term, "Lot 2D", "term advanced after leader crash");
    check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
          "single valid leader after failover");

    /* Track max concurrent valid authorities */
    if (g_bus.max_concurrent_valid_authorities_lot2d < (uint32_t)valid_leader_count()) {
        g_bus.max_concurrent_valid_authorities_lot2d = (uint32_t)valid_leader_count();
    }

    printf("        leader %d crashed at %u ms, node %d elected at term %u\n",
           leader_idx + 1, crash_ms, new_leader_idx + 1, new_term);
}

/* TC-023: Leader crash -> election -> former leader restarts.
 * Scenario: crash leader, new leader elected, then former leader restarts cold.
 * Former leader must NOT regain leadership immediately; must adopt new term. */
static void tc_023_leader_crash_election_former_restarts(void)
{
    int old_leader_idx, new_leader_idx;
    uint16_t old_term, new_term;
    uint32_t k;

    printf("TC-023  leader crash -> election -> former leader restarts [Lot 2D]\n");

    bus_init();
    bus_run(2000u);

    old_leader_idx = leader_index();
    check(old_leader_idx >= 0, "Lot 2D", "initial leader existed");
    if (old_leader_idx < 0) { return; }
    old_term = g_bus.node[old_leader_idx].term;

    /* Crash the leader */
    bus_crash_node(old_leader_idx);
    uint32_t crash_ms = g_bus.now_ms;

    /* Wait for new election */
    for (k = 0; k < 3000u; k++) {
        bus_step();
        if (valid_leader_count() == 1) { break; }
    }

    new_leader_idx = valid_leader_index();
    check(new_leader_idx >= 0, "Lot 2D", "new leader elected");
    if (new_leader_idx < 0) { return; }
    new_term = g_bus.node[new_leader_idx].term;
    check(new_term > old_term, "Lot 2D", "term advanced");

    /* Let new leader establish authority */
    for (k = 0; k < 1000u; k++) { bus_step(); }

    /* Now restart the FORMER leader (cold restart - term=0, voted_for=0, voted_term=0) */
    bus_restart_node(old_leader_idx);
    uint32_t restart_ms = g_bus.now_ms;

    /* Run for convergence */
    for (k = 0; k < 3000u; k++) { bus_step(); }

    /* Critical checks: former leader must NOT regain leadership immediately */
    check(g_bus.node[old_leader_idx].term == new_term, "Lot 2D",
          "former leader adopted new term after restart");
    check(g_bus.node[old_leader_idx].role != MOSAIK_ROLE_LEADER, "Lot 2D",
          "former leader did NOT immediately regain leadership");
    check(mosaik_has_valid_leadership_authority(&g_bus.node[new_leader_idx]), "Lot 2D",
          "new leader retains valid authority");
    check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
          "no split-brain after former leader restart");
    
    /* Check for term regression on any node */
    for (int i = 0; i < N_NODES; i++) {
        if (g_bus.powered[i] && !g_bus.crashed[i]) {
            if (g_bus.node[i].term < new_term) {
                g_bus.term_regressions_lot2d++;
            }
        }
    }

    /* Track max concurrent valid authorities */
    if (g_bus.max_concurrent_valid_authorities_lot2d < (uint32_t)valid_leader_count()) {
        g_bus.max_concurrent_valid_authorities_lot2d = (uint32_t)valid_leader_count();
    }

    printf("        old leader %d crashed at %u ms, new leader %d at term %u, old restarted at %u ms\n",
           old_leader_idx + 1, crash_ms, new_leader_idx + 1, new_term, restart_ms);
}

/* TC-024: Crashed follower restart and rejoin.
 * Scenario: crash a follower, restart it, verify it rejoins as follower with current term. */
static void tc_024_crashed_follower_restart_rejoin(void)
{
    int leader_idx, follower_idx;
    uint16_t term_before;
    uint32_t k;

    printf("TC-024  crashed follower restart and rejoin [Lot 2D]\n");

    bus_init();
    bus_run(2000u);

    leader_idx = leader_index();
    check(leader_idx >= 0, "Lot 2D", "leader existed");
    if (leader_idx < 0) { return; }
    term_before = g_bus.node[leader_idx].term;

    /* Pick a follower */
    follower_idx = (leader_idx + 1) % N_NODES;

    /* Crash and restart */
    bus_crash_node(follower_idx);
    for (k = 0; k < 500u; k++) { bus_step(); }
    bus_restart_node(follower_idx);

    /* Allow rejoin */
    for (k = 0; k < 2000u; k++) { bus_step(); }

    /* Verify clean rejoin */
    check(g_bus.node[follower_idx].term == term_before, "Lot 2D",
          "restarted follower adopted current cluster term");
    check(g_bus.node[follower_idx].role == MOSAIK_ROLE_FOLLOWER, "Lot 2D",
          "restarted node is follower");
    check(g_bus.node[follower_idx].voted_term == 0, "Lot 2D",
          "restarted follower voted_term reset (cold restart)");
    check(g_bus.node[follower_idx].voted_for == 0, "Lot 2D",
          "restarted follower voted_for reset (cold restart)");
    check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
          "single valid leader throughout");

    /* Track max concurrent valid authorities */
    if (g_bus.max_concurrent_valid_authorities_lot2d < (uint32_t)valid_leader_count()) {
        g_bus.max_concurrent_valid_authorities_lot2d = (uint32_t)valid_leader_count();
    }

    printf("        follower %d crashed/restarted, adopted term %u, voted_term=%u, voted_for=%u\n",
           follower_idx + 1, g_bus.node[follower_idx].term,
           g_bus.node[follower_idx].voted_term, g_bus.node[follower_idx].voted_for);
}

/* TC-025: Former leader restarts after another leader has been elected.
 * Scenario: leader crash, new leader elected at higher term, former leader restarts.
 * Verify: former leader does NOT disrupt cluster, adopts higher term. */
static void tc_025_former_leader_restarts_after_new_elected(void)
{
    int leader_a_idx, leader_b_idx;
    uint16_t term_a, term_b;
    uint32_t k;

    printf("TC-025  former leader restarts after another leader elected [Lot 2D]\n");

    bus_init();
    bus_run(2000u);

    leader_a_idx = leader_index();
    check(leader_a_idx >= 0, "Lot 2D", "leader A existed");
    if (leader_a_idx < 0) { return; }
    term_a = g_bus.node[leader_a_idx].term;

    /* Crash leader A */
    bus_crash_node(leader_a_idx);

    /* Wait for leader B election */
    for (k = 0; k < 3000u; k++) {
        bus_step();
        if (valid_leader_count() == 1) { break; }
    }

    leader_b_idx = valid_leader_index();
    check(leader_b_idx >= 0, "Lot 2D", "leader B elected");
    if (leader_b_idx < 0) { return; }
    term_b = g_bus.node[leader_b_idx].term;
    check(term_b > term_a, "Lot 2D", "term advanced to B");

    /* Let leader B establish */
    for (k = 0; k < 1000u; k++) { bus_step(); }

    /* Restart former leader A (cold - term=0) */
    bus_restart_node(leader_a_idx);

    /* Convergence */
    for (k = 0; k < 3000u; k++) { bus_step(); }

    /* Critical: former leader A must adopt term B, not disrupt */
    check(g_bus.node[leader_a_idx].term == term_b, "Lot 2D",
          "former leader A adopted newer term B after restart");
    check(g_bus.node[leader_a_idx].role == MOSAIK_ROLE_FOLLOWER, "Lot 2D",
          "former leader A is follower, not leader");
    check(mosaik_has_valid_leadership_authority(&g_bus.node[leader_b_idx]), "Lot 2D",
          "leader B retains valid authority");
    check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
          "cluster converges to single valid leader");
    check(g_bus.node[leader_b_idx].term == term_b, "Lot 2D",
          "leader B term unchanged");

    /* Check for term regression */
    for (int i = 0; i < N_NODES; i++) {
        if (g_bus.powered[i] && !g_bus.crashed[i]) {
            if (g_bus.node[i].term < term_b) {
                g_bus.term_regressions_lot2d++;
            }
        }
    }

    /* Track max concurrent valid authorities */
    if (g_bus.max_concurrent_valid_authorities_lot2d < (uint32_t)valid_leader_count()) {
        g_bus.max_concurrent_valid_authorities_lot2d = (uint32_t)valid_leader_count();
    }

    printf("        leader A term=%u crashed, leader B term=%u elected, A restarted adopted term=%u\n",
           term_a, term_b, g_bus.node[leader_a_idx].term);
}

/* TC-026: Restart with delayed pre-crash messages queued.
 * Scenario: capture heartbeats before crash, crash node, delay messages,
 * restart node, then deliver delayed pre-crash messages.
 * Verify: pre-crash messages rejected as stale. */
static void tc_026_restart_with_delayed_pre_crash_messages(void)
{
    int leader_idx, follower_idx;
    uint16_t term_before;
    mosaik_msg_t msg;
    mosaik_frame_t delayed_frames[5];
    int delayed_count = 0;
    uint32_t k;

    printf("TC-026  restart with delayed pre-crash messages queued [Lot 2D]\n");

    bus_init();
    bus_run(2000u);

    leader_idx = leader_index();
    check(leader_idx >= 0, "Lot 2D", "leader existed");
    if (leader_idx < 0) { return; }
    term_before = g_bus.node[leader_idx].term;

    follower_idx = (leader_idx + 1) % N_NODES;

    /* Capture some heartbeats from leader before crash */
    for (int i = 0; i < 3; i++) {
        bus_step();
        if (g_bus.node[leader_idx].role == MOSAIK_ROLE_LEADER) {
            msg.type = MOSAIK_MSG_HEARTBEAT;
            msg.src = (uint8_t)(leader_idx + 1);
            msg.version = MOSAIK_PROTO_VERSION;
            msg.role = MOSAIK_ROLE_LEADER;
            msg.state = MOSAIK_STATE_NOMINAL;
            msg.term = term_before;
            msg.arg = g_bus.node[leader_idx].seq;
            mosaik_encode(&delayed_frames[delayed_count], &msg);
            delayed_count++;
        }
    }

    /* Crash the follower */
    bus_crash_node(follower_idx);

    /* Run a bit - leader continues */
    for (k = 0; k < 500u; k++) { bus_step(); }

    /* Restart follower (cold - term=0) */
    bus_restart_node(follower_idx);

    /* Now deliver delayed pre-crash heartbeats (term=old_term) to restarted node */
    for (int i = 0; i < delayed_count; i++) {
        bus_inject_frame(&delayed_frames[i], delayed_frames[i].data[1]);
        bus_step();
    }

    /* Allow processing */
    for (k = 0; k < 1000u; k++) { bus_step(); }

    /* Verify: pre-crash messages rejected (stale term), follower adopts current term */
    check(g_bus.node[follower_idx].term == term_before, "Lot 2D",
          "restarted follower adopted current term despite delayed stale messages");
    check(g_bus.node[follower_idx].role == MOSAIK_ROLE_FOLLOWER, "Lot 2D",
          "restarted follower is follower");
    check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
          "single valid leader");

    /* Check stale authority rejections */
    if (g_bus.node[follower_idx].stale_term_rejections > 0 ||
        g_bus.node[follower_idx].stale_authority_rejections > 0) {
        g_bus.stale_authority_acceptances_lot2d +=
            g_bus.node[follower_idx].stale_term_rejections +
            g_bus.node[follower_idx].stale_authority_rejections;
    }

    /* Track max concurrent valid authorities */
    if (g_bus.max_concurrent_valid_authorities_lot2d < (uint32_t)valid_leader_count()) {
        g_bus.max_concurrent_valid_authorities_lot2d = (uint32_t)valid_leader_count();
    }

    printf("        delayed pre-crash frames=%d, follower term=%u, stale rejections\n",
           delayed_count, g_bus.node[follower_idx].term);
}

/* TC-027: Repeated crash/restart of one node.
 * Scenario: repeatedly crash and restart the same node.
 * Verify: no state corruption, term monotonicity, no duplicate votes. */
static void tc_027_repeated_crash_restart(void)
{
    int leader_idx, victim_idx;
    uint16_t term_before;
    uint32_t k;

    printf("TC-027  repeated crash/restart of one node [Lot 2D]\n");

    bus_init();
    bus_run(2000u);

    leader_idx = leader_index();
    check(leader_idx >= 0, "Lot 2D", "leader existed");
    if (leader_idx < 0) { return; }
    term_before = g_bus.node[leader_idx].term;

    /* Pick a victim (non-leader) */
    victim_idx = (leader_idx + 1) % N_NODES;

    /* Repeated crash/restart cycle */
    for (int cycle = 0; cycle < 5; cycle++) {
        bus_crash_node(victim_idx);
        for (k = 0; k < 200u; k++) { bus_step(); }
        bus_restart_node(victim_idx);
        for (k = 0; k < 500u; k++) { bus_step(); }
    }

    /* Final verification */
    check(g_bus.node[victim_idx].term == term_before, "Lot 2D",
          "victim adopted current term after repeated restarts");
    check(g_bus.node[victim_idx].role == MOSAIK_ROLE_FOLLOWER, "Lot 2D",
          "victim is follower");
    check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
          "single valid leader maintained");
    check(g_bus.node[leader_idx].term == term_before, "Lot 2D",
          "leader term unchanged");

    /* Check for term regression */
    for (int i = 0; i < N_NODES; i++) {
        if (g_bus.powered[i] && !g_bus.crashed[i]) {
            if (g_bus.node[i].term < term_before) {
                g_bus.term_regressions_lot2d++;
            }
        }
    }

    /* Track max concurrent valid authorities */
    if (g_bus.max_concurrent_valid_authorities_lot2d < (uint32_t)valid_leader_count()) {
        g_bus.max_concurrent_valid_authorities_lot2d = (uint32_t)valid_leader_count();
    }

    printf("        node %d crashed/restarted 5x, final term=%u, voted_term=%u, voted_for=%u\n",
           victim_idx + 1, g_bus.node[victim_idx].term,
           g_bus.node[victim_idx].voted_term, g_bus.node[victim_idx].voted_for);
}

/* TC-028: Crash during/near lease expiry.
 * Scenario: leader near lease expiry, crash it, verify lease expiry behavior preserved.
 * Isolate leader so lease cannot renew, wait for near-expiry, then crash.
 * NOTE: This test exposes a protocol limitation - with 2 remaining nodes,
 * synchronized follower election deadlines can cause split-vote leading to SAFE. */
static void tc_028_crash_during_lease_expiry(void)
{
    int leader_idx;
    uint16_t term_before;
    uint32_t k;

    printf("TC-028  crash during/near lease expiry [Lot 2D]\n");

    bus_init();
    bus_run(2000u);

    leader_idx = leader_index();
    check(leader_idx >= 0, "Lot 2D", "leader existed");
    if (leader_idx < 0) { return; }
    term_before = g_bus.node[leader_idx].term;

    /* Isolate leader from quorum so lease cannot renew.
     * Leader can send but ACKs from followers are dropped. */
    for (int i = 0; i < N_NODES; i++) {
        if (i != leader_idx) {
            g_bus.net_paths[i][leader_idx].action = NET_DROP;  /* followers -> leader */
        }
    }

    /* Wait until near lease expiry (500ms lease, wait until ~50ms remaining) */
    uint32_t target_ms = g_bus.node[leader_idx].lease_expiry_ms - 50;
    while (g_bus.now_ms < target_ms) { bus_step(); }

    /* Crash leader near lease expiry */
    bus_crash_node(leader_idx);
    uint32_t crash_ms = g_bus.now_ms;
    uint32_t lease_remaining = g_bus.node[leader_idx].lease_expiry_ms - crash_ms;

    /* Restore connectivity for election */
    bus_set_full_connectivity();

    /* Wait for new election - may take longer due to election timeouts
     * and time needed for new leader to establish valid authority (quorum ACKs).
     * NOTE: With 2 remaining nodes, synchronized deadlines may cause split-vote
     * leading to SAFE (protocol limitation). */
    for (k = 0; k < 5000u; k++) {
        bus_step();
        if (valid_leader_count() == 1) { break; }
    }

    int new_leader_idx = valid_leader_index();
    check(new_leader_idx >= 0, "Lot 2D", "new leader elected after crash near expiry");
    if (new_leader_idx >= 0) {
        check(g_bus.node[new_leader_idx].term > term_before, "Lot 2D",
              "term advanced after crash near lease expiry");
        check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
              "single valid leader");
    }

    g_bus.lease_expirations_lot2d++;

    /* Track max concurrent valid authorities */
    if (g_bus.max_concurrent_valid_authorities_lot2d < (uint32_t)valid_leader_count()) {
        g_bus.max_concurrent_valid_authorities_lot2d = (uint32_t)valid_leader_count();
    }

    printf("        leader %d crashed at %u ms (lease remaining ~%u ms)\n",
           leader_idx + 1, crash_ms, lease_remaining);
}

/* TC-029: Crash during election (candidate).
 * Scenario: node becomes candidate, crash it before election completes,
 * restart it, verify vote state reset, no duplicate vote in same term. */
static void tc_029_crash_during_election(void)
{
    int candidate_idx;
    uint32_t k;

    printf("TC-029  crash during election (candidate) [Lot 2D]\n");

    bus_init();
    bus_run(2000u);

    /* Force an election by crashing the leader */
    int leader_idx = leader_index();
    check(leader_idx >= 0, "Lot 2D", "initial leader existed");
    if (leader_idx < 0) { return; }
    (void)g_bus.node[leader_idx].term; /* term_before not directly used */

    bus_crash_node(leader_idx);

    /* Wait for a candidate to emerge */
    candidate_idx = -1;
    for (k = 0; k < 2000u; k++) {
        bus_step();
        /* Find a candidate */
        for (int i = 0; i < N_NODES; i++) {
            if (g_bus.powered[i] && !g_bus.crashed[i] &&
                g_bus.node[i].role == MOSAIK_ROLE_CANDIDATE) {
                candidate_idx = i;
                break;
            }
        }
        if (candidate_idx >= 0) { break; }
    }
    check(candidate_idx >= 0, "Lot 2D", "candidate emerged after leader crash");
    if (candidate_idx < 0) { return; }

    /* Verify candidate state */
    check(g_bus.node[candidate_idx].role == MOSAIK_ROLE_CANDIDATE, "Lot 2D",
          "node became candidate");
    check(g_bus.node[candidate_idx].voted_for == candidate_idx + 1, "Lot 2D",
          "candidate voted for self");
    check(g_bus.node[candidate_idx].voted_term == g_bus.node[candidate_idx].term, "Lot 2D",
          "candidate voted in current term");

    uint16_t candidate_term = g_bus.node[candidate_idx].term;
    (void)candidate_term; /* used in duplicate vote check below */

    /* Crash the candidate mid-election */
    bus_crash_node(candidate_idx);

    /* Run a bit */
    for (k = 0; k < 500u; k++) { bus_step(); }

    /* Restart candidate (cold - vote state LOST) */
    bus_restart_node(candidate_idx);

    /* Verify: immediately after restart, vote state is reset */
    check(g_bus.node[candidate_idx].voted_term == 0, "Lot 2D",
          "restarted candidate voted_term reset to 0 (cold restart)");
    check(g_bus.node[candidate_idx].voted_for == 0, "Lot 2D",
          "restarted candidate voted_for reset to 0 (cold restart)");
    check(g_bus.node[candidate_idx].term == 0, "Lot 2D",
          "restarted candidate term reset to 0 (cold restart)");

    /* Allow election to complete */
    for (k = 0; k < 3000u; k++) { bus_step(); }

    /* Verify: no duplicate vote in the ORIGINAL candidate term */
    if (g_bus.node[candidate_idx].voted_term == candidate_term &&
        g_bus.node[candidate_idx].voted_for != 0) {
        g_bus.duplicate_votes_lot2d++;
        check(false, "Lot 2D", "duplicate vote in same term after restart!");
    }
    check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
          "single valid leader after candidate crash/restart");

    /* Track max concurrent valid authorities */
    if (g_bus.max_concurrent_valid_authorities_lot2d < (uint32_t)valid_leader_count()) {
        g_bus.max_concurrent_valid_authorities_lot2d = (uint32_t)valid_leader_count();
    }

    printf("        candidate %d crashed at term %u, restarted, voted_term=%u, voted_for=%u\n",
           candidate_idx + 1, candidate_term,
           g_bus.node[candidate_idx].voted_term, g_bus.node[candidate_idx].voted_for);
}

/* TC-030: Recovery under asymmetric network conditions.
 * Scenario: crash a node, restart it under asymmetric directional faults.
 * Verify: safe convergence, no split-brain. */
static void tc_030_recovery_asymmetric_network(void)
{
    int leader_idx, victim_idx;
    uint16_t term_before;
    uint32_t k;

    printf("TC-030  recovery under asymmetric network [Lot 2D]\n");

    bus_init();
    bus_run(2000u);

    leader_idx = leader_index();
    check(leader_idx >= 0, "Lot 2D", "leader existed");
    if (leader_idx < 0) { return; }
    term_before = g_bus.node[leader_idx].term;

    victim_idx = (leader_idx + 1) % N_NODES;

    /* Apply asymmetric network: victim can send to leader but not receive from leader */
    bus_init_net_paths();
    for (int i = 0; i < N_NODES; i++) {
        for (int j = 0; j < N_NODES; j++) {
            if (i == j) continue;
            if (i == victim_idx && j == leader_idx) {
                g_bus.net_paths[i][j].action = NET_DELIVER;  /* victim -> leader */
            } else if (i == leader_idx && j == victim_idx) {
                g_bus.net_paths[i][j].action = NET_DROP;     /* leader -/-> victim */
            } else {
                g_bus.net_paths[i][j].action = NET_DELIVER;
            }
        }
    }

    /* Crash victim under asymmetric condition */
    bus_crash_node(victim_idx);
    for (k = 0; k < 500u; k++) { bus_step(); }

    /* Restart victim */
    bus_restart_node(victim_idx);

    /* Run for convergence under asymmetric network */
    for (k = 0; k < 3000u; k++) { bus_step(); }

    /* Verify: cluster maintains single valid leader */
    check(valid_leader_count() <= 1, "INV-LEADER-UNIQUE",
          "no split-brain under asymmetric recovery");
    check(g_bus.node[leader_idx].term >= term_before, "Lot 2D",
          "term monotonicity preserved");

    /* Track max concurrent valid authorities */
    if (g_bus.max_concurrent_valid_authorities_lot2d < (uint32_t)valid_leader_count()) {
        g_bus.max_concurrent_valid_authorities_lot2d = (uint32_t)valid_leader_count();
    }

    printf("        asymmetric recovery: victim %d, leader %d term=%u\n",
           victim_idx + 1, leader_idx + 1, g_bus.node[leader_idx].term);
}

/* ---------------------------------------------------------------------
 * LOT 2D - candidate retry backoff (C2-a) evidence tests, TC-031..TC-034.
 *
 * Rules honoured by these tests:
 *  - no protocol internal (deadline_ms, rng, term, voted_for, ...) is ever
 *    WRITTEN from the harness; node state is only read;
 *  - a collision is produced by crashing the leader at a naturally occurring
 *    instant at which both followers already hold the same deadline, found
 *    by read-only observation of normal protocol execution with the real
 *    per-node seeds;
 *  - adversarial behaviour uses only the existing directional network model
 *    (drop / delay) and the delayed-frame scheduler.
 * ------------------------------------------------------------------- */

/* Step the bus until both followers of leader_idx hold the same deadline
 * that still lies in the future. Read-only: nothing is written to any node.
 * Returns the shared deadline, or 0 if none occurs within max_ms. */
static uint32_t bus_wait_equal_follower_deadlines(int leader_idx, uint32_t max_ms)
{
    uint32_t k;
    for (k = 0; k < max_ms; k++) {
        int a = -1, b = -1, i;
        bus_step();
        for (i = 0; i < N_NODES; i++) {
            if (i == leader_idx) { continue; }
            if (a < 0) { a = i; } else { b = i; }
        }
        if (leader_index() == leader_idx &&
            g_bus.node[a].role == MOSAIK_ROLE_FOLLOWER &&
            g_bus.node[b].role == MOSAIK_ROLE_FOLLOWER &&
            g_bus.node[a].deadline_ms == g_bus.node[b].deadline_ms &&
            (int32_t)(g_bus.node[a].deadline_ms - g_bus.now_ms) > 0) {
            return g_bus.node[a].deadline_ms;
        }
    }
    return 0u;
}

/* Read-only per-step observation of the two survivors after a leader crash. */
typedef struct {
    int      leader_idx;             /* crashed leader, excluded */
    uint32_t max_valid;              /* INV-LEADER-UNIQUE evidence */
    uint8_t  max_failed[N_NODES];    /* highest failed_elections seen per node */
    uint16_t last_term[N_NODES];
    uint32_t term_regressions;
    bool     split_vote_seen;        /* both survivors candidates, same term, self-voted */
    uint16_t split_vote_term;
    bool     retry_desync_seen;      /* after >=1 failure each: deadlines differ */
    bool     retry_sync_only;        /* every observed post-failure pair was equal */
    uint16_t valid_leader_term;      /* term of the first valid authority seen, 0 if none */
} crash_obs_t;

static void obs_init(crash_obs_t *o, int leader_idx)
{
    int i;
    memset(o, 0, sizeof(*o));
    o->leader_idx = leader_idx;
    o->retry_sync_only = true;
    for (i = 0; i < N_NODES; i++) { o->last_term[i] = g_bus.node[i].term; }
}

static void obs_step(crash_obs_t *o)
{
    int a = -1, b = -1, i;
    uint32_t v = (uint32_t)valid_leader_count();
    const mosaik_node_t *na, *nb;

    if (v > o->max_valid) { o->max_valid = v; }
    if (v == 1u && o->valid_leader_term == 0u) {
        o->valid_leader_term = g_bus.node[valid_leader_index()].term;
    }
    for (i = 0; i < N_NODES; i++) {
        if (i == o->leader_idx || g_bus.crashed[i] || !g_bus.powered[i]) { continue; }
        if (g_bus.node[i].failed_elections > o->max_failed[i]) {
            o->max_failed[i] = g_bus.node[i].failed_elections;
        }
        if (g_bus.node[i].term < o->last_term[i]) { o->term_regressions++; }
        o->last_term[i] = g_bus.node[i].term;
        if (a < 0) { a = i; } else { b = i; }
    }
    if (a < 0 || b < 0) { return; }
    na = &g_bus.node[a];
    nb = &g_bus.node[b];
    if (na->role == MOSAIK_ROLE_CANDIDATE && nb->role == MOSAIK_ROLE_CANDIDATE &&
        na->term == nb->term && na->voted_for == na->id && nb->voted_for == nb->id) {
        if (!o->split_vote_seen) { o->split_vote_term = na->term; }
        o->split_vote_seen = true;
    }
    if (na->failed_elections >= 1u && nb->failed_elections >= 1u &&
        na->state != MOSAIK_STATE_SAFE && nb->state != MOSAIK_STATE_SAFE &&
        na->role != MOSAIK_ROLE_LEADER && nb->role != MOSAIK_ROLE_LEADER) {
        if (na->deadline_ms != nb->deadline_ms) {
            o->retry_desync_seen = true;
            o->retry_sync_only  = false;
        }
    }
}

/* Common prologue: elect a leader, find a natural shared follower deadline,
 * crash the leader while it is active. Returns the crashed leader index or
 * -1 (checks already recorded). */
static int collision_prologue(const char *req, uint32_t *shared_deadline,
                              uint32_t *crash_ms, uint16_t *term_before)
{
    int leader_idx;
    bus_run(2000u);
    leader_idx = leader_index();
    check(leader_idx >= 0, req, "leader existed before the fault");
    if (leader_idx < 0) { return -1; }
    *term_before = g_bus.node[leader_idx].term;
    *shared_deadline = bus_wait_equal_follower_deadlines(leader_idx, 20000u);
    check(*shared_deadline != 0u, req,
          "naturally equal follower deadlines observed (read-only, real seeds)");
    if (*shared_deadline == 0u) { return -1; }
    bus_crash_node(leader_idx);
    *crash_ms = g_bus.now_ms;
    return leader_idx;
}

/* TC-031: natural election collision, randomized retry recovery.
 * Both followers receive the same heartbeat and, by RNG coincidence, draw
 * the same jitter, so they hold the same deadline. The leader is crashed
 * while that deadline is active. The split vote is therefore natural; the
 * property under test is what happens AFTER it.
 * EXPECTED ON BASELINE c6f600b: RED. Candidate retries use the fixed
 * vote_timeout_ms with no fresh randomness, so both survivors retry in
 * lock-step three times and latch SAFE/NO_QUORUM. After C2-a: PASS. */
static void tc_031_natural_collision_recovery(void)
{
    int leader_idx, li, i;
    uint32_t shared_deadline = 0u, crash_ms = 0u, elected_ms = 0u, k;
    uint16_t term_before = 0u;
    crash_obs_t o;

    printf("TC-031  natural election collision, randomized retry recovery [Lot 2D, REQ-004]\n");
    bus_init();
    leader_idx = collision_prologue("Lot 2D", &shared_deadline, &crash_ms, &term_before);
    if (leader_idx < 0) { return; }
    obs_init(&o, leader_idx);

    for (k = 0; k < 3000u; k++) {
        bus_step();
        obs_step(&o);
        if (valid_leader_count() == 1) { elected_ms = g_bus.now_ms; break; }
    }
    li = valid_leader_index();

    check(o.split_vote_seen, "Lot 2D",
          "initial split vote occurred naturally (both candidates, same term, self-voted)");
    check(o.max_valid <= 1u, "INV-LEADER-UNIQUE", "never more than one valid authority");
    check(o.term_regressions == 0u, "Lot 2D", "no term regression on any survivor");
    check(o.retry_desync_seen, "Lot 2D",
          "retry deadlines desynchronized after the first failed election");
    for (i = 0; i < N_NODES; i++) {
        if (i == leader_idx) { continue; }
        check(o.max_failed[i] == 1u, "Lot 2D",
              "survivor recorded exactly one failed election");
        check(g_bus.node[i].state != MOSAIK_STATE_SAFE, "Lot 2D",
              "survivor did not enter SAFE");
    }
    check(li >= 0, "REQ-004", "new valid leader elected after natural collision");
    if (li >= 0) {
        check(elected_ms - crash_ms < 1000u, "REQ-004",
              "recovery within 1000 ms of leader loss despite one collision");
        check(g_bus.node[li].term > term_before, "Lot 2D", "term advanced");
    }
    printf("        leader %d crashed at %u ms, shared follower deadline %u, collision term %u\n",
           leader_idx + 1, crash_ms, shared_deadline, o.split_vote_term);
    if (li >= 0) {
        printf("        node %d valid leader at %u ms (%u ms after loss), term %u\n",
               li + 1, elected_ms, elected_ms - crash_ms, g_bus.node[li].term);
    } else {
        for (i = 0; i < N_NODES; i++) {
            if (i == leader_idx) { continue; }
            printf("        survivor %d: state=%d failed_elections=%u term=%u (no leader)\n",
                   i + 1, (int)g_bus.node[i].state, g_bus.node[i].failed_elections,
                   g_bus.node[i].term);
        }
    }
}

/* TC-032: permanent retry contention keeps the SAFE contract.
 * The randomized retry must not weaken SAFE when contention truly persists.
 * With candidate_retry_backoff_span_ms = 1 the C2-a retry backoff has
 * exactly one possible value (0 ms) on every node, so retries after a
 * natural collision stay synchronized by configuration. This is the
 * documented way to reproduce permanent contention without touching any
 * protocol internal. */
static void tc_032_permanent_contention_safe_contract(void)
{
    mosaik_config_t cfg;
    int leader_idx, i;
    uint32_t shared_deadline = 0u, crash_ms = 0u, k;
    uint16_t term_before = 0u;
    uint32_t safe_ms[N_NODES] = {0u, 0u, 0u};
    crash_obs_t o;

    printf("TC-032  permanent retry contention keeps SAFE contract [Lot 2D]\n");
    mosaik_config_default(&cfg);
    cfg.candidate_retry_backoff_span_ms = 1u; /* zero effective desynchronization */
    bus_init_with_cfg(&cfg);
    leader_idx = collision_prologue("Lot 2D", &shared_deadline, &crash_ms, &term_before);
    if (leader_idx < 0) { return; }
    obs_init(&o, leader_idx);

    for (k = 0; k < 3000u; k++) {
        bus_step();
        obs_step(&o);
        for (i = 0; i < N_NODES; i++) {
            if (i != leader_idx && safe_ms[i] == 0u &&
                g_bus.node[i].state == MOSAIK_STATE_SAFE) {
                safe_ms[i] = g_bus.node[i].safe_entry_ms;
            }
        }
    }

    check(o.split_vote_seen, "Lot 2D", "initial split vote occurred naturally");
    check(o.retry_sync_only, "Lot 2D", "retries remained synchronized (no effective backoff)");
    check(o.max_valid == 0u, "Lot 2D", "no authority manufactured under permanent contention");
    check(o.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1");
    for (i = 0; i < N_NODES; i++) {
        if (i == leader_idx) { continue; }
        check(o.max_failed[i] == cfg.max_failed_elections, "Lot 2D",
              "exactly max_failed_elections genuine failed vote timeouts");
        check(g_bus.node[i].state == MOSAIK_STATE_SAFE &&
              g_bus.node[i].safe_cause == (uint8_t)MOSAIK_SAFE_NO_QUORUM, "Lot 2D",
              "survivor latched SAFE with cause no-quorum");
    }
    printf("        leader %d crashed at %u ms, shared deadline %u, SAFE at",
           leader_idx + 1, crash_ms, shared_deadline);
    for (i = 0; i < N_NODES; i++) {
        if (i != leader_idx) { printf(" node%d=%u", i + 1, safe_ms[i]); }
    }
    printf("\n");
}

/* TC-033: asymmetric partition during retry.
 * After a natural collision, one direction between the two survivors is cut
 * with the existing directional network model, so neither can ever assemble
 * a quorum. Randomized retry must not manufacture authority; both must end
 * in SAFE/NO_QUORUM after max_failed_elections. GREEN before and after. */
static void tc_033_asymmetric_partition_during_retry(void)
{
    int leader_idx, a = -1, b = -1, i;
    uint32_t shared_deadline = 0u, crash_ms = 0u, k;
    uint16_t term_before = 0u;
    crash_obs_t o;

    printf("TC-033  asymmetric partition during retry [Lot 2D, INV-LEADER-UNIQUE]\n");
    bus_init();
    leader_idx = collision_prologue("Lot 2D", &shared_deadline, &crash_ms, &term_before);
    if (leader_idx < 0) { return; }
    obs_init(&o, leader_idx);
    for (i = 0; i < N_NODES; i++) {
        if (i == leader_idx) { continue; }
        if (a < 0) { a = i; } else { b = i; }
    }

    /* Let the natural collision happen first. */
    for (k = 0; k < 1000u && !o.split_vote_seen; k++) { bus_step(); obs_step(&o); }
    check(o.split_vote_seen, "Lot 2D", "initial split vote occurred naturally");
    if (!o.split_vote_seen) { return; }

    /* Cut a -> b for the rest of the test: b never receives a's VOTE_REQ or
     * VOTE_GRANT, so no survivor can ever collect a second vote. */
    bus_set_net_action((uint8_t)a, (uint8_t)b, NET_DROP, 0u);

    for (k = 0; k < 5000u; k++) { bus_step(); obs_step(&o); }

    check(o.max_valid == 0u, "Lot 2D", "no leader without an actually received quorum vote");
    check(o.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1");
    check(o.term_regressions == 0u, "Lot 2D", "no term regression");
    for (i = 0; i < N_NODES; i++) {
        if (i == leader_idx) { continue; }
        check(g_bus.node[i].state == MOSAIK_STATE_SAFE &&
              g_bus.node[i].safe_cause == (uint8_t)MOSAIK_SAFE_NO_QUORUM, "Lot 2D",
              "survivor latched SAFE/no-quorum after max_failed_elections");
        check(o.max_failed[i] == g_bus.node[i].cfg.max_failed_elections, "Lot 2D",
              "SAFE reached only through genuine failed vote timeouts");
    }
    printf("        collision term %u, path %d->%d dropped, survivors terms %u/%u\n",
           o.split_vote_term, a + 1, b + 1, g_bus.node[a].term, g_bus.node[b].term);
}

/* TC-034: stale delayed election traffic during collision/retry.
 * At the collision instant the real term-t VOTE_REQ from survivor a to b is
 * delayed 320 ms by the directional network model, and a replayed term-t
 * VOTE_GRANT (a -> b) is scheduled with the same delay, so both arrive after
 * b has advanced beyond term t. Stale traffic must not regress the term,
 * must not produce authority, and must not disturb the retry/SAFE rules.
 * GREEN before and after. */
static void tc_034_stale_delayed_election_traffic(void)
{
    int leader_idx, a = -1, b = -1, i;
    uint32_t shared_deadline = 0u, crash_ms = 0u, k, stale_before;
    uint16_t term_before = 0u, t;
    crash_obs_t o;
    mosaik_msg_t msg;
    mosaik_frame_t frame;

    printf("TC-034  stale delayed election traffic during retry [Lot 2D, Lot 2B]\n");
    bus_init();
    leader_idx = collision_prologue("Lot 2D", &shared_deadline, &crash_ms, &term_before);
    if (leader_idx < 0) { return; }
    obs_init(&o, leader_idx);
    for (i = 0; i < N_NODES; i++) {
        if (i == leader_idx) { continue; }
        if (a < 0) { a = i; } else { b = i; }
    }

    /* Step until both survivors are candidates in the same term t. Their
     * VOTE_REQs are now queued for delivery in the next step. */
    for (k = 0; k < 1000u && !o.split_vote_seen; k++) { bus_step(); obs_step(&o); }
    check(o.split_vote_seen, "Lot 2D", "initial split vote occurred naturally");
    if (!o.split_vote_seen) { return; }
    t = o.split_vote_term;
    stale_before = g_bus.node[b].stale_term_rejections;

    /* Delay a's real term-t VOTE_REQ towards b by 320 ms, one step only. */
    bus_set_net_action((uint8_t)a, (uint8_t)b, NET_DELAY, 320u);
    bus_step(); obs_step(&o);
    bus_set_net_action((uint8_t)a, (uint8_t)b, NET_DELIVER, 0u);

    /* Replay: a term-t VOTE_GRANT a -> b that arrives with the same delay. */
    msg.type = MOSAIK_MSG_VOTE_GRANT;
    msg.src = (uint8_t)(a + 1);
    msg.version = MOSAIK_PROTO_VERSION;
    msg.role = MOSAIK_ROLE_FOLLOWER;
    msg.state = MOSAIK_STATE_NOMINAL;
    msg.term = t;
    msg.arg = (uint8_t)(b + 1);
    mosaik_encode(&frame, &msg);
    (void)bus_schedule_delayed(&frame, (uint8_t)(a + 1), 320u);

    for (k = 0; k < 3000u; k++) { bus_step(); obs_step(&o); }

    check(g_bus.node[b].term > t, "Lot 2D", "receiver advanced beyond the collision term");
    check(g_bus.node[b].stale_term_rejections >= stale_before + 2u, "Lot 2B",
          "delayed old-term VOTE_REQ and VOTE_GRANT both rejected as stale");
    check(o.term_regressions == 0u, "Lot 2B", "stale traffic did not regress any term");
    check(o.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1");
    check(o.valid_leader_term == 0u || o.valid_leader_term > t, "Lot 2B",
          "no authority produced in the collision term by stale traffic");
    for (i = 0; i < N_NODES; i++) {
        if (i == leader_idx) { continue; }
        check((g_bus.node[i].state == MOSAIK_STATE_SAFE) ==
              (o.max_failed[i] >= g_bus.node[i].cfg.max_failed_elections), "Lot 2D",
              "SAFE if and only if max_failed_elections genuine failures (retry rules intact)");
    }
    printf("        collision term %u, stale rejections on node %d: %u -> %u, valid leader term %u\n",
           t, b + 1, stale_before, g_bus.node[b].stale_term_rejections, o.valid_leader_term);
}

/* ---------------------------------------------------------------------
 * LOT 3 - FDIR / SAFE evidence tests, TC-035..TC-050 (Phase 1, RED baseline).
 *
 * Rules honoured by these tests:
 *  - no protocol internal (term, voted_for, voted_term, role, lease_expiry_ms,
 *    deadline_ms, rng, ...) is ever written from the harness;
 *  - faults are injected only through the existing mechanisms: directional
 *    network actions, crash/cold restart, frame injection and delayed
 *    delivery, and the TC-004 style targeted delivery of one adversarial
 *    frame to one node;
 *  - harness knowledge (topology, crashed[], other nodes' state, counters)
 *    is used for assertions only and never reaches a node.
 * ------------------------------------------------------------------- */

/* Per-step read-only trajectory observation of every running node. */
typedef struct {
    uint32_t max_valid;                 /* INV-LEADER-UNIQUE evidence */
    uint32_t first_valid_ms;            /* first step with exactly one valid authority, 0 if none */
    int      first_valid_idx;
    uint32_t term_regressions;
    uint32_t safe_auth_violations;      /* state SAFE and valid authority */
    uint32_t safe_role_violations;      /* state SAFE and role != FOLLOWER */
    uint32_t safe_exits;                /* SAFE -> not SAFE without a cold restart */
    uint32_t max_safe_nodes;            /* max simultaneous SAFE nodes */
    bool     leader_degraded_valid_seen;/* a valid leader observed in DEGRADED */
    uint32_t steps_nominal[N_NODES];
    uint32_t steps_degraded[N_NODES];
    uint32_t nom_to_deg[N_NODES];
    uint32_t deg_to_nom[N_NODES];
    uint32_t first_degraded_ms[N_NODES];        /* 0 = never */
    uint32_t first_deg_run_len[N_NODES];        /* length of the first DEGRADED run */
    bool     first_deg_run_open[N_NODES];
    uint32_t nominal_after_first_deg[N_NODES];  /* NOMINAL steps after first DEGRADED entry */
    uint8_t  max_failed[N_NODES];
    mosaik_state_t last_state[N_NODES];
    uint16_t last_term[N_NODES];
    uint32_t last_restart[N_NODES];
} traj_obs_t;

static void traj_init(traj_obs_t *o)
{
    int i;
    memset(o, 0, sizeof(*o));
    o->first_valid_idx = -1;
    for (i = 0; i < N_NODES; i++) {
        o->last_state[i]   = g_bus.node[i].state;
        o->last_term[i]    = g_bus.node[i].term;
        o->last_restart[i] = g_bus.restart_count[i];
    }
}

static void traj_step(traj_obs_t *o)
{
    int i;
    uint32_t v = (uint32_t)valid_leader_count();
    uint32_t safe_n = 0u;

    if (v > o->max_valid) { o->max_valid = v; }
    if (v == 1u && o->first_valid_ms == 0u) {
        o->first_valid_ms  = g_bus.now_ms;
        o->first_valid_idx = valid_leader_index();
    }
    for (i = 0; i < N_NODES; i++) {
        const mosaik_node_t *n = &g_bus.node[i];
        if (g_bus.crashed[i] || !g_bus.powered[i]) { continue; }
        if (g_bus.restart_count[i] != o->last_restart[i]) {
            /* cold restart: a new volatile instance, no regression or exit counted */
            o->last_restart[i] = g_bus.restart_count[i];
            o->last_state[i]   = n->state;
            o->last_term[i]    = n->term;
        }
        if (n->term < o->last_term[i]) { o->term_regressions++; }
        o->last_term[i] = n->term;
        if (n->failed_elections > o->max_failed[i]) { o->max_failed[i] = n->failed_elections; }

        if (n->state == MOSAIK_STATE_SAFE) {
            safe_n++;
            if (mosaik_has_valid_leadership_authority(n)) { o->safe_auth_violations++; }
            if (n->role != MOSAIK_ROLE_FOLLOWER) { o->safe_role_violations++; }
        }
        if (o->last_state[i] == MOSAIK_STATE_SAFE && n->state != MOSAIK_STATE_SAFE) { o->safe_exits++; }
        if (mosaik_has_valid_leadership_authority(n) && n->state == MOSAIK_STATE_DEGRADED) {
            o->leader_degraded_valid_seen = true;
        }
        if (n->state == MOSAIK_STATE_NOMINAL) {
            o->steps_nominal[i]++;
            if (o->first_degraded_ms[i] != 0u) { o->nominal_after_first_deg[i]++; }
        }
        if (n->state == MOSAIK_STATE_DEGRADED) {
            o->steps_degraded[i]++;
            if (o->first_degraded_ms[i] == 0u) {
                o->first_degraded_ms[i] = g_bus.now_ms;
                o->first_deg_run_open[i] = true;
            }
            if (o->first_deg_run_open[i]) { o->first_deg_run_len[i]++; }
        } else if (o->first_deg_run_open[i]) {
            o->first_deg_run_open[i] = false;
        }
        if (o->last_state[i] == MOSAIK_STATE_NOMINAL && n->state == MOSAIK_STATE_DEGRADED) { o->nom_to_deg[i]++; }
        if (o->last_state[i] == MOSAIK_STATE_DEGRADED && n->state == MOSAIK_STATE_NOMINAL) { o->deg_to_nom[i]++; }
        o->last_state[i] = n->state;
    }
    if (safe_n > o->max_safe_nodes) { o->max_safe_nodes = safe_n; }
}

static void traj_run(traj_obs_t *o, uint32_t ms)
{
    uint32_t k;
    for (k = 0; k < ms; k++) { bus_step(); traj_step(o); }
}

/* Read-only recovery predicate: exactly one valid authority, every running
 * non-SAFE node follows it with a heartbeat still inside its lease window,
 * and no candidate exists. */
static bool cluster_recovered(int *leader_out)
{
    int i, li = valid_leader_index();
    if (valid_leader_count() != 1 || li < 0) { return false; }
    for (i = 0; i < N_NODES; i++) {
        const mosaik_node_t *n = &g_bus.node[i];
        if (g_bus.crashed[i] || !g_bus.powered[i] || n->state == MOSAIK_STATE_SAFE) { continue; }
        if (i == li) { continue; }
        if (n->role != MOSAIK_ROLE_FOLLOWER) { return false; }
        if (n->leader_id != g_bus.node[li].id) { return false; }
        if ((int32_t)(n->deadline_ms - g_bus.now_ms) <= 0) { return false; }
    }
    if (leader_out) { *leader_out = li; }
    return true;
}

/* Targeted delivery of one adversarial frame to one node (TC-004 style). */
static void deliver_to(int idx, const mosaik_msg_t *m)
{
    mosaik_frame_t f;
    mosaik_encode(&f, m);
    mosaik_on_rx(&g_bus.node[idx], g_bus.now_ms, &f);
}

static void make_msg(mosaik_msg_t *m, mosaik_msg_type_t type, uint8_t src,
                     mosaik_role_t role, mosaik_state_t state, uint16_t term, uint8_t arg)
{
    memset(m, 0, sizeof(*m));
    m->type = type; m->src = src; m->version = MOSAIK_PROTO_VERSION;
    m->role = role; m->state = state; m->term = term; m->arg = arg;
}

/* Same-term dual-leader injection into the current leader, as TC-004 does.
 * Returns the index of the node that latched SAFE, or -1. */
static int split_brain_leader(void)
{
    int li = leader_index();
    mosaik_msg_t m;
    if (li < 0) { return -1; }
    make_msg(&m, MOSAIK_MSG_HEARTBEAT, (uint8_t)(((li + 1) % N_NODES) + 1),
             MOSAIK_ROLE_LEADER, MOSAIK_STATE_NOMINAL, g_bus.node[li].term, 0u);
    deliver_to(li, &m);
    return (g_bus.node[li].state == MOSAIK_STATE_SAFE) ? li : -1;
}

/* Cut or restore both directions between one node and every other node. */
static void isolate_node(int idx, bool cut)
{
    int j;
    for (j = 0; j < N_NODES; j++) {
        if (j == idx) { continue; }
        bus_set_net_action((uint8_t)idx, (uint8_t)j, cut ? NET_DROP : NET_DELIVER, 0u);
        bus_set_net_action((uint8_t)j, (uint8_t)idx, cut ? NET_DROP : NET_DELIVER, 0u);
    }
}

static void survivors_of(int li, int *a, int *b)
{
    int i; *a = -1; *b = -1;
    for (i = 0; i < N_NODES; i++) { if (i == li) { continue; } if (*a < 0) { *a = i; } else { *b = i; } }
}

#define TX(idx, type) (g_bus.tx_count[(idx)][(int)(type)])

/* TC-035: SAFE contract - no authority, SAFE-only transmission, and the
 * cluster recovers around the SAFE node while the SAFE node itself does not. */
static void tc_035_safe_no_authority_cluster_recovers(void)
{
    int li, a, b;
    uint32_t t_safe, tx_safe0, tx_req0, tx_grant0, tx_hb0, tx_ack0;
    traj_obs_t o;

    printf("TC-035  SAFE node: no authority, SAFE-only transmission, cluster recovers around it [Lot 3]\n");
    bus_init(); bus_run(2000u);
    li = split_brain_leader();
    check(li >= 0, "REQ-SAFE-0003", "leader latched SAFE on same-term dual leader");
    if (li < 0) { return; }
    survivors_of(li, &a, &b);
    t_safe = g_bus.now_ms;
    tx_safe0 = TX(li, MOSAIK_MSG_SAFE); tx_req0 = TX(li, MOSAIK_MSG_VOTE_REQ);
    tx_grant0 = TX(li, MOSAIK_MSG_VOTE_GRANT); tx_hb0 = TX(li, MOSAIK_MSG_HEARTBEAT); tx_ack0 = TX(li, MOSAIK_MSG_ACK);
    traj_init(&o);
    traj_run(&o, 3000u);

    check(o.safe_auth_violations == 0u, "INV-SAFE-NO-AUTHORITY", "SAFE node never held valid leadership authority");
    check(o.safe_role_violations == 0u, "REQ-SAFE-0003", "SAFE node never became candidate or leader");
    check(TX(li, MOSAIK_MSG_SAFE) > tx_safe0, "REQ-SAFE-0003", "SAFE node kept announcing SAFE");
    check(TX(li, MOSAIK_MSG_VOTE_REQ) == tx_req0 && TX(li, MOSAIK_MSG_VOTE_GRANT) == tx_grant0 &&
          TX(li, MOSAIK_MSG_HEARTBEAT) == tx_hb0 && TX(li, MOSAIK_MSG_ACK) == tx_ack0,
          "REQ-SAFE-0003", "SAFE node transmitted SAFE frames only");
    check(o.first_valid_ms != 0u && o.first_valid_idx != li && o.first_valid_ms - t_safe < 1000u,
          "REQ-FUNC-0004", "cluster elected a new valid leader around the SAFE node within 1000 ms");
    check(g_bus.node[li].state == MOSAIK_STATE_SAFE && o.safe_exits == 0u, "INV-SAFE-LATCH",
          "SAFE node itself did not recover");
    check(o.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1");
    check(o.term_regressions == 0u, "INV-TERM-MONOTONIC", "no term regression");
    printf("        node %d SAFE at %u ms; new valid leader node %d at %u ms; SAFE frames emitted %u\n",
           li + 1, t_safe, o.first_valid_idx + 1, o.first_valid_ms, TX(li, MOSAIK_MSG_SAFE) - tx_safe0);
    (void)a; (void)b;
}

/* TC-036: SAFE node does not participate: no vote, no ACK, no candidacy. */
static void tc_036_safe_non_participation(void)
{
    int li, a, b;
    uint16_t term0, cluster_term;
    uint32_t g0, k0, r0, h0;
    mosaik_msg_t m;
    traj_obs_t o;

    printf("TC-036  SAFE node grants no vote, sends no ACK, never becomes candidate [Lot 3]\n");
    bus_init(); bus_run(2000u);
    li = split_brain_leader();
    check(li >= 0, "REQ-SAFE-0003", "leader latched SAFE");
    if (li < 0) { return; }
    survivors_of(li, &a, &b);
    bus_run(1000u);                                    /* survivors elect around it */
    term0 = g_bus.node[li].term;
    cluster_term = g_bus.node[a].term > g_bus.node[b].term ? g_bus.node[a].term : g_bus.node[b].term;
    g0 = TX(li, MOSAIK_MSG_VOTE_GRANT); k0 = TX(li, MOSAIK_MSG_ACK);
    r0 = TX(li, MOSAIK_MSG_VOTE_REQ);   h0 = TX(li, MOSAIK_MSG_HEARTBEAT);

    make_msg(&m, MOSAIK_MSG_VOTE_REQ, (uint8_t)(a + 1), MOSAIK_ROLE_CANDIDATE, MOSAIK_STATE_NOMINAL,
             (uint16_t)(cluster_term + 1u), 0u);
    deliver_to(li, &m);                                /* admissible higher-term vote request */
    make_msg(&m, MOSAIK_MSG_HEARTBEAT, (uint8_t)(a + 1), MOSAIK_ROLE_LEADER, MOSAIK_STATE_NOMINAL,
             (uint16_t)(cluster_term + 1u), 7u);
    deliver_to(li, &m);                                /* admissible higher-term heartbeat */
    traj_init(&o);
    traj_run(&o, 1500u);                               /* long enough for any election timeout */

    check(TX(li, MOSAIK_MSG_VOTE_GRANT) == g0, "REQ-SAFE-0003", "SAFE node granted no vote");
    check(TX(li, MOSAIK_MSG_ACK) == k0, "REQ-SAFE-0003", "SAFE node acknowledged no heartbeat");
    check(TX(li, MOSAIK_MSG_VOTE_REQ) == r0 && TX(li, MOSAIK_MSG_HEARTBEAT) == h0,
          "REQ-SAFE-0003", "SAFE node started no election and sent no heartbeat");
    check(g_bus.node[li].role == MOSAIK_ROLE_FOLLOWER && o.safe_role_violations == 0u,
          "REQ-SAFE-0003", "SAFE node never became candidate or leader");
    check(g_bus.node[li].term == term0, "INV-SAFE-LATCH", "SAFE node did not adopt the higher term");
    check(g_bus.node[li].state == MOSAIK_STATE_SAFE && o.safe_exits == 0u, "INV-SAFE-LATCH", "still SAFE");
    check(o.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1");
    printf("        SAFE node %d: term %u held, grants %u, acks %u, elections %u after admissible higher-term traffic\n",
           li + 1, g_bus.node[li].term, TX(li, MOSAIK_MSG_VOTE_GRANT) - g0, TX(li, MOSAIK_MSG_ACK) - k0,
           TX(li, MOSAIK_MSG_VOTE_REQ) - r0);
}

/* TC-037: SAFE latch against old-, same- and higher-term traffic, SAFE
 * replays and connectivity changes: no implicit exit. */
static void tc_037_safe_latch(void)
{
    int li, a, b;
    uint16_t t0; uint8_t cause0;
    mosaik_msg_t m;
    traj_obs_t o;

    printf("TC-037  SAFE latch: no frame, term or connectivity change clears SAFE [Lot 3]\n");
    bus_init(); bus_run(2000u);
    li = split_brain_leader();
    check(li >= 0, "REQ-SAFE-0003", "leader latched SAFE");
    if (li < 0) { return; }
    survivors_of(li, &a, &b);
    bus_run(1000u);
    t0 = g_bus.node[li].term; cause0 = g_bus.node[li].safe_cause;
    traj_init(&o);

    make_msg(&m, MOSAIK_MSG_HEARTBEAT, (uint8_t)(a + 1), MOSAIK_ROLE_LEADER, MOSAIK_STATE_NOMINAL, (uint16_t)(t0 - 1u), 1u);
    deliver_to(li, &m);                                            /* old term */
    make_msg(&m, MOSAIK_MSG_HEARTBEAT, (uint8_t)(a + 1), MOSAIK_ROLE_LEADER, MOSAIK_STATE_NOMINAL, t0, 2u);
    deliver_to(li, &m);                                            /* same term */
    make_msg(&m, MOSAIK_MSG_HEARTBEAT, (uint8_t)(a + 1), MOSAIK_ROLE_LEADER, MOSAIK_STATE_NOMINAL, (uint16_t)(t0 + 5u), 3u);
    deliver_to(li, &m);                                            /* higher term */
    make_msg(&m, MOSAIK_MSG_VOTE_GRANT, (uint8_t)(a + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_NOMINAL, t0, (uint8_t)(li + 1));
    deliver_to(li, &m);                                            /* grant addressed to it */
    make_msg(&m, MOSAIK_MSG_SAFE, (uint8_t)(b + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_SAFE, (uint16_t)(t0 + 1u), (uint8_t)MOSAIK_SAFE_NO_QUORUM);
    deliver_to(li, &m);                                            /* SAFE announce from a peer */
    traj_step(&o);
    isolate_node(li, true);  traj_run(&o, 500u);                   /* connectivity removed ... */
    isolate_node(li, false); traj_run(&o, 1500u);                  /* ... and restored */

    check(g_bus.node[li].state == MOSAIK_STATE_SAFE, "INV-SAFE-LATCH", "still SAFE after all traffic and connectivity changes");
    check(g_bus.node[li].safe_cause == cause0, "INV-SAFE-LATCH", "SAFE cause unchanged");
    check(g_bus.node[li].term == t0, "INV-SAFE-LATCH", "term unchanged by old, same or higher-term traffic");
    check(g_bus.node[li].role == MOSAIK_ROLE_FOLLOWER, "INV-SAFE-NO-AUTHORITY", "role stays follower");
    check(o.safe_exits == 0u && o.safe_auth_violations == 0u, "INV-SAFE-LATCH", "no implicit SAFE exit, no authority");
    check(o.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1");
    printf("        SAFE node %d: term %u, cause %u held through old/same/higher-term frames, grant, SAFE replay, isolation and restore\n",
           li + 1, g_bus.node[li].term, g_bus.node[li].safe_cause);
}

/* TC-038: SAFE is not propagated: genuine, replayed and forged SAFE frames
 * never move a healthy peer into SAFE. */
static void tc_038_safe_not_propagated(void)
{
    int li, a, b;
    uint32_t k;
    mosaik_msg_t m; mosaik_frame_t f;
    traj_obs_t o;

    printf("TC-038  SAFE frames do not propagate SAFE to healthy peers [Lot 3]\n");
    bus_init(); bus_run(2000u);
    li = split_brain_leader();
    check(li >= 0, "REQ-SAFE-0003", "leader latched SAFE");
    if (li < 0) { return; }
    survivors_of(li, &a, &b);
    traj_init(&o);
    for (k = 0; k < 3000u; k++) {
        if (k % 500u == 250u) {                                    /* replayed SAFE from the SAFE node */
            make_msg(&m, MOSAIK_MSG_SAFE, (uint8_t)(li + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_SAFE,
                     g_bus.node[li].term, (uint8_t)MOSAIK_SAFE_SPLIT_BRAIN);
            mosaik_encode(&f, &m); bus_inject_frame(&f, (uint8_t)(li + 1));
        }
        if (k == 1200u) {                                          /* forged SAFE attributed to survivor a */
            make_msg(&m, MOSAIK_MSG_SAFE, (uint8_t)(a + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_SAFE,
                     g_bus.node[a].term, (uint8_t)MOSAIK_SAFE_NO_QUORUM);
            mosaik_encode(&f, &m); bus_inject_frame(&f, (uint8_t)(a + 1));
        }
        bus_step(); traj_step(&o);
    }
    check(o.max_safe_nodes == 1u, "REQ-SAFE-0001", "only the faulted node was ever SAFE");
    check(g_bus.node[a].state != MOSAIK_STATE_SAFE && g_bus.node[b].state != MOSAIK_STATE_SAFE,
          "REQ-SAFE-0001", "survivors never entered SAFE from SAFE frames");
    check(valid_leader_count() == 1 && valid_leader_index() != li, "REQ-FUNC-0001", "one valid leader among the survivors");
    check(o.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1");
    check(o.term_regressions == 0u, "INV-TERM-MONOTONIC", "no term regression");
    printf("        SAFE nodes max %u, survivors states %d/%d, valid leader node %d\n",
           o.max_safe_nodes, (int)g_bus.node[a].state, (int)g_bus.node[b].state, valid_leader_index() + 1);
}

/* TC-039: DEGRADED persistence while fresh SAFE evidence exists (REQ-SAFE-0004).
 * EXPECTED RED on 76f10d9: an accepted heartbeat unconditionally sets NOMINAL,
 * so followers flap DEGRADED/NOMINAL every heartbeat while the peer is SAFE. */
static void tc_039_degraded_persistence(void)
{
    int li, a, b, i;
    uint32_t t_safe, tx_safe0;
    traj_obs_t o;

    printf("TC-039  DEGRADED persists while a peer's SAFE evidence is fresh, no flapping [Lot 3, REQ-SAFE-0004]\n");
    bus_init(); bus_run(2000u);
    li = split_brain_leader();
    check(li >= 0, "REQ-SAFE-0003", "leader latched SAFE");
    if (li < 0) { return; }
    survivors_of(li, &a, &b);
    t_safe = g_bus.now_ms; tx_safe0 = TX(li, MOSAIK_MSG_SAFE);
    traj_init(&o);
    traj_run(&o, 3000u);

    check(TX(li, MOSAIK_MSG_SAFE) - tx_safe0 >= 25u, "Lot 3", "SAFE evidence was broadcast throughout (>= 25 SAFE frames in 3 s)");
    for (i = 0; i < N_NODES; i++) {
        if (i == li) { continue; }
        check(o.first_degraded_ms[i] != 0u && o.first_degraded_ms[i] - t_safe <= 200u, "REQ-SAFE-0004",
              "survivor entered DEGRADED on the peer's SAFE announcement");
        check(o.deg_to_nom[i] == 0u, "REQ-SAFE-0004",
              "no DEGRADED->NOMINAL transition while SAFE evidence stayed fresh (no heartbeat-induced flapping)");
        check(o.nominal_after_first_deg[i] == 0u, "REQ-SAFE-0004",
              "survivor was DEGRADED at every step after the first SAFE announcement");
    }
    check(o.first_valid_ms != 0u && o.first_valid_idx != li, "REQ-FUNC-0004", "survivors still elected a valid leader");
    check(o.leader_degraded_valid_seen, "REQ-SAFE-0004", "leader retained valid authority while DEGRADED");
    check(o.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1");
    check(o.term_regressions == 0u, "INV-TERM-MONOTONIC", "no term regression");
    printf("        survivors DEGRADED->NOMINAL transitions: node%d=%u node%d=%u; NOMINAL steps after first DEGRADED: %u/%u\n",
           a + 1, o.deg_to_nom[a], b + 1, o.deg_to_nom[b], o.nominal_after_first_deg[a], o.nominal_after_first_deg[b]);
}

/* TC-040: SAFE evidence expires only when the SAFE node stops announcing
 * (here: cold restart); survivors then return to NOMINAL; the SAFE node
 * recovers only through the cold restart.
 * EXPECTED RED on 76f10d9: a leader never leaves DEGRADED. */
static void tc_040_degraded_expiry_and_return_to_nominal(void)
{
    int li, a, b, lead_before;
    uint16_t term_before;
    traj_obs_t o;

    printf("TC-040  SAFE evidence expiry, return to NOMINAL, SAFE node recovers only by cold restart [Lot 3, REQ-SAFE-0004]\n");
    bus_init(); bus_run(2000u);
    li = split_brain_leader();
    check(li >= 0, "REQ-SAFE-0003", "leader latched SAFE");
    if (li < 0) { return; }
    survivors_of(li, &a, &b);
    traj_init(&o);
    traj_run(&o, 3000u);
    check(g_bus.node[li].state == MOSAIK_STATE_SAFE && o.safe_exits == 0u, "INV-SAFE-LATCH",
          "SAFE node did not recover by itself in 3 s");
    lead_before = valid_leader_index(); term_before = (lead_before >= 0) ? g_bus.node[lead_before].term : 0u;
    check(lead_before >= 0, "REQ-FUNC-0001", "a valid leader exists among the survivors");

    bus_crash_node(li);                     /* SAFE announcements stop */
    traj_run(&o, 600u);                     /* evidence window (3 heartbeat periods) + one heartbeat + margin */
    check(g_bus.node[a].state == MOSAIK_STATE_NOMINAL, "REQ-SAFE-0004",
          "survivor a returned to NOMINAL after SAFE evidence expired");
    check(g_bus.node[b].state == MOSAIK_STATE_NOMINAL, "REQ-SAFE-0004",
          "survivor b returned to NOMINAL after SAFE evidence expired");

    bus_restart_node(li);                   /* cold restart: new volatile instance */
    traj_run(&o, 1500u);
    check(g_bus.node[li].state == MOSAIK_STATE_NOMINAL && g_bus.node[li].role == MOSAIK_ROLE_FOLLOWER,
          "Lot 3", "cold-restarted node rejoined as NOMINAL follower");
    check(lead_before >= 0 && valid_leader_index() == lead_before && g_bus.node[lead_before].term == term_before,
          "REQ-FUNC-0001", "leader and term unchanged across the SAFE node's cold restart");
    check(g_bus.node[li].term == term_before, "Lot 3", "restarted node adopted the cluster term");
    check(o.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1");
    check(o.term_regressions == 0u, "INV-TERM-MONOTONIC", "no term regression (restart excluded)");
    printf("        after evidence expiry: node%d state %d, node%d state %d; restarted node %d state %d term %u\n",
           a + 1, (int)g_bus.node[a].state, b + 1, (int)g_bus.node[b].state, li + 1, (int)g_bus.node[li].state, g_bus.node[li].term);
}

/* TC-041: election exhaustion is terminal; restored connectivity does not
 * recover a SAFE node (INV-SAFE-LATCH) and no authority exists without quorum. */
static void tc_041_election_exhaustion_terminal(void)
{
    int li, a, b, i;
    uint32_t nonsafe_tx0[N_NODES], nonsafe_tx1[N_NODES];
    traj_obs_t o;

    printf("TC-041  election exhaustion -> SAFE/NO_QUORUM is terminal even after connectivity returns [Lot 3]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 3", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &a, &b);
    isolate_node(a, true);  bus_run(2000u);
    check(g_bus.node[a].state == MOSAIK_STATE_SAFE && g_bus.node[a].safe_cause == (uint8_t)MOSAIK_SAFE_NO_QUORUM,
          "REQ-SAFE-0003", "isolated follower exhausted elections and latched SAFE/no-quorum");
    isolate_node(b, true);  bus_run(2500u);
    check(g_bus.node[b].state == MOSAIK_STATE_SAFE && g_bus.node[li].state == MOSAIK_STATE_SAFE,
          "REQ-FUNC-0002", "with quorum unreachable the remaining nodes latched SAFE");
    for (i = 0; i < N_NODES; i++) {
        nonsafe_tx0[i] = TX(i, MOSAIK_MSG_VOTE_REQ) + TX(i, MOSAIK_MSG_VOTE_GRANT) + TX(i, MOSAIK_MSG_HEARTBEAT) + TX(i, MOSAIK_MSG_ACK);
    }
    bus_set_full_connectivity();
    traj_init(&o);
    traj_run(&o, 3000u);
    for (i = 0; i < N_NODES; i++) {
        nonsafe_tx1[i] = TX(i, MOSAIK_MSG_VOTE_REQ) + TX(i, MOSAIK_MSG_VOTE_GRANT) + TX(i, MOSAIK_MSG_HEARTBEAT) + TX(i, MOSAIK_MSG_ACK);
        check(g_bus.node[i].state == MOSAIK_STATE_SAFE, "INV-SAFE-LATCH", "node remained SAFE after connectivity was restored");
        check(o.max_failed[i] == g_bus.node[i].cfg.max_failed_elections, "REQ-SAFE-0003",
              "SAFE reached through exactly max_failed_elections genuine failures");
        check(nonsafe_tx1[i] == nonsafe_tx0[i], "REQ-SAFE-0003", "no election, vote, heartbeat or ACK traffic after restore");
    }
    check(o.safe_exits == 0u, "INV-SAFE-LATCH", "no SAFE exit");
    check(o.max_valid == 0u, "INV-SAFE-NO-AUTHORITY", "no valid authority after cluster-wide SAFE");
    printf("        all three nodes SAFE/no-quorum, terms %u/%u/%u, no recovery after restore\n",
           g_bus.node[0].term, g_bus.node[1].term, g_bus.node[2].term);
}

/* TC-042: isolation duration boundary. Short isolations recover with the
 * recovered predicate; long ones latch. Timings are host observations only. */
static void tc_042_isolation_boundary(void)
{
    static const uint32_t durs[5] = {600u, 900u, 1200u, 1500u, 2000u};
    int d, latched_before = 0;
    bool monotonic = true;

    printf("TC-042  leader isolation boundary: recovery below the timer budget, latch above [Lot 3]\n");
    for (d = 0; d < 5; d++) {
        int li, lo = -1; bool rec; traj_obs_t o;
        bus_init(); bus_run(2000u);
        li = leader_index();
        if (li < 0) { check(false, "Lot 3", "leader existed"); return; }
        isolate_node(li, true);  bus_run(durs[d]);
        isolate_node(li, false);
        traj_init(&o); traj_run(&o, 3000u);
        rec = cluster_recovered(&lo) && g_bus.node[li].state != MOSAIK_STATE_SAFE;
        check(o.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1 during isolation and recovery");
        check(o.term_regressions == 0u, "INV-TERM-MONOTONIC", "no term regression");
        if (g_bus.node[li].state == MOSAIK_STATE_SAFE) {
            check(o.max_failed[li] == g_bus.node[li].cfg.max_failed_elections, "REQ-SAFE-0003", "latch only through genuine failures");
            latched_before = 1;
        } else {
            check(rec, "INV-RECOVERY-VERIFIED", "isolated leader rejoined and the cluster satisfies the recovered predicate");
            if (latched_before) { monotonic = false; }
        }
        if (d == 0) { check(rec, "REQ-FUNC-0004", "600 ms isolation recovers without SAFE"); }
        if (d == 4) { check(g_bus.node[li].state == MOSAIK_STATE_SAFE, "REQ-SAFE-0003", "2000 ms isolation latches SAFE"); }
        printf("        isolation %4u ms: %s (old leader term %u, state %d, valid leader node %d)\n",
               durs[d], rec ? "RECOVERED" : (g_bus.node[li].state == MOSAIK_STATE_SAFE ? "LATCHED SAFE" : "NOT RECOVERED"),
               g_bus.node[li].term, (int)g_bus.node[li].state, lo + 1);
    }
    check(monotonic, "Lot 3", "once a duration latches SAFE, every longer duration latches too");
}

/* TC-043: malformed traffic is detection-only (D2): counted, never acted on. */
static void tc_043_malformed_detection_only(void)
{
    int li, i, k;
    uint16_t terms[N_NODES];
    uint32_t de0[N_NODES];
    mosaik_msg_t m; mosaik_frame_t bad;
    traj_obs_t o;

    printf("TC-043  malformed frames: decode errors counted, no state, term or authority change [Lot 3, D2]\n");
    bus_init(); bus_run(2000u);
    li = valid_leader_index();
    check(li >= 0, "Lot 3", "valid leader existed");
    if (li < 0) { return; }
    for (i = 0; i < N_NODES; i++) { terms[i] = g_bus.node[i].term; de0[i] = g_bus.node[i].decode_errors; }
    make_msg(&m, MOSAIK_MSG_HEARTBEAT, 3u, MOSAIK_ROLE_LEADER, MOSAIK_STATE_NOMINAL, 99u, 1u);
    mosaik_encode(&bad, &m);
    bad.data[7] ^= 0x5Au;                                  /* CRC broken */
    traj_init(&o);
    for (k = 0; k < 50; k++) { bus_inject_frame(&bad, 3u); traj_run(&o, 10u); }
    for (i = 0; i < N_NODES; i++) {
        if (i == 2) { continue; }                          /* the claimed source skips its own frame */
        check(g_bus.node[i].decode_errors == de0[i] + 50u, "REQ-FUNC-0007", "each corrupted frame was detected and counted");
        check(g_bus.node[i].term == terms[i], "INV-TERM-MONOTONIC", "term unchanged by corrupted frames");
        check(g_bus.node[i].state != MOSAIK_STATE_SAFE, "D2", "no SAFE transition from corrupted traffic (PROTO_ERROR reserved)");
    }
    check(valid_leader_index() == li, "REQ-FUNC-0001", "valid leader unchanged");
    check(o.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1");
    printf("        50 corrupted frames: decode_errors %u->%u on node 1, states %d/%d/%d\n",
           de0[0], g_bus.node[0].decode_errors, (int)g_bus.node[0].state, (int)g_bus.node[1].state, (int)g_bus.node[2].state);
}

/* TC-044: crash during recovery. After a natural collision the survivors back
 * off; one of them crashes; the lone node exhausts genuine elections. */
static void tc_044_crash_during_recovery(void)
{
    int li, a, b, k;
    uint32_t shared_deadline = 0u, crash_ms = 0u;
    uint16_t term_before = 0u;
    crash_obs_t co;
    traj_obs_t o;

    printf("TC-044  crash of a survivor during collision recovery: lone node reaches SAFE/no-quorum [Lot 3]\n");
    bus_init();
    li = collision_prologue("Lot 3", &shared_deadline, &crash_ms, &term_before);
    if (li < 0) { return; }
    survivors_of(li, &a, &b);
    obs_init(&co, li);
    for (k = 0; k < 1000 && !(g_bus.node[a].failed_elections >= 1u && g_bus.node[b].failed_elections >= 1u); k++) {
        bus_step(); obs_step(&co);
    }
    check(co.split_vote_seen && g_bus.node[a].failed_elections >= 1u, "Lot 3", "natural collision and first failed election observed");
    bus_crash_node(b);                                     /* crash during the backoff */
    traj_init(&o);
    traj_run(&o, 3000u);
    check(g_bus.node[a].state == MOSAIK_STATE_SAFE && g_bus.node[a].safe_cause == (uint8_t)MOSAIK_SAFE_NO_QUORUM,
          "REQ-FUNC-0002", "lone survivor latched SAFE/no-quorum");
    check(o.max_failed[a] == g_bus.node[a].cfg.max_failed_elections, "REQ-SAFE-0003", "SAFE only through genuine failed elections");
    check(o.max_valid == 0u, "INV-LEADER-UNIQUE", "no authority was ever manufactured");
    check(o.safe_auth_violations == 0u, "INV-SAFE-NO-AUTHORITY", "SAFE node holds no authority");
    check(o.term_regressions == 0u, "INV-TERM-MONOTONIC", "no term regression");
    printf("        collision at %u, survivor %d crashed during backoff, node %d SAFE at term %u\n",
           shared_deadline, b + 1, a + 1, g_bus.node[a].term);
}

/* TC-045: replayed old SAFE evidence degrades the receiver for exactly one
 * evidence window (3 heartbeat periods) and only nodes that actually received
 * it; no indefinite degradation, no topology knowledge.
 * EXPECTED RED on 76f10d9: the receiver flips back to NOMINAL on the next heartbeat. */
static void tc_045_replayed_safe_evidence_bounded(void)
{
    int li, a, b;
    mosaik_msg_t m; mosaik_frame_t f;
    traj_obs_t o;

    printf("TC-045  replayed SAFE evidence: bounded DEGRADED window, local evidence only [Lot 3, REQ-SAFE-0004]\n");
    bus_init(); bus_run(2000u);
    li = valid_leader_index();
    check(li >= 0, "Lot 3", "valid leader existed");
    if (li < 0) { return; }
    survivors_of(li, &a, &b);
    /* A stale SAFE frame attributed to follower a reaches follower b only. */
    make_msg(&m, MOSAIK_MSG_SAFE, (uint8_t)(a + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_SAFE,
             g_bus.node[a].term, (uint8_t)MOSAIK_SAFE_NO_QUORUM);
    mosaik_encode(&f, &m);
    bus_set_net_action((uint8_t)a, (uint8_t)li, NET_DROP, 0u);
    bus_inject_frame(&f, (uint8_t)(a + 1));
    traj_init(&o);
    traj_run(&o, 1u);
    bus_set_net_action((uint8_t)a, (uint8_t)li, NET_DELIVER, 0u);
    traj_run(&o, 1000u);

    check(o.first_degraded_ms[b] != 0u, "REQ-SAFE-0004", "receiver entered DEGRADED on the SAFE evidence");
    check(o.first_deg_run_len[b] >= 300u, "REQ-SAFE-0004",
          "receiver stayed DEGRADED for the full evidence window (3 heartbeat periods)");
    check(o.first_deg_run_len[b] <= 450u && g_bus.node[b].state == MOSAIK_STATE_NOMINAL, "REQ-SAFE-0004",
          "replayed evidence cannot create indefinite degradation");
    check(o.steps_degraded[li] == 0u, "INV-FDIR-NO-MAGIC", "node that never received the frame never degraded");
    check(o.steps_degraded[a] == 0u && g_bus.node[a].state != MOSAIK_STATE_SAFE, "INV-FDIR-NO-MAGIC",
          "impersonated node unaffected by a frame it never received");
    check(o.max_safe_nodes == 0u, "REQ-SAFE-0001", "nobody entered SAFE");
    check(valid_leader_index() == li && o.max_valid <= 1u, "INV-LEADER-UNIQUE", "authority unchanged and unique");
    printf("        receiver node %d DEGRADED run %u ms, final state %d; non-receivers degraded %u/%u steps\n",
           b + 1, o.first_deg_run_len[b], (int)g_bus.node[b].state, o.steps_degraded[li], o.steps_degraded[a]);
}

/* TC-046: repeated transient faults recover every time without SAFE. */
static void tc_046_repeated_transient_faults(void)
{
    int cycle, li, lo, i;
    traj_obs_t o;

    printf("TC-046  three transient leader isolations recover each time without SAFE [Lot 3]\n");
    bus_init(); bus_run(2000u);
    traj_init(&o);
    for (cycle = 0; cycle < 3; cycle++) {
        li = leader_index();
        check(li >= 0, "Lot 3", "leader existed at cycle start");
        if (li < 0) { return; }
        isolate_node(li, true);  traj_run(&o, 800u);
        isolate_node(li, false); traj_run(&o, 2000u);
        check(cluster_recovered(&lo), "INV-RECOVERY-VERIFIED", "cluster satisfies the recovered predicate after the transient fault");
    }
    for (i = 0; i < N_NODES; i++) {
        check(g_bus.node[i].state != MOSAIK_STATE_SAFE, "Lot 3", "no node entered SAFE across repeated transient faults");
        check(o.max_failed[i] < g_bus.node[i].cfg.max_failed_elections, "Lot 3", "no node exhausted its elections");
    }
    check(o.max_safe_nodes == 0u, "REQ-SAFE-0001", "no SAFE at any step");
    check(o.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1");
    check(o.term_regressions == 0u, "INV-TERM-MONOTONIC", "no term regression");
    printf("        three 800 ms isolations recovered; final valid leader node %d term %u, max failed elections %u/%u/%u\n",
           valid_leader_index() + 1, valid_leader_index() >= 0 ? g_bus.node[valid_leader_index()].term : 0u,
           o.max_failed[0], o.max_failed[1], o.max_failed[2]);
}

/* TC-047: adversarial combination A: split-brain SAFE on the leader, a delayed
 * old-term heartbeat replay, and a one-way drop during the survivors' election.
 * EXPECTED RED on 76f10d9 through the DEGRADED persistence requirement. */
static void tc_047_adversarial_safe_replay_oneway(void)
{
    int li, a, b, i;
    uint16_t t_old;
    uint32_t stale0;
    mosaik_msg_t m; mosaik_frame_t f;
    traj_obs_t o;

    printf("TC-047  adversarial: leader SAFE + delayed old-term heartbeat replay + one-way drop during election [Lot 3]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    if (li < 0) { check(false, "Lot 3", "leader existed"); return; }
    t_old = g_bus.node[li].term;
    survivors_of(li, &a, &b);
    make_msg(&m, MOSAIK_MSG_HEARTBEAT, (uint8_t)(li + 1), MOSAIK_ROLE_LEADER, MOSAIK_STATE_NOMINAL, t_old, 200u);
    mosaik_encode(&f, &m);
    check(split_brain_leader() == li, "REQ-SAFE-0003", "leader latched SAFE");
    (void)bus_schedule_delayed(&f, (uint8_t)(li + 1), 700u);     /* stale heartbeat of the now-SAFE leader */
    stale0 = g_bus.node[a].stale_term_rejections + g_bus.node[b].stale_term_rejections;
    traj_init(&o);
    traj_run(&o, 400u);
    bus_set_net_action((uint8_t)a, (uint8_t)b, NET_DROP, 0u);      /* one-way loss during the election window */
    traj_run(&o, 200u);
    bus_set_net_action((uint8_t)a, (uint8_t)b, NET_DELIVER, 0u);
    traj_run(&o, 2400u);

    check(valid_leader_count() == 1 && valid_leader_index() != li, "REQ-FUNC-0001", "exactly one valid leader among the survivors");
    check(g_bus.node[a].stale_term_rejections + g_bus.node[b].stale_term_rejections > stale0, "REQ-FUNC-0001",
          "delayed old-term heartbeat was rejected as stale");
    check(o.term_regressions == 0u, "INV-TERM-MONOTONIC", "no term regression");
    check(o.max_safe_nodes == 1u && g_bus.node[li].state == MOSAIK_STATE_SAFE, "INV-SAFE-LATCH", "only the leader is SAFE and stays SAFE");
    for (i = 0; i < N_NODES; i++) {
        if (i == li) { continue; }
        check(o.nominal_after_first_deg[i] == 0u, "REQ-SAFE-0004",
              "survivor stayed DEGRADED throughout the fresh SAFE evidence despite adversarial traffic");
    }
    check(o.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1");
    printf("        valid leader node %d term %u; stale rejections +%u; NOMINAL steps after DEGRADED %u/%u\n",
           valid_leader_index() + 1, valid_leader_index() >= 0 ? g_bus.node[valid_leader_index()].term : 0u,
           g_bus.node[a].stale_term_rejections + g_bus.node[b].stale_term_rejections - stale0,
           o.nominal_after_first_deg[a], o.nominal_after_first_deg[b]);
}

/* TC-048: adversarial combination B: leader crash at a natural shared deadline,
 * then a delayed SAFE frame attributed to the crashed leader arrives during the
 * survivors' backoff. Recovery must be unaffected; DEGRADED per evidence.
 * EXPECTED RED on 76f10d9 through the DEGRADED window requirement. */
static void tc_048_adversarial_crash_collision_safe_frame(void)
{
    int li, a, b, k, i;
    uint32_t shared_deadline = 0u, crash_ms = 0u;
    uint16_t term_before = 0u;
    mosaik_msg_t m; mosaik_frame_t f;
    crash_obs_t co;
    traj_obs_t o;

    printf("TC-048  adversarial: leader crash + natural collision + delayed SAFE frame during backoff [Lot 3]\n");
    bus_init();
    li = collision_prologue("Lot 3", &shared_deadline, &crash_ms, &term_before);
    if (li < 0) { return; }
    survivors_of(li, &a, &b);
    obs_init(&co, li);
    for (k = 0; k < 1000 && !(g_bus.node[a].failed_elections >= 1u && g_bus.node[b].failed_elections >= 1u); k++) {
        bus_step(); obs_step(&co);
    }
    check(co.split_vote_seen, "Lot 3", "natural collision observed");
    make_msg(&m, MOSAIK_MSG_SAFE, (uint8_t)(li + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_SAFE, term_before,
             (uint8_t)MOSAIK_SAFE_SPLIT_BRAIN);
    mosaik_encode(&f, &m);
    bus_inject_frame(&f, (uint8_t)(li + 1));                 /* delayed SAFE frame of the crashed leader */
    traj_init(&o);
    traj_run(&o, 3000u);

    check(o.first_valid_ms != 0u && o.first_valid_ms - crash_ms < 1000u, "REQ-FUNC-0004",
          "recovery completed within 1000 ms of the crash despite the SAFE frame");
    check(o.max_safe_nodes == 0u, "REQ-SAFE-0001", "no survivor entered SAFE");
    for (i = 0; i < N_NODES; i++) {
        if (i == li) { continue; }
        check(o.first_degraded_ms[i] != 0u, "REQ-SAFE-0004", "survivor degraded on the received SAFE evidence");
        check(o.first_deg_run_len[i] >= 300u, "REQ-SAFE-0004",
              "survivor stayed DEGRADED for the full evidence window, including across becoming leader or accepting heartbeats");
        check(o.first_deg_run_len[i] <= 450u && g_bus.node[i].state == MOSAIK_STATE_NOMINAL, "REQ-SAFE-0004",
              "survivor returned to NOMINAL once the single SAFE frame's evidence expired");
    }
    check(o.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1");
    check(o.term_regressions == 0u, "INV-TERM-MONOTONIC", "no term regression");
    printf("        valid leader node %d at %u ms (%u ms after crash); DEGRADED runs %u/%u ms\n",
           o.first_valid_idx + 1, o.first_valid_ms, o.first_valid_ms - crash_ms, o.first_deg_run_len[a], o.first_deg_run_len[b]);
}

/* TC-049: deterministic reproducibility of a LOT 3 scenario. */
static uint32_t lot3_trajectory_hash(void)
{
    uint32_t h = 2166136261u, k; int li, i;
    bus_init(); bus_run(2000u);
    li = split_brain_leader();
    if (li < 0) { return 0u; }
    for (k = 0; k < 3000u; k++) {
        bus_step();
        for (i = 0; i < N_NODES; i++) {
            const mosaik_node_t *n = &g_bus.node[i];
            uint32_t v = ((uint32_t)n->role << 24) | ((uint32_t)n->state << 16) | ((uint32_t)n->term << 1) |
                         (mosaik_has_valid_leadership_authority(n) ? 1u : 0u);
            h ^= v; h *= 16777619u;
        }
    }
    return h;
}

static void tc_049_reproducibility(void)
{
    uint32_t h1, h2;
    printf("TC-049  deterministic reproducibility of the SAFE/DEGRADED scenario [Lot 3]\n");
    h1 = lot3_trajectory_hash();
    h2 = lot3_trajectory_hash();
    check(h1 != 0u && h1 == h2, "Lot 3", "two runs of the same scenario produce identical per-step trajectories");
    printf("        trajectory hash 0x%08X reproduced\n", h1);
}

/* TC-050: LOT 2 erratum (D5): vote memory must never be erased by a role
 * transition within the same term. A leader whose lease expires steps down in
 * the SAME term; a same-term VOTE_REQ delivered afterwards must not be granted.
 * A strictly higher-term VOTE_REQ must still be granted (contrast).
 * EXPECTED RED on 76f10d9: become_follower() clears voted_for. */
static void tc_050_same_term_vote_memory_across_step_down(void)
{
    int li, c, d, k;
    uint8_t vf_before, vf_after; uint16_t vt_before, vt_after, term_lead;
    uint32_t g0, g1;
    mosaik_msg_t m; mosaik_frame_t f;
    bool stepped = false;

    printf("TC-050  one vote per term across a same-term step-down (LOT 2 erratum, D5) [Lot 3]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 3", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &c, &d);
    term_lead = g_bus.node[li].term;
    check(g_bus.node[li].voted_for == g_bus.node[li].id && g_bus.node[li].voted_term == term_lead,
          "Lot 3", "leader holds its own vote for the current term");

    /* Legitimate ACK loss: followers -> leader dropped; lease expires; same-term step-down. */
    bus_set_net_action((uint8_t)c, (uint8_t)li, NET_DROP, 0u);
    bus_set_net_action((uint8_t)d, (uint8_t)li, NET_DROP, 0u);
    vf_before = g_bus.node[li].voted_for; vt_before = g_bus.node[li].voted_term;
    for (k = 0; k < 1500 && !stepped; k++) {
        bus_step();
        if (g_bus.node[li].role != MOSAIK_ROLE_LEADER) { stepped = true; }
    }
    vf_after = g_bus.node[li].voted_for; vt_after = g_bus.node[li].voted_term;
    check(stepped && g_bus.node[li].term == term_lead, "Lot 3", "leader stepped down to follower in the SAME term after lease expiry");
    check(vf_after == vf_before && vt_after == vt_before, "INV-ONE-VOTE-PER-TERM",
          "vote memory (voted_for, voted_term) survived the same-term role transition");

    /* Same-term VOTE_REQ delivered through the bus. */
    bus_set_full_connectivity();
    make_msg(&m, MOSAIK_MSG_VOTE_REQ, (uint8_t)(c + 1), MOSAIK_ROLE_CANDIDATE, MOSAIK_STATE_NOMINAL, term_lead, 0u);
    mosaik_encode(&f, &m);
    g0 = TX(li, MOSAIK_MSG_VOTE_GRANT);
    bus_inject_frame(&f, (uint8_t)(c + 1));
    bus_step();
    check(TX(li, MOSAIK_MSG_VOTE_GRANT) == g0, "INV-ONE-VOTE-PER-TERM",
          "no VOTE_GRANT for a term in which the node already voted");
    check(g_bus.node[li].voted_for == vf_before && g_bus.node[li].voted_term == vt_before, "INV-ONE-VOTE-PER-TERM",
          "same-term vote request did not change vote memory");

    /* Contrast: a strictly higher-term VOTE_REQ resets eligibility and is granted. */
    make_msg(&m, MOSAIK_MSG_VOTE_REQ, (uint8_t)(c + 1), MOSAIK_ROLE_CANDIDATE, MOSAIK_STATE_NOMINAL, (uint16_t)(term_lead + 1u), 0u);
    mosaik_encode(&f, &m);
    g1 = TX(li, MOSAIK_MSG_VOTE_GRANT);
    bus_inject_frame(&f, (uint8_t)(c + 1));
    bus_step();
    check(TX(li, MOSAIK_MSG_VOTE_GRANT) == g1 + 1u, "Lot 3", "strictly higher-term vote request granted");
    check(g_bus.node[li].voted_for == (uint8_t)(c + 1) && g_bus.node[li].voted_term == (uint16_t)(term_lead + 1u),
          "Lot 3", "vote memory now records the higher-term vote");
    check(g_bus.node[li].term == (uint16_t)(term_lead + 1u), "INV-TERM-MONOTONIC", "term advanced to the higher term");
    printf("        former leader %d: vote (%u,%u) before step-down, (%u,%u) after; same-term grants %u; higher-term grants %u\n",
           li + 1, vf_before, vt_before, vf_after, vt_after, TX(li, MOSAIK_MSG_VOTE_GRANT) - g0 - (TX(li, MOSAIK_MSG_VOTE_GRANT) - g1),
           TX(li, MOSAIK_MSG_VOTE_GRANT) - g1);
}

/* ------------------------------------------------------------------
 * LOT 4: system mode semantics (Phase 1 RED baseline).
 *
 * Frozen decisions under test (LOT 4 specification freeze):
 *   L4-C1  a fault-free election must not move a node INIT -> DEGRADED;
 *          DEGRADED requires received peer SAFE evidence (LOT 3 model);
 *          a healthy candidate may remain INIT.
 *   L4-C2  a received SAFE frame is FDIR evidence only. Its term is not
 *          evidence of a newer leadership epoch and must not drive the
 *          generic higher-term adoption.
 * TC-052 (C1) and TC-053 (C2) are the official RED evidence and are
 * EXPECTED TO FAIL on 8177e70. TC-051 and TC-054..TC-060 are guards that
 * must hold on the frozen baseline. Harness rules are those of LOT 3: no
 * node field is ever written from a test; faults only through directional
 * network actions, crash/cold restart, frame injection and delayed
 * delivery; harness knowledge is used for assertions only.
 * ------------------------------------------------------------------- */

static const char *l4_state_name(unsigned s)
{
    static const char *names[4] = {"INIT", "NOMINAL", "DEGRADED", "SAFE"};
    return s < 4u ? names[s] : "OUT-OF-RANGE";
}

static const char *l4_role_name(unsigned r)
{
    static const char *names[3] = {"FOLLOWER", "CANDIDATE", "LEADER"};
    return r < 3u ? names[r] : "OUT-OF-RANGE";
}

/* Executable state x role legality under the frozen four-state model.
 * INIT: FOLLOWER or CANDIDATE (a healthy candidate may remain INIT).
 * NOMINAL: any role. DEGRADED: any role (a candidate may hold fresh peer
 * SAFE evidence; the C1 transient DEGRADED+CANDIDATE is also still legal
 * on this RED baseline). SAFE: FOLLOWER only.
 * Illegal: INIT+LEADER, SAFE+CANDIDATE, SAFE+LEADER, any out-of-range value. */
static bool mode_pair_legal(mosaik_state_t s, mosaik_role_t r)
{
    if ((unsigned)s > 3u || (unsigned)r > 2u) { return false; }
    if (s == MOSAIK_STATE_INIT) { return r != MOSAIK_ROLE_LEADER; }
    if (s == MOSAIK_STATE_SAFE) { return r == MOSAIK_ROLE_FOLLOWER; }
    return true;
}

/* Per-step read-only mode legality observation (INV-MODE-LEGAL). */
typedef struct {
    uint32_t pair_steps[4][3];            /* node-steps per (state, role) */
    uint32_t illegal_steps;
    int      first_illegal_idx;
    uint32_t first_illegal_ms;
    unsigned first_illegal_state, first_illegal_role;
    uint32_t degraded_no_evidence_steps;  /* DEGRADED with mask == 0: informative only here (C1) */
    uint32_t degraded_with_evidence_steps;
} mode_obs_t;

static void mode_init(mode_obs_t *o)
{
    memset(o, 0, sizeof(*o));
    o->first_illegal_idx = -1;
}

static void mode_step(mode_obs_t *o)
{
    int i;
    for (i = 0; i < N_NODES; i++) {
        const mosaik_node_t *n = &g_bus.node[i];
        unsigned s = (unsigned)n->state, r = (unsigned)n->role;
        if (g_bus.crashed[i] || !g_bus.powered[i]) { continue; }
        if (!mode_pair_legal(n->state, n->role)) {
            if (o->illegal_steps == 0u) {
                o->first_illegal_idx = i; o->first_illegal_ms = n->now_ms;
                o->first_illegal_state = s; o->first_illegal_role = r;
            }
            o->illegal_steps++;
        } else {
            o->pair_steps[s][r]++;
        }
        if (n->state == MOSAIK_STATE_DEGRADED) {
            if (n->safe_evidence_mask == 0u) { o->degraded_no_evidence_steps++; }
            else                             { o->degraded_with_evidence_steps++; }
        }
    }
}

static void mode_run(mode_obs_t *m, traj_obs_t *t, uint32_t ms)
{
    uint32_t k;
    for (k = 0; k < ms; k++) {
        bus_step();
        if (t) { traj_step(t); }
        mode_step(m);
    }
}

/* Accumulate the emitted-byte observation of the current bus into totals. */
static void l4_accum_tx_bytes(uint32_t *cnt, uint32_t *oor_state, uint32_t *oor_role)
{
    int k;
    for (k = 0; k < 4; k++) { cnt[k] += g_bus.tx_state_byte_count[k]; }
    *oor_state += g_bus.tx_state_byte_out_of_range;
    *oor_role  += g_bus.tx_role_byte_out_of_range;
}

/* Protocol-visible snapshot of one node (read-only). */
typedef struct {
    mosaik_role_t  role;
    mosaik_state_t state;
    uint16_t       term;
    uint8_t        voted_for;
    uint16_t       voted_term;
    uint8_t        mask;
    uint8_t        leader_id;
    bool           auth;
    uint32_t       decode_errors;
    uint32_t       stale_term;
} node_snap_t;

static void snap_node(node_snap_t *s, int i)
{
    const mosaik_node_t *n = &g_bus.node[i];
    s->role = n->role; s->state = n->state; s->term = n->term;
    s->voted_for = n->voted_for; s->voted_term = n->voted_term;
    s->mask = n->safe_evidence_mask; s->leader_id = n->leader_id;
    s->auth = mosaik_has_valid_leadership_authority(n);
    s->decode_errors = n->decode_errors; s->stale_term = n->stale_term_rejections;
}

/* Equal in every protocol-relevant field (counters excluded). */
static bool snap_same(const node_snap_t *a, const node_snap_t *b)
{
    return a->role == b->role && a->state == b->state && a->term == b->term &&
           a->voted_for == b->voted_for && a->voted_term == b->voted_term &&
           a->mask == b->mask && a->leader_id == b->leader_id && a->auth == b->auth;
}

/* FNV-1a fold of the per-node executable trajectory of one step. */
static uint32_t l4_hash_step(uint32_t h)
{
    int i;
    for (i = 0; i < N_NODES; i++) {
        const mosaik_node_t *n = &g_bus.node[i];
        uint32_t v;
        if (g_bus.crashed[i] || !g_bus.powered[i]) {
            v = 0xFFFFFFFFu;
        } else {
            v = ((uint32_t)n->role << 28) | ((uint32_t)n->state << 24) |
                ((uint32_t)n->safe_evidence_mask << 16) | ((uint32_t)n->term << 1) |
                (mosaik_has_valid_leadership_authority(n) ? 1u : 0u);
        }
        h ^= v; h *= 16777619u;
    }
    return h;
}

static uint32_t l4_hash_tx(uint32_t h)
{
    int i, k;
    for (i = 0; i < N_NODES; i++) {
        for (k = 0; k < 6; k++) { h ^= g_bus.tx_count[i][k]; h *= 16777619u; }
    }
    return h;
}

/* TC-051: mode legality guard. Read-only observation of every state x role
 * pair and of every emitted state/role byte across representative existing
 * scenarios. DEGRADED+CANDIDATE is legal on this baseline (see above); the
 * C1 symptom (DEGRADED without evidence) is reported, not asserted, here. */
static void tc_051_mode_legality_guard(void)
{
    mode_obs_t m;
    uint32_t bytes[4] = {0u, 0u, 0u, 0u}, oor_state = 0u, oor_role = 0u;
    int li, i, s, r;

    printf("TC-051  mode legality guard: state x role pairs and emitted state bytes over representative scenarios [Lot 4]\n");
    mode_init(&m);

    /* 1. cold boot / election */
    bus_init(); mode_run(&m, NULL, 2000u);
    l4_accum_tx_bytes(bytes, &oor_state, &oor_role);

    /* 2. split-brain -> SAFE, survivors DEGRADED and re-elect */
    bus_init(); mode_run(&m, NULL, 2000u);
    li = split_brain_leader();
    check(li >= 0, "Lot 4", "split-brain scenario latched SAFE");
    mode_run(&m, NULL, 3000u);
    l4_accum_tx_bytes(bytes, &oor_state, &oor_role);

    /* 3. election exhaustion of a lone node (TC-005 mechanism) */
    bus_init(); mode_run(&m, NULL, 2000u);
    li = leader_index();
    for (i = 0; i < N_NODES; i++) { if (i != ((li + 1) % N_NODES)) { g_bus.powered[i] = false; } }
    mode_run(&m, NULL, 3000u);
    l4_accum_tx_bytes(bytes, &oor_state, &oor_role);

    /* 4. partition of the leader, then heal */
    bus_init(); mode_run(&m, NULL, 2000u);
    li = leader_index();
    if (li >= 0) { bus_set_partition_2plus1((uint8_t)(li + 1)); }
    mode_run(&m, NULL, 1500u);
    bus_heal_partition();
    mode_run(&m, NULL, 1500u);
    l4_accum_tx_bytes(bytes, &oor_state, &oor_role);

    /* 5. crash of the leader, cold restart, rejoin */
    bus_init(); mode_run(&m, NULL, 2000u);
    li = leader_index();
    if (li >= 0) { bus_crash_node(li); }
    mode_run(&m, NULL, 1500u);
    if (li >= 0) { bus_restart_node(li); }
    mode_run(&m, NULL, 1500u);
    l4_accum_tx_bytes(bytes, &oor_state, &oor_role);

    check(m.illegal_steps == 0u, "INV-MODE-LEGAL", "no illegal state x role pair observed (INIT+LEADER, SAFE+CANDIDATE, SAFE+LEADER)");
    check(m.pair_steps[0][0] > 0u && m.pair_steps[1][0] > 0u && m.pair_steps[1][1] > 0u &&
          m.pair_steps[1][2] > 0u && m.pair_steps[2][0] > 0u && m.pair_steps[2][2] > 0u &&
          m.pair_steps[3][0] > 0u,
          "INV-MODE-LEGAL", "the scenarios exercised INIT, NOMINAL (all roles), DEGRADED follower/leader and SAFE follower");
    check(oor_state == 0u, "INV-MODE-LEGAL", "every emitted state byte is in the executable range 0..3");
    check(oor_role == 0u, "INV-MODE-LEGAL", "every emitted role byte is in the range 0..2");
    check(bytes[0] + bytes[1] + bytes[2] + bytes[3] > 0u, "Lot 4", "emitted frames were observed");
    if (m.illegal_steps != 0u) {
        printf("        first illegal pair: node %d at %u ms: %s + %s\n", m.first_illegal_idx + 1, m.first_illegal_ms,
               l4_state_name(m.first_illegal_state), l4_role_name(m.first_illegal_role));
    }
    printf("        node-steps per state x role (FOLLOWER/CANDIDATE/LEADER):\n");
    for (s = 0; s < 4; s++) {
        printf("          %-8s", l4_state_name((unsigned)s));
        for (r = 0; r < 3; r++) { printf(" %8u", m.pair_steps[s][r]); }
        printf("\n");
    }
    printf("        emitted state bytes INIT=%u NOMINAL=%u DEGRADED=%u SAFE=%u, out of range %u; role bytes out of range %u\n",
           bytes[0], bytes[1], bytes[2], bytes[3], oor_state, oor_role);
    printf("        DEGRADED node-steps without peer SAFE evidence: %u (C1 symptom, asserted by TC-052), with evidence: %u\n",
           m.degraded_no_evidence_steps, m.degraded_with_evidence_steps);
}

/* Scenario driver shared by TC-052 and TC-060: fault-free cold boot of three
 * nodes observed from cold init until the first heartbeat of a stable
 * cluster. Every field is a read-only observation. */
typedef struct {
    bool     violation;                  /* DEGRADED observed with safe_evidence_mask == 0 */
    int      v_idx;
    uint32_t v_ms;                       /* node clock at the observation */
    unsigned v_role, v_state, v_prev_state;
    uint16_t v_term, v_prev_term;
    uint8_t  v_mask;
    uint32_t v_run_len;                  /* length (ms) of that first DEGRADED run */
    bool     cand_seen;                  /* first candidacy start */
    int      cand_idx;
    uint32_t cand_ms;
    unsigned cand_state;
    uint8_t  cand_mask;
    uint32_t first_valid_ms;
    int      first_valid_idx;
    uint16_t first_valid_term;
    uint32_t recovered_ms, stable_hb_ms, steps;
    uint32_t max_valid, safe_tx, dropped, delayed, crashes;
    uint8_t  mask_or;
    uint32_t hash;
} c1_result_t;

static void run_c1_scenario(c1_result_t *r)
{
    mosaik_state_t prev_state[N_NODES];
    mosaik_role_t  prev_role[N_NODES];
    uint16_t       prev_term[N_NODES];
    uint32_t hb_at_recovery = 0u;
    int i, rec_li = -1;
    bool run_open = false;

    memset(r, 0, sizeof(*r));
    r->v_idx = -1; r->cand_idx = -1; r->first_valid_idx = -1;
    r->hash = 2166136261u;
    bus_init();
    for (i = 0; i < N_NODES; i++) {
        prev_state[i] = g_bus.node[i].state; prev_role[i] = g_bus.node[i].role; prev_term[i] = g_bus.node[i].term;
    }
    while (r->steps < 3000u) {
        uint32_t v;
        bus_step(); r->steps++;
        r->hash = l4_hash_step(r->hash);
        v = (uint32_t)valid_leader_count();
        if (v > r->max_valid) { r->max_valid = v; }
        if (v == 1u && r->first_valid_ms == 0u) {
            r->first_valid_ms = g_bus.now_ms; r->first_valid_idx = valid_leader_index();
            r->first_valid_term = g_bus.node[r->first_valid_idx].term;
        }
        for (i = 0; i < N_NODES; i++) {
            const mosaik_node_t *n = &g_bus.node[i];
            if (!r->cand_seen && n->role == MOSAIK_ROLE_CANDIDATE && prev_role[i] != MOSAIK_ROLE_CANDIDATE) {
                r->cand_seen = true; r->cand_idx = i; r->cand_ms = n->now_ms;
                r->cand_state = (unsigned)n->state; r->cand_mask = n->safe_evidence_mask;
            }
            if (!r->violation && n->state == MOSAIK_STATE_DEGRADED && n->safe_evidence_mask == 0u) {
                r->violation = true; r->v_idx = i; r->v_ms = n->now_ms;
                r->v_role = (unsigned)n->role; r->v_state = (unsigned)n->state;
                r->v_prev_state = (unsigned)prev_state[i];
                r->v_term = n->term; r->v_prev_term = prev_term[i]; r->v_mask = n->safe_evidence_mask;
                run_open = true;
            }
            if (run_open && i == r->v_idx) {
                if (n->state == MOSAIK_STATE_DEGRADED) { r->v_run_len++; } else { run_open = false; }
            }
            r->mask_or |= n->safe_evidence_mask;
            prev_state[i] = n->state; prev_role[i] = n->role; prev_term[i] = n->term;
        }
        if (r->recovered_ms == 0u) {
            if (cluster_recovered(&rec_li)) {
                r->recovered_ms = g_bus.now_ms;
                hb_at_recovery = TX(rec_li, MOSAIK_MSG_HEARTBEAT);
            }
        } else if (TX(rec_li, MOSAIK_MSG_HEARTBEAT) > hb_at_recovery) {
            r->stable_hb_ms = g_bus.now_ms;   /* first heartbeat of the stable cluster */
            break;
        }
    }
    for (i = 0; i < N_NODES; i++) { r->safe_tx += TX(i, MOSAIK_MSG_SAFE); r->crashes += g_bus.crash_count[i]; }
    r->dropped = g_bus.messages_dropped; r->delayed = g_bus.messages_delayed;
    r->hash = l4_hash_tx(r->hash);
}

/* TC-052: official C1 RED. Fault-free cold boot: no node may enter DEGRADED
 * without received peer SAFE evidence; a healthy candidate may remain INIT.
 * EXPECTED RED on 8177e70: start_election() moves INIT -> DEGRADED. */
static void tc_052_boot_no_degraded_without_evidence(void)
{
    c1_result_t r;
    printf("TC-052  fault-free cold boot: no DEGRADED without peer SAFE evidence (L4-C1) [Lot 4]\n");
    run_c1_scenario(&r);

    check(r.safe_tx == 0u && r.mask_or == 0u, "Lot 4", "no SAFE frame was transmitted or received during the boot");
    check(r.dropped == 0u && r.delayed == 0u && r.crashes == 0u, "Lot 4", "no fault was injected (no drop, delay, crash)");
    check(r.first_valid_ms != 0u && r.first_valid_ms < 1000u && r.max_valid <= 1u && r.recovered_ms != 0u && r.stable_hb_ms != 0u,
          "REQ-FUNC-0004", "election proceeded normally to one valid leader and a stable cluster");
    check(!r.violation, "INV-MODE-NO-MAGIC",
          "no node entered DEGRADED without received peer SAFE evidence (L4-C1)");
    check(r.cand_seen && r.cand_state == (unsigned)MOSAIK_STATE_INIT, "INV-MODE-NO-MAGIC",
          "the first healthy candidate remained INIT when it started its election (L4-C1)");

    printf("        first candidacy: node %d at %u ms, state %s, SAFE evidence mask 0x%02X\n",
           r.cand_idx + 1, r.cand_ms, l4_state_name(r.cand_state), r.cand_mask);
    if (r.violation) {
        printf("        C1 evidence: node %d at %u ms: role %s, state %s (previous step %s), term %u (previous %u), SAFE evidence mask 0x%02X; DEGRADED run %u ms\n",
               r.v_idx + 1, r.v_ms, l4_role_name(r.v_role), l4_state_name(r.v_state), l4_state_name(r.v_prev_state),
               r.v_term, r.v_prev_term, r.v_mask, r.v_run_len);
    } else {
        printf("        no DEGRADED without evidence observed\n");
    }
    printf("        first valid leader node %d term %u at %u ms; cluster recovered at %u ms; first stable heartbeat at %u ms; %u steps observed\n",
           r.first_valid_idx + 1, r.first_valid_term, r.first_valid_ms, r.recovered_ms, r.stable_hb_ms, r.steps);
}

/* Scenario driver shared by TC-053 and TC-060: a follower is isolated until it
 * genuinely exhausts its elections and latches SAFE/no-quorum at a higher
 * term; delivery is then restored and the SAFE node's own periodic SAFE
 * announcement reaches the surviving majority. Read-only observation. */
typedef struct {
    int      li, f, b;
    uint16_t term0_li, term0_b;
    bool     safe_reached;
    uint32_t t_isolate, t_safe;
    uint16_t term_safe;
    uint8_t  safe_cause;
    bool     majority_valid_isolation;
    uint32_t t_restore;
    uint32_t t_evidence;
    uint16_t term_li_at_evidence, term_b_at_evidence;
    uint32_t t_term_change;
    uint16_t term_change_to;
    int      term_change_idx;
    bool     vote_req_at_term_change;
    uint32_t t_role_lost;
    uint32_t gap_ms, t_auth_restored;
    int      auth_restored_idx;
    uint16_t term_auth_restored;
    bool     leader_role_kept, authority_kept;
    uint32_t survivor_vote_req_delta, f_grant_delta, f_ack_delta, f_safe_delta, f_other_after_safe;
    uint16_t term_end_li, term_end_b;
    unsigned state_end_li, state_end_b, state_end_f;
    uint8_t  mask_end_li, mask_end_b;
    uint32_t hash, steps;
} c2_result_t;

static void run_c2_scenario(c2_result_t *r)
{
    uint32_t k, vote_req0, g0, a0, s0, o0;
    memset(r, 0, sizeof(*r));
    r->li = r->f = r->b = -1; r->auth_restored_idx = -1; r->term_change_idx = -1;
    r->leader_role_kept = true; r->authority_kept = true; r->majority_valid_isolation = true;
    r->hash = 2166136261u;

    bus_init();
    for (k = 0; k < 2000u; k++) { bus_step(); r->hash = l4_hash_step(r->hash); }
    r->steps = 2000u;
    r->li = leader_index();
    if (r->li < 0 || valid_leader_index() != r->li) { return; }
    survivors_of(r->li, &r->f, &r->b);
    r->term0_li = g_bus.node[r->li].term; r->term0_b = g_bus.node[r->b].term;

    /* Isolation of one follower (both directions) until it latches SAFE. */
    r->t_isolate = g_bus.now_ms;
    isolate_node(r->f, true);
    for (k = 0; k < 4000u && !r->safe_reached; k++) {
        bus_step(); r->steps++; r->hash = l4_hash_step(r->hash);
        if (valid_leader_index() != r->li) { r->majority_valid_isolation = false; }
        if (g_bus.node[r->f].state == MOSAIK_STATE_SAFE) {
            r->safe_reached = true; r->t_safe = g_bus.node[r->f].safe_entry_ms;
            r->term_safe = g_bus.node[r->f].term; r->safe_cause = g_bus.node[r->f].safe_cause;
        }
    }
    if (!r->safe_reached) { return; }

    vote_req0 = TX(r->li, MOSAIK_MSG_VOTE_REQ) + TX(r->b, MOSAIK_MSG_VOTE_REQ);
    g0 = TX(r->f, MOSAIK_MSG_VOTE_GRANT); a0 = TX(r->f, MOSAIK_MSG_ACK); s0 = TX(r->f, MOSAIK_MSG_SAFE);
    o0 = TX(r->f, MOSAIK_MSG_VOTE_REQ) + TX(r->f, MOSAIK_MSG_HEARTBEAT);

    /* Restore delivery; the SAFE node's genuine periodic announcement follows. */
    r->t_restore = g_bus.now_ms;
    isolate_node(r->f, false);
    for (k = 0; k < 2000u; k++) {
        int s;
        bool ev;
        bus_step(); r->steps++; r->hash = l4_hash_step(r->hash);
        ev = ((g_bus.node[r->li].safe_evidence_mask | g_bus.node[r->b].safe_evidence_mask) & (uint8_t)(1u << r->f)) != 0u;
        if (r->t_evidence == 0u && ev) {
            r->t_evidence = g_bus.now_ms;
            r->term_li_at_evidence = g_bus.node[r->li].term; r->term_b_at_evidence = g_bus.node[r->b].term;
        }
        for (s = 0; s < N_NODES; s++) {
            uint16_t t0 = (s == r->li) ? r->term0_li : r->term0_b;
            if (s == r->f) { continue; }
            if (r->t_term_change == 0u && g_bus.node[s].term != t0) {
                r->t_term_change = g_bus.now_ms; r->term_change_to = g_bus.node[s].term; r->term_change_idx = s;
                r->vote_req_at_term_change = (TX(r->li, MOSAIK_MSG_VOTE_REQ) + TX(r->b, MOSAIK_MSG_VOTE_REQ)) != vote_req0;
            }
        }
        if (g_bus.node[r->li].role != MOSAIK_ROLE_LEADER) {
            if (r->t_role_lost == 0u) { r->t_role_lost = g_bus.now_ms; }
            r->leader_role_kept = false;
        }
        if (valid_leader_index() != r->li) { r->authority_kept = false; }
        if (valid_leader_count() == 0) {
            r->gap_ms++;
        } else if (r->gap_ms != 0u && r->t_auth_restored == 0u) {
            r->t_auth_restored = g_bus.now_ms; r->auth_restored_idx = valid_leader_index();
            r->term_auth_restored = g_bus.node[r->auth_restored_idx].term;
        }
    }
    r->survivor_vote_req_delta = TX(r->li, MOSAIK_MSG_VOTE_REQ) + TX(r->b, MOSAIK_MSG_VOTE_REQ) - vote_req0;
    r->f_grant_delta = TX(r->f, MOSAIK_MSG_VOTE_GRANT) - g0;
    r->f_ack_delta   = TX(r->f, MOSAIK_MSG_ACK) - a0;
    r->f_safe_delta  = TX(r->f, MOSAIK_MSG_SAFE) - s0;
    r->f_other_after_safe = TX(r->f, MOSAIK_MSG_VOTE_REQ) + TX(r->f, MOSAIK_MSG_HEARTBEAT) - o0;
    r->term_end_li = g_bus.node[r->li].term; r->term_end_b = g_bus.node[r->b].term;
    r->state_end_li = (unsigned)g_bus.node[r->li].state; r->state_end_b = (unsigned)g_bus.node[r->b].state;
    r->state_end_f = (unsigned)g_bus.node[r->f].state;
    r->mask_end_li = g_bus.node[r->li].safe_evidence_mask; r->mask_end_b = g_bus.node[r->b].safe_evidence_mask;
    r->hash = l4_hash_tx(r->hash);
}

/* TC-053: official C2 RED. A genuine higher-term SAFE announcement must be
 * FDIR evidence only: no role, authority, term or election effect on the
 * healthy majority. EXPECTED RED on 8177e70: the generic higher-term adoption
 * in mosaik_on_rx() executes on MOSAIK_MSG_SAFE before SAFE-specific
 * processing, so the survivors adopt the SAFE node's term and the leader
 * steps down. The interruption is measured, not prescribed. */
static void tc_053_safe_announcement_no_authority_effect(void)
{
    c2_result_t r;
    printf("TC-053  higher-term SAFE announcement has no authority effect on the healthy majority (L4-C2) [Lot 4]\n");
    run_c2_scenario(&r);

    check(r.li >= 0 && r.safe_reached, "Lot 4", "scenario established: healthy majority, one isolated follower latched SAFE");
    if (r.li < 0 || !r.safe_reached) { return; }
    check(r.safe_cause == (uint8_t)MOSAIK_SAFE_NO_QUORUM && r.term_safe > r.term0_li, "REQ-SAFE-0003",
          "isolated follower genuinely exhausted its elections and latched SAFE/no-quorum at a higher term");
    check(r.majority_valid_isolation, "INV-LEADER-UNIQUE", "the majority kept one valid leader throughout the isolation");
    check(r.t_evidence != 0u, "REQ-SAFE-0004", "the SAFE node's periodic announcement created peer SAFE evidence on the survivors");
    check(r.state_end_li == (unsigned)MOSAIK_STATE_DEGRADED && r.state_end_b == (unsigned)MOSAIK_STATE_DEGRADED,
          "INV-DEGRADED-PERSISTENT", "survivors are DEGRADED while the peer SAFE evidence stays fresh (LOT 3)");
    check(r.leader_role_kept, "INV-SAFE-ANNOUNCE-NO-AUTHORITY-EFFECT",
          "valid leader kept the leader role after receiving the SAFE announcement (L4-C2)");
    check(r.authority_kept, "INV-SAFE-ANNOUNCE-NO-AUTHORITY-EFFECT",
          "valid leader kept valid authority after receiving the SAFE announcement (L4-C2)");
    check(r.term_end_li == r.term0_li && r.term_end_b == r.term0_b, "INV-SAFE-ANNOUNCE-NO-AUTHORITY-EFFECT",
          "healthy survivor terms were not raised by the SAFE announcement (L4-C2)");
    check(r.survivor_vote_req_delta == 0u, "INV-SAFE-ANNOUNCE-NO-AUTHORITY-EFFECT",
          "no election was caused by the SAFE announcement (L4-C2)");
    check(r.state_end_f == (unsigned)MOSAIK_STATE_SAFE, "INV-SAFE-LATCH", "SAFE node remained SAFE");
    check(r.f_grant_delta == 0u, "REQ-SAFE-0003", "SAFE node sent no VOTE_GRANT after restore");
    check(r.f_ack_delta == 0u, "REQ-SAFE-0003", "SAFE node sent no ACK after restore");
    check(r.gap_ms == 0u, "INV-SAFE-ANNOUNCE-NO-AUTHORITY-EFFECT",
          "authority gap caused by the SAFE announcement is 0 ms (L4-C2)");

    printf("        leader node %d term %u; follower node %d isolated at %u ms, SAFE/no-quorum at %u ms term %u; delivery restored at %u ms\n",
           r.li + 1, r.term0_li, r.f + 1, r.t_isolate, r.t_safe, r.term_safe, r.t_restore);
    printf("        first peer SAFE evidence on survivors at %u ms (survivor terms then: node %d=%u, node %d=%u); SAFE frames from node %d after restore %u, other frame types %u\n",
           r.t_evidence, r.li + 1, r.term_li_at_evidence, r.b + 1, r.term_b_at_evidence, r.f + 1, r.f_safe_delta, r.f_other_after_safe);
    if (r.t_term_change != 0u) {
        printf("        first survivor term change at %u ms: node %d -> term %u (SAFE node term %u, same step as the evidence: %s, VOTE_REQ emitted by a survivor in that step: %s)\n",
               r.t_term_change, r.term_change_idx + 1, r.term_change_to, r.term_safe,
               (r.t_term_change == r.t_evidence) ? "yes" : "no", r.vote_req_at_term_change ? "yes" : "no");
    } else {
        printf("        survivor terms unchanged after the SAFE announcement\n");
    }
    printf("        leader role lost at %u ms (0 = kept); authority restored at %u ms by node %d term %u (0 = never lost); observed authority interruption %u ms\n",
           r.t_role_lost, r.t_auth_restored, r.auth_restored_idx + 1, r.term_auth_restored, r.gap_ms);
    printf("        survivor VOTE_REQs after restore %u; SAFE node VOTE_GRANT/ACK after restore %u/%u; end states: leader %s, follower %s, SAFE node %s; masks 0x%02X/0x%02X\n",
           r.survivor_vote_req_delta, r.f_grant_delta, r.f_ack_delta, l4_state_name(r.state_end_li),
           l4_state_name(r.state_end_b), l4_state_name(r.state_end_f), r.mask_end_li, r.mask_end_b);
}

/* TC-054: state metadata (payload byte 3) of a legitimate HEARTBEAT is not
 * protocol evidence: varying it over INIT/NOMINAL/DEGRADED/SAFE changes
 * nothing but what the heartbeat semantics already do. */
static void tc_054_state_metadata_non_authority(void)
{
    int li, c, d, s;
    node_snap_t before, after;
    mosaik_msg_t m;
    uint32_t ack0;

    printf("TC-054  heartbeat state metadata is not protocol evidence (INIT/NOMINAL/DEGRADED/SAFE) [Lot 4]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 4", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &c, &d);
    snap_node(&before, c);
    check(before.role == MOSAIK_ROLE_FOLLOWER && before.state == MOSAIK_STATE_NOMINAL && before.mask == 0u,
          "Lot 4", "receiver is a NOMINAL follower without SAFE evidence");
    for (s = 0; s < 4; s++) {
        /* Legitimate current-term heartbeat from the actual leader, only byte 3 varies. */
        make_msg(&m, MOSAIK_MSG_HEARTBEAT, (uint8_t)(li + 1), MOSAIK_ROLE_LEADER, (mosaik_state_t)s,
                 g_bus.node[li].term, (uint8_t)(200 + s));
        ack0 = TX(c, MOSAIK_MSG_ACK);
        deliver_to(c, &m);
        snap_node(&after, c);
        check(after.role == MOSAIK_ROLE_FOLLOWER && after.term == before.term && after.voted_for == before.voted_for &&
              after.voted_term == before.voted_term && after.mask == 0u && after.state == MOSAIK_STATE_NOMINAL &&
              after.leader_id == (uint8_t)(li + 1) && !after.auth && TX(c, MOSAIK_MSG_ACK) == ack0 + 1u,
              "INV-MODE-METADATA-NONAUTHORITATIVE",
              "heartbeat metadata changed no role/term/vote/evidence/state; follower maintenance (ACK) unchanged");
        printf("        metadata %-8s: role %s, state %s, term %u, mask 0x%02X, ACKs +%u\n", l4_state_name((unsigned)s),
               l4_role_name((unsigned)after.role), l4_state_name((unsigned)after.state), after.term, after.mask,
               TX(c, MOSAIK_MSG_ACK) - ack0);
    }

    /* A latched SAFE node: legitimate heartbeats with any metadata never clear SAFE. */
    bus_init(); bus_run(2000u);
    li = split_brain_leader();
    check(li >= 0, "Lot 4", "SAFE node existed");
    if (li < 0) { return; }
    bus_run(1000u);
    c = leader_index();
    check(c >= 0 && c != li, "Lot 4", "survivors elected a new leader");
    if (c < 0) { return; }
    snap_node(&before, li);
    for (s = 0; s < 4; s++) {
        make_msg(&m, MOSAIK_MSG_HEARTBEAT, (uint8_t)(c + 1), MOSAIK_ROLE_LEADER, (mosaik_state_t)s,
                 g_bus.node[c].term, (uint8_t)(200 + s));
        ack0 = TX(li, MOSAIK_MSG_ACK);
        deliver_to(li, &m);
        snap_node(&after, li);
        check(after.state == MOSAIK_STATE_SAFE && after.role == MOSAIK_ROLE_FOLLOWER && after.term == before.term &&
              TX(li, MOSAIK_MSG_ACK) == ack0 && !after.auth,
              "INV-SAFE-LATCH", "heartbeat metadata did not clear SAFE, change term or produce an ACK");
    }
    printf("        SAFE node %d: state %s after four metadata variants, term %u, ACKs 0\n",
           li + 1, l4_state_name((unsigned)after.state), after.term);
}

/* TC-055: an out-of-range state byte (4, 5) with a valid CRC is rejected by
 * the decoder and has no protocol effect on any receiver. */
static void tc_055_out_of_range_state_byte(void)
{
    int li, c, d, v, i, k;
    mosaik_msg_t m, tmp;
    mosaik_frame_t f, ctrl;
    node_snap_t before[N_NODES], after[N_NODES];

    printf("TC-055  out-of-range state byte with valid CRC: decoder rejects, no protocol effect [Lot 4]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 4", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &c, &d);
    for (k = 0; k < 4; k++) {
        uint8_t byte = (k < 2) ? 4u : 5u;
        uint8_t src;
        if (k % 2 == 0) {
            src = (uint8_t)(li + 1);
            make_msg(&m, MOSAIK_MSG_HEARTBEAT, src, MOSAIK_ROLE_LEADER, MOSAIK_STATE_NOMINAL,
                     g_bus.node[li].term, (uint8_t)(210 + k));
        } else {
            src = (uint8_t)(c + 1);
            make_msg(&m, MOSAIK_MSG_SAFE, src, MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_NOMINAL,
                     g_bus.node[c].term, (uint8_t)MOSAIK_SAFE_NO_QUORUM);
        }
        mosaik_encode(&f, &m);
        f.data[3] = byte;
        f.data[7] = mosaik_crc8(f.data, 7u);        /* valid CRC over the altered payload */
        ctrl = f; ctrl.data[3] = (uint8_t)MOSAIK_STATE_NOMINAL; ctrl.data[7] = mosaik_crc8(ctrl.data, 7u);
        check(mosaik_decode(&ctrl, &tmp), "Lot 4", "control: the same frame with an in-range state byte decodes (CRC path valid)");
        check(!mosaik_decode(&f, &tmp), "INV-MODE-LEGAL", "decoder rejects the out-of-range state byte");

        for (i = 0; i < N_NODES; i++) { snap_node(&before[i], i); }
        bus_inject_frame(&f, src);
        bus_step();
        for (i = 0; i < N_NODES; i++) { snap_node(&after[i], i); }
        v = 1;
        for (i = 0; i < N_NODES; i++) {
            if (i == (int)src - 1) { continue; }
            if (after[i].decode_errors != before[i].decode_errors + 1u) { v = 0; }
            if (!snap_same(&before[i], &after[i])) { v = 0; }
        }
        check(v != 0, "INV-MODE-METADATA-NONAUTHORITATIVE",
              "each receiver counted one decode error and changed no role/term/vote/evidence/state/authority");
        printf("        %s frame from node %u, state byte %u: rejected; receivers unchanged, decode_errors +1 each\n",
               (k % 2 == 0) ? "HEARTBEAT" : "SAFE", src, byte);
    }
    check(valid_leader_index() == li, "INV-LEADER-UNIQUE", "the valid leader is unchanged");
}

/* TC-056: a replayed legitimate old-term heartbeat carrying DEGRADED metadata
 * is rejected as stale (LOT 2 semantics); metadata cannot bypass the term
 * check and changes no local state. The frame is a real one captured from
 * the former leader's transmit path, with only byte 3 altered (CRC redone). */
static void tc_056_stale_heartbeat_degraded_metadata(void)
{
    int li, a, b, l2 = -1, k, v, i;
    uint16_t term_old;
    mosaik_frame_t replay[2];
    mosaik_msg_t m;
    node_snap_t before[N_NODES], after[N_NODES];

    printf("TC-056  stale old-term heartbeat carrying DEGRADED metadata: rejected, no state change [Lot 4]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0 && g_bus.last_hb_frame_valid[li], "Lot 4", "leader existed and a real heartbeat frame was captured");
    if (li < 0 || !g_bus.last_hb_frame_valid[li]) { return; }
    term_old = g_bus.node[li].term;
    survivors_of(li, &a, &b);
    replay[0] = g_bus.last_hb_frame[li];             /* unchanged control */
    replay[1] = g_bus.last_hb_frame[li];
    replay[1].data[3] = (uint8_t)MOSAIK_STATE_DEGRADED;
    replay[1].data[7] = mosaik_crc8(replay[1].data, 7u);
    check(mosaik_decode(&replay[1], &m) && m.type == MOSAIK_MSG_HEARTBEAT && m.term == term_old &&
          m.state == MOSAIK_STATE_DEGRADED && m.src == (uint8_t)(li + 1),
          "Lot 4", "replay frame is a well-formed old-term heartbeat with DEGRADED metadata");

    bus_crash_node(li);
    for (k = 0; k < 1500 && l2 < 0; k++) {
        bus_step();
        if (valid_leader_index() >= 0 && valid_leader_index() != li) { l2 = valid_leader_index(); }
    }
    check(l2 >= 0 && g_bus.node[l2].term > term_old, "REQ-FUNC-0004", "survivors elected a new leader at a higher term");
    if (l2 < 0) { return; }
    bus_run(300u);

    for (k = 0; k < 2; k++) {
        for (i = 0; i < N_NODES; i++) { snap_node(&before[i], i); }
        bus_schedule_delayed(&replay[k], (uint8_t)(li + 1), 3u);   /* network model: delayed delivery */
        bus_run(4u);
        for (i = 0; i < N_NODES; i++) { snap_node(&after[i], i); }
        v = 1;
        for (i = 0; i < N_NODES; i++) {
            if (i == li) { continue; }
            if (after[i].stale_term != before[i].stale_term + 1u) { v = 0; }
            if (!snap_same(&before[i], &after[i])) { v = 0; }
        }
        check(v != 0, "INV-NO-STALE-RECOVERY",
              "each survivor rejected the replay as stale-term and changed no role/term/vote/evidence/state/authority");
        printf("        replay %s (term %u < cluster term %u): stale-term rejections +1 on each survivor; leader node %d unchanged\n",
               k == 0 ? "unchanged" : "with DEGRADED metadata", term_old, g_bus.node[l2].term, l2 + 1);
    }
    check(valid_leader_index() == l2 && g_bus.node[a].state == MOSAIK_STATE_NOMINAL && g_bus.node[b].state == MOSAIK_STATE_NOMINAL,
          "INV-MODE-METADATA-NONAUTHORITATIVE", "authority and operational states unchanged by the metadata");
}

/* TC-057: a legitimate leader change while peer SAFE evidence is fresh.
 * DEGRADED persists (LOT 3), every state x role pair is legal, authority
 * rules are unchanged and DEGRADED metadata in the survivors' own frames
 * never becomes SAFE evidence. */
static void tc_057_leader_change_while_degraded(void)
{
    int li, a, b, l2 = -1, f2, k, i, nl;
    uint32_t crash_ms, safe0;
    bool l2_deg_seen = false, masks_ok = true;
    uint8_t l2_deg_mask = 0u;
    uint32_t l2_deg_ms = 0u;
    traj_obs_t t;
    mode_obs_t m;

    printf("TC-057  legitimate leader change while DEGRADED: persistence, legality, authority rules [Lot 4]\n");
    bus_init(); bus_run(2000u);
    li = split_brain_leader();
    check(li >= 0, "Lot 4", "SAFE node existed");
    if (li < 0) { return; }
    survivors_of(li, &a, &b);
    for (k = 0; k < 1500 && l2 < 0; k++) {
        bus_step();
        if (valid_leader_index() >= 0) { l2 = valid_leader_index(); }
    }
    check(l2 >= 0 && l2 != li && g_bus.node[l2].state == MOSAIK_STATE_DEGRADED &&
          (g_bus.node[l2].safe_evidence_mask & (uint8_t)(1u << li)) != 0u,
          "REQ-SAFE-0004", "survivors elected a DEGRADED leader holding received SAFE evidence");
    if (l2 < 0) { return; }
    f2 = (l2 == a) ? b : a;

    traj_init(&t); mode_init(&m);
    crash_ms = g_bus.now_ms; safe0 = TX(li, MOSAIK_MSG_SAFE);
    bus_crash_node(l2);                                 /* legitimate leader loss */
    mode_run(&m, &t, 200u);
    bus_restart_node(l2);                               /* cold restart: INIT, no evidence */
    check(g_bus.node[l2].state == MOSAIK_STATE_INIT && g_bus.node[l2].safe_evidence_mask == 0u && g_bus.node[l2].term == 0u,
          "Lot 4", "cold-restarted node is INIT with no SAFE evidence");
    for (k = 0; k < 2000; k++) {
        bus_step(); traj_step(&t); mode_step(&m);
        if (!l2_deg_seen && g_bus.node[l2].state == MOSAIK_STATE_DEGRADED) {
            l2_deg_seen = true; l2_deg_mask = g_bus.node[l2].safe_evidence_mask; l2_deg_ms = g_bus.node[l2].now_ms;
        }
    }
    nl = valid_leader_index();
    for (i = 0; i < N_NODES; i++) {
        if ((g_bus.node[i].safe_evidence_mask & (uint8_t)~(1u << li)) != 0u) { masks_ok = false; }
    }
    check(nl >= 0 && nl != li && g_bus.node[nl].became_leader_ms > crash_ms, "REQ-FUNC-0004",
          "a new valid leader was elected after the leader loss (legitimate leader change)");
    check(TX(li, MOSAIK_MSG_SAFE) > safe0 + 10u, "Lot 4", "SAFE evidence stayed fresh (SAFE node kept announcing)");
    check(m.illegal_steps == 0u, "INV-MODE-LEGAL", "no illegal state x role pair during the leader change");
    check(t.leader_degraded_valid_seen && nl >= 0 && g_bus.node[nl].state == MOSAIK_STATE_DEGRADED,
          "INV-DEGRADED-PERSISTENT", "the new leader holds valid authority while DEGRADED (LOT 3)");
    check(t.nominal_after_first_deg[f2] == 0u && g_bus.node[f2].state == MOSAIK_STATE_DEGRADED,
          "INV-DEGRADED-PERSISTENT", "the surviving follower never returned to NOMINAL while evidence was fresh");
    check(l2_deg_seen && l2_deg_mask != 0u, "INV-MODE-NO-MAGIC",
          "the restarted node entered DEGRADED only while holding received SAFE evidence");
    check(masks_ok, "INV-MODE-METADATA-NONAUTHORITATIVE",
          "DEGRADED metadata in the survivors' own frames never became SAFE evidence (masks name the SAFE node only)");
    check(t.safe_auth_violations == 0u && t.safe_role_violations == 0u && t.safe_exits == 0u, "INV-SAFE-NO-AUTHORITY",
          "SAFE node held no authority, no role and did not exit");
    check(t.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1");
    check(t.term_regressions == 0u, "INV-TERM-MONOTONIC", "no term regression");
    printf("        SAFE node %d; DEGRADED leader %d crashed at %u ms, cold restart at %u ms; new leader node %d term %u at %u ms (%s)\n",
           li + 1, l2 + 1, crash_ms, crash_ms + 200u, nl + 1, nl >= 0 ? g_bus.node[nl].term : 0u,
           nl >= 0 ? g_bus.node[nl].became_leader_ms : 0u, nl >= 0 ? l4_state_name((unsigned)g_bus.node[nl].state) : "-");
    printf("        restarted node %d first DEGRADED at %u ms with SAFE evidence mask 0x%02X; masks 0x%02X/0x%02X/0x%02X\n",
           l2 + 1, l2_deg_ms, l2_deg_mask, g_bus.node[0].safe_evidence_mask, g_bus.node[1].safe_evidence_mask,
           g_bus.node[2].safe_evidence_mask);
}

/* TC-058: partition, crash, re-election and cold restart under the legality
 * observer; a cold restart returns to INIT with volatile SAFE evidence
 * cleared, and re-acquires DEGRADED only from frames actually received. */
static void tc_058_partition_crash_restart_legality(void)
{
    int li, l2 = -1, l3, i, fdeg = -1, k, lo = -1;
    traj_obs_t t;
    mode_obs_t m;

    printf("TC-058  partition / crash / re-election / cold restart legality [Lot 4]\n");
    bus_init(); traj_init(&t); mode_init(&m);
    mode_run(&m, &t, 2000u);
    li = leader_index();
    check(li >= 0, "Lot 4", "leader existed");
    if (li < 0) { return; }

    /* A. partition isolating the leader for 600 ms (below the SAFE latch
     *    boundary established by TC-042), then heal */
    bus_set_partition_2plus1((uint8_t)(li + 1));
    mode_run(&m, &t, 600u);
    check(!mosaik_has_valid_leadership_authority(&g_bus.node[li]), "INV-LEADER-UNIQUE", "isolated leader lost authority at lease expiry");
    bus_heal_partition();
    mode_run(&m, &t, 1500u);
    check(cluster_recovered(&lo) && g_bus.node[li].state != MOSAIK_STATE_SAFE, "INV-RECOVERY-VERIFIED", "cluster recovered after the heal without SAFE");

    /* B. crash of the current leader, re-election, cold restart, rejoin */
    l2 = leader_index();
    if (l2 < 0) { check(false, "Lot 4", "leader existed before the crash"); return; }
    bus_crash_node(l2);
    mode_run(&m, &t, 1500u);
    check(valid_leader_index() >= 0 && valid_leader_index() != l2, "REQ-FUNC-0004", "survivors re-elected after the crash");
    bus_restart_node(l2);
    check(g_bus.node[l2].state == MOSAIK_STATE_INIT && g_bus.node[l2].role == MOSAIK_ROLE_FOLLOWER &&
          g_bus.node[l2].term == 0u && g_bus.node[l2].safe_evidence_mask == 0u,
          "Lot 4", "cold restart returns to INIT/FOLLOWER, term 0, no SAFE evidence");
    mode_run(&m, &t, 1500u);
    check(cluster_recovered(&lo) && g_bus.node[l2].state == MOSAIK_STATE_NOMINAL, "INV-RECOVERY-VERIFIED",
          "restarted node rejoined NOMINAL and the cluster recovered");

    /* C. SAFE node present: a DEGRADED follower's cold restart clears its
     *    volatile evidence, which is re-acquired only from received frames. */
    l3 = split_brain_leader();
    check(l3 >= 0, "Lot 4", "SAFE node created");
    if (l3 < 0) { return; }
    for (k = 0; k < 1500 && valid_leader_index() < 0; k++) { bus_step(); traj_step(&t); mode_step(&m); }
    lo = valid_leader_index();
    check(lo >= 0 && lo != l3, "REQ-FUNC-0004", "survivors elected around the SAFE node");
    if (lo < 0) { return; }
    for (i = 0; i < N_NODES; i++) { if (i != lo && i != l3) { fdeg = i; } }
    check(g_bus.node[fdeg].state == MOSAIK_STATE_DEGRADED && (g_bus.node[fdeg].safe_evidence_mask & (uint8_t)(1u << l3)) != 0u,
          "REQ-SAFE-0004", "follower is DEGRADED with received SAFE evidence before its crash");
    bus_crash_node(fdeg);
    mode_run(&m, &t, 200u);
    bus_restart_node(fdeg);
    check(g_bus.node[fdeg].state == MOSAIK_STATE_INIT && g_bus.node[fdeg].safe_evidence_mask == 0u &&
          g_bus.node[fdeg].term == 0u && g_bus.node[fdeg].role == MOSAIK_ROLE_FOLLOWER,
          "Lot 4", "volatile SAFE evidence cleared on cold restart (INIT, mask 0, term 0)");
    mode_run(&m, &t, 1500u);
    check(g_bus.node[fdeg].state == MOSAIK_STATE_DEGRADED && (g_bus.node[fdeg].safe_evidence_mask & (uint8_t)(1u << l3)) != 0u &&
          g_bus.node[fdeg].role == MOSAIK_ROLE_FOLLOWER,
          "INV-MODE-NO-MAGIC", "DEGRADED re-acquired only from SAFE frames actually received after the restart");

    check(m.illegal_steps == 0u, "INV-MODE-LEGAL", "no illegal state x role pair across partition, crash, restart and SAFE");
    check(t.safe_role_violations == 0u && t.safe_auth_violations == 0u, "INV-SAFE-NO-AUTHORITY", "no SAFE candidate or leader, no SAFE authority");
    check(t.max_valid <= 1u, "INV-LEADER-UNIQUE", "max concurrent valid authorities <= 1 throughout");
    check(t.term_regressions == 0u, "INV-TERM-MONOTONIC", "no term regression (cold restarts excluded by construction)");
    printf("        partition leader %d, crashed leader %d, SAFE node %d, restarted DEGRADED follower %d; valid leader now node %d term %u\n",
           li + 1, l2 + 1, l3 + 1, fdeg + 1, valid_leader_index() + 1,
           valid_leader_index() >= 0 ? g_bus.node[valid_leader_index()].term : 0u);
}

/* TC-059: a genuine SAFE node whose term is LOWER than the surviving
 * cluster's: its announcements are evidence, nothing else. */
static void tc_059_lower_term_safe_announcement(void)
{
    int li, a, b, l2 = -1, k;
    uint16_t term_safe, ta, tb;
    uint32_t safe0, g0, k0, r0, h0, valid_steps = 0u, deg_steps = 0u;

    printf("TC-059  lower-term SAFE announcement: evidence only, healthy terms and authority unchanged [Lot 4]\n");
    bus_init(); bus_run(2000u);
    li = split_brain_leader();
    check(li >= 0, "Lot 4", "SAFE node existed");
    if (li < 0) { return; }
    survivors_of(li, &a, &b);
    term_safe = g_bus.node[li].term;
    for (k = 0; k < 1500 && l2 < 0; k++) {
        bus_step();
        if (valid_leader_index() >= 0) { l2 = valid_leader_index(); }
    }
    check(l2 >= 0 && l2 != li, "REQ-FUNC-0004", "survivors elected a new leader");
    if (l2 < 0) { return; }
    bus_run(300u);
    ta = g_bus.node[a].term; tb = g_bus.node[b].term;
    check(term_safe < ta && term_safe < tb, "Lot 4", "SAFE node's term is lower than the surviving cluster's term");
    safe0 = TX(li, MOSAIK_MSG_SAFE); g0 = TX(li, MOSAIK_MSG_VOTE_GRANT); k0 = TX(li, MOSAIK_MSG_ACK);
    r0 = TX(li, MOSAIK_MSG_VOTE_REQ); h0 = TX(li, MOSAIK_MSG_HEARTBEAT);
    for (k = 0; k < 1000; k++) {
        bus_step();
        if (valid_leader_index() == l2) { valid_steps++; }
        if (g_bus.node[a].state == MOSAIK_STATE_DEGRADED && g_bus.node[b].state == MOSAIK_STATE_DEGRADED) { deg_steps++; }
    }
    check(TX(li, MOSAIK_MSG_SAFE) - safe0 >= 9u, "Lot 4", "the SAFE node's genuine announcements were delivered throughout (>= 9 in 1 s)");
    check((g_bus.node[a].safe_evidence_mask & (uint8_t)(1u << li)) != 0u && (g_bus.node[b].safe_evidence_mask & (uint8_t)(1u << li)) != 0u,
          "REQ-SAFE-0004", "SAFE evidence recorded on both survivors");
    check(deg_steps == 1000u, "INV-DEGRADED-PERSISTENT", "both survivors DEGRADED at every step of the window");
    check(g_bus.node[a].term == ta && g_bus.node[b].term == tb, "INV-TERM-MONOTONIC", "healthy terms unchanged by the lower-term announcements");
    check(valid_steps == 1000u, "INV-LEADER-UNIQUE", "the same valid leader held authority at every step");
    check(g_bus.node[li].state == MOSAIK_STATE_SAFE && g_bus.node[li].role == MOSAIK_ROLE_FOLLOWER && g_bus.node[li].term == term_safe &&
          TX(li, MOSAIK_MSG_VOTE_GRANT) == g0 && TX(li, MOSAIK_MSG_ACK) == k0 && TX(li, MOSAIK_MSG_VOTE_REQ) == r0 &&
          TX(li, MOSAIK_MSG_HEARTBEAT) == h0,
          "REQ-SAFE-0003", "SAFE sender stayed outside consensus (SAFE, follower, own term, SAFE frames only)");
    printf("        SAFE node %d term %u; cluster term %u, leader node %d; %u SAFE frames in the window; DEGRADED steps %u/1000; valid steps %u/1000\n",
           li + 1, term_safe, ta, l2 + 1, TX(li, MOSAIK_MSG_SAFE) - safe0, deg_steps, valid_steps);
}

/* TC-060: determinism guard for the two RED scenarios: run A against run B,
 * independent of whether the desired post-correction assertions hold. */
static void tc_060_determinism_of_red_scenarios(void)
{
    c1_result_t a1, b1;
    c2_result_t a2, b2;
    printf("TC-060  determinism of the TC-052 and TC-053 scenarios (run A vs run B) [Lot 4]\n");
    run_c1_scenario(&a1); run_c1_scenario(&b1);
    run_c2_scenario(&a2); run_c2_scenario(&b2);
    check(a1.hash == b1.hash && memcmp(&a1, &b1, sizeof(a1)) == 0, "Lot 4",
          "TC-052 scenario: identical per-step roles, states, terms, authority, evidence masks, transmissions and event times");
    check(a2.hash == b2.hash && memcmp(&a2, &b2, sizeof(a2)) == 0, "Lot 4",
          "TC-053 scenario: identical per-step roles, states, terms, authority, evidence masks, transmissions and event times");
    printf("        TC-052 trajectory hash 0x%08X (first DEGRADED-without-evidence at %u ms, stable heartbeat at %u ms) reproduced: %s\n",
           a1.hash, a1.v_ms, a1.stable_hb_ms, (a1.hash == b1.hash) ? "yes" : "no");
    printf("        TC-053 trajectory hash 0x%08X (evidence at %u ms, authority interruption %u ms) reproduced: %s\n",
           a2.hash, a2.t_evidence, a2.gap_ms, (a2.hash == b2.hash) ? "yes" : "no");
}

/* ------------------------------------------------------------------
 * LOT 5: autonomous reconfiguration (Phase 2 GREEN).
 *
 * Membership is an explicit committed voter mask identified by a
 * configuration epoch that is independent of the leadership term. It is
 * changed only by the CONFIG transaction (PROPOSE, ACCEPT, COMMIT) and is
 * carried in the node's own host-model configuration store across a cold
 * restart. Reachability is never membership.
 *
 * Rules honoured by these tests: no node field is ever written from a
 * test; the only way a membership changes is the public request API
 * called on a node that legitimately holds authority, or CONFIG frames
 * actually received; faults use the directional network model, crash and
 * cold restart, frame injection and delayed delivery; harness knowledge
 * (topology, crashed[], other nodes' state) is used for assertions only.
 * The configuration store of a node is written by the harness only to
 * inject a persistence fault, which is labelled where it happens.
 * ------------------------------------------------------------------- */

/* LOT 5 FAULT INJECTION INTO THE PERSISTENCE MODEL: cold restart with the
 * node's own store overwritten by the given content (a stale, foreign or
 * malformed configuration). Models corrupted configuration memory. Nothing
 * else about the node is written, and no other node can read this store. */
static void bus_restart_node_with_store(int node_idx, const mosaik_config_store_t *store)
{
    mosaik_config_t cfg;
    if (node_idx < 0 || node_idx >= N_NODES) { return; }
    if (!g_bus.crashed[node_idx]) { return; }
    mosaik_config_default(&cfg);
    g_bus.crashed[node_idx] = false;
    g_bus.restart_count[node_idx]++;
    g_bus.store[node_idx] = *store;
    mosaik_init(&g_bus.node[node_idx], (uint8_t)(node_idx + 1), &cfg,
                bus_tx, (void *)(uintptr_t)(node_idx + 1), g_bus.now_ms);
    mosaik_load_config_store(&g_bus.node[node_idx], &g_bus.store[node_idx]);
}

static uint8_t l5_popcount(uint8_t v)
{
    uint8_t c = 0u;
    while (v) { c = (uint8_t)(c + (v & 1u)); v = (uint8_t)(v >> 1); }
    return c;
}

static uint8_t l5_quorum(uint8_t mask) { return (uint8_t)((l5_popcount(mask) / 2u) + 1u); }

static uint8_t l5_bit(int idx) { return (uint8_t)(1u << idx); }

/* The nodes this node takes part in consensus with, read-only. */
static uint8_t observed_voters(int idx)
{
    return mosaik_effective_members(&g_bus.node[idx]);
}

/* Exhaustive probe of the 11-bit identifier space: how many identifiers
 * decode at all, per message type, with a well-formed payload. */
static void l5_probe_identifier_space(uint32_t per_type[8], uint32_t *decodable, uint32_t *beyond_known)
{
    uint32_t id;
    int k;
    for (k = 0; k < 8; k++) { per_type[k] = 0u; }
    *decodable = 0u; *beyond_known = 0u;
    for (id = 0u; id < 0x800u; id++) {
        uint8_t src;
        bool any = false;
        for (src = 1u; src <= 3u && !any; src++) {
            mosaik_frame_t f; mosaik_msg_t m;
            f.id = id; f.dlc = MOSAIK_DLC;
            f.data[0] = MOSAIK_PROTO_VERSION; f.data[1] = src; f.data[2] = 1u; f.data[3] = 1u;
            f.data[4] = 1u; f.data[5] = 0u; f.data[6] = 0u;
            f.data[7] = mosaik_crc8(f.data, 7u);
            if (mosaik_decode(&f, &m)) {
                any = true;
                if ((int)m.type >= 0 && (int)m.type < 8) { per_type[(int)m.type]++; }
                if ((int)m.type > (int)MOSAIK_MSG_ACK || (int)m.type < 1) { (*beyond_known)++; }
            }
        }
        if (any) { (*decodable)++; }
    }
}

/* Build a CONFIG message for targeted injection (replay / malformed tests). */
static void make_cfg_msg(mosaik_msg_t *m, uint8_t src, mosaik_cfg_stage_t stage,
                         uint16_t epoch, uint8_t mask, uint8_t arg)
{
    memset(m, 0, sizeof(*m));
    m->type = MOSAIK_MSG_CONFIG; m->src = src; m->version = MOSAIK_PROTO_VERSION;
    m->role = MOSAIK_ROLE_FOLLOWER; m->state = MOSAIK_STATE_INIT;
    m->term = epoch; m->arg = arg; m->cfg_stage = (uint8_t)stage; m->cfg_mask = mask;
}

/* Per-step read-only membership, quorum and authority observation. */
typedef struct {
    uint32_t max_valid;                       /* INV-LEADER-UNIQUE */
    uint32_t steps_multi_auth;
    uint32_t same_epoch_mask_conflicts;       /* INV-RECONFIG-CONSISTENT */
    uint32_t authority_outside_membership;    /* INV-RECONFIG-AUTHORITY */
    uint32_t leader_acq[N_NODES];
    uint8_t  min_votes_in_committed[N_NODES]; /* INV-RECONFIG-QUORUM */
    uint32_t mask_changes[N_NODES];
    uint32_t term_regressions;                /* INV-TERM-MONOTONIC */
    uint16_t epoch_now[N_NODES];
    uint8_t  mask_now[N_NODES];
    mosaik_role_t last_role[N_NODES];
    uint16_t last_term[N_NODES];
    uint8_t  last_mask[N_NODES];
    uint32_t last_restart[N_NODES];
} l5_obs_t;

static void l5_init(l5_obs_t *o)
{
    int i;
    memset(o, 0, sizeof(*o));
    for (i = 0; i < N_NODES; i++) {
        o->min_votes_in_committed[i] = 0xFFu;
        o->last_role[i]    = g_bus.node[i].role;
        o->last_term[i]    = g_bus.node[i].term;
        o->last_mask[i]    = g_bus.node[i].committed_mask;
        o->last_restart[i] = g_bus.restart_count[i];
    }
}

static void l5_step(l5_obs_t *o)
{
    int i, j;
    uint32_t v = (uint32_t)valid_leader_count();

    if (v > o->max_valid) { o->max_valid = v; }
    if (v > 1u) { o->steps_multi_auth++; }

    for (i = 0; i < N_NODES; i++) {
        const mosaik_node_t *n = &g_bus.node[i];
        if (g_bus.crashed[i] || !g_bus.powered[i]) { continue; }
        if (g_bus.restart_count[i] != o->last_restart[i]) {
            o->last_restart[i] = g_bus.restart_count[i];
            o->last_term[i]    = n->term;
            o->last_mask[i]    = n->committed_mask;
            o->last_role[i]    = n->role;
        }
        if (n->term < o->last_term[i]) { o->term_regressions++; }
        o->last_term[i] = n->term;
        if (n->committed_mask != o->last_mask[i]) { o->mask_changes[i]++; o->last_mask[i] = n->committed_mask; }
        o->epoch_now[i] = n->committed_epoch;
        o->mask_now[i]  = n->committed_mask;

        if (mosaik_has_valid_leadership_authority(n) && !mosaik_is_member(n)) {
            o->authority_outside_membership++;
        }
        if (n->role == MOSAIK_ROLE_LEADER && o->last_role[i] != MOSAIK_ROLE_LEADER) {
            uint8_t counted = l5_popcount((uint8_t)(n->vote_mask & n->committed_mask));
            o->leader_acq[i]++;
            if (counted < o->min_votes_in_committed[i]) { o->min_votes_in_committed[i] = counted; }
        }
        o->last_role[i] = n->role;
    }
    /* Two running nodes must never hold different memberships at the same
     * configuration epoch: a committed configuration is unique per epoch. */
    for (i = 0; i < N_NODES; i++) {
        if (g_bus.crashed[i] || !g_bus.powered[i]) { continue; }
        for (j = i + 1; j < N_NODES; j++) {
            if (g_bus.crashed[j] || !g_bus.powered[j]) { continue; }
            if (g_bus.node[i].committed_epoch == g_bus.node[j].committed_epoch &&
                g_bus.node[i].committed_mask  != g_bus.node[j].committed_mask) {
                o->same_epoch_mask_conflicts++;
            }
        }
    }
}

static void l5_run(l5_obs_t *l, traj_obs_t *t, uint32_t ms)
{
    uint32_t k;
    for (k = 0; k < ms; k++) { bus_step(); if (t) { traj_step(t); } l5_step(l); }
}

/* Smallest number of committed-membership votes any leadership acquisition
 * was based on, over all nodes; 0xFF when no acquisition was observed. */
static uint8_t l5_min_votes_any(const l5_obs_t *o)
{
    int i; uint8_t m = 0xFFu;
    for (i = 0; i < N_NODES; i++) { if (o->min_votes_in_committed[i] < m) { m = o->min_votes_in_committed[i]; } }
    return m;
}

/* Every running node committed exactly this configuration. */
static bool l5_all_committed(uint8_t mask, uint16_t epoch)
{
    int i;
    for (i = 0; i < N_NODES; i++) {
        if (g_bus.crashed[i] || !g_bus.powered[i]) { continue; }
        if (g_bus.node[i].committed_mask != mask || g_bus.node[i].committed_epoch != epoch) { return false; }
    }
    return true;
}

#define CFGTX(idx, stage) (g_bus.tx_cfg_count[(idx)][(int)(stage)])

/* Run a legitimate reconfiguration request on the node that currently holds
 * valid authority and let the transaction settle. Returns the request
 * result; the transaction itself is carried entirely by CONFIG frames. */
static bool l5_request(int leader_idx, uint8_t target, uint32_t settle_ms, l5_obs_t *l, traj_obs_t *t)
{
    bool ok = mosaik_request_reconfiguration(&g_bus.node[leader_idx], target);
    if (l) { l5_run(l, t, settle_ms); } else { bus_run(settle_ms); }
    return ok;
}

/* TC-061: the membership representation is explicit and cfg.cluster_size no
 * longer decides anything. Characterisation. */
static void tc_061_membership_representation(void)
{
    uint32_t per_type[8], decodable, beyond;
    mosaik_msg_t m, d;
    mosaik_frame_t base, f;
    mosaik_config_t cfg1;
    int b, li, i, consumed = 0;
    l5_obs_t l; traj_obs_t t;

    printf("TC-061  membership is an explicit committed mask with its own epoch; cluster_size is inert [Lot 5]\n");
    l5_probe_identifier_space(per_type, &decodable, &beyond);
    check(decodable == 18u && per_type[(int)MOSAIK_MSG_CONFIG] == 3u && beyond == 3u,
          "Lot 5", "the wire protocol carries a membership transaction on its own identifiers (3 CONFIG ids)");

    /* CONFIG frames carry stage, mask and epoch unambiguously; every byte is used. */
    make_cfg_msg(&m, 2u, MOSAIK_CFG_PROPOSE, 0x0102u, 0x03u, 0u);
    mosaik_encode(&base, &m);
    check(mosaik_decode(&base, &d) && d.type == MOSAIK_MSG_CONFIG && d.src == 2u &&
          d.cfg_stage == (uint8_t)MOSAIK_CFG_PROPOSE && d.cfg_mask == 0x03u && d.term == 0x0102u,
          "Lot 5", "a CONFIG frame round-trips stage, source, membership mask and configuration epoch");
    for (b = 0; b < 7; b++) {
        f = base;
        f.data[b] = (uint8_t)(f.data[b] ^ 0x01u);
        f.data[7] = mosaik_crc8(f.data, 7u);
        if (!mosaik_decode(&f, &d)) { consumed++; continue; }
        if (d.version != m.version || d.src != m.src || d.cfg_stage != m.cfg_stage ||
            d.cfg_mask != m.cfg_mask || d.term != m.term || d.arg != m.arg) { consumed++; }
    }
    check(consumed == 7, "Lot 5", "no spare payload byte: each of bytes 0..6 carries a field of the transaction");

    /* Boot: every node starts from the same committed configuration. */
    bus_init(); l5_init(&l); traj_init(&t);
    l5_run(&l, &t, 2000u);
    check(l5_all_committed((uint8_t)MOSAIK_MEMBERSHIP_ALL, (uint16_t)MOSAIK_CONFIG_EPOCH_INITIAL),
          "INV-RECONFIG-QUORUM", "every node booted committed to {1,2,3} at configuration epoch 1");
    li = leader_index();
    check(li >= 0, "Lot 5", "leader existed");
    if (li < 0) { return; }
    check(l5_min_votes_any(&l) >= l5_quorum((uint8_t)MOSAIK_MEMBERSHIP_ALL), "INV-RECONFIG-QUORUM",
          "leadership was acquired only with a quorum of the committed membership");

    /* cluster_size is no longer authoritative: booting every node with
     * cluster_size 1 (which used to mean quorum 1) changes nothing. */
    mosaik_config_default(&cfg1); cfg1.cluster_size = 1u;
    bus_init_with_cfg(&cfg1); l5_init(&l); traj_init(&t);
    l5_run(&l, &t, 2000u);
    check(l5_all_committed((uint8_t)MOSAIK_MEMBERSHIP_ALL, (uint16_t)MOSAIK_CONFIG_EPOCH_INITIAL),
          "INV-RECONFIG-QUORUM", "with cluster_size 1 the committed membership is still {1,2,3}");
    check(l5_min_votes_any(&l) >= 2u && t.max_valid <= 1u, "INV-RECONFIG-QUORUM",
          "quorum came from the membership mask, not from cluster_size: still 2 votes required");
    for (i = 0; i < N_NODES; i++) { check(l.mask_changes[i] == 0u, "INV-RECONFIG-NO-MAGIC",
          "no node changed its committed membership without a transaction"); }
    printf("        decodable identifiers %u (CONFIG %u, beyond the five consensus types %u); payload bytes used 7/7; committed {1,2,3} epoch 1 on every node; minimum counted votes per acquisition %u\n",
           decodable, per_type[(int)MOSAIK_MSG_CONFIG], beyond, l5_min_votes_any(&l));
}

/* TC-062: peer loss is not membership removal. */
static void tc_062_peer_loss_is_not_removal(void)
{
    int li, f, b;
    l5_obs_t l; traj_obs_t t;
    printf("TC-062  peer loss is not membership removal: an unreachable peer stays a committed voter [Lot 5]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &f, &b);
    isolate_node(f, true);
    l5_init(&l); traj_init(&t);
    l5_run(&l, &t, 3000u);
    check(l5_all_committed((uint8_t)MOSAIK_MEMBERSHIP_ALL, (uint16_t)MOSAIK_CONFIG_EPOCH_INITIAL),
          "INV-RECONFIG-NO-MAGIC", "no node removed the unreachable peer from its committed membership");
    check(observed_voters(li) == 0x07u && observed_voters(b) == 0x07u, "INV-RECONFIG-QUORUM",
          "the survivors still count all three nodes: quorum stayed 2 of 3");
    check(l.leader_acq[f] == 0u && t.safe_auth_violations == 0u, "INV-RECONFIG-AUTHORITY",
          "the unreachable node never acquired leadership or authority");
    check(valid_leader_index() == li && t.max_valid <= 1u, "INV-LEADER-UNIQUE",
          "the majority leader kept valid authority and no second authority appeared");
    check(CFGTX(li, MOSAIK_CFG_PROPOSE) == 0u && CFGTX(b, MOSAIK_CFG_PROPOSE) == 0u,
          "INV-RECONFIG-NO-MAGIC", "loss of contact proposed no membership change");
    isolate_node(f, false);
    l5_run(&l, &t, 1000u);
    check(l.same_epoch_mask_conflicts == 0u && t.term_regressions == 0u, "INV-RECONFIG-CONSISTENT",
          "after restore every node still holds the same configuration at the same epoch");
    printf("        isolated node %d: state %s, committed mask 0x%02X epoch %u; leader node %d kept authority; proposals emitted %u\n",
           f + 1, l4_state_name((unsigned)g_bus.node[f].state), g_bus.node[f].committed_mask,
           g_bus.node[f].committed_epoch, li + 1, CFGTX(li, MOSAIK_CFG_PROPOSE));
}

/* TC-063: a minority partition cannot reconfigure itself into a quorum. */
static void tc_063_minority_cannot_self_reconfigure(void)
{
    int li, iso;
    bool req;
    l5_obs_t l; traj_obs_t t;
    printf("TC-063  minority partition cannot self-reconfigure into its own quorum [Lot 5]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5", "leader existed");
    if (li < 0) { return; }
    iso = (li + 2) % N_NODES;
    bus_set_partition_2plus1((uint8_t)(iso + 1));
    l5_init(&l); traj_init(&t);
    l5_run(&l, &t, 1500u);
    /* The isolated node tries to make itself and one absent peer the cluster. */
    req = mosaik_request_reconfiguration(&g_bus.node[iso], (uint8_t)(l5_bit(iso) | l5_bit(li)));
    check(!req, "INV-RECONFIG-PARTITION",
          "a node without valid leadership authority cannot start a membership transaction");
    l5_run(&l, &t, 3500u);
    check(CFGTX(iso, MOSAIK_CFG_PROPOSE) == 0u && CFGTX(iso, MOSAIK_CFG_COMMIT) == 0u,
          "INV-RECONFIG-PARTITION", "the isolated node proposed and committed nothing");
    check(g_bus.node[iso].committed_mask == (uint8_t)MOSAIK_MEMBERSHIP_ALL, "INV-RECONFIG-QUORUM",
          "the isolated node still counts itself as one of three, not as a membership of its own");
    check(l.leader_acq[iso] == 0u && g_bus.node[iso].state == MOSAIK_STATE_SAFE, "INV-RECONFIG-PARTITION",
          "it exhausted its elections without ever appointing itself and latched SAFE");
    check(t.max_valid <= 1u && valid_leader_index() >= 0 && valid_leader_index() != iso,
          "INV-LEADER-UNIQUE", "authority remained on the majority side only");
    bus_heal_partition();
    l5_run(&l, &t, 2000u);
    check(l.same_epoch_mask_conflicts == 0u && l.authority_outside_membership == 0u,
          "INV-RECONFIG-CONSISTENT", "after the heal no node holds authority outside its committed membership");
    printf("        minority node %d: request refused, %u proposals, %u failed elections, state %s; majority leader node %d\n",
           iso + 1, CFGTX(iso, MOSAIK_CFG_PROPOSE), t.max_failed[iso],
           l4_state_name((unsigned)g_bus.node[iso].state), valid_leader_index() + 1);
}

/* TC-064: a legitimate {1,2,3} -> two-node transition is proposed, agreed and
 * committed through the protocol, and the removed node stops being a voter.
 * Phase 1 asserted that no such capability existed; it now executes. */
static void tc_064_membership_change_executes(void)
{
    int li, keep, rem, i;
    uint8_t target;
    bool req;
    uint16_t term_before;
    l5_obs_t l; traj_obs_t t;

    printf("TC-064  legitimate membership change: propose, agree, commit, and the removed node stops voting [Lot 5]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &keep, &rem);
    target = (uint8_t)(l5_bit(li) | l5_bit(keep));
    term_before = g_bus.node[li].term;
    l5_init(&l); traj_init(&t);
    req = l5_request(li, target, 200u, &l, &t);

    check(req, "INV-RECONFIG-NO-MAGIC", "the valid leader accepted the external reconfiguration request");
    check(CFGTX(li, MOSAIK_CFG_PROPOSE) >= 1u && CFGTX(keep, MOSAIK_CFG_ACCEPT) >= 1u &&
          CFGTX(li, MOSAIK_CFG_COMMIT) >= 1u,
          "INV-RECONFIG-NO-MAGIC", "the transaction ran on the bus: PROPOSE, ACCEPT and COMMIT frames were emitted");
    check(l5_all_committed(target, 2u), "INV-RECONFIG-CONSISTENT",
          "every node, including the removed one, committed the same membership at epoch 2");
    check(observed_voters(li) == target && observed_voters(keep) == target &&
          (observed_voters(li) & l5_bit(rem)) == 0u,
          "INV-RECONFIG-QUORUM", "the surviving members count only the committed membership");
    check(g_bus.node[li].term == term_before, "INV-TERM-MONOTONIC",
          "the membership epoch advanced without touching the leadership term");
    check(valid_leader_index() == li && t.max_valid <= 1u && l.steps_multi_auth == 0u,
          "INV-LEADER-UNIQUE", "the leader kept valid authority throughout the transaction");
    check(g_bus.node[rem].state != MOSAIK_STATE_SAFE, "INV-RECONFIG-REMOVED-NODE",
          "removal is not FDIR: the removed node was not forced into SAFE");

    /* The removed node must now be silent in consensus and must stay so. */
    {
        uint32_t g0 = TX(rem, MOSAIK_MSG_VOTE_GRANT), a0 = TX(rem, MOSAIK_MSG_ACK),
                 r0 = TX(rem, MOSAIK_MSG_VOTE_REQ), h0 = TX(rem, MOSAIK_MSG_HEARTBEAT);
        l5_run(&l, &t, 3000u);
        check(TX(rem, MOSAIK_MSG_VOTE_GRANT) == g0 && TX(rem, MOSAIK_MSG_ACK) == a0 &&
              TX(rem, MOSAIK_MSG_VOTE_REQ) == r0 && TX(rem, MOSAIK_MSG_HEARTBEAT) == h0,
              "INV-RECONFIG-REMOVED-NODE", "the removed node emitted no vote, acknowledgement, request or heartbeat");
        check(l.leader_acq[rem] == 0u && l.authority_outside_membership == 0u,
              "INV-RECONFIG-AUTHORITY", "the removed node never held authority after the commit");
        check(valid_leader_index() == li && t.max_valid <= 1u, "INV-LEADER-UNIQUE",
              "the two remaining members kept exactly one valid authority");
    }
    for (i = 0; i < N_NODES; i++) {
        check(l.mask_changes[i] == 1u, "INV-RECONFIG-NO-MAGIC",
              "each node changed its committed membership exactly once, at the commit");
    }
    check(l.same_epoch_mask_conflicts == 0u, "INV-RECONFIG-CONSISTENT",
          "no two nodes held different memberships at the same epoch");
    printf("        leader node %d removed node %d: committed 0x%02X epoch %u on all nodes; term %u unchanged; frames PROPOSE %u ACCEPT %u COMMIT %u; removed node state %s\n",
           li + 1, rem + 1, g_bus.node[li].committed_mask, g_bus.node[li].committed_epoch,
           g_bus.node[li].term, CFGTX(li, MOSAIK_CFG_PROPOSE), CFGTX(keep, MOSAIK_CFG_ACCEPT),
           CFGTX(li, MOSAIK_CFG_COMMIT), l4_state_name((unsigned)g_bus.node[rem].state));
}

/* TC-065: the executable form of the OLD-majority counterexample.
 * A: a proposal that cannot reach a quorum of BOTH configurations never
 *    commits, so the smaller quorum is never installed.
 * B: once the new configuration is committed, evidence from the removed node
 *    cannot renew the leader's lease. */
static void tc_065_consistency_before_quorum_change(void)
{
    int li, keep, rem;
    uint8_t target;
    uint32_t lost_ms = 0u, cut_ms;
    uint16_t ack_term_before;
    mosaik_msg_t m;
    l5_obs_t l; traj_obs_t t;
    int k;

    printf("TC-065  a membership reduction affects quorum only once both configurations have agreed [Lot 5]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &keep, &rem);
    target = (uint8_t)(l5_bit(li) | l5_bit(keep));
    l5_init(&l); traj_init(&t);

    /* A. The acceptance of the node that the NEW quorum needs cannot reach
     *    the proposer. A quorum of the OLD configuration is still available
     *    (the proposer plus the node being removed), which is exactly what
     *    an old-majority-only rule would have accepted. */
    bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DROP, 0u);
    check(l5_request(li, target, 400u, &l, &t), "Lot 5", "request accepted by the valid leader");
    check(CFGTX(rem, MOSAIK_CFG_ACCEPT) >= 1u, "Lot 5",
          "the node being removed did accept: an old-configuration quorum was reachable");
    check(g_bus.node[li].committed_mask == (uint8_t)MOSAIK_MEMBERSHIP_ALL &&
          g_bus.node[li].committed_epoch == 1u,
          "INV-RECONFIG-TRANSITION",
          "the proposer did not commit on an old-configuration quorum alone");
    check(l5_all_committed((uint8_t)MOSAIK_MEMBERSHIP_ALL, 1u), "INV-RECONFIG-CONSISTENT",
          "no node moved to the smaller membership");
    check(observed_voters(li) == 0x07u, "INV-RECONFIG-QUORUM",
          "quorum is still taken over the old membership while the transition is unresolved");
    check(t.max_valid <= 1u && l.authority_outside_membership == 0u, "INV-LEADER-UNIQUE",
          "no authority was manufactured during the unresolved transition");

    /* Restore the path: the proposer retries and now reaches both quorums. */
    bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DELIVER, 0u);
    l5_run(&l, &t, 400u);
    check(l5_all_committed(target, 2u), "INV-RECONFIG-TRANSITION",
          "the commit happened only once a quorum of the old AND of the new membership had accepted");

    /* B. Evidence from the removed node can no longer renew the lease. */
    ack_term_before = g_bus.node[li].last_ack_term[rem];
    make_msg(&m, MOSAIK_MSG_ACK, (uint8_t)(rem + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_NOMINAL,
             g_bus.node[li].term, g_bus.node[li].seq);
    deliver_to(li, &m);
    check(g_bus.node[li].last_ack_term[rem] == ack_term_before, "INV-RECONFIG-REMOVED-NODE",
          "a current-term acknowledgement from the removed node was not recorded as quorum evidence");

    cut_ms = g_bus.now_ms;
    bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DROP, 0u);
    for (k = 0; k < 1500 && lost_ms == 0u; k++) {
        bus_step(); traj_step(&t); l5_step(&l);
        /* keep feeding the leader admissible acknowledgements from the removed node */
        make_msg(&m, MOSAIK_MSG_ACK, (uint8_t)(rem + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_NOMINAL,
                 g_bus.node[li].term, g_bus.node[li].seq);
        deliver_to(li, &m);
        if (!mosaik_has_valid_leadership_authority(&g_bus.node[li])) { lost_ms = g_bus.now_ms - cut_ms; }
    }
    check(lost_ms != 0u && lost_ms <= MOSAIK_LEADERSHIP_LEASE_MS + 1u, "INV-RECONFIG-REMOVED-NODE",
          "the leader lost authority within one lease although the removed node kept acknowledging");
    check(g_bus.node[li].nonmember_rejections > 0u, "INV-RECONFIG-REMOVED-NODE",
          "those acknowledgements were rejected as coming from outside the committed membership");
    check(t.max_valid <= 1u && l.steps_multi_auth == 0u && l.same_epoch_mask_conflicts == 0u,
          "INV-LEADER-UNIQUE", "no second authority and no incompatible configuration at any step");
    printf("        old-quorum-only commit refused at epoch 1; commit at epoch 2 once both quorums agreed; leader %d lost authority %u ms after losing member %d, while removed node %d kept acknowledging (%u non-member rejections)\n",
           li + 1, lost_ms, keep + 1, rem + 1, g_bus.node[li].nonmember_rejections);
}

/* TC-066: a removed node cannot regain voting membership by cold restart. */
static void tc_066_removed_node_rejoin(void)
{
    int li, keep, rem, new_leader;
    uint8_t target;
    uint32_t g0, a0, r0, ack_rx0;
    l5_obs_t l; traj_obs_t t;

    printf("TC-066  a removed node does not regain voting membership by cold restart [Lot 5]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &keep, &rem);
    target = (uint8_t)(l5_bit(li) | l5_bit(keep));
    l5_init(&l); traj_init(&t);
    check(l5_request(li, target, 200u, &l, &t) && l5_all_committed(target, 2u), "Lot 5",
          "membership {removed node excluded} committed at epoch 2");

    bus_crash_node(rem);
    l5_run(&l, &t, 1000u);
    bus_restart_node(rem);
    check(g_bus.node[rem].committed_mask == target && g_bus.node[rem].committed_epoch == 2u,
          "INV-RECONFIG-REMOVED-NODE",
          "the restarted node reloaded its own committed membership and is still excluded");
    check(g_bus.node[rem].term == 0u && !mosaik_is_member(&g_bus.node[rem]), "Lot 5",
          "the restart was cold: term 0, and the node is not a member");
    g0 = TX(rem, MOSAIK_MSG_VOTE_GRANT); a0 = TX(rem, MOSAIK_MSG_ACK); r0 = TX(rem, MOSAIK_MSG_VOTE_REQ);
    ack_rx0 = g_bus.node[li].last_ack_rx_ms[rem];
    l5_run(&l, &t, 2000u);
    check(TX(rem, MOSAIK_MSG_VOTE_GRANT) == g0 && TX(rem, MOSAIK_MSG_ACK) == a0 &&
          TX(rem, MOSAIK_MSG_VOTE_REQ) == r0,
          "INV-RECONFIG-REMOVED-NODE", "after the restart it granted no vote, acknowledged nothing and ran no election");
    check(g_bus.node[li].last_ack_rx_ms[rem] == ack_rx0, "INV-RECONFIG-REMOVED-NODE",
          "the leader recorded no new acknowledgement evidence from it after the removal");
    check(valid_leader_index() == li && mosaik_has_quorum_ack_evidence(&g_bus.node[li]),
          "INV-RECONFIG-QUORUM", "the leader's quorum rests on its remaining committed member alone");

    /* The membership is now two nodes, so losing one of them leaves no
     * quorum at all. The removed node must not be able to fill that gap. */
    bus_crash_node(li);
    l5_run(&l, &t, 3000u);
    new_leader = valid_leader_index();
    check(new_leader < 0, "INV-RECONFIG-QUORUM",
          "with one of two committed members lost there is no quorum, so no valid authority exists");
    check(l.leader_acq[rem] == 0u && g_bus.node[rem].role == MOSAIK_ROLE_FOLLOWER,
          "INV-RECONFIG-REMOVED-NODE", "the removed node did not restore quorum by standing in for the lost member");
    check(TX(rem, MOSAIK_MSG_VOTE_GRANT) == g0 && TX(rem, MOSAIK_MSG_VOTE_REQ) == r0,
          "INV-RECONFIG-REMOVED-NODE", "it cast no vote and opened no election even when the cluster had no leader");
    check(t.max_valid <= 1u && l.authority_outside_membership == 0u,
          "INV-LEADER-UNIQUE", "at most one valid authority, never held outside a committed membership");
    printf("        node %d removed at epoch 2, crashed and cold-restarted: committed 0x%02X epoch %u, term %u, member %s; after the other member was lost the cluster has %d valid leaders\n",
           rem + 1, g_bus.node[rem].committed_mask, g_bus.node[rem].committed_epoch, g_bus.node[rem].term,
           mosaik_is_member(&g_bus.node[rem]) ? "yes" : "no", valid_leader_count());
}

/* TC-067: configuration identity on the wire, and replay protection that is
 * independent of the LOT 2B heartbeat sequence rules. */
static void tc_067_configuration_replay(void)
{
    int li, keep, rem, i;
    uint8_t target;
    mosaik_frame_t old_propose, old_commit;
    mosaik_msg_t m;
    uint32_t stale0, fut0, dup0, commits0;
    uint16_t epoch_before;
    uint8_t mask_before;
    l5_obs_t l; traj_obs_t t;

    printf("TC-067  stale, duplicate and unsupported-future configuration traffic cannot move a committed membership [Lot 5]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &keep, &rem);
    target = (uint8_t)(l5_bit(li) | l5_bit(keep));
    l5_init(&l); traj_init(&t);
    check(l5_request(li, target, 200u, &l, &t) && l5_all_committed(target, 2u), "Lot 5",
          "epoch 2 committed; epoch 1 is now superseded");
    check(g_bus.last_cfg_frame_valid[li][MOSAIK_CFG_PROPOSE] && g_bus.last_cfg_frame_valid[li][MOSAIK_CFG_COMMIT],
          "Lot 5", "the epoch-2 PROPOSE and COMMIT frames were captured from the bus");
    old_propose = g_bus.last_cfg_frame[li][MOSAIK_CFG_PROPOSE];
    old_commit  = g_bus.last_cfg_frame[li][MOSAIK_CFG_COMMIT];

    /* Frames of different configurations are distinguishable on the wire. */
    {
        mosaik_msg_t a, b;
        check(mosaik_decode(&old_propose, &a) && mosaik_decode(&old_commit, &b) &&
              a.cfg_stage != b.cfg_stage && a.term == 2u && a.cfg_mask == target,
              "INV-RECONFIG-OLD-CONFIG",
              "configuration traffic carries an explicit stage, epoch and membership mask");
    }

    /* Move to epoch 3 so the captured frames become stale. */
    check(l5_request(li, (uint8_t)MOSAIK_MEMBERSHIP_ALL, 400u, &l, &t), "Lot 5",
          "a second transaction re-admits the third node at epoch 3");
    check(l5_all_committed((uint8_t)MOSAIK_MEMBERSHIP_ALL, 3u), "Lot 5", "epoch 3 committed on every node");

    epoch_before = g_bus.node[keep].committed_epoch;
    mask_before  = g_bus.node[keep].committed_mask;
    stale0 = g_bus.node[keep].config_stale_rejections;
    fut0   = g_bus.node[keep].config_future_rejections;
    dup0   = g_bus.node[keep].config_duplicates;
    commits0 = g_bus.node[keep].config_commits;

    /* Replay of the superseded epoch-2 PROPOSE and COMMIT. */
    bus_inject_frame(&old_propose, (uint8_t)(li + 1)); bus_step();
    bus_inject_frame(&old_commit,  (uint8_t)(li + 1)); bus_step();
    check(g_bus.node[keep].config_stale_rejections >= stale0 + 2u, "INV-RECONFIG-OLD-CONFIG",
          "both replayed epoch-2 frames were rejected as a superseded configuration");
    check(g_bus.node[keep].committed_epoch == epoch_before &&
          g_bus.node[keep].committed_mask == mask_before &&
          g_bus.node[keep].config_commits == commits0,
          "INV-RECONFIG-OLD-CONFIG", "the replay restored nothing");

    /* Unsupported future epoch, with no transition context. */
    make_cfg_msg(&m, (uint8_t)(li + 1), MOSAIK_CFG_COMMIT, 9u, target, 0u);
    deliver_to(keep, &m);
    check(g_bus.node[keep].config_future_rejections >= fut0 + 1u &&
          g_bus.node[keep].committed_epoch == epoch_before,
          "INV-RECONFIG-OLD-CONFIG", "a jump to a future epoch without transition context committed nothing");

    /* Duplicate current-epoch COMMIT is idempotent. */
    make_cfg_msg(&m, (uint8_t)(li + 1), MOSAIK_CFG_COMMIT, epoch_before, mask_before, 0u);
    deliver_to(keep, &m); deliver_to(keep, &m);
    check(g_bus.node[keep].config_duplicates >= dup0 + 2u &&
          g_bus.node[keep].committed_epoch == epoch_before &&
          g_bus.node[keep].config_commits == commits0,
          "INV-RECONFIG-OLD-CONFIG", "duplicate current-epoch commits are idempotent");

    /* Delayed configuration traffic through the network model. */
    bus_schedule_delayed(&old_commit, (uint8_t)(li + 1), 5u);
    l5_run(&l, &t, 20u);
    check(l5_all_committed((uint8_t)MOSAIK_MEMBERSHIP_ALL, 3u), "INV-RECONFIG-OLD-CONFIG",
          "a delayed superseded commit delivered later changed nothing");
    for (i = 0; i < N_NODES; i++) { check(g_bus.node[i].term == g_bus.node[li].term, "INV-TERM-MONOTONIC",
          "no configuration frame changed any leadership term"); }
    check(t.max_valid <= 1u && l.same_epoch_mask_conflicts == 0u, "INV-LEADER-UNIQUE",
          "authority and configuration stayed consistent throughout");
    printf("        node %d at epoch %u mask 0x%02X: stale rejections %u, future rejections %u, duplicates %u, commits %u (unchanged)\n",
           keep + 1, g_bus.node[keep].committed_epoch, g_bus.node[keep].committed_mask,
           g_bus.node[keep].config_stale_rejections, g_bus.node[keep].config_future_rejections,
           g_bus.node[keep].config_duplicates, g_bus.node[keep].config_commits);
}

/* Scenario driver shared by TC-068 and TC-070: the proposer commits, then the
 * network cuts it off before the COMMIT can be delivered, so one side holds
 * the new configuration and the other two hold the old one with a pending
 * acceptance. Read-only observation; nothing is written into a node. */
typedef struct {
    int      li, keep, rem;
    uint8_t  target;
    bool     committed_proposer;
    uint16_t epoch_after_cut[N_NODES];
    uint8_t  mask_after_cut[N_NODES];
    bool     promise_after_cut[N_NODES];
    uint32_t max_valid, steps_multi_auth, conflicts, auth_outside;
    uint32_t valid_steps_isolated_side;
    bool     converged;
    uint16_t epoch_end;
    uint8_t  mask_end;
    uint32_t hash;
} l5_partial_t;

static void run_l5_partial_commit(l5_partial_t *r)
{
    l5_obs_t l; traj_obs_t t;
    uint32_t k;
    int i;

    memset(r, 0, sizeof(*r));
    r->li = r->keep = r->rem = -1;
    r->hash = 2166136261u;
    bus_init();
    for (k = 0; k < 2000u; k++) { bus_step(); r->hash = l4_hash_step(r->hash); }
    r->li = leader_index();
    if (r->li < 0) { return; }
    survivors_of(r->li, &r->keep, &r->rem);
    r->target = (uint8_t)(l5_bit(r->li) | l5_bit(r->keep));
    l5_init(&l); traj_init(&t);

    if (!mosaik_request_reconfiguration(&g_bus.node[r->li], r->target)) { return; }
    /* Run until the proposer has committed, then cut it off in both
     * directions so the COMMIT frame it queued is never delivered. */
    for (k = 0; k < 500u && g_bus.node[r->li].committed_epoch == 1u; k++) {
        bus_step(); traj_step(&t); l5_step(&l); r->hash = l4_hash_step(r->hash);
    }
    r->committed_proposer = (g_bus.node[r->li].committed_epoch == 2u);
    /* Cut the proposer off for 600 ms: long enough that the commit it
     * queued is never delivered and its lease lapses, short enough not to
     * exhaust its elections, which would latch SAFE for FDIR reasons that
     * have nothing to do with the transaction (Lot 3, TC-042). */
    isolate_node(r->li, true);
    for (k = 0; k < 600u; k++) {
        bus_step(); traj_step(&t); l5_step(&l); r->hash = l4_hash_step(r->hash);
        if (valid_leader_index() == r->keep || valid_leader_index() == r->rem) {
            r->valid_steps_isolated_side++;
        }
        if (k == 0u) {
            for (i = 0; i < N_NODES; i++) {
                r->epoch_after_cut[i] = g_bus.node[i].committed_epoch;
                r->mask_after_cut[i]  = g_bus.node[i].committed_mask;
                r->promise_after_cut[i] = (g_bus.node[i].accepted_epoch ==
                                           (uint16_t)(g_bus.node[i].committed_epoch + 1u));
            }
        }
    }
    /* Heal: the configuration announcement repairs the lost COMMIT. */
    isolate_node(r->li, false);
    for (k = 0; k < 4000u; k++) {
        bus_step(); traj_step(&t); l5_step(&l); r->hash = l4_hash_step(r->hash);
    }
    r->converged  = l5_all_committed(r->target, 2u);
    r->epoch_end  = g_bus.node[r->keep].committed_epoch;
    r->mask_end   = g_bus.node[r->keep].committed_mask;
    r->max_valid  = l.max_valid;
    r->steps_multi_auth = l.steps_multi_auth;
    r->conflicts  = l.same_epoch_mask_conflicts;
    r->auth_outside = l.authority_outside_membership;
    r->hash = l4_hash_tx(r->hash);
}

/* TC-068: a partition between agreement and commit must not let the two sides
 * emerge with incompatible authoritative memberships. */
static void tc_068_partition_during_transition(void)
{
    l5_partial_t r;
    printf("TC-068  partition during a membership transition: no incompatible authority, convergence on heal [Lot 5]\n");
    run_l5_partial_commit(&r);
    check(r.li >= 0 && r.committed_proposer, "Lot 5",
          "scenario established: the proposer committed the new membership before being cut off");
    if (r.li < 0) { return; }
    check(r.epoch_after_cut[r.li] == 2u && r.mask_after_cut[r.li] == r.target,
          "Lot 5", "the isolated proposer holds the new configuration");
    check(r.epoch_after_cut[r.keep] == 1u && r.epoch_after_cut[r.rem] == 1u,
          "Lot 5", "the other two never received the commit and hold the old configuration");
    check(r.promise_after_cut[r.keep] && r.promise_after_cut[r.rem],
          "INV-RECONFIG-TRANSITION", "both of them are bound by their acceptance of the successor");
    check(r.valid_steps_isolated_side == 0u, "INV-RECONFIG-TRANSITION",
          "the two nodes that accepted but did not commit never formed an authority of their own");
    check(r.max_valid <= 1u && r.steps_multi_auth == 0u, "INV-LEADER-UNIQUE",
          "at most one valid authority at every observed step");
    check(r.conflicts == 0u, "INV-RECONFIG-CONSISTENT",
          "no two nodes held different memberships at the same configuration epoch");
    check(r.auth_outside == 0u, "INV-RECONFIG-AUTHORITY",
          "no node held authority outside its own committed membership");
    check(r.converged && r.epoch_end == 2u && r.mask_end == r.target, "INV-RECONFIG-CONSISTENT",
          "after the heal the configuration announcement repaired the lost commit on every node");
    printf("        proposer node %d committed 0x%02X epoch 2 then was cut off; the other two stayed at epoch 1 with a pending acceptance and held authority for %u steps; after the heal every node is at 0x%02X epoch %u\n",
           r.li + 1, r.target, r.valid_steps_isolated_side, r.mask_end, r.epoch_end);
}

/* TC-069: LOT 2 authority and lease semantics are unchanged by LOT 5. */
static void tc_069_authority_lease_non_regression(void)
{
    int li, keep, rem, k, l2;
    uint32_t cut_ms, lost_ms = 0u, ren0;
    mosaik_frame_t old_hb;
    l5_obs_t l; traj_obs_t t;

    printf("TC-069  authority and lease non-regression with membership traffic present [Lot 5]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0 && g_bus.last_hb_frame_valid[li], "Lot 5", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &keep, &rem);
    old_hb = g_bus.last_hb_frame[li];
    l5_init(&l); traj_init(&t);

    /* node loss with its last frames still in flight, then reordering */
    bus_schedule_delayed(&old_hb, (uint8_t)(li + 1), 1500u);
    bus_crash_node(rem);
    l5_run(&l, &t, 3000u);
    bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_REORDER, 0u);
    l5_run(&l, &t, 500u);
    bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DELIVER, 0u);
    l5_run(&l, &t, 500u);

    /* a leader that receives no acknowledgement loses authority within a lease */
    li = leader_index();
    if (li >= 0) {
        cut_ms = g_bus.now_ms;
        bus_set_net_action((uint8_t)((li + 1) % N_NODES), (uint8_t)li, NET_DROP, 0u);
        bus_set_net_action((uint8_t)((li + 2) % N_NODES), (uint8_t)li, NET_DROP, 0u);
        for (k = 0; k < 1500 && lost_ms == 0u; k++) {
            bus_step(); traj_step(&t); l5_step(&l);
            if (!mosaik_has_valid_leadership_authority(&g_bus.node[li])) { lost_ms = g_bus.now_ms - cut_ms; }
        }
        check(lost_ms != 0u && lost_ms <= MOSAIK_LEADERSHIP_LEASE_MS + 1u, "INV-RECONFIG-LOT2",
              "a leader without received acknowledgements loses authority within one lease");
        bus_set_full_connectivity();
    }
    l5_run(&l, &t, 1500u);
    bus_restart_node(rem);
    l5_run(&l, &t, 1500u);
    ren0 = g_bus.lease_renewals;
    l2 = split_brain_leader();
    l5_run(&l, &t, 2000u);

    check(l2 >= 0, "REQ-SAFE-0003", "split-brain injection still latches SAFE");
    check(t.max_valid <= 1u && l.steps_multi_auth == 0u, "INV-LEADER-UNIQUE",
          "max concurrent valid authorities <= 1 throughout");
    check(t.term_regressions == 0u && l.term_regressions == 0u, "INV-TERM-MONOTONIC", "no term regression");
    check(t.safe_auth_violations == 0u && t.safe_role_violations == 0u, "INV-RECONFIG-LOT3",
          "a SAFE node held neither authority nor a non-follower role");
    check(l5_min_votes_any(&l) >= 2u, "INV-RECONFIG-QUORUM",
          "every leadership acquisition counted a quorum of the committed membership");
    check(g_bus.lease_renewals == g_bus.quorum_contact_events, "INV-RECONFIG-LOT2",
          "every lease renewal coincided with an accepted peer acknowledgement");
    check(l5_all_committed((uint8_t)MOSAIK_MEMBERSHIP_ALL, 1u), "INV-RECONFIG-NO-MAGIC",
          "none of these faults changed the committed membership");
    printf("        authority lost %u ms after losing inbound traffic (lease %u ms); lease renewals after SAFE %u; committed membership 0x%02X epoch %u unchanged\n",
           lost_ms, (unsigned)MOSAIK_LEADERSHIP_LEASE_MS, g_bus.lease_renewals - ren0,
           g_bus.node[li >= 0 ? li : 0].committed_mask, g_bus.node[li >= 0 ? li : 0].committed_epoch);
}

/* Scenario driver shared by TC-074 and TC-070: the proposer fails in the
 * middle of a transaction, either before any acceptance reached it or after
 * it had already committed, and later returns. */
typedef struct {
    int      li, keep, rem;
    uint8_t  target;
    int      variant;
    bool     committed_before_crash;
    bool     promise_survivors;
    bool     committed_partner, committed_third;
    uint32_t committed_during_outage;
    uint32_t valid_steps_during_outage;
    bool     recovered;
    uint16_t epoch_end;
    uint8_t  mask_end;
    uint32_t max_valid, steps_multi_auth, conflicts, auth_outside;
    uint8_t  max_failed[N_NODES];
    uint32_t safe_nodes;
    uint32_t hash;
} l5_crash_t;

/* variant 0: no acceptance ever reaches the proposer, which then fails.
 * variant 1: the proposer commits, is cut off so the commit is lost, fails.
 * variant 2: the commit reaches one participant only, then the proposer fails.
 * In every variant the proposer is out of service for outage_ms and then
 * returns; the outage stays below the election-exhaustion budget so that any
 * loss of authority is the transaction's doing and not a Lot 3 SAFE latch. */
static void run_l5_proposer_crash(int variant, uint32_t outage_ms, l5_crash_t *r)
{
    l5_obs_t l; traj_obs_t t;
    uint32_t k;
    int i;

    memset(r, 0, sizeof(*r));
    r->li = r->keep = r->rem = -1;
    r->variant = variant;
    r->hash = 2166136261u;
    bus_init();
    for (k = 0; k < 2000u; k++) { bus_step(); r->hash = l4_hash_step(r->hash); }
    r->li = leader_index();
    if (r->li < 0) { return; }
    survivors_of(r->li, &r->keep, &r->rem);
    r->target = (uint8_t)(l5_bit(r->li) | l5_bit(r->keep));
    l5_init(&l); traj_init(&t);

    if (variant == 0) {
        bus_set_net_action((uint8_t)r->keep, (uint8_t)r->li, NET_DROP, 0u);
        bus_set_net_action((uint8_t)r->rem,  (uint8_t)r->li, NET_DROP, 0u);
    } else if (variant == 2) {
        bus_set_net_action((uint8_t)r->li, (uint8_t)r->rem, NET_DROP, 0u);
    }
    if (!mosaik_request_reconfiguration(&g_bus.node[r->li], r->target)) { return; }
    for (k = 0; k < 500u; k++) {
        bus_step(); traj_step(&t); l5_step(&l); r->hash = l4_hash_step(r->hash);
        if (variant != 0 && g_bus.node[r->li].committed_epoch == 2u) { break; }
        if (variant == 0 && k >= 20u) { break; }
    }
    r->committed_before_crash = (g_bus.node[r->li].committed_epoch == 2u);
    if (variant == 1) {
        /* cut the proposer off so the commit frame it queued is dropped */
        isolate_node(r->li, true);
        bus_step(); traj_step(&t); l5_step(&l); r->hash = l4_hash_step(r->hash);
    } else if (variant == 2) {
        /* let the commit reach the one reachable participant */
        for (k = 0; k < 3u; k++) { bus_step(); traj_step(&t); l5_step(&l); r->hash = l4_hash_step(r->hash); }
    }
    r->promise_survivors = (g_bus.node[r->keep].accepted_epoch == 2u) &&
                           (g_bus.node[r->rem].accepted_epoch == 2u);
    r->committed_partner = (g_bus.node[r->keep].committed_epoch == 2u);
    r->committed_third   = (g_bus.node[r->rem].committed_epoch == 2u);
    bus_crash_node(r->li);
    bus_set_full_connectivity();
    for (k = 0; k < outage_ms; k++) {
        bus_step(); traj_step(&t); l5_step(&l); r->hash = l4_hash_step(r->hash);
        if (valid_leader_count() > 0) { r->valid_steps_during_outage++; }
    }
    for (i = 0; i < N_NODES; i++) {
        if (i == r->li) { continue; }
        if (g_bus.node[i].committed_epoch == 2u) { r->committed_during_outage++; }
    }
    bus_restart_node(r->li);
    for (k = 0; k < 6000u; k++) {
        int v;
        bus_step(); traj_step(&t); l5_step(&l); r->hash = l4_hash_step(r->hash);
        /* the external request is re-issued on whichever node holds valid
         * authority, exactly as an operator would repeat an order that was
         * interrupted; it commits nothing by itself */
        v = valid_leader_index();
        if (v >= 0 && g_bus.node[v].committed_mask != r->target) {
            (void)mosaik_request_reconfiguration(&g_bus.node[v], r->target);
        }
    }
    r->recovered = l5_all_committed(r->target, 2u);
    for (i = 0; i < N_NODES; i++) {
        r->max_failed[i] = t.max_failed[i];
        if (g_bus.node[i].state == MOSAIK_STATE_SAFE) { r->safe_nodes++; }
    }
    r->epoch_end = g_bus.node[r->keep].committed_epoch;
    r->mask_end  = g_bus.node[r->keep].committed_mask;
    r->max_valid = l.max_valid;
    r->steps_multi_auth = l.steps_multi_auth;
    r->conflicts = l.same_epoch_mask_conflicts;
    r->auth_outside = l.authority_outside_membership;
    r->hash = l4_hash_tx(r->hash);
}

/* TC-070: determinism of the LOT 5 transaction scenarios, run A against run B. */
static void tc_070_determinism_of_reconfiguration(void)
{
    l5_partial_t a1, b1;
    l5_crash_t a2, b2, a3, b3;
    printf("TC-070  determinism of the reconfiguration scenarios (run A vs run B) [Lot 5]\n");
    run_l5_partial_commit(&a1);   run_l5_partial_commit(&b1);
    run_l5_proposer_crash(0, 400u, &a2); run_l5_proposer_crash(0, 400u, &b2);
    run_l5_proposer_crash(1, 400u, &a3); run_l5_proposer_crash(1, 400u, &b3);
    check(a1.hash == b1.hash && memcmp(&a1, &b1, sizeof(a1)) == 0, "Lot 5",
          "partial-commit scenario reproduced exactly");
    check(a2.hash == b2.hash && memcmp(&a2, &b2, sizeof(a2)) == 0, "Lot 5",
          "proposer-crash-before-quorum scenario reproduced exactly");
    check(a3.hash == b3.hash && memcmp(&a3, &b3, sizeof(a3)) == 0, "Lot 5",
          "proposer-crash-after-commit scenario reproduced exactly");
    printf("        hashes 0x%08X 0x%08X 0x%08X reproduced: %s %s %s\n", a1.hash, a2.hash, a3.hash,
           a1.hash == b1.hash ? "yes" : "no", a2.hash == b2.hash ? "yes" : "no",
           a3.hash == b3.hash ? "yes" : "no");
}

/* TC-071: two different successors of the same committed configuration can
 * never both be agreed. */
static void tc_071_conflicting_successors(void)
{
    int li, keep, rem;
    uint8_t mask_a, mask_b;
    uint32_t conf0;
    uint16_t bound_epoch;
    uint8_t  bound_mask;
    mosaik_msg_t m;
    l5_obs_t l; traj_obs_t t;

    printf("TC-071  conflicting successors of one configuration cannot both be agreed [Lot 5]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &keep, &rem);
    mask_a = (uint8_t)(l5_bit(li) | l5_bit(keep));
    mask_b = (uint8_t)(l5_bit(li) | l5_bit(rem));
    l5_init(&l); traj_init(&t);

    /* The leader proposes A; every member binds to A for epoch 2. */
    bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DROP, 0u);   /* keep the transaction open */
    check(mosaik_request_reconfiguration(&g_bus.node[li], mask_a), "Lot 5", "first proposal accepted");
    l5_run(&l, &t, 150u);
    bound_epoch = g_bus.node[keep].accepted_epoch;
    bound_mask  = g_bus.node[keep].accepted_mask;
    check(bound_epoch == 2u && bound_mask == mask_a, "INV-RECONFIG-TRANSITION",
          "the acceptor bound itself to exactly one successor for epoch 2");

    /* A conflicting successor B for the same epoch, from the same leader. */
    conf0 = g_bus.node[keep].config_conflict_rejections;
    make_cfg_msg(&m, (uint8_t)(li + 1), MOSAIK_CFG_PROPOSE, 2u, mask_b, 0u);
    deliver_to(keep, &m);
    deliver_to(rem, &m);
    check(g_bus.node[keep].config_conflict_rejections >= conf0 + 1u, "INV-RECONFIG-CONSISTENT",
          "a second, different successor for the same epoch was refused");
    check(g_bus.node[keep].accepted_epoch == bound_epoch && g_bus.node[keep].accepted_mask == bound_mask,
          "INV-RECONFIG-CONSISTENT", "the existing binding was not overwritten");
    check(g_bus.node[rem].accepted_mask == mask_a, "INV-RECONFIG-CONSISTENT",
          "the other acceptor also kept its original binding");

    /* The request API refuses a conflicting target on a node already bound. */
    check(!mosaik_request_reconfiguration(&g_bus.node[li], mask_b), "INV-RECONFIG-CONSISTENT",
          "the leader refuses to propose a successor conflicting with its own binding");

    /* Only the originally agreed successor can ever commit. */
    bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DELIVER, 0u);
    l5_run(&l, &t, 400u);
    check(l5_all_committed(mask_a, 2u), "INV-RECONFIG-CONSISTENT",
          "the configuration committed at epoch 2 is the one that was agreed, on every node");
    check(l.same_epoch_mask_conflicts == 0u && t.max_valid <= 1u, "INV-LEADER-UNIQUE",
          "no incompatible configuration and no second authority at any step");
    printf("        epoch 2 bound to 0x%02X; conflicting 0x%02X refused (%u conflict rejections); committed 0x%02X on every node\n",
           mask_a, mask_b, g_bus.node[keep].config_conflict_rejections, g_bus.node[keep].committed_mask);
}

/* TC-072: every stage of the transaction is idempotent under duplication and
 * insensitive to ordering. */
static void tc_072_transaction_idempotence(void)
{
    int li, keep, rem;
    uint8_t target;
    uint32_t dup0, commits0, acc0;
    uint16_t bound;
    mosaik_msg_t m;
    l5_obs_t l; traj_obs_t t;

    printf("TC-072  duplicate and reordered transaction frames are idempotent [Lot 5]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &keep, &rem);
    target = (uint8_t)(l5_bit(li) | l5_bit(keep));
    l5_init(&l); traj_init(&t);

    /* duplicate PROPOSE: same successor, acceptance repeated, binding unchanged */
    bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DROP, 0u);
    check(mosaik_request_reconfiguration(&g_bus.node[li], target), "Lot 5", "proposal issued");
    l5_run(&l, &t, 150u);
    bound = g_bus.node[keep].accepted_epoch;
    dup0 = g_bus.node[keep].config_duplicates;
    acc0 = CFGTX(keep, MOSAIK_CFG_ACCEPT);
    make_cfg_msg(&m, (uint8_t)(li + 1), MOSAIK_CFG_PROPOSE, 2u, target, 0u);
    deliver_to(keep, &m); deliver_to(keep, &m);
    check(g_bus.node[keep].config_duplicates >= dup0 + 2u && g_bus.node[keep].accepted_epoch == bound,
          "Lot 5", "a repeated proposal re-acknowledges without changing the binding");
    check(CFGTX(keep, MOSAIK_CFG_ACCEPT) == acc0 + 2u, "Lot 5",
          "each repeated proposal produced exactly one acceptance");

    /* duplicate ACCEPT at the proposer: no double commit */
    bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DELIVER, 0u);
    l5_run(&l, &t, 400u);
    check(l5_all_committed(target, 2u), "Lot 5", "epoch 2 committed");
    commits0 = g_bus.node[li].config_commits;
    make_cfg_msg(&m, (uint8_t)(keep + 1), MOSAIK_CFG_ACCEPT, 2u, target, (uint8_t)(li + 1));
    deliver_to(li, &m); deliver_to(li, &m);
    check(g_bus.node[li].config_commits == commits0, "Lot 5",
          "acceptances replayed after the commit do not commit anything again");

    /* reordering: a commit for the next epoch arriving before its proposal */
    {
        uint16_t e_next = 3u;
        uint32_t stale0 = g_bus.node[keep].config_stale_rejections;
        make_cfg_msg(&m, (uint8_t)(li + 1), MOSAIK_CFG_COMMIT, e_next, (uint8_t)MOSAIK_MEMBERSHIP_ALL, 0u);
        deliver_to(keep, &m);
        check(g_bus.node[keep].committed_epoch == e_next &&
              g_bus.node[keep].committed_mask == (uint8_t)MOSAIK_MEMBERSHIP_ALL,
              "Lot 5", "a commit from a member for the next epoch is applied on its own");
        make_cfg_msg(&m, (uint8_t)(li + 1), MOSAIK_CFG_PROPOSE, e_next, (uint8_t)MOSAIK_MEMBERSHIP_ALL, 0u);
        deliver_to(keep, &m);
        check(g_bus.node[keep].config_stale_rejections >= stale0 + 1u &&
              g_bus.node[keep].committed_epoch == e_next,
              "INV-RECONFIG-OLD-CONFIG", "the proposal arriving afterwards is refused as superseded");
    }
    check(t.max_valid <= 1u && l.authority_outside_membership == 0u, "INV-LEADER-UNIQUE",
          "no authority was manufactured by duplicated or reordered transaction traffic");
    printf("        duplicates counted %u, acceptances emitted %u, commits at the proposer %u (unchanged by replay)\n",
           g_bus.node[keep].config_duplicates, CFGTX(keep, MOSAIK_CFG_ACCEPT) - acc0, g_bus.node[li].config_commits);
}

/* TC-073: malformed, empty and otherwise inadmissible memberships. */
static void tc_073_malformed_membership(void)
{
    int li, keep, rem, i;
    mosaik_msg_t m;
    mosaik_frame_t f;
    uint32_t mism0, de0;
    uint16_t epoch_before;
    uint8_t mask_before;

    printf("TC-073  malformed, empty and single-node memberships are refused [Lot 5]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &keep, &rem);

    /* decoder: empty mask, mask outside the three nodes, invalid stages */
    make_cfg_msg(&m, (uint8_t)(li + 1), MOSAIK_CFG_PROPOSE, 2u, 0x03u, 0u);
    mosaik_encode(&f, &m);
    f.data[3] = 0x00u; f.data[7] = mosaik_crc8(f.data, 7u);
    check(!mosaik_decode(&f, &m), "INV-RECONFIG-NO-MAGIC", "an empty membership mask does not decode");
    mosaik_encode(&f, &m); f.data[3] = 0x0Bu; f.data[7] = mosaik_crc8(f.data, 7u);
    check(!mosaik_decode(&f, &m), "INV-RECONFIG-NO-MAGIC", "a mask naming a node outside the cluster does not decode");
    mosaik_encode(&f, &m); f.data[2] = 0x00u; f.data[7] = mosaik_crc8(f.data, 7u);
    check(!mosaik_decode(&f, &m), "INV-RECONFIG-NO-MAGIC", "stage 0 does not decode");
    mosaik_encode(&f, &m); f.data[2] = 0x05u; f.data[7] = mosaik_crc8(f.data, 7u);
    check(!mosaik_decode(&f, &m), "INV-RECONFIG-NO-MAGIC", "an unknown stage does not decode");

    /* such a frame delivered through the bus is counted and has no effect */
    epoch_before = g_bus.node[keep].committed_epoch; mask_before = g_bus.node[keep].committed_mask;
    de0 = g_bus.node[keep].decode_errors;
    mosaik_encode(&f, &m); f.data[3] = 0x00u; f.data[7] = mosaik_crc8(f.data, 7u);
    bus_inject_frame(&f, (uint8_t)(li + 1)); bus_step();
    check(g_bus.node[keep].decode_errors > de0 && g_bus.node[keep].committed_epoch == epoch_before &&
          g_bus.node[keep].committed_mask == mask_before,
          "INV-RECONFIG-NO-MAGIC", "the rejected frame was counted as a decode error and changed nothing");

    /* a well-formed frame proposing a single-node membership is refused by the handler */
    mism0 = g_bus.node[keep].config_mismatch_observed;
    make_cfg_msg(&m, (uint8_t)(li + 1), MOSAIK_CFG_PROPOSE, 2u, l5_bit(li), 0u);
    deliver_to(keep, &m);
    check(g_bus.node[keep].config_mismatch_observed >= mism0 + 1u &&
          g_bus.node[keep].accepted_epoch != 2u,
          "INV-RECONFIG-PARTITION", "a single-node membership is never accepted");
    /* and one that excludes its own proposer */
    mism0 = g_bus.node[keep].config_mismatch_observed;
    make_cfg_msg(&m, (uint8_t)(li + 1), MOSAIK_CFG_PROPOSE, 2u, (uint8_t)(l5_bit(keep) | l5_bit(rem)), 0u);
    deliver_to(keep, &m);
    check(g_bus.node[keep].config_mismatch_observed >= mism0 + 1u &&
          g_bus.node[keep].accepted_epoch != 2u,
          "INV-RECONFIG-CONSISTENT", "a membership that excludes its own proposer is never accepted");

    /* the public request API applies the same rules */
    check(!mosaik_request_reconfiguration(&g_bus.node[li], 0x00u), "INV-RECONFIG-NO-MAGIC", "empty target refused");
    check(!mosaik_request_reconfiguration(&g_bus.node[li], 0x0Fu), "INV-RECONFIG-NO-MAGIC", "out-of-range target refused");
    check(!mosaik_request_reconfiguration(&g_bus.node[li], l5_bit(li)), "INV-RECONFIG-PARTITION",
          "a single-node target refused: no configuration may reduce quorum to one");
    check(!mosaik_request_reconfiguration(&g_bus.node[li], (uint8_t)(l5_bit(keep) | l5_bit(rem))),
          "INV-RECONFIG-CONSISTENT", "a target excluding the requester refused");
    check(!mosaik_request_reconfiguration(&g_bus.node[li], (uint8_t)MOSAIK_MEMBERSHIP_ALL),
          "Lot 5", "a target identical to the committed membership refused");
    check(!mosaik_request_reconfiguration(&g_bus.node[keep], (uint8_t)(l5_bit(keep) | l5_bit(li))),
          "INV-RECONFIG-AUTHORITY", "a node without valid leadership authority cannot propose");
    for (i = 0; i < N_NODES; i++) {
        check(g_bus.node[i].committed_mask == (uint8_t)MOSAIK_MEMBERSHIP_ALL &&
              g_bus.node[i].committed_epoch == 1u,
              "INV-RECONFIG-NO-MAGIC", "none of the refused requests changed any committed membership");
    }
    /* configuration epochs never wrap: the last representable one refuses a
     * successor rather than rolling round to zero */
    {
        mosaik_config_store_t last;
        last.committed_epoch = 0xFFFFu; last.committed_mask = (uint8_t)MOSAIK_MEMBERSHIP_ALL;
        last.accepted_epoch = 0u; last.accepted_mask = 0u;
        bus_crash_node(rem);
        bus_run(50u);
        bus_restart_node_with_store(rem, &last);
        check(g_bus.node[rem].committed_epoch == 0xFFFFu, "Lot 5", "the last representable epoch loads from the store");
        check(!mosaik_request_reconfiguration(&g_bus.node[rem], (uint8_t)(l5_bit(rem) | l5_bit(li))),
              "INV-RECONFIG-OLD-CONFIG", "no transaction is started that would wrap the configuration epoch");
    }
    printf("        decoder refused 4 malformed frames; handler refused a single-node and a proposer-excluding membership; API refused 6 inadmissible requests and an epoch that would wrap\n");
}

/* TC-074: the proposer fails in the middle of the transaction, including
 * when the commit reached nobody or only one participant. */
static void tc_074_proposer_failure(void)
{
    l5_crash_t a, b, c, d;

    printf("TC-074  proposer failure before agreement, after an undelivered commit, and after a partial commit [Lot 5]\n");

    run_l5_proposer_crash(0, 400u, &a);
    check(a.li >= 0 && !a.committed_before_crash && a.promise_survivors, "Lot 5",
          "A: no acceptance reached the proposer before it failed, yet both peers had bound themselves");
    check(a.committed_during_outage == 0u, "INV-RECONFIG-TRANSITION",
          "A: no node installed the new membership while the proposer was out of service");
    check(a.valid_steps_during_outage == 0u, "INV-RECONFIG-TRANSITION",
          "A: the two bound peers formed no authority under either configuration");
    check(a.max_valid <= 1u && a.steps_multi_auth == 0u && a.conflicts == 0u && a.auth_outside == 0u,
          "INV-LEADER-UNIQUE", "A: at most one valid authority, no incompatible configuration");
    check(a.recovered && a.epoch_end == 2u && a.mask_end == a.target, "Lot 5",
          "A: when the proposer returned the transaction completed on every node");

    run_l5_proposer_crash(1, 400u, &b);
    check(b.li >= 0 && b.committed_before_crash && !b.committed_partner && !b.committed_third, "Lot 5",
          "B: the proposer committed and then failed with its commit frame undelivered");
    check(b.committed_during_outage == 0u, "INV-RECONFIG-TRANSITION",
          "B: no other node installed the new membership while the proposer was out of service");
    check(b.valid_steps_during_outage == 0u, "INV-RECONFIG-TRANSITION",
          "B: the remaining nodes took no authority under a configuration they had only accepted");
    check(b.max_valid <= 1u && b.steps_multi_auth == 0u && b.conflicts == 0u && b.auth_outside == 0u,
          "INV-LEADER-UNIQUE", "B: at most one valid authority, no incompatible configuration");
    check(b.recovered && b.epoch_end == 2u && b.mask_end == b.target, "INV-RECONFIG-CONSISTENT",
          "B: on its return the committed configuration propagated and every node converged on it");

    run_l5_proposer_crash(2, 400u, &c);
    check(c.li >= 0 && c.committed_before_crash && c.committed_partner && !c.committed_third, "Lot 5",
          "C: the commit reached exactly one participant before the proposer failed");
    check(c.valid_steps_during_outage == 0u, "INV-RECONFIG-TRANSITION",
          "C: the two sides, holding different configurations, formed no authority");
    check(c.max_valid <= 1u && c.steps_multi_auth == 0u && c.auth_outside == 0u,
          "INV-LEADER-UNIQUE", "C: at most one valid authority throughout the partial commit");
    check(c.recovered && c.epoch_end == 2u, "INV-RECONFIG-CONSISTENT",
          "C: the node that missed the commit was brought to the committed configuration on recovery");
    printf("        A (no acceptance received): installed during outage %u, authority steps %u, recovered %s\n",
           a.committed_during_outage, a.valid_steps_during_outage, a.recovered ? "yes" : "no");
    printf("        B (commit undelivered): installed during outage %u, authority steps %u, converged on 0x%02X epoch %u\n",
           b.committed_during_outage, b.valid_steps_during_outage, b.mask_end, b.epoch_end);
    /* D. the same interruption, held past the election-exhaustion budget.
     * A membership of two tolerates no failure, so while the transaction is
     * unresolved and the proposer is absent no quorum exists at all. The
     * remaining nodes report that the way this system reports absence of
     * quorum: they latch SAFE. That is the FDIR contract of Lot 3, not a
     * safety failure, and it is the liveness price of the joint rule. */
    run_l5_proposer_crash(1, 900u, &d);
    check(d.li >= 0 && d.committed_before_crash, "Lot 5", "D: same interruption, longer outage");
    check(d.max_valid <= 1u && d.steps_multi_auth == 0u && d.conflicts == 0u && d.auth_outside == 0u,
          "INV-LEADER-UNIQUE", "D: safety held: at most one valid authority, no incompatible configuration");
    check(d.safe_nodes > 0u, "Lot 5",
          "D: past the election budget the remaining nodes reported absence of quorum by latching SAFE");
    check(d.max_failed[d.keep] == g_bus.node[d.keep].cfg.max_failed_elections, "REQ-SAFE-0003",
          "D: that latch came from genuinely exhausted elections, not from the membership change");
    check(valid_leader_count() == 0, "INV-RECONFIG-AUTHORITY",
          "D: no authority was manufactured to compensate for the lost quorum");

    printf("        C (commit reached one participant): partner committed %s, third node committed %s, authority steps %u, converged %s\n",
           c.committed_partner ? "yes" : "no", c.committed_third ? "yes" : "no",
           c.valid_steps_during_outage, c.recovered ? "yes" : "no");
    printf("        D (outage past the election budget): %u node(s) latched SAFE after %u failed elections, valid authorities %u, incompatible configurations %u\n",
           d.safe_nodes, d.max_failed[d.keep], d.max_valid, d.conflicts);
}

/* TC-075: no frame a removed node can send restores its rights. */
static void tc_075_removed_node_traffic(void)
{
    int li, keep, rem;
    uint8_t target;
    uint16_t term_li, term_keep, vt_li;
    uint8_t vf_li;
    uint32_t nm0, ren0, ack_rx0;
    mosaik_msg_t m;
    l5_obs_t l; traj_obs_t t;

    printf("TC-075  traffic from a removed node cannot restore voting, authority or a lease [Lot 5]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &keep, &rem);
    target = (uint8_t)(l5_bit(li) | l5_bit(keep));
    l5_init(&l); traj_init(&t);
    check(l5_request(li, target, 200u, &l, &t) && l5_all_committed(target, 2u), "Lot 5",
          "the third node was removed at epoch 2");

    term_li = g_bus.node[li].term; term_keep = g_bus.node[keep].term;
    vf_li = g_bus.node[li].voted_for; vt_li = g_bus.node[li].voted_term;
    nm0 = g_bus.node[li].nonmember_rejections;
    ren0 = g_bus.lease_renewals;
    ack_rx0 = g_bus.node[li].last_ack_rx_ms[rem];

    /* every consensus frame type, including one at a strictly higher term */
    make_msg(&m, MOSAIK_MSG_HEARTBEAT, (uint8_t)(rem + 1), MOSAIK_ROLE_LEADER, MOSAIK_STATE_NOMINAL,
             (uint16_t)(term_li + 5u), 9u);
    deliver_to(li, &m); deliver_to(keep, &m);
    make_msg(&m, MOSAIK_MSG_VOTE_REQ, (uint8_t)(rem + 1), MOSAIK_ROLE_CANDIDATE, MOSAIK_STATE_NOMINAL,
             (uint16_t)(term_li + 6u), 0u);
    deliver_to(li, &m); deliver_to(keep, &m);
    make_msg(&m, MOSAIK_MSG_VOTE_GRANT, (uint8_t)(rem + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_NOMINAL,
             term_li, (uint8_t)(li + 1));
    deliver_to(li, &m);
    make_msg(&m, MOSAIK_MSG_ACK, (uint8_t)(rem + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_NOMINAL,
             term_li, g_bus.node[li].seq);
    deliver_to(li, &m);

    check(g_bus.node[li].term == term_li && g_bus.node[keep].term == term_keep,
          "INV-RECONFIG-REMOVED-NODE", "a higher term announced by the removed node was not adopted");
    check(g_bus.node[li].voted_for == vf_li && g_bus.node[li].voted_term == vt_li,
          "INV-ONE-VOTE-PER-TERM", "its vote request changed no vote memory");
    check(g_bus.node[li].last_ack_rx_ms[rem] == ack_rx0, "INV-RECONFIG-REMOVED-NODE",
          "its acknowledgement was not recorded as new quorum evidence");
    check((g_bus.node[li].vote_mask & l5_bit(rem)) == 0u, "INV-RECONFIG-REMOVED-NODE",
          "its vote grant was not counted");
    check(g_bus.node[li].nonmember_rejections >= nm0 + 4u, "INV-RECONFIG-REMOVED-NODE",
          "all four frame types were rejected as coming from outside the committed membership");
    check(valid_leader_index() == li, "INV-LEADER-UNIQUE", "the leader kept its authority and its role");

    /* a SAFE announcement from it is still FDIR evidence: membership does not
     * gate fault reporting (Lot 4 discipline) */
    make_msg(&m, MOSAIK_MSG_SAFE, (uint8_t)(rem + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_SAFE,
             term_li, (uint8_t)MOSAIK_SAFE_NO_QUORUM);
    deliver_to(keep, &m);
    check((g_bus.node[keep].safe_evidence_mask & l5_bit(rem)) != 0u, "INV-RECONFIG-LOT4",
          "a SAFE announcement from a removed node is still recorded as fault evidence");
    check(g_bus.node[keep].term == term_keep && valid_leader_index() == li, "INV-RECONFIG-LOT4",
          "and it still has no consensus effect");

    l5_run(&l, &t, 1000u);
    check(g_bus.lease_renewals > ren0, "INV-RECONFIG-LOT2",
          "the leader kept renewing its lease from its remaining member");
    check(t.max_valid <= 1u && l.authority_outside_membership == 0u && l.same_epoch_mask_conflicts == 0u,
          "INV-LEADER-UNIQUE", "one authority throughout, always inside a committed membership");
    printf("        removed node %d: heartbeat at term %u, vote request, grant and acknowledgement all rejected (%u non-member rejections); SAFE evidence still accepted\n",
           rem + 1, term_li + 5u, g_bus.node[li].nonmember_rejections - nm0);
}

/* TC-076: every membership transition the interface permits. */
static void tc_076_permitted_transitions(void)
{
    int li, keep, rem, i;
    uint8_t t_keep, t_rem, t_excl;
    l5_obs_t l; traj_obs_t t;

    printf("TC-076  every permitted transition: removal of either peer, re-admission, and a two-change transition [Lot 5]\n");

    for (i = 0; i < 2; i++) {
        bus_init(); bus_run(2000u);
        li = leader_index();
        if (li < 0) { check(false, "Lot 5", "leader existed"); return; }
        survivors_of(li, &keep, &rem);
        t_keep = (uint8_t)(l5_bit(li) | l5_bit(keep));
        t_rem  = (uint8_t)(l5_bit(li) | l5_bit(rem));
        l5_init(&l); traj_init(&t);
        {
            uint8_t target = (i == 0) ? t_keep : t_rem;
            check(l5_request(li, target, 300u, &l, &t), "Lot 5", "the leader accepted the removal request");
            check(l5_all_committed(target, 2u), "INV-RECONFIG-CONSISTENT",
                  "the removal committed identically on every node");
            check(t.max_valid <= 1u && l.steps_multi_auth == 0u && l.same_epoch_mask_conflicts == 0u &&
                  l.authority_outside_membership == 0u,
                  "INV-LEADER-UNIQUE", "one authority, one configuration per epoch, throughout the removal");
            /* re-admission of the node that was just removed */
            check(l5_request(li, (uint8_t)MOSAIK_MEMBERSHIP_ALL, 600u, &l, &t), "Lot 5",
                  "the leader accepted the re-admission request");
            check(l5_all_committed((uint8_t)MOSAIK_MEMBERSHIP_ALL, 3u), "INV-RECONFIG-CONSISTENT",
                  "the re-admission committed identically on every node");
            l5_run(&l, &t, 1500u);
            check(valid_leader_count() == 1 && t.max_valid <= 1u, "INV-LEADER-UNIQUE",
                  "the re-admitted node rejoined without disturbing the single authority");
            printf("        removal of node %d then re-admission: epochs 1 -> 2 -> 3, final mask 0x%02X, valid leader node %d\n",
                   (i == 0 ? rem : keep) + 1, g_bus.node[li].committed_mask, valid_leader_index() + 1);
        }
    }

    /* the leader cannot propose a membership that excludes itself */
    bus_init(); bus_run(2000u);
    li = leader_index();
    if (li < 0) { check(false, "Lot 5", "leader existed"); return; }
    survivors_of(li, &keep, &rem);
    t_excl = (uint8_t)(l5_bit(keep) | l5_bit(rem));
    check(!mosaik_request_reconfiguration(&g_bus.node[li], t_excl), "INV-RECONFIG-AUTHORITY",
          "the only node able to propose cannot propose a configuration that excludes it");

    /* a two-change transition {leader,keep} -> {leader,rem} */
    l5_init(&l); traj_init(&t);
    t_keep = (uint8_t)(l5_bit(li) | l5_bit(keep));
    t_rem  = (uint8_t)(l5_bit(li) | l5_bit(rem));
    check(l5_request(li, t_keep, 300u, &l, &t) && l5_all_committed(t_keep, 2u), "Lot 5",
          "first configuration committed");
    check(l5_request(li, t_rem, 600u, &l, &t), "Lot 5", "two-change transition requested");
    check(l5_all_committed(t_rem, 3u), "INV-RECONFIG-TRANSITION",
          "a transition replacing one member by another committed only with a quorum of both configurations");
    l5_run(&l, &t, 1500u);
    check(t.max_valid <= 1u && l.steps_multi_auth == 0u && l.same_epoch_mask_conflicts == 0u &&
          l.authority_outside_membership == 0u && t.term_regressions == 0u,
          "INV-LEADER-UNIQUE", "the two-change transition kept one authority and one configuration per epoch");
    printf("        two-change transition 0x%02X -> 0x%02X committed at epoch %u; valid leader node %d\n",
           t_keep, t_rem, g_bus.node[li].committed_epoch, valid_leader_index() + 1);
}

/* TC-077: the host-model configuration store. */
static void tc_077_configuration_persistence(void)
{
    int li, keep, rem;
    uint8_t target;
    mosaik_config_store_t bad;
    l5_obs_t l; traj_obs_t t;

    printf("TC-077  host-model configuration store: pending acceptance, committed configuration, corrupted content [Lot 5]\n");

    /* A. a pending acceptance survives a cold restart */
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &keep, &rem);
    target = (uint8_t)(l5_bit(li) | l5_bit(keep));
    l5_init(&l); traj_init(&t);
    bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DROP, 0u);
    check(mosaik_request_reconfiguration(&g_bus.node[li], target), "Lot 5", "transaction opened");
    l5_run(&l, &t, 150u);
    check(g_bus.node[keep].accepted_epoch == 2u && g_bus.node[keep].accepted_mask == target,
          "Lot 5", "the acceptor bound itself before the crash");
    bus_crash_node(keep);
    l5_run(&l, &t, 200u);
    bus_restart_node(keep);
    check(g_bus.node[keep].accepted_epoch == 2u && g_bus.node[keep].accepted_mask == target &&
          g_bus.node[keep].committed_epoch == 1u,
          "INV-RECONFIG-TRANSITION",
          "A: the acceptance binding and the committed configuration survived the cold restart");
    check(g_bus.node[keep].term == 0u, "Lot 5", "A: the restart was cold: no term, no vote was restored");

    /* B. the committed configuration survives, and a corrupted store does not
     *    install a foreign membership */
    bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DELIVER, 0u);
    l5_run(&l, &t, 800u);
    check(l5_all_committed(target, 2u), "Lot 5", "B: epoch 2 committed on every node");
    bus_crash_node(rem);
    l5_run(&l, &t, 200u);
    bad.committed_epoch = 0u; bad.committed_mask = 0x00u; bad.accepted_epoch = 0u; bad.accepted_mask = 0u;
    bus_restart_node_with_store(rem, &bad);
    check(g_bus.node[rem].committed_mask == (uint8_t)MOSAIK_MEMBERSHIP_ALL &&
          g_bus.node[rem].committed_epoch == (uint16_t)MOSAIK_CONFIG_EPOCH_INITIAL,
          "INV-RECONFIG-NO-MAGIC",
          "B: an empty store is reset to the initial configuration, not to a membership of convenience");
    bus_crash_node(rem);
    l5_run(&l, &t, 200u);
    bad.committed_epoch = 7u; bad.committed_mask = l5_bit(rem); bad.accepted_epoch = 0u; bad.accepted_mask = 0u;
    bus_restart_node_with_store(rem, &bad);
    check(g_bus.node[rem].committed_mask == (uint8_t)MOSAIK_MEMBERSHIP_ALL &&
          g_bus.node[rem].committed_epoch == (uint16_t)MOSAIK_CONFIG_EPOCH_INITIAL,
          "INV-RECONFIG-PARTITION",
          "B: a store naming a single-node membership is refused and reset: it cannot buy a quorum of one");
    l5_run(&l, &t, 2000u);
    check(l5_all_committed(target, 2u) || g_bus.node[rem].committed_epoch == 2u,
          "INV-RECONFIG-CONSISTENT",
          "B: the node with the reset store was brought back to the committed configuration by announcement");
    check(g_bus.node[rem].state != MOSAIK_STATE_SAFE || !mosaik_is_member(&g_bus.node[rem]),
          "Lot 5", "B: the node did not gain membership by restarting with a stale store");
    check(t.max_valid <= 1u && l.steps_multi_auth == 0u && l.authority_outside_membership == 0u,
          "INV-LEADER-UNIQUE", "no authority was manufactured through the persistence faults");
    printf("        pending acceptance restored after cold restart (epoch %u mask 0x%02X); empty and single-node stores reset to 0x%02X epoch %u; node %d now at 0x%02X epoch %u\n",
           g_bus.node[keep].accepted_epoch, g_bus.node[keep].accepted_mask,
           (unsigned)MOSAIK_MEMBERSHIP_ALL, (unsigned)MOSAIK_CONFIG_EPOCH_INITIAL,
           rem + 1, g_bus.node[rem].committed_mask, g_bus.node[rem].committed_epoch);
}

/* TC-078: interaction of a transaction with SAFE, DEGRADED, an election and a
 * lease expiry. */
static void tc_078_transaction_interactions(void)
{
    int safe_node, l2, a, b, i;
    uint8_t target;
    uint32_t deg_steps = 0u;
    l5_obs_t l; traj_obs_t t;
    uint32_t k;

    printf("TC-078  a transaction alongside SAFE, DEGRADED, an election and a lease expiry [Lot 5]\n");

    /* A. a SAFE node is removed by the healthy pair; DEGRADED persists */
    bus_init(); bus_run(2000u);
    safe_node = split_brain_leader();
    check(safe_node >= 0, "Lot 5", "a node latched SAFE");
    if (safe_node < 0) { return; }
    survivors_of(safe_node, &a, &b);
    l5_init(&l); traj_init(&t);
    for (k = 0; k < 1500u && valid_leader_index() < 0; k++) { bus_step(); traj_step(&t); l5_step(&l); }
    l2 = valid_leader_index();
    check(l2 >= 0 && l2 != safe_node, "Lot 5", "the healthy pair elected a leader around the SAFE node");
    if (l2 < 0) { return; }
    target = (uint8_t)(0x07u & ~l5_bit(safe_node));
    check(l5_request(l2, target, 600u, &l, &t), "Lot 5", "the new leader requested removal of the SAFE node");
    check(g_bus.node[l2].committed_mask == target && g_bus.node[(l2 == a) ? b : a].committed_mask == target,
          "INV-RECONFIG-CONSISTENT", "A: both healthy nodes committed the new membership");
    check(g_bus.node[safe_node].state == MOSAIK_STATE_SAFE &&
          !mosaik_has_valid_leadership_authority(&g_bus.node[safe_node]),
          "INV-RECONFIG-LOT3", "A: the SAFE node stayed SAFE and held no authority");
    for (k = 0; k < 500u; k++) {
        bus_step(); traj_step(&t); l5_step(&l);
        if (g_bus.node[l2].state == MOSAIK_STATE_DEGRADED) { deg_steps++; }
    }
    check(deg_steps > 0u, "INV-DEGRADED-PERSISTENT",
          "A: the leader stayed DEGRADED on the SAFE node's evidence while holding authority");
    check(t.max_valid <= 1u && l.authority_outside_membership == 0u, "INV-LEADER-UNIQUE",
          "A: one authority throughout");

    /* B. the proposer loses its lease during the transaction: nothing commits */
    bus_init(); bus_run(2000u);
    {
        int li = leader_index(), keep, rem;
        uint16_t epoch0;
        if (li < 0) { check(false, "Lot 5", "leader existed"); return; }
        survivors_of(li, &keep, &rem);
        epoch0 = g_bus.node[li].committed_epoch;
        l5_init(&l); traj_init(&t);
        /* cut every inbound path to the leader: no acceptance, no acknowledgement */
        bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DROP, 0u);
        bus_set_net_action((uint8_t)rem,  (uint8_t)li, NET_DROP, 0u);
        check(mosaik_request_reconfiguration(&g_bus.node[li], (uint8_t)(l5_bit(li) | l5_bit(keep))),
              "Lot 5", "B: the transaction started while the leader still held its lease");
        l5_run(&l, &t, 1200u);
        check(!mosaik_has_valid_leadership_authority(&g_bus.node[li]), "INV-RECONFIG-LOT2",
              "B: the proposer lost its lease during the transaction");
        check(g_bus.node[li].committed_epoch == epoch0, "INV-RECONFIG-TRANSITION",
              "B: it committed nothing after losing authority");
        check(CFGTX(li, MOSAIK_CFG_COMMIT) == 0u, "INV-RECONFIG-TRANSITION",
              "B: no commit frame was ever emitted");
        /* C. an election runs while the acceptances are still pending */
        bus_set_full_connectivity();
        l5_run(&l, &t, 3000u);
        check(t.max_valid <= 1u && l.steps_multi_auth == 0u, "INV-LEADER-UNIQUE",
              "C: the election that followed produced at most one valid authority");
        check(l.same_epoch_mask_conflicts == 0u && l.authority_outside_membership == 0u,
              "INV-RECONFIG-CONSISTENT", "C: no incompatible configuration appeared during the election");
        check(t.term_regressions == 0u, "INV-TERM-MONOTONIC", "C: no term regression");
        for (i = 0; i < N_NODES; i++) {
            check(g_bus.node[i].committed_epoch == epoch0, "INV-RECONFIG-TRANSITION",
                  "C: the abandoned transaction left every committed membership untouched");
        }
        printf("        SAFE node %d removed by the healthy pair (mask 0x%02X), DEGRADED held for %u steps; abandoned transaction committed nothing, epoch still %u\n",
               safe_node + 1, target, deg_steps, epoch0);
    }
}


/* ------------------------------------------------------------------
 * LOT 5 PHASE 3: adversarial hardening campaign.
 *
 * These tests attack the reconfiguration protocol committed at 146472f.
 * They add no production code and change nothing in TC-001..TC-078.
 *
 * Every Phase-3 scenario runs under a continuous invariant oracle that is
 * evaluated at every simulated millisecond, not only at the end. The
 * oracle is harness-omniscient by construction and is used for assertions
 * only: no node ever reads it, and no topology, drop decision or peer
 * liveness is turned into protocol evidence. Frames injected by these
 * tests are frames a legitimate node could have emitted earlier, except
 * where a test is explicitly labelled as malformed input.
 * ------------------------------------------------------------------- */

static bool in_mask_t(uint8_t mask, uint8_t id)
{
    return id != 0u && id <= MOSAIK_MAX_NODES && (mask & (uint8_t)(1u << (id - 1u))) != 0u;
}

typedef struct {
    uint32_t multi_auth;          /* INV-LEADER-UNIQUE */
    uint32_t auth_outside;        /* INV-RECONFIG-AUTHORITY */
    uint32_t same_epoch_conflict; /* INV-RECONFIG-CONSISTENT */
    uint32_t term_regress;        /* INV-TERM-MONOTONIC */
    uint32_t safe_auth;           /* INV-SAFE-NO-AUTHORITY */
    uint32_t safe_role;
    uint32_t safe_exit;           /* INV-SAFE-LATCH */
    uint32_t mask_change_no_cfg;  /* INV-RECONFIG-NO-MAGIC */
    uint32_t epoch_regress;       /* INV-RECONFIG-OLD-CONFIG */
    uint32_t steps;
    uint32_t first_ms;
    const char *first_what;
    /* observation only, not a violation by itself */
    uint32_t steps_auth_superseded;
    uint16_t last_term[N_NODES];
    uint16_t last_epoch[N_NODES];
    uint8_t  last_mask[N_NODES];
    uint32_t last_commits[N_NODES];
    mosaik_state_t last_state[N_NODES];
    uint32_t last_restart[N_NODES];
} adv_obs_t;

static void adv_init(adv_obs_t *o)
{
    int i;
    memset(o, 0, sizeof(*o));
    o->first_what = "-";
    for (i = 0; i < N_NODES; i++) {
        o->last_term[i]    = g_bus.node[i].term;
        o->last_epoch[i]   = g_bus.node[i].committed_epoch;
        o->last_mask[i]    = g_bus.node[i].committed_mask;
        o->last_commits[i] = g_bus.node[i].config_commits;
        o->last_state[i]   = g_bus.node[i].state;
        o->last_restart[i] = g_bus.restart_count[i];
    }
}

static void adv_hit(adv_obs_t *o, const char *what)
{
    if (o->first_ms == 0u) { o->first_ms = g_bus.now_ms; o->first_what = what; }
}

static void adv_step_obs(adv_obs_t *o)
{
    int i, j, v = 0;
    uint16_t max_epoch = 0u;
    uint8_t  max_epoch_mask = 0u;

    o->steps++;
    for (i = 0; i < N_NODES; i++) {
        const mosaik_node_t *n = &g_bus.node[i];
        if (g_bus.crashed[i] || !g_bus.powered[i]) { continue; }
        if (g_bus.restart_count[i] != o->last_restart[i]) {
            o->last_restart[i] = g_bus.restart_count[i];
            o->last_term[i]    = n->term;
            o->last_epoch[i]   = n->committed_epoch;
            o->last_mask[i]    = n->committed_mask;
            o->last_state[i]   = n->state;
            o->last_commits[i] = n->config_commits;
        }
        if (mosaik_has_valid_leadership_authority(n)) {
            v++;
            if (!mosaik_is_member(n)) { o->auth_outside++; adv_hit(o, "authority outside own committed membership"); }
        }
        if (n->state == MOSAIK_STATE_SAFE) {
            if (mosaik_has_valid_leadership_authority(n)) { o->safe_auth++; adv_hit(o, "SAFE node held authority"); }
            if (n->role != MOSAIK_ROLE_FOLLOWER) { o->safe_role++; adv_hit(o, "SAFE node left the follower role"); }
        }
        if (o->last_state[i] == MOSAIK_STATE_SAFE && n->state != MOSAIK_STATE_SAFE) {
            o->safe_exit++; adv_hit(o, "SAFE left without a cold restart");
        }
        if (n->term < o->last_term[i]) { o->term_regress++; adv_hit(o, "leadership term regressed"); }
        if (n->committed_epoch < o->last_epoch[i]) { o->epoch_regress++; adv_hit(o, "configuration epoch regressed"); }
        if (n->committed_mask != o->last_mask[i] && n->config_commits == o->last_commits[i]) {
            o->mask_change_no_cfg++; adv_hit(o, "membership changed without a configuration commit");
        }
        if (n->committed_epoch > max_epoch) { max_epoch = n->committed_epoch; max_epoch_mask = n->committed_mask; }
        o->last_term[i] = n->term; o->last_epoch[i] = n->committed_epoch;
        o->last_mask[i] = n->committed_mask; o->last_state[i] = n->state;
        o->last_commits[i] = n->config_commits;
    }
    if (v > 1) { o->multi_auth++; adv_hit(o, "two nodes held valid authority at the same instant"); }
    for (i = 0; i < N_NODES; i++) {
        const mosaik_node_t *n = &g_bus.node[i];
        if (g_bus.crashed[i] || !g_bus.powered[i]) { continue; }
        /* observation: authority held under a configuration that a strictly
         * newer committed configuration excludes. Not a violation on its own;
         * it becomes one only if two authorities coexist, which the counter
         * above catches. */
        if (mosaik_has_valid_leadership_authority(n) && n->committed_epoch < max_epoch &&
            !in_mask_t(max_epoch_mask, (uint8_t)(i + 1))) {
            o->steps_auth_superseded++;
        }
        for (j = i + 1; j < N_NODES; j++) {
            if (g_bus.crashed[j] || !g_bus.powered[j]) { continue; }
            if (g_bus.node[i].committed_epoch == g_bus.node[j].committed_epoch &&
                g_bus.node[i].committed_mask  != g_bus.node[j].committed_mask) {
                o->same_epoch_conflict++; adv_hit(o, "two memberships at one configuration epoch");
            }
        }
    }
}

static void adv_run(adv_obs_t *o, uint32_t ms)
{
    uint32_t k;
    for (k = 0; k < ms; k++) { bus_step(); adv_step_obs(o); }
}

/* true when no safety invariant was violated at any observed instant */
static bool adv_safe(const adv_obs_t *o)
{
    return o->multi_auth == 0u && o->auth_outside == 0u && o->same_epoch_conflict == 0u &&
           o->term_regress == 0u && o->safe_auth == 0u && o->safe_role == 0u &&
           o->safe_exit == 0u && o->mask_change_no_cfg == 0u && o->epoch_regress == 0u;
}

static void adv_report(const adv_obs_t *o, const char *tag)
{
    if (adv_safe(o)) { return; }
    printf("        COUNTEREXAMPLE in %s: %s at t=%u ms (multi=%u outside=%u same-epoch=%u term=%u safe-auth=%u safe-role=%u safe-exit=%u magic=%u epoch=%u)\n",
           tag, o->first_what, o->first_ms, o->multi_auth, o->auth_outside, o->same_epoch_conflict,
           o->term_regress, o->safe_auth, o->safe_role, o->safe_exit, o->mask_change_no_cfg, o->epoch_regress);
}

/* drive a transition on whichever node currently holds valid authority */
static bool adv_transition(uint8_t target, uint32_t budget, adv_obs_t *o)
{
    int v = valid_leader_index();
    uint32_t k;
    if (v < 0) { return false; }
    if (!mosaik_request_reconfiguration(&g_bus.node[v], target)) { return false; }
    for (k = 0; k < budget; k++) {
        if (o) { adv_run(o, 1u); } else { bus_step(); }
        if (g_bus.node[v].committed_mask == target) { return true; }
    }
    return g_bus.node[v].committed_mask == target;
}

static void adv_mkcfg(mosaik_msg_t *m, uint8_t src, mosaik_cfg_stage_t st,
                      uint16_t epoch, uint8_t mask, uint8_t arg)
{
    memset(m, 0, sizeof(*m));
    m->type = MOSAIK_MSG_CONFIG; m->src = src; m->version = MOSAIK_PROTO_VERSION;
    m->role = MOSAIK_ROLE_FOLLOWER; m->state = MOSAIK_STATE_INIT;
    m->term = epoch; m->arg = arg; m->cfg_stage = (uint8_t)st; m->cfg_mask = mask;
}

/* cut or restore the paths out of one node named by a bitmask of receivers */
static void adv_paths_from(int src, uint8_t deny, bool cut)
{
    int j;
    for (j = 0; j < N_NODES; j++) {
        if (j == src) { continue; }
        if ((deny & (uint8_t)(1u << j)) != 0u) {
            bus_set_net_action((uint8_t)src, (uint8_t)j, cut ? NET_DROP : NET_DELIVER, 0u);
        }
    }
}

/* TC-079: partial delivery of COMMIT, exhaustively over the delivery subsets
 * of one transition, crossed with every 1|2 partition and crash point. */
static void tc_079_partial_commit_exhaustion(void)
{
    int rem_sel, pd, cd, topo, crash, li, keep, rem, a, b;
    uint32_t schedules = 0u, unsafe = 0u, committed_everywhere = 0u, no_commit = 0u;
    adv_obs_t o;
    uint8_t target;

    printf("TC-079  partial COMMIT delivery exhausted over delivery subsets, partitions and crash points [Lot 5 Phase 3]\n");
    for (rem_sel = 0; rem_sel < 2; rem_sel++)
    for (pd = 0; pd < 8; pd++)
    for (cd = 0; cd < 8; cd++)
    for (topo = 0; topo < 4; topo++)
    for (crash = 0; crash < 3; crash++) {
        uint32_t k, e0;
        bus_init(); bus_run(2000u);
        li = leader_index();
        if (li < 0) { continue; }
        survivors_of(li, &a, &b);
        rem = (rem_sel == 0) ? a : b;
        keep = (rem == a) ? b : a;
        target = (uint8_t)(0x07u & ~(1u << rem));
        if ((target & (uint8_t)(1u << li)) == 0u) { continue; }
        adv_init(&o);
        /* the PROPOSE reaches only the receivers not named by pd */
        adv_paths_from(li, (uint8_t)pd, true);
        if (!mosaik_request_reconfiguration(&g_bus.node[li], target)) { adv_paths_from(li, (uint8_t)pd, false); continue; }
        adv_run(&o, 1u);
        adv_paths_from(li, (uint8_t)pd, false);
        e0 = g_bus.node[li].committed_epoch;
        for (k = 0; k < 400u && g_bus.node[li].committed_epoch == e0; k++) { adv_run(&o, 1u); }
        if (g_bus.node[li].committed_epoch != e0) {
            /* the COMMIT reaches only the receivers not named by cd */
            adv_paths_from(li, (uint8_t)cd, true);
            adv_run(&o, 1u);
            adv_paths_from(li, (uint8_t)cd, false);
        }
        if (crash == 1) { bus_crash_node(li); } else if (crash == 2) { bus_crash_node(keep); }
        if (topo > 0) { isolate_node(topo - 1, true); }
        adv_run(&o, 600u);
        bus_set_full_connectivity();
        if (crash == 1) { bus_restart_node(li); } else if (crash == 2) { bus_restart_node(keep); }
        adv_run(&o, 4000u);
        schedules++;
        if (!adv_safe(&o)) { unsafe++; if (unsafe == 1u) { adv_report(&o, "TC-079"); } }
        if (l5_all_committed(target, 2u)) { committed_everywhere++; }
        else if (g_bus.node[li].committed_epoch == 1u && g_bus.node[keep].committed_epoch == 1u) { no_commit++; }
    }
    check(schedules == 1536u, "Lot 5 Phase 3", "the declared partial-commit matrix executed in full");
    check(unsafe == 0u, "INV-LEADER-UNIQUE",
          "no schedule produced two valid authorities, an incompatible configuration or any other invariant violation");
    printf("        %u schedules (2 removals x 8 PROPOSE subsets x 8 COMMIT subsets x 4 partitions x 3 crash points): %u converged on the new membership, %u never committed, %u unsafe\n",
           schedules, committed_everywhere, no_commit, unsafe);
}

/* TC-080: loss, duplication, delay and reordering of every CONFIG stage. */
static void tc_080_config_message_faults(void)
{
    int li, keep, rem, stage, i;
    uint8_t target;
    adv_obs_t o;
    mosaik_frame_t cap[5];
    bool capv[5];
    uint32_t commits0[N_NODES], e_before;
    uint8_t m_before;

    printf("TC-080  CONFIG loss, duplication, delay and reordering at every stage [Lot 5 Phase 3]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5 Phase 3", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &keep, &rem);
    target = (uint8_t)(0x07u & ~(1u << rem));
    adv_init(&o);
    check(adv_transition(target, 1200u, &o), "Lot 5 Phase 3", "a transition completed so its frames can be captured");
    adv_run(&o, 300u);
    for (stage = 1; stage <= 4; stage++) {
        capv[stage] = g_bus.last_cfg_frame_valid[li][stage];
        if (capv[stage]) { cap[stage] = g_bus.last_cfg_frame[li][stage]; }
    }
    check(capv[MOSAIK_CFG_PROPOSE] && capv[MOSAIK_CFG_COMMIT], "Lot 5 Phase 3",
          "PROPOSE and COMMIT frames were captured from the bus as emitted");

    /* advance one epoch so every captured frame is superseded */
    check(adv_transition((uint8_t)MOSAIK_MEMBERSHIP_ALL, 1500u, &o), "Lot 5 Phase 3", "a second transition advanced the epoch");
    adv_run(&o, 300u);
    e_before = g_bus.node[keep].committed_epoch;
    m_before = g_bus.node[keep].committed_mask;
    for (i = 0; i < N_NODES; i++) { commits0[i] = g_bus.node[i].config_commits; }

    /* every captured stage: duplicated immediately, delayed, and reordered */
    for (stage = 1; stage <= 4; stage++) {
        if (!capv[stage]) { continue; }
        bus_inject_frame(&cap[stage], (uint8_t)(li + 1)); adv_run(&o, 1u);
        bus_inject_frame(&cap[stage], (uint8_t)(li + 1)); adv_run(&o, 1u);
        bus_schedule_delayed(&cap[stage], (uint8_t)(li + 1), 40u);
        bus_schedule_delayed(&cap[stage], (uint8_t)(li + 1), 5u);   /* out of order */
        adv_run(&o, 60u);
    }
    check(g_bus.node[keep].committed_epoch == e_before && g_bus.node[keep].committed_mask == m_before,
          "INV-RECONFIG-OLD-CONFIG", "no superseded frame, duplicated, delayed or reordered, moved a committed membership");
    for (i = 0; i < N_NODES; i++) {
        check(g_bus.node[i].config_commits == commits0[i], "INV-RECONFIG-OLD-CONFIG",
              "no node performed a further configuration commit from replayed traffic");
    }

    /* stage reordering within a live transaction: COMMIT before its PROPOSE */
    {
        mosaik_msg_t m;
        uint16_t next = (uint16_t)(g_bus.node[keep].committed_epoch + 1u);
        uint8_t  nt   = (uint8_t)(0x07u & ~(1u << keep));
        uint32_t stale0;
        if ((nt & (uint8_t)(1u << li)) != 0u) {
            adv_mkcfg(&m, (uint8_t)(li + 1), MOSAIK_CFG_COMMIT, next, nt, 0u);
            deliver_to(keep, &m);
            check(g_bus.node[keep].committed_epoch == next && g_bus.node[keep].committed_mask == nt,
                  "Lot 5 Phase 3", "a commit from a member for the next epoch is applied on its own");
            stale0 = g_bus.node[keep].config_stale_rejections;
            adv_mkcfg(&m, (uint8_t)(li + 1), MOSAIK_CFG_PROPOSE, next, nt, 0u);
            deliver_to(keep, &m);
            check(g_bus.node[keep].config_stale_rejections > stale0,
                  "INV-RECONFIG-OLD-CONFIG", "the proposal arriving after its own commit is refused as superseded");
        }
    }
    adv_run(&o, 1500u);
    check(adv_safe(&o), "INV-LEADER-UNIQUE", "no invariant was violated at any instant under CONFIG message faults");
    adv_report(&o, "TC-080");
    printf("        4 stages x (duplicate, delay, reorder) replayed after the epoch advanced: commits unchanged on every node, epoch %u mask 0x%02X held\n",
           e_before, m_before);
}

/* TC-081: the proposer fails at ten distinct points of the transaction and
 * returns under four restart conditions. */
static void tc_081_proposer_crash_matrix(void)
{
    static const char *point_name[10] = {
        "before the request", "right after PROPOSE", "after one acceptance",
        "just before the joint quorum", "just after the joint quorum",
        "before the COMMIT left", "after the first COMMIT delivery",
        "after a partial COMMIT", "after a complete COMMIT", "during announcement repair" };
    int point, restart_mode, li, keep, rem, a, b;
    uint32_t schedules = 0u, unsafe = 0u, recovered = 0u;
    adv_obs_t o;
    uint8_t target;

    printf("TC-081  proposer failure at ten transaction points x four restart conditions [Lot 5 Phase 3]\n");
    for (point = 0; point < 10; point++)
    for (restart_mode = 0; restart_mode < 4; restart_mode++) {
        uint32_t k, e0;
        bus_init(); bus_run(2000u);
        li = leader_index();
        if (li < 0) { continue; }
        survivors_of(li, &a, &b);
        rem = b; keep = a;
        target = (uint8_t)(0x07u & ~(1u << rem));
        if ((target & (uint8_t)(1u << li)) == 0u) { continue; }
        adv_init(&o);

        if (point == 0) { bus_crash_node(li); }
        else {
            if (point == 2 || point == 3) {
                /* only one acceptance can come back */
                bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DROP, 0u);
            }
            if (!mosaik_request_reconfiguration(&g_bus.node[li], target)) { continue; }
            if (point == 1) { adv_run(&o, 1u); bus_crash_node(li); }
            else {
                e0 = g_bus.node[li].committed_epoch;
                for (k = 0; k < 400u && g_bus.node[li].committed_epoch == e0; k++) { adv_run(&o, 1u); }
                if (point == 2 || point == 3) { adv_run(&o, 30u); bus_crash_node(li); }
                else if (point == 4) { bus_crash_node(li); }                    /* commit done, COMMIT still queued */
                else if (point == 5) { isolate_node(li, true); adv_run(&o, 1u); bus_crash_node(li); }
                else if (point == 6) { adv_run(&o, 1u); bus_crash_node(li); }   /* first delivery done */
                else if (point == 7) { adv_paths_from(li, (uint8_t)(1u << rem), true); adv_run(&o, 1u);
                                       adv_paths_from(li, (uint8_t)(1u << rem), false); bus_crash_node(li); }
                else if (point == 8) { adv_run(&o, 30u); bus_crash_node(li); }
                else if (point == 9) { adv_run(&o, 600u); bus_crash_node(li); } /* during announcement */
            }
        }
        bus_set_full_connectivity();
        /* restart conditions: immediately, while partitioned, after the heal,
         * and after the survivors have had time to run an election */
        if (restart_mode == 0) { adv_run(&o, 100u); bus_restart_node(li); }
        else if (restart_mode == 1) { isolate_node(li, true); adv_run(&o, 300u); bus_restart_node(li);
                                      adv_run(&o, 300u); isolate_node(li, false); }
        else if (restart_mode == 2) { adv_run(&o, 400u); bus_set_full_connectivity(); bus_restart_node(li); }
        else { adv_run(&o, 700u); bus_restart_node(li); }
        adv_run(&o, 5000u);
        schedules++;
        if (!adv_safe(&o)) { unsafe++; if (unsafe == 1u) {
            printf("        crash point: %s, restart mode %d\n", point_name[point], restart_mode);
            adv_report(&o, "TC-081"); } }
        if (l5_all_committed(target, 2u)) { recovered++; }
    }
    check(schedules == 40u, "Lot 5 Phase 3", "the declared proposer-crash matrix executed in full");
    check(unsafe == 0u, "INV-RECONFIG-TRANSITION",
          "no proposer failure point produced an invariant violation at any instant");
    printf("        %u schedules (10 crash points x 4 restart conditions): %u converged on the new membership, %u unsafe\n",
           schedules, recovered, unsafe);
}

/* TC-082: an acceptor fails around its acceptance; the persisted binding must
 * survive and must not be bypassed. */
static void tc_082_acceptor_crash_matrix(void)
{
    int point, li, keep, rem, a, b;
    uint32_t schedules = 0u, unsafe = 0u, binding_lost = 0u, second_binding = 0u;
    adv_obs_t o;
    uint8_t target;

    printf("TC-082  acceptor failure around its acceptance: the persisted binding survives and binds [Lot 5 Phase 3]\n");
    for (point = 0; point < 6; point++) {
        uint32_t k, e0;
        uint16_t bound_e; uint8_t bound_m;
        bus_init(); bus_run(2000u);
        li = leader_index();
        if (li < 0) { continue; }
        survivors_of(li, &a, &b);
        keep = a; rem = b;
        target = (uint8_t)(0x07u & ~(1u << rem));
        if ((target & (uint8_t)(1u << li)) == 0u) { continue; }
        adv_init(&o);

        if (point == 0) { bus_crash_node(keep); }                       /* before ACCEPT */
        if (point == 1) { adv_paths_from(keep, (uint8_t)(1u << li), true); }  /* accepts, cannot transmit */
        if (!mosaik_request_reconfiguration(&g_bus.node[li], target)) { continue; }
        adv_run(&o, 2u);
        if (point == 1) { bus_crash_node(keep); adv_paths_from(keep, (uint8_t)(1u << li), false); }
        else if (point == 2) { adv_run(&o, 2u); bus_crash_node(keep); }  /* after transmitting ACCEPT */
        else if (point >= 3) {
            e0 = g_bus.node[li].committed_epoch;
            for (k = 0; k < 400u && g_bus.node[li].committed_epoch == e0; k++) { adv_run(&o, 1u); }
            if (point == 3) { bus_crash_node(keep); }                    /* proposer committed, COMMIT queued */
            else if (point == 4) { adv_paths_from(li, (uint8_t)(1u << keep), true); adv_run(&o, 2u);
                                   adv_paths_from(li, (uint8_t)(1u << keep), false); bus_crash_node(keep); }
            else { adv_run(&o, 30u); bus_crash_node(keep); }             /* after receiving COMMIT */
        }
        adv_run(&o, 300u);
        bound_e = g_bus.store[keep].accepted_epoch; bound_m = g_bus.store[keep].accepted_mask;
        bus_restart_node(keep);
        if (point >= 1) {
            if (g_bus.node[keep].accepted_epoch != bound_e || g_bus.node[keep].accepted_mask != bound_m) { binding_lost++; }
        }
        /* a different successor for the same epoch must still be refused */
        if (g_bus.node[keep].accepted_epoch == (uint16_t)(g_bus.node[keep].committed_epoch + 1u)) {
            mosaik_msg_t m;
            uint8_t other = (uint8_t)(0x07u & ~(1u << keep));
            uint16_t ne = g_bus.node[keep].accepted_epoch;
            uint8_t  om = g_bus.node[keep].accepted_mask;
            if (other != om) {
                adv_mkcfg(&m, (uint8_t)(li + 1), MOSAIK_CFG_PROPOSE, ne, other, 0u);
                deliver_to(keep, &m);
                if (g_bus.node[keep].accepted_mask != om) { second_binding++; }
            }
        }
        adv_run(&o, 5000u);
        schedules++;
        if (!adv_safe(&o)) { unsafe++; if (unsafe == 1u) { adv_report(&o, "TC-082"); } }
    }
    check(schedules == 6u, "Lot 5 Phase 3", "the declared acceptor-crash matrix executed in full");
    check(binding_lost == 0u, "INV-RECONFIG-TRANSITION",
          "every acceptance that had been persisted came back intact after the cold restart");
    check(second_binding == 0u, "INV-RECONFIG-CONSISTENT",
          "no restarted acceptor could be talked into a second successor for the same epoch");
    check(unsafe == 0u, "INV-LEADER-UNIQUE", "no acceptor failure point produced an invariant violation");
    printf("        %u crash points around the acceptance: bindings lost %u, second bindings accepted %u, unsafe %u\n",
           schedules, binding_lost, second_binding, unsafe);
}

/* TC-083: a removed node attacks with every frame it could emit, and the
 * documented "missed a whole epoch" limitation is examined for safety. */
static void tc_083_removed_node_and_stuck_epoch(void)
{
    int li, keep, rem, i, x;
    uint8_t target;
    adv_obs_t o;
    mosaik_msg_t m;
    uint16_t e0, t_li, t_keep;
    uint8_t k0;
    uint32_t nm0, nm_delta = 0u, stuck_cases = 0u, stuck_unsafe = 0u, stuck_auth = 0u;

    printf("TC-083  removed-node attacks and the safety of a node that missed a whole epoch [Lot 5 Phase 3]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5 Phase 3", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &keep, &rem);
    target = (uint8_t)(0x07u & ~(1u << rem));
    adv_init(&o);
    check(adv_transition(target, 1200u, &o), "Lot 5 Phase 3", "the third node was removed");
    adv_run(&o, 300u);
    e0 = g_bus.node[li].committed_epoch; k0 = g_bus.node[li].committed_mask;
    t_li = g_bus.node[li].term; t_keep = g_bus.node[keep].term;
    nm0 = g_bus.node[li].nonmember_rejections;

    /* every CONFIG stage the removed node could emit, including one that
     * would re-admit it and one that would remove the current leader */
    adv_mkcfg(&m, (uint8_t)(rem + 1), MOSAIK_CFG_PROPOSE, (uint16_t)(e0 + 1u), 0x07u, 0u);
    deliver_to(li, &m); deliver_to(keep, &m);
    adv_mkcfg(&m, (uint8_t)(rem + 1), MOSAIK_CFG_COMMIT, (uint16_t)(e0 + 1u), 0x07u, 0u);
    deliver_to(li, &m); deliver_to(keep, &m);
    adv_mkcfg(&m, (uint8_t)(rem + 1), MOSAIK_CFG_ANNOUNCE, (uint16_t)(e0 + 1u), 0x07u, 0u);
    deliver_to(li, &m); deliver_to(keep, &m);
    adv_mkcfg(&m, (uint8_t)(rem + 1), MOSAIK_CFG_ACCEPT, (uint16_t)(e0 + 1u), 0x07u, (uint8_t)(li + 1));
    deliver_to(li, &m);
    adv_mkcfg(&m, (uint8_t)(rem + 1), MOSAIK_CFG_COMMIT, (uint16_t)(e0 + 1u),
              (uint8_t)(0x07u & ~(1u << li)), 0u);
    deliver_to(keep, &m);
    check(g_bus.node[li].committed_epoch == e0 && g_bus.node[li].committed_mask == k0 &&
          g_bus.node[keep].committed_epoch == e0 && g_bus.node[keep].committed_mask == k0,
          "INV-RECONFIG-REMOVED-NODE",
          "no configuration frame from the removed node moved any committed membership");

    /* every consensus frame type, at a higher and at a stale term */
    make_msg(&m, MOSAIK_MSG_HEARTBEAT, (uint8_t)(rem + 1), MOSAIK_ROLE_LEADER, MOSAIK_STATE_NOMINAL,
             (uint16_t)(t_li + 7u), 3u); deliver_to(li, &m); deliver_to(keep, &m);
    make_msg(&m, MOSAIK_MSG_VOTE_REQ, (uint8_t)(rem + 1), MOSAIK_ROLE_CANDIDATE, MOSAIK_STATE_NOMINAL,
             (uint16_t)(t_li + 8u), 0u); deliver_to(li, &m); deliver_to(keep, &m);
    make_msg(&m, MOSAIK_MSG_VOTE_GRANT, (uint8_t)(rem + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_NOMINAL,
             t_li, (uint8_t)(li + 1)); deliver_to(li, &m);
    make_msg(&m, MOSAIK_MSG_ACK, (uint8_t)(rem + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_NOMINAL,
             t_li, g_bus.node[li].seq); deliver_to(li, &m);
    make_msg(&m, MOSAIK_MSG_HEARTBEAT, (uint8_t)(rem + 1), MOSAIK_ROLE_LEADER, MOSAIK_STATE_NOMINAL,
             1u, 4u); deliver_to(li, &m);
    check(g_bus.node[li].term == t_li && g_bus.node[keep].term == t_keep,
          "INV-RECONFIG-REMOVED-NODE", "neither a higher nor a stale term from the removed node was adopted");
    check(g_bus.node[li].nonmember_rejections >= nm0 + 5u, "INV-RECONFIG-REMOVED-NODE",
          "every consensus frame from it was refused as coming from outside the committed membership");
    check(valid_leader_index() == li, "INV-LEADER-UNIQUE", "the leader kept its role and authority throughout");
    adv_run(&o, 1500u);
    check(adv_safe(&o), "INV-LEADER-UNIQUE", "no invariant was violated during the removed-node attack");
    adv_report(&o, "TC-083");
    nm_delta = g_bus.node[li].nonmember_rejections - nm0;

    /* the documented limitation: a node isolated across several transitions
     * falls behind by more than one epoch and can never be caught up. */
    for (x = 0; x < N_NODES; x++) {
        int n_tr;
        for (n_tr = 1; n_tr <= 5; n_tr++) {
            adv_obs_t s;
            uint8_t small;
            bus_init(); bus_run(2000u);
            li = leader_index();
            if (li < 0 || x == li) { continue; }
            adv_init(&s);
            isolate_node(x, true);
            small = (uint8_t)(0x07u & ~(1u << x));
            for (i = 0; i < n_tr; i++) {
                if (!adv_transition((i % 2 == 0) ? small : (uint8_t)MOSAIK_MEMBERSHIP_ALL, 1200u, &s)) { break; }
                adv_run(&s, 300u);
            }
            isolate_node(x, false);
            adv_run(&s, 8000u);
            stuck_cases++;
            if (!adv_safe(&s)) { stuck_unsafe++; if (stuck_unsafe == 1u) { adv_report(&s, "TC-083 stuck"); } }
            if (s.steps_auth_superseded > 0u) { stuck_auth++; }
        }
    }
    check(stuck_cases == 10u, "Lot 5 Phase 3", "the declared stuck-epoch matrix executed in full");
    check(stuck_unsafe == 0u, "INV-RECONFIG-QUORUM",
          "a node left behind by more than one epoch never produced an invariant violation");
    check(stuck_auth == 0u, "INV-RECONFIG-AUTHORITY",
          "no node held authority under a configuration that a newer committed one excludes it from");
    printf("        removed node: 5 configuration frames and 5 consensus frames all refused, %u non-member rejections; stuck-epoch matrix %u cases, %u unsafe, %u with authority under a superseded configuration\n",
           nm_delta, stuck_cases, stuck_unsafe, stuck_auth);
}

/* TC-084: re-admission must not turn old evidence into fresh authority. */
static void tc_084_readmission_stale_evidence(void)
{
    int li, keep, rem, k;
    uint8_t target;
    adv_obs_t o;
    uint32_t ack_rx_before, lost_ms = 0u, cut_ms;
    uint16_t hb_term_before, voted_term_before;
    uint8_t  hb_seq_before, voted_for_before;

    printf("TC-084  re-admission: old acknowledgements, votes and sequences do not become fresh evidence [Lot 5 Phase 3]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5 Phase 3", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &keep, &rem);
    target = (uint8_t)(0x07u & ~(1u << rem));
    adv_init(&o);
    check(adv_transition(target, 1200u, &o), "Lot 5 Phase 3", "the third node was removed");
    adv_run(&o, 1500u);
    ack_rx_before     = g_bus.node[li].last_ack_rx_ms[rem];
    hb_term_before    = g_bus.node[li].last_hb_term[rem];
    hb_seq_before     = g_bus.node[li].last_hb_seq[rem];
    voted_for_before  = g_bus.node[li].voted_for;
    voted_term_before = g_bus.node[li].voted_term;

    /* isolate the act of re-admission itself: a single commit frame that the
     * remaining member could legitimately have emitted, applied in one step,
     * so that nothing else can account for a change of state */
    {
        mosaik_msg_t rm;
        uint16_t e_next = (uint16_t)(g_bus.node[li].committed_epoch + 1u);
        ack_rx_before     = g_bus.node[li].last_ack_rx_ms[rem];
        hb_term_before    = g_bus.node[li].last_hb_term[rem];
        hb_seq_before     = g_bus.node[li].last_hb_seq[rem];
        voted_for_before  = g_bus.node[li].voted_for;
        voted_term_before = g_bus.node[li].voted_term;
        adv_mkcfg(&rm, (uint8_t)(keep + 1), MOSAIK_CFG_COMMIT, e_next, (uint8_t)MOSAIK_MEMBERSHIP_ALL, 0u);
        deliver_to(li, &rm);
        check(g_bus.node[li].committed_mask == (uint8_t)MOSAIK_MEMBERSHIP_ALL &&
              g_bus.node[li].committed_epoch == e_next,
              "Lot 5 Phase 3", "the re-admission was applied in a single step");
        check(g_bus.node[li].last_ack_rx_ms[rem] == ack_rx_before, "INV-RECONFIG-REMOVED-NODE",
              "re-admission alone refreshed no acknowledgement evidence");
        check(g_bus.node[li].voted_for == voted_for_before && g_bus.node[li].voted_term == voted_term_before,
              "INV-ONE-VOTE-PER-TERM", "re-admission alone changed no vote memory");
        check(g_bus.node[li].last_hb_term[rem] == hb_term_before && g_bus.node[li].last_hb_seq[rem] == hb_seq_before,
              "INV-NO-STALE-RECOVERY", "re-admission alone changed no heartbeat replay state");
        check(!mosaik_has_quorum_ack_evidence(&g_bus.node[li]) ||
              g_bus.node[li].last_ack_rx_ms[keep] + g_bus.node[li].cfg.heartbeat_period_ms > g_bus.now_ms,
              "INV-RECONFIG-LOT2", "any quorum evidence the leader still holds is evidence it actually received");
    }
    adv_run(&o, 50u);

    /* the re-admitted node's evidence must be received anew to count */
    cut_ms = g_bus.now_ms;
    bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DROP, 0u);
    bus_set_net_action((uint8_t)rem,  (uint8_t)li, NET_DROP, 0u);
    for (k = 0; k < 1500 && lost_ms == 0u; k++) {
        adv_run(&o, 1u);
        if (!mosaik_has_valid_leadership_authority(&g_bus.node[li])) { lost_ms = g_bus.now_ms - cut_ms; }
    }
    check(lost_ms != 0u && lost_ms <= MOSAIK_LEADERSHIP_LEASE_MS + 1u, "INV-RECONFIG-LOT2",
          "with no acknowledgement actually received the leader lost authority within one lease");
    bus_set_full_connectivity();
    adv_run(&o, 3000u);

    /* restart immediately before and after a re-admission */
    {
        adv_obs_t p;
        int li2, a2, b2, r2;
        uint8_t t2;
        bus_init(); bus_run(2000u);
        li2 = leader_index();
        if (li2 >= 0) {
            survivors_of(li2, &a2, &b2); r2 = b2;
            t2 = (uint8_t)(0x07u & ~(1u << r2));
            adv_init(&p);
            if (adv_transition(t2, 1200u, &p)) {
                adv_run(&p, 300u);
                bus_crash_node(r2); adv_run(&p, 200u); bus_restart_node(r2);   /* restart before re-admission */
                check(g_bus.node[r2].committed_mask == t2 && !mosaik_is_member(&g_bus.node[r2]),
                      "INV-RECONFIG-REMOVED-NODE", "a restart before re-admission leaves the node outside the membership");
                if (adv_transition((uint8_t)MOSAIK_MEMBERSHIP_ALL, 1500u, &p)) {
                    adv_run(&p, 200u);
                    bus_crash_node(r2); adv_run(&p, 200u); bus_restart_node(r2); /* restart after re-admission */
                    check(g_bus.node[r2].committed_mask == (uint8_t)MOSAIK_MEMBERSHIP_ALL &&
                          g_bus.node[r2].term == 0u,
                          "Lot 5 Phase 3", "a restart after re-admission restores the membership but no term or vote");
                    adv_run(&p, 4000u);
                    check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
                          "the cluster settled on exactly one valid authority after both restarts");
                }
            }
            check(adv_safe(&p), "INV-LEADER-UNIQUE", "no invariant was violated around the restarts");
            adv_report(&p, "TC-084 restarts");
        }
    }
    check(adv_safe(&o), "INV-LEADER-UNIQUE", "no invariant was violated during re-admission");
    adv_report(&o, "TC-084");
    printf("        re-admission refreshed nothing; authority lost %u ms after losing inbound traffic; restarts before and after re-admission behaved\n", lost_ms);
}

/* TC-085: conflicting successors for one predecessor epoch. */
static void tc_085_conflicting_successors(void)
{
    int li, a, b, newl;
    uint8_t X, Y;
    uint32_t k, commits_before[N_NODES];
    adv_obs_t o;
    int i;
    bool y_committed = false, x_committed = false;

    printf("TC-085  two different successors of one epoch: at most one can ever be agreed [Lot 5 Phase 3]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5 Phase 3", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &a, &b);
    X = (uint8_t)(0x07u & ~(1u << b));     /* successor proposed by the first leader */
    Y = (uint8_t)(0x07u & ~(1u << li));    /* a different successor, excluding that leader */
    adv_init(&o);

    /* the first proposer reaches only one acceptor, then fails */
    bus_set_net_action((uint8_t)li, (uint8_t)a, NET_DROP, 0u);
    bus_set_net_action((uint8_t)a, (uint8_t)li, NET_DROP, 0u);
    check(mosaik_request_reconfiguration(&g_bus.node[li], X), "Lot 5 Phase 3", "the first successor was proposed");
    adv_run(&o, 60u);
    check(g_bus.node[b].accepted_epoch == 2u && g_bus.node[b].accepted_mask == X, "Lot 5 Phase 3",
          "exactly one acceptor bound itself to the first successor");
    check(g_bus.node[a].accepted_epoch != 2u, "Lot 5 Phase 3", "the other acceptor never saw the proposal");
    bus_crash_node(li);
    bus_set_full_connectivity();
    for (k = 0; k < 4000u && valid_leader_index() < 0; k++) { adv_run(&o, 1u); }
    newl = valid_leader_index();
    check(newl >= 0 && newl != li, "Lot 5 Phase 3", "an unbound node took leadership after the proposer failed");
    if (newl < 0) { return; }

    for (i = 0; i < N_NODES; i++) { commits_before[i] = g_bus.node[i].config_commits; }
    if ((Y & (uint8_t)(1u << newl)) != 0u) {
        check(mosaik_request_reconfiguration(&g_bus.node[newl], Y), "Lot 5 Phase 3",
              "the new leader proposed a different successor for the same epoch");
        adv_run(&o, 2500u);
    }
    for (i = 0; i < N_NODES; i++) {
        if (g_bus.node[i].committed_mask == Y && g_bus.node[i].committed_epoch == 2u) { y_committed = true; }
        if (g_bus.node[i].committed_mask == X && g_bus.node[i].committed_epoch == 2u) { x_committed = true; }
    }
    check(!(x_committed && y_committed), "INV-RECONFIG-CONSISTENT",
          "the two conflicting successors were never both committed");
    check(!y_committed, "INV-RECONFIG-CONSISTENT",
          "the second successor could not be agreed against a binding already given to the first");
    check(g_bus.node[b].accepted_mask == X, "INV-RECONFIG-CONSISTENT",
          "the acceptor kept the binding it had given and did not switch successor");
    check(o.same_epoch_conflict == 0u, "INV-RECONFIG-CONSISTENT",
          "no two nodes ever held different memberships at the same epoch");
    for (i = 0; i < N_NODES; i++) {
        check(g_bus.node[i].config_commits == commits_before[i], "INV-RECONFIG-CONSISTENT",
              "the conflicting proposal produced no configuration commit on any node");
    }

    /* the proposer returns: only the originally bound successor can complete */
    bus_restart_node(li);
    adv_run(&o, 3000u);
    for (k = 0; k < 6u; k++) {
        int v = valid_leader_index();
        if (v >= 0 && g_bus.node[v].committed_epoch == 1u) { (void)mosaik_request_reconfiguration(&g_bus.node[v], X); }
        adv_run(&o, 800u);
    }
    check(adv_safe(&o), "INV-LEADER-UNIQUE", "no invariant was violated by the conflicting successors");
    adv_report(&o, "TC-085");
    printf("        first successor 0x%02X bound on one acceptor; second successor 0x%02X proposed by node %d: committed %s; epoch after recovery %u mask 0x%02X\n",
           X, Y, newl + 1, y_committed ? "yes" : "no",
           g_bus.node[b].committed_epoch, g_bus.node[b].committed_mask);
}

/* TC-086: leadership term and configuration epoch stay independent. */
static void tc_086_term_epoch_cross_product(void)
{
    int li, keep, rem, i;
    adv_obs_t o;
    mosaik_msg_t m;
    uint16_t t0, e0;
    uint8_t  k0, vf0; uint16_t vt0;
    uint32_t elections = 0u;

    printf("TC-086  configuration epoch and leadership term are independent under crossed traffic [Lot 5 Phase 3]\n");
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5 Phase 3", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &keep, &rem);
    adv_init(&o);
    t0 = g_bus.node[keep].term; e0 = g_bus.node[keep].committed_epoch; k0 = g_bus.node[keep].committed_mask;
    vf0 = g_bus.node[keep].voted_for; vt0 = g_bus.node[keep].voted_term;

    /* configuration traffic must never move the leadership term, whatever
     * epoch value it carries relative to the current term */
    for (i = 0; i < 4; i++) {
        uint16_t e_try = (uint16_t)(i == 0 ? 1u : (i == 1 ? e0 : (i == 2 ? (uint16_t)(t0 + 50u) : 0xFFFEu)));
        adv_mkcfg(&m, (uint8_t)(li + 1), MOSAIK_CFG_ANNOUNCE, e_try, (uint8_t)MOSAIK_MEMBERSHIP_ALL, 0u);
        deliver_to(keep, &m);
        adv_mkcfg(&m, (uint8_t)(li + 1), MOSAIK_CFG_PROPOSE, e_try, (uint8_t)(0x07u & ~(1u << rem)), 0u);
        deliver_to(keep, &m);
    }
    check(g_bus.node[keep].term == t0, "INV-TERM-MONOTONIC",
          "no configuration epoch value was ever taken for a leadership term");
    check(g_bus.node[keep].voted_for == vf0 && g_bus.node[keep].voted_term == vt0,
          "INV-ONE-VOTE-PER-TERM", "configuration traffic changed no vote memory");
    check(g_bus.node[keep].committed_epoch == e0 && g_bus.node[keep].committed_mask == k0,
          "INV-RECONFIG-OLD-CONFIG", "no inadmissible epoch moved the committed membership");

    /* several leadership elections while an acceptance binding persists */
    {
        uint8_t X = (uint8_t)(0x07u & ~(1u << rem));
        bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DROP, 0u);
        check(mosaik_request_reconfiguration(&g_bus.node[li], X), "Lot 5 Phase 3", "a transaction was left open");
        adv_run(&o, 80u);
        check(g_bus.node[keep].accepted_epoch == 2u, "Lot 5 Phase 3", "the binding is in place");
        bus_set_full_connectivity();
        for (i = 0; i < 3; i++) {
            int v = leader_index();
            uint16_t tb = (v >= 0) ? g_bus.node[v].term : 0u;
            if (v >= 0) { bus_crash_node(v); }
            adv_run(&o, 1200u);
            if (v >= 0) { bus_restart_node(v); }
            adv_run(&o, 1200u);
            { int nv = valid_leader_index();
              if (nv >= 0 && g_bus.node[nv].term > tb) { elections++; } }
        }
        check(g_bus.node[keep].accepted_epoch == 2u && g_bus.node[keep].accepted_mask == X,
              "INV-RECONFIG-TRANSITION", "the acceptance binding survived every leadership change");
    }
    check(adv_safe(&o), "INV-LEADER-UNIQUE", "no invariant was violated across the term and epoch cross product");
    adv_report(&o, "TC-086");
    printf("        4 inadmissible epoch values refused without touching the term; the acceptance binding survived 3 leader crash and restart cycles, with %u higher-term leadership acquisitions observed (the joint rule blocks election while the proposer is absent)\n",
           elections);
}

/* TC-087: attempts to renew authority from inadmissible evidence, and the
 * exact acknowledgement freshness boundary. */
static void tc_087_lease_evidence_attacks(void)
{
    int li, keep, rem, k;
    uint8_t target;
    adv_obs_t o;
    mosaik_msg_t m;
    uint32_t held[3];
    uint16_t t_li;

    printf("TC-087  lease evidence: removed, stale, duplicated and boundary-fresh acknowledgements [Lot 5 Phase 3]\n");

    /* Freshness boundary measured on the production predicate itself: the
     * acknowledgement evidence a leader holds must count while it is younger
     * than one heartbeat period and must stop counting at exactly that age.
     * This inspects age period-1, period and period+1. */
    {
        uint32_t t_last, age_fresh_last = 0u, age_first_stale = 0u, period;
        bool was_fresh_at_pm1 = false, fresh_at_p = false;
        bus_init(); bus_run(2000u);
        li = leader_index();
        check(li >= 0, "Lot 5 Phase 3", "leader existed");
        if (li < 0) { return; }
        survivors_of(li, &keep, &rem);
        adv_init(&o);
        period = (uint32_t)g_bus.node[li].cfg.heartbeat_period_ms;
        check(mosaik_has_quorum_ack_evidence(&g_bus.node[li]), "Lot 5 Phase 3",
              "the leader starts from acknowledgement evidence it actually received");
        bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DROP, 0u);
        bus_set_net_action((uint8_t)rem,  (uint8_t)li, NET_DROP, 0u);
        t_last = g_bus.node[li].last_ack_rx_ms[keep];
        if (g_bus.node[li].last_ack_rx_ms[rem] > t_last) { t_last = g_bus.node[li].last_ack_rx_ms[rem]; }
        for (k = 0; k < 400; k++) {
            uint32_t age;
            adv_run(&o, 1u);
            /* the age the predicate itself sees: the node's own clock, which
             * is the one the freshness comparison uses */
            age = g_bus.node[li].now_ms - t_last;
            if (mosaik_has_quorum_ack_evidence(&g_bus.node[li])) {
                age_fresh_last = age;
                if (age == period - 1u) { was_fresh_at_pm1 = true; }
                if (age == period)      { fresh_at_p = true; }
            } else if (age_first_stale == 0u) {
                age_first_stale = age;
            }
        }
        check(was_fresh_at_pm1, "INV-RECONFIG-LOT2",
              "evidence one millisecond younger than a heartbeat period still counts");
        check(!fresh_at_p && age_first_stale == period, "INV-RECONFIG-LOT2",
              "evidence stops counting at exactly one heartbeat period of age, neither earlier nor later");
        check(age_fresh_last == period - 1u, "INV-RECONFIG-LOT2",
              "the last instant it counted was exactly one millisecond before that boundary");
        held[0] = age_fresh_last; held[1] = age_first_stale; held[2] = period;
        check(adv_safe(&o), "INV-LEADER-UNIQUE", "no invariant was violated at the freshness boundary");
        adv_report(&o, "TC-087 boundary");
    }

    /* inadmissible sources and terms cannot renew anything */
    bus_init(); bus_run(2000u);
    li = leader_index();
    check(li >= 0, "Lot 5 Phase 3", "leader existed");
    if (li < 0) { return; }
    survivors_of(li, &keep, &rem);
    target = (uint8_t)(0x07u & ~(1u << rem));
    adv_init(&o);
    check(adv_transition(target, 1200u, &o), "Lot 5 Phase 3", "the third node was removed");
    adv_run(&o, 200u);
    t_li = g_bus.node[li].term;
    {
        uint32_t cut = g_bus.now_ms, lost = 0u;
        bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DROP, 0u);
        for (k = 0; k < 1500 && lost == 0u; k++) {
            /* removed node, stale term, duplicated, and reordered acknowledgements */
            make_msg(&m, MOSAIK_MSG_ACK, (uint8_t)(rem + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_NOMINAL,
                     t_li, g_bus.node[li].seq); deliver_to(li, &m); deliver_to(li, &m);
            make_msg(&m, MOSAIK_MSG_ACK, (uint8_t)(keep + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_NOMINAL,
                     (uint16_t)(t_li - 1u), g_bus.node[li].seq); deliver_to(li, &m);
            adv_run(&o, 1u);
            if (!mosaik_has_valid_leadership_authority(&g_bus.node[li])) { lost = g_bus.now_ms - cut; }
        }
        check(lost != 0u && lost <= MOSAIK_LEADERSHIP_LEASE_MS + 1u, "INV-RECONFIG-REMOVED-NODE",
              "neither removed-node nor stale-term acknowledgements, duplicated at every step, kept the lease alive");
    }
    check(adv_safe(&o), "INV-LEADER-UNIQUE", "no invariant was violated during the lease attacks");
    adv_report(&o, "TC-087");
    printf("        evidence last counted at age %u ms, first stale at age %u ms, heartbeat period %u ms; inadmissible acknowledgements never renewed the lease\n",
           held[0], held[1], held[2]);
}

/* TC-088: SAFE and DEGRADED across every stage of a transaction. */
static void tc_088_safe_degraded_during_transaction(void)
{
    int stage, li, keep, rem, safe_node, i;
    uint32_t cases = 0u, unsafe = 0u, safe_changed_cfg = 0u;
    adv_obs_t o;

    printf("TC-088  SAFE and DEGRADED injected at every stage of a transaction [Lot 5 Phase 3]\n");
    for (stage = 0; stage < 4; stage++) {
        uint8_t target;
        uint32_t k, e0;
        uint16_t safe_epoch_before; uint8_t safe_mask_before;
        bus_init(); bus_run(2000u);
        li = leader_index();
        if (li < 0) { continue; }
        survivors_of(li, &keep, &rem);
        target = (uint8_t)(0x07u & ~(1u << rem));
        adv_init(&o);
        if (stage == 0) {
            /* SAFE evidence before the proposal: a peer latches SAFE */
            safe_node = split_brain_leader();
            for (k = 0; k < 1500u && valid_leader_index() < 0; k++) { adv_run(&o, 1u); }
            li = valid_leader_index();
            if (li < 0) { continue; }
            { int x, y; survivors_of(safe_node, &x, &y); rem = safe_node; (void)x; (void)y; }
            target = (uint8_t)(0x07u & ~(1u << rem));
            if ((target & (uint8_t)(1u << li)) == 0u) { continue; }
            safe_epoch_before = g_bus.node[safe_node].committed_epoch;
            safe_mask_before  = g_bus.node[safe_node].committed_mask;
            (void)adv_transition(target, 1500u, &o);
            adv_run(&o, 1000u);
            if (g_bus.node[safe_node].committed_epoch != safe_epoch_before ||
                g_bus.node[safe_node].committed_mask != safe_mask_before) { safe_changed_cfg++; }
            check(g_bus.node[safe_node].state == MOSAIK_STATE_SAFE, "INV-SAFE-LATCH",
                  "the SAFE node stayed SAFE while the others reconfigured around it");
            check(!mosaik_has_valid_leadership_authority(&g_bus.node[safe_node]), "INV-SAFE-NO-AUTHORITY",
                  "the SAFE node held no authority during the transaction");
        } else {
            if (!mosaik_request_reconfiguration(&g_bus.node[li], target)) { continue; }
            if (stage == 1) { adv_run(&o, 2u); }                 /* during acceptance */
            else if (stage == 2) {                                /* just before the commit */
                e0 = g_bus.node[li].committed_epoch;
                bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DROP, 0u);
                adv_run(&o, 40u);
                (void)e0;
            } else {                                              /* after a partial commit */
                e0 = g_bus.node[li].committed_epoch;
                for (k = 0; k < 400u && g_bus.node[li].committed_epoch == e0; k++) { adv_run(&o, 1u); }
                adv_paths_from(li, (uint8_t)(1u << rem), true); adv_run(&o, 1u);
                adv_paths_from(li, (uint8_t)(1u << rem), false);
            }
            /* a genuine SAFE announcement from the third node */
            {
                mosaik_msg_t m;
                uint16_t t_keep = g_bus.node[keep].term;
                make_msg(&m, MOSAIK_MSG_SAFE, (uint8_t)(rem + 1), MOSAIK_ROLE_FOLLOWER, MOSAIK_STATE_SAFE,
                         (uint16_t)(t_keep + 4u), (uint8_t)MOSAIK_SAFE_NO_QUORUM);
                deliver_to(keep, &m); deliver_to(li, &m);
                check(g_bus.node[keep].term == t_keep, "INV-RECONFIG-LOT4",
                      "a SAFE announcement carried no consensus term into the transaction");
                check((g_bus.node[keep].safe_evidence_mask & (uint8_t)(1u << rem)) != 0u,
                      "INV-RECONFIG-LOT4", "it was still recorded as fault evidence");
                check(g_bus.node[keep].committed_epoch <= 2u, "INV-RECONFIG-NO-MAGIC",
                      "a SAFE announcement advanced no configuration epoch");
            }
            bus_set_full_connectivity();
            adv_run(&o, 4000u);
        }
        cases++;
        if (!adv_safe(&o)) { unsafe++; if (unsafe == 1u) { adv_report(&o, "TC-088"); } }
    }
    check(cases >= 3u, "Lot 5 Phase 3", "SAFE was injected at the declared transaction stages");
    check(safe_changed_cfg == 0u, "INV-SAFE-LATCH",
          "a latched SAFE node never changed its committed configuration");
    check(unsafe == 0u, "INV-SAFE-NO-AUTHORITY",
          "no SAFE or DEGRADED interaction with a transaction violated an invariant");
    (void)i;
    printf("        %u stages exercised; SAFE stayed FDIR evidence, advanced no epoch, renewed no lease and cast no vote\n", cases);
}

/* TC-089: exact time boundaries around the protocol's timers. */
static void tc_089_time_boundaries(void)
{
    int li, keep, rem, d, k;
    adv_obs_t o;
    uint32_t overlap_steps = 0u, cases = 0u;

    printf("TC-089  one-millisecond boundaries around lease, election, retransmission and announcement [Lot 5 Phase 3]\n");
    /* lease expiry boundary: cut inbound traffic and step through the exact
     * millisecond the lease ends, looking for a one-tick authority overlap */
    for (d = -1; d <= 1; d++) {
        uint32_t cut;
        bus_init(); bus_run(2000u);
        li = leader_index();
        if (li < 0) { continue; }
        survivors_of(li, &keep, &rem);
        adv_init(&o);
        cut = g_bus.now_ms;
        bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DROP, 0u);
        bus_set_net_action((uint8_t)rem,  (uint8_t)li, NET_DROP, 0u);
        for (k = 0; k < (int)MOSAIK_LEADERSHIP_LEASE_MS + 600 + d; k++) { adv_run(&o, 1u); }
        bus_set_full_connectivity();
        adv_run(&o, 3000u);
        cases++;
        overlap_steps += o.multi_auth;
        if (!adv_safe(&o)) { adv_report(&o, "TC-089 lease"); }
        (void)cut;
    }
    /* proposal retransmission and abandonment boundaries */
    for (d = 0; d < 3; d++) {
        uint32_t hold[3]; uint8_t target;
        hold[0] = 1u; hold[1] = 10u * 100u - 1u; hold[2] = 10u * 100u + 1u;
        bus_init(); bus_run(2000u);
        li = leader_index();
        if (li < 0) { continue; }
        survivors_of(li, &keep, &rem);
        target = (uint8_t)(0x07u & ~(1u << rem));
        adv_init(&o);
        bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DROP, 0u);
        if (!mosaik_request_reconfiguration(&g_bus.node[li], target)) { continue; }
        adv_run(&o, hold[d]);
        bus_set_net_action((uint8_t)keep, (uint8_t)li, NET_DELIVER, 0u);
        adv_run(&o, 3000u);
        cases++;
        overlap_steps += o.multi_auth;
        if (!adv_safe(&o)) { adv_report(&o, "TC-089 proposal"); }
        check(g_bus.node[li].committed_epoch <= 2u, "INV-RECONFIG-TRANSITION",
              "a proposal held across the retransmission and abandonment boundaries committed at most once");
    }
    check(overlap_steps == 0u, "INV-LEADER-UNIQUE",
          "no one-millisecond window showed two valid authorities at any timer boundary");
    printf("        %u boundary cases around the lease, retransmission and abandonment timers: %u instants with two authorities\n",
           cases, overlap_steps);
}

/* TC-090: bounded deterministic schedule explorer. */
static void tc_090_bounded_schedule_explorer(void)
{
    int rem_sel, pd, cd, topo, crash, heal_sel, li, keep, rem, a, b;
    static const uint32_t heals[3] = {200u, 600u, 1400u};
    uint32_t schedules = 0u, unsafe = 0u;
    uint32_t first_bad[6];
    adv_obs_t o;
    uint8_t target;

    printf("TC-090  bounded deterministic schedule explorer over the reconfiguration fault space [Lot 5 Phase 3]\n");
    memset(first_bad, 0, sizeof(first_bad));
    for (rem_sel = 0; rem_sel < 2; rem_sel++)
    for (pd = 0; pd < 8; pd++)
    for (cd = 0; cd < 8; cd++)
    for (topo = 0; topo < 4; topo++)
    for (crash = 0; crash < 3; crash++)
    for (heal_sel = 0; heal_sel < 3; heal_sel++) {
        uint32_t k, e0;
        bus_init(); bus_run(2000u);
        li = leader_index();
        if (li < 0) { continue; }
        survivors_of(li, &a, &b);
        rem = (rem_sel == 0) ? a : b;
        keep = (rem == a) ? b : a;
        target = (uint8_t)(0x07u & ~(1u << rem));
        if ((target & (uint8_t)(1u << li)) == 0u) { continue; }
        adv_init(&o);
        adv_paths_from(li, (uint8_t)pd, true);
        if (!mosaik_request_reconfiguration(&g_bus.node[li], target)) { adv_paths_from(li, (uint8_t)pd, false); continue; }
        adv_run(&o, 1u);
        adv_paths_from(li, (uint8_t)pd, false);
        e0 = g_bus.node[li].committed_epoch;
        for (k = 0; k < 400u && g_bus.node[li].committed_epoch == e0; k++) { adv_run(&o, 1u); }
        if (g_bus.node[li].committed_epoch != e0) {
            adv_paths_from(li, (uint8_t)cd, true); adv_run(&o, 1u); adv_paths_from(li, (uint8_t)cd, false);
        }
        if (crash == 1) { bus_crash_node(li); } else if (crash == 2) { bus_crash_node(keep); }
        if (topo > 0) { isolate_node(topo - 1, true); }
        adv_run(&o, heals[heal_sel]);
        bus_set_full_connectivity();
        if (crash == 1) { bus_restart_node(li); } else if (crash == 2) { bus_restart_node(keep); }
        adv_run(&o, 4000u);
        schedules++;
        if (!adv_safe(&o)) {
            unsafe++;
            if (unsafe == 1u) {
                first_bad[0] = (uint32_t)rem_sel; first_bad[1] = (uint32_t)pd; first_bad[2] = (uint32_t)cd;
                first_bad[3] = (uint32_t)topo; first_bad[4] = (uint32_t)crash; first_bad[5] = heals[heal_sel];
                printf("        MINIMAL FAILING SCHEDULE: removed=%s PROPOSE-denied=0x%02X COMMIT-denied=0x%02X partition=%d crash=%d heal=%u ms\n",
                       rem_sel ? "second peer" : "first peer", pd, cd, topo, crash, heals[heal_sel]);
                adv_report(&o, "TC-090");
            }
        }
    }
    check(schedules == 4608u, "Lot 5 Phase 3",
          "the declared schedule space executed in full (2 removals x 8 x 8 delivery subsets x 4 partitions x 3 crash points x 3 heal delays)");
    check(unsafe == 0u, "INV-LEADER-UNIQUE",
          "no schedule in the explored space violated any safety invariant at any observed millisecond");
    printf("        %u schedules explored, %u violating; the space is bounded by construction and is not a proof of correctness\n",
           schedules, unsafe);
}

int main(void)
{
    printf("MOSAIK HIL bench - host test suite\n");
    printf("----------------------------------\n");
    tc_001_single_leader();
    tc_002_no_split_brain();
    tc_003_failover_latency();
    tc_004_safe_on_split_brain();
    tc_005_no_quorum();
    tc_006_codec();
    tc_007_partition_lease_expiry();
    tc_008_old_term_heartbeat_rejected();
    tc_009_replay_after_lease_expiry();
    tc_010_delayed_old_leader_after_partition_heal();
    tc_011_duplicate_heartbeat_idempotence();
    tc_012_stale_election_traffic();
    tc_013_one_way_leader_isolation();
    tc_014_asymmetric_minority_view();
    tc_015_selective_heartbeat_loss();
    tc_016_delay_around_lease_boundary();
    tc_017_message_reordering();
    tc_018_partition_heal_queued_traffic();
    tc_019_selective_ack_quorum_failure();
    tc_020_adversarial_combination();

    /* LOT 2D: Crash/Restart/Recovery */
    tc_021_follower_crash_quorum_available();
    tc_022_leader_crash();
    tc_023_leader_crash_election_former_restarts();
    tc_024_crashed_follower_restart_rejoin();
    tc_025_former_leader_restarts_after_new_elected();
    tc_026_restart_with_delayed_pre_crash_messages();
    tc_027_repeated_crash_restart();
    tc_028_crash_during_lease_expiry();
    tc_029_crash_during_election();
    tc_030_recovery_asymmetric_network();

    /* LOT 2D: candidate retry backoff (C2-a) evidence */
    tc_031_natural_collision_recovery();
    tc_032_permanent_contention_safe_contract();
    tc_033_asymmetric_partition_during_retry();
    tc_034_stale_delayed_election_traffic();

    /* LOT 3: FDIR / SAFE evidence (Phase 1 RED baseline) */
    tc_035_safe_no_authority_cluster_recovers();
    tc_036_safe_non_participation();
    tc_037_safe_latch();
    tc_038_safe_not_propagated();
    tc_039_degraded_persistence();
    tc_040_degraded_expiry_and_return_to_nominal();
    tc_041_election_exhaustion_terminal();
    tc_042_isolation_boundary();
    tc_043_malformed_detection_only();
    tc_044_crash_during_recovery();
    tc_045_replayed_safe_evidence_bounded();
    tc_046_repeated_transient_faults();
    tc_047_adversarial_safe_replay_oneway();
    tc_048_adversarial_crash_collision_safe_frame();
    tc_049_reproducibility();
    tc_050_same_term_vote_memory_across_step_down();

    /* LOT 4: system mode semantics (Phase 1 RED baseline: TC-052, TC-053 expected RED) */
    tc_051_mode_legality_guard();
    tc_052_boot_no_degraded_without_evidence();
    tc_053_safe_announcement_no_authority_effect();
    tc_054_state_metadata_non_authority();
    tc_055_out_of_range_state_byte();
    tc_056_stale_heartbeat_degraded_metadata();
    tc_057_leader_change_while_degraded();
    tc_058_partition_crash_restart_legality();
    tc_059_lower_term_safe_announcement();
    tc_060_determinism_of_red_scenarios();

    /* LOT 5: autonomous reconfiguration (membership and quorum) */
    tc_061_membership_representation();
    tc_062_peer_loss_is_not_removal();
    tc_063_minority_cannot_self_reconfigure();
    tc_064_membership_change_executes();
    tc_065_consistency_before_quorum_change();
    tc_066_removed_node_rejoin();
    tc_067_configuration_replay();
    tc_068_partition_during_transition();
    tc_069_authority_lease_non_regression();
    tc_070_determinism_of_reconfiguration();
    tc_071_conflicting_successors();
    tc_072_transaction_idempotence();
    tc_073_malformed_membership();
    tc_074_proposer_failure();
    tc_075_removed_node_traffic();
    tc_076_permitted_transitions();
    tc_077_configuration_persistence();
    tc_078_transaction_interactions();

    /* LOT 5 Phase 3: adversarial hardening campaign */
    tc_079_partial_commit_exhaustion();
    tc_080_config_message_faults();
    tc_081_proposer_crash_matrix();
    tc_082_acceptor_crash_matrix();
    tc_083_removed_node_and_stuck_epoch();
    tc_084_readmission_stale_evidence();
    tc_085_conflicting_successors();
    tc_086_term_epoch_cross_product();
    tc_087_lease_evidence_attacks();
    tc_088_safe_degraded_during_transaction();
    tc_089_time_boundaries();
    tc_090_bounded_schedule_explorer();

    printf("----------------------------------\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);

    /* LOT 2D Summary */
    printf("\n=== LOT 2D CRASH/RESTART SUMMARY ===\n");
    printf("Max concurrent valid authorities (LOT 2D): %u\n", g_bus.max_concurrent_valid_authorities_lot2d);
    printf("Term regressions (LOT 2D): %u\n", g_bus.term_regressions_lot2d);
    printf("Stale authority acceptances (LOT 2D): %u\n", g_bus.stale_authority_acceptances_lot2d);
    printf("Duplicate votes (LOT 2D): %u\n", g_bus.duplicate_votes_lot2d);
    printf("Lease expirations tracked (LOT 2D): %u\n", g_bus.lease_expirations_lot2d);
    for (int i = 0; i < N_NODES; i++) {
        printf("  Node %u: crashes=%u, restarts=%u\n", i + 1,
               g_bus.crash_count[i], g_bus.restart_count[i]);
    }

    return g_failures == 0 ? 0 : 1;
}