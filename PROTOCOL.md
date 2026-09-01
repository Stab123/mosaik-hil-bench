# MOSAIK HIL Bench — Wire Protocol

**Document:** MOSAIK-HIL-PROTO-001
**Issue:** 0.1 — 1 September 2026
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
| Heartbeat | `0x200 + node_id` | lowest |

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
| 6 | Argument — heartbeat: sequence number; vote grant: target node id; SAFE: cause code |
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

The election timeout is drawn from a per-node deterministic xorshift sequence
seeded by node id, so that simulation runs are reproducible and nodes do not
contend indefinitely.

## 7. State machine

Roles are follower, candidate and leader. States are INIT, NOMINAL, DEGRADED
and SAFE.

**Election.** On election timeout a follower increments its term, becomes
candidate, votes for itself and broadcasts a vote request. On receiving grants
from a quorum it becomes leader and immediately broadcasts a heartbeat. A
candidate that does not reach quorum before the vote timeout increments its
failure count and starts a new election; after three consecutive failures it
latches SAFE with cause no-quorum.

**Single-leader invariant (REQ-002).** A node grants at most one vote per term.
A node observing any message with a higher term adopts it and steps down. These
two rules together make two leaders in the same term impossible under the
assumed fault model.

**Split-brain detection (REQ-005).** If a leader nevertheless receives a
heartbeat from another node claiming leadership in the same term, the invariant
has been violated. The node latches SAFE immediately, inside the receive path,
and announces the cause on the bus. It does not attempt to arbitrate.

**Degraded operation.** A node that observes a peer in SAFE moves from NOMINAL
to DEGRADED while continuing to operate.

## 8. Assumed fault model

Covered: loss of power of any single node, including the leader; loss of
heartbeats; corrupted frames; partition leaving fewer than quorum nodes.

Not covered: byzantine nodes, bus-off recovery, transceiver stuck-dominant
faults, clock drift beyond the tolerance implied by the timeout margins.

## 9. Traceability

| Requirement (MOSAIK-ADD-0001) | Section | Test case |
|---|---|---|
| REQ-002 exactly one leader, split-brain prohibited | 7 | TC-001, TC-002, TC-004 |
| REQ-003 quorum-based reconfiguration | 6, 7 | TC-005 |
| REQ-004 election within 1 s of leader loss | 6, 7 | TC-003 |
| REQ-005 SAFE within 10 ms of invariant violation | 7 | TC-004 |

The remaining requirements of MOSAIK-ADD-0001 are out of scope for this bench.
