# MOSAÏK HIL Bench — Communication Substrate Requirements (LOT 6A)

**Document:** MOSAIK-HIL-COMMS-001
**Issue:** 1.1 — 20 September 2026
**Lot:** LOT 6A (pre-hardware)
**Interface:** `docs/ICD-HIL.md`

---

## 0. Status of these requirements

These are **HIL-derived requirements**. They were written because the bench
needs them in order to run a controlled communication experiment, not because
an ADD passage demands them.

- They carry **no ADD requirement identifier**, and none is invented. Where an
  ADD requirement is related, it is named as related, not as a source.
- They are **not** a transcription of the ADD ICD.
- `requirements/SYSTEM-REQUIREMENTS.md` remains the ADD transcription and is
  not modified by this document.
- Deviating from the ADD here is a legitimate HIL decision (`ROADMAP.md` §2).

**No hardware exists.** Every requirement below is a requirement on
**software behaviour**, verifiable on the host. Requirements that would need
hardware are listed separately in §3 as LOT 6B obligations with no evidence.

---

## 1. Invariants introduced by LOT 6A

| ID | Invariant |
|----|-----------|
| **INV-TRANSPORT-LOCAL-EVIDENCE** | A node's transport status is local evidence about that node's own controller only. It never encodes, implies or is derived from any peer's liveness, the topology, delivery of any frame, the identity of the leader, or the membership. |
| **INV-TRANSPORT-NO-MUTE-AUTHORITY** | A node that locally knows its own transport cannot transmit holds no valid leadership authority, from the instant it knows. |
| **INV-TRANSPORT-NO-TX** | While a node's own transport cannot transmit, the protocol core hands no frame to the transmit interface. |
| **INV-TRANSPORT-NO-FABRICATED-EVIDENCE** | A transport fault or its recovery never creates protocol evidence. No lease renewal, acknowledgement evidence, vote or quorum contact may accrue from a transport event. |
| **INV-TRANSPORT-NOT-FDIR** | A transport fault is not an observed invariant violation. It never by itself latches SAFE. Escalation remains through the existing evidence-based paths. |
| **INV-TRANSPORT-RECOVERY-NO-AUTHORITY** | Recovery of transport restores no authority by itself. Authority may return only on evidence actually received after recovery. |
| **INV-ICD-FRAME-GEOMETRY** | The logical frame is independent of the physical transport: 11-bit identifier, DLC 8. Adopting CAN-FD does not widen it, and payload lengths above 8 bytes are rejected. |
| **INV-CHRONOLOGY-SUFFICIENT** | Every authority-bearing event observable at the transmit boundary is uniquely identifiable by the external observer's timestamp together with the fields already on the wire. |

---

## 2. Requirements verifiable without hardware (LOT 6A)

