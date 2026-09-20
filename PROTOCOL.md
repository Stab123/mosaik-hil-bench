# MOSAIK HIL Bench — Wire Protocol

**Document:** MOSAIK-HIL-PROTO-001
**Issue:** 0.8 — 20 September 2026
**Author:** Sami Bey
**Parent:** MOSAIK-ADD-0001 (architectural design document, TRL 3)

## 1. Scope

This document specifies the bus protocol and node state machine for a
three-node hardware-in-the-loop bench built to obtain measured evidence for a
subset of the MOSAIK architecture requirements. It covers frame formats,
identifiers, timing parameters and the state machine.

It does **not** specify the flight architecture. The bench is a reduced
three-node subset of the six-node concept described in MOSAIK-ADD-0001,
carrying no payload functions.

## 2. Scope limitations

- Three nodes, coordination role only. No payload, thermal or optical function.
- Leader election is timeout-and-priority based, using per-term voting and a
  quorum rule. It is **inspired by** Raft. It is not Raft: there is no log
  replication. Since Lot 5 there **is** a membership-change protocol
  (section 10); it is a two-phase transaction over an explicit committed
  membership, not Raft joint consensus over a replicated log.
- Persistence is limited and specific. Term, vote and SAFE state are **not**
  persisted: a restart is a cold start. Since Lot 5 one host-model
  configuration store per node survives a restart, holding only that node's
  committed configuration and its acceptance binding (section 10.7). This is
  a host model. It is not non-volatile-memory validation, not a power-loss
  atomicity argument and not flight persistence.
- No authentication or integrity protection beyond CRC-8. The bus is trusted.
- Recovery from SAFE is not implemented. SAFE is latched and requires an
  operator reset, standing in for the PENDING_GROUND_ARBITRATION mode of the
  parent architecture.
- **Leadership lease is a host-model parameter (500 ms nominal); it does not
  validate physical CAN-FD timing. The lease mechanism prevents an isolated
  leader from retaining authority indefinitely during a network partition.
  This is a host software demonstrator only — no hardware validation.**
- **Stale/replay immunity (Lot 2B) uses semantic rejection based on term
  monotonicity, lease validity, and sender state. No cryptographic anti-replay
  or sequence-number protection is implemented; the current 8-byte frame
  format has no room for correlation_id. This is a host software demonstrator
  only — no hardware validation.**
- **Leadership authority evidence (Lot 2C): a leader's lease is renewed only
  from an ACK actually received from a peer in the current term. Outbound
  heartbeat delivery alone never renews it. The renewal bookkeeping is
  performed by the host bus model from the node's own recorded ACK evidence
  and uses no topology or delivery knowledge.**
- **Crash and restart (Lot 2D): a crash is the loss of all volatile state; a
  restart is a cold initialisation with term 0 and no vote. No term or vote
  persistence exists. Candidate retries after a failed election use a
  randomised local backoff; a split vote remains possible, only its
  indefinite persistence is addressed. Host software demonstrator only.**
- **FDIR and SAFE (Lot 3): SAFE is latched for the lifetime of one powered
  node instance; a cold restart clears it because no persistence exists.
  DEGRADED is a cluster-awareness state derived only from SAFE frames
  actually received from peers and does not revoke leadership authority.
  Cause code 3 (protocol error) is reserved and not implemented: malformed
  frames are counted and never cause SAFE. No ground arbitration, safety
  discrete, watchdog or local fault input exists. Host software
  demonstrator only — no hardware validation.**

## 3. Physical layer — PLANNED, NOT VALIDATED

**No hardware exists.** Nothing in this section has been built, connected,
powered or measured. Every value is PLANNED or UNVALIDATED, and none of it is
evidence. Physical validation is LOT 6B.

The protocol uses 11-bit identifiers and an 8-byte payload, which is a valid
payload on classical CAN 2.0B and on CAN FD alike. **The logical frame does
not depend on which of the two carries it** (`docs/ICD-HIL.md` §5.1), so the
choice of transport does not disturb the LOT 2 to LOT 5 evidence.

