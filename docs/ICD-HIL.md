# MOSAÏK HIL Bench — Interface Control Document

**Document:** MOSAIK-HIL-ICD-001
**Issue:** 1.1 — 20 September 2026
**Lot:** LOT 6A (pre-hardware)
**Status:** SPECIFICATION. No physical layer has been built, connected or measured.

---

## 0. What this document is, and is not

This is the ICD of the **MOSAÏK HIL bench**. It is **not** the ADD ICD and is
not a transcription of one. It specifies the interface that the bench's own
protocol core presents, so that the same core can run unchanged over the host
virtual bus today and over a physical CAN-FD bus when hardware exists.

**No hardware exists.** Every physical value in this document is marked
**PLANNED** or **UNVALIDATED**. Nothing here is measured, nothing is
validated, and nothing constitutes CbT evidence. Physical validation is
LOT 6B.

Where this ICD deviates from MOSAIK-ADD-0001, the deviation is a legitimate
HIL decision recorded here, not a defect (`ROADMAP.md` §2).

---

## 1. Logical frame — NORMATIVE

These properties are implemented, exercised by the host test suite and are
transport-independent. They hold on Classical CAN 2.0B and on CAN-FD alike.

| Property | Value | Evidence |
|---|---|---|
| Identifier width | **11 bit** (standard identifier) | `mosaik_proto.h`, TC-099 |
| Identifier range used | `0x081`–`0x303`, 18 values | TC-099 |
| Payload length | **8 bytes, DLC 8, always** | TC-006, TC-099 |
| Larger CAN-FD payload lengths (12–64) | **rejected by the decoder** | TC-099 |
| Byte order | Byte-addressed; the only multi-byte field (term / configuration epoch) is **little-endian**: byte 4 low, byte 5 high | `mosaik_proto.c`, TC-006 |
| Protocol version | byte 0 = `0x01`; any other value rejected | TC-006 |
| Application CRC | byte 7, **CRC-8/SAE-J1850** over bytes 0..6: polynomial `0x1D`, init `0xFF`, final XOR `0xFF` | TC-006, TC-043 |
| Remote / extended / FD-specific bits | not used | — |

### 1.1 Identifier allocation and priority

CAN arbitration is won by the numerically lower identifier, so the allocation
**is** the priority scheme. It is deliberate, not incidental:

| Message | Identifier | Rationale for its position |
|---|---|---|
| SAFE announce | `0x080 + node_id` | Highest priority: a node announcing a latched safety state must reach the bus ahead of routine traffic. |
| Vote request | `0x100 + node_id` | Recovery of authority precedes routine operation. |
| Vote grant | `0x180 + node_id` | Paired with the request it answers. |
| Heartbeat | `0x200 + node_id` | Routine liveness. |
| Configuration (Lot 5) | `0x280 + node_id` | Membership transactions are not more urgent than liveness. |
| Acknowledgement (Lot 2C) | `0x300 + node_id` | Lowest: an ACK is evidence, and losing one to arbitration is already handled by the lease. |

`node_id` ∈ {1, 2, 3}. The **source is encoded in the identifier** as well as
in payload byte 1, and a receiver rejects any frame where the two disagree.
A base identifier (`0x080`, `0x100`, `0x180`, `0x200`, `0x280`, `0x300`) is
never transmitted and is not decodable. Exactly **18 identifiers** decode.

**PLANNED / UNVALIDATED:** that this allocation yields acceptable latency under
real bus load. No arbitration measurement exists. LOT 6B.

### 1.2 Payload layout

| Byte | All frames except CONFIG | CONFIG frame |
|---|---|---|
| 0 | Protocol version `0x01` | Protocol version `0x01` |
| 1 | Source node id (1..3) | Source node id (1..3) |
| 2 | Role: 0 follower, 1 candidate, 2 leader | **Stage**: 1 PROPOSE, 2 ACCEPT, 3 COMMIT, 4 ANNOUNCE |
| 3 | State: 0 INIT, 1 NOMINAL, 2 DEGRADED, 3 SAFE | **Membership mask**, bit *n*−1 = node *n* |
| 4 | Term, low byte | Configuration epoch, low byte |
| 5 | Term, high byte | Configuration epoch, high byte |
| 6 | Heartbeat: sequence · Vote grant: target id · SAFE: cause · ACK: echoed heartbeat sequence | ACCEPT: proposer id |
| 7 | CRC-8 over bytes 0..6 | CRC-8 over bytes 0..6 |

A CONFIG frame carries no role and no state; a receiver decodes those as
FOLLOWER and INIT. Its bytes 4–5 are a **membership epoch and never a
leadership term** (`PROTOCOL.md` §10.2).

### 1.3 Valid / invalid frame rules — NORMATIVE