| ID | Requirement | Invariant | Verification | LOT 6A result |
|----|-------------|-----------|--------------|---------------|
| **HIL-COM-001** | The protocol core shall accept a transport status reported by its platform describing **only that node's own** controller. | INV-TRANSPORT-LOCAL-EVIDENCE | TC-091 | **PASS** |
| **HIL-COM-002** | The transport status shall distinguish an able-to-transmit fault indication (error-passive) from muteness (bus-off, recovering). | INV-TRANSPORT-LOCAL-EVIDENCE | TC-091, TC-093 | **PASS** |
| **HIL-COM-003** | Reporting a transport status to one node shall change no protocol field of any other node. | INV-TRANSPORT-LOCAL-EVIDENCE | TC-091, TC-097 | **PASS** |
| **HIL-COM-004** | A node whose own transport cannot transmit shall not hold valid leadership authority. | INV-TRANSPORT-NO-MUTE-AUTHORITY | TC-092, TC-096, TC-098, TC-101, TC-102, TC-111 | **PASS** since `0667d04` |
| **HIL-COM-005** | A node whose own transport cannot transmit shall hand no frame to the transmit interface, on any path, including the configuration path. | INV-TRANSPORT-NO-TX | TC-093, TC-097, TC-098, TC-105, TC-107, TC-109, TC-111 | **PASS** since `0667d04`, measured at the true callback boundary |
| **HIL-COM-006** | A transport fault shall create no lease renewal, acknowledgement evidence or quorum contact. | INV-TRANSPORT-NO-FABRICATED-EVIDENCE | TC-094 | **PASS** |
| **HIL-COM-007** | A transport fault shall not by itself latch SAFE. A persistently mute node shall still escalate through the existing NO_QUORUM path and with that cause. | INV-TRANSPORT-NOT-FDIR | TC-095 | **PASS** |
| **HIL-COM-008** | After transport recovery, authority shall be re-established only from evidence actually received after recovery. | INV-TRANSPORT-RECOVERY-NO-AUTHORITY | TC-096, TC-103, TC-111, TC-114, **TC-116** | **PASS** |
| **HIL-COM-009** | The logical frame shall remain an 11-bit identifier with DLC 8 regardless of the physical transport, and payload lengths above 8 bytes shall be rejected. | INV-ICD-FRAME-GEOMETRY | TC-099 | **PASS** |
| **HIL-COM-010** | The chronology of authority-bearing events shall be reconstructible from the external observer's timestamp and the fields already on the wire, without a `correlation_id`. | INV-CHRONOLOGY-SUFFICIENT | TC-100 | **PASS**, bounded by the measured 25 600 ms sequence-wrap (`docs/ICD-HIL.md` §6) |
| **HIL-COM-011** | A transport fault during a membership transaction shall not cause a commit on evidence that could not have been received, nor an inconsistent configuration. | INV-RECONFIG-QUORUM, INV-RECONFIG-CONSISTENT | TC-098, TC-106, TC-107, TC-108 | **PASS** |

**HIL-COM-004 and HIL-COM-005 were RED at the RED baseline `f3d6484`**
(TC-092, TC-093, TC-096, TC-097, TC-098 — seven failing checks), because the
transport interface was specified and inert. They **pass since `0667d04`**,
and all eleven requirements are satisfied at `2c13556` after the adversarial
campaign TC-101 to TC-117, including 1152 bounded exploration schedules with
zero violations. See `LOT6A_TRANSPORT_REPORT.md`.

**Adversarially confirmed properties not separately numbered above:** DEGRADED
remains transmit-capable and is never treated as peer SAFE evidence (TC-110);
RECOVERING is non-transmitting and reaching UP through it restores nothing
(TC-111); transport recovery does not restore voting membership (TC-108);
SAFE stays latched across an outage and announcements resume rather than
replay (TC-109); the critical recovery case, where the transport returns
before a replacement is elected, restores no authority (TC-116).

---

## 3. Requirements that need hardware — LOT 6B, NO EVIDENCE

Stated so that LOT 6B has a target. **None of these has any evidence, and none
may be claimed.**

| ID | Requirement | Why it needs hardware |
|----|-------------|----------------------|
| **HIL-COM-B01** | The platform shall report BUS_OFF within a bound to be measured. | Detection latency is a controller property. |
| **HIL-COM-B02** | The platform shall report RECOVERING and then UP across a bus-off recovery, within a bound to be measured. | CAN bus-off recovery is a hardware sequence. |
| **HIL-COM-B03** | The bench shall operate at its planned bitrates with a measured bus load and error rate. | No bus exists. |
| **HIL-COM-B04** | The identifier priority allocation shall be shown adequate under measured bus load. | Arbitration is physical. |
| **HIL-COM-B05** | Clock drift between physical nodes shall be measured against the timeout margins. | Requires three real clocks. |
| **HIL-COM-B06** | The external observer shall timestamp captured frames with a stated accuracy. | Required by HIL-COM-010 on hardware; LOT 7 / LOT 6B. |

**Related ADD requirements** (related, not sources): REQ-IF-0001 (CAN-FD
backbone ICD), REQ-ICD-001 (frame format), REQ-ICD-002 (CRC-8/SAE-J1850),
REQ-PERF-0004 (bitrates), REQ-FUNC-0007 (CRC and correlation_id). Their ADD
status is unchanged by this document; ADD-F004 and ADD-F007 remain open.

---

## 4. Status

**LOT 6 is NOT closed. LOT 6A is GREEN — PRE-HARDWARE. LOT 6B is NOT
STARTED and has no evidence.**
No physical CAN-FD validation. No measured timing. No CbT evidence.
