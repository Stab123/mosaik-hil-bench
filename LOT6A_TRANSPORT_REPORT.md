# LOT 6A — HIL Communication Substrate, Pre-Hardware Validation

**Document:** MOSAIK-HIL-LOT6A-001
**Issue:** 1.0 — 20 September 2026
**Repository:** `Stab123/mosaik-hil-bench`, branch `lot2c-network-adversarial`

> **HOST SOFTWARE DEMONSTRATOR ONLY — NO HARDWARE EXISTS.**
> No physical CAN-FD bus, transceiver, termination, bitrate, arbitration,
> bus-off detection or bus-off recovery has been built, connected, powered or
> measured. Nothing in this report is CbT evidence. No TRL change. **LOT 6 is
> NOT closed.** Physical validation is LOT 6B (section 14).

---

## 1. Commit lineage

| Stage | SHA | Content |
|---|---|---|
| Starting baseline | `ea33d679feb3fdcc3154452e839b0b327dd2f48a` | HIL/Advanced roadmap separation; 90 tests, 761 checks, 0 failures |
| **RED baseline** | `f3d64846e67c09f739a2078a788191099cbe5905` | ICD, requirements, `PROTOCOL.md` §11, TC-091–TC-100, inert transport interface. **100 tests, 811 checks, 7 failures** |
| **Minimal GREEN** | `0667d04f740629de841fdfa1b4fa03546b497a42` | Twelve lines of logic in `mosaik_node.c`. **100 tests, 811 checks, 0 failures** |
| **Adversarial** | `2c13556f5fdd419289beb4496e777c53110f4284` | TC-101–TC-117, oracle extension, `tx_calls[]`. Firmware untouched. **117 tests, 888 checks, 0 failures** |
| Documentation | this commit | Documentation and traceability only |

The RED commit is scientific evidence and was not rewritten, amended or
squashed.

---

## 2. Frozen RED failures

At `f3d6484` the suite failed exactly seven checks:

| Test | Failing checks | Property denied |
|---|---|---|
| TC-092 | 2 | A bus-off leader held valid authority, at the fault instant and throughout a 400 ms outage |
| TC-093 | 1 | A bus-off leader handed 4 frames to its transmit callback in 300 ms |
| TC-096 | 1 | An outage shorter than the lease left authority valid throughout |
| TC-097 | 1 | A bus-off follower handed 1 frame to its transmit callback |
| TC-098 | 2 | A mute proposer remained an authority and kept offering CONFIG frames |

TC-091, TC-094, TC-095, TC-099 and TC-100 passed at RED: they are the guards
that had to keep holding, not the defect.

---

## 3. Root cause

**Confirmed, single root cause.** The transport status introduced at `f3d6484`
was recorded but no protocol decision consumed it.

`mosaik_tx_fn` returns `void`, so before LOT 6A the protocol core had no way
to learn that its own transmitter had failed. It could observe that it heard
nothing — a partition, modelled since Lot 2C — but never that *it itself
could not speak*. The two are different faults with different evidence: a
partitioned node knows nothing about its own transmitter, while a bus-off node
knows, locally and immediately, that nothing it sends can reach anyone.

The safety consequence was concrete: `mosaik_has_valid_leadership_authority()`
returned true for the remainder of the 500 ms lease while the node would
locally know it was mute, and every transmit path kept offering frames to a
dead controller as though submission had succeeded.

---

## 4. Transport states — final semantics

The four states are the software-facing abstraction of the CAN error-
confinement states, which are defined by the CAN standard and are therefore
not invented here.

| State | CAN state | Transmit | Authority | Protocol effect |
|---|---|---|---|---|
| `UP` | error-active | yes | unaffected | Normal operation. |
| `DEGRADED` | error-passive | **yes** | **unaffected** | A persistent fault indication. **Not muteness.** Transmission continues; the node's own mode is untouched; it is never mistaken for peer SAFE evidence. |
| `BUS_OFF` | bus-off | no | **revoked** | The controller has removed itself from the bus. Local knowledge of own muteness. |
| `RECOVERING` | recovery in progress | no | **revoked** | Still off the bus. |

