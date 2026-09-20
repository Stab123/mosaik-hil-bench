# MOSAÏK Bidirectional Traceability

**Document:** MOSAIK-TRACE-001  
**Issue:** 1.5 — 20 September 2026  
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

## 2. Existing Test Cases Traceability (TC-001 through TC-012)

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
| TC-013 | REQ-FUNC-0001, REQ-FUNC-0002 | REQ-FUN-001, REQ-FUN-002 | Cluster_Task / svc_mosaik_proto (lease, ACK evidence) | `mosaik_node.c:368` + `test_mosaik.c:427-445` | PASS | IMPLEMENTED-SIM |
| TC-014 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:89-121, 223-225` | PASS | IMPLEMENTED-SIM |
| TC-015 | REQ-FUNC-0001, REQ-FUNC-0002 | REQ-FUN-001, REQ-FUN-002 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:276, 368` | PASS | IMPLEMENTED-SIM |
| TC-016 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:89-121, 368` | PASS (7 offsets) | IMPLEMENTED-SIM |
| TC-017 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:89-121` | PASS | IMPLEMENTED-SIM |
| TC-018 | REQ-FUNC-0001, REQ-FUNC-0002 | REQ-FUN-001, REQ-FUN-002 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:89-121, 304-318` + partition | PASS | IMPLEMENTED-SIM |
| TC-019 | REQ-FUNC-0002, REQ-FUNC-0001 | REQ-FUN-002, REQ-FUN-001 | Cluster_Task / svc_mosaik_proto (ACK evidence) | `test_mosaik.c:196-221, 427-445` + `mosaik_node.c:368` | PASS | IMPLEMENTED-SIM |
| TC-020 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:89-121, 223-225, 287-291` | PASS | IMPLEMENTED-SIM |
| TC-021 | REQ-FUNC-0001 (uniqueness); cold-restart property | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:160-199, 223-225` + `test_mosaik.c:333, 345` | PASS | IMPLEMENTED-SIM |
| TC-022 | REQ-FUNC-0004, REQ-PERF-0001, REQ-FUNC-0001 | REQ-PERF-001, REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:276, 133-158` | PASS | IMPLEMENTED-SIM |
| TC-023 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:160-199, 223-225, 89-121` | PASS | IMPLEMENTED-SIM |
| TC-024 | REQ-FUNC-0001 (uniqueness); cold-restart property | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:160-199` | PASS | IMPLEMENTED-SIM |
| TC-025 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:160-199, 223-225, 89-121` | PASS | IMPLEMENTED-SIM |
| TC-026 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:89-121, 287-291` | PASS | IMPLEMENTED-SIM |
| TC-027 | REQ-FUNC-0001 (uniqueness); cold-restart property | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:160-199` | PASS (5 cycles) | IMPLEMENTED-SIM |
| TC-028 | REQ-FUNC-0004, REQ-PERF-0001, REQ-FUNC-0001 | REQ-PERF-001, REQ-FUN-001 | Cluster_Task / svc_mosaik_proto (retry backoff) | `mosaik_node.c:276, 296, 384-405` | PASS (628 ms sim) | IMPLEMENTED-SIM |
| TC-029 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:133-146, 160-199, 296` | PASS | IMPLEMENTED-SIM |
| TC-030 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:160-199, 223-225` | PASS | IMPLEMENTED-SIM |
| TC-031 | REQ-FUNC-0004, REQ-PERF-0001, REQ-FUNC-0001 | REQ-PERF-001, REQ-FUN-001 | Cluster_Task / svc_mosaik_proto (retry backoff) | `mosaik_node.c:384-405, 296, 304-318` | PASS (680 ms sim; RED at `d38985d`) | IMPLEMENTED-SIM |
| TC-032 | REQ-FUNC-0002, REQ-SAFE-0003 | REQ-FUN-002, REQ-SAF-002 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:384-387, 398-404` (span 1) | PASS (SAFE at 2980 ms) | IMPLEMENTED-SIM |
| TC-033 | REQ-FUNC-0002, REQ-SAFE-0003, REQ-FUNC-0001 | REQ-FUN-002, REQ-SAF-002, REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:304-318, 384-405` | PASS | IMPLEMENTED-SIM |
| TC-034 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:287-291, 305-309, 384-405` | PASS | IMPLEMENTED-SIM |
| TC-035 | REQ-SAFE-0003, REQ-FUNC-0004 | REQ-SAF-002, REQ-PERF-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:71, 245` | PASS | IMPLEMENTED-SIM |
| TC-036 | REQ-SAFE-0003 | REQ-SAF-002 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:245` | PASS | IMPLEMENTED-SIM |
| TC-037 | REQ-SAFE-0003 | REQ-SAF-002 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:245` | PASS | IMPLEMENTED-SIM |
| TC-038 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:368` | PASS | IMPLEMENTED-SIM |
| TC-039 | REQ-SAFE-0004 | REQ-SAF-003 | Cluster_Task / svc_mosaik_proto (SAFE evidence) | `mosaik_node.c:128, 376, 292, 168` | PASS (RED at af5da87) | IMPLEMENTED-SIM |
| TC-040 | REQ-SAFE-0004, REQ-FUNC-0001 | REQ-SAF-003, REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:400, 183` | PASS (RED at af5da87) | IMPLEMENTED-SIM |
| TC-041 | REQ-SAFE-0003, REQ-FUNC-0002 | REQ-SAF-002, REQ-FUN-002 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:443, 245` | PASS | IMPLEMENTED-SIM |
| TC-042 | REQ-FUNC-0004, REQ-PERF-0001 | REQ-PERF-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:425, 443` | PASS (host observations) | IMPLEMENTED-SIM |
| TC-043 | REQ-FUNC-0007 | REQ-ICD-002 | svc_mosaik_proto | `mosaik_proto.c:mosaik_decode`, `mosaik_node.c:238` | PASS | PARTIAL |
| TC-044 | REQ-FUNC-0002, REQ-SAFE-0003 | REQ-FUN-002, REQ-SAF-002 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:461, 443` | PASS | IMPLEMENTED-SIM |
| TC-045 | REQ-SAFE-0004 | REQ-SAF-003 | Cluster_Task / svc_mosaik_proto (SAFE evidence) | `mosaik_node.c:376, 400` | PASS (RED at af5da87) | IMPLEMENTED-SIM |
| TC-046 | REQ-FUNC-0004, REQ-FUNC-0001 | REQ-PERF-001, REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:425, 141` | PASS | IMPLEMENTED-SIM |
| TC-047 | REQ-SAFE-0004, REQ-FUNC-0001 | REQ-SAF-003, REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:292` | PASS (RED at af5da87) | IMPLEMENTED-SIM |
| TC-048 | REQ-SAFE-0004, REQ-FUNC-0004 | REQ-SAF-003, REQ-PERF-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:168, 400` | PASS (RED at af5da87) | IMPLEMENTED-SIM |
| TC-049 | NONE (reproducibility) | — | svc_mosaik_proto | deterministic RNG | PASS | IMPLEMENTED-SIM |
| TC-050 | NONE (protocol invariant; related REQ-FUNC-0001) | — | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:141, 324` | PASS (RED at af5da87; green at 034db92) | IMPLEMENTED-SIM |
| TC-051 | NONE (INV-MODE-LEGAL) | — | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:71, 170`, `mosaik_proto.c:107` | PASS (0 illegal pairs, 0 out-of-range bytes) | IMPLEMENTED-SIM |
| TC-052 | NONE (INV-MODE-NO-MAGIC; related REQ-SAFE-0004 evidence rule) | — | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:153-168` (start_election) | PASS (RED at 58a1b5d; green at ae9e408) | IMPLEMENTED-SIM |
| TC-053 | REQ-SAFE-0004, REQ-FUNC-0001 | REQ-SAF-003, REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:256` (adoption excludes SAFE), `375` (SAFE handler) | PASS (RED at 58a1b5d, 397 ms interruption; green at ae9e408, 0 ms) | IMPLEMENTED-SIM |
| TC-054 | NONE (INV-MODE-METADATA-NONAUTHORITATIVE) | — | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:265-320` (HEARTBEAT handler ignores msg.state) | PASS | IMPLEMENTED-SIM |
| TC-055 | REQ-FUNC-0007 | REQ-ICD-002 | svc_mosaik_proto | `mosaik_proto.c:107`, `mosaik_node.c:240` | PASS (4 frames rejected, receivers unchanged) | PARTIAL |
| TC-056 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:265` (check_heartbeat_stale) | PASS (stale-term rejections +1 per replay) | IMPLEMENTED-SIM |
| TC-057 | REQ-SAFE-0004, REQ-FUNC-0004 | REQ-SAF-003, REQ-PERF-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:128, 170, 185, 414` | PASS | IMPLEMENTED-SIM |
| TC-058 | REQ-FUNC-0001 (uniqueness); cold-restart property | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:185, 433, 471` | PASS | IMPLEMENTED-SIM |
| TC-059 | REQ-SAFE-0004 | REQ-SAF-003 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:256, 375` | PASS (terms and authority unchanged over 1000 steps) | IMPLEMENTED-SIM |
| TC-060 | NONE (reproducibility) | — | svc_mosaik_proto | deterministic RNG | PASS | IMPLEMENTED-SIM |
| TC-061 | NONE (HIL-derived: INV-RECONFIG-QUORUM) | — | svc_mosaik_proto | `mosaik_node.c:quorum_of`, `mosaik_proto.c:decode` | PASS | IMPLEMENTED-SIM |
| TC-062 | NONE (HIL-derived: INV-RECONFIG-NO-MAGIC) | — | Cluster_Task / svc_mosaik_proto | membership gate in `mosaik_on_rx` | PASS | IMPLEMENTED-SIM |
| TC-063 | REQ-FUNC-0002 (no authority without quorum) | REQ-FUN-002 | Cluster_Task / svc_mosaik_proto | `mosaik_request_reconfiguration` authority gate | PASS | IMPLEMENTED-SIM |
| TC-064 | REQ-FUNC-0002 (quorum-based reconfiguration) | REQ-FUN-002 | Cluster_Task / svc_mosaik_proto | `handle_config` PROPOSE/ACCEPT/COMMIT | PASS (RED at 02b27fa) | IMPLEMENTED-SIM |
| TC-065 | NONE (HIL-derived: INV-RECONFIG-TRANSITION) | — | Cluster_Task / svc_mosaik_proto | joint commit rule in `handle_config` | PASS (RED at 02b27fa) | IMPLEMENTED-SIM |
| TC-066 | NONE (HIL-derived: INV-RECONFIG-REMOVED-NODE) | — | Cluster_Task / svc_mosaik_proto | `mosaik_load_config_store`, membership gate | PASS (RED at 02b27fa) | IMPLEMENTED-SIM |
| TC-067 | NONE (HIL-derived: INV-RECONFIG-OLD-CONFIG) | — | svc_mosaik_proto | epoch comparison in `handle_config` | PASS (RED at 02b27fa) | IMPLEMENTED-SIM |
| TC-068 | REQ-FUNC-0001 (leader uniqueness) | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | joint election and lease rules | PASS (RED at 02b27fa) | IMPLEMENTED-SIM |
| TC-069 | REQ-FUNC-0001, REQ-FUNC-0002 | REQ-FUN-001, REQ-FUN-002 | Cluster_Task / svc_mosaik_proto | `mosaik_has_quorum_ack_evidence` | PASS | IMPLEMENTED-SIM |
| TC-070 | NONE (reproducibility) | — | svc_mosaik_proto | deterministic core and harness | PASS | IMPLEMENTED-SIM |
| TC-071 | NONE (HIL-derived: INV-RECONFIG-CONSISTENT) | — | svc_mosaik_proto | one binding per epoch, conflict refusal | PASS | IMPLEMENTED-SIM |
| TC-072 | NONE (HIL-derived: INV-RECONFIG-OLD-CONFIG) | — | svc_mosaik_proto | duplicate and reorder handling | PASS | IMPLEMENTED-SIM |
| TC-073 | REQ-FUNC-0007 (frame validity) | REQ-ICD-002 | svc_mosaik_proto | `mosaik_decode` CONFIG validation, `mask_valid` | PASS | PARTIAL |
| TC-074 | NONE (HIL-derived: INV-RECONFIG-TRANSITION) | — | Cluster_Task / svc_mosaik_proto | commit rule, announcement repair | PASS | IMPLEMENTED-SIM |
| TC-075 | NONE (HIL-derived: INV-RECONFIG-REMOVED-NODE) | — | Cluster_Task / svc_mosaik_proto | membership gate in `mosaik_on_rx` | PASS | IMPLEMENTED-SIM |
| TC-076 | REQ-FUNC-0002 | REQ-FUN-002 | Cluster_Task / svc_mosaik_proto | joint commit rule, ACCEPT source rule | PASS | IMPLEMENTED-SIM |
| TC-077 | NONE (HIL-derived: host-model persistence) | — | svc_mosaik_proto | `mosaik_load_config_store`, `config_persist` | PASS | IMPLEMENTED-SIM |
| TC-078 | REQ-SAFE-0003, REQ-SAFE-0004 | REQ-SAF-002, REQ-SAF-003 | Cluster_Task / svc_mosaik_proto | SAFE gate, DEGRADED evidence, proposer maintenance | PASS | IMPLEMENTED-SIM |
| TC-079 | REQ-FUNC-0001 (adversarial) | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | whole reconfiguration path | PASS (1536 schedules) | IMPLEMENTED-SIM |
| TC-080 | NONE (HIL-derived: INV-RECONFIG-OLD-CONFIG) | — | svc_mosaik_proto | epoch comparison, duplicate handling | PASS | IMPLEMENTED-SIM |
| TC-081 | NONE (HIL-derived: INV-RECONFIG-TRANSITION) | — | Cluster_Task / svc_mosaik_proto | commit rule, store reload, announcement repair | PASS (40 schedules) | IMPLEMENTED-SIM |
| TC-082 | NONE (HIL-derived: INV-RECONFIG-CONSISTENT) | — | svc_mosaik_proto | persisted binding, conflict refusal | PASS (6 crash points) | IMPLEMENTED-SIM |
| TC-083 | NONE (HIL-derived: INV-RECONFIG-REMOVED-NODE, INV-RECONFIG-QUORUM) | — | Cluster_Task / svc_mosaik_proto | membership gate, epoch admission rule | PASS (10 stuck cases) | IMPLEMENTED-SIM |
| TC-084 | NONE (HIL-derived: INV-RECONFIG-REMOVED-NODE) | — | Cluster_Task / svc_mosaik_proto | `config_commit`, evidence freshness | PASS | IMPLEMENTED-SIM |
| TC-085 | NONE (HIL-derived: INV-RECONFIG-CONSISTENT) | — | svc_mosaik_proto | one binding per epoch | PASS | IMPLEMENTED-SIM |
| TC-086 | NONE (HIL-derived: INV-TERM-MONOTONIC and epoch independence) | — | svc_mosaik_proto | CONFIG dispatch before term processing | PASS | IMPLEMENTED-SIM |
| TC-087 | REQ-FUNC-0001 (authority evidence) | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_has_quorum_ack_evidence` freshness | PASS (boundary 99/100 ms) | IMPLEMENTED-SIM |
| TC-088 | REQ-SAFE-0003, REQ-SAFE-0004 | REQ-SAF-002, REQ-SAF-003 | Cluster_Task / svc_mosaik_proto | SAFE gate, DEGRADED evidence | PASS | IMPLEMENTED-SIM |
| TC-089 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | lease, retransmission and abandonment timers | PASS (0 overlaps) | IMPLEMENTED-SIM |
| TC-090 | REQ-FUNC-0001 (adversarial) | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | whole reconfiguration path | PASS (4608 schedules) | IMPLEMENTED-SIM |

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
| TC-013 | F-04 (partition lease) | RELATED | LOT 2C; one-way isolation, ACK-evidence lease |
| TC-014 | F-02 (split-brain absence) | RELATED | LOT 2C; asymmetric view |
| TC-015 | F-04 (partition lease) | RELATED | LOT 2C; selective heartbeat loss |
| TC-016 | R-02 (expired lease replay) | RELATED | LOT 2C; delay at lease boundary |
| TC-017 | R-04 (duplicate idempotence) | RELATED | LOT 2C; reordering |
| TC-018 | R-03 (partition recovery safety) | RELATED | LOT 2C; heal with queued traffic |
| TC-019 | F-04 (partition lease) | RELATED | LOT 2C; quorum-contact loss |
| TC-020 | F-02 (split-brain absence) | RELATED | LOT 2C; combined faults |
| TC-021 | — | NONE | LOT 2D; crash/restart not in the current Section 82 transcription (ADD-F001); protocol-property evidence |
| TC-022 | P-01 (failover latency), F-01 (leader election) | RELATED | LOT 2D; leader crash, sim time |
| TC-023 | R-03 (partition recovery safety) | RELATED | LOT 2D; former leader cold restart |
| TC-024 | — | NONE | LOT 2D; cold-restart vote reset; protocol-property evidence |
| TC-025 | R-03 (partition recovery safety) | RELATED | LOT 2D; former leader after new election |
| TC-026 | R-01 (stale term rejection) | RELATED | LOT 2D; delayed pre-crash frames |
| TC-027 | — | NONE | LOT 2D; repeated cold restart; protocol-property evidence |
| TC-028 | P-01 (failover latency) | RELATED | LOT 2D; 628 ms sim after crash near lease expiry; not a bound |
| TC-029 | F-01 (leader election) | RELATED | LOT 2D; no duplicate vote after candidate cold restart |
| TC-030 | F-02 (split-brain absence) | RELATED | LOT 2D; restart under asymmetric faults |
| TC-031 | P-01 (failover latency) | RELATED | LOT 2D; 680 ms sim after natural split vote; not a bound |
| TC-032 | F-03 (no quorum SAFE) | RELATED | LOT 2D; SAFE under configuration-forced permanent contention |
| TC-033 | F-03 (no quorum SAFE) | RELATED | LOT 2D; SAFE under directional cut during retry |
| TC-034 | R-05 (stale election rejection) | RELATED | LOT 2D; stale VOTE_REQ/VOTE_GRANT during retry |
| TC-035 | S-01 (SAFE latch), F-01 (leader election) | RELATED | LOT 3; SAFE contract and recovery around a SAFE node |
| TC-036 | S-01 (SAFE latch) | RELATED | LOT 3; SAFE non-participation |
| TC-037 | S-01 (SAFE latch) | RELATED | LOT 3; latch against adversarial traffic |
| TC-038 | F-02 (split-brain absence) | RELATED | LOT 3; SAFE not propagated |
| TC-039 | — | NONE | LOT 3; DEGRADED persistence; no Section 82 test case for DEGRADED (ADD-F011) |
| TC-040 | — | NONE | LOT 3; DEGRADED expiry and cold-restart rejoin |
| TC-041 | F-03 (no quorum SAFE) | RELATED | LOT 3; exhaustion terminal after restore |
| TC-042 | P-01 (failover latency) | RELATED | LOT 3; isolation boundary, host observations only |
| TC-043 | I-01 (codec/CRC) | RELATED | LOT 3; detection-only, PROTO_ERROR reserved |
| TC-044 | F-03 (no quorum SAFE) | RELATED | LOT 3; crash during recovery |
| TC-045 | — | NONE | LOT 3; bounded replayed SAFE evidence |
| TC-046 | P-01 (failover latency) | RELATED | LOT 3; repeated transient recovery |
| TC-047 | S-01, R-01 (stale term rejection) | RELATED | LOT 3; adversarial combination |
| TC-048 | P-01 (failover latency) | RELATED | LOT 3; adversarial combination |
| TC-049 | — | NONE | LOT 3; reproducibility |
| TC-050 | F-01 (leader election) | RELATED | LOT 2 erratum found in LOT 3; one vote per term across same-term demotion |
| TC-051 | — | NONE | LOT 4; state × role legality and emitted byte range |
| TC-052 | — | NONE | LOT 4; election does not degrade (ADD-F012) |
| TC-053 | S-01 (SAFE / peer DEGRADED), F-01 (leader election) | RELATED | LOT 4; SAFE announcement term not adopted (ADD-F013) |
| TC-054 | — | NONE | LOT 4; heartbeat state metadata non-authoritative |
| TC-055 | I-01 (codec/CRC) | RELATED | LOT 4; out-of-range state byte rejected by the decoder |
| TC-056 | R-01 (stale term rejection) | RELATED | LOT 4; stale heartbeat with DEGRADED metadata |
| TC-057 | S-01, P-01 | RELATED | LOT 4; leader change while DEGRADED |
| TC-058 | F-01 | RELATED | LOT 4; partition, crash, cold restart legality |
| TC-059 | S-01 | RELATED | LOT 4; lower-term SAFE announcement |
| TC-060 | — | NONE | LOT 4; determinism of the RED scenarios |
| TC-061–TC-063 | — | NONE | LOT 5; membership representation and the rule that reachability is not membership |
| TC-064, TC-076 | F-02 (quorum reconfiguration) | RELATED | LOT 5; a membership transition executed through the protocol |
| TC-065, TC-068, TC-074, TC-081 | — | NONE | LOT 5; joint quorum and partial-commit safety, HIL-derived |
| TC-066, TC-075, TC-083, TC-084 | — | NONE | LOT 5; removed-node and re-admission semantics, HIL-derived |
| TC-067, TC-072, TC-080 | R-01 (stale rejection) | RELATED | LOT 5; configuration replay protection, distinct from heartbeat replay |
| TC-069, TC-087, TC-089 | F-01, P-01 | RELATED | LOT 5; authority and lease evidence under membership change |
| TC-070 | — | NONE | LOT 5; determinism |
| TC-071, TC-082, TC-085 | — | NONE | LOT 5; one successor per epoch, HIL-derived |
| TC-073 | I-01 (codec/CRC) | RELATED | LOT 5; configuration frame validation |
| TC-077 | — | NONE | LOT 5; host-model configuration store, no ADD persistence requirement is claimed |
| TC-078, TC-088 | S-01 (SAFE) | RELATED | LOT 5; SAFE and DEGRADED during a transaction |
| TC-079, TC-090 | — | NONE | LOT 5; bounded adversarial schedule matrices |
| TC-086 | — | NONE | LOT 5; configuration epoch and leadership term independence |

**Policy:** Repository TC-xxx IDs are preserved. Mapping to ADD test cases is explicit above. No renaming of repo tests. A mapping can be: FULL / PARTIAL / RELATED / NONE. Do not imply equivalence merely because two tests examine similar behavior. In particular, LOT 2B tests TC-008–TC-012 are PARTIAL evidence toward ADD anti-replay/safety requirements because they test deterministic scenarios only with semantic rejection, not cryptographic anti-replay. LOT 2C and LOT 2D tests TC-013–TC-034 are RELATED evidence only: the current Section 82 transcription (ADD-F001) contains no test case for directional faults, crash, restart or election-retry behaviour, so no FULL mapping is claimed and no ADD identifier is invented. LOT 3 tests TC-035–TC-050 follow the same rule: RELATED where a Section 82 test case examines similar SAFE, quorum, latency or codec behaviour, NONE for DEGRADED semantics (ADD-F011), reproducibility and the one-vote invariant. LOT 4 tests TC-051–TC-060 follow the same rule: RELATED where a Section 82 test case examines similar SAFE, election, stale-term or codec behaviour, NONE for the mode invariants (INV-MODE-LEGAL, INV-MODE-NO-MAGIC, INV-MODE-METADATA-NONAUTHORITATIVE), which have no ADD requirement ID, and for determinism. LOT 5 tests TC-061–TC-090 follow it again. The eight reconfiguration invariants (INV-RECONFIG-NO-MAGIC, INV-RECONFIG-CONSISTENT, INV-RECONFIG-AUTHORITY, INV-RECONFIG-QUORUM, INV-RECONFIG-PARTITION, INV-RECONFIG-OLD-CONFIG, INV-RECONFIG-REMOVED-NODE, INV-RECONFIG-TRANSITION) have no normative ADD requirement identifier and are labelled **HIL-derived experimental invariants**: they were frozen during the LOT 5 specification freeze for the three-node host demonstrator, not transcribed from the ADD. Where a LOT 5 test also exercises an inherited invariant (INV-LEADER-UNIQUE, INV-SAFE-NO-AUTHORITY, INV-SAFE-LATCH, INV-NO-STALE-RECOVERY, INV-TERM-MONOTONIC, INV-ONE-VOTE-PER-TERM) the corresponding ADD requirement is named in the matrix above. No requirement identifier was invented.

---

## 5. Traceability Maintenance Rules

1. **Every new test** must add forward/reverse trace rows
2. **Every implementation change** must update affected trace rows
3. **Evidence class** must be explicit (CbD/CbA/CbT/IMPLEMENTED-SIM/etc.)
4. **Gaps/limitations** documented per trace
5. **Review at Lot closure** — all traces for Lot requirements must be complete

---

## 6. LOT 2C and LOT 2D Test Cases Traceability (TC-013 through TC-034)

Validated at commit `f4e0f3c1606766ac9b5b3332964e3cdbe5f1e2ea` (205 checks,
0 failures, ASan/UBSan clean) and re-validated unchanged, byte-identical
output, at `7df0af0` (360 checks, 0 failures). Line references below are to
`f4e0f3c`. All evidence is IMPLEMENTED-SIM: deterministic
host demonstrator only. Line references are to that commit. Where no ADD
requirement covers a test, the entry says so and the test is recorded as
verification evidence for a protocol property or invariant. INV-LEADER-UNIQUE
(maximum concurrent valid leadership authorities observed at the defined
deterministic observation points ≤ 1) is an invariant, not a requirement ID;
every test below asserts it.

### TC-013: One-Way Leader Isolation: Authority Expires

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001, REQ-FUNC-0002 → REQ-FUN-001, REQ-FUN-002 → L4:svc_mosaik_proto (lease, quorum-contact evidence) → `mosaik_node.c:368` (step-down at lease expiry) + `mosaik_node.c:321-338` (ACK evidence recorded per peer) + `test_mosaik.c:196-221, 427-445` (renewal only on ACK actually received) → TC-013 → PASS (authority expired under one-way isolation, leader stepped down, max concurrent valid authorities observed 1) → IMPLEMENTED-SIM |
| **Reverse** | TC-013 → `test_mosaik.c:261` (one-way isolation: leader sends, cannot receive) → lease not renewed → `mosaik_node.c:368` → REQ-FUN-001/002 → REQ-FUNC-0001/0002 |
| **Limitation** | Deterministic directional model on a virtual bus; lease renewal bookkeeping is harness-mediated from node-recorded ACK evidence; no physical bus behaviour. |

### TC-014: Asymmetric Minority View

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:223-225` (higher term adopted), `mosaik_node.c:89-121` (stale rejection), lease expiry `368` → TC-014 → PASS (no double valid authority, term monotonicity preserved) → IMPLEMENTED-SIM |
| **Reverse** | TC-014 → `test_mosaik.c:284` (fixed asymmetric directional pattern) → term monotonicity + lease → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | One fixed asymmetric pattern; deterministic host model only. |

