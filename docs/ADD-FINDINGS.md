# MOSAÏK ADD Findings Register

**Document:** MOSAIK-ADD-FIND-001  
**Issue:** 1.4 — 20 September 2026  
**Purpose:** Formal documentation of discrepancies, conflicts, and interpretations found in MOSAIK-ADD-0001

---

## 1. Finding Template

Each finding contains:
- **ID:** ADD-Fxxx
- **Source Section(s):** ADD section numbers
- **Class:** ADD-INTERNAL / IMPLEMENTATION-GAP / VERIFICATION-GAP / TRACEABILITY-GAP
- **Issue:** Description of discrepancy/conflict
- **Impact:** Effect on implementation, verification, or acceptance
- **Proposed Implementation Interpretation:** How the repository will interpret until resolved
- **Status:** OPEN / RESOLVED
- **Resolution Rationale:** When closed, why

---

## 2. Findings

### ADD-F001: Requirement Namespace Mismatch (Section 10 vs Section 82)

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 10 (root requirements) and Section 82 (traceability matrix) |
| **Class** | TRACEABILITY-GAP |
| **Issue** | Two distinct requirement naming schemes exist:<br/>- Section 10: REQ-FUNC-xxxx, REQ-SAFE-xxxx, REQ-PERF-xxxx, REQ-ENV-xxxx, REQ-IF-xxxx, REQ-LOG-xxxx (4-digit suffix)<br/>- Section 82: REQ-FUN-xxx, REQ-SAF-xxx, REQ-PERF-xxx, REQ-ICD-xxx (3-digit suffix)<br/>Prefixes differ (FUNC vs FUN, SAFE vs SAF). Numbering schemes differ. No explicit mapping provided in ADD. |
| **Impact** | Cannot automatically trace root requirements to test cases. Implementation decisions may reference wrong namespace. Verification evidence may be attributed to wrong requirement. |
| **Proposed Interpretation** | Section 10 = ROOT SYSTEM REQUIREMENTS (controlling). Section 82 = DERIVED/DETAILED REQUIREMENTS (test traceability). Explicit mapping table maintained in `REQUIREMENTS-BASELINE.md` and `ADD-MAPPING.md`. All mappings marked PROVISIONAL until architecture review. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F002: Leader Uniqueness Scope — "Outside Declared Partition" vs "At All Times"

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 10: REQ-FUNC-0001 states "one and only one active leader **outside a declared partition**".<br/>ADD Safety language (implied in detailed requirements) may require leader uniqueness "at all times". |
| **Class** | ADD-INTERNAL |
| **Issue** | The phrase "outside a declared partition" implies that *during* a declared partition, multiple leaders might be permitted. However, safety-critical systems typically require leader uniqueness at all times. The detailed safety requirements (Section 82) do not explicitly qualify the uniqueness claim with "outside partition". |
| **Impact** | LOT 2A implementation enforces leader uniqueness **at all times** via lease expiry (INV-LEADER-UNIQUE: max 1 valid authority at every simulation point). This is stricter than the literal reading of REQ-FUNC-0001. If the ADD intent permits multiple leaders during partition, LOT 2A over-constrains. If the ADD intent is uniqueness at all times, the "outside declared partition" wording is misleading. |
| **Proposed Interpretation** | Implement uniqueness at all times (current LOT 2A behavior). Document the discrepancy. Request architecture clarification. The lease mechanism ensures that even during partition, only one node holds *valid* leadership authority.<br/><br/>**Evidence language:** Current LOT2A evidence must be described as: "Maximum concurrent valid leadership authorities observed at the defined deterministic simulation observation points: 1." Do NOT state "leader uniqueness at every possible instant is proven" or equivalent. The implementation policy may remain conservative: valid leadership authority should remain unique during tested partitions. But distinguish: DESIGN INTENT from TESTED EVIDENCE from FORMAL PROOF. Formal proof belongs to LOT 10. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F003: System Mode Terminology Differences

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 4 (system modes) vs Repository implementation vs ADD Section 10 requirements |
| **Class** | TRACEABILITY-GAP, IMPLEMENTATION-GAP |
| **Issue** | Terminology inconsistencies:<br/>- ADD Section 4 modes: INIT, NOMINAL, ADAPTIVE, DEGRADED, SAFE, PGA (6 modes)<br/>- Repository (LOT 2A): INIT, NOMINAL, DEGRADED, SAFE (4 modes) — ADAPTIVE and PGA not implemented<br/>- ADD Section 10 REQ-FUNC-0005 references "DEGRADED mode"<br/>- Repository uses `MOSAIK_STATE_` prefix: INIT, NOMINAL, DEGRADED, SAFE |
| **Impact** | Mode-dependent requirements (e.g., REQ-FUNC-0005 "mission remains useful in DEGRADED") cannot be fully verified until ADAPTIVE and PGA are implemented. PGA (Pending Ground Arbitration) is the formal exit from SAFE in ADD; repository has no PGA — SAFE is terminal in simulation. |
| **Proposed Interpretation** | Repository modes are a subset. ADAPTIVE and PGA marked DESIGN-ONLY in `SOFTWARE-ARCHITECTURE.md`. SAFE latch is terminal in simulation (no ground arbitration path). Document as known gap. LOT 4 (commit `ae9e408`) froze and validated the local semantics of the four implemented states and their combination with the three roles in the host demonstrator (`LOT4_MODE_SEMANTICS_REPORT.md`); ADAPTIVE and PGA are dependency-blocked in this bench and deferred to the separate ADD-driven project (MOSAÏK Advanced). |
| **Status** | OPEN (four-state local semantics closed in LOT 4; ADAPTIVE and PGA not implemented) |
| **Resolution** | — |

