# MOSAÏK Development Roadmap

**Document:** MOSAIK-ROADMAP-001  
**Issue:** 1.4 — 20 September 2026  
**Parent:** MOSAIK-ADD-0001 (Issue 1 / Rev 1, dated 24 April 2026)

---

## 1. Purpose

This roadmap defines the controlled, incremental development of the MOSAÏK distributed avionics architecture from the Architectural Design Document (ADD) baseline through to a reproducible demonstrator release. Each Lot represents a verifiable increment with documented requirements, implementation, tests, evidence, regression status, and known limitations.

**Governance rule:** A Lot may only close when its requirements, implementation, tests, evidence, regression status, and limitations are documented.

---

## 2. Lot Families

| Lot | Title | Scope | Status |
|-----|-------|-------|--------|
| **LOT 0** | ADD Baseline, Repository Architecture, Traceability | Governance, requirements transcription, architecture documentation, bidirectional traceability, ADD findings | **COMPLETE (with open tracked findings)** |
| **LOT 1** | Core Protocol Foundations | Wire protocol, frame format, CRC, identifiers, basic state machine | NOT STARTED |
| **LOT 2** | Distributed Leadership and Authority | Leader election, quorum, lease, partitions, recovery | PARTIAL |
| **LOT 2A** | Leader Lease / 2+1 Partition | 500 ms leadership lease, explicit valid authority, deterministic 2+1 partition test | **IMPLEMENTED (host sim)** |
| **LOT 2B** | Stale/Delayed/Replayed Message Immunity | Message freshness, sequence numbers, replay protection | **IMPLEMENTED (host sim)** |
| **LOT 2C** | Asymmetric Partitions, Loss, Delay, Reorder | Directional fault model (deliver/drop/delay/reorder), ACK-based lease evidence, TC-013–TC-020 | **IMPLEMENTED (host sim)** — no dedicated report yet |
| **LOT 2D** | Crash/Restart/Recovery | Crash and cold-restart models, candidate retry backoff after split vote, TC-021–TC-034 | **IMPLEMENTED (host sim)** — persistent terms NOT implemented (cold restart only) |
| **LOT 3** | FDIR and SAFE | SAFE contract, DEGRADED from received peer SAFE evidence, recovery around a SAFE node, TC-035–TC-050; PROTO_ERROR policy, local fault input, SAFE_ASSERT and PGA deferred | **IMPLEMENTED (host sim)** |
| **LOT 4** | Local Mode Semantics (four-state host model) | INIT, NOMINAL, DEGRADED, SAFE × FOLLOWER, CANDIDATE, LEADER: election does not degrade (C1), SAFE announcement term is not consensus evidence (C2), state metadata non-authoritative, TC-051–TC-060; ADAPTIVE and PGA dependency-blocked, deferred to MOSAÏK Advanced | **IMPLEMENTED (host sim)** |
| **LOT 5** | Autonomous Reconfiguration | Quorum reconfiguration, membership changes: committed membership mask, configuration epoch, PROPOSE/ACCEPT/COMMIT transaction with joint old-and-new quorum, membership-aware elections and lease, removed-node exclusion, host-model configuration store, TC-061–TC-090 | **CLOSED — HOST DEMONSTRATOR** |
| **LOT 6** | CAN-FD / Communications / ICD | Physical layer, bitrates, ICD, bus-off handling | NOT STARTED |
| **LOT 7** | Embedded Services | Logger, time, file system, health monitoring | NOT STARTED |
| **LOT 8** | EN/CN/COMN Multi-Node Architecture | Full 6-node architecture (3 EN, CN, COMN) | NOT STARTED |
| **LOT 9** | Adversarial and Campaign Verification | Systematic fault injection, statistical campaigns | NOT STARTED |
| **LOT 10** | Formal Verification | Model checking, proof of critical invariants | NOT STARTED |
| **LOT 11** | STM32H743 / FreeRTOS Port | Target platform port, RTOS integration | NOT STARTED |
| **LOT 12** | HIL MicroLab | Hardware-in-the-loop test infrastructure | NOT STARTED |
| **LOT 13** | MOSAÏK V1 Hardware | Flight-representative hardware procurement/integration | NOT STARTED |
| **LOT 14** | System Verification Campaign | Measured timing evidence on hardware (Level 2) | NOT STARTED |
| **LOT 15** | Reproducible MOSAÏK Demonstrator Release | Packaged release with complete evidence | NOT STARTED |

---

## 3. Dependency Rules

