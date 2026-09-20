# MOSAIK HIL Bench — Test Plan

**Document:** MOSAIK-HIL-TP-001
**Issue:** 0.7 — 20 September 2026

## 1. Two levels of verification

**Level 1 — logic verification on host.** The protocol core is compiled for the
host and driven by a virtual bus in simulated time. This verifies state machine
behaviour and message handling. It produces no timing evidence: the millisecond
values printed by the suite are simulated, not measured.

**Level 2 — timing measurement on hardware.** Three nodes on a physical bus,
observed by an external logger. This produces the measured latencies. Not yet
executed; hardware not yet procured.

Nothing in this repository claims a measured result until Level 2 has run and
the traces are committed under `results/`.

## 2. Level 1 test cases

| ID | Title | Requirement | Result |
|---|---|---|---|
| TC-001 | A single leader is elected and held | REQ-002 | pass |
| TC-002 | No two leaders at any instant over 20 s | REQ-002 | pass |
| TC-003 | New leader elected after leader power loss | REQ-004 | pass |
| TC-004 | SAFE latched on same-term dual leader | REQ-005, REQ-002 | pass |
| TC-005 | Isolated node cannot self-appoint, latches SAFE | REQ-003 | pass |
| TC-006 | Frame codec round-trip, corrupted frames rejected | — | pass |
| TC-007 | 2+1 partition: lease expiry enforces unique valid leader | REQ-002, Lot 2A | pass |
| TC-008 | Old term heartbeat rejected | REQ-002, Lot 2B | pass |
| TC-009 | Replay after lease expiry rejected | REQ-002, Lot 2B | pass |
| TC-010 | Delayed old leader after partition recovery rejected | REQ-002, Lot 2B | pass |
| TC-011 | Duplicate heartbeat idempotence | REQ-002, Lot 2B | pass |
| TC-012 | Stale election/vote traffic rejected | REQ-002, Lot 2B | pass |
| TC-013 | One-way leader isolation: authority expires | REQ-002, Lot 2C | pass |
| TC-014 | Asymmetric minority view | REQ-002, Lot 2C | pass |
| TC-015 | Selective heartbeat loss | REQ-002, Lot 2C | pass |
| TC-016 | Delay around lease boundary | REQ-002, Lot 2C | pass |
| TC-017 | Message reordering, no state regression | REQ-002, Lot 2C | pass |
| TC-018 | Partition heal with queued traffic | REQ-002, Lot 2C | pass |
| TC-019 | Selective quorum contact failure | REQ-002, Lot 2C | pass |
| TC-020 | Adversarial combination | REQ-002, Lot 2C | pass |
| TC-021 | Follower crash while leader/quorum available | REQ-002, Lot 2D | pass |
| TC-022 | Leader crash, new leader elected | REQ-004, Lot 2D | pass |
| TC-023 | Leader crash, election, former leader restarts cold | REQ-002, Lot 2D | pass |
| TC-024 | Crashed follower restart and rejoin | REQ-002, Lot 2D | pass |
| TC-025 | Former leader restarts after another leader elected | REQ-002, Lot 2D | pass |
| TC-026 | Restart with delayed pre-crash messages queued | REQ-002, Lot 2B, Lot 2D | pass |
| TC-027 | Repeated crash/restart of one node | REQ-002, Lot 2D | pass |
| TC-028 | Crash during/near lease expiry, new valid leader | REQ-004, Lot 2D | pass |
| TC-029 | Crash during election (candidate), no duplicate vote | REQ-002, Lot 2D | pass |
| TC-030 | Recovery under asymmetric network | REQ-002, Lot 2D | pass |
| TC-031 | Natural election collision, randomized retry recovery | REQ-004, REQ-002, Lot 2D | pass |
| TC-032 | Permanent retry contention keeps SAFE contract (backoff span 1) | REQ-003, Lot 2D | pass |
| TC-033 | Asymmetric partition during retry, no authority without quorum | REQ-002, REQ-003, Lot 2D | pass |
| TC-034 | Stale delayed election traffic during retry | REQ-002, Lot 2B, Lot 2D | pass |
| TC-035 | SAFE node: no authority, SAFE-only transmission, cluster recovers around it | REQ-003, REQ-004, Lot 3 | pass |
| TC-036 | SAFE node grants no vote, sends no ACK, never becomes candidate | REQ-003, Lot 3 | pass |
| TC-037 | SAFE latch against old/same/higher-term traffic, grants, replays, connectivity | REQ-003, Lot 3 | pass |
| TC-038 | SAFE frames do not propagate SAFE to healthy peers | REQ-002, Lot 3 | pass |
| TC-039 | DEGRADED persists while peer SAFE evidence is fresh, no flapping | REQ-SAFE-0004, Lot 3 | pass (RED at `af5da87`) |
| TC-040 | SAFE evidence expiry, return to NOMINAL, SAFE node recovers only by cold restart | REQ-SAFE-0004, Lot 3 | pass (RED at `af5da87`) |
| TC-041 | Election exhaustion terminal even after connectivity returns | REQ-003, Lot 3 | pass |
| TC-042 | Leader isolation boundary 600–2000 ms with recovered predicate | REQ-004, Lot 3 | pass |
| TC-043 | Malformed frames: decode errors counted, no state change (PROTO_ERROR reserved) | REQ-FUNC-0007, Lot 3 | pass |
| TC-044 | Survivor crash during collision recovery, lone node SAFE/no-quorum | REQ-003, Lot 3 | pass |
| TC-045 | Replayed SAFE evidence: bounded DEGRADED window, local evidence only | REQ-SAFE-0004, Lot 3 | pass (RED at `af5da87`) |
| TC-046 | Three transient leader isolations recover without SAFE | REQ-004, Lot 3 | pass |
| TC-047 | Leader SAFE + stale heartbeat replay + one-way drop during election | REQ-SAFE-0004, REQ-002, Lot 3 | pass (RED at `af5da87`) |
| TC-048 | Leader crash + natural collision + delayed SAFE frame during backoff | REQ-SAFE-0004, REQ-004, Lot 3 | pass (RED at `af5da87`) |
| TC-049 | Deterministic reproducibility of the SAFE/DEGRADED scenario | — | pass |
| TC-050 | One vote per term across a same-term step-down (LOT 2 erratum) | — (protocol invariant), Lot 3 | pass (RED at `af5da87`, green at `034db92`) |
| TC-051 | Mode legality guard: state × role pairs and emitted state bytes over representative scenarios | INV-MODE-LEGAL, Lot 4 | pass |
| TC-052 | Fault-free cold boot: no DEGRADED without peer SAFE evidence (C1) | INV-MODE-NO-MAGIC, Lot 4 | pass (RED at `58a1b5d`) |
| TC-053 | Higher-term SAFE announcement has no authority effect on the healthy majority (C2) | INV-SAFE-ANNOUNCE-NO-AUTHORITY-EFFECT, REQ-SAFE-0004, Lot 4 | pass (RED at `58a1b5d`) |
| TC-054 | Heartbeat state metadata is not protocol evidence (INIT/NOMINAL/DEGRADED/SAFE) | INV-MODE-METADATA-NONAUTHORITATIVE, Lot 4 | pass |
| TC-055 | Out-of-range state byte with valid CRC: decoder rejects, no protocol effect | REQ-FUNC-0007, Lot 4 | pass |
| TC-056 | Stale old-term heartbeat carrying DEGRADED metadata: rejected, no state change | Lot 2B semantics, Lot 4 | pass |
| TC-057 | Legitimate leader change while DEGRADED: persistence, legality, authority rules | REQ-SAFE-0004, REQ-FUNC-0004, Lot 4 | pass |
| TC-058 | Partition / crash / re-election / cold restart legality | INV-MODE-LEGAL, Lot 4 | pass |
| TC-059 | Lower-term SAFE announcement: evidence only, healthy terms and authority unchanged | REQ-SAFE-0004, Lot 4 | pass |
| TC-060 | Determinism of the TC-052 and TC-053 scenarios (run A vs run B) | — | pass |
| TC-061 | Membership is an explicit committed mask with its own epoch; cluster_size is inert | INV-RECONFIG-QUORUM, Lot 5 | pass |
| TC-062 | Peer loss is not membership removal | INV-RECONFIG-NO-MAGIC, Lot 5 | pass |
| TC-063 | Minority partition cannot reconfigure itself into a quorum | INV-RECONFIG-PARTITION, Lot 5 | pass |
| TC-064 | A membership change is proposed, agreed and committed | REQ-FUNC-0002, Lot 5 | pass (RED at `02b27fa`) |
| TC-065 | A reduction affects quorum only once both configurations agreed | INV-RECONFIG-TRANSITION, Lot 5 | pass (RED at `02b27fa`) |
| TC-066 | A removed node does not regain voting membership by cold restart | INV-RECONFIG-REMOVED-NODE, Lot 5 | pass (RED at `02b27fa`) |
| TC-067 | Stale, duplicate and unsupported-future configuration traffic | INV-RECONFIG-OLD-CONFIG, Lot 5 | pass (RED at `02b27fa`) |
| TC-068 | Partition during a transition: no incompatible authority | INV-RECONFIG-TRANSITION, Lot 5 | pass (RED at `02b27fa`) |
| TC-069 | Authority and lease non-regression with membership traffic | REQ-FUNC-0001, Lot 5 | pass |
| TC-070 | Determinism of the reconfiguration scenarios | — | pass |
| TC-071 | Conflicting successors of one epoch cannot both be agreed | INV-RECONFIG-CONSISTENT, Lot 5 | pass |
| TC-072 | Duplicate and reordered transaction frames are idempotent | INV-RECONFIG-OLD-CONFIG, Lot 5 | pass |
| TC-073 | Malformed, empty and single-node memberships refused; epoch does not wrap | REQ-FUNC-0007, Lot 5 | pass |
| TC-074 | Proposer failure before agreement, after an undelivered commit, after a partial commit | INV-RECONFIG-TRANSITION, Lot 5 | pass |
| TC-075 | Removed-node traffic restores no voting, authority or lease | INV-RECONFIG-REMOVED-NODE, Lot 5 | pass |
| TC-076 | Every permitted transition, re-admission and a two-change transition | REQ-FUNC-0002, Lot 5 | pass |
| TC-077 | Host-model configuration store: binding, commit, corrupted content | INV-RECONFIG-NO-MAGIC, Lot 5 | pass |
| TC-078 | Transaction alongside SAFE, DEGRADED, an election and a lease expiry | REQ-SAFE-0003/0004, Lot 5 | pass |
| TC-079 | Partial COMMIT delivery exhausted over subsets, partitions, crash points | REQ-FUNC-0001, Lot 5 Phase 3 | pass (1536 schedules) |
| TC-080 | CONFIG loss, duplication, delay and reordering at every stage | INV-RECONFIG-OLD-CONFIG, Lot 5 Phase 3 | pass |
| TC-081 | Proposer failure at ten points x four restart conditions | INV-RECONFIG-TRANSITION, Lot 5 Phase 3 | pass (40 schedules) |
| TC-082 | Acceptor failure around its acceptance; persisted binding binds | INV-RECONFIG-CONSISTENT, Lot 5 Phase 3 | pass (6 crash points) |
| TC-083 | Removed-node attacks and missed-epoch safety | INV-RECONFIG-REMOVED-NODE, Lot 5 Phase 3 | pass (10 stuck cases) |
| TC-084 | Re-admission does not turn old evidence into fresh authority | INV-RECONFIG-REMOVED-NODE, Lot 5 Phase 3 | pass |
| TC-085 | Two different successors of one epoch | INV-RECONFIG-CONSISTENT, Lot 5 Phase 3 | pass |
| TC-086 | Configuration epoch and leadership term independence | INV-TERM-MONOTONIC, Lot 5 Phase 3 | pass |
| TC-087 | Lease evidence attacks and the exact freshness boundary | REQ-FUNC-0001, Lot 5 Phase 3 | pass (99/100 ms) |
| TC-088 | SAFE and DEGRADED injected at every transaction stage | REQ-SAFE-0003/0004, Lot 5 Phase 3 | pass |
| TC-089 | One-millisecond boundaries around the protocol timers | REQ-FUNC-0001, Lot 5 Phase 3 | pass (0 overlaps) |
| TC-090 | Bounded deterministic schedule explorer | REQ-FUNC-0001, Lot 5 Phase 3 | pass (4608 schedules) |
| TC-091 | Transport status is local evidence about one node's own controller only | HIL-COM-001/002/003, INV-TRANSPORT-LOCAL-EVIDENCE | pass (9 checks) |
| TC-092 | A bus-off leader must hold no valid leadership authority | HIL-COM-004, INV-TRANSPORT-NO-MUTE-AUTHORITY | **RED — 2 failing checks** |
| TC-093 | A bus-off node must hand no frame to its transmitter; DEGRADED still transmits | HIL-COM-005/002, INV-TRANSPORT-NO-TX | **RED — 1 failing check** |
| TC-094 | A transport outage fabricates no lease, acknowledgement or quorum evidence | HIL-COM-006, INV-TRANSPORT-NO-FABRICATED-EVIDENCE | pass (5 checks) |
| TC-095 | Bus-off is not FDIR evidence and does not latch SAFE by itself | HIL-COM-007, INV-TRANSPORT-NOT-FDIR | pass (4 checks) |
| TC-096 | An outage shorter than the lease still revokes authority; recovery restores none | HIL-COM-004/008, INV-TRANSPORT-RECOVERY-NO-AUTHORITY | **RED — 1 failing check** |
| TC-097 | A bus-off follower must offer no acknowledgement or vote grant | HIL-COM-005, INV-TRANSPORT-NO-TX | **RED — 1 failing check** |
| TC-098 | Transport fault during a membership transaction: no mute authority, no mute CONFIG traffic, joint quorum intact | HIL-COM-004/005/011 | **RED — 2 failing checks** |
| TC-099 | CAN-FD transport does not widen the logical frame; FD payload lengths rejected | HIL-COM-009, INV-ICD-FRAME-GEOMETRY | pass (5 checks) |
| TC-100 | Chronology reconstruction without a correlation_id, and its measured boundary | HIL-COM-010, INV-CHRONOLOGY-SUFFICIENT | pass (4 checks) |
| TC-101 | Local muteness beats acknowledgements that really do form fresh quorum evidence | HIL-COM-004, INV-TRANSPORT-NO-MUTE-AUTHORITY | pass (4 checks) |
| TC-102 | Bus-off at maximum remaining lease (499 ms unspent) | HIL-COM-004 | pass (4 checks) |
| TC-103 | BUS_OFF to RECOVERING to UP with delayed pre-fault frames replayed | HIL-COM-008, INV-NO-STALE-RECOVERY | pass (4 checks) |
| TC-104 | Flapping across heartbeat and election boundaries, 16 schedules | all LOT 6A invariants, continuous oracle | pass (2 checks) |
| TC-105 | Follower muted exactly as it would grant a vote | HIL-COM-005, INV-ONE-VOTE-PER-TERM | pass (5 checks) |
| TC-106 | Leader muted at eight membership-transaction stages | HIL-COM-011, INV-RECONFIG-QUORUM | pass (3 checks) |
| TC-107 | Acceptor muted between binding and transmission | HIL-COM-005/011, INV-RECONFIG-CONSISTENT | pass (7 checks) |
| TC-108 | Removed node loses and regains transport | INV-RECONFIG-REMOVED-NODE | pass (8 checks) |
| TC-109 | SAFE node loses and regains transport | HIL-COM-005, INV-SAFE-LATCH | pass (6 checks) |
| TC-110 | DEGRADED transport is a fault indication, not muteness | HIL-COM-002, INV-TRANSPORT-NOT-FDIR | pass (6 checks) |
| TC-111 | RECOVERING is still off the bus | HIL-COM-004/005/008 | pass (5 checks) |
| TC-112 | Transport fault combined with a network partition | INV-TRANSPORT-LOCAL-EVIDENCE | pass (5 checks) |
| TC-113 | Transport fault across election collisions, 10 offsets | INV-ONE-VOTE-PER-TERM, INV-TERM-MONOTONIC | pass (2 checks) |
| TC-114 | Pre-fault frames released after recovery | HIL-COM-008, INV-LEADER-UNIQUE | pass (4 checks) |
| TC-115 | Deterministic reproducibility over a 6000-byte state trace | LOT 6A determinism | pass (1 check) |
| TC-116 | **Critical case:** transport returns before a replacement is elected | HIL-COM-008, INV-TRANSPORT-RECOVERY-NO-AUTHORITY | pass (9 checks) |
| TC-117 | Bounded deterministic exploration, 1152 schedules | all LOT 6A invariants, continuous oracle | pass (2 checks, 0 violating schedules) |

