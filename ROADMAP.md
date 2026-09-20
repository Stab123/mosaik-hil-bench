# MOSAÏK HIL Bench — Development Roadmap

**Document:** MOSAIK-ROADMAP-001  
**Issue:** 1.6 — 20 September 2026  
**Parent (architectural reference, not conformity obligation):** MOSAIK-ADD-0001 (Issue 1 / Rev 1, dated 24 April 2026)  
**Applies to:** `mosaik-hil-bench` only. The complete ADD-driven implementation is a separate future project, provisionally **MOSAÏK Advanced**.

---

## 1. Purpose

This roadmap defines the controlled, incremental development of the **MOSAÏK HIL bench**: an experimental platform used to discover, reproduce, measure and validate distributed autonomy and fault-tolerance behaviour. It converges toward a reproducible, instrumented, adversarial end-to-end experiment on physical hardware (section 6), **not** toward a complete implementation of the Architectural Design Document (ADD).

Each Lot represents a verifiable increment with documented requirements, implementation, tests, evidence, regression status, and known limitations.

**Governance rule:** A Lot may only close when its requirements, implementation, tests, evidence, regression status, and limitations are documented.

---

## 2. Repository Scope Boundary — HIL Bench vs MOSAÏK Advanced

Two distinct projects exist. This separation is authoritative and governs all Lots from LOT 6 onward.

| | **MOSAÏK HIL Bench** (this repository) | **MOSAÏK Advanced** (separate future project) |
|---|---|---|
| Nature | Experimental verification bench | ADD-driven implementation of the complete architecture |
| Purpose | Discover, reproduce, measure and validate distributed autonomy / fault-tolerance behaviour | Build the complete MOSAÏK architecture and, eventually, mission software |
| Topology | Minimal generic coordination cluster (three nodes today) | Full EN/CN/COMN/GSE physical architecture |
| Modes | The local mode model actually implemented and tested here (four states) | The six-mode ADD model, including ADAPTIVE and PGA |
| Success criterion | A reproducible, instrumented, adversarial experiment that reports what actually happens | Architectural and, ultimately, mission conformity |

**This repository is therefore NOT required to become:** the complete ADD architecture; the complete EN/CN/COMN architecture; the six-mode ADD implementation; the flight architecture; the final mission software. Each of those belongs to MOSAÏK Advanced.

### 2.1 Relationship to the ADD

The ADD is **not removed from this project's history** and is not demoted. It remains:

- the architectural parent and reference;
- the source of the hypotheses this bench tests;
- the source of the requirement set and of the findings register (`docs/ADD-FINDINGS.md`);
- the future source of requirements for MOSAÏK Advanced.

However: **HIL deviations and experimental discoveries are allowed and expected.** Where the bench implements something differently from the ADD, or discovers that an ADD statement is ambiguous, under-specified or unsafe as written, that is a legitimate experimental result, recorded as a finding — not a defect of the bench. **The HIL repository is not required to converge structurally to the ADD.** Requirement traceability in `docs/ADD-MAPPING.md` and `verification/TRACEABILITY.md` records what the bench does and does not evidence; it does not constitute an obligation to implement every ADD requirement here.

### 2.2 Supersession of earlier forward references

Before Issue 1.5, LOT 8 was defined as the full six-node EN/CN/COMN architecture, and several documents forward-referenced "LOT 8" for full-architecture items (ground arbitration / PGA, CN blackbox, COMN log export, target-cluster voting membership). Those items are now **deferred to MOSAÏK Advanced**. Historical Lot reports (`LOT2A_*`, `LOT2B_*`, `LOT2D_*`, `LOT3_*`, `LOT4_*`, `LOT5_*`) are frozen evidence records and are **not** rewritten; where they forward-reference "LOT 8" for a full-architecture item, that reference is superseded by this section.

---

