# MOSAÏK ADD-to-Implementation Mapping

**Document:** MOSAIK-ADD-MAP-001  
**Issue:** 1.3 — 20 September 2026  
**Purpose:** Master implementation map linking ADD requirements to architecture, code, tests, and evidence

---

## 1. Mapping Table Columns

| Column | Description |
|--------|-------------|
| **ADD Section** | Source section in MOSAIK-ADD-0001 |
| **Requirement ID** | ROOT (Section 10) or DERIVED (Section 82) |
| **Requirement Summary** | Normalized statement |
| **Target Component** | Architecture element (layer/module/task) |
| **GitHub Implementation** | File(s) or "NOT IMPLEMENTED" |
| **Test** | Repository test case(s) |
| **Evidence** | Evidence reference / classification |
| **LOT** | Development Lot of this repository. **Adv** = deferred to MOSAÏK Advanced, the separate future ADD-driven project; see `ROADMAP.md` §2. |
| **Status** | IMPLEMENTED-SIM / PARTIAL / DESIGN-ONLY / NOT-STARTED / HARDWARE-REQUIRED / TBC |
| **Gap / Limitation** | Known limitations, simulation-only, missing scope |

---

## 2. Initial Mapping (Conservative)

| ADD Section | Requirement ID | Requirement Summary | Target Component | GitHub Implementation | Test | Evidence | LOT | Status | Gap / Limitation |
|-------------|----------------|---------------------|------------------|----------------------|------|----------|-----|--------|------------------|
| 10 / 82 | REQ-FUNC-0001 / REQ-FUN-001 | One active leader outside declared partition | L4: svc_mosaik_proto, L5: Cluster_Task | `firmware/core/mosaik_node.c/h` | TC-001, TC-002, TC-007, TC-008, TC-009, TC-010, TC-011, TC-012 | LOT2A_LEADER_LEASE_REPORT.md, LOT2B_STALE_REPLAY_REPORT.md | 2A, 2B | PARTIAL | Host sim only; 3-node subset; "outside declared partition" vs "at all times" (ADD-F002) |
| 10 / 82 | REQ-FUNC-0002 / REQ-FUN-002 | Quorum required for reconfiguration | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` (quorum func) | TC-005, TC-007 | LOT2A_LEADER_LEASE_REPORT.md | 2A | IMPLEMENTED-SIM | 3-node quorum=2 only; not universal proof; GSE voting role TBC (ADD-F008) |
| 10 / 82 | REQ-FUNC-0003 / REQ-FUN-003 | Heartbeat nominal period 100 ms | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` (heartbeat_period_ms=100) | TC-001, TC-006 | PROTOCOL.md §6 | 1 | IMPLEMENTED-SIM | Period configured; not measured on hardware; derived tolerance ±2% (ADD-F010) |
| 10 / 82 | REQ-FUNC-0004 / REQ-PERF-001 | Leader loss detection & re-election < 1 s | L4: svc_mosaik_proto, L5: Cluster_Task | `firmware/core/mosaik_node.c` (election timeout) | TC-003 | LOT2A_LEADER_LEASE_REPORT.md | 2A | IMPLEMENTED-SIM | Simulated time (452 ms); no hardware measurement |
| 10 | REQ-FUNC-0005 | Mission useful after 1 EN loss in DEGRADED | L6: mission modules, L5: Cluster_Task | NOT IMPLEMENTED | — | — | Adv | DESIGN-ONLY | Requires 6-node architecture — deferred to MOSAÏK Advanced |
| 10 | REQ-FUNC-0006 | Mode transitions & critical decisions logged | L4: svc_logger, L5: Logger_Task | NOT IMPLEMENTED | — | — | 7 | DESIGN-ONLY | Logger service not implemented |
| 10 / 82 | REQ-FUNC-0007 / REQ-ICD-001, REQ-ICD-002 | Critical CAN msgs: CRC + correlation_id | L3: drv_canfd, L4: svc_mosaik_proto | `firmware/core/mosaik_proto.c` (CRC-8) | TC-006 | PROTOCOL.md §5 | 1 | PARTIAL | CRC-8 implemented; correlation_id NOT implemented (ADD-F007) |
| 10 | REQ-FUNC-0008 | CN maintains cluster blackbox | L4: svc_logger, L5: Logger_Task | NOT IMPLEMENTED | — | — | Adv | DESIGN-ONLY | CN node not implemented — deferred to MOSAÏK Advanced |
| 10 | REQ-FUNC-0009 | COMN exports logs to GSE | L4: svc_tm, L5: Companion_Task | NOT IMPLEMENTED | — | — | Adv | DESIGN-ONLY | COMN node not implemented — deferred to MOSAÏK Advanced |
| 10 | REQ-FUNC-0010 | Complete event timeline reconstruction | L4: svc_logger, L5: Logger_Task | NOT IMPLEMENTED | — | — | Adv / 7 | DESIGN-ONLY | ADD-level reconstruction requires blackbox + GSE (MOSAÏK Advanced); the bench run chronology is LOT 7 |
| 10 / 82 | REQ-SAFE-0002 / REQ-SAF-001 | Split-brain detection & latch < 10 ms | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` (enter_safe) | TC-004 | LOT2A_LEADER_LEASE_REPORT.md | 2A | IMPLEMENTED-SIM | Host receive path (0 ms sim); no hardware measurement |
| 10 / 82 | REQ-SAFE-0003 / REQ-SAF-002 | SAFE irreversible without ground arbitration | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` (SAFE latch, receive and service gates) | TC-004, TC-005; TC-035–TC-037, TC-041 | LOT2A_LEADER_LEASE_REPORT.md; LOT3_FDIR_SAFE_REPORT.md §6 | 2A, 3 | IMPLEMENTED-SIM | Software latch within one powered node instance; cold restart clears it; no PGA (ADD-F009) |
| 10 / 82 | REQ-SAFE-0004 / REQ-SAF-003 | DEGRADED on peer SAFE | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` (SAFE handler evidence, heartbeat/leader state decision, DEGRADED exit) | TC-039, TC-040 (first executable evidence); TC-045, TC-047, TC-048 | LOT3_FDIR_SAFE_REPORT.md §7–§10 | 3 | IMPLEMENTED-SIM | TC-004 asserts SAFE/split-brain only and is not evidence for this row; exit/freshness semantics are a host interpretation (ADD-F011) |
| 10 / 82 | REQ-PERF-0001 / REQ-PERF-001 | Election completion < 1000 ms | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` | TC-003 | LOT2A_LEADER_LEASE_REPORT.md | 2A | IMPLEMENTED-SIM | Simulated time only |
| 10 / 82 | REQ-PERF-0002 / REQ-PERF-002 | SAFE latch < 10 ms | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` | TC-004 | LOT2A_LEADER_LEASE_REPORT.md | 2A | IMPLEMENTED-SIM | Host receive path |
| 10 | REQ-PERF-0003 | Heartbeat period 100 ms ± tolerance | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` | TC-001 | PROTOCOL.md §6 | 1 | IMPLEMENTED-SIM | Configured; not measured; derived: 10 Hz ±2% (ADD-F010) |
| 10 | REQ-PERF-0004 | CAN-FD 500k/2M bit/s | L3: drv_canfd | NOT IMPLEMENTED | — | — | 6 | HARDWARE-REQUIRED | Requires STM32H743 FDCAN; protocol model compatible — see ADD-F004 |
| 10 | REQ-ENV-0001 | LEO thermal/vibration/radiation | L0-L6: all | NOT IMPLEMENTED | — | — | 13-14 | HARDWARE-REQUIRED | Flight hardware required; no environmental qualification (ADD-F005) |
| 10 | REQ-IF-0001 | CAN-FD primary backbone ICD | L3: drv_canfd, L4: svc_mosaik_proto | `firmware/core/mosaik_proto.c/h` (frame format) | TC-006 | PROTOCOL.md | 1 | PARTIAL | Frame format defined; FD not validated (ADD-F004) |
| 10 | REQ-IF-0002 | Ethernet secondary | L3: drv_eth | NOT IMPLEMENTED | — | — | 6 | DESIGN-ONLY | |
| 10 | REQ-IF-0003 | Wired safety discretes | L3: drv_gpio, L5: Safety_Task | NOT IMPLEMENTED | — | — | 11 | DESIGN-ONLY | Software SAFE only (ADD-F009); LOT 3 documented the gap, no software substitute |
| 10 | REQ-IF-0004 | 24V distributed power | L2: BSP power | NOT IMPLEMENTED | — | — | 13 | NOT-STARTED | |
| 10 | REQ-LOG-0001 | Mode transitions logged | L4: svc_logger | NOT IMPLEMENTED | — | — | 7 | DESIGN-ONLY | |
| 10 | REQ-LOG-0002 | Critical decisions logged w/ correlation_id | L4: svc_logger | NOT IMPLEMENTED | — | — | 7 | DESIGN-ONLY | correlation_id not yet in wire format (ADD-F007) |
| 10 | REQ-LOG-0003 | Blackbox survives restart | L4: svc_fs, L5: Logger_Task | NOT IMPLEMENTED | — | — | 8 | DESIGN-ONLY | |