Previous total at commit `9ccd28e`: 90 tests, 761 checks, 0 failures.
LOT 6A RED baseline `f3d6484`: 100 tests, 811 checks, **7 failures** in
exactly TC-092 (2), TC-093 (1), TC-096 (1), TC-097 (1), TC-098 (2).
LOT 6A minimal GREEN `0667d04`: 100 tests, 811 checks, 0 failures.

**Current total at commit `2c13556`: 117 tests, 888 checks, 0 failures,
sanitizer clean.** TC-001 to TC-090 and TC-091 to TC-100 are unchanged and
their output is byte-identical to `0667d04`. RED-before-fix evidence is preserved in history:
TC-031 was added RED at `d38985d` (6 failing checks) and passes since
`f4e0f3c` (LOT 2D); TC-035–TC-050 were added at `af5da87` with 13 failing
checks in exactly TC-039, TC-040, TC-045, TC-047, TC-048 and TC-050; TC-050
passes since `034db92` and the five DEGRADED tests since `7df0af0`;
TC-051–TC-060 were added at `58a1b5d` with 7 failing checks in exactly
TC-052 (2) and TC-053 (5), which pass since `ae9e408`; TC-061–TC-070 were
added at `02b27fa` with 11 failing checks in exactly TC-064, TC-065, TC-066,
TC-067 and TC-068, which pass since `146472f`. See
`LOT2D_CRASH_RECOVERY_REPORT.md`, `LOT3_FDIR_SAFE_REPORT.md`,
`LOT4_MODE_SEMANTICS_REPORT.md` and `LOT5_RECONFIGURATION_REPORT.md`.

