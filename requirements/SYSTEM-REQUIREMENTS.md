# MOSAÏK System Requirements Baseline

**Document:** MOSAIK-SYSREQ-001  
**Issue:** 1.1 — 15 September 2026  
**Source:** MOSAIK-ADD-0001, Issue 1 / Rev 1, dated 24 April 2026  
**Scope:** Structured transcription of ADD requirements relevant to software/system implementation

---

## 1. Requirement Namespaces

The ADD contains at least two requirement naming schemes:

| Namespace | Source | Prefix Examples | Role |
|-----------|--------|-----------------|------|
| **ROOT SYSTEM REQUIREMENTS** | ADD Section 10 | REQ-FUNC-xxxx, REQ-SAFE-xxxx, REQ-PERF-xxxx, REQ-ENV-xxxx, REQ-IF-xxxx, REQ-LOG-xxxx | Top-level system requirements |
| **DERIVED / DETAILED REQUIREMENTS** | ADD Section 82 (traceability matrix) | REQ-FUN-xxx, REQ-SAF-xxx, REQ-PERF-xxx, REQ-ICD-xxx | Detailed traceability to tests/implementation |

**Policy:** Until formally resolved, Section 10 requirements are ROOT. Section 82 requirements are DERIVED. Mappings must be explicit. Conflicts documented in `ADD-FINDINGS.md`. No value silently changed.

---

## 2. ROOT SYSTEM REQUIREMENTS (ADD Section 10)

### 2.1 Functional Requirements (REQ-FUNC)

| ID | Statement (normalized) | Required Verification Method | Impl Status | Evidence Status | Notes |
|----|------------------------|---------------------|-------------|-----------------|-------|
| REQ-FUNC-0001 | One and only one active leader outside a declared partition | CbD, CbT | PARTIAL (IMPLEMENTED-SIM) | LOT2A: TC-001, TC-002, TC-007 | "Outside declared partition" — see ADD-F002 |
| REQ-FUNC-0002 | Major reconfiguration requires formal majority quorum | CbD, CbT | PARTIAL (IMPLEMENTED-SIM) | LOT2A: quorum=2 for 3-node | 3-node subset only; GSE voting role TBC — see ADD-F008 |
| REQ-FUNC-0003 | Heartbeat nominal period 100 ms | CbT | IMPLEMENTED-SIM | LOT1: TC-006 (period), TC-001 | Host sim only; derived tolerance ±2% — see ADD-F010 |
| REQ-FUNC-0004 | Leader loss detection and re-election < 1 s | CbT | IMPLEMENTED-SIM | LOT2A: TC-003 (452 ms sim); LOT2D: TC-028 (628 ms sim), TC-031 (680 ms sim, after one split vote) | Simulated time only; not a worst-case bound |
| REQ-FUNC-0005 | Mission remains useful after loss of one EN in DEGRADED mode | CbA, CbT | DESIGN-ONLY | — | 6-node architecture required |
| REQ-FUNC-0006 | Mode transitions and critical decisions must generate logged events | CbD, CbT | DESIGN-ONLY | — | Logger service LOT 7 |
| REQ-FUNC-0007 | Critical CAN messages include CRC and correlation_id | CbD, CbT | PARTIAL | LOT1: TC-006 (CRC) | correlation_id not yet implemented — see ADD-F007 |
| REQ-FUNC-0008 | CN maintains an authoritative cluster blackbox | CbD, CbT | DESIGN-ONLY | — | CN node LOT 8 |
| REQ-FUNC-0009 | COMN exports consolidated logs to GSE | CbD, CbT | DESIGN-ONLY | — | COMN node LOT 8 |
| REQ-FUNC-0010 | Complete event timeline reconstruction | CbA, CbT | DESIGN-ONLY | — | Requires blackbox + GSE |

### 2.2 Safety Requirements (REQ-SAFE)

| ID | Statement (normalized) | Required Verification Method | Impl Status | Evidence Status | Notes |
|----|------------------------|---------------------|-------------|-----------------|-------|
| REQ-SAFE-0001 | No single point of failure shall cause loss of mission | CbA, CbT | DESIGN-ONLY | — | Architecture-level |
| REQ-SAFE-0002 | Split-brain shall be detected and latched within 10 ms | CbT | IMPLEMENTED-SIM | LOT2A: TC-004 (0 ms sim) | Host sim detection path |
| REQ-SAFE-0003 | SAFE mode shall be irreversible without ground arbitration | CbD | IMPLEMENTED-SIM | LOT2A: TC-004, TC-005 | Latch implemented; PGA not implemented — see ADD-F009 |
| REQ-SAFE-0004 | Cluster shall enter DEGRADED when peer enters SAFE | CbD | IMPLEMENTED-SIM | LOT2A: TC-004 | State transition implemented |

### 2.3 Performance Requirements (REQ-PERF)

| ID | Statement (normalized) | Required Verification Method | Impl Status | Evidence Status | Notes |
|----|------------------------|---------------------|-------------|-----------------|-------|
| REQ-PERF-0001 | Election completion < 1000 ms after leader loss | CbT | IMPLEMENTED-SIM | LOT2A: TC-003; LOT2D: TC-028, TC-031 | Simulated time; deterministic host model only |
| REQ-PERF-0002 | SAFE latch latency < 10 ms from detection | CbT | IMPLEMENTED-SIM | LOT2A: TC-004 | Host receive path |
| REQ-PERF-0003 | Heartbeat period 100 ms ± tolerance (derived: 10 Hz ±2%) | CbT | IMPLEMENTED-SIM | LOT1: TC-001 | Period configured; tolerance traceability — see ADD-F010 |
| REQ-PERF-0004 | CAN-FD arbitration 500 kbit/s, data 2 Mbit/s | CbT | HARDWARE-REQUIRED | — | LOT 6 |

