# LOT 2A Leader Lease Fix — Evidence Report

> **Status note (16 September 2026).** Sections 4.4 and 5 describe lease
> renewal "on heartbeat delivery to quorum". That harness behaviour was
> superseded at commit `c6f600b`: the lease now renews only on an
> acknowledgement actually received by the leader in its current term, and
> outbound heartbeat delivery alone never renews it. See
> `LOT2D_CRASH_RECOVERY_REPORT.md` section 6 and PROTOCOL.md issue 0.4.
> The rest of this report is kept as the historical LOT 2A record.

**Document:** LOT2A_LEADER_LEASE_REPORT.md
**Date:** 14 September 2026
**Author:** Implementation Engineer
**Branch:** lot2-distributed-safety-core
**Starting SHA:** b9097d47ad3a22034c09833282bc368b0c69fcc2

---

## 1. Defect Description

The MOSAIK host implementation (pre-Lot 2A) allowed an isolated leader to retain leadership indefinitely during a 2+1 network partition. Under a 3-node cluster with symmetric partition isolating one node from the other two:

- The isolated old leader continued sending heartbeats (never receiving conflicting messages)
- The two-node majority elected a new leader (achieving quorum)
- Both nodes simultaneously held `ROLE_LEADER`, violating REQ-002 (exactly one active leader)

The root cause was the absence of a **leadership lease** mechanism. A leader that could not maintain evidence of majority connectivity had no obligation to relinquish authority.

---

## 2. Root Cause

In `mosaik_node.c`, the leader role (`ROLE_LEADER`) was permanent once acquired, only revoked by:
- Receiving a higher term
- Detecting a same-term peer leader (split-brain → SAFE)

No timeout or lease expiry existed. An isolated leader never received higher terms or peer heartbeats, so it remained leader forever.

---

## 3. Files Modified

| File | Changes |
|------|---------|
| `firmware/core/mosaik_node.h` | Added `MOSAIK_LEADERSHIP_LEASE_MS` (500), `last_quorum_contact_ms`, `lease_expiry_ms` to `mosaik_node_t`; added `mosaik_has_valid_leadership_authority()` declaration |
| `firmware/core/mosaik_node.c` | Implemented lease tracking in `mosaik_init`, `become_leader`, `mosaik_tick`; added `mosaik_has_valid_leadership_authority()` definition; leader steps down on lease expiry |
| `test/test_mosaik.c` | Replaced global `bus_up` with pairwise `connectivity[3][3]`; added `bus_set_partition_2plus1()`; lease renewal on heartbeat delivery; new test `tc_007_partition_lease_expiry()` |
| `PROTOCOL.md` | Added leadership lease (500 ms) to timing parameters; documented lease semantics and valid leadership authority predicate |
| `docs/TEST-PLAN.md` | Added TC-007; updated limitations (host-model only, no hardware validation) |
| `README.md` | Updated test count (22 checks, 7 tests); documented lease and partition test |

---

## 4. Implementation Description

### 4.1 Leadership Lease Concept

- **Nominal lease:** 500 ms simulated time (`MOSAIK_LEADERSHIP_LEASE_MS`)
- **Renewal:** At 2 Hz (every 500 ms) — triggered when leader's heartbeat reaches quorum (≥1 other node in 3-node cluster)
- **Expiry action:** Leader steps down to follower (`become_follower()`), losing both `ROLE_LEADER` and valid authority
- **Valid authority predicate:** `mosaik_has_valid_leadership_authority(node)` = `role == LEADER && now_ms < lease_expiry_ms`

### 4.2 Distinction: ROLE_LEADER vs LEADERSHIP_AUTHORITY_VALID

- `mosaik_is_leader()` → returns `role == MOSAIK_ROLE_LEADER` (backward compatible)
- `mosaik_has_valid_leadership_authority()` → returns true only if leader role held AND lease valid
- Safety-critical decisions must use the latter

### 4.3 Follower Election Timeout Alignment

Followers now set their election deadline to `last_hb_rx_ms + MOSAIK_LEADERSHIP_LEASE_MS + jitter` (0–49 ms) upon receiving a heartbeat. This ensures followers do not challenge the leader before its lease expires, preventing split-vote during partition.

### 4.4 Test Harness: Pairwise Connectivity Model

```c
bool connectivity[N_NODES][N_NODES];  // connectivity[from][to]
```

Supports:
- Full connectivity (default)
- Symmetric 2+1 partition (`bus_set_partition_2plus1(isolated_node)`)
- Partition healing (`bus_set_full_connectivity()`)

Lease renewal occurs only when a leader's heartbeat is **delivered** to at least one other powered node, aligning with follower deadline reset.

---

## 5. Lease Semantics