| Item | Value | Status |
|---|---|---|
| Physical transport | CAN-FD | PLANNED |
| Arbitration bitrate | 500 kbit/s | PLANNED, UNVALIDATED |
| Data-phase bitrate | 2 Mbit/s | PLANNED, UNVALIDATED |
| Data-phase bit-rate switching | not required — the payload is 8 bytes | decision, `docs/ICD-HIL.md` §5.1 |
| Bus topology | linear, shared CANH/CANL | PLANNED |
| Termination | 120 Ω at each of the two physical ends | PLANNED — a LOT 6B setup requirement, not a protocol requirement |
| Measured bitrate, bus load, arbitration latency | — | **NONE** |

The planned bitrates are the values of MOSAIK-ADD-0001, retained as the
starting bench configuration so that HIL results stay comparable with the
parent architecture. They are **not** an ADD conformity claim and **not**
measured; ADD-F004 remains open. Changing them on LOT 6B evidence is a
legitimate HIL decision (`ROADMAP.md` §4.1).

**Node platform.** The bench platform baseline is the substitutable choice of
`ROADMAP.md` §4.2, to be settled by the physical-port work (LOT 11) and by
procurement (LOT 13), not by this document. A candidate procurement
configuration is recorded in `docs/ICD-HIL.md` §5.3; it is not a protocol
requirement and nothing has been selected or ordered.

*(Issue 0.8 removed an obsolete physical-layer table that named specific ESP32
parts. It predated the HIL/Advanced separation and the roadmap's platform
audit, and it stated termination as fact for a bus that does not exist.)*

## 4. Identifiers

Lower numeric identifier wins CAN arbitration, so safety traffic is allocated
the lowest range.

| Message | Identifier | Priority |
|---|---|---|
| SAFE announce | `0x080 + node_id` | highest |
| Vote request | `0x100 + node_id` | |
| Vote grant | `0x180 + node_id` | |
| Heartbeat | `0x200 + node_id` | |
| Configuration (Lot 5) | `0x280 + node_id` | |
| Acknowledgement (Lot 2C) | `0x300 + node_id` | lowest |

`node_id` is 1..3, so the decodable identifiers are `0x081`..`0x083`,
`0x101`..`0x103`, `0x181`..`0x183`, `0x201`..`0x203`, `0x281`..`0x283` and
`0x301`..`0x303`: eighteen in total. A base identifier such as `0x080` or
`0x280` is itself unused.

## 5. Payload

All messages use DLC 8.

| Byte | Field |
|---|---|
| 0 | Protocol version (`0x01`) |
| 1 | Source node id |
| 2 | Role (0 follower, 1 candidate, 2 leader) — configuration frame: stage |
| 3 | State (0 INIT, 1 NOMINAL, 2 DEGRADED, 3 SAFE) — configuration frame: membership mask |
| 4 | Term, low byte — configuration frame: configuration epoch, low byte |
| 5 | Term, high byte — configuration frame: configuration epoch, high byte |
| 6 | Argument — heartbeat: sequence number; vote grant: target node id; SAFE: cause code; acknowledgement: echoed heartbeat sequence number; configuration ACCEPT: proposer node id |
| 7 | CRC-8 over bytes 0..6 |

A configuration frame carries no role and no state; a receiver decodes those
two fields as follower and INIT respectively. Its bytes 4 and 5 carry a
**configuration epoch**, which is never a leadership term (section 10.2).

CRC-8 is SAE-J1850: polynomial `0x1D`, initial value `0xFF`, final XOR `0xFF`.
It is an **application-layer** check covering the payload octets through the
software path, the host bus, and injected or replayed frames. It is distinct
from, and additional to, the link-layer CRC, bit stuffing and error counters
that a CAN or CAN-FD controller applies in hardware. What each layer does and
does not demonstrate is analysed in `docs/ICD-HIL.md` §7; in particular, no
Hamming-distance or residual-error-rate argument for the combined stack exists
or is claimed.

A receiver rejects a frame whose identifier is unknown, whose DLC is not 8,
whose version byte does not match, whose CRC fails, whose source id is out of
range, or whose source id disagrees with the identifier. For a non-configuration
frame it additionally rejects a role byte above 2 or a state byte above 3. For a
configuration frame it instead rejects a stage outside 1..4 and a membership
mask that is empty or names a node outside 1..3.

SAFE cause codes: 1 split-brain, 2 no-quorum, 3 protocol error. Code 3 is
RESERVED / NOT IMPLEMENTED in the current host demonstrator: a receiver
counts malformed frames in `decode_errors` and never enters SAFE because of
them. No threshold, window or recovery semantics are defined by the
available normative transcription (Lot 3, decision D2).