### ADD-F004: CAN-FD Bitrate — Protocol Model vs Physical Validation

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 3 (Physical Layer) vs ADD Section 6/82 (ICD) vs Repository implementation |
| **Class** | IMPLEMENTATION-GAP, VERIFICATION-GAP |
| **Issue** | Section 3 states: "CAN FD, 500 kbit/s arbitration, 2 Mbit/s data". Section 6 (Protocol) and ICD may reference only 500 kbit/s. Current repository is a host software demonstrator with no physical CAN hardware. The protocol model uses an 11-bit identifier / 8-byte payload representation compatible with a Classical-CAN-sized frame. |
| **Impact** | No physical CAN-FD bus has been validated. No arbitration/data bitrate has been measured. Frame format in repository is compatible with Classical CAN frame size but FD benefits (larger payload, faster data phase) not utilized. Timing analysis for REQ-PERF-0004 requires FD data phase. |
| **Proposed Interpretation** | Current protocol model uses an 11-bit identifier / 8-byte payload representation compatible with a Classical-CAN-sized frame. No physical CAN-FD bus has been validated. No arbitration/data bitrate has been measured. ADD target remains: CAN-FD with the normative bitrate/configuration defined by the resolved ADD/ICD baseline. LOT 6 must close the protocol/ICD question. LOT 12/14 must provide physical timing evidence. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F005: Environmental Requirements — Bench vs Flight

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 7 (Environmental) vs Repository limitations |
| **Class** | VERIFICATION-GAP |
| **Issue** | ADD specifies LEO environment: thermal cycling, vibration, radiation. Repository uses commercial development boards at ambient conditions. No environmental qualification. |
| **Impact** | All CbT claims require flight-representative hardware and an environmental campaign. Both are deferred to MOSAÏK Advanced: the HIL bench targets commercial development boards and makes no parts-quality, radiation, thermal or vibration argument (see `ROADMAP.md` §4.1). Current IMPLEMENTED-SIM evidence does not satisfy REQ-ENV-0001. HIL/engineering hardware testing is separate from environmental qualification hardware and campaign. |
| **Proposed Interpretation** | Explicitly state in all evidence: "Host simulation only — no environmental qualification." Bench is logic verification (TRL 3), not environmental. Keep all relevant ENV requirements: HARDWARE-REQUIRED / TBC as appropriate. No CbT claim. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F006: Test ID / Requirement Mapping Inconsistencies

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 82 (traceability matrix) vs Repository test cases |
| **Class** | TRACEABILITY-GAP |
| **Issue** | ADD test case IDs (e.g., F-01, S-01, P-01, I-01) do not match repository TC-xxx IDs. Some ADD test cases combine multiple requirements; repository tests are granular (one requirement per test where possible). ADD matrix may have gaps or duplications not yet fully transcribed. |
| **Impact** | Traceability requires explicit mapping table (`TRACEABILITY.md`). Cannot rely on ID matching. |
| **Proposed Interpretation** | Maintain explicit mapping in `TRACEABILITY.md` Section 4. Repository TC-xxx IDs preserved. ADD test case IDs mapped provisionally. A mapping can be: FULL / PARTIAL / RELATED / NONE. Do not imply equivalence merely because two tests examine similar behavior. In particular, LOT2A TC-007 is only PARTIAL evidence toward ADD R-04 because it exercises one deterministic 3-node 2+1 partition scenario. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F007: Correlation_ID in Critical Messages

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 10 REQ-FUNC-0007: "Critical CAN messages include CRC and correlation_id" |
| **Class** | IMPLEMENTATION-GAP |
| **Issue** | Current protocol (PROTOCOL.md, `mosaik_proto.c`) implements CRC-8 (SAE-J1850) but **no correlation_id field** in the 8-byte payload. Payload bytes 0-6 are fully allocated (version, src, role, state, term[2], arg). No room for correlation_id without frame format change. |
| **Impact** | REQ-FUNC-0007 partially implemented (CRC yes, correlation_id no). Traceability (REQ-LOG-0002) requires correlation_id for timeline reconstruction. |
| **Proposed Interpretation** | Mark as PARTIAL in `ADD-MAPPING.md`. CRC capability: implemented in current protocol model. correlation_id: missing. Wire-format allocation for correlation_id is TBC in LOT 6. Do NOT choose the future wire-format solution in LOT0. Do not decide now between: a) larger CAN-FD payload, b) separate message, c) another encoding. That architecture decision belongs to LOT 6. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F008: Quorum Definition — Voting Membership Not Fully Specified in ADD

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 10 REQ-FUNC-0002: "major reconfiguration requires formal majority quorum" vs Repository 3-node subset |
| **Class** | ADD-INTERNAL, IMPLEMENTATION-GAP |
| **Issue** | The ADD V1 physical architecture lists: EN-1, EN-2, EN-3, CN-1, COMN-1, GSE-1 ground station / MCC. GSE must NOT automatically be counted as a consensus voting member. The presence of 3 EN + CN + COMN + GSE does not by itself establish a six-voter quorum. The repository currently verifies majority behavior only on a 3-node logical cluster. The voting membership of EN/CN/COMN and the role of GSE in consensus must be derived explicitly from the normative consensus architecture. GSE shall not be assumed to vote merely because it appears in the physical architecture. |
| **Impact** | The generic majority expression floor(N/2)+1 may be implemented, but evidence from the 3-node LOT2A test does not verify every target MOSAÏK membership configuration. Do NOT state "6-node quorum = 4" unless directly demonstrated by a normative ADD passage. |
| **Proposed Interpretation** | Preserve generic majority computation. Classify 3-node quorum behavior as IMPLEMENTED-SIM. Classify full target-cluster voting membership as TBC/DESIGN-ONLY. Explicitly state that GSE is not counted as a voter unless a normative ADD requirement explicitly assigns it voting authority. Resolve exact voting membership in MOSAÏK Advanced, where the target cluster is actually built (this was previously assigned to LOT 8; see `ROADMAP.md` §2.2).<br/><br/>**LOT 5 update (commit `146472f`, validated at `9ccd28e`).** The repository no longer derives quorum from a configured cluster size. Voting membership is now an explicit **committed membership mask** with its own configuration epoch, and quorum is `popcount(mask) / 2 + 1` over that mask; while a successor is accepted but not committed, a quorum of both configurations is required. This is a **HIL interpretation** for the three-node host demonstrator: it gives the repository a concrete, testable notion of voting membership and a protocol for changing it, and it demonstrates that a majority of the outgoing configuration alone is not a sufficient authorisation rule (`LOT5_RECONFIGURATION_REPORT.md` section 7). It does **not** resolve the ADD-level ambiguity: which of EN-1, EN-2, EN-3, CN-1, COMN-1 are voters in the target architecture, and whether GSE-1 ever votes, remains a normative architecture question. GSE is still not counted as a voter here. |
| **Status** | OPEN at ADD level (target voting membership still to be derived from a normative passage); HIL interpretation implemented and validated in LOT 5 for the three-node demonstrator |
| **Resolution** | — |

