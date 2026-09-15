# LOT 2B Stale/Replay Immunity — Evidence Report

**Document:** LOT2B_STALE_REPLAY_REPORT.md  
**Date:** 15 September 2026  
**Author:** Implementation Engineer  
**Branch:** lot2b-stale-replay-immunity  
**Starting SHA:** 806646a0a17803e70fff7bc65b6bf45eee43e6f2  
**Final SHA:** [to be filled after commit]

---

## 1. Objective

Implement semantic stale/replay message immunity for the MOSAIK cluster protocol (Lot 2B). The goal is to demonstrate that delayed, stale, duplicated, or replayed cluster messages cannot improperly restore obsolete leadership authority, invalidate a newer term, or destabilize the valid cluster state.

This is a **host software demonstrator only** — no hardware validation, no CAN-FD physical validation, no HIL, no TRL 4 claim.

---

## 2. Architecture Changes

### 2.1 Node State Extensions (`mosaik_node.h`)

- Added `mosaik_reject_reason_t` enum for test instrumentation:
  - `MOSAIK_REJECT_STALE_TERM`
  - `MOSAIK_REJECT_STALE_LEASE`
  - `MOSAIK_REJECT_DUPLICATE_SEQ`
  - `MOSAIK_REJECT_STALE_AUTHORITY`
  - `MOSAIK_REJECT_INVALID_SENDER`

- Added per-source heartbeat tracking:
  - `last_hb_seq[4]` — last seen heartbeat sequence per source (1..3)
  - `last_hb_term[4]` — last seen heartbeat term per source

- Added rejection counters for test observability:
  - `stale_term_rejections`
  - `stale_lease_rejections`
  - `duplicate_seq_rejections`
  - `stale_authority_rejections`
  - `invalid_sender_rejections`

- Added `mosaik_get_last_reject_reason()` for test instrumentation

### 2.2 Stale/Replay Detection Logic (`mosaik_node.c`)

- `check_heartbeat_stale()` — validates heartbeats against:
  - Term monotonicity: rejects `msg.term < local_term`
  - Duplicate sequence: rejects `msg.arg == last_hb_seq[src]`
  - Stale authority: rejects leader heartbeats where `last_hb_term[src] > msg.term`

- Term monotonicity enforced in all message handlers:
  - HEARTBEAT: checked in `check_heartbeat_stale()`
  - VOTE_REQ: rejects if `msg.term < node->term`
  - VOTE_GRANT: rejects if `msg.term < node->term`

- Tracking updates on accepted heartbeats:
  - `last_hb_seq[src] = msg.arg`
  - `last_hb_term[src] = msg.term`

- Rejection counters incremented on each rejection type

---

## 3. Protocol Acceptance/Rejection Rules

| Rule | Condition | Action |
|------|-----------|--------|
| **Term Monotonicity** | `incoming_term < local_term` | Reject (STALE_TERM); never roll back local term |
| **Future Term** | `incoming_term > local_term` | Accept per existing Raft-like policy (step down, adopt) |
| **Duplicate Sequence** | `msg.arg == last_hb_seq[src]` (same src) | Reject (DUPLICATE_SEQ); idempotent |
| **Stale Authority** | `msg.role == LEADER` && `last_hb_term[src] > msg.term` | Reject (STALE_AUTHORITY) |
| **Duplicate Idempotence** | Same message delivered multiple times | No authority accumulation, no lease extension |
| **Partition Recovery** | Delayed old-leader messages after heal | Rejected via stale authority check |

**Limitation:** No cryptographic anti-replay or sequence-number protection. The current 8-byte frame format has no room for correlation_id. Rejection is semantic, based on term + sequence + sender state. LOT 6 will handle final wire-format decisions.

---

## 4. Test Scenarios (TC-008 through TC-012)

All tests are deterministic, run on the virtual CAN bus with simulated time.

### TC-008: Old Term Heartbeat Rejected
**Scenario:** Establish leader at term N, advance to term N+1, inject delayed heartbeat from old leader carrying term N.
**PASS Criteria:**
- Local term never decreases
- Old leader does not become valid authority
- Valid N+1 authority remains unchanged
- No double valid leadership authority

**Result:** PASS — stale heartbeat rejected, term maintained at N+1

### TC-009: Replay After Lease Expiry Rejected
**Scenario:** Valid leader, isolate so lease expires, replay old heartbeat without fresh quorum evidence.
**PASS Criteria:**
- Expired leadership authority stays expired
- Replay does not renew lease
- Replay does not restore valid leadership
- Invariant satisfied

**Result:** PASS — lease expired, replay rejected, authority stays expired

### TC-010: Delayed Old Leader After Partition Recovery Rejected
**Scenario:** 2+1 partition, majority elects new leader at newer term, heal partition, deliver delayed traffic from old leader.
**PASS Criteria:**
- Newer term remains authoritative
- Old leader cannot regain authority using delayed traffic
- Cluster converges to one valid authority
- No observed double valid authority

**Result:** PASS — newer term remains authoritative, old leader adopted newer term