**LOT 5 test phases.** TC-061 to TC-063 are Phase-1 characterisation of the
fixed-membership baseline. TC-064 to TC-068 are the Phase-1 RED evidence,
rewritten at `146472f` from "capability absent" observations into executable
functional properties. TC-069 to TC-078 are Phase-2 functional validation of
the implemented mechanism. TC-079 to TC-090 are the Phase-3 adversarial
campaign, added at `9ccd28e` with no firmware change, covering partial
COMMIT, partitions, proposer crash, acceptor crash, restart, loss,
duplication, delay, reordering, removed-node attacks, re-admission,
conflicting successors, term and configuration-epoch interaction, lease
evidence, SAFE and DEGRADED, time boundaries, and a bounded deterministic
schedule explorer. The committed and reproducible adversarial evidence is
6236 in-suite schedules; exploratory work outside the repository is not
counted as evidence.

**LOT 6A test phase (pre-hardware).** TC-091 to TC-100 test the
**software-facing** communication contract only. None of them measures a
physical bus, a bitrate, a transceiver, an arbitration delay or a real bus-off
detection or recovery time; no hardware exists and no such claim is made.
Physical validation is LOT 6B. The transport fault is injected by the harness
as a status reported to **one node about its own controller**, together with
the physical consequence that its frames do not reach the bus and bus frames
do not reach it. No node is told anything about any peer, and the emitted-frame
counters are observation only, read by tests and never by a node.