## 6. Timing parameters

| Parameter | Value | Origin |
|---|---|---|
| Heartbeat period | 100 ms | MOSAIK-ADD-0001 |
| Election timeout | 300–500 ms, randomised per node | 3–5 missed heartbeats |
| Vote timeout | 150 ms | bench choice |
| Cluster size | 3 | bench scope |
| Quorum | 2 | `floor(n/2) + 1` |
| Failed elections before SAFE | 3 | bench choice |
| **Leadership lease** | **500 ms** | **Lot 2A / MOSAIK-ADD-0001** |
| **Stale message rejection** | **immediate (in receive path)** | **Lot 2B** |
| **Lease evidence freshness** | **100 ms (received current-term ACK)** | **Lot 2C** |
| **Candidate retry backoff** | **0–49 ms, randomised per node (span 50)** | **Lot 2D** |
| **Peer SAFE evidence freshness** | **3 heartbeat periods (300 ms), derived, not configurable** | **Lot 3** |

The election timeout and the candidate retry backoff are drawn from a
per-node deterministic xorshift sequence seeded by node id, so that
simulation runs are reproducible. Randomisation reduces contention; it does
not make a simultaneous election impossible (Lot 2D).

**Leadership lease semantics (Lot 2A).** A leader must maintain evidence of
majority connectivity (quorum contact) to retain valid leadership authority.
The lease is renewed only when the leader actually receives an admissible
current-term acknowledgement from a peer while fresh evidence (within 100 ms)
from a quorum of peers exists; the leader itself is the implicit member of
the quorum. Delivery of the leader's own heartbeat never renews the lease
by itself (Lot 2C). If a leader cannot renew its lease within 500 ms, its leadership
authority becomes invalid and it steps down to follower. This prevents an
isolated leader in a 2+1 network partition from believing it remains leader
indefinitely. The predicate `mosaik_has_valid_leadership_authority()` returns
true only when the node holds the leader role AND its lease is valid. Safety-
critical decisions must use this predicate, not `mosaik_is_leader()`.

**Stale/replay rejection semantics (Lot 2B).** The node rejects messages that
would improperly restore obsolete leadership authority, invalidate a newer
term, or destabilize valid cluster state. Rejection is based on:

- **Term monotonicity:** messages with `term < local_term` are rejected
  (stale term). The node never rolls back its term.
- **Same-term authority:** heartbeats with duplicate sequence numbers from
  the same source are rejected. A heartbeat claiming leadership in the local
  term from a sender whose authority has expired is rejected.
- **Future terms:** messages with `term > local_term` are accepted per the
  existing Raft-like policy (step down and adopt).
- **Duplicate idempotence:** receiving the same message multiple times does
  not accumulate authority or extend an obsolete lease.
- **Partition recovery:** after a partition heals, delayed messages from a
  former leader at an older term cannot restore former authority.

The predicate `mosaik_get_last_reject_reason()` provides the rejection reason
for test instrumentation. No cryptographic anti-replay or sequence-number
protection is implemented; the current 8-byte frame format has no room for
correlation_id. This is a host-model parameter; it does not validate physical
CAN-FD timing.

## 7. State machine

Roles are follower, candidate and leader. States are INIT, NOMINAL, DEGRADED
and SAFE.

**Election.** On election timeout a follower increments its term, becomes
candidate, votes for itself and broadcasts a vote request. On receiving grants
from a quorum it becomes leader and immediately broadcasts a heartbeat. A
candidate that does not reach quorum before the vote timeout increments its
failure count; after three consecutive failures it latches SAFE with cause
no-quorum. Otherwise it waits a randomised local retry backoff (Lot 2D,
0–49 ms by default, drawn from its own RNG) before starting the next
election. During that wait it holds the follower role, keeps its term and
its vote for the failed term, and grants no second vote in that term. The
wait is not a failed election. The backoff breaks the lock-step that
otherwise makes a split vote between two candidates persist; it does not
prevent the first collision. Starting an election is consensus activity and
does not change the operational state (Lot 4): a node that boots INIT may
remain INIT while candidate; NOMINAL arises only from becoming leader or
accepting a heartbeat, DEGRADED only from received peer SAFE evidence.