## 3. Lot Families

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
| **LOT 6** | HIL Communication Substrate (CAN-FD / ICD) | A **controlled communication layer for the HIL experiment**, split into a pre-hardware part and a physical part. Deviations and limitations against the ADD are explicitly documented. Physical measurements only where hardware actually exists. Not an attempt to reproduce the complete ADD communications architecture. | **NOT CLOSED** |
| **LOT 6A** | Communication substrate, pre-hardware | Everything establishable without hardware: HIL communication requirements, the bench ICD (`docs/ICD-HIL.md`), the transport abstraction boundary, the encode/decode contract, frame and identifier rules, the **software-facing** transport status and bus-off contract, chronology and correlation requirements for LOT 7, host-testable transport-independent properties, and the LOT 6B physical validation plan. TC-091–TC-100. | **IN PROGRESS — PRE-HARDWARE** (RED baseline established) |
| **LOT 6B** | Communication substrate, physical validation | Everything that requires hardware: bitrate validity, transceiver behaviour, electrical integrity, arbitration timing, bus load, clock drift, real bus-off detection and recovery timing, hardware error-frame behaviour, measured latency, physical partition behaviour. | NOT STARTED — **NO HARDWARE** |
| **LOT 7** | Embedded Services and Experiment Observability | Time service, event logging and health monitoring sufficient to **timestamp and reconstruct the chronology of a HIL run** (section 6.2). CN-authoritative blackbox and COMN/GSE log export deferred to MOSAÏK Advanced. | NOT STARTED |
| **LOT 8** | Experimental Function Ownership, Redistribution and Decision Safety Gate | Minimal experimental **function abstraction** (function identity, owner node, capability eligibility, execution state, handover/reassignment decision, safety preconditions) solely to test autonomous redistribution on the bench; plus an explicit **safety-decision gate** that refuses a safety-critical experimental action when the required authority, membership, freshness, health and function preconditions are not satisfied, records the refusal reason, and transitions to SAFE where the safety contract requires it. **The full EN/CN/COMN six-node architecture is NOT in scope for this repository** and is deferred to MOSAÏK Advanced. | NOT STARTED |
| **LOT 9** | Adversarial and Campaign Verification | Systematic fault injection and statistical campaigns against the **integrated bench**: leader failure, multiple failures, partitions, asymmetric communication, message loss/delay/reorder, membership changes, function redistribution, unsafe decisions, SAFE transition, network heal, reconciliation and recovery. | NOT STARTED |
| **LOT 10** | Formal Verification of HIL Invariants | Model checking and proof of critical invariants **of the protocol and safety model actually implemented in this bench**. No claim that it verifies the complete ADD architecture. | NOT STARTED |
| **LOT 11** | Target Platform Port (STM32H743 / FreeRTOS) | Port of the bench protocol core to the HIL node platform. Retained as **HIL infrastructure** — physical nodes are required to produce measured evidence — and not as an ADD conformity obligation; substitutable if a different bench platform is justified (section 4.2). | NOT STARTED |
| **LOT 12** | HIL MicroLab | Hardware-in-the-loop test infrastructure for the **bench topology** (three coordination nodes), with controlled fault injection and observability. Six-node hardware deferred to MOSAÏK Advanced. | NOT STARTED |
| **LOT 13** | HIL Bench Hardware | Procurement and integration of the hardware needed to run the final HIL experiment on physical nodes. **Flight-representative and flight-qualified hardware deferred to MOSAÏK Advanced.** | NOT STARTED |
| **LOT 14** | Measured HIL Verification Campaign | Measured timing and behavioural evidence on bench hardware (Level 2) **for the parameters this bench actually implements**. Environmental and qualification campaigns deferred to MOSAÏK Advanced. | NOT STARTED |
| **LOT 15** | Reproducible HIL Bench Release | Packaged release of the experimental bench and its evidence, **including the final integrated HIL experiment of section 6**. No claim of complete MOSAÏK ADD implementation, flight qualification, flight readiness, arbitrary cluster proof, or TRL 4. | NOT STARTED |

---

## 4. LOT 6–15 Scope Classification

Every scope item of LOT 6 through LOT 15 was classified before this Issue was written:

**A** — belongs to the HIL experimental bench · **B** — belongs to future MOSAÏK Advanced · **C** — shared enabling infrastructure · **D** — was ambiguous and required explicit separation (resolution given).

### 4.1 Classification

