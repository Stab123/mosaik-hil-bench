# MOSAÏK Bidirectional Traceability

**Document:** MOSAIK-TRACE-001  
**Issue:** 1.1 — 15 September 2026  
**Purpose:** Bidirectional traceability matrix linking ADD requirements ↔ derived requirements ↔ architecture ↔ implementation ↔ test ↔ result ↔ evidence

---

## 1. Traceability Directions

### Forward Trace (Requirements → Evidence)
```
ADD Requirement (Section 10)
    │
    ├──→ Derived Requirement (Section 82)
    │       │
    │       └──→ Architecture Element (SYSTEM-ARCHITECTURE.md / SOFTWARE-ARCHITECTURE.md)
    │               │
    │               └──→ Implementation (GitHub file/function)
    │                       │
    │                       └──→ Test Case (TC-xxx)
    │                               │
    │                               └──→ Test Result (PASS/FAIL + metrics)
    │                                       │
    │                                       └──→ Evidence (Report / Classification)
```

### Reverse Trace (Evidence → Requirements)
```
Test Result / Evidence
    │
    └──→ Test Case (TC-xxx)
            │
            └──→ Implementation (file:line)
                    │
                    └──→ Architecture Element
                            │
                            └──→ Derived Requirement (Section 82)
                                    │
                                    └──→ ADD Root Requirement (Section 10)
```

---

## 2. Existing Test Cases Traceability (TC-001 through TC-007)

### TC-001: Single Leader Elected and Held

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto/L5:Cluster_Task → `mosaik_node.c:become_leader()` → TC-001 → PASS (1 leader at 2000ms) → IMPLEMENTED-SIM |
| **Reverse** | TC-001 → `mosaik_node.c:100-108` → Cluster_Task logic → REQ-FUN-001 → REQ-FUNC-0001 |

### TC-002: No Split-Brain Over 20s

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001, REQ-SAF-001 → L4:svc_mosaik_proto → `mosaik_node.c:171-175` (split-brain detection) → TC-002 → PASS (max 1 leader) → IMPLEMENTED-SIM |
| **Reverse** | TC-002 → `mosaik_node.c:171-175` → split-brain SAFE → REQ-SAF-001 → REQ-SAFE-0002 |

### TC-003: Failover After Leader Power Loss

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0004 → REQ-PERF-001 → L4:svc_mosaik_proto → `mosaik_node.c:election_timeout()` + follower logic → TC-003 → PASS (452 ms sim) → IMPLEMENTED-SIM |
| **Reverse** | TC-003 → `mosaik_node.c:34-39, 85-98` → election timeout + start_election → REQ-PERF-001 → REQ-FUNC-0004 |

### TC-004: SAFE Latched on Same-Term Dual Leader

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-SAFE-0002 → REQ-SAF-001, REQ-PERF-002 → L4:svc_mosaik_proto → `mosaik_node.c:62-74` (enter_safe) + `171-175` → TC-004 → PASS (0 ms detection-to-SAFE sim) → IMPLEMENTED-SIM |
| **Reverse** | TC-004 → `mosaik_node.c:62-74` → enter_safe() → REQ-SAF-001 → REQ-SAFE-0002 |

### TC-005: Lone Node Cannot Self-Appoint, Latches SAFE

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0002, REQ-SAFE-0003 → REQ-FUN-002, REQ-SAF-002 → L4:svc_mosaik_proto → `mosaik_node.c:243-248` (candidate failures → SAFE) → TC-005 → PASS (SAFE no-quorum) → IMPLEMENTED-SIM |
| **Reverse** | TC-005 → `mosaik_node.c:243-248` → max_failed_elections → REQ-SAF-002 → REQ-SAFE-0003 |

### TC-006: Frame Codec Round-Trip and CRC Rejection

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0007 → REQ-ICD-002 → L4:svc_mosaik_proto → `mosaik_proto.c:mosaik_encode/decode` + `mosaik_crc8` → TC-006 → PASS (8/8 corruptions rejected) → PARTIAL (correlation_id missing) |
| **Reverse** | TC-006 → `mosaik_proto.c:14-29, 56-112` → CRC-8 SAE-J1850 → REQ-ICD-002 → REQ-FUNC-0007 |

### TC-007: 2+1 Partition Lease Expiry Enforces Unique Valid Leader

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001, REQ-FUN-002 → L4:svc_mosaik_proto (lease) → `mosaik_node.c:lease_expiry_ms, mosaik_has_valid_leadership_authority()` + `test_mosaik.c:connectivity[][]` → TC-007 → PASS (maximum concurrent valid leadership authorities observed at the defined deterministic simulation observation points: 1) → IMPLEMENTED-SIM |
| **Reverse** | TC-007 → `mosaik_node.c:106, 239-249, 270-276` + `test_mosaik.c:bus_set_partition_2plus1` → lease expiry + valid authority → REQ-FUN-001/002 → REQ-FUNC-0001 |

### TC-008: Old Term Heartbeat Rejected

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:check_heartbeat_stale()` (term monotonicity) → TC-008 → PASS (stale heartbeat rejected, term maintained) → IMPLEMENTED-SIM |
| **Reverse** | TC-008 → `mosaik_node.c:88-120` → term monotonicity check → REQ-FUN-001 → REQ-FUNC-0001 |

### TC-009: Replay After Lease Expiry Rejected

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:check_heartbeat_stale()` (lease expiry check) → TC-009 → PASS (expired authority stays expired) → IMPLEMENTED-SIM |
| **Reverse** | TC-009 → `mosaik_node.c:88-120, 321-322` → lease expiry check + step-down → REQ-FUN-001 → REQ-FUNC-0001 |