---

## 3. LOT 2A Specific Mapping (Current Baseline)

| ADD Ref | LOT 2A Feature | Implementation | Test | Evidence | Status | Limitation |
|---------|----------------|----------------|------|----------|--------|------------|
| REQ-FUNC-0001 | 500 ms leadership lease | `mosaik_node.c`: lease_expiry_ms, step-down on expiry | TC-007 | LOT2A_LEADER_LEASE_REPORT.md | IMPLEMENTED-SIM | Host sim; deterministic; 3-node 2+1 only |
| REQ-FUNC-0001 | Valid authority predicate | `mosaik_node.h/c`: `mosaik_has_valid_leadership_authority()` | TC-007 | LOT2A_LEADER_LEASE_REPORT.md | IMPLEMENTED-SIM | Distinguishes ROLE_LEADER from authority |
| REQ-FUNC-0002 | Lease renewal on quorum contact | `mosaik_node.c`: ACK handler records per-peer current-term evidence; `test_mosaik.c`: renewal only on an ACK actually received by the leader (commit `c6f600b`) | TC-007, TC-013, TC-019 | LOT2A_LEADER_LEASE_REPORT.md, LOT2D_CRASH_RECOVERY_REPORT.md §6 | IMPLEMENTED-SIM | Harness-mediated renewal bookkeeping from node-recorded evidence only; outbound heartbeat alone never renews |
| REQ-FUNC-0004 | Follower election timeout aligned to lease | `mosaik_node.c`: deadline = HB + lease + jitter | TC-007 | LOT2A_LEADER_LEASE_REPORT.md | IMPLEMENTED-SIM | Prevents premature challenge |
| — | INV-LEADER-UNIQUE invariant | TC-007 checks valid_leader_count ≤ 1 at every step | TC-007 | LOT2A_LEADER_LEASE_REPORT.md | CbD (sim) | Observed at simulation points only |

