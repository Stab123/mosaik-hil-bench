# LOT 2D Crash / Restart / Recovery and Candidate Retry Backoff — Evidence Report

**Document:** MOSAIK-LOT2D-001
**Issue:** 0.1 — 16 September 2026
**Parent:** MOSAIK-ADD-0001 (architectural design document, TRL 3)
**Branch:** `lot2c-network-adversarial`
**Validated commit:** `f4e0f3c1606766ac9b5b3332964e3cdbe5f1e2ea`

**HOST SOFTWARE DEMONSTRATOR ONLY — NO HARDWARE VALIDATION. Do not claim TRL 4.**

Everything in this report was obtained from the deterministic host
demonstrator: three protocol-core instances on a virtual bus in simulated
millisecond time. Every millisecond value below is simulated, not measured.

---

## 1. Scope

LOT 2D covers, in the host demonstrator only:

- a crash model (loss of all volatile state, no transmission, no reception,
  no service) and a restart model (cold re-initialisation with no persisted
  term or vote), applied to followers, leaders and candidates;
- the test cases TC-021 through TC-030 that exercise those models;
- the finding raised by TC-028 (leader crash near lease expiry), its
  root-cause analysis, and the protocol correction that closes it
  (candidate retry backoff, design option "C2-a");
- the test cases TC-031 through TC-034 that capture the defect before the
  correction and validate the correction after it;
- the LOT 2C harness-integrity correction (`c6f600b`) that the TC-028
  investigation exposed and that was applied before the protocol change.

Out of scope: term or vote persistence across restart (not implemented, see
section 13), any physical bus, any timing measurement, and any hardware.

---

## 2. Requirements and invariants exercised

| Requirement / invariant | Meaning in this lot | Exercised by |
|---|---|---|
| REQ-002 | exactly one leader, split-brain prohibited | TC-021 to TC-034 (max concurrent valid authorities) |
| REQ-003 | quorum-based reconfiguration: no authority without quorum | TC-032, TC-033 |
| REQ-004 | new leader within 1000 ms of leader loss | TC-022, TC-028, TC-031 |
| INV-LEADER-UNIQUE | at most one valid leadership authority at every observation point | every LOT 2D test |
| Term monotonicity (Lot 2B) | a node never lowers its term | TC-023, TC-025, TC-026, TC-031 to TC-034 |
| One vote per term | `voted_for` is set at most once per `voted_term` | TC-029, TC-031, TC-032 |
| SAFE / NO_QUORUM contract | three consecutive genuine failed elections latch SAFE | TC-032, TC-033 |
| Lot 2C authority evidence | lease renews only on actually received current-term ACK from a peer | TC-028 setup, TC-013, TC-019 |

---

## 3. Crash / restart model

Implemented entirely in the harness (`test/test_mosaik.c`); the protocol core
is unaware of it.

**Crash, `bus_crash_node()`.** Sets a `crashed` flag. From that step on the
node's transmit callback discards every frame, the bus never calls
`mosaik_on_rx()` or `mosaik_tick()` for it, and its struct is left untouched
but unreachable. Frames the node had already placed on the bus stay in the
network and are delivered under the normal network model. This stands in for
a power loss: a real node loses its RAM and its bus presence at once.

**Restart, `bus_restart_node()`.** Only a crashed node can restart. The node
is re-initialised with `mosaik_init()` at the current bus time: term 0,
`voted_for` 0, `voted_term` 0, state INIT, role follower, per-node RNG
re-seeded from its node id. Nothing is carried over. This is the current
architecture: there is no persistent storage in the protocol core.

**Distinction that matters for TC-028.** TC-028, TC-031, TC-032, TC-033 and
TC-034 crash the leader and never restart it. Persistence of term or vote
state is therefore not on the causal path of any of those tests; the two
survivors keep every field intact throughout.

---

## 4. Test matrix, TC-021 through TC-034

Executed check counts at `f4e0f3c`; all pass.