TC-092, TC-093, TC-096, TC-097 and TC-098 were RED by design at `f3d6484`:
the transport interface was inert — the status was recorded and no protocol
decision read it. They pass since the minimal GREEN `0667d04`, which added
twelve lines of logic to `firmware/core/mosaik_node.c` and changed no test.

**LOT 6A adversarial phase (TC-101 to TC-117).** The implementation is
attacked, not merely exercised. Two fault shapes are used and the difference
is the point: either the controller is off the bus entirely, or the node
reports itself unable to transmit **while the harness keeps delivering frames
to it**. The second is physically real — a dead transmitter or a broken TX
line whose receiver still works — and it is what shows the protocol carries
the invariant rather than the harness carrying it. TC-101 hands a mute leader
two acknowledgements that genuinely satisfy `mosaik_has_quorum_ack_evidence()`
and the authority is still refused. Transmit refusal is measured at the true
callback boundary, so "the core never called tx" is distinguished from "the
core called tx and the frame was dropped"; the measured result is zero calls.
TC-117 explores 1152 bounded schedules under a per-millisecond oracle with
zero violations — bounded exploration, not a proof and not model checking.
No counterexample against the implementation was found. See
`LOT6A_TRANSPORT_REPORT.md`.

TC-100 reports a measured limitation rather than asserting its absence: the
heartbeat sequence is one byte, so (source, term, sequence) repeats after
25 600 ms within a single term — 41 repeats were observed in a 30 s stable
term. The external observer's timestamp is therefore **required**, and with it
no `correlation_id` is needed (`docs/ICD-HIL.md` §6).

