# MOSAIK HIL Bench

A three-node hardware-in-the-loop bench for the MOSAIK distributed avionics
architecture: heartbeat, leader election, quorum and SAFE-mode logic over CAN,
with a test suite traced to the architecture requirements.

Parent work: MOSAIK-ADD-0001, a TRL 3 architectural design dossier for a
distributed fault-tolerant avionics architecture for LEO constellations
(Swiss patent CH000441/2026).

## Status

**Level 1 — logic verification on host: complete.** The protocol core builds
with `-Wall -Wextra -Werror` and passes 761 checks across 90 test cases, run by
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
in same-term vote memory (see `LOT3_FDIR_SAFE_REPORT.md`). Lot 4 separates
consensus from FDIR evidence in the four-state local mode model: starting an
election no longer degrades a node, and a SAFE announcement's term is no
longer adopted as a consensus epoch (see `LOT4_MODE_SEMANTICS_REPORT.md`).

Lot 5 replaces the fixed membership with an explicit committed membership mask
carrying its own configuration epoch, changed only by a PROPOSE/ACCEPT/COMMIT
transaction that requires a quorum of the old and of the new configuration (see
`LOT5_RECONFIGURATION_REPORT.md`).

**Level 2 — timing measurement on hardware: not started.** Hardware not yet
procured. No measured latency is reported anywhere in this repository.

## Scope boundary

This repository is a **deterministic experimental protocol and verification
bench**: a platform used to discover, reproduce, measure and validate
distributed autonomy and fault-tolerance behaviour.

It is **not** the complete MOSAÏK ADD implementation, and it is not required to
become one. The complete ADD-driven architecture — the full EN/CN/COMN node set,
the six-mode model including ADAPTIVE and PGA, the flight architecture and the
mission software — is planned as a **separate future project, MOSAÏK Advanced**.

The ADD remains this project's architectural parent and the source of its
hypotheses, requirements and findings, but the bench is free to deviate from it
where an experiment justifies the deviation; such deviations are recorded as
results, not treated as defects. The bench converges instead toward one
reproducible, instrumented, adversarial end-to-end experiment — healthy start,
leader kill, re-election, second loss, membership reconfiguration, function
redistribution, partition, refusal of an unsafe decision, SAFE where the safety
contract requires it, network heal, reconciliation, and full chronology
reconstruction from recorded evidence. That objective, the HIL/Advanced split
and the knowledge-transfer path between the two are defined in `ROADMAP.md`
sections 2, 4, 6 and 11.

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
- **Mode semantics (Lot 4): a fault-free election does not move a node
  INIT to DEGRADED (a healthy candidate may remain INIT), and a received
  SAFE announcement is FDIR evidence only: its term is not adopted, so a
  latched peer cannot step a valid leader down or force an election (the
  tested authority interruption went from 397 ms to 0 ms); heartbeat state
  metadata and out-of-range state bytes have no protocol effect — verified
  by TC-051 through TC-060, captured RED at commit `58a1b5d` before the
  correction `ae9e408`.**
- **Autonomous reconfiguration (Lot 5): membership is an explicit committed
  voter mask with its own configuration epoch, changed only by a distributed
  transaction whose commit requires a quorum of the old and of the new
  configuration; a removed node stops voting, acknowledging and standing for
  election without being treated as faulted, and a node's own committed
  configuration survives a cold restart — verified by TC-061 through TC-090,
  captured RED at commit `02b27fa` before the correction `146472f`, then
  attacked by 6236 bounded adversarial schedules at `9ccd28e` under a
  per-millisecond invariant oracle with no safety counterexample observed
  (see `LOT5_RECONFIGURATION_REPORT.md`).**

## What it does not demonstrate

- Any measured timing. The intervals printed by the test suite are simulated
  time on a virtual bus.
- Any behaviour on real CAN hardware: no transceiver, bus-off handling,
  arbitration under load, or clock drift between physical nodes.
- Raft. Election is timeout-and-priority based with per-term voting and a
  quorum rule, inspired by Raft but without log replication. Membership can be
  changed since Lot 5, by a two-phase transaction over an explicit committed
  membership rather than by Raft joint consensus over a replicated log.
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
  it also clears SAFE: SAFE is not persisted across a power cycle. Lot 5 adds
  one host-model configuration store per node, holding only that node's
  committed membership and its acceptance binding; it is a host model, not
  non-volatile-memory validation.**
- **Ground arbitration (PGA), a SAFE exit API, a PROTO_ERROR policy, safety
  discretes, watchdogs, or any hardware FDIR. Malformed frames are counted
  and never cause SAFE.**

## Build and test