| ID | Title | Purpose | Checks |
|---|---|---|---|
| TC-021 | follower crash while leader/quorum available | leader keeps authority; restarted follower adopts current term | 8 |
| TC-022 | leader crash | new leader elected, term advanced, single valid authority | 5 |
| TC-023 | leader crash, election, former leader restarts | cold-restarted former leader adopts the new term, does not regain authority | 7 |
| TC-024 | crashed follower restart and rejoin | vote state reset on cold restart, rejoin without disturbance | 6 |
| TC-025 | former leader restarts after another leader elected | no stale authority, term monotonic | 8 |
| TC-026 | restart with delayed pre-crash messages queued | delayed pre-crash frames rejected as stale after restart | 4 |
| TC-027 | repeated crash/restart of one node | five cycles, no duplicate vote, no authority regression | 5 |
| TC-028 | crash during/near lease expiry | leader isolated from ACKs, crashed 50 ms before lease expiry; new valid leader required | 4 |
| TC-029 | crash during election (candidate) | candidate crashed mid-election, restarted cold, no duplicate vote in the original term | 9 |
| TC-030 | recovery under asymmetric network | crash and restart under a directional fault; single valid leader | 3 |
| TC-031 | natural election collision, randomized retry recovery | natural split vote, recovery by randomized retry, REQ-004 in host model | 13 |
| TC-032 | permanent retry contention keeps SAFE contract | backoff span 1: retries stay synchronized, SAFE/NO_QUORUM after three genuine failures | 10 |
| TC-033 | asymmetric partition during retry | one direction cut after the collision: no authority, SAFE only via genuine failures | 10 |
| TC-034 | stale delayed election traffic during retry | delayed old-term VOTE_REQ and replayed VOTE_GRANT rejected, no term regression | 10 |

TC-031 to TC-034 never write a protocol internal. Collisions are created by
crashing the leader at a naturally occurring instant at which both followers
already hold the same deadline, found by read-only observation of normal
protocol execution with the real per-node seeds. Adversarial behaviour uses
only the existing directional network model and the delayed-frame scheduler.

---

## 5. Initial TC-028 finding

At the LOT 2D diagnostic checkpoint `9e3f061`, the suite reported 160 checks
and 1 failure: TC-028, "new leader elected after crash near expiry".

Observed sequence (leader node 2, survivors nodes 1 and 3):

| now_ms | Event |
|---|---|
| 2000 | followers' ACKs to the leader dropped by the test, so the lease cannot renew |
| 2015 | harness still renewed the lease to 2515 from stale pre-isolation evidence (see section 6) |
| 2315 | last heartbeat received by both survivors; both drew jitter 7, deadline 2822 for both |
| 2365 | leader crashed, 150 ms before expiry instead of the intended 50 |
| 2822 | both survivors started an election for term 2 in the same step and self-voted |
| 2823 | each received the other's VOTE_REQ and rejected it (one vote per term) |
| 2972, 3122 | both timed out together and retried together, terms 3 and 4 |
| 3272 | both latched SAFE with cause NO_QUORUM |

No VOTE_GRANT was ever transmitted after the crash. A sweep of every crash
offset from 0 to 500 ms before expiry showed 200 of 501 offsets failing, in
two 100 ms windows, each following a heartbeat at which both survivors drew
equal jitter. Every failing offset had equal survivor deadlines; no passing
offset did.

---

## 6. LOT 2C harness-integrity correction relevant to the investigation

Commit `c6f600b`, "fix: require actual received ACK evidence for leadership
lease". Harness only; no protocol file changed.

**Defect.** The virtual bus credited the leader with quorum-contact evidence
from two illegitimate sources: every delivered frame of any type, and the
delivery of the leader's own heartbeat to a follower whenever the reverse
path was configured as deliverable. The second source credited an ACK that
had not been received and, in TC-028, could not be received. The leader's
lease was therefore renewed from information a real leader cannot possess.

