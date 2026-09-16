# MOSAIK HIL Bench — Wire Protocol

**Document:** MOSAIK-HIL-PROTO-001
**Issue:** 0.4 — 16 September 2026
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
  replication, no persistent state across reset, and no membership change
  protocol.
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

## 3. Physical layer

The protocol uses 11-bit identifiers and an 8-byte payload, which is valid on
both classical CAN 2.0B and CAN FD. The target has not yet been fixed:

| Option | Bus | Note |
|---|---|---|
| ESP32-C5 | CAN FD, 500 kbit/s arbitration, 2 Mbit/s data | matches MOSAIK-ADD-0001; requires an FD-rated transceiver |
| ESP32 classic | CAN 2.0B, 500 kbit/s | deviation from the parent architecture, to be declared in results |

Bit rate for the arbitration phase is 500 kbit/s in both cases. Bus
termination is 120 Ω at each end.

## 4. Identifiers

Lower numeric identifier wins CAN arbitration, so safety traffic is allocated
the lowest range.

| Message | Identifier | Priority |
|---|---|---|
| SAFE announce | `0x080 + node_id` | highest |
| Vote request | `0x100 + node_id` | |
| Vote grant | `0x180 + node_id` | |
| Heartbeat | `0x200 + node_id` | |
| Acknowledgement (Lot 2C) | `0x300 + node_id` | lowest |

`node_id` is 1..3. Identifier `0x080` itself is unused.

## 5. Payload

All messages use DLC 8.

| Byte | Field |
|---|---|
| 0 | Protocol version (`0x01`) |
| 1 | Source node id |
| 2 | Role (0 follower, 1 candidate, 2 leader) |
| 3 | State (0 INIT, 1 NOMINAL, 2 DEGRADED, 3 SAFE) |
| 4 | Term, low byte |
| 5 | Term, high byte |
| 6 | Argument — heartbeat: sequence number; vote grant: target node id; SAFE: cause code; acknowledgement: echoed heartbeat sequence number |
| 7 | CRC-8 over bytes 0..6 |

CRC-8 is SAE-J1850: polynomial `0x1D`, initial value `0xFF`, final XOR `0xFF`.

A receiver rejects a frame whose identifier is unknown, whose DLC is not 8,
whose version byte does not match, whose CRC fails, whose source id is out of
range, or whose source id disagrees with the identifier.

SAFE cause codes: 1 split-brain, 2 no-quorum, 3 protocol error.

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
prevent the first collision.

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

**Single-leader invariant (REQ-002).** A node grants at most one vote per term.
A node observing any message with a higher term adopts it and steps down. These
two rules together make two leaders in the same term impossible under the
assumed fault model. The leadership lease further ensures that even under a
2+1 network partition, at most one node holds valid leadership authority at
any time (INV-LEADER-UNIQUE). Stale/replay rejection ensures that delayed or
replayed traffic cannot create a second valid authority.

**Split-brain detection (REQ-005).** If a leader nevertheless receives a
heartbeat from another node claiming leadership in the same term, the invariant
has been violated. The node latches SAFE immediately, inside the receive path,
and announces the cause on the bus. It does not attempt to arbitrate.

**Degraded operation.** A node that observes a peer in SAFE moves from NOMINAL
to DEGRADED while continuing to operate.

**Crash and restart (Lot 2D).** A crashed node transmits, receives and
services nothing. A restarted node starts from term 0 with no vote and no
knowledge of the cluster; it adopts the current term from the first
higher-term message it receives and cannot regain former authority.

## 8. Assumed fault model

Covered: loss of power of any single node, including the leader; loss of
heartbeats; corrupted frames; partition leaving fewer than quorum nodes.

Not covered: byzantine nodes, bus-off recovery, transceiver stuck-dominant
faults, clock drift beyond the tolerance implied by the timeout margins.

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

The remaining requirements of MOSAIK-ADD-0001 are out of scope for this bench.
