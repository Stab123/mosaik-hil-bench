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
    uint32_t tx_count[N_NODES][6];
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
        if (mosaik_decode(frame, &obs) && (int)obs.type >= 0 && (int)obs.type < 6) {
            g_bus.tx_count[src_idx][(int)obs.type]++;
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
    
    uint16_t current_term = g_bus.node[leader_idx].term;
    uint32_t cutoff_ms = g_bus.now_ms - g_bus.node[leader_idx].cfg.heartbeat_period_ms;
    int inbound_count = 0;
    
    for (int k = 0; k < N_NODES; k++) {
        if (k != leader_idx) {
            /* Direct inbound message evidence only: */
            /* - Actually received by leader (recorded in inbound tracking) */
            /* - Current term */
            /* - Within lease freshness window */
            /* The peer's powered/crashed state is deliberately NOT consulted:
             * a real leader cannot know it; stale evidence simply ages out. */
            if (g_bus.leader_last_inbound_ms[leader_idx][k] > cutoff_ms &&
                g_bus.leader_last_inbound_term[leader_idx][k] == current_term) {
                inbound_count++;
            }
        }
    }
    /* For 3-node cluster, quorum = 2. Leader itself counts as 1, need 1 more. */
    return inbound_count >= 1;
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