### 2.4 Environmental Requirements (REQ-ENV)

| ID | Statement (normalized) | Required Verification Method | Impl Status | Evidence Status | Notes |
|----|------------------------|---------------------|-------------|-----------------|-------|
| REQ-ENV-0001 | Operation at LEO thermal/vibration/radiation | CbT | HARDWARE-REQUIRED | — | LOT 13–14; no environmental qualification — see ADD-F005 |
| REQ-ENV-0002 | Commercial development board qualification | CbA | NOT-STARTED | — | Bench only |

### 2.5 Interface Requirements (REQ-IF)

| ID | Statement (normalized) | Required Verification Method | Impl Status | Evidence Status | Notes |
|----|------------------------|---------------------|-------------|-----------------|-------|
| REQ-IF-0001 | CAN-FD primary backbone ICD | CbD, CbT | DESIGN-ONLY | — | LOT 6 |
| REQ-IF-0002 | Ethernet secondary data network | CbD, CbT | NOT-STARTED | — | LOT 6 |
| REQ-IF-0003 | Wired safety discretes (SAFE_ASSERT, E_STOP, NODE_FAULT_N, POWER_EN, LEADER_ASSERT) | CbD, CbT | DESIGN-ONLY | — | LOT 3, 11 |
| REQ-IF-0004 | 24 V distributed power interface | CbD, CbT | NOT-STARTED | — | LOT 13 |

### 2.6 Logging Requirements (REQ-LOG)

| ID | Statement (normalized) | Required Verification Method | Impl Status | Evidence Status | Notes |
|----|------------------------|---------------------|-------------|-----------------|-------|
| REQ-LOG-0001 | All mode transitions logged with timestamp | CbD, CbT | DESIGN-ONLY | — | LOT 7 |
| REQ-LOG-0002 | All critical decisions logged with correlation_id | CbD, CbT | DESIGN-ONLY | — | LOT 7; correlation_id not yet in wire format — see ADD-F007 |
| REQ-LOG-0003 | Blackbox survives node restart | CbD, CbT | DESIGN-ONLY | — | LOT 8 |

---

## 3. DERIVED / DETAILED REQUIREMENTS (ADD Section 82)

| ADD Section 82 ID | Description | Maps to ROOT | Notes |
|--------------------|-------------|--------------|-------|
| REQ-FUN-001 | Leader election | REQ-FUNC-0001, REQ-FUNC-0004 | |
| REQ-FUN-002 | Quorum enforcement | REQ-FUNC-0002 | |
| REQ-FUN-003 | Heartbeat generation | REQ-FUNC-0003 | |
| REQ-FUN-006 | Heartbeat 10 Hz ±2% | REQ-FUNC-0003, REQ-PERF-0003 | Tolerance traceability — see ADD-F010 |
| REQ-SAF-001 | Split-brain detection | REQ-SAFE-0002 | |
| REQ-SAF-002 | SAFE latching | REQ-SAFE-0003 | |
| REQ-SAF-003 | DEGRADED on peer SAFE | REQ-SAFE-0004 | |
| REQ-PERF-001 | Election < 1 s | REQ-PERF-0001 | |
| REQ-PERF-002 | SAFE < 10 ms | REQ-PERF-0002 | |
| REQ-ICD-001 | CAN-FD frame format | REQ-IF-0001 | |
| REQ-ICD-002 | CRC-8/SAE-J1850 | REQ-FUNC-0007 | |

**Status:** Mapping incomplete — full Section 82 transcription pending. See ADD-F001.

---

## 4. Required Verification Method Legend

- **CbD** = Compliant-by-Design (architectural enforcement, e.g., one vote per term)
- **CbA** = Compliant-by-Analysis (mathematical proof, static analysis)
- **CbT** = Compliant-by-Test on **physical hardware** (measured). As a *required* method it states what the requirement will need; it is not an achieved result. No CbT evidence exists in this repository (host simulation only).
- **IMPLEMENTED-SIM** = Implemented and tested in host simulation only
- **PARTIAL** = Partially implemented / partially evidenced
- **DESIGN-ONLY** = Documented in architecture, not implemented
- **NOT-STARTED** = No work begun
- **HARDWARE-REQUIRED** = Cannot be evidenced without physical hardware
- **TBC** = To Be Confirmed / conflict under investigation

---

## 5. Implementation Status Summary

| Category | Total | IMPLEMENTED-SIM | PARTIAL | DESIGN-ONLY | NOT-STARTED | HARDWARE-REQUIRED |
|----------|-------|-----------------|---------|-------------|-------------|-------------------|
| REQ-FUNC | 10 | 2 | 2 | 4 | 1 | 1 |
| REQ-SAFE | 4 | 2 | 0 | 1 | 1 | 0 |
| REQ-PERF | 4 | 3 | 0 | 0 | 0 | 1 |
| REQ-ENV | 2 | 0 | 0 | 0 | 1 | 1 |
| REQ-IF | 4 | 0 | 0 | 1 | 3 | 0 |
| REQ-LOG | 3 | 0 | 0 | 0 | 3 | 0 |
| **TOTAL** | **27** | **7** | **2** | **6** | **8** | **3** |

*Counts based on initial transcription; will be updated as mapping completes.*