### TC-015: Selective Heartbeat Loss

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001, REQ-FUNC-0002 → REQ-FUN-001, REQ-FUN-002 → L4:svc_mosaik_proto → `mosaik_node.c:276` (follower deadline = lease + jitter), `368` (lease expiry step-down), `test_mosaik.c:427-445` (ACK evidence) → TC-015 → PASS (single and transient loss keep one authority; sustained loss invalidates authority and the leader steps down) → IMPLEMENTED-SIM |
| **Reverse** | TC-015 → `test_mosaik.c:99` (per-path drop patterns: single, two, three consecutive, sustained) → lease and follower deadline → REQ-FUN-001/002 → REQ-FUNC-0001/0002 |
| **Limitation** | Deterministic loss patterns only; no statistical loss model; host model only. |

### TC-016: Delay Around Lease Boundary

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:89-121` (stale heartbeat rejection), `368` (lease expiry) → TC-016 → PASS (expired authority not resurrected by late delivery at offsets −100, −10, −1, 0, +1, +10, +100 ms; no term regression) → IMPLEMENTED-SIM |
| **Reverse** | TC-016 → `test_mosaik.c:140` (frame injection at deterministic offsets) → stale rejection + lease expiry → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | Seven deterministic offsets in simulated milliseconds; sub-millisecond timing not modelled. |

### TC-017: Message Reordering

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:89-121` (term monotonicity, duplicate sequence), `223-225` → TC-017 → PASS (no term regression, valid authority maintained, no double authority) → IMPLEMENTED-SIM |
| **Reverse** | TC-017 → `test_mosaik.c:140` (M2 delivered before M1) → term monotonicity → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | Single reordering pair; semantic rejection only, no cryptographic anti-replay. |