---

## 4. LOT 2B Specific Mapping (Stale/Replay Immunity)

| ADD Ref | LOT 2B Feature | Implementation | Test | Evidence | Status | Limitation |
|---------|----------------|----------------|------|----------|--------|------------|
| REQ-FUNC-0001 | Term monotonicity enforcement | `mosaik_node.c`: check_heartbeat_stale, term check | TC-008, TC-012 | LOT2B_STALE_REPLAY_REPORT.md | IMPLEMENTED-SIM | Host sim; semantic rejection only |
| REQ-FUNC-0001 | Duplicate sequence rejection | `mosaik_node.c`: last_hb_seq tracking | TC-011 | LOT2B_STALE_REPLAY_REPORT.md | IMPLEMENTED-SIM | No cryptographic anti-replay |
| REQ-FUNC-0001 | Expired lease replay rejection | `mosaik_node.c`: lease_expiry_ms check in stale check | TC-009 | LOT2B_STALE_REPLAY_REPORT.md | IMPLEMENTED-SIM | Requires isolation to test expiry |
| REQ-FUNC-0001 | Partition recovery safety | `mosaik_node.c`: stale authority check via last_hb_term | TC-010 | LOT2B_STALE_REPLAY_REPORT.md | IMPLEMENTED-SIM | Deterministic 2+1 partition |
| REQ-FUNC-0001 | Stale vote request rejection | `mosaik_node.c`: term check in VOTE_REQ handler | TC-012 | LOT2B_STALE_REPLAY_REPORT.md | IMPLEMENTED-SIM | No vote replay protection beyond term |
| — | INV-LEADER-UNIQUE under stale traffic | TC-008–TC-012 check valid_leader_count ≤ 1 | TC-008–TC-012 | LOT2B_STALE_REPLAY_REPORT.md | CbD (sim) | At instrumented observation points |