| Lot | Scope item | Class | Disposition |
|-----|------------|-------|-------------|
| 6 | Physical CAN-FD transport for the bench | A | Retained. Required for physical HIL. **LOT 6B.** |
| 6 | Frame format / ICD definition | C | Retained. Shared: the ICD is reused as an input to MOSAÏK Advanced. **Delivered by LOT 6A** as `docs/ICD-HIL.md`. |
| 6 | Bus-off handling — software-facing contract | A | Retained. **LOT 6A**, specified in `PROTOCOL.md` §11; reaction not yet implemented. |
| 6 | Bus-off handling — hardware detection and recovery timing | A | Retained. **LOT 6B**, no evidence. |
| 6 | ADD bitrate conformity (500 kbit/s / 2 Mbit/s as a conformity claim) | D → B | The bench documents the bitrate it actually runs and its deviation; proving ADD bitrate conformity belongs to MOSAÏK Advanced (ADD-F004 remains open). |
| 6 | `correlation_id` in the wire format (ADD-F007) | C | Retained only to the extent the chronology of section 6.2 needs it. |
| 7 | Time service | A, C | Retained. Timestamping is a precondition of section 6.2. |
| 7 | Event logging / recorded evidence | A, C | Retained. This is the chronology infrastructure. |
| 7 | Health monitoring | A | Retained. Local health evidence feeds the LOT 8 decision preconditions. |
| 7 | File system | D → A (reduced) | Retained only as the evidence persistence the bench needs to survive a node restart and to export a run. |
| 7 | CN-authoritative blackbox; COMN export to GSE | B | Deferred to MOSAÏK Advanced. |
| 8 | Full 6-node architecture (3 EN, CN, COMN) | **B** | **Removed from this repository.** Deferred to MOSAÏK Advanced. |
| 8 | GSE / ground segment, PGA / ground arbitration | B | Deferred to MOSAÏK Advanced (ADD-F009). |
| 8 | Experimental function abstraction and ownership | A | New HIL scope (section 6.3). |
| 8 | Capability eligibility and advertisement | A | New HIL scope, only if the redistribution experiment needs it. |
| 8 | Autonomous function reassignment / handover | A | New HIL scope. |
| 8 | Decision authority and safety-decision gate | A | New HIL scope (section 6.4). |
| 9 | Integrated adversarial campaign | A | Retained and extended to function redistribution and unsafe decisions. |
| 10 | Model checking of implemented invariants | A | Retained, scoped to this bench's protocol and safety model. |
| 10 | Verification of the complete ADD architecture | B | Explicitly not claimed. |
| 11 | Target MCU/RTOS port as HIL infrastructure | D → A | See 4.2. |
| 11 | STM32H743 / FreeRTOS as ADD conformity | D → B | See 4.2. |
| 12 | HIL MicroLab, fault injection, observability | A | Retained. |
| 12 | Six-node hardware | D → B | Removed from the LOT 12 milestone; the bench topology is three nodes. |
| 13 | Bench hardware procurement and integration | A | Retained. |
| 13 | "Flight-representative" hardware, parts quality, radiation tolerance | D → B | Deferred to MOSAÏK Advanced. The bench targets commercial development boards and claims nothing about parts quality. |
| 14 | Measured timing/behavioural evidence on bench hardware | A | Retained, for implemented parameters only. |
| 14 | Environmental / qualification campaign (REQ-ENV, ADD-F005) | B | Deferred to MOSAÏK Advanced. |
| 15 | Reproducible packaged release of the bench and its evidence | A | Retained. |
| 15 | Final integrated HIL experiment | A | Retained (section 6). |
| 15 | "Complete MOSAÏK demonstrator", flight qualification/readiness, arbitrary cluster proof, TRL 4 | B | Explicitly not claimed by this repository. |

### 4.2 STM32H743 / FreeRTOS — audit result

The STM32H743 and FreeRTOS selections were **inherited from the ADD**, not derived from a HIL requirement. They are **not deleted**, and they are **not retained as an ADD conformity obligation**. They are retained as the **bench platform baseline**, on this justification: producing measured physical evidence (LOT 14) requires physical nodes; an MCU and RTOS already described throughout `architecture/SOFTWARE-ARCHITECTURE.md` is the lowest-friction credible choice and keeps the HIL results relevant to MOSAÏK Advanced. They are **substitutable**: if a different bench platform produces the same measured evidence more cheaply, that substitution is a legitimate HIL decision and does not constitute a deviation from anything this repository owes. No ADD conformity claim attaches to either choice.

Likewise audited: **MicroLab** — retained, class A, it is the bench itself. **MOSAÏK V1 Hardware** — retained as bench hardware (LOT 13), with the "flight-representative" obligation removed. **Six-node hardware** — removed from this repository. **Flight-representative wording** — removed from LOT 13 and from the LOT 12 milestone; it survives only in the frozen historical reports and in descriptions of the ADD target, where it describes the ADD and not an obligation on this bench.

---

## 5. Dependency Rules