### TC-018: Partition Heal With Queued Traffic

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001, REQ-FUNC-0002 → REQ-FUN-001, REQ-FUN-002 → L4:svc_mosaik_proto → `mosaik_node.c:89-121` (stale authority via last_hb_term), `223-225` (adopt newer term), quorum election `304-318` → TC-018 → PASS (majority elects new leader at higher term; newer term remains authoritative after heal with queued traffic; old leader adopts newer term; convergence to one valid authority) → IMPLEMENTED-SIM |
| **Reverse** | TC-018 → `test_mosaik.c:242` (2+1 partition), `140` (queued old traffic), `301` (heal) → stale authority rejection → REQ-FUN-001/002 → REQ-FUNC-0001/0002 |
| **Limitation** | One deterministic 3-node 2+1 partition and heal; host model only. |

### TC-019: Selective Quorum Contact Failure

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0002, REQ-FUNC-0001 → REQ-FUN-002, REQ-FUN-001 → L4:svc_mosaik_proto → `test_mosaik.c:196-221, 427-445` (fresh received current-term ACK evidence required), `mosaik_node.c:368` (step-down) → TC-019 → PASS (authority expires without inbound quorum contact; leader steps down; new valid leader elected) → IMPLEMENTED-SIM |
| **Reverse** | TC-019 → outbound-only leader (ACK paths dropped) → no renewal → `mosaik_node.c:368` → election → REQ-FUN-002/001 → REQ-FUNC-0002/0001 |
| **Limitation** | Harness-mediated renewal bookkeeping; deterministic host model; outbound heartbeat delivery alone never renews (commit `c6f600b`). |