---

## 5. LOT 2C / LOT 2D Specific Mapping (Directional Faults, Crash/Restart, Retry Backoff)

| ADD Ref | Feature | Implementation | Test | Evidence | Status | Limitation |
|---------|---------|----------------|------|----------|--------|------------|
| REQ-FUNC-0001 | Authority under directional faults | `test_mosaik.c`: per-path deliver/drop/delay/reorder model | TC-013–TC-020 | PROTOCOL.md §6–7, TEST-PLAN.md, TRACEABILITY.md §6 | IMPLEMENTED-SIM | Validated in the deterministic host demonstrator; no dedicated LOT 2C report (documentation limitation only) |
| REQ-FUNC-0002 | Quorum evidence only from received ACK | `mosaik_node.c`: ACK handler; `test_mosaik.c`: `bus_step()` renewal on received ACK | TC-013, TC-019, TC-028 | LOT2D_CRASH_RECOVERY_REPORT.md §6 | IMPLEMENTED-SIM | Harness-mediated bookkeeping |
| REQ-FUNC-0001 | Crash / cold restart | `test_mosaik.c`: `bus_crash_node()`, `bus_restart_node()` | TC-021–TC-027, TC-029, TC-030 | LOT2D_CRASH_RECOVERY_REPORT.md §3–4 | IMPLEMENTED-SIM | No term/vote persistence |
| REQ-FUNC-0004 / REQ-PERF-0001 | Recovery from election collision | `mosaik_node.c`: candidate retry backoff in `mosaik_tick()`, `candidate_retry_backoff_span_ms` | TC-028, TC-031 | LOT2D_CRASH_RECOVERY_REPORT.md §8–10 | IMPLEMENTED-SIM | 628 ms / 680 ms in host model only; split vote remains possible |
| REQ-SAFE-0003 | SAFE after genuine failed elections under persistent contention | unchanged `enter_safe()` path; TC-032 uses backoff span 1 | TC-032, TC-033 | LOT2D_CRASH_RECOVERY_REPORT.md §10–11 | IMPLEMENTED-SIM | Deterministic host model |
| — | INV-LEADER-UNIQUE under crash, restart, collision, stale retry traffic | max valid authorities observed 1 | TC-021–TC-034 | LOT2D_CRASH_RECOVERY_REPORT.md §11 | CbD (sim) | At instrumented observation points |

---

## 6. LOT 3 Specific Mapping (FDIR / SAFE)

| ADD Ref | Feature | Implementation | Test | Evidence | Status | Limitation |
|---------|---------|----------------|------|----------|--------|------------|
| REQ-SAFE-0003 | SAFE contract: no authority, no participation, SAFE-only transmission, latch within one powered node instance | `mosaik_node.c`: `enter_safe()`, receive-path gate, service gate | TC-035–TC-038, TC-041 | LOT3_FDIR_SAFE_REPORT.md §6 | IMPLEMENTED-SIM | Cold restart clears SAFE; no PGA |
| REQ-SAFE-0004 | DEGRADED from received peer SAFE evidence, freshness 3 heartbeat periods, evidence-based exit | `mosaik_node.c`: `last_safe_rx_ms[]`, `safe_evidence_mask`, `peer_safe_evidence_fresh()` | TC-039, TC-040, TC-045, TC-047, TC-048 | LOT3_FDIR_SAFE_REPORT.md §7–§10 | IMPLEMENTED-SIM | Host interpretation ADD-F011; locally received frames only |
| REQ-FUNC-0004 / REQ-PERF-0001 | Recovery around a SAFE node; transient isolation recovery | unchanged election, lease and backoff paths | TC-035, TC-042, TC-046 | LOT3_FDIR_SAFE_REPORT.md §10 | IMPLEMENTED-SIM | Durations are host observations, not bounds |
| REQ-FUNC-0002 | No authority without quorum; exhaustion terminal | unchanged `enter_safe()` NO_QUORUM path | TC-041, TC-044 | LOT3_FDIR_SAFE_REPORT.md §6 | IMPLEMENTED-SIM | Deterministic host model |
| REQ-FUNC-0007 | Corrupted frames detected and counted only (PROTO_ERROR reserved) | `mosaik_proto.c` decoder, `decode_errors` | TC-043 | LOT3_FDIR_SAFE_REPORT.md §4 (D2) | PARTIAL | No PROTO_ERROR policy defined |
| — | One vote per term across same-term role change (LOT 2 erratum) | `mosaik_node.c`: `become_follower()` no longer erases `voted_for` (commit `034db92`) | TC-050 | LOT3_FDIR_SAFE_REPORT.md §15–§16; LOT2D_CRASH_RECOVERY_REPORT.md addendum | IMPLEMENTED-SIM | Protocol invariant, no ADD requirement ID |
| — | INV-LEADER-UNIQUE under SAFE, DEGRADED and adversarial traffic | max valid authorities observed 1 | TC-035–TC-050 | LOT3_FDIR_SAFE_REPORT.md §11 | CbD (sim) | At instrumented observation points |