**Acknowledgement (Lot 2C).** A follower that accepts a heartbeat replies
with an acknowledgement echoing the heartbeat sequence number. The leader
records the acknowledgement per peer for its current term; this record is
the only admissible quorum-contact evidence.

**Leadership lease (Lot 2A).** Upon becoming leader, a node initializes a
500 ms leadership lease. The lease is renewed only on actually received
current-term acknowledgements from a quorum of peers, the leader itself being
the implicit member (Lot 2C). If the lease expires, the leader loses valid
leadership authority and steps down to follower.
The predicate `mosaik_has_valid_leadership_authority()` distinguishes valid
authority from merely holding the leader role.

**Stale/replay immunity (Lot 2B).** The receive path explicitly rejects
messages that would violate term monotonicity or restore expired authority:

- Any message with `term < local_term` is rejected immediately (stale term).
- Heartbeats with duplicate sequence numbers from the same source are
  rejected (duplicate/replay).
- A heartbeat claiming leadership in the local term from a sender whose
  recorded authority term is newer than the message term is rejected (stale
  authority).
- Receiving the same message multiple times is idempotent for leadership
  authority and lease state.
- After partition recovery, delayed messages from a former leader at an older
  term cannot restore former authority.

The function `mosaik_get_last_reject_reason()` reports the rejection cause
for test instrumentation.

**Quorum (Lot 5).** Quorum is taken over the node's own committed membership
mask, not over `cfg.cluster_size`:

```
quorum(mask) = popcount(mask) / 2 + 1
```

`cfg.cluster_size` is a legacy boot parameter since Lot 5 and decides nothing.
While a successor configuration has been accepted but not yet committed, an
election and a leadership lease each require a quorum of the committed
membership **and** a quorum of the accepted successor (section 10.4).
Historical descriptions of a fixed three-node quorum elsewhere in this
document and in the Lot 2 reports describe the behaviour before Lot 5 and are
retained as history.

**Single-leader invariant (REQ-002).** A node grants at most one vote per term.
A node observing any consensus-bearing message (heartbeat, vote request,
vote grant, acknowledgement) with a higher term adopts it and steps down. A
SAFE announcement is not consensus-bearing: its term is FDIR metadata and is
never adopted (Lot 4, see the SAFE contract below). These
two rules together make two leaders in the same term impossible under the
assumed fault model. Vote memory (`voted_for`, `voted_term`) is never erased
by a role change within the same term; only a strictly higher term makes a
node eligible to vote again, because `voted_term` then differs (LOT 2
erratum corrected in Lot 3, TC-050). The leadership lease further ensures that even under a
2+1 network partition, at most one node holds valid leadership authority at
any time (INV-LEADER-UNIQUE). Stale/replay rejection ensures that delayed or
replayed traffic cannot create a second valid authority.

**Split-brain detection (REQ-005).** If a leader nevertheless receives a
heartbeat from another node claiming leadership in the same term, the invariant
has been violated. The node latches SAFE immediately, inside the receive path,
and announces the cause on the bus. It does not attempt to arbitrate.

**SAFE contract (Lot 3).** A SAFE node holds the follower role, never has
valid leadership authority, never becomes candidate or leader, grants no
vote, sends no acknowledgement, transmits SAFE announcements only (one per
heartbeat period, carrying the cause), and ignores every received frame
before any term processing. Old-, same- or higher-term traffic, grants,
SAFE replays and restored connectivity cannot clear SAFE. SAFE is latched
for the lifetime of one powered node instance; a cold restart creates a new
volatile instance and therefore clears it, since persistence is not
implemented. SAFE is not propagated: a received SAFE frame never moves a
peer into SAFE. Two SAFE nodes leave the third without quorum, which then
latches SAFE through election exhaustion. A received SAFE frame is FDIR
evidence only (Lot 4): it updates the receiver's per-peer SAFE evidence and
its DEGRADED state as described below, but its term, even when higher than
the receiver's, is not adopted and changes no role, term, vote, leader
identity, lease or election timing. A node that latched SAFE through
election exhaustion carries a term above the cluster's; its announcements
therefore no longer step the valid leader down (Lot 4, TC-053).