**DEGRADED is not muteness.** Silencing an error-passive controller would
convert a recoverable fault into an outage, and would cost the cluster a node
that can still communicate. TC-110 pins it: a DEGRADED node keeps
transmitting, keeps its authority, and its transport state never becomes an
FDIR observation.

**RECOVERING is non-transmitting.** This was already the frozen contract at
`f3d6484` (`docs/ICD-HIL.md` §3, `PROTOCOL.md` §11.1) and it is also the
conservative reading: bus-off recovery is a sequence, not a completion, and a
node must not assume it can communicate merely because recovery has started.
TC-111 validates it and validates that reaching UP through it restores
nothing.

Detection latency, recovery latency, and whether recovery is automatic or
commanded are **not specified and not validated**. They are LOT 6B.

---

## 5. Implemented semantics

Twelve lines of logic in one file, `firmware/core/mosaik_node.c`. No test was
altered, no timing constant changed, no frame geometry changed, no
hardware-specific code, no FreeRTOS, no correlation_id.

### 5.1 One rule, two conjuncts

> **Valid leadership authority requires an unexpired lease AND a locally
> transmit-capable transport.**

```c
bool mosaik_has_valid_leadership_authority(const mosaik_node_t *node)
{
    if (node->role != MOSAIK_ROLE_LEADER)      { return false; }
    if (!mosaik_transport_can_transmit(node))  { return false; }   /* LOT 6A */
    return node->now_ms < node->lease_expiry_ms;
}
```

### 5.2 Why BUS_OFF revokes authority immediately

A lease proves that a quorum was reachable in the **recent past**. Local
muteness is evidence about the **present**, and it is strictly stronger: a
leader that cannot put a frame on the bus cannot lead, whatever it was able to
do 100 ms ago. Past evidence must never override stronger current local
evidence. Waiting out the lease would leave up to 500 ms in which a node holds
authority it knows it cannot exercise — TC-102 catches the worst case, with
499 ms of lease unspent at the instant of the fault.

### 5.3 Two mechanisms, and why both are needed

`mosaik_set_transport_status()` additionally expires the lease when the
reported status is not transmit-capable:

```c
if (!mosaik_transport_can_transmit(node)) { node->lease_expiry_ms = now_ms; }
```

The predicate guard of §5.1 alone would **not** have been sufficient. An
outage shorter than the lease would end with the lease still running, and
authority would spring back the instant the transport returned — with no new
evidence whatsoever. That is exactly the counterexample TC-096 captured at
RED. The lease expiry is what makes the revocation **outlive the fault**.

Conversely the expiry alone would not be sufficient either, because a
receive-capable but transmit-dead node can still be handed qualifying
acknowledgements. TC-101 delivers exactly that: two acknowledgements that
really do satisfy `mosaik_has_quorum_ack_evidence()`, to a leader that is
mute. Evidence present, authority still refused.

### 5.4 Step-down uses the existing path

Expiring the lease feeds the step-down that `mosaik_tick()` already performs:
the leader observes `now_ms >= lease_expiry_ms` and calls `become_follower()`
at its own term, exactly as when a partition starves its lease. There remains
**one** mechanism for losing leadership rather than two. The term is
untouched, no election is invented, and no leader role is left standing with
permanently invalid authority. TC-116 confirms the step-down happens and that
the term did not change.

### 5.5 The transmission gate

Two call sites cover all nine protocol frame kinds, because all of them pass
through exactly two functions:

| Gate | Frames covered |
|---|---|
| `emit()` | heartbeat, acknowledgement, vote request, vote grant, SAFE announcement |
| `emit_config()` | CONFIG PROPOSE, ACCEPT, COMMIT, ANNOUNCE |