### TC-020: Adversarial Combination

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:89-121, 223-225, 287-291` (stale rejection, term adoption) + lease → TC-020 → PASS (no split-brain, term monotonicity preserved under combined asymmetric, drop, delayed-stale and reordering faults) → IMPLEMENTED-SIM |
| **Reverse** | TC-020 → `test_mosaik.c:140, 99` (combined faults) → term monotonicity + lease → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | One deterministic combination; not a randomised campaign (LOT 9). |

### TC-021: Follower Crash While Leader and Quorum Available

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:160-199` (cold `mosaik_init()`: term 0, no vote), `223-225` (adopt current term from first higher-term message) → TC-021 → PASS (leader authority and term unchanged during follower crash; restarted follower adopts current term, is follower, holds no vote in the current term; single valid leader) → IMPLEMENTED-SIM. Protocol-property evidence (cold-restart semantics); no ADD requirement for restart behaviour exists in the current transcription. |
| **Reverse** | TC-021 → `test_mosaik.c:333` (crash), `345` (cold restart) → `mosaik_node.c:160-199, 223-225` → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | Crash and restart are harness models; no term/vote persistence is implemented; restart loses all volatile state by design. |

### TC-022: Leader Crash

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0004, REQ-PERF-0001, REQ-FUNC-0001 → REQ-PERF-001, REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:276` (lease-aligned follower deadline), `133-146` (start_election), `148-158` (become_leader on quorum) → TC-022 → PASS (new leader differs from crashed node, term advanced, single valid authority) → IMPLEMENTED-SIM |
| **Reverse** | TC-022 → `test_mosaik.c:333` (leader crash) → follower timeout → election → REQ-PERF-001/REQ-FUN-001 → REQ-FUNC-0004/0001 |
| **Limitation** | Simulated time only; the crash instant (2000 ms) is one deterministic case, not a worst case. |

### TC-023: Leader Crash, Election, Former Leader Restarts

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:160-199` (cold init), `223-225` (adopt newer term), `89-121` (stale authority) → TC-023 → PASS (former leader adopts the new term after cold restart, does not regain leadership, new leader retains valid authority, no split-brain) → IMPLEMENTED-SIM |
| **Reverse** | TC-023 → `test_mosaik.c:333, 345` → `mosaik_node.c:160-199, 223-225` → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | Cold restart only; no persistence; deterministic host model. |

### TC-024: Crashed Follower Restart and Rejoin

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:160-199` (voted_term and voted_for reset to 0 on cold init), `223-225` → TC-024 → PASS (restarted follower adopts current cluster term, is follower, vote state reset, single valid leader throughout) → IMPLEMENTED-SIM. Protocol-property evidence for cold-restart semantics. |
| **Reverse** | TC-024 → `test_mosaik.c:333, 345` → `mosaik_node.c:160-199` → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | Demonstrates loss of volatile vote state on restart; does not demonstrate persistence, which is not implemented. |

### TC-025: Former Leader Restarts After Another Leader Elected

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:160-199, 223-225, 89-121` → TC-025 → PASS (former leader adopts newer term, is follower, new leader retains authority and its term is unchanged, single valid leader) → IMPLEMENTED-SIM |
| **Reverse** | TC-025 → `test_mosaik.c:333, 345` → newer term adopted, no stale authority → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | Cold restart only; deterministic host model. |

### TC-026: Restart With Delayed Pre-Crash Messages Queued

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:89-121` (stale heartbeat), `287-291` (stale VOTE_REQ), `223-225` → TC-026 → PASS (restarted follower adopts current term despite delayed stale pre-crash frames; is follower; single valid leader) → IMPLEMENTED-SIM (Lot 2B rejection under Lot 2D restart) |
| **Reverse** | TC-026 → `test_mosaik.c:333, 345` + `140, 149` (delayed pre-crash frames) → stale rejection → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | Three delayed frames, deterministic; semantic rejection only. |

### TC-027: Repeated Crash/Restart of One Node

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:160-199, 223-225` → TC-027 → PASS (after five crash/restart cycles the node adopts the current term, is follower, single valid leader maintained, leader term unchanged) → IMPLEMENTED-SIM. Protocol-property evidence (no state corruption across repeated cold restarts). |
| **Reverse** | TC-027 → `test_mosaik.c:333, 345` ×5 → `mosaik_node.c:160-199` → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | Five deterministic cycles; cold restart only. |