**Degraded operation (Lot 3).** A node records, per peer, the local time of
the latest SAFE frame actually received from that peer. That evidence is
fresh for three heartbeat periods. A received SAFE frame moves NOMINAL to
DEGRADED. While any peer evidence is fresh the node remains DEGRADED: an
accepted heartbeat or becoming leader keeps it DEGRADED. DEGRADED does not
revoke leadership authority and does not change quorum or voting; a leader
may be DEGRADED while holding valid authority. Once no evidence is fresh,
the node returns to NOMINAL only with legitimate local leader evidence: a
leader with valid authority, or a follower that knows its leader and whose
heartbeat deadline still lies in the future. A candidate, or a follower in
retry backoff, does not return to NOMINAL. A dropped SAFE frame is absence
of local evidence; no remote state is inferred from anything but received
frames.

**Crash and restart (Lot 2D, Lot 5).** A crashed node transmits, receives and
services nothing. A restarted node starts from term 0 with no vote and no
knowledge of the cluster; it adopts the current term from the first
higher-term message it receives and cannot regain former authority. Since
Lot 5 it does reload its own committed configuration and acceptance binding
from its host-model configuration store (section 10.7); nothing else is
restored, and a node removed by a committed configuration is therefore still
removed after a restart.

## 8. Assumed fault model

Covered: loss of power of any single node, including the leader; loss of
heartbeats; corrupted frames; partition leaving fewer than quorum nodes.

Since Lot 6A the model also contains a node's knowledge of **its own**
transmitter: a locally reported transport status, including bus-off
(section 11). The **software-facing contract** for that status is specified;
the **protocol reaction to it is not implemented** at the LOT 6A RED baseline.

Not covered: byzantine nodes; the hardware detection and recovery timing of
bus-off; transceiver stuck-dominant faults; clock drift beyond the tolerance
implied by the timeout margins. Every one of those needs hardware and is
LOT 6B.

## 9. Traceability

| Requirement (MOSAIK-ADD-0001) | Section | Test case |
|---|---|---|
| REQ-002 exactly one leader, split-brain prohibited | 7 | TC-001, TC-002, TC-004, TC-007 |
| REQ-003 quorum-based reconfiguration | 6, 7 | TC-005 |
| REQ-004 election within 1 s of leader loss | 6, 7 | TC-003, TC-022, TC-028, TC-031 (host model) |
| REQ-005 SAFE within 10 ms of invariant violation | 7 | TC-004 |
| **Lot 2A: leadership lease under partition** | **7** | **TC-007** |
| **Lot 2B: stale/replay immunity** | **7** | **TC-008, TC-009, TC-010, TC-011, TC-012** |
| **Lot 2C: directional faults and authority evidence** | **6, 7** | **TC-013 to TC-020** |
| **Lot 2D: crash, restart, candidate retry backoff** | **7** | **TC-021 to TC-034** |
| **Lot 3: SAFE contract, DEGRADED evidence, recovery around SAFE, one-vote erratum** | **5, 7** | **TC-035 to TC-050** |
| **Lot 4: election does not degrade, SAFE term not adopted, state metadata non-authoritative** | **5, 7** | **TC-051 to TC-060** |
| **Lot 5: membership and quorum reconfiguration** | **4, 5, 10** | **TC-061 to TC-090** |
| **Lot 6A: transport contract, ICD geometry, chronology** (HIL-derived, no ADD identifier) | **3, 5, 11** | **TC-091 to TC-100** |

The remaining requirements of MOSAIK-ADD-0001 are out of scope for this bench.

The Lot 6A row is **HIL-derived**: `requirements/HIL-COMMS-REQUIREMENTS.md`
carries no ADD requirement identifier, and none was invented. TC-092, TC-093,
TC-096, TC-097 and TC-098 are RED at the LOT 6A RED baseline.

---

## 10. Membership and quorum reconfiguration (Lot 5)

Before Lot 5 membership was the configuration parameter `cluster_size`,
copied once at initialisation and never changed. Since Lot 5 membership is
an explicit committed value that a distributed transaction can change at run
time. Reachability is never membership: loss of contact with a peer is not
evidence that the peer has ceased to be a member.

### 10.1 Committed membership

A three-bit voter mask, bit `n-1` for node `n`. The masks a node may commit
to are the subsets of the three nodes with at least two members: `{1,2}`,
`{1,3}`, `{2,3}` and `{1,2,3}`. A single-node membership is refused by the
handler and by the request interface, because it would reduce quorum to one
and let an isolated node appoint itself.

### 10.2 Configuration epoch