A receiver rejects a frame whose:

- identifier is not one of the 18 decodable values;
- DLC is not 8 (this includes every CAN-FD length above 8);
- version byte is not `0x01`;
- source id is outside 1..3, or disagrees with the identifier;
- CRC-8 does not verify;
- **non-CONFIG** frame has a role byte above 2 or a state byte above 3;
- **CONFIG** frame has a stage outside 1..4, or a mask that is empty or names
  a node outside bits 0..2.

A rejected frame increments `decode_errors` and has **no protocol effect
whatsoever**. It never causes SAFE: cause code 3 (PROTO_ERROR) is reserved and
not implemented (`PROTOCOL.md` §5).

### 1.4 Message semantics referenced by this ICD

Full semantics are in `PROTOCOL.md`; this ICD fixes only what crosses the wire.

- **ACK** (`PROTOCOL.md` §6, Lot 2C): echoes the heartbeat sequence of byte 6 in
  the sender's current term. It is the **only** frame that can renew a leader's
  lease, and only when actually received by that leader.
- **SAFE** (`PROTOCOL.md` §7, Lot 3): byte 6 carries the cause (1 split-brain,
  2 no-quorum, 3 reserved). A SAFE frame is FDIR evidence only; its term is
  **not** adopted as a consensus epoch (Lot 4).
- **CONFIG** (`PROTOCOL.md` §10, Lot 5): the four-stage membership transaction.

---

## 2. Transport boundary — NORMATIVE

The protocol core is platform-independent. Its entire interface to any
transport is:

| Direction | Interface | Meaning |
|---|---|---|
| Transmit | `mosaik_tx_fn(const mosaik_frame_t *, void *user)` | The core offers one frame to the transport. |
| Receive | `mosaik_on_rx(node, now_ms, frame)` | The transport delivers one frame that actually arrived. |
| Time | `now_ms` argument | Monotonic milliseconds, injected. |
| **Transport status (LOT 6A)** | `mosaik_set_transport_status(node, now_ms, status)` | The platform reports the state of **this node's own controller**. |

Audit result (LOT 6A): the transmit/receive/time boundary is **sufficient and
is not redesigned**. It already lets the identical core run over the host
virtual bus and over a physical driver. The single thing it could not express
is a node's knowledge about its **own** transmitter, which §3 adds.

`mosaik_tx_fn` returns `void`. That is deliberate and retained: a CAN
controller accepts a frame into a mailbox and reports success or failure
asynchronously, so a synchronous return value would be a false promise. The
asynchronous report is what the transport status carries.

**No hardware-specific code exists in the protocol core, and none is added by
LOT 6A.**

---

## 3. Transport status model — NORMATIVE (software-facing only)

`mosaik_transport_status_t` is the software-facing abstraction of the CAN
error-confinement states, which are defined by the CAN standard itself:

| Value | CAN error-confinement state | Can transmit? | Meaning to the protocol |
|---|---|---|---|
| `MOSAIK_TRANSPORT_UP` | error-active | yes | Normal. |
| `MOSAIK_TRANSPORT_DEGRADED` | error-passive | **yes** | A persistent fault is indicated, but the controller still transmits. **This is not muteness.** |
| `MOSAIK_TRANSPORT_BUS_OFF` | bus-off | **no** | The controller has removed itself from the bus. **Local knowledge of own muteness.** |
| `MOSAIK_TRANSPORT_RECOVERING` | bus-off recovery in progress | **no** | Still off the bus. Recovery is a sequence, not a completion; a node must not assume it can communicate because recovery has started. Validated by TC-111. |

**Locality rule (INV-TRANSPORT-LOCAL-EVIDENCE).** The status describes *this
node's own controller only*. It carries nothing about any peer, about
topology, about whether any frame was delivered anywhere, about who is
leader, or about what membership should become. Supplying another node's
status across this interface would be magical information and is forbidden.
TC-091 is the standing guard.

**Not specified here, because no hardware exists:** how a platform detects
bus-off, how long detection takes, how long recovery takes, and whether
recovery is automatic or commanded. All four are LOT 6B.

### 3.1 Separation of concerns

LOT 6A deliberately separates three things that are easy to conflate:

1. **Transport detection** — the platform's job. Out of scope for LOT 6A;
   measured in LOT 6B.
2. **Protocol reaction** — specified here and in `PROTOCOL.md` §11.2, and
   **implemented at `0667d04`**, adversarially validated at `2c13556`
   (`LOT6A_TRANSPORT_REPORT.md`).
3. **Recovery policy** — when and how the transport is brought back. The
   protocol core does not command recovery; it reacts to the status reported.

### 3.2 Bus-off is not FDIR evidence

