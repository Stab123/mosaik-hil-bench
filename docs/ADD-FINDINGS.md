# MOSAÏK ADD Findings Register

**Document:** MOSAIK-ADD-FIND-001  
**Issue:** 1.0 — 15 September 2026  
**Purpose:** Formal documentation of discrepancies, conflicts, and interpretations found in MOSAIK-ADD-0001

---

## 1. Finding Template

Each finding contains:
- **ID:** ADD-Fxxx
- **Source Section(s):** ADD section numbers
- **Issue:** Description of discrepancy/conflict
- **Impact:** Effect on implementation, verification, or acceptance
- **Proposed Implementation Interpretation:** How the repository will interpret until resolved
- **Status:** OPEN / RESOLVED
- **Resolution Rationale:** When closed, why

---

## 2. Findings

### ADD-F001: Requirement Namespace Mismatch (Section 10 vs Section 8.2)

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 10 (root requirements) and Section 8.2 (traceability matrix) |
| **Issue** | Two distinct requirement naming schemes exist:<br/>- Section 10: REQ-FUNC-xxxx, REQ-SAFE-xxxx, REQ-PERF-xxxx, REQ-ENV-xxxx, REQ-IF-xxxx, REQ-LOG-xxxx (4-digit suffix)<br/>- Section 8.2: REQ-FUN-xxx, REQ-SAF-xxx, REQ-PERF-xxx, REQ-ICD-xxx (3-digit suffix)<br/>Prefixes differ (FUNC vs FUN, SAFE vs SAF). Numbering schemes differ. No explicit mapping provided in ADD. |
| **Impact** | Cannot automatically trace root requirements to test cases. Implementation decisions may reference wrong namespace. Verification evidence may be attributed to wrong requirement. |
| **Proposed Interpretation** | Section 10 = ROOT SYSTEM REQUIREMENTS (controlling). Section 8.2 = DERIVED/DETAILED REQUIREMENTS (test traceability). Explicit mapping table maintained in `REQUIREMENTS-BASELINE.md` and `ADD-MAPPING.md`. All mappings marked PROVISIONAL until architecture review. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F002: Leader Uniqueness Scope — "Outside Declared Partition" vs "At All Times"

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 10: REQ-FUNC-0001 states "one and only one active leader **outside a declared partition**".<br/>ADD Safety language (implied in detailed requirements) may require leader uniqueness "at all times". |
| **Issue** | The phrase "outside a declared partition" implies that *during* a declared partition, multiple leaders might be permitted. However, safety-critical systems typically require leader uniqueness at all times. The detailed safety requirements (Section 8.2) do not explicitly qualify the uniqueness claim with "outside partition". |
| **Impact** | LOT 2A implementation enforces leader uniqueness **at all times** via lease expiry (INV-LEADER-UNIQUE: max 1 valid authority at every simulation point). This is stricter than the literal reading of REQ-FUNC-0001. If the ADD intent permits multiple leaders during partition, LOT 2A over-constrains. If the ADD intent is uniqueness at all times, the "outside declared partition" wording is misleading. |
| **Proposed Interpretation** | Implement uniqueness at all times (current LOT 2A behavior). Document the discrepancy. Request architecture clarification. The lease mechanism ensures that even during partition, only one node holds *valid* leadership authority. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F003: System Mode Terminology Differences

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 4 (system modes) vs Repository implementation vs ADD Section 10 requirements |
| **Issue** | Terminology inconsistencies:<br/>- ADD Section 4 modes: INIT, NOMINAL, ADAPTIVE, DEGRADED, SAFE, PGA (6 modes)<br/>- Repository (LOT 2A): INIT, NOMINAL, DEGRADED, SAFE (4 modes) — ADAPTIVE and PGA not implemented<br/>- ADD Section 10 REQ-FUNC-0005 references "DEGRADED mode"<br/>- Repository uses `MOSAIK_STATE_` prefix: INIT, NOMINAL, DEGRADED, SAFE |
| **Impact** | Mode-dependent requirements (e.g., REQ-FUNC-0005 "mission remains useful in DEGRADED") cannot be fully verified until ADAPTIVE and PGA are implemented. PGA (Pending Ground Arbitration) is the formal exit from SAFE in ADD; repository has no PGA — SAFE is terminal in simulation. |
| **Proposed Interpretation** | Repository modes are a subset. ADAPTIVE and PGA marked DESIGN-ONLY in `SOFTWARE-ARCHITECTURE.md`. SAFE latch is terminal in simulation (no ground arbitration path). Document as known gap. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F004: CAN-FD Bitrate Inconsistency

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 3 (Physical Layer) vs ADD Section 6/8.2 (ICD) |
| **Issue** | Section 3 states: "CAN FD, 500 kbit/s arbitration, 2 Mbit/s data". Section 6 (Protocol) and ICD may reference only 500 kbit/s. Current bench uses Classical CAN 2.0B at 500 kbit/s (ESP32 classic option) — no FD data phase. |
| **Impact** | Frame format in repository (11-bit ID, 8-byte DLC) is valid for both Classical CAN and CAN-FD. However, FD benefits (larger payload, faster data phase) not utilized. Timing analysis for REQ-PERF-0004 requires FD data phase. |
| **Proposed Interpretation** | Document current implementation as Classical CAN 2.0B compatible subset. FD data phase marked HARDWARE-REQUIRED (LOT 6). Frame format designed to be FD-compatible (8-byte DLC ≤ 64 bytes FD max). |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F005: Environmental Requirements — Bench vs Flight

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 7 (Environmental) vs Repository limitations |
| **Issue** | ADD specifies LEO environment: thermal cycling, vibration, radiation. Repository uses commercial development boards (ESP32) at ambient conditions. No environmental qualification. |
| **Impact** | All CbT claims require flight-representative hardware (LOT 13) and environmental campaign (LOT 14). Current IMPLEMENTED-SIM evidence does not satisfy REQ-ENV-0001. |
| **Proposed Interpretation** | Explicitly state in all evidence: "Host simulation only — no environmental qualification." Bench is logic verification (TRL 3), not environmental. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F006: Test ID / Requirement Mapping Inconsistencies

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 8.2 (traceability matrix) vs Repository test cases |
| **Issue** | ADD test case IDs (e.g., F-01, S-01, P-01, I-01) do not match repository TC-xxx IDs. Some ADD test cases combine multiple requirements; repository tests are granular (one requirement per test where possible). ADD matrix may have gaps or duplications not yet fully transcribed. |
| **Impact** | Traceability requires explicit mapping table (`TRACEABILITY.md`). Cannot rely on ID matching. |
| **Proposed Interpretation** | Maintain explicit mapping in `TRACEABILITY.md` Section 4. Repository TC-xxx IDs preserved. ADD test case IDs mapped provisionally. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F007: Correlation_ID in Critical Messages

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 10 REQ-FUNC-0007: "Critical CAN messages include CRC and correlation_id" |
| **Issue** | Current protocol (PROTOCOL.md, `mosaik_proto.c`) implements CRC-8 (SAE-J1850) but **no correlation_id field** in the 8-byte payload. Payload bytes 0-6 are fully allocated (version, src, role, state, term[2], arg). No room for correlation_id without frame format change. |
| **Impact** | REQ-FUNC-0007 partially implemented (CRC yes, correlation_id no). Traceability (REQ-LOG-0002) requires correlation_id for timeline reconstruction. |
| **Proposed Interpretation** | Mark as PARTIAL in `ADD-MAPPING.md`. Correlation_id requires either:<br/>a) Extended CAN-FD frame (64 bytes) — LOT 6<br/>b) Separate correlation message — architecture decision needed<br/>Document as gap. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F008: Quorum Definition — Cluster Size Dependency

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 10 REQ-FUNC-0002: "major reconfiguration requires formal majority quorum" vs Repository 3-node subset |
| **Issue** | ADD defines 6-node cluster (3 EN + CN + COMN + GSE). Quorum = 4. Repository tests 3-node subset with quorum = 2. REQ-FUNC-0002 verified only for 3-node case. |
| **Impact** | Quorum logic (`floor(n/2)+1`) is generalizable, but not tested for 6-node. Failure modes differ (e.g., 2-node loss in 6-node = 4 remaining = quorum; 2-node loss in 3-node = 1 remaining = no quorum). |
| **Proposed Interpretation** | Document as PARTIAL. 3-node subset verified. 6-node quorum behavior DESIGN-ONLY until LOT 8. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F009: SAFE Exit — Ground Arbitration vs Terminal Latch

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 4: PGA (Pending Ground Arbitration) is exit from SAFE. Repository: SAFE is terminal (no exit implemented). |
| **Issue** | ADD requires ground arbitration to exit SAFE. Repository latches SAFE permanently (simulation only). No GSE/COMN path implemented. |
| **Impact** | REQ-SAFE-0003 (SAFE irreversible without ground arbitration) implemented as "irreversible period" in simulation. PGA not tested. |
| **Proposed Interpretation** | Software latch implemented. PGA marked DESIGN-ONLY (LOT 3, 8). Document in limitations. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F010: Heartbeat Period Tolerance

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 10 REQ-FUNC-0003: "heartbeat nominal period 100 ms" — no tolerance specified. REQ-PERF-0003: "100 ms ± tolerance" — tolerance value not given. |
| **Issue** | Repository uses exactly 100 ms (configured). No jitter/tolerance modeled. Election timeout (300-500 ms) implies 3-5 missed heartbeats tolerance. |
| **Impact** | Timing margins not fully specified. Hardware clock drift not characterized (LOT 14). |
| **Proposed Interpretation** | Nominal 100 ms implemented. Tolerance derived from election timeout margins (3-5 periods). Document as TBC — requires ADD clarification. |
| **Status** | OPEN |
| **Resolution** | — |

---

## 3. Finding Status Summary

| ID | Title | Status |
|----|-------|--------|
| ADD-F001 | Requirement Namespace Mismatch | OPEN |
| ADD-F002 | Leader Uniqueness Scope | OPEN |
| ADD-F003 | System Mode Terminology | OPEN |
| ADD-F004 | CAN-FD Bitrate Inconsistency | OPEN |
| ADD-F005 | Environmental Requirements | OPEN |
| ADD-F006 | Test ID Mapping Inconsistencies | OPEN |
| ADD-F007 | Correlation_ID Missing | OPEN |
| ADD-F008 | Quorum Definition / Cluster Size | OPEN |
| ADD-F009 | SAFE Exit / PGA | OPEN |
| ADD-F010 | Heartbeat Period Tolerance | OPEN |

**Total:** 10 findings, all OPEN.  
**Next review:** LOT 0 closure / architecture review board.