**Rule applied.** Leadership authority may be renewed only from fresh,
current-term, admissible contact actually received from another peer. For
the current ACK protocol, peer contact is credited only when an ACK frame is
actually delivered to a node holding the leader role and the node's own ACK
handler recorded it for its current term. Nothing is inferred from outbound
heartbeat delivery, reverse-path state, topology, powered or crashed state,
or any other frame type. The leader itself is the implicit member of the
2-of-3 quorum. Old-term frames are never accepted by the node and therefore
never credited. A delayed current-term ACK counts at the time it is actually
received, subject to the 100 ms freshness window.

**Effect.** Outbound heartbeat alone renews lease: NO. Fresh received quorum
evidence required: YES. During TC-028's isolation window eight heartbeats
reached the followers with all ACKs dropped; the original harness renewed
once, the corrected harness zero times. TC-028's crash moved from 2365 ms to
2366 ms with the intended 50 ms of lease remaining. TC-007 and TC-009 expiry
printouts moved by one millisecond because renewal now happens on ACK
receipt rather than on heartbeat delivery. No assertion outcome changed, and
TC-028 still failed by the identical post-crash mechanism. The harness defect
was therefore not the cause of TC-028.

Note on the lease bookkeeping: the protocol core records per-peer ACK
evidence in its ACK handler, but the write to `lease_expiry_ms` on renewal is
performed by the harness from that recorded evidence. This is harness-mediated
renewal, as already declared for Lot 2A; it now uses only evidence the node
itself accepted.

---

## 7. RED baseline at `d38985d`

Commit `d38985d`, "test: capture candidate retry collision failure before
LOT 2D fix". Tests TC-031 to TC-034 added, plus a configuration-taking bus
initialiser and read-only observers. No protocol file changed. TC-028 and
every existing assertion untouched.

Result on that commit: 201 checks, 7 failures.

| Failure | Origin |
|---|---|
| TC-028: new leader elected after crash near expiry | pre-existing |
| TC-031: retry deadlines desynchronized after the first failed election | intentional RED |
| TC-031: survivor recorded exactly one failed election (node 1) | intentional RED, observed 3 |
| TC-031: survivor did not enter SAFE (node 1) | intentional RED |
| TC-031: survivor recorded exactly one failed election (node 3) | intentional RED, observed 3 |
| TC-031: survivor did not enter SAFE (node 3) | intentional RED |
| TC-031: new valid leader elected after natural collision | intentional RED |

TC-032, TC-033 and TC-034 passed on this commit: the SAFE contract, the
no-authority-without-quorum rule and stale-traffic rejection already held.
TC-001 to TC-030 output was byte-identical to `c6f600b`.

---

## 8. Root-cause analysis

**Causal chain.**

1. Both followers receive the same heartbeat in the same step. Each sets its
   deadline to now plus the 500 ms lease plus a jitter of `rng % 50`
   (Lot 2A follower alignment). The jitter draws coincide.
2. The leader crashes. No further heartbeat arrives, so nothing re-draws the
   deadline.
3. At the shared deadline both survivors call `start_election()` in the same
   bus step. Each increments its term, records a self-vote, and only then
   emits VOTE_REQ. Each request reaches the other one step later and fails
   the one-vote-per-term test.
4. `start_election()` set the candidate deadline to now plus a fixed
   `vote_timeout_ms` of 150 ms. `election_timeout()` is never called on the
   candidate path. The RNG state of both nodes is frozen from the last
   heartbeat until SAFE.
5. Both time out in the same step, retry in the same step, collide again,
   and after the third failure latch SAFE.

**Why the deadlines were equal.** RNG coincidence, not shared state. The
two survivors have different seeds (0x00000008 and 0x449114AA) and identical
event histories, so they are always on the same draw index. At draw 24 both
streams are 7 modulo 50. Over 20000 aligned draws the streams coincide
modulo 50 about once in 46, against once in 50 expected. The 50-value lease
jitter makes such a coincidence about four times more likely per heartbeat
than the 201-value election timeout it replaced for followers.