- LOT 0 must complete before any other Lot can formally close
- LOT 1 is prerequisite for LOT 2 family
- LOT 2A is complete (host simulation only)
- LOT 2B–2D depend on LOT 1 and LOT 2A
- LOT 3–5 depend on LOT 2 family
- LOT 6A needs no hardware and is a prerequisite for LOT 6B
- LOT 6B is prerequisite for LOT 11–14
- LOT 7 depends on LOT 6 for the physical substrate; its host-side observability may be developed against the host model first
- LOT 8 depends on LOT 5 (membership) and LOT 7 (observability), not on LOT 6
- LOT 9 depends on LOT 2–5 and LOT 8
- LOT 10 depends on LOT 2–5 and LOT 8
- LOT 11–13 depend on LOT 6
- LOT 14 depends on LOT 12–13
- LOT 15 depends on all prior Lots and on the final integrated experiment of section 6

---

## 6. Final HIL Experimental Objective

The HIL roadmap converges toward **one reproducible end-to-end experiment** on the bench. Every Lot from LOT 6 onward exists to make this experiment possible; a scope item that does not serve it belongs to MOSAÏK Advanced.

### 6.1 The chain the final experiment must exercise

1. cluster starts healthy;
2. identify the valid leader;
3. kill the leader;
4. detect loss of authority;
5. elect a new leader;
6. lose a second node or function provider;
7. evaluate and reconfigure membership / quorum using **legitimate local evidence only**;
8. redistribute an experimental function / workload where safe and possible;
9. create a network partition;
10. attempt a deliberately unsafe or dangerous decision;
11. the safety mechanism **refuses** that decision;
12. the system transitions to SAFE **where the defined safety contract requires it**;
13. reunify the network;
14. recover and reconcile according to the implemented rules;
15. reconstruct the complete chronology from recorded evidence.

**Required properties.** The experiment must be deterministic and reproducible where determinism is intended, instrumented, timestamped, measurable, traceable, repeatable, adversarial, and scientifically honest.

**It must report failures if failures occur. It must NOT force the expected happy-path outcome.** A run that refuses to complete the chain, or that completes it while violating an invariant, is a result to be published, not a defect in the experiment. The bench's own governance rule — no weakening of safety assertions to make a test pass — applies to the final experiment without exception.

**Evidence discipline.** Steps 1–7 and 13–14 are exercised today only in the deterministic host model (LOT 2–5). Steps 8 and 10–11 have no implementation at all. Nothing in this section is evidence; it is the objective the roadmap converges toward.

### 6.2 Chronology and observability (LOT 7)

Step 15 requires that a completed run be reconstructible, with timestamps, over at least: node state; role; term; configuration epoch; membership; leader identity; authority validity; lease evidence; votes; ACK evidence; SAFE/DEGRADED evidence; function ownership; reconfiguration events; injected network faults; decision requests; decision accept/refuse outcome; and recovery/reconciliation events.

LOT 7 (Embedded Services) is the natural home for the logging, time and health infrastructure this requires, and is scoped accordingly in section 3. **Not implemented; not started.**

### 6.3 Function redistribution (LOT 8)

The bench today has coordination nodes and **no payload function**, so step 8 cannot be exercised. A future Lot may introduce a **minimal experimental function abstraction**, solely to test autonomous redistribution:

| Element | Meaning |
|---------|---------|
| Function *F* | An identified experimental workload |
| Owner node | The node currently responsible for *F* |
| Capability eligibility | Which nodes may legitimately own *F* |
| Execution state | Whether *F* is running, suspended or unassigned |
| Handover / reassignment decision | How ownership legitimately changes |
| Safety preconditions | What must hold before a handover is permitted |

This must **not** require implementing the full EN/CN/COMN mission architecture. It is placed in LOT 8. **Not implemented; not started.**

### 6.4 Dangerous-decision refusal (LOT 8)

Steps 10–12 require an explicit **safety-decision gate**: a node or leader must not perform a safety-critical experimental action unless the required authority, membership, freshness, health and function preconditions are satisfied. The gate must eventually support a test of the shape:

> unsafe command proposed → prerequisites evaluated → command **refused** → refusal reason recorded → SAFE if policy requires it.

The refusal, and its reason, are themselves evidence and must appear in the chronology of 6.2. This is placed in LOT 8. **Not implemented; not started.**

---

## 7. Evidence Classification

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

## 8. Milestone Definitions