### ADD-F009: SAFE Exit — Ground Arbitration vs Terminal Latch

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 4: PGA (Pending Ground Arbitration) is exit from SAFE. Repository: SAFE is terminal (no exit implemented). |
| **Class** | IMPLEMENTATION-GAP |
| **Issue** | ADD requires ground arbitration to exit SAFE. Repository latches SAFE permanently (simulation only). No GSE/COMN path implemented. |
| **Impact** | REQ-SAFE-0003 (SAFE irreversible without ground arbitration) implemented as "irreversible period" in simulation. PGA not tested. |
| **Proposed Interpretation** | Software latch implemented (LOT 3). PGA marked DESIGN-ONLY; it requires the GSE / ground-segment path, which is not in this repository's scope and is deferred to MOSAÏK Advanced (previously assigned to LOT 8; see `ROADMAP.md` §2.2). Document in limitations. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F010: Heartbeat Root/Derived Timing Traceability

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 10 REQ-FUNC-0003: "heartbeat nominal period 100 ms" and ADD Section 82 REQ-FUN-006: "Each node emits cluster heartbeat at: 10 Hz ± 2 %" |
| **Class** | TRACEABILITY-GAP |
| **Issue** | The root requirement layer expresses heartbeat nominally as 100 ms, while the detailed/derived requirement layer specifies 10 Hz ±2 %. This is not an "unknown tolerance". It is a requirement-layer relationship that must be traced explicitly. |
| **Impact** | Repository uses exactly 100 ms nominal timing only. Jitter/tolerance compliance: NOT YET VERIFIED. Hardware clock drift/timing: NOT VERIFIED. |
| **Proposed Interpretation** | Root: 100 ms nominal heartbeat period. Derived: 10 Hz ±2 %. Equivalent nominal frequency: 10 Hz. Use the derived ±2 % requirement as the verification tolerance unless a higher-level conflict is discovered. Current host implementation: 100 ms nominal timing only. Jitter/tolerance compliance: NOT YET VERIFIED. Hardware clock drift/timing: NOT VERIFIED. |
| **Status** | OPEN |
| **Resolution** | — |