**Classification.** The persistence of the collision is a protocol defect:
a candidate retry with a fixed timeout and no fresh randomness, which
contradicts the protocol's own statement that randomisation ensures nodes
"do not contend indefinitely". It is amplified by quorum 2 with one node
dead, where any equal-term collision is a lost round, and by the SAFE
threshold of three. The deterministic harness, with its shared clock and
one-step delivery, turns integer-equal deadlines into strictly simultaneous
events and resolves the sub-millisecond race pessimistically every round; it
guarantees the collision whenever the draws match but does not invent it.
The test itself was sound; its only imprecision (150 ms rather than 50 ms
lease remaining) came from the harness defect of section 6.

**CAN arbitration verdict.** The claim that real CAN or CAN-FD arbitration
would serialise the simultaneous VOTE_REQs and let one candidate win is
FALSE for this implementation. Arbitration orders frames on the wire; it
cannot retract a vote committed in local RAM before the frame was queued.
No branch of the receive path abandons a candidacy on an equal-term
VOTE_REQ. The harness already delivers the lower identifier first, and
reversing the order gives the same SAFE outcome. Only a candidate whose
timer has not yet fired when the other's request arrives converges, which is
a timing difference, not arbitration.

**Persistence verdict.** Not involved. No restart occurs before the failed
assertion.

**Hidden information.** None in the protocol core: nodes read only their own
struct and received frames. The harness idealisations (shared clock,
zero-jitter delivery, lock-step order) are not information leaks.

---

## 9. C2-a design decision

Alternatives considered, all evaluated against safety, the REQ-004 budget in
the host model, and causal minimality:

| Option | Description | Verdict |
|---|---|---|
| B | re-randomise the retry with the 300–500 ms `election_timeout()` range | rejected: one collision can exceed 1000 ms in the host model |
| C1 | add a random term to every candidate deadline in `start_election()` | rejected: changes the first attempt and every election's RNG consumption |
| C2-b | randomise only the retry's deadline, but still emit the retry at the failed timeout | rejected: both retries are still emitted in lock-step, so a second collision is guaranteed |
| D | deterministic yield by node id | rejected: introduces priority semantics absent from the protocol, trusts the frame role byte, and is no faster than C2-a with two survivors |
| **C2-a** | wait a local random backoff after a genuine failed timeout, before the next VOTE_REQ | **accepted** |

**Accepted semantics.** The first election attempt is unchanged and
`vote_timeout_ms` remains 150 ms. When a candidate's vote timeout expires
without quorum: `failed_elections` increments exactly as before; if it
reaches `max_failed_elections` (still 3) the node enters SAFE exactly as
before; otherwise the node drops to the follower role, clears only the votes
it had collected, keeps its term, `voted_for` and `voted_term`, and sets its
deadline to now plus `rng % candidate_retry_backoff_span_ms` using only its
own per-node RNG. When that deadline expires the ordinary follower path
calls `start_election()`, which advances the term. The wait is never counted
as a failure. A span of S gives exactly S possible deterministic waits, 0 to
S−1 ms; span 1 gives 0 ms on every node; 0 is treated as 1. Default span:
50 ms, matching the existing heartbeat jitter width.

**Location.** `firmware/core/mosaik_node.h` (configuration field),
`firmware/core/mosaik_node.c` (`mosaik_config_default()` and the candidate
branch of `mosaik_tick()`). `start_election()`, `become_follower()`,
`become_leader()`, `enter_safe()`, the VOTE_REQ, VOTE_GRANT and ACK
handlers and the authority predicate are byte-identical to the parent
commit.

**What C2-a does and does not do.** It inserts fresh local randomness after
a genuine failed election and before the next VOTE_REQ, which breaks the
lock-step that made a split vote persist. It does not prevent the first
collision, which remains possible. It uses no node-id priority, no topology
knowledge, no shared randomness and no property of the bus.

---

## 10. Final validation at `f4e0f3c`

| Check | Result |
|---|---|
| Strict build, `-std=c99 -Wall -Wextra -Werror -O1` | clean |
| Full suite | 205 checks, 0 failures |
| AddressSanitizer + UndefinedBehaviorSanitizer | 0 findings, output identical to the plain run |
| Maximum concurrent valid authorities | 1 |
| Term regressions | 0 |
| Unexpected SAFE transitions | none; SAFE occurs only where asserted (TC-004, TC-005, TC-032, TC-033) |