---

## 7. LOT 4 Specific Mapping (Mode Semantics, four-state host model)

| ADD Ref | Feature | Implementation | Test | Evidence | Status | Limitation |
|---------|---------|----------------|------|----------|--------|------------|
| — (ADD-F012) | Election start does not degrade: a fault-free election leaves the operational state untouched; a healthy candidate may remain INIT | `mosaik_node.c`: `start_election()` no longer writes `state` (commit `ae9e408`) | TC-052 (RED at `58a1b5d`), TC-051, TC-058 | LOT4_MODE_SEMANTICS_REPORT.md §8, §11 | IMPLEMENTED-SIM | Four-state host model only; no ADAPTIVE, no PGA |
| REQ-SAFE-0004 (ADD-F013) | SAFE announcement is FDIR evidence only: its term is not adopted as a consensus epoch; the valid leader keeps role and authority; no election is caused | `mosaik_node.c`: higher-term adoption in `mosaik_on_rx()` excludes `MOSAIK_MSG_SAFE` (commit `ae9e408`); SAFE handler unchanged | TC-053 (RED at `58a1b5d`), TC-059, TC-057 | LOT4_MODE_SEMANTICS_REPORT.md §9, §11, §12 | IMPLEMENTED-SIM | Tested deterministic scenario: interruption 397 ms → 0 ms; not a general bound; host interpretation pending review |
| — | State × role legality (INV-MODE-LEGAL): INIT never LEADER, SAFE only FOLLOWER; emitted state byte always 0..3 | unchanged `enter_safe()`, `become_leader()`, encoder | TC-051, TC-057, TC-058 | LOT4_MODE_SEMANTICS_REPORT.md §5, §12 | IMPLEMENTED-SIM | Observed over the executed scenarios only |
| REQ-FUNC-0007 | Out-of-range state byte (4, 5) with valid CRC rejected by the decoder, no protocol effect | `mosaik_proto.c` decoder range check, `decode_errors` | TC-055 | LOT4_MODE_SEMANTICS_REPORT.md §12 | PARTIAL | Detection only; PROTO_ERROR reserved (LOT 3 D2) |
| — | Heartbeat state metadata (payload byte 3) is not protocol evidence; stale heartbeats with DEGRADED metadata rejected under Lot 2B rules | unchanged HEARTBEAT handler, `check_heartbeat_stale()` | TC-054, TC-056 | LOT4_MODE_SEMANTICS_REPORT.md §12 | IMPLEMENTED-SIM | Executable state values only |
| — | Determinism of the two RED scenarios | deterministic core and harness | TC-060 (and TC-049) | LOT4_MODE_SEMANTICS_REPORT.md §14 | IMPLEMENTED-SIM | Host model reproducibility only |

---

## 8. LOT 5 Specific Mapping (Autonomous Reconfiguration)

