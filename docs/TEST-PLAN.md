# MOSAIK HIL Bench — Test Plan

**Document:** MOSAIK-HIL-TP-001
**Issue:** 0.6 — 17 September 2026

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

Current total at commit `ae9e408`: 60 tests, 455 checks, 0 failures,
sanitizer clean. RED-before-fix evidence is preserved in history:
TC-031 was added RED at `d38985d` (6 failing checks) and passes since
`f4e0f3c` (LOT 2D); TC-035–TC-050 were added at `af5da87` with 13 failing
checks in exactly TC-039, TC-040, TC-045, TC-047, TC-048 and TC-050; TC-050
passes since `034db92` and the five DEGRADED tests since `7df0af0`;
TC-051–TC-060 were added at `58a1b5d` with 7 failing checks in exactly
TC-052 (2) and TC-053 (5), which pass since `ae9e408`. See
`LOT2D_CRASH_RECOVERY_REPORT.md`, `LOT3_FDIR_SAFE_REPORT.md` and
`LOT4_MODE_SEMANTICS_REPORT.md`.

Run with `make test`. The suite returns a non-zero exit code on any failure and
is executed on every push by the CI workflow.

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
| — | bus load at 500 kbit/s under nominal traffic | bus timestamps |

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
- **HOST SOFTWARE DEMONSTRATOR ONLY — NO HARDWARE VALIDATION. No TRL 4 claim.**