### TC-028: Crash During/Near Lease Expiry

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0004, REQ-PERF-0001, REQ-FUNC-0001 → REQ-PERF-001, REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:276` (lease-aligned follower deadline), `296` (one vote per term), `384-405` (candidate retry backoff, C2-a), `133-146`, `148-158` + `test_mosaik.c:427-445` (received-ACK evidence for the isolation setup) → TC-028 → PASS (leader crashed at 2366 ms with 50 ms lease remaining; natural split vote at 2822; node 3 valid at 2994 ms, 628 ms after the crash in the host model; term 3; single valid authority) → IMPLEMENTED-SIM. History: FAIL at `9e3f061`, `c6f600b`, `d38985d`; PASS since `f4e0f3c`. Evidence: LOT2D_CRASH_RECOVERY_REPORT.md §5–§10. |
| **Reverse** | TC-028 → `test_mosaik.c:333` (crash), ACK paths dropped → `mosaik_node.c:384-405` (retry backoff) → election → REQ-PERF-001/REQ-FUN-001 → REQ-FUNC-0004/0001 |
| **Limitation** | One deterministic recovery time in the host model, not a worst-case bound; the first collision remains possible; sub-millisecond bus races not modelled. |

### TC-029: Crash During Election (Candidate)

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:133-146` (self-vote in current term), `160-199` (cold init resets term, voted_for, voted_term), `296` (one vote per term) → TC-029 → PASS (candidate self-voted in its term; after cold restart term, voted_for and voted_term are 0; no duplicate vote in the original term; single valid leader) → IMPLEMENTED-SIM |
| **Reverse** | TC-029 → `test_mosaik.c:333, 345` (candidate crashed and restarted) → `mosaik_node.c:133-146, 160-199, 296` → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | Cold restart only; the printed `voted_term` value depends on the deterministic retry timing (4 at `f4e0f3c`). |

### TC-030: Recovery Under Asymmetric Network

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:160-199, 223-225, 89-121` + lease → TC-030 → PASS (no split-brain during crash and restart under directional faults; term monotonicity preserved) → IMPLEMENTED-SIM |
| **Reverse** | TC-030 → `test_mosaik.c:333, 345` under `284`/`99` directional faults → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | One deterministic asymmetric pattern; host model only. |

### TC-031: Natural Election Collision, Randomized Retry Recovery

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0004, REQ-PERF-0001, REQ-FUNC-0001 → REQ-PERF-001, REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:384-405` (C2-a: failed timeout, backoff, retry via start_election), `296` (one vote per term), `304-318` (quorum) + `test_mosaik.c:2211` (read-only shared-deadline finder), `2255` (read-only observer), `2295` → TC-031 → PASS (natural split vote at term 2; retry deadlines desynchronized; exactly one failed election per survivor; no SAFE; node 1 valid at 2696 ms, 680 ms after the crash at 2016 in the host model; term 3; max valid authorities 1; no term regression) → IMPLEMENTED-SIM. History: 6 checks RED at `d38985d` by design; PASS since `f4e0f3c`. Evidence: LOT2D_CRASH_RECOVERY_REPORT.md §7–§10. |
| **Reverse** | TC-031 → leader crashed at a naturally shared follower deadline (no protocol internal written) → `mosaik_node.c:384-405` → election → REQ-PERF-001/REQ-FUN-001 → REQ-FUNC-0004/0001 |
| **Limitation** | Deterministic host model; one measured recovery, not a bound; collisions remain possible; no hardware timing. |

### TC-032: Permanent Retry Contention Keeps SAFE Contract

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0002, REQ-SAFE-0003 → REQ-FUN-002, REQ-SAF-002 → L4:svc_mosaik_proto → `mosaik_node.h:47` + `mosaik_node.c:398-404` (`candidate_retry_backoff_span_ms` = 1, zero effective desynchronisation), `384-387` (SAFE after max_failed_elections) + `test_mosaik.c:309` (configuration-taking init) → TC-032 → PASS (retries stayed synchronized; no authority manufactured; exactly three genuine failed vote timeouts on each survivor; SAFE/NO_QUORUM on both at 2980 ms; max valid authorities 1) → IMPLEMENTED-SIM. Evidence: LOT2D_CRASH_RECOVERY_REPORT.md §10–§11. |
| **Reverse** | TC-032 → backoff span 1 by public configuration → `mosaik_node.c:384-387` (enter_safe NO_QUORUM) → REQ-SAF-002/REQ-FUN-002 → REQ-SAFE-0003/REQ-FUNC-0002 |
| **Limitation** | Configuration-forced contention in the host model; SAFE latch only, no ground-arbitration exit (ADD-F009). |

### TC-033: Asymmetric Partition During Retry

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0002, REQ-SAFE-0003, REQ-FUNC-0001 → REQ-FUN-002, REQ-SAF-002, REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:304-318` (quorum requires a received grant), `384-405` (retry backoff and SAFE) + `test_mosaik.c:99` (one direction dropped after the collision) → TC-033 → PASS (no valid authority ever; no term regression; both survivors SAFE/NO_QUORUM only after three genuine failed timeouts; max valid authorities 1) → IMPLEMENTED-SIM. Evidence: LOT2D_CRASH_RECOVERY_REPORT.md §10–§11. |
| **Reverse** | TC-033 → natural collision, then path a→b dropped → no grant can arrive → `mosaik_node.c:384-387` → REQ-SAF-002/REQ-FUN-002/REQ-FUN-001 → REQ-SAFE-0003/REQ-FUNC-0002/0001 |
| **Limitation** | One deterministic directional cut; host model only. |

### TC-034: Stale Delayed Election Traffic During Retry

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:287-291` (stale VOTE_REQ rejected), `305-309` (stale VOTE_GRANT rejected), `384-405` (retry rules) + `test_mosaik.c:99` (NET_DELAY on the real term-2 VOTE_REQ), `149` (replayed term-2 VOTE_GRANT) → TC-034 → PASS (receiver advanced beyond the collision term; both delayed frames rejected as stale; no term regression; no authority in the collision term; eventual leader at term 3; SAFE if and only if max_failed_elections genuine failures; max valid authorities 1) → IMPLEMENTED-SIM (Lot 2B rejection under Lot 2D retry). Evidence: LOT2D_CRASH_RECOVERY_REPORT.md §10–§11. |
| **Reverse** | TC-034 → delayed old-term VOTE_REQ and replayed VOTE_GRANT → `mosaik_node.c:287-291, 305-309` → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | Semantic rejection only; one deterministic 320 ms delay; no cryptographic anti-replay. |

---

## 7. LOT 3 Test Cases Traceability (TC-035 through TC-050)

Validated at commit `7df0af01d6ae2120bce9a5c6378305e3ef7eeb5c` (360 checks,
0 failures, ASan/UBSan clean). Line references are to that commit. RED
evidence at `af5da87` is recorded per test. All evidence is IMPLEMENTED-SIM:
deterministic host demonstrator only. Where no ADD requirement covers a
test, the entry says NONE and names the invariant or property evidenced.

### TC-035: SAFE Node: No Authority, SAFE-Only Transmission, Cluster Recovers Around It

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-SAFE-0003, REQ-FUNC-0004, REQ-FUNC-0001 → REQ-SAF-002, REQ-PERF-001, REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:71` (enter_safe: follower role, SAFE announce), `245` (receive gate) + service gate in `mosaik_tick()` → TC-035 → PASS (SAFE node never valid or leader, SAFE frames only; survivors elected node 3 at 2452 ms, 452 ms after the latch; SAFE node did not recover) → IMPLEMENTED-SIM |
| **Reverse** | TC-035 → TC-004 style same-term heartbeat delivered to the leader → `mosaik_node.c:71, 245` → REQ-SAF-002/REQ-PERF-001 → REQ-SAFE-0003/REQ-FUNC-0004 |
| **Limitation** | Deterministic host model; recovery time is one observation, not a bound. |

### TC-036: SAFE Node Grants No Vote, Sends No ACK, Never Becomes Candidate

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-SAFE-0003 → REQ-SAF-002 → L4:svc_mosaik_proto → `mosaik_node.c:245` (SAFE gate precedes term adoption and every handler) → TC-036 → PASS (0 grants, 0 ACKs, 0 elections after admissible higher-term VOTE_REQ and HEARTBEAT; term unchanged) → IMPLEMENTED-SIM |
| **Reverse** | TC-036 → targeted higher-term frames to the SAFE node → `mosaik_node.c:245` → REQ-SAF-002 → REQ-SAFE-0003 |
| **Limitation** | Targeted single-frame delivery; host model only. |

### TC-037: SAFE Latch Against Old-, Same- and Higher-Term Traffic, Grants, Replays and Connectivity

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-SAFE-0003 → REQ-SAF-002 → L4:svc_mosaik_proto → `mosaik_node.c:245` → TC-037 → PASS (state, cause, term and role unchanged after every frame class, isolation and restore) → IMPLEMENTED-SIM. INV-SAFE-LATCH evidence. |
| **Reverse** | TC-037 → adversarial frames and `test_mosaik.c` isolate/restore → `mosaik_node.c:245` → REQ-SAF-002 → REQ-SAFE-0003 |
| **Limitation** | Latch demonstrated within one powered node instance; a cold restart clears SAFE (no persistence). |

### TC-038: SAFE Frames Do Not Propagate SAFE

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:368` (SAFE handler records evidence and sets DEGRADED only) → TC-038 → PASS (max simultaneous SAFE nodes 1 under genuine, replayed and forged SAFE frames; one valid leader) → IMPLEMENTED-SIM |
| **Reverse** | TC-038 → `bus_inject_frame` replays and a forged SAFE → `mosaik_node.c:368` → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | Trusted bus assumption; forgery only demonstrates non-propagation. |