| Milestone | Criteria |
|-----------|----------|
| **LOT 0 Complete** | All governance docs created, traceability established, ADD findings documented, zero regression |
| **LOT 1 Complete** | Protocol spec frozen, codec verified, basic state machine implemented |
| **LOT 2 Family Complete** | All leadership/partition invariants implemented and tested in simulation |
| **LOT 6A Complete** | HIL communication requirements, bench ICD and the software-facing transport contract specified, implemented and green on the host; no hardware claim |
| **LOT 6B Complete** | CAN-FD physical layer validated on target hardware, with measured evidence |
| **LOT 11 Complete** | Full firmware builds on STM32H743/FreeRTOS |
| **LOT 8 Complete** | Experimental function ownership, autonomous redistribution and the safety-decision gate implemented and tested in the bench |
| **LOT 12 Complete** | HIL MicroLab operational with the three-node bench topology |
| **LOT 14 Complete** | 30-run measured campaigns for REQ-004, REQ-005 on hardware |
| **LOT 15 Complete** | All evidence packaged, reproducible build, release tagged, and the final integrated HIL experiment (section 6) reproducible end-to-end |

---

## 9. Current Baseline (LOT 2A through LOT 5)

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

## 10. Governance Notes

- No Lot modifies behavior of a previous Lot without explicit regression verification
- No weakening of safety assertions to make tests pass
- All findings documented in `ADD-FINDINGS.md` with explicit status
- Simulation evidence never converted to hardware compliance
- No merge to `main` until LOT 15 release criteria met
- No Lot of this repository may be defined as an obligation to implement the complete ADD architecture (section 2)
- The final integrated experiment (section 6) reports what actually happens; a failing run is published, not suppressed

---

## 11. HIL → MOSAÏK Advanced Knowledge Transfer

The two projects are connected by **knowledge transfer, not by code migration**. Code migration is not the default and is not assumed.

The intended handoff path:

1. **HIL observation** — the bench exhibits a behaviour, under a bounded and instrumented scenario.
2. **Reproducible counterexample or finding** — the behaviour is reduced to a deterministic scenario that reproduces it, and recorded with its evidence (the LOT 2D split-vote defect, the LOT 3 same-term vote-memory erratum, the LOT 4 C1/C2 corrections, and the LOT 5 old-majority counterexample are existing examples of this step).
3. **Documented design lesson** — what the behaviour implies about the design rule, written in the Lot report and, where it concerns the architecture, raised as an entry in `docs/ADD-FINDINGS.md`.
4. **MOSAÏK Advanced requirement or test** — the lesson becomes a requirement, a design constraint or a test obligation in the Advanced project.
5. **Reproduce the original behaviour where necessary** — Advanced reproduces the counterexample against its own implementation, to establish that the problem is real in its architecture and not an artefact of the bench model.
6. **Implement the correction or the deviation** — in Advanced, on its own terms; the HIL implementation is an existence proof, not a reference implementation to be copied.
7. **Rerun the HIL-derived adversarial scenario** — the scenario library built here (LOT 9) is rerun against Advanced as a regression obligation.

Nothing in this path obliges the bench to adopt the Advanced architecture, and nothing obliges Advanced to adopt the bench's implementation choices.

---

## 12. LOT 0 Closure Status

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
| ADD-F005 | Environmental Requirements | VERIFICATION-GAP | Environmental/qualification campaign deferred to MOSAÏK Advanced (was LOT 13, 14) |
| ADD-F006 | Test ID / Requirement Mapping Inconsistencies | TRACEABILITY-GAP | Ongoing |
| ADD-F007 | Correlation_ID in Critical Messages | IMPLEMENTATION-GAP | LOT 6 |
| ADD-F008 | Quorum Definition — Voting Membership | ADD-INTERNAL, IMPLEMENTATION-GAP | HIL interpretation implemented and validated in LOT 5 for the three-node demonstrator; general ADD target-cluster voting membership deferred to MOSAÏK Advanced (was LOT 8) |
| ADD-F009 | SAFE Exit / PGA | IMPLEMENTATION-GAP | LOT 3 closed with the software latch documented; PGA / ground arbitration requires the GSE path and is deferred to MOSAÏK Advanced (was LOT 8) |
| ADD-F011 | DEGRADED Exit / Freshness Semantics | ADD-INTERNAL, IMPLEMENTATION-GAP | Host interpretation implemented in LOT 3; architecture review |
| ADD-F010 | Heartbeat Root/Derived Timing Traceability | TRACEABILITY-GAP | Architecture review / LOT 6 |

**No blocking findings for LOT 1 start.**