Changed output relative to the RED baseline `d38985d`, over TC-001 to
TC-030: the TC-028 failure line is gone, and TC-029 prints `voted_term=4`
instead of `voted_term=5` because each retry now waits up to 49 ms longer,
so the surviving pair advanced one term fewer before the restarted node
adopted the cluster term. Every other line is identical.

**Retry timelines observed.**

| Event | TC-028 | TC-031 |
|---|---|---|
| leader crash | 2366 ms, node 2 | 2016 ms, node 2 |
| first shared election deadline | 2822 | 2528 |
| both self-vote and emit VOTE_REQ, term 2 | 2822 | 2528 |
| mutual VOTE_REQ rejected | 2823 | 2529 |
| first failed timeout, `failed_elections` 1 on both | 2972 | 2678 |
| backoff drawn, node 1 / node 3 | 40 / 19 ms | 15 / 41 ms |
| next election start | node 3 at 2991, term 3 | node 1 at 2693, term 3 |
| peer steps down and grants | 2992 | 2694 |
| VOTE_GRANT received, leader role | 2993 | 2695 |
| valid authority observed by the test | 2994 | 2696 |
| recovery from crash | 628 ms | 680 ms |
| final term | 3 | 3 |
| `failed_elections`, winner / loser | 0 / 1 | 0 / 1 |

**TC-032** with `candidate_retry_backoff_span_ms = 1`: both survivors
retried in lock-step, no valid authority ever appeared, each recorded exactly
three genuine failed vote timeouts, and both latched SAFE with cause
NO_QUORUM at 2980 ms.

**TC-033**: with one direction between the survivors cut after the natural
collision, no valid authority ever appeared, no term regressed, and both
survivors reached SAFE only after three genuine failed timeouts.

**TC-034**: the delayed term-2 VOTE_REQ and the replayed term-2 VOTE_GRANT
were both rejected as stale, no term regressed, and the eventual leader held
term 3, above the collision term.

---

## 11. Safety observations

- Maximum concurrent valid leadership authorities observed at the defined
  deterministic observation points: 1, in every test of the suite.
- One vote per term is preserved through the backoff: `voted_for` and
  `voted_term` are never written on that path, and a same-term VOTE_REQ
  arriving during the wait is rejected by the unchanged check. The TC-028
  and TC-031 traces show the waiting node as a follower with `voted_for`
  still equal to its own id.
- Term monotonicity is preserved: 0 regressions across the suite.
- The SAFE threshold and cause are unchanged. The third genuine failure
  latches SAFE at the timeout with no wait.
- The backoff cannot manufacture authority: leadership still requires a
  received grant from a peer (TC-032, TC-033).
- Stale election traffic cannot cancel the retry rules (TC-034).
- The LOT 2D summary printed at the end of the suite reflects only the last
  `bus_init()`, which is now TC-034; its "max concurrent valid authorities"
  line reads 0 for that reason, not because of a change in behaviour.

---

## 12. Liveness observations

- In the two tested collision cases the survivors recovered in 628 ms
  (TC-028) and 680 ms (TC-031) after the leader crash, below the 1000 ms of
  REQ-004, in the deterministic host model. These are two deterministic
  measurements, not a worst-case bound and not a hardware result.
- Arithmetic in the host model with the current constants, taking the worst
  case of a leader lost right after its heartbeat, maximum follower jitter
  and one millisecond per hop: no collision ≤ 552 ms, one collision
  ≤ 751 ms, two consecutive collisions ≤ 950 ms. A third failure latches
  SAFE, consistent with `max_failed_elections` = 3.
- The first collision remains possible. With followers on identical event
  histories it occurs whenever their aligned jitter draws coincide, about
  once per 50 heartbeats for the current seeds. What C2-a changes is that
  a collision no longer persists: the retry draws must also coincide, and
  then coincide again, before SAFE is reached.