### TC-010: Delayed Old Leader After Partition Recovery Rejected

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001, REQ-FUN-002 → L4:svc_mosaik_proto → `mosaik_node.c:check_heartbeat_stale()` (stale authority via last_hb_term) + `test_mosaik.c:partition/heal` → TC-010 → PASS (newer term remains authoritative) → IMPLEMENTED-SIM |
| **Reverse** | TC-010 → `mosaik_node.c:88-120, 109-116` → stale authority check via last_hb_term → REQ-FUN-001/002 → REQ-FUNC-0001 |

### TC-011: Duplicate Heartbeat Idempotence

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:check_heartbeat_stale()` (duplicate seq check via last_hb_seq) → TC-011 → PASS (duplicate delivery idempotent) → IMPLEMENTED-SIM |
| **Reverse** | TC-011 → `mosaik_node.c:88-120, 105-108` → duplicate sequence check → REQ-FUN-001 → REQ-FUNC-0001 |

### TC-012: Stale Election/Vote Traffic Rejected

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:VOTE_REQ handler` (term monotonicity) → TC-012 → PASS (stale vote request rejected) → IMPLEMENTED-SIM |
| **Reverse** | TC-012 → `mosaik_node.c:269-272` → term check in VOTE_REQ → REQ-FUN-001 → REQ-FUNC-0001 |

---

## 3. Bidirectional Matrix Summary

| Test Case | ADD Root Req | ADD Derived Req | Architecture Element | Implementation | Result | Evidence Class |
|-----------|--------------|-----------------|---------------------|----------------|--------|----------------|
| TC-001 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:become_leader` | PASS | IMPLEMENTED-SIM |
| TC-002 | REQ-FUNC-0001 | REQ-FUN-001, REQ-SAF-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:split_brain_check` | PASS | IMPLEMENTED-SIM |
| TC-003 | REQ-FUNC-0004 | REQ-PERF-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:election_timeout` | PASS (452ms) | IMPLEMENTED-SIM |
| TC-004 | REQ-SAFE-0002 | REQ-SAF-001, REQ-PERF-002 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:enter_safe` | PASS (0ms) | IMPLEMENTED-SIM |
| TC-005 | REQ-FUNC-0002, REQ-SAFE-0003 | REQ-FUN-002, REQ-SAF-002 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:candidate_failures` | PASS | IMPLEMENTED-SIM |
| TC-006 | REQ-FUNC-0007 | REQ-ICD-002 | svc_mosaik_proto | `mosaik_proto.c:crc8, encode/decode` | PASS | PARTIAL |
| TC-007 | REQ-FUNC-0001 | REQ-FUN-001, REQ-FUN-002 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:lease` + `test_mosaik.c:partition` | PASS (max concurrent valid authorities observed at simulation points: 1) | IMPLEMENTED-SIM |
| TC-008 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:check_heartbeat_stale` | PASS | IMPLEMENTED-SIM |
| TC-009 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:check_heartbeat_stale` + lease check | PASS | IMPLEMENTED-SIM |
| TC-010 | REQ-FUNC-0001 | REQ-FUN-001, REQ-FUN-002 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:check_heartbeat_stale` + partition | PASS | IMPLEMENTED-SIM |
| TC-011 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:check_heartbeat_stale` | PASS | IMPLEMENTED-SIM |
| TC-012 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:VOTE_REQ handler` | PASS | IMPLEMENTED-SIM |

---

## 4. Repository TC-xxx ↔ ADD Test Case Mapping

| Repo Test ID | ADD Section 82 Test Case | Mapping Status | Notes |
|--------------|---------------------------|----------------|-------|
| TC-001 | F-01 (leader election) | PROVISIONAL | Naming differs |
| TC-002 | F-02 (split-brain absence) | PROVISIONAL | 20s sim vs ADD campaign |
| TC-003 | P-01 (failover latency) | PROVISIONAL | Sim vs hardware |
| TC-004 | S-01 (SAFE latch) | PROVISIONAL | Receive path only |
| TC-005 | F-03 (no quorum SAFE) | PROVISIONAL | 3-node subset |
| TC-006 | I-01 (codec/CRC) | PROVISIONAL | Correlation_id gap |
| TC-007 | F-04 (partition lease) | PROVISIONAL | LOT 2A specific; PARTIAL evidence for ADD R-04 — exercises one deterministic 3-node 2+1 partition scenario |
| TC-008 | R-01 (stale term rejection) | PROVISIONAL | LOT 2B specific; semantic rejection only |
| TC-009 | R-02 (expired lease replay) | PROVISIONAL | LOT 2B specific; requires isolation |
| TC-010 | R-03 (partition recovery safety) | PROVISIONAL | LOT 2B specific; 2+1 partition |
| TC-011 | R-04 (duplicate idempotence) | PROVISIONAL | LOT 2B specific; no crypto anti-replay |
| TC-012 | R-05 (stale election rejection) | PROVISIONAL | LOT 2B specific; term monotonicity |

**Policy:** Repository TC-xxx IDs are preserved. Mapping to ADD test cases is explicit above. No renaming of repo tests. A mapping can be: FULL / PARTIAL / RELATED / NONE. Do not imply equivalence merely because two tests examine similar behavior. In particular, LOT 2B tests TC-008–TC-012 are PARTIAL evidence toward ADD anti-replay/safety requirements because they test deterministic scenarios only with semantic rejection, not cryptographic anti-replay.

---

## 5. Traceability Maintenance Rules

1. **Every new test** must add forward/reverse trace rows
2. **Every implementation change** must update affected trace rows
3. **Evidence class** must be explicit (CbD/CbA/CbT/IMPLEMENTED-SIM/etc.)
4. **Gaps/limitations** documented per trace
5. **Review at Lot closure** — all traces for Lot requirements must be complete