| Event | Lease Effect |
|-------|-------------|
| Node becomes leader (quorum votes) | `lease_expiry_ms = now_ms + 500` |
| Leader's heartbeat delivered to quorum | `lease_expiry_ms = now_ms + 500` (renewal) |
| Leader isolated (no heartbeat delivery) | Lease not renewed; expires 500 ms after last delivery |
| Lease expires (`now_ms >= lease_expiry_ms`) | Leader calls `become_follower(term)`; loses role and authority |
| New leader elected in majority | Gains valid authority immediately (heartbeat delivered to quorum) |

---

## 6. INV-LEADER-UNIQUE Definition

> **INV-LEADER-UNIQUE:** At every simulated observation point, `valid_leader_count ≤ 1`, where `valid_leader_count` is the number of nodes for which `mosaik_has_valid_leadership_authority()` returns true.

This invariant is checked at **every millisecond** in TC-007, not just at steady state.

---

## 7. 2+1 Partition Scenario (TC-007)

**Scenario:**
1. Start 3 healthy fully connected nodes
2. Allow stable leader election (node 2, term 1)
3. At `partition_time = 2000 ms`, isolate leader (node 2) from nodes 1 and 3
4. Continue simulation, tracking at every step:
   - `valid_leader_count`
   - Old leader authority expiry
   - New majority leader valid authority acquisition

**Measured Simulated Event Times (deterministic run):**

| Event | Simulated Time | Delta from Partition |
|-------|----------------|---------------------|
| Partition created | 2000 ms | 0 ms |
| Old leader authority expiry | 2416 ms | 416 ms |
| New majority leader valid | 2452 ms | 452 ms |
| Maximum concurrent valid leaders | **1** | — |

**Note:** The old leader's authority expires at 416 ms post-partition (not 500 ms) because its last heartbeat delivery was at 1916 ms (heartbeat period 100 ms, leader elected at ~1916 ms). The lease expires 500 ms after last delivery: 1916 + 500 = 2416 ms. The new leader gains valid authority at 2452 ms after election in the majority partition.

**Critical Result:** `maximum_concurrent_valid_leaders = 1` — **INV-LEADER-UNIQUE SATISFIED**

---

## 8. Test Results

### Original Regression Tests (TC-001 through TC-006)

| Test | Requirement | Checks | Result |
|------|-------------|--------|--------|
| TC-001 | REQ-002 | 1 | PASS |
| TC-002 | REQ-002 | 1 | PASS |
| TC-003 | REQ-004 | 3 | PASS |
| TC-004 | REQ-005, REQ-002 | 3 | PASS |
| TC-005 | REQ-003 | 3 | PASS |
| TC-006 | — | 5 | PASS |
| **Total** | | **16** | **ALL PASS** |

### New Test (TC-007)

| Test | Requirement | Checks | Result |
|------|-------------|--------|--------|
| TC-007 | REQ-002, Lot 2A | 6 | PASS |

### Overall Summary

| Metric | Value |
|--------|-------|
| Original tests | 6 |
| New tests | 1 (TC-007) |
| Total tests | 7 |
| Original checks | 16 |
| New checks | 6 |
| **Total checks** | **22** |
| **Failures** | **0** |

---

## 9. Sanitizer Results

| Sanitizer | Command | Result |
|-----------|---------|--------|
| AddressSanitizer + UndefinedBehaviorSanitizer | `CC=clang CFLAGS="-fsanitize=address,undefined" make test` | **PASS** — No memory errors, no undefined behavior detected |

---

## 10. Known Limitations

1. **Host software demonstrator only** — The leadership lease (500 ms) and partition test are simulated on a virtual bus. No physical CAN-FD timing validation is performed.

2. **No hardware validation** — TC-007 uses a simulated pairwise connectivity model. Real network partitions involve transceiver behavior, bus arbitration, and clock drift not modeled here.

3. **Deterministic simulation** — The xorshift RNG seeded by node ID ensures reproducible test runs but does not cover all timing interleavings possible on hardware.

4. **Lease renewal granularity** — Lease is renewed on discrete heartbeat delivery (every 100 ms). The 500 ms lease may expire up to 100 ms after actual partition if the last heartbeat was delivered just before partition.

5. **No TRL 4 claim** — This is a logic verification bench (TRL 3). Measured timing evidence requires Level 2 hardware campaign.

---

## 11. Required Statement

**HOST SOFTWARE DEMONSTRATOR ONLY — NO HARDWARE VALIDATION**

Do not claim TRL 4.

---

## 12. Verification Checklist

- [x] `git diff --check` — no whitespace errors
- [x] `git status --short` — only intended files modified
- [x] `git diff --stat` — changes confined to 6 files
- [x] `make clean && make test` — all 22 checks pass, 0 failures
- [x] Sanitizer build (ASan + UBSan) — clean
- [x] No weakened assertions (original tests unchanged except TC-003 latency increased due to lease-aligned election timeout)
- [x] No dead code
- [x] No compiler warnings (`-Wall -Wextra -Werror`)
- [x] Lease boundary conditions correct (leader steps down at expiry)
- [x] No hidden split-brain intervals (max concurrent valid leaders = 1 at all observation points)
- [x] Test verifies invariant at every step, not just final state