Run with `make test`. The suite returns a non-zero exit code on any failure and
is executed on every push by the CI workflow. It exits zero at `2c13556`; it
exited non-zero at the RED baseline `f3d6484`, by design.

## 3. Level 2 measurement method

**Instrumentation.** A USB-CAN adapter in listen-only mode logs every frame
with a kernel timestamp (`candump -ta`). Each node additionally drives one GPIO
high while holding leadership and one GPIO high while in SAFE; both are captured
by a logic analyser.

Serial output over USB is used for diagnostics only. It is not a measurement
path: the USB polling interval is of the same order as the intervals being
measured.

**Fault injection.** The leader's supply is interrupted by a MOSFET switch
driven from the host, giving a repeatable and representative loss-of-node
event. Injecting a "kill" frame instead would bypass timeout-based detection
and is therefore not used for REQ-004.

**Measurands.**

| Requirement | Measurand | Source |
|---|---|---|
| REQ-004 | interval from last heartbeat of the failed leader to first heartbeat of the new leader | bus timestamps |
| REQ-004 | interval between leadership GPIO falling on the old leader and rising on the new one | logic analyser |
| REQ-005 | interval from injected frame on the bus to SAFE GPIO rising | logic analyser |
| — | bus load at the planned 500 kbit/s arbitration bitrate under nominal traffic (PLANNED, no hardware) | bus timestamps |