A bus-off node has lost the ability to communicate. It has **not** observed a
violated invariant. The reaction is therefore specified as:

- authority is revoked immediately (§4, INV-TRANSPORT-NO-MUTE-AUTHORITY);
- transmission is refused (§4, INV-TRANSPORT-NO-TX);
- **SAFE is NOT latched by bus-off alone.**

Rationale, derived from the bench's existing safety semantics rather than
assumed: SAFE is latched and terminal within one powered instance and requires
an operator reset, and its two implemented causes are both *observed
violations* (split-brain, or three failed elections). Bus-off is a recoverable
controller condition that the CAN standard expects controllers to recover
from. Mapping it directly to SAFE would make a transient, recoverable fault
permanently disable a node; it would also require the node to transmit SAFE
announcements, which it cannot do while mute, and would drive its peers into
DEGRADED over a fault that had already cleared. The persistent case is already
covered by evidence: a node that stays mute fails elections and reaches SAFE
through the existing NO_QUORUM path. TC-095 pins this decision.

---

## 4. Requirements introduced by LOT 6A

See `requirements/HIL-COMMS-REQUIREMENTS.md` for the requirement text and
`verification/TRACEABILITY.md` for the test mapping. These are **HIL-derived
requirements**; they carry no ADD requirement identifier and none is invented.

All eleven host-verifiable requirements HIL-COM-001 to HIL-COM-011 are
**satisfied** at `2c13556`. The transmit-refusal contract is measured at the
true callback boundary: the harness counts every call into the transmit
callback before any filtering, so "the core never called tx" is distinguished
from "the core called tx and the frame was dropped". The measured result is
**zero calls** while a node reports itself unable to transmit.

---

## 5. Physical transport — PLANNED / UNVALIDATED

**Nothing in this section has been built, connected, powered or measured.**

| Item | Value | Status |
|---|---|---|
| Physical transport | CAN-FD | **PLANNED** |
| Arbitration bitrate | 500 kbit/s | **PLANNED, UNVALIDATED** |
| Data-phase bitrate | 2 Mbit/s | **PLANNED, UNVALIDATED** |
| Frame format on the wire | Standard 11-bit identifier, 8-byte payload | NORMATIVE (§1), unaffected by the choice of bitrate |
| Data-phase bit-rate switching | Not required — the payload is 8 bytes | **DECISION** (§5.1) |
| Bus topology | Linear, shared CANH/CANL | **PLANNED** |
| Termination | 120 Ω at each of the two physical ends | **PLANNED**, a setup requirement for LOT 6B, not a protocol requirement |
| Electrical reference | Common reference between nodes as required by the selected transceivers | **PLANNED** |
| Measured bitrate | — | **NONE** |
| Measured bus load | — | **NONE** |
| Measured arbitration latency | — | **NONE** |
| Measured bus-off detection / recovery time | — | **NONE** |

### 5.1 Why CAN-FD does not widen the frame

CAN-FD is selected as the **physical transport** for the bench. It does not
follow that the **logical protocol** must change. The current 8-byte payload is
a legal CAN-FD payload and a legal Classical CAN payload, so the identical
frame runs on either. Enlarging the frame merely because FD permits it would
invalidate the frozen LOT 2–LOT 5 evidence for no requirement. The ICD
therefore keeps DLC 8, and TC-099 enforces that the decoder rejects every
larger FD length. **Any future enlargement must be justified by a stated
requirement and must be preceded by RED tests.**

### 5.2 Retaining 500 kbit/s + 2 Mbit/s as PLANNED

These are the ADD values. They are retained as the **planned initial bench
configuration** because they give a starting point that keeps HIL results
comparable with the parent architecture, and because no bench evidence yet
exists that would justify a different choice. They are **not** an ADD
conformity claim and **not** measured (`ROADMAP.md` §4.1; ADD-F004 stays open).
If LOT 6B measurement shows them unsuitable for the bench, changing them is a
legitimate HIL decision.

### 5.3 Planned LOT 6B physical topology

Described so LOT 6B has a target. **It does not exist.**

- 3 physical nodes, one CAN-FD controller and one CAN-FD transceiver each;
- one shared CANH/CANL pair, linear topology;
- 120 Ω termination at each of the two physical ends (two terminations total);
- one external observer/logger with an independent timestamp source, used for
  the chronology of §6 and for LOT 7;
- a common electrical reference as required by the selected transceivers.

**Candidate procurement configuration — NOT a protocol requirement, NOT
selected, NOT ordered:** 3 × STM32 Nucleo-class boards, 3 × CAN-FD transceiver
modules, 1 × USB-CAN-FD observer. Commercial product choices are recorded here
precisely so that they stay out of the normative sections. The specific MCU
remains the substitutable bench-platform question of `ROADMAP.md` §4.2, to be
settled by the physical-port work, not by this ICD.