### TC-039: DEGRADED Persists While Peer SAFE Evidence Is Fresh, No Flapping

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-SAFE-0004 → REQ-SAF-003 → L4:svc_mosaik_proto → `mosaik_node.c:128` (freshness helper), `376` (evidence record), `292` (heartbeat state decision), `168` (become_leader decision) → TC-039 → PASS at `7df0af0` (0 DEGRADED→NOMINAL transitions, 0 NOMINAL steps after the first SAFE announcement on both survivors; leader valid while DEGRADED). RED at `af5da87`: 26 and 1 transitions, 1273 and 50 NOMINAL steps → IMPLEMENTED-SIM. First executable evidence for REQ-SAFE-0004. |
| **Reverse** | TC-039 → leader SAFE via same-term heartbeat, 3 s observation → `mosaik_node.c:376, 292, 168` → REQ-SAF-003 → REQ-SAFE-0004 |
| **Limitation** | Host interpretation of exit and freshness (ADD-F011); evidence from received frames only. |

### TC-040: SAFE Evidence Expiry, Return to NOMINAL, SAFE Node Recovers Only by Cold Restart

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-SAFE-0004, REQ-FUNC-0001 → REQ-SAF-003, REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:400` (DEGRADED exit with leader evidence), `183` (cold init zeroes evidence) → TC-040 → PASS at `7df0af0` (both survivors NOMINAL 600 ms after the SAFE node was crashed; restarted node rejoined NOMINAL at term 2; leader and term unchanged). RED at `af5da87`: leader stayed DEGRADED → IMPLEMENTED-SIM |
| **Reverse** | TC-040 → `bus_crash_node` then `bus_restart_node` of the SAFE node → `mosaik_node.c:400, 183` → REQ-SAF-003 → REQ-SAFE-0004 |
| **Limitation** | Cold restart is the only SAFE exit; no persistence; host model. |

### TC-041: Election Exhaustion Terminal Even After Connectivity Returns

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-SAFE-0003, REQ-FUNC-0002 → REQ-SAF-002, REQ-FUN-002 → L4:svc_mosaik_proto → `mosaik_node.c:443` (third failure → SAFE/NO_QUORUM), `245` → TC-041 → PASS (three nodes SAFE at term 4 through exactly max_failed_elections genuine failures; no exit, no authority, no non-SAFE traffic after restore) → IMPLEMENTED-SIM |
| **Reverse** | TC-041 → sequential full isolation of two nodes, then restore → `mosaik_node.c:443` → REQ-SAF-002/REQ-FUN-002 → REQ-SAFE-0003/REQ-FUNC-0002 |
| **Limitation** | Cluster-wide SAFE through quorum loss, not propagation; recovery requires cold restart, not implemented as a protocol path. |

### TC-042: Leader Isolation Boundary With Recovered Predicate

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0004, REQ-PERF-0001 → REQ-PERF-001 → L4:svc_mosaik_proto → `mosaik_node.c:425` (lease expiry step-down), `141`, `443` → TC-042 → PASS (600 and 900 ms isolations recovered with the recovered predicate; 1200, 1500 and 2000 ms latched SAFE; monotonic; INV-LEADER-UNIQUE held in every run) → IMPLEMENTED-SIM |
| **Reverse** | TC-042 → `test_mosaik.c` isolate/restore sweep and `cluster_recovered()` (read-only) → `mosaik_node.c:425, 443` → REQ-PERF-001 → REQ-FUNC-0004 |
| **Limitation** | Durations are deterministic host observations under current timers (D6), not worst-case bounds. |

### TC-043: Malformed Frames Detection-Only

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0007 → REQ-ICD-002 → L4:svc_mosaik_proto → `mosaik_proto.c:mosaik_decode()` (CRC reject), `mosaik_node.c:238` (count only) → TC-043 → PASS (50 corrupted frames counted, no state, term or authority change) → PARTIAL (PROTO_ERROR reserved, decision D2) |
| **Reverse** | TC-043 → CRC-corrupted frames via `bus_inject_frame` → `mosaik_node.c:238` → REQ-ICD-002 → REQ-FUNC-0007 |
| **Limitation** | No PROTO_ERROR threshold, window or recovery is defined by the transcription; none implemented. |

### TC-044: Survivor Crash During Collision Recovery

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0002, REQ-SAFE-0003 → REQ-FUN-002, REQ-SAF-002 → L4:svc_mosaik_proto → `mosaik_node.c:461` (retry backoff), `443` → TC-044 → PASS (lone survivor SAFE/NO_QUORUM after exactly three genuine failures, no authority ever) → IMPLEMENTED-SIM |
| **Reverse** | TC-044 → natural collision, `bus_crash_node` during backoff → `mosaik_node.c:461, 443` → REQ-FUN-002 → REQ-FUNC-0002 |
| **Limitation** | Deterministic host model. |

### TC-045: Replayed SAFE Evidence Bounded, Local Evidence Only

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-SAFE-0004 → REQ-SAF-003 → L4:svc_mosaik_proto → `mosaik_node.c:376`, `128`, `400` → TC-045 → PASS at `7df0af0` (receiver DEGRADED for exactly 300 ms then NOMINAL; non-receivers 0 DEGRADED steps; nobody SAFE). RED at `af5da87`: 15 ms run → IMPLEMENTED-SIM. INV-FDIR-NO-MAGIC evidence. |
| **Reverse** | TC-045 → one replayed SAFE frame delivered to one follower via the directional model → `mosaik_node.c:376, 400` → REQ-SAF-003 → REQ-SAFE-0004 |
| **Limitation** | Window derived from heartbeat period; host model. |

### TC-046: Three Transient Leader Isolations Recover Without SAFE

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0004, REQ-FUNC-0001 → REQ-PERF-001, REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:425`, `141` → TC-046 → PASS (three 800 ms isolations recovered, recovered predicate after each, max failed elections 0, no SAFE) → IMPLEMENTED-SIM |
| **Reverse** | TC-046 → repeated isolate/restore → `mosaik_node.c:425` → REQ-PERF-001 → REQ-FUNC-0004 |
| **Limitation** | Fixed 800 ms cycles under current timers; host observation. |

