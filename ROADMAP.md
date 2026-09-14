# MOSAÏK Development Roadmap

**Document:** MOSAIK-ROADMAP-001  
**Issue:** 1.0 — 15 September 2026  
**Parent:** MOSAIK-ADD-0001 (Issue 1 / Rev 1, dated 24 April 2026)

---

## 1. Purpose

This roadmap defines the controlled, incremental development of the MOSAÏK distributed avionics architecture from the Architectural Design Document (ADD) baseline through to a reproducible demonstrator release. Each Lot represents a verifiable increment with documented requirements, implementation, tests, evidence, regression status, and known limitations.

**Governance rule:** A Lot may only close when its requirements, implementation, tests, evidence, regression status, and limitations are documented.

---

## 2. Lot Families

| Lot | Title | Scope | Status |
|-----|-------|-------|--------|
| **LOT 0** | ADD Baseline, Repository Architecture, Traceability | Governance, requirements transcription, architecture documentation, bidirectional traceability, ADD findings | **IN PROGRESS** |
| **LOT 1** | Core Protocol Foundations | Wire protocol, frame format, CRC, identifiers, basic state machine | NOT STARTED |
| **LOT 2** | Distributed Leadership and Authority | Leader election, quorum, lease, partitions, recovery | PARTIAL |
| **LOT 2A** | Leader Lease / 2+1 Partition | 500 ms leadership lease, explicit valid authority, deterministic 2+1 partition test | **IMPLEMENTED (host sim)** |
| **LOT 2B** | Stale/Delayed/Replayed Message Immunity | Message freshness, sequence numbers, replay protection | NOT STARTED |
| **LOT 2C** | Asymmetric Partitions, Loss, Delay, Reorder | Generalized fault injection framework | NOT STARTED |
| **LOT 2D** | Crash/Restart/Recovery | Node restart, state recovery, persistent terms | NOT STARTED |
| **LOT 3** | FDIR and SAFE | Fault detection, isolation, recovery, SAFE mode behavior | NOT STARTED |
| **LOT 4** | MOSAÏK System Mode State Machine | INIT, NOMINAL, ADAPTIVE, DEGRADED, SAFE, PGA | NOT STARTED |
| **LOT 5** | Autonomous Reconfiguration | Quorum reconfiguration, membership changes | NOT STARTED |
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

## 6. Current Baseline (LOT 2A)

**Verified commit:** `45ffca0193f365d0ab6eb772a4053482caef050d`  
**Branch:** `lot2-distributed-safety-core`  
**Tests:** 7 test cases (TC-001 through TC-007)  
**Checks:** 22 checks, 0 failures  
**Compiler:** `-std=c99 -Wall -Wextra -Werror` PASS  
**Sanitizers:** AddressSanitizer + UndefinedBehaviorSanitizer PASS  
**Limitations:** Host deterministic simulation only; 3-node topology; 500 ms simulated lease; no physical CAN-FD validation; no HIL; no TRL 4.

---

## 7. Governance Notes

- No Lot modifies behavior of a previous Lot without explicit regression verification
- No weakening of safety assertions to make tests pass
- All findings documented in `ADD-FINDINGS.md` with explicit status
- Simulation evidence never converted to hardware compliance
- No merge to `main` until LOT 15 release criteria met