A 16-bit membership epoch, initial value 1, advancing by exactly one per
committed transaction. It is compared explicitly and is never read as a
leadership term, and no leadership term is ever read as an epoch. Epochs do
not wrap: at the last representable epoch a node refuses to start a
successor rather than rolling round to zero.

A committed configuration is identified by the pair
`(configuration epoch, membership mask)`.

### 10.3 The transaction

| Stage | Byte 2 | Direction | Meaning |
|---|---|---|---|
| PROPOSE | 1 | proposer to members | this `(epoch+1, mask)` is proposed |
| ACCEPT | 2 | member to proposer | acceptance bound to that exact pair; byte 6 names the proposer |
| COMMIT | 3 | proposer to members | the pair is committed |
| ANNOUNCE | 4 | any node, every five heartbeat periods | this is my committed `(epoch, mask)` |

An external command starts the transaction on a node that is a committed
member, holds valid leadership authority, has no transaction pending, and
names a valid mask containing itself that differs from the committed one and
does not conflict with an acceptance it has already given. The command
commits nothing by itself.

A PROPOSE is honoured only from a node in the receiver's committed
membership. A receiver that is itself a member additionally requires the
proposer to be the leader it currently follows; a receiver that is not a
member has no meaningful leader and accepts from any member. An ACCEPT
counts when it comes from a node of either configuration of the transaction,
because a node the proposal adds is exactly what the new quorum needs.

### 10.4 Joint quorum

Quorum is taken over a mask: `quorum(mask) = popcount(mask) / 2 + 1`.

A successor is committed only when the acceptances actually received satisfy
a quorum of the old configuration **and** a quorum of the new one. A majority
of the old configuration alone is not sufficient: it would install the
smaller quorum while nodes that must still be counted under the old
configuration have promised nothing.

While an acceptance is pending, the same conjunction governs elections and
the leadership lease.

### 10.5 Participation, candidacy, elections and the lease

Taking part in consensus and standing for election are different. A node
takes part — votes, acknowledges, is counted — while it belongs to **either**
configuration of a pending transaction. A node may stand for election only
while it belongs to **every** configuration that might currently be in force.

A vote from outside the participation set is never counted. A leader's
authority requires acknowledgements actually received, in its current term
and younger than one heartbeat period, forming a quorum as in section 10.4;
outbound traffic never counts. A node that commits a configuration excluding
it drops any candidacy and any leadership in the same step.

### 10.6 Removed and re-admitted nodes

A node excluded by a committed configuration becomes passive: it neither
stands for election nor retries one, and the remaining members refuse its
heartbeats, acknowledgements, vote requests, vote grants and any leadership
term it announces. It is **not** forced into SAFE; exclusion from the
membership and FDIR are different concepts, and it keeps its timers, term and
vote memory.

Its SAFE announcements remain fault evidence and are still recorded by peers.
Membership gates consensus, not fault reporting.

Re-admission restores membership only. It refreshes no acknowledgement
evidence, no vote memory and no heartbeat replay state.

### 10.7 Host-model persistence

Each node owns a configuration store holding what that node itself wrote:
its committed `(epoch, mask)` and its single acceptance binding for the next
epoch. It is reloaded at cold restart. No other node can read it and nothing
in it is derived from topology. An empty, malformed or single-node store is
reset to the initial configuration.

**Host model only.** Term, vote and SAFE state remain unpersisted. This is
not non-volatile-memory validation, not a power-loss atomicity argument and
not flight persistence.

### 10.8 Replay, idempotence and conflicts

An epoch below the committed one is refused as superseded; an epoch above
`committed + 1` is refused for want of transition context; the same epoch
with the same mask is an idempotent duplicate; the same epoch with a
different mask is refused and recorded as an observed inconsistency. A node
binds at most one successor per epoch, and that binding is persisted, so two
different successors of one epoch can never both be agreed.

### 10.9 Repair of a lost commit

There is no reliable broadcast. A commit may reach nobody, one participant or
all. The periodic announcement of section 10.3 repairs a lost commit, because
a member's announcement for `epoch + 1` is admissible. A node that a
configuration removed cannot propagate that configuration, since an
announcement must name a mask containing its sender.

### 10.10 Limitations