| ADD Ref | Feature | Implementation | Test | Evidence | Status | Limitation |
|---------|---------|----------------|------|----------|--------|------------|
| REQ-FUNC-0002 (ADD-F008) | Voting membership made explicit: committed membership mask with its own configuration epoch; quorum `popcount(mask)/2+1` | `mosaik_node.c`: `quorum_of()`, `mosaik_effective_members()`, committed/accepted state | TC-061, TC-064, TC-076 | LOT5_RECONFIGURATION_REPORT.md §9–§14 | IMPLEMENTED-SIM | Three-node HIL interpretation; ADD target voting membership still open (ADD-F008) |
| REQ-FUNC-0002 | Membership change as a distributed transaction: PROPOSE, ACCEPT, COMMIT, ANNOUNCE on identifiers `0x281`–`0x283` | `mosaik_proto.c` codec; `mosaik_node.c`: `handle_config()`, `mosaik_request_reconfiguration()` | TC-064, TC-072, TC-074, TC-076 | LOT5_RECONFIGURATION_REPORT.md §11 | IMPLEMENTED-SIM | No prepare-and-adopt recovery phase; a deadlocked epoch needs an operator |
| — (HIL-derived) | Joint authorisation: a quorum of the old **and** of the new configuration | `mosaik_node.c`: commit test in `handle_config()` | TC-065, TC-068, TC-074, TC-081 | LOT5_RECONFIGURATION_REPORT.md §7, §14 | IMPLEMENTED-SIM | Counterexample analysis on three nodes, not a proof |
| REQ-FUNC-0001 | Membership-aware elections and lease evidence | `mosaik_node.c`: membership gate in `mosaik_on_rx()`, `mosaik_has_quorum_ack_evidence()` | TC-069, TC-075, TC-087, TC-089 | LOT5_RECONFIGURATION_REPORT.md §16–§17 | IMPLEMENTED-SIM | Freshness boundary measured at 99/100 ms in simulated time |
| — (HIL-derived) | Removed-node exclusion without conflating removal with FDIR | membership gate; `config_commit()` | TC-066, TC-075, TC-083, TC-084 | LOT5_RECONFIGURATION_REPORT.md §18–§19 | IMPLEMENTED-SIM | A node that missed a whole epoch is not caught up automatically |
| — (HIL-derived) | Configuration replay protection, distinct from heartbeat replay | explicit epoch comparison in `handle_config()` | TC-067, TC-072, TC-080 | LOT5_RECONFIGURATION_REPORT.md §20 | IMPLEMENTED-SIM | Semantic only; no cryptographic membership authentication |
| — (no ADD persistence requirement claimed) | Host-model configuration store: committed configuration and acceptance binding | `mosaik_node.h`: `mosaik_config_store_t`; `mosaik_load_config_store()`, `config_persist()` | TC-077, TC-082, TC-084 | LOT5_RECONFIGURATION_REPORT.md §21 | IMPLEMENTED-SIM | Host model only; no NVM/FRAM, no power-loss atomicity; term, vote and SAFE remain unpersisted |
| REQ-FUNC-0007 | Configuration frame validation: stage 1..4, non-empty mask within three nodes | `mosaik_proto.c`: `mosaik_decode()` | TC-073 | LOT5_RECONFIGURATION_REPORT.md §11 | PARTIAL | Detection only; correlation_id still absent (ADD-F007) |
| — (HIL-derived) | INV-LEADER-UNIQUE preserved under reconfiguration faults | whole reconfiguration path | TC-079, TC-090 and the Phase-3 oracle | LOT5_RECONFIGURATION_REPORT.md §26–§28 | CbD (sim) | 6236 bounded schedules; bounded space, not exhaustive |

---

## 9. Evidence Classification Reference

| Code | Definition | Current Max Claim |
|------|------------|-------------------|
| **CbD** | Compliant-by-Design (architectural enforcement) | LOT 2A: vote-per-term, quorum, lease step-down; LOT 2B: term monotonicity, duplicate rejection |
| **CbA** | Compliant-by-Analysis (math/static analysis) | None yet |
| **CbT** | Compliant-by-Test on **physical hardware** | **NONE** — no hardware tests |
| **IMPLEMENTED-SIM** | Implemented + tested in host simulation | LOT 1, 2A, 2B, 2C, 2D, 3, 4, 5 |
| **PARTIAL** | Partially implemented/evidenced | REQ-FUNC-0007 (CRC yes, correlation_id no) |
| **DESIGN-ONLY** | Documented in architecture, not implemented | Most LOT 3+ requirements |
| **NOT-STARTED** | No work begun | Many |
| **HARDWARE-REQUIRED** | Cannot evidence without physical hardware | REQ-PERF-0004, REQ-ENV-0001, etc. |
| **TBC** | Conflict under investigation | ADD-F001, ADD-F002, ADD-F003 |

---

## 10. Mapping Maintenance Rules

1. Every new implementation must add/update a row in this table
2. Status changes require evidence reference
3. Gap/limitation must be explicit for non-CbT claims
4. Mapping reviewed at each Lot closure
5. Conflicts → `ADD-FINDINGS.md`