`last_tx_ms` is deliberately **not** advanced when nothing was sent. Recording
a transmission that did not happen would itself be fabricated evidence. The
consequence is that a node resumes immediately when its transport returns,
with **one** current frame — it does not replay the frames it suppressed.

### 5.6 What `mosaik_set_transport_status()` deliberately does not touch

Term, `voted_for`, `voted_term`, `vote_mask`, `committed_epoch`,
`committed_mask`, the acceptance binding, the configuration store, `state`
(so it neither enters nor clears SAFE and neither sets nor clears DEGRADED),
`safe_evidence_mask`, `last_safe_rx_ms`, `last_ack_rx_ms`, `last_hb_*`,
`last_ack_*`, `last_quorum_contact_ms`, `role`, `leader_id`. It starts no
election and nominates nobody. It knows nothing about peers.

---

## 6. Why BUS_OFF is not automatically SAFE

**Decision: BUS_OFF does not latch SAFE.** Derived from the bench's existing
safety semantics rather than assumed.

SAFE is latched, terminal within one powered instance, and requires an
operator reset. Both implemented causes are **observed violations**:
split-brain, or three failed elections. Bus-off is neither — it is a
recoverable controller condition that the CAN standard expects controllers to
recover from. Mapping it directly to SAFE would:

1. make a transient, recoverable fault permanently disable a node;
2. require the node to transmit SAFE announcements it physically cannot send;
3. on recovery, drive its peers into DEGRADED over a fault that had cleared.

The persistent case needs no new mechanism, because it is already covered by
evidence: a node that stays mute fails elections and reaches SAFE through the
existing NO_QUORUM path, with that cause. TC-095 measured exactly that
(final state SAFE, cause 2 = NO_QUORUM) and pins the decision so that no
future change can quietly map BUS_OFF to SAFE.

---

## 7. SAFE interaction

A node already in SAFE owes periodic SAFE announcements. A bus-off controller
cannot send them. The protocol **intent** to announce is distinguished from
the **ability** to submit a frame: the node stays logically SAFE, the central
gate suppresses the submissions, and nothing is fabricated.

TC-109 measures it: while mute, zero calls into the transmit callback and zero
SAFE frames on the bus, with SAFE latched throughout. In the 500 ms after
recovery, 5 announcements were emitted — the ordinary cadence resuming, **not**
a replay of the 5 that were suppressed.

**Documented limitation.** The announcements a SAFE node owed while mute are
lost. Peers therefore see a gap in SAFE evidence, and a peer whose DEGRADED
evidence window (3 heartbeat periods) expires during the outage returns to
NOMINAL and only re-enters DEGRADED when announcements resume. This is
correct — evidence must come from frames actually received — but it means a
transport outage on a SAFE node is visible to peers as a temporary loss of
that node's SAFE evidence.

---

## 8. Membership interaction

Transport failure is **not** membership reconfiguration. A mute node is not a
removed node, and the Lot 5 joint old-and-new quorum rule is untouched.

- **TC-106**: the leader is muted at eight points across a transaction. No
  stage completed on evidence that could not have been exchanged, no node
  changed its membership at an unchanged epoch, and the continuous oracle saw
  no violation across all eight, including recovery.
- **TC-107**: an acceptor's transmitter dies between recording its binding and
  sending the ACCEPT. The binding is recorded **and persisted to its own
  store**, zero calls into the transmit callback, no ACCEPT on the bus, no node
  commits the successor, and the surviving binding still refuses a rival
  successor for the same epoch after recovery.
- **TC-108**: a removed node loses and regains its transport. Its committed
  epoch and mask are unchanged, it is still not a member, and it holds no
  authority. **Transport recovery is not readmission.**

Where a transaction stalls because the evidence genuinely was never exchanged,
that stall is correct. Safety is preferred over forcing progress.

---

## 9. Recovery semantics