### TC-047: Leader SAFE + Stale Heartbeat Replay + One-Way Drop During Election

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-SAFE-0004, REQ-FUNC-0001 → REQ-SAF-003, REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:292` (state decision after stale rejection), stale heartbeat rejection in `check_heartbeat_stale()` → TC-047 → PASS at `7df0af0` (one valid leader at term 3; stale replay rejected twice; 0 NOMINAL steps after DEGRADED). RED at `af5da87`: 2063 and 87 NOMINAL steps → IMPLEMENTED-SIM |
| **Reverse** | TC-047 → `bus_schedule_delayed` replay + `bus_set_net_action` one-way drop → `mosaik_node.c:292` → REQ-SAF-003/REQ-FUN-001 → REQ-SAFE-0004/REQ-FUNC-0001 |
| **Limitation** | One deterministic adversarial combination. |

### TC-048: Leader Crash + Natural Collision + Delayed SAFE Frame During Backoff

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-SAFE-0004, REQ-FUNC-0004 → REQ-SAF-003, REQ-PERF-001 → L4:svc_mosaik_proto → `mosaik_node.c:168` (become_leader DEGRADED while evidence fresh), `292`, `400` → TC-048 → PASS at `7df0af0` (valid leader 680 ms after the crash; DEGRADED runs exactly 300 ms on both survivors; NOMINAL afterwards). RED at `af5da87`: 16 and 17 ms runs → IMPLEMENTED-SIM |
| **Reverse** | TC-048 → natural shared deadline crash + `bus_inject_frame` SAFE attributed to the crashed leader → `mosaik_node.c:168, 400` → REQ-SAF-003/REQ-PERF-001 → REQ-SAFE-0004/REQ-FUNC-0004 |
| **Limitation** | Deterministic host model; recovery time is one observation. |

### TC-049: Deterministic Reproducibility of the SAFE/DEGRADED Scenario

| Direction | Trace |
|-----------|-------|
| **Forward** | NONE (reproducibility evidence) → per-node xorshift RNG seeded by id, integer-only simulation → TC-049 → PASS (identical per-step trajectory hash on two runs) → IMPLEMENTED-SIM |
| **Reverse** | TC-049 → repeated scenario → deterministic core and harness → NONE |
| **Limitation** | Reproducibility of the host model only. |

### TC-050: One Vote Per Term Across a Same-Term Step-Down (LOT 2 Erratum)

| Direction | Trace |
|-----------|-------|
| **Forward** | NONE as a requirement ID; protocol invariant of PROTOCOL.md section 7 (one vote per term), related to REQ-FUNC-0001 → L4:svc_mosaik_proto → `mosaik_node.c:141` (become_follower no longer erases vote memory, commit `034db92`), `324` (one-vote check keyed on voted_term) → TC-050 → PASS since `034db92` (vote (2,1) preserved across the lease-expiry demotion, 0 same-term grants, 1 higher-term grant). RED at `af5da87`: vote erased to (0,1), same-term grant emitted → IMPLEMENTED-SIM |
| **Reverse** | TC-050 → ACK paths dropped, lease expiry, same-term and higher-term VOTE_REQ via bus → `mosaik_node.c:141, 324` → PROTOCOL.md §7 → related REQ-FUNC-0001 |
| **Limitation** | LOT 2 erratum discovered in LOT 3; concurrent double authority was not observed and is not reachable with current timers; historical LOT 2 evidence unchanged. |

---

## 8. LOT 4 Test Cases Traceability (TC-051 through TC-060)

Validated at commit `ae9e408a50d6f80257f77fa245247741295c0b7d` (455 checks,
0 failures, ASan/UBSan clean). Line references are to that commit. RED
evidence at `58a1b5d` is recorded per test. All evidence is IMPLEMENTED-SIM:
deterministic host demonstrator only. Where no ADD requirement covers a
test, the entry says NONE and names the invariant or property evidenced.
The three mode invariants are defined in `LOT4_MODE_SEMANTICS_REPORT.md`
section 5.

### TC-051: Mode Legality Guard (State × Role Pairs, Emitted State Bytes)

| Direction | Trace |
|-----------|-------|
| **Forward** | NONE (INV-MODE-LEGAL) → L4:svc_mosaik_proto → `mosaik_node.c:71` (enter_safe: follower role), `170` (become_leader: NOMINAL or DEGRADED, never INIT), `mosaik_proto.c:107` (decoder state range) → TC-051 → PASS (0 illegal pairs over boot, split-brain, exhaustion, partition, crash and cold restart; emitted state bytes 0..3 only, role bytes 0..2 only; 0 DEGRADED node-steps without evidence at `ae9e408`, 10 at `58a1b5d`) → IMPLEMENTED-SIM |
| **Reverse** | TC-051 → read-only per-step observation of every running node and of every emitted frame's raw bytes → `mosaik_node.c:71, 170` → NONE |
| **Limitation** | Legality is evidenced only for pairs reached by the executed scenarios; DEGRADED + CANDIDATE is legal (fresh evidence during an election) and was observed. |

### TC-052: Fault-Free Cold Boot, No DEGRADED Without Peer SAFE Evidence (C1)

| Direction | Trace |
|-----------|-------|
| **Forward** | NONE as a requirement ID; INV-MODE-NO-MAGIC, the LOT 3 evidence rule behind REQ-SAFE-0004 (ADD-F011, ADD-F012) → L4:svc_mosaik_proto → `mosaik_node.c:153-168` (start_election no longer writes the state, commit `ae9e408`) → TC-052 → PASS since `ae9e408` (first candidacy node 2 at 312 ms, state INIT, mask 0x00; no DEGRADED without evidence; valid leader at 315 ms, stable heartbeat at 415 ms). RED at `58a1b5d`: node 2 CANDIDATE/DEGRADED at 312 ms, previous state INIT, term 1, mask 0x00, 2 ms run; 2 failing checks → IMPLEMENTED-SIM |
| **Reverse** | TC-052 → ordinary deterministic boot, no fault, no SAFE frame → `mosaik_node.c:153-168` → PROTOCOL.md §7 (election does not change the operational state) → NONE / ADD-F012 |
| **Limitation** | One deterministic boot trajectory; the property is asserted from cold init to the first stable heartbeat only. |

### TC-053: Higher-Term SAFE Announcement Has No Authority Effect (C2)

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-SAFE-0004, REQ-FUNC-0001 → REQ-SAF-003, REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:256` (higher-term adoption excludes MOSAIK_MSG_SAFE, commit `ae9e408`), `375` (SAFE handler records evidence only) → TC-053 → PASS since `ae9e408` (isolated follower SAFE/no-quorum at 2940 ms term 4; first evidence on survivors at 2942 ms; survivor terms 1 and 1; leader role and authority kept; 0 survivor VOTE_REQ; SAFE node 0 grants, 0 ACKs; survivors DEGRADED; authority interruption 0 ms). RED at `58a1b5d`: survivor terms 1 → 4 in the evidence step, leader role lost at 2942 ms, re-election, authority restored at 3339 ms by node 3 term 5, interruption 397 ms; 5 failing checks → IMPLEMENTED-SIM |
| **Reverse** | TC-053 → bidirectional isolation of one follower until genuine exhaustion, restore, the SAFE node's own periodic announcement → `mosaik_node.c:256, 375` → PROTOCOL.md §7 (SAFE contract: term not adopted) → REQ-SAF-003 → REQ-SAFE-0004 / ADD-F013 |
| **Limitation** | The interruption values are observations of one deterministic scenario, not bounds; a SAFE node's term is ignored for adoption, not detected or reacted to. |

### TC-054: Heartbeat State Metadata Is Not Protocol Evidence

| Direction | Trace |
|-----------|-------|
| **Forward** | NONE (INV-MODE-METADATA-NONAUTHORITATIVE) → L4:svc_mosaik_proto → `mosaik_node.c:265-320` (HEARTBEAT handler: msg.state is never read) → TC-054 → PASS (four legitimate current-term heartbeats with state INIT, NOMINAL, DEGRADED, SAFE: follower role, term, vote memory, evidence mask, NOMINAL state and leader identity unchanged, one ACK each; a latched SAFE node stays SAFE and sends no ACK for any variant) → IMPLEMENTED-SIM |
| **Reverse** | TC-054 → targeted delivery of encoded heartbeats to one follower and to one SAFE node → `mosaik_node.c:265-320` → NONE |
| **Limitation** | Executable state values only; the receiver was a stable follower or a latched SAFE node. |

### TC-055: Out-of-Range State Byte With Valid CRC Is Rejected, No Protocol Effect

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0007 → REQ-ICD-002 → L4:svc_mosaik_proto → `mosaik_proto.c:107` (state byte > 3 rejected), `mosaik_node.c:240` (decode_errors) → TC-055 → PASS (HEARTBEAT and SAFE frames with state byte 4 and 5 and a recomputed valid CRC: control frame with byte 1 decodes, altered frame rejected, each receiver counted one decode error and changed no role, term, vote, evidence, state or authority; valid leader unchanged) → PARTIAL |
| **Reverse** | TC-055 → frames built by `mosaik_encode()` with byte 3 altered and CRC recomputed, injected on the bus → `mosaik_proto.c:107` → REQ-ICD-002 → REQ-FUNC-0007 |
| **Limitation** | Detection only; PROTO_ERROR remains reserved (LOT 3 D2); PARTIAL because REQ-FUNC-0007 also covers correlation identifiers not present in the frame. |

