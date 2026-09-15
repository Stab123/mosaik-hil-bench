# MOSAIK HIL Bench

A three-node hardware-in-the-loop bench for the MOSAIK distributed avionics
architecture: heartbeat, leader election, quorum and SAFE-mode logic over CAN,
with a test suite traced to the architecture requirements.

Parent work: MOSAIK-ADD-0001, a TRL 3 architectural design dossier for a
distributed fault-tolerant avionics architecture for LEO constellations
(Swiss patent CH000441/2026).

## Status

**Level 1 — logic verification on host: complete.** The protocol core builds
with `-Wall -Wextra -Werror` and passes 55 checks across 12 test cases, run by
CI on every push. Lot 2A adds a 500 ms leadership lease to prevent an isolated
leader from retaining authority indefinitely during a 2+1 network partition.
Lot 2B adds stale/replay message immunity using semantic rejection based on
term monotonicity, lease validity, and sender state.

**Level 2 — timing measurement on hardware: not started.** Hardware not yet
procured. No measured latency is reported anywhere in this repository.

## What this repository demonstrates

- A wire protocol specified before implementation, with identifiers, payload
  layout, CRC and timing parameters fixed in `PROTOCOL.md`.
- A node state machine with no platform dependency — no OS, no heap, no
  floating point — driven by an injected clock and a transmit callback, so the
  identical code runs on the host bench and on the target.
- A test suite in which each case names the requirement it exercises.
- Single-leader and quorum invariants enforced by construction: one vote per
  term, step down on a higher term, quorum required to take leadership.
- Split-brain treated as a violation to be latched, not a condition to
  arbitrate: a leader observing a same-term peer leader enters SAFE inside the
  receive path.
- **Leadership lease (500 ms) with explicit valid leadership authority
  predicate (`mosaik_has_valid_leadership_authority()`), verified under a
  deterministic 2+1 network partition test (TC-007).**
- **Stale/replay message immunity (Lot 2B): term monotonicity enforcement,
  duplicate sequence rejection, expired lease replay rejection, and partition
  recovery safety — verified by TC-008 through TC-012.**

## What it does not demonstrate

- Any measured timing. The intervals printed by the test suite are simulated
  time on a virtual bus.
- Any behaviour on real CAN hardware: no transceiver, bus-off handling,
  arbitration under load, or clock drift between physical nodes.
- Raft. Election is timeout-and-priority based with per-term voting and a
  quorum rule, inspired by Raft but without log replication, persistence or
  membership change.
- Anything about parts quality. The bench targets commercial development
  boards. No radiation tolerance, derating, thermal or vibration argument is
  available, and none is claimed. A flight architecture would assume a
  radiation-tolerant MCU and a separate qualification campaign.
- **Physical CAN-FD timing validation. The leadership lease and stale/replay
  immunity are host-model parameters only. No hardware network partition
  testing.**
- **Cryptographic anti-replay or sequence-number protection. The current
  8-byte frame format has no room for correlation_id. Lot 2B uses semantic
  rejection only. LOT 6 will handle final wire-format decisions.**

## Build and test