### ADD-F011: DEGRADED Exit and Freshness Semantics Not Defined

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 10 REQ-SAFE-0004 "Cluster shall enter DEGRADED when peer enters SAFE"; ADD Section 82 REQ-SAF-003; ADD Section 4 mode table (DEGRADED exit: "Further fault → SAFE, or recovery → NOMINAL") |
| **Class** | ADD-INTERNAL, IMPLEMENTATION-GAP |
| **Issue** | The transcribed requirement defines DEGRADED entry on a peer's SAFE but defines neither how a node knows a peer is still SAFE, how long that knowledge stays valid, nor what "recovery → NOMINAL" requires. Before LOT 3 the host demonstrator entered DEGRADED on a received SAFE frame and cleared it on the next accepted heartbeat, so followers flapped every heartbeat while a peer was SAFE and a leader never left DEGRADED. The evidence cited for REQ-SAFE-0004 (TC-004) asserts SAFE entry only, not peer DEGRADED behaviour. |
| **Impact** | REQ-SAFE-0004 was not met in substance and had no executable evidence. |
| **Proposed Interpretation** | Implemented in LOT 3 (commit `7df0af0`) as the host-demonstrator interpretation, to be confirmed by architecture review:<br/>- an actually received peer SAFE frame creates local per-peer evidence (`last_safe_rx_ms[]`, `safe_evidence_mask`);<br/>- evidence freshness is 3 heartbeat periods, derived from `heartbeat_period_ms` (300 ms by default), not a separate parameter;<br/>- DEGRADED is held while any peer evidence is fresh;<br/>- a normal heartbeat or leadership acquisition cannot prematurely clear it;<br/>- after evidence expires, NOMINAL requires legitimate locally known leader evidence (a leader with valid authority, or a follower that knows its leader with its heartbeat deadline in the future);<br/>- no topology or harness knowledge is used; a dropped SAFE frame is absence of evidence;<br/>- DEGRADED does not revoke legitimate leadership authority or change quorum and voting.<br/>**Requirement text** says only "enter DEGRADED when peer enters SAFE". Everything above beyond entry is **host-demonstrator interpretation**. First executable evidence: TC-039, TC-040; adversarial evidence: TC-045, TC-047, TC-048 (LOT3_FDIR_SAFE_REPORT.md). |
| **Status** | OPEN (interpretation implemented; normative confirmation pending) |
| **Resolution** | — |

