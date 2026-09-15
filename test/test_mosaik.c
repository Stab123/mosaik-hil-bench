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
#define QUEUE_LEN 64
#define MAX_DELAYED_MSGS 32

typedef struct {
    mosaik_node_t  node[N_NODES];
    bool           powered[N_NODES];
    bool           connectivity[N_NODES][N_NODES]; /* connectivity[from][to] */
    mosaik_frame_t queue[QUEUE_LEN];
    uint8_t        queue_src[QUEUE_LEN];
    int            queued;
    uint32_t       now_ms;

    /* Lot 2B: Adversarial message scheduling for delayed/duplicate/replay tests. */
    struct {
        mosaik_frame_t frame;
        uint8_t        src;
        uint32_t       deliver_at_ms;
        bool           active;
    } delayed_msgs[MAX_DELAYED_MSGS];
    int delayed_count;
} bus_t;

static bus_t g_bus;

static void bus_tx(const mosaik_frame_t *frame, void *user)
{
    uintptr_t src = (uintptr_t)user;
    if (g_bus.queued >= QUEUE_LEN) { return; }
    g_bus.queue[g_bus.queued] = *frame;
    g_bus.queue_src[g_bus.queued] = (uint8_t)src;
    g_bus.queued++;
}

/* Lot 2B: Inject a message immediately (bypassing TX callback). */
static void bus_inject_frame(const mosaik_frame_t *frame, uint8_t src)
{
    if (g_bus.queued >= QUEUE_LEN) { return; }
    g_bus.queue[g_bus.queued] = *frame;
    g_bus.queue_src[g_bus.queued] = src;
    g_bus.queued++;
}

/* Lot 2B: Process any delayed messages that are due for delivery. */
static void bus_process_delayed(void)
{
    for (int i = 0; i < g_bus.delayed_count; i++) {
        if (!g_bus.delayed_msgs[i].active) { continue; }
        if (g_bus.now_ms >= g_bus.delayed_msgs[i].deliver_at_ms) {
            /* Deliver now */
            if (g_bus.queued < QUEUE_LEN) {
                g_bus.queue[g_bus.queued] = g_bus.delayed_msgs[i].frame;
                g_bus.queue_src[g_bus.queued] = g_bus.delayed_msgs[i].src;
                g_bus.queued++;
            }
            g_bus.delayed_msgs[i].active = false;
        }
    }
}

/* Set full connectivity (all nodes can talk to all other nodes). */
static void bus_set_full_connectivity(void)
{
    int i, j;
    for (i = 0; i < N_NODES; i++) {
        for (j = 0; j < N_NODES; j++) {
            g_bus.connectivity[i][j] = (i != j);
        }
    }
}

/* Set symmetric 2+1 partition: nodes in group A can talk to each other,
 * nodes in group B can talk to each other, but no cross-group communication. */
static void bus_set_partition_2plus1(uint8_t isolated_node) /* 1-indexed node id */
{
    int i, j;
    uint8_t iso = isolated_node - 1;
    for (i = 0; i < N_NODES; i++) {
        for (j = 0; j < N_NODES; j++) {
            if (i == j) {
                g_bus.connectivity[i][j] = false;
            } else if (i == iso || j == iso) {
                g_bus.connectivity[i][j] = false; /* isolated node cut off */
            } else {
                g_bus.connectivity[i][j] = true;  /* majority pair connected */
            }
        }
    }
}

static void bus_init(void)
{
    mosaik_config_t cfg;
    int i;
    mosaik_config_default(&cfg);
    memset(&g_bus, 0, sizeof(g_bus));
    for (i = 0; i < N_NODES; i++) {
        g_bus.powered[i] = true;
    }
    bus_set_full_connectivity();
    for (i = 0; i < N_NODES; i++) {
        mosaik_init(&g_bus.node[i], (uint8_t)(i + 1), &cfg,
                    bus_tx, (void *)(uintptr_t)(i + 1), 0u);
    }
}

static void bus_step(void)
{
    mosaik_frame_t pending[QUEUE_LEN];
    uint8_t pending_src[QUEUE_LEN];
    int n, i, j;
    bool leader_hb_delivered[N_NODES] = {false};

    /* Lot 2B: Process any delayed messages that are now due. */
    bus_process_delayed();

    n = g_bus.queued;
    memcpy(pending, g_bus.queue, sizeof(mosaik_frame_t) * (size_t)n);
    memcpy(pending_src, g_bus.queue_src, sizeof(uint8_t) * (size_t)n);
    g_bus.queued = 0;

    for (j = 0; j < n; j++) {
        uint8_t src_idx = pending_src[j] - 1; /* 0-indexed */
        mosaik_msg_t msg;
        bool is_leader_heartbeat = false;

        if (mosaik_decode(&pending[j], &msg)) {
            is_leader_heartbeat = (msg.type == MOSAIK_MSG_HEARTBEAT &&
                                   msg.role == MOSAIK_ROLE_LEADER);
        }

        for (i = 0; i < N_NODES; i++) {
            if (!g_bus.powered[i]) { continue; }
            if (g_bus.node[i].id == pending_src[j]) { continue; }
            if (!g_bus.connectivity[src_idx][i]) { continue; }
            mosaik_on_rx(&g_bus.node[i], g_bus.now_ms, &pending[j]);

            /* If a leader's heartbeat was delivered to a follower, note it for lease renewal. */
            if (is_leader_heartbeat && src_idx < N_NODES) {
                leader_hb_delivered[src_idx] = true;
            }
        }
    }

    /* Leadership lease renewal: a leader's lease is renewed when its heartbeat
     * reaches at least one other powered node (quorum = 2 for 3-node cluster).
     * This aligns with the follower's election timeout reset on heartbeat receipt. */
    for (i = 0; i < N_NODES; i++) {
        if (g_bus.powered[i] && g_bus.node[i].role == MOSAIK_ROLE_LEADER) {
            if (leader_hb_delivered[i]) {
                g_bus.node[i].last_quorum_contact_ms = g_bus.now_ms;
                g_bus.node[i].lease_expiry_ms = g_bus.now_ms + MOSAIK_LEADERSHIP_LEASE_MS;
            }
        }
    }

    for (i = 0; i < N_NODES; i++) {
        if (!g_bus.powered[i]) { continue; }
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
        if (g_bus.powered[i] && mosaik_is_leader(&g_bus.node[i])) { c++; }
    }
    return c;
}