---

## 6. Chronology and correlation — DECISION

**Decision: no `correlation_id` is added to the wire format in LOT 6A.**

ADD-F007 records that the ADD mentions a `correlation_id`. The roadmap retains
it only to the extent the HIL chronology needs it (`ROADMAP.md` §4.1), so the
question was measured rather than assumed. TC-100 captures every frame at the
transmit boundary, exactly as an external bus logger would, and establishes:

1. **(observer timestamp, source, type, term, argument) uniquely keys every
   observed frame** — no collisions in the captured run.
2. **Every acknowledgement binds to exactly one preceding heartbeat** using
   only (term, echoed sequence) — 54 of 54 matched, 0 unmatched.
3. **The boundary of the claim, measured not assumed:** the heartbeat sequence
   is one byte, so (source, term, sequence) repeats after 256 heartbeats,
   i.e. **25 600 ms** within a single term. TC-100 observed **41 such repeats**
   in a 30 s stable term.

Conclusion: the existing fields plus **the external observer's own timestamp**
are sufficient to reconstruct the chronology required by `ROADMAP.md` §6.2. The
observer timestamp is not optional — it is what disambiguates a term longer
than 25.6 s — and LOT 7 must supply it. Adding a `correlation_id` would consume
payload space the 8-byte frame does not have, would force an ICD change that
invalidates frozen evidence, and would buy nothing the timestamp does not
already provide.

**This decision is revisited only if a stated requirement shows the timestamp
insufficient** — for example a chronology obligation across observers with no
common time base. Any such extension must be the smallest one that satisfies
the requirement, must preserve backward traceability, and must receive RED
tests before implementation.

---

## 7. CRC layering — ANALYSIS

The bench has two independent error-detecting layers. Confusing them would
overstate what the application CRC demonstrates.

| Layer | Mechanism | Protects | Who checks it |
|---|---|---|---|
| **Link (CAN / CAN-FD)** | 15-bit CRC (Classical), 17- or 21-bit CRC (FD), bit stuffing, form check, ACK slot, automatic retransmission, error counters | The frame **on the wire**, between transceivers, for the duration of one transmission | The CAN controller, in hardware |
| **Application (this ICD)** | CRC-8/SAE-J1850 over payload bytes 0..6 | The **payload octets** as they pass through software: the encode path, any buffering or queueing, injection by a test harness, and replay of a captured frame | `mosaik_decode()`, in software |

**What the application CRC-8 does demonstrate.** It detects corruption
introduced anywhere the link CRC does not cover — in the sender's software
path, in buffers, in the host virtual bus, and in frames a test injects or
replays. TC-006 shows all 8 single-bit payload corruptions rejected; TC-043
shows CRC-corrupted frames counted and given no protocol effect.

**What it does NOT demonstrate.** It is not a second opinion on wire
integrity: today there is no wire. It gives **no** Hamming-distance argument
for the combined stack, **no** residual-error-rate figure, and **no** argument
about undetected multi-bit corruption on a physical bus. An 8-bit CRC over 7
bytes is a modest check, chosen to fit the frame, not a safety-grade integrity
mechanism. It is not authentication: the bus is trusted and there is no
cryptographic protection (`PROTOCOL.md` §2).

**Decision: CRC-8 is retained.** It is removed only if evidence shows it
redundant, and no such evidence can exist before LOT 6B. Removing it now would
also remove the only integrity check that covers the software path and the
host bus, where the frozen LOT 2–LOT 5 evidence was produced.

---

## 8. Logical versus physical responsibilities

| Concern | Owner | Lot |
|---|---|---|
| Frame layout, identifiers, CRC-8, validity rules | Protocol core | LOT 1, 5, **6A** |
| Consensus, authority, lease, SAFE, membership | Protocol core | LOT 2–5 |
| Reaction to a reported transport status | Protocol core | **6A GREEN** |
| Bus-off detection and its timing | Platform / controller | **6B** |
| Bus-off recovery mechanism and its timing | Platform / controller | **6B** |
| Bitrate, bit timing, sample point | Platform | **6B** |
| Termination, wiring, electrical reference | Bench hardware | **6B / LOT 13** |
| Arbitration, bus load, error frames | Physical bus | **6B / LOT 14** |
| Observer timestamping and event capture | Bench instrumentation | **LOT 7** |

---

## 9. Status

**LOT 6 is NOT closed. LOT 6A is GREEN — PRE-HARDWARE. LOT 6B is NOT
STARTED.**
No physical CAN-FD validation. No measured timing. No CbT evidence. No TRL
increase. The LOT 6B handoff checklist is in `LOT6A_TRANSPORT_REPORT.md` §15.