A node that misses an entire epoch is not caught up automatically: later
announcements are unsupported future epochs for it, and recovery requires an
operator. Conflicting acceptance bindings for one epoch can deadlock that
epoch permanently, because there is no prepare-and-adopt phase by which a new
proposer would adopt the highest accepted value. Neither limitation produced
a safety-invariant violation in the executed scenarios. See
`LOT5_RECONFIGURATION_REPORT.md` sections 29 and 30.

---

## 11. Transport status and the bus-off contract (Lot 6A)

**PRE-HARDWARE SPECIFICATION. The reaction specified in 11.2 is NOT
implemented at the LOT 6A RED baseline.** The interface exists and is inert:
the status is recorded and no protocol decision reads it. TC-092, TC-093,
TC-096, TC-097 and TC-098 fail for exactly that reason, by design. The
complete interface is specified in `docs/ICD-HIL.md` §3.

### 11.1 What the status is

Until Lot 6A a node could observe only that it heard nothing. That is a
partition, and the bench has modelled it since Lot 2C. It could not observe
that **it itself cannot speak**, which is a different fault with different
evidence: a partitioned node knows nothing about its own transmitter, while a
bus-off node knows, locally and immediately, that nothing it sends can reach
anyone.

`mosaik_set_transport_status()` carries that one fact, and only that fact:

| Status | CAN error-confinement state | Can transmit? |
|---|---|---|
| UP | error-active | yes |
| DEGRADED | error-passive | **yes** — a fault indication, not muteness |
| BUS_OFF | bus-off | **no** |
| RECOVERING | bus-off recovery in progress | **no** |

**INV-TRANSPORT-LOCAL-EVIDENCE.** The status describes this node's own
controller and nothing else. It carries no information about any peer, about
topology, about whether any frame was delivered, about who is leader, or about
membership. Passing a peer's status across this interface would be magical
information and is forbidden. TC-091 is the standing guard.

The three things that are easy to conflate are kept separate: **transport
detection** is the platform's job and is measured in LOT 6B; **protocol
reaction** is specified below; **recovery policy** is not commanded by the
protocol core, which only reacts to what it is told. Detection and recovery
**timing** are not specified here and are not validated, because no hardware
exists.

### 11.2 The reaction — specified, NOT yet implemented

1. **No authority while mute.** A node whose own transport cannot transmit
   holds no valid leadership authority, from the instant it knows. The lease
   is not sufficient here: a lease is evidence of *past* reception, and local
   muteness is *present* knowledge, which is strictly stronger. This is the
   dangerous case, because an outage shorter than the 500 ms lease expires
   nothing by itself (TC-096).
2. **No transmission while mute.** The protocol core hands no frame to the
   transmit interface, on any path, including the configuration path of
   section 10. A frame given to a dead controller is either discarded or
   queued, and a queued frame becomes stale traffic released after recovery.
3. **No fabricated evidence.** A transport fault or its recovery creates no
   lease renewal, acknowledgement evidence, vote or quorum contact. This
   already holds, because evidence derives only from frames actually received
   (Lot 2C); TC-094 keeps it holding.
4. **Recovery restores nothing.** Authority may return only on evidence
   actually received *after* recovery.
5. **DEGRADED is not muteness.** An error-passive controller still transmits.
   Silencing it would convert a recoverable fault into an outage (TC-093).

### 11.3 Bus-off does not latch SAFE

A bus-off node has lost the ability to communicate. It has **not** observed a
violated invariant, and it is therefore not an FDIR event.

This follows from the bench's existing safety semantics rather than being
assumed. SAFE is latched and terminal within one powered instance and requires
an operator reset (section 2), and both implemented causes are *observed
violations*: split-brain, or three failed elections. Bus-off is a recoverable
controller condition that the CAN standard expects controllers to recover
from. Latching SAFE on it would make a transient fault permanently disable a
node; it would also require the node to transmit SAFE announcements, which it
cannot do while mute; and on recovery its peers would enter DEGRADED over a
fault that had already cleared.

The persistent case needs no new mechanism, because it is already covered by
evidence: a node that stays mute fails elections and reaches SAFE through the
existing NO_QUORUM path, with that cause. TC-095 measured exactly that and
pins the decision, so that Lot 6A GREEN cannot quietly map BUS_OFF to SAFE.

### 11.4 What is not specified here

Hardware detection of bus-off, its latency, the recovery mechanism, recovery
latency, and whether recovery is automatic or commanded. All four are
LOT 6B and have **no evidence**.