### TC-056: Stale Old-Term Heartbeat Carrying DEGRADED Metadata Is Rejected

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:265` (check_heartbeat_stale: stale term) → TC-056 → PASS (a real heartbeat frame captured from the former leader, replayed unchanged and with byte 3 set to DEGRADED and CRC recomputed, delivered with a 3 ms network delay after a crash and re-election at term 2: stale-term rejections +1 on each survivor per replay, no role, term, vote, evidence, state, leader identity or authority change) → IMPLEMENTED-SIM |
| **Reverse** | TC-056 → captured frame, delayed delivery through the network model → `mosaik_node.c:265` → PROTOCOL.md §7 (stale/replay immunity) → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | One old-term value (term 1 against term 2); semantic rejection only, no cryptographic anti-replay. |

### TC-057: Legitimate Leader Change While DEGRADED

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-SAFE-0004, REQ-FUNC-0004 → REQ-SAF-003, REQ-PERF-001 → L4:svc_mosaik_proto → `mosaik_node.c:128` (evidence freshness), `170` (become_leader keeps DEGRADED), `185` (cold restart clears evidence), `414` (DEGRADED exit) → TC-057 → PASS (DEGRADED leader crashed and cold-restarted while the SAFE node kept announcing; new leader node 1 term 3 elected DEGRADED; surviving follower never returned to NOMINAL; restarted node entered DEGRADED only with mask 0x02, i.e. received evidence; evidence masks name the SAFE node only although every survivor frame carried DEGRADED metadata; 0 illegal pairs; max valid authorities 1; 0 term regressions) → IMPLEMENTED-SIM |
| **Reverse** | TC-057 → split-brain SAFE, leader crash, cold restart, re-election → `mosaik_node.c:128, 170, 185, 414` → REQ-SAF-003 / REQ-PERF-001 → REQ-SAFE-0004 / REQ-FUNC-0004 |
| **Limitation** | One deterministic leader change; recovery time is an observation. |

### TC-058: Partition, Crash, Re-Election and Cold Restart Legality

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-FUNC-0001 (uniqueness); cold-restart property → REQ-FUN-001 → L4:svc_mosaik_proto → `mosaik_node.c:185` (mosaik_init: INIT, term 0, empty evidence), `433` (lease expiry step-down), `471` (election start) → TC-058 → PASS (600 ms leader partition: authority lost at lease expiry, cluster recovered after the heal without SAFE; leader crash: re-election, cold restart returns INIT/FOLLOWER, term 0, mask 0, rejoin NOMINAL; with a SAFE node present a DEGRADED follower's cold restart cleared its evidence and DEGRADED was re-acquired only from received SAFE frames; 0 illegal pairs; 0 SAFE authority or role violations; max valid authorities 1; 0 term regressions excluding cold restarts) → IMPLEMENTED-SIM |
| **Reverse** | TC-058 → partition, heal, crash, cold restart, split-brain SAFE → `mosaik_node.c:185, 433, 471` → REQ-FUN-001 → REQ-FUNC-0001 |
| **Limitation** | The 600 ms partition duration is below the SAFE latch boundary of TC-042 by construction; a longer partition latches the leader SAFE, which TC-042 covers. |

### TC-059: Lower-Term SAFE Announcement, Evidence Only

| Direction | Trace |
|-----------|-------|
| **Forward** | REQ-SAFE-0004 → REQ-SAF-003 → L4:svc_mosaik_proto → `mosaik_node.c:256` (no adoption of a lower term either), `375` (SAFE handler) → TC-059 → PASS (split-brain SAFE node at term 1, surviving cluster at term 2; 10 SAFE frames delivered in a 1000 ms window; evidence recorded on both survivors; both DEGRADED at every step; healthy terms unchanged; the same valid leader at every step; SAFE sender stayed SAFE, follower, own term, SAFE frames only) → IMPLEMENTED-SIM |
| **Reverse** | TC-059 → genuine periodic announcements of a lower-term SAFE node → `mosaik_node.c:256, 375` → REQ-SAF-003 → REQ-SAFE-0004 |
| **Limitation** | Passing at both `58a1b5d` and `ae9e408`: a lower term was never adopted; this test guards the direction C2 did not change. |

### TC-060: Determinism of the TC-052 and TC-053 Scenarios

| Direction | Trace |
|-----------|-------|
| **Forward** | NONE (reproducibility evidence) → per-node xorshift RNG seeded by id, integer-only simulation → TC-060 → PASS (run A equals run B in per-step roles, states, terms, authority, evidence masks, transmit counts and event times for both scenarios; hashes 0x19E7D8FF and 0xEA9EE0E6 at `ae9e408`, 0x5DE7D8FF and 0x70EE8189 at `58a1b5d`) → IMPLEMENTED-SIM |
| **Reverse** | TC-060 → the TC-052 and TC-053 scenario drivers run twice → deterministic core and harness → NONE |
| **Limitation** | Reproducibility of the host model only; the comparison is run against run, independent of the RED properties. |

---

## 9. LOT 5 Test Cases Traceability (TC-061 through TC-090)

Validated at commit `9ccd28e867d74cb9667addb5676ad009fa849a07` (761 checks,
0 failures, ASan/UBSan clean). RED evidence at `02b27fa` is recorded per
test. All evidence is IMPLEMENTED-SIM: deterministic host demonstrator only.
The eight INV-RECONFIG invariants are HIL-derived experimental invariants
with no normative ADD identifier; see the policy note in section 4.

### Phase 1, characterisation and RED baseline (`02b27fa`)

| Test | Checks | Property | History |
|---|---|---|---|
| TC-061 | 11 | membership is an explicit committed mask with its own epoch; `cluster_size` is inert | characterisation |
| TC-062 | 7 | peer loss is not membership removal | guard |
| TC-063 | 7 | a minority partition cannot reconfigure itself into a quorum | guard |
| TC-064 | 15 | a membership change is proposed, agreed and committed | RED at `02b27fa` (2 checks) |
| TC-065 | 12 | a reduction affects quorum only once both configurations agreed | RED at `02b27fa` (5 checks) |
| TC-066 | 11 | a removed node does not regain voting by cold restart | RED at `02b27fa` (2 checks) |
| TC-067 | 15 | stale, duplicate and future configuration traffic move nothing | RED at `02b27fa` (1 check) |
| TC-068 | 9 | partition during a transition yields no incompatible authority | RED at `02b27fa` (1 check) |
| TC-069 | 9 | LOT 2 authority and lease semantics unchanged | guard |
| TC-070 | 3 | determinism of the reconfiguration scenarios | guard |

### Phase 2, functional validation (`146472f`)

| Test | Checks | Property |
|---|---|---|
| TC-071 | 9 | conflicting successors of one epoch cannot both be agreed |
| TC-072 | 9 | every transaction stage is idempotent and order-insensitive |
| TC-073 | 19 | malformed, empty, single-node and proposer-excluding memberships refused; epoch does not wrap |
| TC-074 | 19 | proposer failure before agreement, after an undelivered commit, after a partial commit, and past the election budget |
| TC-075 | 12 | removed-node traffic restores no voting, authority or lease |
| TC-076 | 17 | every permitted transition, including re-admission and a two-change transition |
| TC-077 | 11 | host-model configuration store: pending binding, committed configuration, corrupted content |
| TC-078 | 17 | transaction alongside SAFE, DEGRADED, an election and a lease expiry |

### Phase 3, adversarial campaign (`9ccd28e`, test-only)

| Test | Checks | Attack category | Schedules |
|---|---|---|---|
| TC-079 | 2 | partial COMMIT delivery x partitions x crash points | 1536 |
| TC-080 | 11 | CONFIG loss, duplication, delay, reordering, stage inversion | targeted |
| TC-081 | 2 | proposer crash at 10 points x 4 restart conditions | 40 |
| TC-082 | 4 | acceptor crash around its acceptance | 6 |
| TC-083 | 10 | removed-node frame attacks; missed-epoch safety | 10 stuck cases |
| TC-084 | 13 | re-admission isolated to a single step; restarts either side | targeted |
| TC-085 | 14 | two different successors of one epoch | targeted |
| TC-086 | 8 | configuration epoch and leadership term cross product | targeted |
| TC-087 | 10 | lease evidence attacks; exact freshness boundary | targeted |
| TC-088 | 14 | SAFE and DEGRADED at four transaction stages | 4 stages |
| TC-089 | 4 | one-millisecond timer boundaries | 6 |
| TC-090 | 2 | bounded deterministic schedule explorer | 4608 |

Total committed adversarial schedules: **6236**. Every Phase-3 scenario runs
under a continuous invariant oracle evaluated at each simulated millisecond.
No safety counterexample was observed. Two liveness limitations are recorded
by the tests themselves: see `LOT5_RECONFIGURATION_REPORT.md` section 29.

### Invariant coverage

| Invariant | Class | Covered by |
|---|---|---|
| INV-RECONFIG-NO-MAGIC | HIL-derived | TC-062, TC-063, TC-073, TC-077, TC-080, TC-083 |
| INV-RECONFIG-CONSISTENT | HIL-derived | TC-064, TC-065, TC-071, TC-082, TC-085 |
| INV-RECONFIG-AUTHORITY | HIL-derived | TC-063, TC-073, TC-075, TC-083, TC-084 |
| INV-RECONFIG-QUORUM | HIL-derived | TC-061, TC-062, TC-064, TC-066, TC-083 |
| INV-RECONFIG-PARTITION | HIL-derived | TC-063, TC-073, TC-077 |
| INV-RECONFIG-OLD-CONFIG | HIL-derived | TC-067, TC-072, TC-073, TC-080 |
| INV-RECONFIG-REMOVED-NODE | HIL-derived | TC-065, TC-066, TC-075, TC-083, TC-084, TC-087 |
| INV-RECONFIG-TRANSITION | HIL-derived | TC-065, TC-068, TC-071, TC-074, TC-077, TC-081, TC-086 |
| INV-LEADER-UNIQUE | inherited, REQ-FUNC-0001 | TC-062..TC-090, continuously in Phase 3 |
| INV-SAFE-NO-AUTHORITY | inherited, REQ-SAFE-0003 | TC-063, TC-078, TC-088, Phase-3 oracle |
| INV-SAFE-LATCH | inherited, REQ-SAFE-0003 | TC-078, TC-088, Phase-3 oracle |
| INV-NO-STALE-RECOVERY | inherited | TC-067, TC-084 |
| INV-TERM-MONOTONIC | inherited | TC-064, TC-086, Phase-3 oracle |
| INV-ONE-VOTE-PER-TERM | inherited | TC-075, TC-084, TC-086 |