### ADD-F012: Election Start Conflated with FDIR Degradation

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 4 mode table (NOMINAL entry: "INIT complete, leader valid"; DEGRADED entry: reduced redundancy after a fault); ADD Section 10 REQ-SAFE-0004 (DEGRADED when a peer enters SAFE); repository `start_election()` before LOT 4 |
| **Class** | IMPLEMENTATION-GAP |
| **Issue** | Before LOT 4 the host demonstrator moved a node from INIT to DEGRADED merely because it started an election. In a fault-free cold boot (TC-052 RED at `58a1b5d`) node 2 became CANDIDATE/DEGRADED at 312 ms of simulated time with an empty SAFE evidence mask, for 2 ms, before winning term 1. No ADD mode entry condition and no received peer SAFE evidence justified DEGRADED. The transition conflated consensus progress with FDIR degradation. |
| **Impact** | Emitted state metadata and local mode observations were wrong during every boot election; the LOT 3 rule "DEGRADED only from received peer SAFE evidence" was violated by an internal transition. No authority or safety invariant was affected. |
| **Proposed Interpretation** | Implemented in LOT 4 (commit `ae9e408`): `start_election()` no longer touches the operational state. A healthy candidate may remain INIT; NOMINAL arises only from becoming leader or accepting a heartbeat; DEGRADED only from received peer SAFE evidence (ADD-F011). All other election operations are unchanged. Evidence: TC-052 (RED at `58a1b5d`, green at `ae9e408`), guards TC-051, TC-057, TC-058. This is a host-demonstrator interpretation of the four-state model only; it says nothing about ADAPTIVE or PGA. |
| **Status** | RESOLVED (host demonstrator, four-state model) |
| **Resolution** | The artificial transition had no requirement basis; removing it restored the LOT 3 evidence rule without changing election semantics. Regression: TC-001–TC-050 unchanged in outcome at `ae9e408`. |

### ADD-F013: SAFE Announcement Term Treated as Consensus Epoch