- Two nodes with identical RNG state would still lock indefinitely under
  any RNG-based scheme. The per-node seeding by id prevents this; it is a
  precondition, not a guarantee proven here.
- No triple coincidence of the two survivors' streams modulo 50 was found in
  the first 6000 aligned draws. This is an observation about these seeds,
  not a probabilistic proof of convergence.

---

## 13. Known limitations

1. Host software demonstrator only. No physical CAN or CAN-FD, no
   transceiver, no bus-off handling, no clock drift, no hardware timing.
2. Deterministic simulation with a shared millisecond clock and one-step
   frame delivery. Deadlines that are equal in integer milliseconds are
   strictly simultaneous here; on hardware the same case is a
   sub-millisecond race that this bench does not model or validate.
3. Term and vote persistence across restart is not implemented. Restart is
   a cold `mosaik_init()`. The ROADMAP scope line "persistent terms" for
   LOT 2D is therefore not closed by this lot.
4. Lease renewal bookkeeping remains harness-mediated, now from the node's
   own recorded ACK evidence only.
5. Three nodes, quorum two, a single loss is the only tolerable fault.
6. Collisions are not impossible and their probability on hardware is not
   characterised.
7. No cryptographic anti-replay; stale rejection is semantic (Lot 2B).
8. LOT 2C has no dedicated evidence report. This is a documentation
   limitation only: TC-013 to TC-020 execute and pass in the deterministic
   host demonstrator and are traced per test in `verification/TRACEABILITY.md`
   section 6, `docs/ADD-MAPPING.md` section 5, `docs/TEST-PLAN.md` and
   PROTOCOL.md section 9.
9. Per-test traceability for TC-013 to TC-034 is complete in
   `verification/TRACEABILITY.md` section 6 (issue 1.2). Mapping to ADD
   Section 82 test cases is RELATED or NONE because that transcription does
   not yet contain crash, restart or retry test cases (ADD-F001, ADD-F006).

---

## 14. Reproducibility

Commit: `f4e0f3c1606766ac9b5b3332964e3cdbe5f1e2ea` on
`lot2c-network-adversarial`.

Normal build and run, from the repository root:

```
make test
```

which executes:

```
gcc -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/core \
    firmware/core/mosaik_proto.c firmware/core/mosaik_node.c \
    test/test_mosaik.c -o build/test_mosaik
./build/test_mosaik
```

Sanitizer build and run:

```
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    -fno-omit-frame-pointer -Ifirmware/core \
    firmware/core/mosaik_proto.c firmware/core/mosaik_node.c \
    test/test_mosaik.c -o build/test_mosaik_san
./build/test_mosaik_san
```

Expected: the last summary line reads `205 checks, 0 failures`, exit code 0,
and the sanitizer run prints no `runtime error`, AddressSanitizer or
LeakSanitizer report. Expected TC-028 line: `leader 2 crashed at 2366 ms
(lease remaining ~50 ms)`. Expected TC-031 line: `node 1 valid leader at
2696 ms (680 ms after loss), term 3`. Validated with gcc 13.3.0 on Linux;
the simulation is integer-only and deterministic, so other conforming C99
compilers are expected to reproduce the same values.

The RED baseline is reproducible from commit `d38985d` with the same
commands: expected `201 checks, 7 failures`, all failures in TC-028 and
TC-031.

---

## 15. Claim boundary

The current implementation is only a **deterministic host demonstrator of
MOSAÏK leadership authority semantics**.

This report does NOT claim: hardware validation; physical CAN or CAN-FD
validation; HIL validation; STM32 validation; FreeRTOS validation; flight
readiness; TRL 4; formal proof; probabilistic proof of election
convergence; impossibility of a split vote; production readiness;
cryptographic anti-replay; validation of sub-millisecond bus races.

REQ-004 wording: the tested TC-028 and TC-031 recovery cases complete in
628 ms and 680 ms respectively, below the 1000 ms requirement, in the
deterministic host model. These two measurements are not a universal or
hardware worst-case guarantee.

Evidence classification for every item in this report: IMPLEMENTED-SIM.
