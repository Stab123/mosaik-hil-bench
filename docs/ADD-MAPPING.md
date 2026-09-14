# MOSAÏK ADD-to-Implementation Mapping

**Document:** MOSAIK-ADD-MAP-001  
**Issue:** 1.0 — 15 September 2026  
**Purpose:** Master implementation map linking ADD requirements to architecture, code, tests, and evidence

---

## 1. Mapping Table Columns

| Column | Description |
|--------|-------------|
| **ADD Section** | Source section in MOSAIK-ADD-0001 |
| **Requirement ID** | ROOT (Section 10) or DERIVED (Section 8.2) |
| **Requirement Summary** | Normalized statement |
| **Target Component** | Architecture element (layer/module/task) |
| **GitHub Implementation** | File(s) or "NOT IMPLEMENTED" |
| **Test** | Repository test case(s) |
| **Evidence** | Evidence reference / classification |
| **LOT** | Development Lot |
| **Status** | IMPLEMENTED-SIM / PARTIAL / DESIGN-ONLY / NOT-STARTED / HARDWARE-REQUIRED / TBC |
| **Gap / Limitation** | Known limitations, simulation-only, missing scope |

---

## 2. Initial Mapping (Conservative)

| ADD Section | Requirement ID | Requirement Summary | Target Component | GitHub Implementation | Test | Evidence | LOT | Status | Gap / Limitation |
|-------------|----------------|---------------------|------------------|----------------------|------|----------|-----|--------|------------------|
| 10 / 8.2 | REQ-FUNC-0001 / REQ-FUN-001 | One active leader outside declared partition | L4: svc_mosaik_proto, L5: Cluster_Task | `firmware/core/mosaik_node.c/h` | TC-001, TC-002, TC-007 | LOT2A_LEADER_LEASE_REPORT.md | 2A | PARTIAL | Host sim only; 3-node subset; "outside declared partition" vs "at all times" (ADD-F002) |
| 10 / 8.2 | REQ-FUNC-0002 / REQ-FUN-002 | Quorum required for reconfiguration | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` (quorum func) | TC-005, TC-007 | LOT2A_LEADER_LEASE_REPORT.md | 2A | IMPLEMENTED-SIM | 3-node quorum=2 only; not universal proof |
| 10 / 8.2 | REQ-FUNC-0003 / REQ-FUN-003 | Heartbeat nominal period 100 ms | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` (heartbeat_period_ms=100) | TC-001, TC-006 | PROTOCOL.md §6 | 1 | IMPLEMENTED-SIM | Period configured; not measured on hardware |
| 10 / 8.2 | REQ-FUNC-0004 / REQ-PERF-001 | Leader loss detection & re-election < 1 s | L4: svc_mosaik_proto, L5: Cluster_Task | `firmware/core/mosaik_node.c` (election timeout) | TC-003 | LOT2A_LEADER_LEASE_REPORT.md | 2A | IMPLEMENTED-SIM | Simulated time (452 ms); no hardware measurement |
| 10 | REQ-FUNC-0005 | Mission useful after 1 EN loss in DEGRADED | L6: mission modules, L5: Cluster_Task | NOT IMPLEMENTED | — | — | 8 | DESIGN-ONLY | Requires 6-node architecture |
| 10 | REQ-FUNC-0006 | Mode transitions & critical decisions logged | L4: svc_logger, L5: Logger_Task | NOT IMPLEMENTED | — | — | 7 | DESIGN-ONLY | Logger service not implemented |
| 10 / 8.2 | REQ-FUNC-0007 / REQ-ICD-001, REQ-ICD-002 | Critical CAN msgs: CRC + correlation_id | L3: drv_canfd, L4: svc_mosaik_proto | `firmware/core/mosaik_proto.c` (CRC-8) | TC-006 | PROTOCOL.md §5 | 1 | PARTIAL | CRC-8 implemented; correlation_id NOT implemented |
| 10 | REQ-FUNC-0008 | CN maintains cluster blackbox | L4: svc_logger, L5: Logger_Task | NOT IMPLEMENTED | — | — | 8 | DESIGN-ONLY | CN node not implemented |
| 10 | REQ-FUNC-0009 | COMN exports logs to GSE | L4: svc_tm, L5: Companion_Task | NOT IMPLEMENTED | — | — | 8 | DESIGN-ONLY | COMN node not implemented |
| 10 | REQ-FUNC-0010 | Complete event timeline reconstruction | L4: svc_logger, L5: Logger_Task | NOT IMPLEMENTED | — | — | 8 | DESIGN-ONLY | Requires blackbox + GSE |
| 10 / 8.2 | REQ-SAFE-0002 / REQ-SAF-001 | Split-brain detection & latch < 10 ms | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` (enter_safe) | TC-004 | LOT2A_LEADER_LEASE_REPORT.md | 2A | IMPLEMENTED-SIM | Host receive path (0 ms sim); no hardware measurement |
| 10 / 8.2 | REQ-SAFE-0003 / REQ-SAF-002 | SAFE irreversible without ground arbitration | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` (SAFE latch) | TC-004, TC-005 | LOT2A_LEADER_LEASE_REPORT.md | 2A | IMPLEMENTED-SIM | Software latch only; no PGA discretes |
| 10 / 8.2 | REQ-SAFE-0004 / REQ-SAF-003 | DEGRADED on peer SAFE | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` (SAFE msg handler) | TC-004 | LOT2A_LEADER_LEASE_REPORT.md | 2A | IMPLEMENTED-SIM | State transition implemented |
| 10 / 8.2 | REQ-PERF-0001 / REQ-PERF-001 | Election completion < 1000 ms | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` | TC-003 | LOT2A_LEADER_LEASE_REPORT.md | 2A | IMPLEMENTED-SIM | Simulated time only |
| 10 / 8.2 | REQ-PERF-0002 / REQ-PERF-002 | SAFE latch < 10 ms | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` | TC-004 | LOT2A_LEADER_LEASE_REPORT.md | 2A | IMPLEMENTED-SIM | Host receive path |
| 10 | REQ-PERF-0003 | Heartbeat period 100 ms ± tolerance | L4: svc_mosaik_proto | `firmware/core/mosaik_node.c` | TC-001 | PROTOCOL.md §6 | 1 | IMPLEMENTED-SIM | Configured; not measured |
| 10 | REQ-PERF-0004 | CAN-FD 500k/2M bit/s | L3: drv_canfd | NOT IMPLEMENTED | — | — | 6 | HARDWARE-REQUIRED | Requires STM32H743 FDCAN |
| 10 | REQ-ENV-0001 | LEO thermal/vibration/radiation | L0-L6: all | NOT IMPLEMENTED | — | — | 13-14 | HARDWARE-REQUIRED | Flight hardware required |
| 10 | REQ-IF-0001 | CAN-FD primary backbone ICD | L3: drv_canfd, L4: svc_mosaik_proto | `firmware/core/mosaik_proto.c/h` (frame format) | TC-006 | PROTOCOL.md | 1 | PARTIAL | Frame format defined; FD not validated |
| 10 | REQ-IF-0002 | Ethernet secondary | L3: drv_eth | NOT IMPLEMENTED | — | — | 6 | DESIGN-ONLY | |
| 10 | REQ-IF-0003 | Wired safety discretes | L3: drv_gpio, L5: Safety_Task | NOT IMPLEMENTED | — | — | 3, 11 | DESIGN-ONLY | Software SAFE only |
| 10 | REQ-IF-0004 | 24V distributed power | L2: BSP power | NOT IMPLEMENTED | — | — | 13 | NOT-STARTED | |
| 10 | REQ-LOG-0001 | Mode transitions logged | L4: svc_logger | NOT IMPLEMENTED | — | — | 7 | DESIGN-ONLY | |
| 10 | REQ-LOG-0002 | Critical decisions logged w/ correlation_id | L4: svc_logger | NOT IMPLEMENTED | — | — | 7 | DESIGN-ONLY | |
| 10 | REQ-LOG-0003 | Blackbox survives restart | L4: svc_fs, L5: Logger_Task | NOT IMPLEMENTED | — | — | 8 | DESIGN-ONLY | |

---

## 3. LOT 2A Specific Mapping (Current Baseline)

| ADD Ref | LOT 2A Feature | Implementation | Test | Evidence | Status | Limitation |
|---------|----------------|----------------|------|----------|--------|------------|
| REQ-FUNC-0001 | 500 ms leadership lease | `mosaik_node.c`: lease_expiry_ms, step-down on expiry | TC-007 | LOT2A_LEADER_LEASE_REPORT.md | IMPLEMENTED-SIM | Host sim; deterministic; 3-node 2+1 only |
| REQ-FUNC-0001 | Valid authority predicate | `mosaik_node.h/c`: `mosaik_has_valid_leadership_authority()` | TC-007 | LOT2A_LEADER_LEASE_REPORT.md | IMPLEMENTED-SIM | Distinguishes ROLE_LEADER from authority |
| REQ-FUNC-0002 | Lease renewal on quorum contact | `test_mosaik.c`: lease renewal on HB delivery | TC-007 | LOT2A_LEADER_LEASE_REPORT.md | IMPLEMENTED-SIM | Harness-mediated renewal |
| REQ-FUNC-0004 | Follower election timeout aligned to lease | `mosaik_node.c`: deadline = HB + lease + jitter | TC-007 | LOT2A_LEADER_LEASE_REPORT.md | IMPLEMENTED-SIM | Prevents premature challenge |
| — | INV-LEADER-UNIQUE invariant | TC-007 checks valid_leader_count ≤ 1 at every step | TC-007 | LOT2A_LEADER_LEASE_REPORT.md | CbD (sim) | Observed at simulation points only |

---

## 4. Evidence Classification Reference

| Code | Definition | Current Max Claim |
|------|------------|-------------------|
| **CbD** | Compliant-by-Design (architectural enforcement) | LOT 2A: vote-per-term, quorum, lease step-down |
| **CbA** | Compliant-by-Analysis (math/static analysis) | None yet |
| **CbT** | Compliant-by-Test on **physical hardware** | **NONE** — no hardware tests |
| **IMPLEMENTED-SIM** | Implemented + tested in host simulation | LOT 1, 2A |
| **PARTIAL** | Partially implemented/evidenced | REQ-FUNC-0007 (CRC yes, correlation_id no) |
| **DESIGN-ONLY** | Documented in architecture, not implemented | Most LOT 3+ requirements |
| **NOT-STARTED** | No work begun | Many |
| **HARDWARE-REQUIRED** | Cannot evidence without physical hardware | REQ-PERF-0004, REQ-ENV-0001, etc. |
| **TBC** | Conflict under investigation | ADD-F001, ADD-F002, ADD-F003 |

---

## 5. Mapping Maintenance Rules

1. Every new implementation must add/update a row in this table
2. Status changes require evidence reference
3. Gap/limitation must be explicit for non-CbT claims
4. Mapping reviewed at each Lot closure
5. Conflicts → `ADD-FINDINGS.md`