- LOT 0 must complete before any other Lot can formally close
- LOT 1 is prerequisite for LOT 2 family
- LOT 2A is complete (host simulation only)
- LOT 2B–2D depend on LOT 1 and LOT 2A
- LOT 3–5 depend on LOT 2 family
- LOT 6 is prerequisite for LOT 11–14
- LOT 7–8 depend on LOT 6
- LOT 9–10 depend on LOT 2–5
- LOT 11–13 depend on LOT 6–8
- LOT 14 depends on LOT 12–13
- LOT 15 depends on all prior Lots

---

## 4. Evidence Classification

All evidence in this repository uses these classifications:

| Code | Meaning |
|------|---------|
| **CbD** | Compliant-by-Design (architectural enforcement) |
| **CbA** | Compliant-by-Analysis (mathematical/static analysis) |
| **CbT** | Compliant-by-Test on **physical hardware** |
| **IMPLEMENTED-SIM** | Implemented and tested in host simulation only |
| **PARTIAL** | Partially implemented / partially evidenced |
| **DESIGN-ONLY** | Documented in architecture, not implemented |
| **NOT-STARTED** | No work begun |
| **HARDWARE-REQUIRED** | Cannot be evidenced without physical hardware |
| **TBC** | To Be Confirmed / conflict under investigation |

**Current repository status:** Only IMPLEMENTED-SIM, PARTIAL, and DESIGN-ONLY claims are valid. No CbT claims exist. No TRL 4 claim.

---

## 5. Milestone Definitions

| Milestone | Criteria |
|-----------|----------|
| **LOT 0 Complete** | All governance docs created, traceability established, ADD findings documented, zero regression |
| **LOT 1 Complete** | Protocol spec frozen, codec verified, basic state machine implemented |
| **LOT 2 Family Complete** | All leadership/partition invariants implemented and tested in simulation |
| **LOT 6 Complete** | CAN-FD physical layer validated on target hardware |
| **LOT 11 Complete** | Full firmware builds on STM32H743/FreeRTOS |
| **LOT 12 Complete** | HIL MicroLab operational with 6 nodes |
| **LOT 14 Complete** | 30-run measured campaigns for REQ-004, REQ-005 on hardware |
| **LOT 15 Complete** | All evidence packaged, reproducible build, release tagged |

---

## 6. Current Baseline (LOT 2A through LOT 5)

**Verified commit:** `9ccd28e867d74cb9667addb5676ad009fa849a07`  
**Branch:** `lot2c-network-adversarial`  
**Tests:** 90 test cases (TC-001 through TC-090)  
**Checks:** 761 checks, 0 failures  
**Compiler:** `-std=c99 -Wall -Wextra -Werror -O1` PASS  
**Sanitizers:** AddressSanitizer + UndefinedBehaviorSanitizer PASS, 0 findings  
**Invariants:** maximum concurrent valid authorities 1; term regressions 0  
**History:** `c6f600b` harness lease evidence corrected; `d38985d` RED baseline 201 checks / 7 failures (TC-028 pre-existing, TC-031 intentional); `f4e0f3c` candidate retry backoff, 205 / 0; `76f10d9` LOT 2 closure; `af5da87` LOT 3 RED baseline 360 checks / 13 failed checks in exactly TC-039, TC-040, TC-045, TC-047, TC-048, TC-050; `034db92` LOT 2 same-term vote-memory erratum corrected, TC-050 green; `7df0af0` DEGRADED evidence semantics, 360 / 0; `8177e70` LOT 3 closure; `58a1b5d` LOT 4 RED baseline 455 checks / 7 failed checks in exactly TC-052 (2) and TC-053 (5); `ae9e408` election no longer degrades and SAFE term no longer adopted, 455 / 0; `7d35cc8` LOT 4 closure; `02b27fa` LOT 5 RED baseline 515 checks / 11 failed checks in exactly TC-064, TC-065, TC-066, TC-067 and TC-068; `146472f` LOT 5 membership reconfiguration implemented, 667 / 0; `9ccd28e` LOT 5 adversarial campaign, test-only, 761 / 0. See `LOT2D_CRASH_RECOVERY_REPORT.md`, `LOT3_FDIR_SAFE_REPORT.md`, `LOT4_MODE_SEMANTICS_REPORT.md` and `LOT5_RECONFIGURATION_REPORT.md`.  
**LOT 5 closure lineage:** RED `02b27faa55d0e437337cb3d513a1d9683797416a` (70 / 515 / 11); GREEN `146472f888b3f784fe0de25bd5c8e5cc39a6d5c3` (78 / 667 / 0, the only LOT 5 commit that changed firmware); adversarial `9ccd28e867d74cb9667addb5676ad009fa849a07` (90 / 761 / 0, `test/test_mosaik.c` only); documentation closure, this commit. Evidence is bounded deterministic three-node host evidence: **not** a formal proof, **not** validation for arbitrary cluster sizes, **not** hardware validation. Within the bounded adversarial state space executed by TC-061 to TC-090, no safety counterexample was observed. Two liveness limitations remain open and are documented in `LOT5_RECONFIGURATION_REPORT.md` section 29.