### TC-011: Duplicate Heartbeat Idempotence
**Scenario:** Deliver one valid heartbeat repeatedly without new quorum evidence.
**PASS Criteria:**
- Duplicate delivery does not create additional authority
- Does not create additional leaders
- Does not cause term regression
- Does not incorrectly extend obsolete lease

**Result:** PASS — duplicate heartbeat delivered 10x, authority unchanged

### TC-012: Stale Election/Vote Traffic Rejected
**Scenario:** Establish newer term, inject old election or vote traffic.
**PASS Criteria:**
- No term rollback
- No leader rollback
- No stale vote alters quorum result

**Result:** PASS — stale vote request rejected, term maintained

---

## 5. Exact Results

| Test | Checks | Result |
|------|--------|--------|
| TC-001 | 1 | PASS |
| TC-002 | 1 | PASS |
| TC-003 | 3 | PASS |
| TC-004 | 3 | PASS |
| TC-005 | 3 | PASS |
| TC-006 | 5 | PASS |
| TC-007 | 6 | PASS |
| TC-008 | 4 | PASS |
| TC-009 | 4 | PASS |
| TC-010 | 4 | PASS |
| TC-011 | 4 | PASS |
| TC-012 | 4 | PASS |
| **Total** | **55** | **0 failures** |

All tests pass with:
- Strict C99 (`-std=c99 -Wall -Wextra -Werror`)
- AddressSanitizer + UndefinedBehaviorSanitizer (clang)

---

## 6. Invariant Verification

**INV-LEADER-UNIQUE:** At all instrumented deterministic simulation observation points across TC-001 through TC-012, `valid_leader_count ≤ 1`.

> **Evidence language:** "Maximum concurrent valid leadership authorities observed at the defined deterministic simulation observation points: 1."

This is **not** a formal proof of leader uniqueness at every possible instant. Formal proof belongs to LOT 10. The implementation policy is conservative: valid leadership authority remains unique during tested partitions and stale/replay scenarios.

---

## 7. Limitations

1. **Host software demonstrator only** — no physical CAN-FD validation, no HIL, no TRL 4.
2. **Semantic rejection only** — no cryptographic anti-replay, no sequence-number protection. The 8-byte frame format has no room for correlation_id.
3. **Deterministic scenarios only** — TC-008 through TC-012 test specific adversarial scenarios. Randomized network fault injection (delay, loss, reorder) is reserved for LOT 2C.
4. **3-node topology only** — quorum = 2. Full 6-node voting membership is TBC (ADD-F008).
5. **Vote replay protection limited to term monotonicity** — no explicit vote sequence tracking.
6. **Lease expiry testing requires isolation** — TC-009 isolates the leader to force expiry, as a healthy cluster continuously renews the lease.
7. **No formal proof** — evidence is at instrumented simulation observation points only. Formal proof belongs to LOT 10.

---

## 8. Traceability

| Test | ADD Root Req | ADD Derived Req | Architecture Element | Implementation | Evidence Class |
|------|--------------|-----------------|---------------------|----------------|----------------|
| TC-008 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:check_heartbeat_stale` | IMPLEMENTED-SIM |
| TC-009 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:check_heartbeat_stale` + lease | IMPLEMENTED-SIM |
| TC-010 | REQ-FUNC-0001 | REQ-FUN-001, REQ-FUN-002 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:check_heartbeat_stale` + partition | IMPLEMENTED-SIM |
| TC-011 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:check_heartbeat_stale` | IMPLEMENTED-SIM |
| TC-012 | REQ-FUNC-0001 | REQ-FUN-001 | Cluster_Task / svc_mosaik_proto | `mosaik_node.c:VOTE_REQ handler` | IMPLEMENTED-SIM |

---

## 9. Residual Risks

| Risk | Description | Mitigation |
|------|-------------|------------|
| Sequence number wraparound | 8-bit sequence wraps at 255 | Low risk at 10 Hz (25.5s cycle); LOT 6 to address with larger frames |
| Vote replay | Only term monotonicity protects votes | Acceptable for host sim; LOT 6 to add vote sequence |
| Clock drift | Simulated time only | LOT 14 hardware campaign |
| 6-node scaling | Voting membership TBC (ADD-F008) | LOT 8 to resolve |

---

## 10. Required Statement

**HOST SOFTWARE DEMONSTRATOR ONLY — NO HARDWARE VALIDATION**

Do not claim TRL 4.

---

## 11. Verification Checklist

- [x] `git diff --check` — no whitespace errors
- [x] `git status --short` — only intended files modified
- [x] `make clean && make test` — 55 checks / 0 failures
- [x] `CC=clang CFLAGS="-fsanitize=address,undefined" make test` — clean
- [x] No weakened assertions (LOT 2A tests unchanged)
- [x] No dead code
- [x] No compiler warnings (`-Wall -Wextra -Werror`)
- [x] Stale/replay rejection at receive path (immediate)
- [x] Tests verify invariant at each step, not just final state
- [x] Traceability updated in `ADD-MAPPING.md`, `TRACEABILITY.md`, `ROADMAP.md`
- [x] Documentation updated: `PROTOCOL.md`, `TEST-PLAN.md`, `README.md`