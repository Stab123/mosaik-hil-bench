# MOSAIK HIL Bench

A three-node hardware-in-the-loop bench for the MOSAIK distributed avionics
architecture: heartbeat, leader election, quorum and SAFE-mode logic over CAN,
with a test suite traced to the architecture requirements.

Parent work: MOSAIK-ADD-0001, a TRL 3 architectural design dossier for a
distributed fault-tolerant avionics architecture for LEO constellations
(Swiss patent CH000441/2026).

## Status

**Level 1 — logic verification on host: complete.** The protocol core builds
with `-Wall -Wextra -Werror` and passes 360 checks across 50 test cases, run by
CI on every push. Lot 2A adds a 500 ms leadership lease to prevent an isolated
leader from retaining authority indefinitely during a 2+1 network partition.
Lot 2B adds stale/replay message immunity using semantic rejection based on
term monotonicity, lease validity, and sender state. Lot 2C adds a directional
network fault model and restricts lease renewal to acknowledgements actually
received by the leader. Lot 2D adds crash and cold-restart models and a
randomised candidate retry backoff that stops a split vote from persisting in
lock-step (see `LOT2D_CRASH_RECOVERY_REPORT.md`). Lot 3 formalises and tests
the SAFE contract and makes DEGRADED persist on fresh, locally received peer
SAFE evidence; its adversarial tests also exposed and closed a LOT 2 erratum
in same-term vote memory (see `LOT3_FDIR_SAFE_REPORT.md`).

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
- **Authority evidence integrity (Lot 2C): a leader's lease renews only on
  acknowledgements it actually received in its current term, never on its
  own heartbeat delivery — verified by TC-013 through TC-020.**
- **Crash, cold restart and split-vote recovery (Lot 2D): a candidate whose
  election fails waits a randomised local backoff before retrying, so two
  colliding candidates do not retry in lock-step until SAFE — verified by
  TC-021 through TC-034, with the defect captured RED at commit `d38985d`
  before the fix.**
- **FDIR / SAFE semantics (Lot 3): a SAFE node holds no authority, never
  votes, acknowledges or runs, transmits SAFE announcements only and is
  latched for the lifetime of one powered node instance; DEGRADED is held
  while a peer's received SAFE evidence is fresh (3 heartbeat periods) and
  does not revoke authority; the cluster recovers around a SAFE node —
  verified by TC-035 through TC-050, captured RED at commit `af5da87` before
  the corrections `034db92` and `7df0af0`.**

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
- **Impossibility of a split vote, or a proof of election convergence. Lot 2D
  shows recovery from the tested collision cases in the deterministic host
  model (628 ms and 680 ms after the crash); it does not bound the hardware
  worst case and does not model sub-millisecond bus races.**
- **Term or vote persistence across restart. A restart is a cold start, and
  it also clears SAFE: SAFE is not persisted across a power cycle.**
- **Ground arbitration (PGA), a SAFE exit API, a PROTO_ERROR policy, safety
  discretes, watchdogs, or any hardware FDIR. Malformed frames are counted
  and never cause SAFE.**

## Build and test