**Limitations:** Host deterministic simulation only; 3-node topology; 500 ms simulated lease; semantic stale/replay rejection only (no cryptographic anti-replay); no term, vote or SAFE persistence (LOT 5 adds a host-model configuration store for the committed membership only); split vote possible, only its lock-step persistence addressed; sub-millisecond bus races not modelled; no physical CAN-FD validation; no HIL; no TRL 4; SAFE latched within one powered node instance only (cleared by cold restart); PROTO_ERROR reserved, not implemented; no PGA, discretes, watchdog or hardware FDIR; four local states only, ADAPTIVE and PGA not implemented; this bench is not the complete ADD implementation (planned separately as MOSAÏK Advanced).

Previous baseline (LOT 2A through LOT 3): commit `7df0af01d6ae2120bce9a5c6378305e3ef7eeb5c`, 50 test cases, 360 checks, 0 failures.

Previous baseline (LOT 2A through LOT 2D): commit `f4e0f3c1606766ac9b5b3332964e3cdbe5f1e2ea`, 34 test cases, 205 checks, 0 failures.

Previous baseline (LOT 2A + LOT 2B): commit `806646a0a17803e70fff7bc65b6bf45eee43e6f2`, branch `lot2b-stale-replay-immunity`, 12 test cases, 55 checks, 0 failures.

---

## 7. Governance Notes

- No Lot modifies behavior of a previous Lot without explicit regression verification
- No weakening of safety assertions to make tests pass
- All findings documented in `ADD-FINDINGS.md` with explicit status
- Simulation evidence never converted to hardware compliance
- No merge to `main` until LOT 15 release criteria met

---

## 8. LOT 0 Closure Status

**LOT 0 establishes the controlled baseline and traceability framework.**

**LOT 0 does NOT mean:**
- every ADD requirement is implemented
- every ADD inconsistency is resolved
- hardware is validated
- HIL is complete
- system qualification is complete
- TRL 4 has been achieved

**Open findings are allowed to remain open if:**
- they are explicitly documented
- their impact is known
- they have a planned closure Lot
- they do not prevent the next software development Lot

**LOT 0 is therefore CLOSED WITH OPEN TRACKED FINDINGS.**

### Open Tracked Findings (Non-Blocking for LOT 1+)

| ID | Title | Class | Planned Closure |
|----|-------|-------|-----------------|
| ADD-F001 | Requirement Namespace Mismatch | TRACEABILITY-GAP | Architecture review |
| ADD-F002 | Leader Uniqueness Scope | ADD-INTERNAL | Architecture review / LOT 10 |
| ADD-F003 | System Mode Terminology | TRACEABILITY-GAP, IMPLEMENTATION-GAP | LOT 4 closed the four-state local semantics (host); ADAPTIVE and PGA remain open, MOSAÏK Advanced |
| ADD-F012 | Election Start Conflated with FDIR Degradation | IMPLEMENTATION-GAP | Resolved in LOT 4 (host), commit `ae9e408` |
| ADD-F013 | SAFE Announcement Term Treated as Consensus Epoch | ADD-INTERNAL, IMPLEMENTATION-GAP | Host interpretation implemented in LOT 4; architecture review |
| ADD-F004 | CAN-FD Bitrate — Protocol Model vs Physical Validation | IMPLEMENTATION-GAP, VERIFICATION-GAP | LOT 6, 12, 14 |
| ADD-F005 | Environmental Requirements | VERIFICATION-GAP | LOT 13, 14 |
| ADD-F006 | Test ID / Requirement Mapping Inconsistencies | TRACEABILITY-GAP | Ongoing |
| ADD-F007 | Correlation_ID in Critical Messages | IMPLEMENTATION-GAP | LOT 6 |
| ADD-F008 | Quorum Definition — Voting Membership | ADD-INTERNAL, IMPLEMENTATION-GAP | HIL interpretation implemented and validated in LOT 5 for the three-node demonstrator; general ADD voting membership still open, LOT 8 |
| ADD-F009 | SAFE Exit / PGA | IMPLEMENTATION-GAP | LOT 8 (LOT 3 closed with the software latch documented; PGA not implemented) |
| ADD-F011 | DEGRADED Exit / Freshness Semantics | ADD-INTERNAL, IMPLEMENTATION-GAP | Host interpretation implemented in LOT 3; architecture review |
| ADD-F010 | Heartbeat Root/Derived Timing Traceability | TRACEABILITY-GAP | Architecture review / LOT 6 |

**No blocking findings for LOT 1 start.**