| Field | Detail |
|-------|--------|
| **Source** | ADD Section 10 REQ-SAFE-0004 and Section 82 REQ-SAF-003 (peer SAFE → DEGRADED); ADD Section 4 SAFE mode ("Ground arbitration (PGA) only" exit); PROTOCOL.md section 7 term-adoption rule; repository `mosaik_on_rx()` before LOT 4 |
| **Class** | ADD-INTERNAL, IMPLEMENTATION-GAP |
| **Issue** | The ADD does not state whether the term carried by a SAFE announcement is evidence of a newer leadership epoch. Before LOT 4 the host demonstrator applied its generic higher-term adoption to every accepted frame before type dispatch, SAFE included. A node that latches SAFE through election exhaustion legitimately carries a term above the cluster's (three failed elections). When its genuine periodic SAFE announcement reached the healthy majority (TC-053 RED at `58a1b5d`), both survivors adopted term 4, the valid leader stepped down in the same step, a re-election followed and valid authority was absent for 397 ms of simulated time. The frame was genuine, not forged. |
| **Impact** | A peer that has left consensus could interrupt the valid leader once per reconnection; REQ-FUNC-0004 style recovery was exercised without any leader fault. Safety invariants held (max one valid authority, no term regression). |
| **Proposed Interpretation** | Implemented in LOT 4 (commit `ae9e408`) as the host-demonstrator interpretation, to be confirmed by architecture review: the higher-term adoption applies to consensus-bearing message types only (HEARTBEAT, VOTE_REQ, VOTE_GRANT, ACK). A received SAFE frame remains FDIR evidence (`last_safe_rx_ms[]`, `safe_evidence_mask`, DEGRADED semantics of ADD-F011) but its term changes no term, role, vote, leader identity, lease or election timing. The wire format, the sender's term and the received frame are untouched. Evidence: TC-053 (RED at `58a1b5d`, green at `ae9e408`, tested-scenario interruption 397 ms → 0 ms), guards TC-059 (lower-term announcement), TC-057, TC-058. |
| **Status** | OPEN (interpretation implemented; normative confirmation pending) |
| **Resolution** | — |

---

## 3. Finding Status Summary

| ID | Title | Class | Status |
|----|-------|-------|--------|
| ADD-F001 | Requirement Namespace Mismatch | TRACEABILITY-GAP | OPEN |
| ADD-F002 | Leader Uniqueness Scope | ADD-INTERNAL | OPEN |
| ADD-F003 | System Mode Terminology | TRACEABILITY-GAP, IMPLEMENTATION-GAP | OPEN (four-state local semantics closed in LOT 4; ADAPTIVE and PGA not implemented) |
| ADD-F004 | CAN-FD Bitrate — Protocol Model vs Physical Validation | IMPLEMENTATION-GAP, VERIFICATION-GAP | OPEN |
| ADD-F005 | Environmental Requirements | VERIFICATION-GAP | OPEN |
| ADD-F006 | Test ID / Requirement Mapping Inconsistencies | TRACEABILITY-GAP | OPEN |
| ADD-F007 | Correlation_ID in Critical Messages | IMPLEMENTATION-GAP | OPEN |
| ADD-F008 | Quorum Definition — Voting Membership | ADD-INTERNAL, IMPLEMENTATION-GAP | OPEN at ADD level (HIL interpretation implemented and validated in LOT 5) |
| ADD-F009 | SAFE Exit / PGA | IMPLEMENTATION-GAP | OPEN |
| ADD-F010 | Heartbeat Root/Derived Timing Traceability | TRACEABILITY-GAP | OPEN |
| ADD-F011 | DEGRADED Exit and Freshness Semantics Not Defined | ADD-INTERNAL, IMPLEMENTATION-GAP | OPEN (interpretation implemented in LOT 3) |
| ADD-F012 | Election Start Conflated with FDIR Degradation | IMPLEMENTATION-GAP | RESOLVED (host demonstrator, LOT 4) |
| ADD-F013 | SAFE Announcement Term Treated as Consensus Epoch | ADD-INTERNAL, IMPLEMENTATION-GAP | OPEN (interpretation implemented in LOT 4) |

**Total:** 13 findings; 12 OPEN, 1 RESOLVED (ADD-F012, host demonstrator).  
**Next review:** LOT 0 closure / architecture review board.