Transport recovery is **not** leadership recovery. BUS_OFF → RECOVERING → UP
restores no lease, no authority, no acknowledgement evidence, no vote, no
membership, no configuration epoch; it does not clear SAFE, does not clear
DEGRADED evidence, and invents no leader knowledge.

**The critical case (TC-116).** A leader goes mute, steps down, and its
transport returns *before* the cluster has elected a replacement. Could its
stale leader knowledge, lease bookkeeping, heartbeat deadline or
acknowledgement records let authority become valid again without a new
legitimate election?

The test establishes the answer is **no**, and it establishes it in a way that
the transport check cannot explain: **the transport is UP for the whole window
under test**, and the test asserts that the lease is still expired at the
instant of recovery. Over the following 2500 ms the old leader never held
authority at or below its pre-fault term. It regained leadership only by
advancing to a strictly higher term through the ordinary election mechanism
(term 1 → 2). No fast path was added.

---

## 10. The transmit-callback question

The distinction between "the core never called `tx`" and "the core called `tx`
and the harness dropped the frame" is scientifically important: a node that
locally knows its controller is unavailable must not behave as though
submission succeeded.

`bus_tx()` therefore increments `tx_calls[]` as its **first statement, before
any harness filtering whatsoever**. TC-105, TC-107, TC-109 and TC-111 assert
against that counter, not against a filtered one. Measured result: **zero calls
into the transmit callback while a node reports itself unable to transmit**, on
the heartbeat, acknowledgement, vote-grant, SAFE and CONFIG paths alike.

---

## 11. Decisions carried forward unchanged

**`correlation_id`: DEFERRED, not added.** Measured at RED by TC-100, not
assumed: (observer timestamp, source, type, term, argument) uniquely keys
every observed frame, and every acknowledgement binds to exactly one preceding
heartbeat via (term, echoed sequence) — 54 of 54 matched. The measured
boundary is that the heartbeat sequence is one byte, so (source, term,
sequence) repeats after 25 600 ms in one term; 41 repeats were observed in a
30 s stable term. The external observer's timestamp is therefore **required**
and, with it, sufficient. No new evidence in LOT 6A changes this. ADD-F007
remains OPEN, and MOSAÏK Advanced may decide differently.

**CRC-8 retained, layering documented.** `docs/ICD-HIL.md` §7 states what the
link layer (15/17/21-bit CRC, bit stuffing, form check, ACK slot,
retransmission, error counters) and the application layer each protect. No
Hamming-distance or residual-error-rate argument for the combined stack exists
or is claimed, and CRC-8 is not authentication.

**Frame geometry unchanged.** 11-bit identifier, DLC 8, 18 decodable
identifiers. CAN-FD is the planned physical transport; it does not widen the
logical frame, and TC-099 enforces that every CAN-FD payload length above 8
bytes is rejected.

---

## 12. Test results

| Stage | Tests | Checks | Failures |
|---|---|---|---|
| Starting baseline `ea33d67` | 90 | 761 | 0 |
| RED `f3d6484` | 100 | 811 | **7** (TC-092×2, TC-093, TC-096, TC-097, TC-098×2) |
| GREEN `0667d04` | 100 | 811 | 0 |
| Adversarial `2c13556` | **117** | **888** | **0** |

Per-test checks, TC-101 to TC-117: 4, 4, 4, 2, 5, 3, 7, 8, 6, 6, 5, 5, 2, 4,
1, 9, 2 — **77 new checks**.

**No behavioural regression.** The GREEN firmware built against the unmodified
TC-001–TC-090 suite reproduces `ea33d67` **byte for byte** (761 checks, 0
failures). TC-001–TC-090 and TC-091–TC-100 output is byte-identical at
`2c13556`.

**Validation at every stage:** strict C99 `-Wall -Wextra -Werror -O1`, empty
stderr; AddressSanitizer and UndefinedBehaviorSanitizer with
`-fno-sanitize-recover=all`, empty stderr and output identical to the normal
build; two consecutive runs byte-identical; `make test` exit 0.