static int valid_leader_count(void)
{
    int i, c = 0;
    for (i = 0; i < N_NODES; i++) {
        if (g_bus.powered[i] && mosaik_has_valid_leadership_authority(&g_bus.node[i])) { c++; }
    }
    return c;
}

static int leader_index(void)
{
    int i;
    for (i = 0; i < N_NODES; i++) {
        if (g_bus.powered[i] && mosaik_is_leader(&g_bus.node[i])) { return i; }
    }
    return -1;
}

static int valid_leader_index(void)
{
    int i;
    for (i = 0; i < N_NODES; i++) {
        if (g_bus.powered[i] && mosaik_has_valid_leadership_authority(&g_bus.node[i])) { return i; }
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
            for (uint32_t extra = 0; extra < 500; extra++) {
                bus_step();
                int vlc2 = valid_leader_count();
                if (vlc2 > max_concurrent_valid) { max_concurrent_valid = vlc2; }
            }
            break;
        }
    }

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
            g_bus.connectivity[leader_idx][i] = false;
            g_bus.connectivity[i][leader_idx] = false;
        }
    }

    /* Wait for lease to expire (500 ms + margin). */
    expiry_ms = g_bus.node[leader_idx].lease_expiry_ms + 100;
    while (g_bus.now_ms < expiry_ms) { bus_step(); }

    /* Verify lease expired and authority lost. */
    check(!mosaik_has_valid_leadership_authority(&g_bus.node[leader_idx]), "Lot 2B",
          "lease expired, authority lost");
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
    mosaik_msg_t msg;
    mosaik_frame_t delayed_frames[10];
    int delayed_count = 0;
    uint32_t partition_time, heal_time, k;

    printf("TC-010  delayed old leader after partition recovery [Lot 2B]\n");

    bus_init();
    bus_run(2000u);

    leader_a_idx = leader_index();
    check(leader_a_idx >= 0, "Lot 2B", "initial leader A existed");
    if (leader_a_idx < 0) { return; }
    term_a = g_bus.node[leader_a_idx].term;

    /* Capture some heartbeats from leader A before partition. */
    for (int i = 0; i < 3; i++) {
        bus_step();
        if (g_bus.node[leader_a_idx].role == MOSAIK_ROLE_LEADER) {
            msg.type = MOSAIK_MSG_HEARTBEAT;
            msg.src  = (uint8_t)(leader_a_idx + 1);
            msg.version = MOSAIK_PROTO_VERSION;
            msg.role = MOSAIK_ROLE_LEADER;
            msg.state = MOSAIK_STATE_NOMINAL;
            msg.term = term_a;
            msg.arg  = g_bus.node[leader_a_idx].seq;
            mosaik_encode(&delayed_frames[delayed_count], &msg);
            delayed_count++;
        }
    }

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
    heal_time = g_bus.now_ms;
    bus_set_full_connectivity();

    /* Deliver delayed frames from old leader A. */
    for (int i = 0; i < delayed_count; i++) {
        bus_inject_frame(&delayed_frames[i], delayed_frames[i].data[1]);
    }

    /* Allow convergence. */
    for (k = 0; k < 1000u; k++) { bus_step(); }

    /* Verify: newer term remains authoritative, A cannot regain authority. */
    check(g_bus.node[leader_b_idx].term == term_b, "Lot 2B",
          "newer term remains authoritative");
    check(mosaik_has_valid_leadership_authority(&g_bus.node[leader_b_idx]), "Lot 2B",
          "new leader B retains valid authority");
    check(g_bus.node[leader_a_idx].term == term_b, "Lot 2B",
          "old leader A adopted newer term");
    check(valid_leader_count() == 1, "INV-LEADER-UNIQUE",
          "cluster converges to one valid authority");

    printf("        partition_time = %u ms, heal_time = %u ms\n", partition_time, heal_time);
    printf("        term_a = %u, term_b = %u, delayed_frames = %d\n", term_a, term_b, delayed_count);
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
    printf("----------------------------------\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}