**Sample size.** Thirty repetitions per measurand. Reporting minimum, median,
maximum and standard deviation. A single figure is not reported.

**Pass criteria.** REQ-004: all thirty failover intervals below 1000 ms.
REQ-005: all thirty detection-to-SAFE intervals below 10 ms. Any outlier is
reported, not discarded.

## 4. Known limitations of the bench

- Three nodes, so quorum is two and a single loss is the only tolerable fault.
- Bench conducted at ambient conditions. No vibration, thermal or EMC
  environment. Results carry no environmental qualification claim.
- Commercial development boards, not representative hardware. No radiation,
  derating or parts-quality argument is available.
- Clock drift between nodes is not characterised.
- Results are valid for the timing parameters of PROTOCOL.md section 6 only.
- **Leadership lease (500 ms) is a host-model parameter; it does not validate
  physical CAN-FD timing. TC-007 uses a simulated pairwise connectivity model
  for the 2+1 partition; no hardware network partition is tested.**
- **Stale/replay immunity (Lot 2B) uses semantic rejection based on term
  monotonicity, lease validity, and sender state. No cryptographic anti-replay
  or sequence-number protection is implemented; the current 8-byte frame
  format has no room for correlation_id. TC-008–TC-012 test deterministic
  scenarios only; no randomized network fault injection (reserved for LOT 2C).**
- **Lot 2C uses a directional per-path network model (deliver, drop, delay,
  reorder) on the virtual bus. Lease renewal is credited only from ACK frames
  actually received by the leader in its current term; the bookkeeping is
  performed by the harness from the node's own recorded evidence.**
- **Lot 2D crash and restart are harness models: a crash silences the node
  and a restart is a cold `mosaik_init()`. No persistence is implemented.
  TC-031 to TC-034 create election collisions by crashing the leader at a
  naturally occurring shared follower deadline found by read-only
  observation; no protocol internal is written by any test. Recovery times
  (TC-028: 628 ms, TC-031: 680 ms) are deterministic host-model values, not
  a worst-case bound. A split vote remains possible; sub-millisecond bus
  races are not modelled.**
- **Lot 3 SAFE and DEGRADED are protocol-core semantics on the virtual bus.
  SAFE is latched within one powered node instance and cleared by a cold
  restart because persistence is not implemented; PGA is not implemented.
  DEGRADED is derived only from SAFE frames actually received; the harness
  observes states and transmissions but supplies no evidence to any node.
  PROTO_ERROR is reserved and never raised. TC-042 isolation durations are
  deterministic host observations, not timing bounds.**
- **Lot 6A is pre-hardware. The transport status, the bus-off contract and the
  ICD are specified, implemented and host-tested as software behaviour only.
  Hardware bus-off detection and recovery timing, bitrates, transceiver
  behaviour, electrical integrity, arbitration timing, bus load, clock drift
  and hardware error-frame behaviour have NO evidence and are LOT 6B. The
  protocol also trusts the transport status it is told: a platform that
  reports UP while its controller is bus-off defeats every Lot 6A guarantee,
  and validating that reporting is LOT 6B. SAFE announcements a node owed
  while mute are lost, so a peer's DEGRADED evidence can expire during a
  transport outage.**
- **HOST SOFTWARE DEMONSTRATOR ONLY — NO HARDWARE VALIDATION. No TRL 4 claim.**