**Firmware was not modified during the adversarial phase**
(`git diff 0667d04 -- firmware/` is empty). No implementation change was made
to satisfy any adversarial test.

---

## 13. Adversarial campaign

| Test | Attack | Checks |
|---|---|---|
| TC-101 | Mute leader handed acknowledgements that really do form fresh quorum evidence | 4 |
| TC-102 | Bus-off at maximum remaining lease (499 ms unspent) | 4 |
| TC-103 | BUS_OFF → RECOVERING → UP with delayed pre-fault frames replayed | 4 |
| TC-104 | Flapping across heartbeat and election boundaries, 16 schedules | 2 |
| TC-105 | Follower muted exactly as it would grant a vote | 5 |
| TC-106 | Leader muted at 8 membership-transaction stages | 3 |
| TC-107 | Acceptor muted between binding and transmission | 7 |
| TC-108 | Removed node loses and regains transport | 8 |
| TC-109 | SAFE node loses and regains transport | 6 |
| TC-110 | DEGRADED transport is not muteness | 6 |
| TC-111 | RECOVERING semantics | 5 |
| TC-112 | Transport fault combined with a network partition | 5 |
| TC-113 | Transport fault across election collisions, 10 offsets | 2 |
| TC-114 | Pre-fault frames released after recovery | 4 |
| TC-115 | Determinism over a 6000-byte state trace | 1 |
| TC-116 | **Critical case:** transport returns before a replacement is elected | 9 |
| TC-117 | Bounded exploration, **1152 schedules** | 2 |

### 13.1 The receive-capable mute node

The decisive adversarial device is `bus_set_transport_rxok()`: the node reports
itself unable to transmit while the harness **keeps delivering frames to it**.
This is physically real — a dead transmitter, a broken TX line, or a
stuck-dominant driver whose receiver still works — and it is what proves the
**protocol** carries the invariant rather than the harness carrying it. Under
a plain drop-everything model, a passing test would prove only that the
harness stopped delivering.

### 13.2 Bounded exploration (TC-117)

**1152 schedules.** Product of: faulted node role 2 × injection offset 6
(0–250 ms) × outage duration 4 (60, 180, 420, 900 ms) × reported status 2
(BUS_OFF, RECOVERING) × receive behaviour 2 (off the bus, or transmit-dead
with receive alive) × network condition 3 (clean, 2+1 partition, one-way
leader isolation) × membership transaction 2 (idle, in progress).

The LOT 5 Phase-3 continuous oracle evaluates every safety invariant at
**every simulated millisecond of every schedule**, extended for LOT 6A with:
a mute node holding valid authority; a mute node submitting a frame to its
transport; a mute node holding a live lease; a rewritten vote within a term;
a mute node's peer SAFE evidence changing. Inherited: at most one valid
authority, authority within own membership, one membership per epoch, term
monotonicity, epoch monotonicity, SAFE holds no authority, SAFE keeps the
follower role, SAFE never exits without a cold restart, membership never
changes without a commit.

**0 violating schedules.**

**This is not a proof and not model checking.** It is an enumeration of a
bounded, deliberately chosen product of schedules in the deterministic host
model. It says nothing about schedules outside that product, about cluster
sizes other than three, or about any physical bus.

### 13.3 Preserved erratum — oracle, not protocol

The new mute-lease oracle check first read *"`lease_expiry_ms` must not
increase while mute"*. It fired in TC-104, TC-112, TC-113 and TC-117.

It was **characterised before anything was changed**. At every flagged instant
the node was already a FOLLOWER, held **no valid authority**, and satisfied
`lease_expiry_ms <= now_ms` — the lease was expired. The numeric increase was
`mosaik_set_transport_status()` re-stamping an **already expired** lease to the
current instant on each new BUS_OFF report during flapping (2000 → 2013 at
t=2013). Re-stamping can raise the number, but it can never produce a live
lease, because the assignment is `lease = now` and a live lease requires
`lease > now`.

**The defect was in the check, not in the protocol.** The implementation was
**not** changed to accommodate it. The predicate was replaced by the strictly
stronger and correct statement — *a mute node never holds a live lease*,
evaluated against the node's own clock — and the erratum is preserved in the
source at the check and in this report.

A second bookkeeping error is recorded for the same reason: TC-117 was
asserted at 576 schedules when the product 2×6×4×2×2×3×2 is **1152**. The
stated number was corrected to the measured truth; the explored space was not
reduced.

**No genuine counterexample against the implementation was found.**

---

## 14. Known limitations

1. **No hardware.** Nothing physical is validated (section 19 of every claim
   boundary in this repository).
2. **Detection and recovery timing are unspecified.** LOT 6A specifies only
   what the core does with a status it is told. How a platform detects
   bus-off, how fast, how it recovers and how fast are LOT 6B.
3. **SAFE announcements owed while mute are lost** (section 7), so a peer's
   DEGRADED evidence can expire during a transport outage.
4. **Liveness is not guaranteed.** A stall caused by evidence that genuinely
   was never exchanged is accepted as correct. Safety is preferred over
   forcing progress.
5. **Bounded exploration is bounded.** 1152 schedules, three nodes,
   deterministic host model. Not a proof.
6. **The transport status is trusted.** A platform that reports UP while its
   controller is bus-off defeats every invariant here. Validating the
   platform's own reporting is LOT 6B.
7. **No cryptographic protection.** The bus is trusted; CRC-8 is an integrity
   check, not authentication.
8. **Three nodes only**, and the two open LOT 5 liveness limitations
   (`LOT5_RECONFIGURATION_REPORT.md` §29) are unchanged by LOT 6A.

---

## 15. LOT 6B handoff checklist

**Nothing below has been started. No item may be marked complete.**

| # | Item |
|---|---|
| B-01 | Assemble three physical nodes on one shared CAN-FD bus |
| B-02 | Select and port to the bench MCU platform (`ROADMAP.md` §4.2 — substitutable, not an ADD conformity obligation) |
| B-03 | Integrate one CAN-FD transceiver per node |
| B-04 | Verify 120 Ω termination at each of the two physical ends |
| B-05 | Establish nominal communication between all three nodes |
| B-06 | Verify the 18 identifiers and the DLC-8 frame on the physical wire against `docs/ICD-HIL.md` §1 |
| B-07 | Verify the planned bitrates (500 kbit/s arbitration, 2 Mbit/s data) or record a justified deviation |
| B-08 | Observe real controller error-confinement states (error-active, error-passive, bus-off) |
| B-09 | Measure real bus-off detection latency and feed it to `mosaik_set_transport_status()` |
| B-10 | Measure real bus-off recovery behaviour and latency; decide automatic versus commanded recovery |
| B-11 | Implement and verify TX/RX error handling and error-counter reporting in the platform layer |
| B-12 | Attach an external CAN-FD logger with a stated timestamp accuracy (required by TC-100's chronology decision) |
| B-13 | Preserve raw traces as primary evidence |
| B-14 | Establish run-to-run repeatability on hardware |
| B-15 | Measure timing where the bench actually implements the parameter; report outliers, do not discard them |
| B-16 | Re-run the LOT 6A adversarial scenarios against the physical transport |

---

## 16. Status

| Claim | Status |
|---|---|
| LOT 6A implementation and pre-hardware validation | **GREEN** |
| LOT 6 | **IN PROGRESS — NOT CLOSED** |
| Physical hardware tested | **NO** |
| Physical CAN-FD validated | **NO** |
| 500 kbit/s validated | **NO** |
| 2 Mbit/s validated | **NO** |
| Bus-off physical timing validated | **NO** |
| Measured failover claimed | **NO** |
| CbT claimed | **NO** |
| TRL increase claimed | **NO** |
