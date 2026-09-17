# LOT 4 Mode Semantics — Evidence Report

**Document:** MOSAIK-LOT4-001
**Issue:** 0.1 — 17 September 2026
**Parent:** MOSAIK-ADD-0001 (architectural design document, TRL 3)
**Branch:** `lot2c-network-adversarial`
**RED commit:** `58a1b5db708ded17dbfd0827cbe307656831a3de`
**GREEN commit:** `ae9e408a50d6f80257f77fa245247741295c0b7d`

**HOST SOFTWARE DEMONSTRATOR ONLY — NO HARDWARE VALIDATION. Do not claim TRL 4.**

Everything in this report was obtained from the deterministic host
demonstrator: three protocol-core instances on a virtual bus in simulated
millisecond time. Every millisecond value below is simulated, not measured.
This repository is an experimental deterministic distributed-protocol and
verification bench; it is not the complete MOSAÏK ADD implementation
(section 18 and section 22).

---

## 1. Scope

LOT 4 investigated, in the host demonstrator only, the local mode semantics
of the protocol core and the interaction between consensus evidence and FDIR
evidence. It started from the LOT 3 closure commit `8177e70` (50 tests, 360
checks, 0 failures) and ends at `ae9e408` (60 tests, 455 checks, 0 failures,
sanitizers clean). Two hypotheses were tested (section 4). Each was captured
RED by a dedicated test before any production change, then corrected by one
minimal change in `firmware/core/mosaik_node.c`. No new protocol message, no
new timer, no new RNG draw, no new state, no cluster-mode abstraction and no
harness-to-node information path was added.

Out of scope, and kept as future work in a separate project: ADAPTIVE and
PGA modes, the five-node and six-node ADD architectures, EN/CN/COMN roles,
ground arbitration, persistence, hardware, HIL and formal verification
(sections 17 and 18).

---

## 2. Experimental model

The frozen executable model is the three-node model of PROTOCOL.md issue
0.6 with four local states and three roles:

| States | Roles |
|---|---|
| INIT, NOMINAL, DEGRADED, SAFE | FOLLOWER, CANDIDATE, LEADER |

Timing parameters are those of PROTOCOL.md section 6: heartbeat 100 ms,
election timeout 300 to 500 ms drawn from a per-node xorshift RNG, vote
timeout 150 ms, leadership lease 500 ms, candidate retry backoff 0 to 49 ms,
SAFE evidence freshness 3 heartbeat periods, three failed elections before
SAFE/no-quorum. The harness is the LOT 2/LOT 3 virtual bus: one-millisecond
steps, a receive pass then a tick pass, a directional network model
(deliver, drop, delay, reorder), crash and cold restart, frame injection and
delayed delivery. Nodes decide only from received frames and local timers.
Harness knowledge is used for assertions only.

---

## 3. Preconditions and baseline

| Item | Value at `8177e70` (LOT 3 closure) |
|---|---|
| Tests | 50 (TC-001 through TC-050) |
| Checks | 360, 0 failures |
| Strict build `-std=c99 -Wall -Wextra -Werror -O1` | clean |
| ASan, UBSan | clean, output identical to the strict run |
| Output digest | md5 `223f69ad9760b3c25077cbe2336c41de` |

The LOT 4 pre-implementation audit and specification freeze (not committed
as documents) fixed the two decisions below before any test was written:
C1 CHANGE, C2 CHANGE, ADAPTIVE and PGA DEPENDENCY-BLOCKED, no executable
leader-announced cluster mode, no new semantics for heartbeat payload byte 3.

---

## 4. Hypotheses C1 and C2

**C1 — election is not degradation.** A fault-free election is consensus
activity. Starting an election must not, by itself, move a node from INIT
to DEGRADED. Under the frozen LOT 3 model DEGRADED is associated only with
actually received peer SAFE evidence. A healthy candidate may remain INIT.

**C2 — a SAFE announcement is FDIR evidence, not a consensus epoch.**
`MOSAIK_MSG_SAFE` carries the sender's term, but that term is not sufficient
evidence of a newer leadership epoch. A higher-term SAFE announcement must
not, by itself, raise survivor terms, demote a valid leader, invalidate
authority, modify voting state, renew or destroy a lease, or force an
election. It may and must still update the receiver's SAFE evidence
(`last_safe_rx_ms[]`, `safe_evidence_mask`) and DEGRADED state according
to LOT 3.

Both hypotheses were expected to be false on the LOT 3 closure commit.
Both were.

---

## 5. Invariants

Preserved from LOT 2 and LOT 3: INV-LEADER-UNIQUE, INV-SAFE-NO-AUTHORITY,
INV-SAFE-LATCH, INV-NO-STALE-RECOVERY, INV-TERM-MONOTONIC, INV-FDIR-NO-MAGIC,
INV-DEGRADED-PERSISTENT, INV-ONE-VOTE-PER-TERM.

Added observation and assertion coverage in LOT 4:

| Invariant | Statement | Evidence |
|---|---|---|
| INV-MODE-LEGAL | Only these state × role pairs occur: INIT with FOLLOWER or CANDIDATE; NOMINAL and DEGRADED with any role; SAFE with FOLLOWER only. Every emitted state byte is 0..3 and every emitted role byte is 0..2. | TC-051, TC-057, TC-058 |
| INV-MODE-NO-MAGIC | A node enters DEGRADED only while holding SAFE evidence actually received from a peer; a cold restart clears that evidence. | TC-052 (RED then green), TC-057, TC-058 |
| INV-MODE-METADATA-NONAUTHORITATIVE | The state byte of a received frame is metadata: it changes no role, term, vote, evidence, authority or state on its own, and an out-of-range value is rejected by the decoder. | TC-054, TC-055, TC-056 |
| INV-SAFE-ANNOUNCE-NO-AUTHORITY-EFFECT | A received SAFE announcement, whatever its term, leaves the receiver's role, term, votes, leader identity, lease and election timing unchanged. | TC-053 (RED then green), TC-059 |

DEGRADED + CANDIDATE is legal: a candidate may hold fresh peer SAFE evidence
(observed in the split-brain scenario of TC-051, 2 node-steps at GREEN). On
the RED baseline it was also the C1 symptom; TC-051 was deliberately written
not to fail for that reason, so that TC-052 alone carries the C1 evidence.

No ADAPTIVE, PGA or cluster-mode invariant exists in this bench.

---

## 6. RED methodology

Phase 1 modified only `test/test_mosaik.c`. Rules honoured, as in LOT 3:

- no protocol internal (term, voted_for, voted_term, role, state, lease,
  deadline, RNG, evidence mask) is ever written from a test;
- faults are injected only through the directional network model, crash
  and cold restart, frame injection, delayed delivery and the TC-004 style
  targeted delivery of one frame to one node;
- the two RED tests use ordinary deterministic startup and ordinary timers;
  nothing was tuned to produce the failure;
- harness observation added: raw emitted state and role byte counters and a
  copy of each node's last heartbeat frame, read in the transmit callback;
  neither is readable by a node;
- expected RED tests were named before execution: exactly TC-052 (C1) and
  TC-053 (C2). Any other failure would have stopped the phase.

TC-052 observes a fault-free boot from cold init until the first heartbeat
of a stable cluster and records, per step, every node's role, state, term
and evidence mask. TC-053 boots the cluster, isolates one follower in both
directions until it genuinely exhausts three elections and latches
SAFE/no-quorum, restores delivery, and lets the SAFE node's own periodic
announcement reach the majority; it measures the authority interruption
rather than prescribing it.

---

## 7. RED observations, `58a1b5d`

| Item | Value |
|---|---|
| Tests, checks, failures | 60, 455, 7 |
| Failing tests | TC-052 (2 checks), TC-053 (5 checks); no other |
| TC-001 through TC-050 | all pass; output byte-identical to `8177e70` |
| Strict build, ASan, UBSan | clean; sanitizer output identical to the strict run |

**TC-052 trace.** Node 2 at 312 ms of simulated time: role CANDIDATE, state
DEGRADED (previous step INIT), term 1 (previous 0), SAFE evidence mask 0x00.
The DEGRADED run lasted 2 ms. Node 2 held valid authority at term 1 from
315 ms, the cluster was recovered at 316 ms and the first stable heartbeat
was at 415 ms. No frame was dropped or delayed, no node crashed and no SAFE
frame was transmitted or received. TC-051 counted 10 DEGRADED node-steps
without evidence over its five scenarios: the 2 ms transient of each boot.

**TC-053 trace.** Leader node 2, term 1. Follower node 1 isolated at
2000 ms; SAFE/no-quorum at 2940 ms, term 4; delivery restored at 2941 ms.
The first SAFE evidence appeared on both survivors at 2942 ms and, in the
same step, both survivor terms went from 1 to 4, equal to the SAFE node's
term, with no VOTE_REQ emitted by any survivor in that step and no frame of
any type other than SAFE emitted by the SAFE node after its latch. The
leader lost its role at 2942 ms; a survivor election followed (1 VOTE_REQ);
authority was restored at 3339 ms by node 3 at term 5. Observed authority
interruption: 397 ms. The SAFE node stayed SAFE and emitted 0 VOTE_GRANT and
0 ACK. Survivors ended DEGRADED with masks 0x01 (LOT 3 held).

---

## 8. Finding L4-C1

**Observed behaviour.** `start_election()` at `8177e70` contained, after the
vote deadline and before the VOTE_REQ emission:

```
if (n->state == MOSAIK_STATE_INIT) {
    n->state = MOSAIK_STATE_DEGRADED;
}
```

Every node that started an election from INIT became DEGRADED. In the
fault-free boot this happened to node 2 at 312 ms with an empty evidence
mask; the state was overwritten to NOMINAL 2 ms later by `become_leader()`.

**Scientific interpretation.** The transition conflated consensus progress
with FDIR degradation. Under the frozen LOT 3/LOT 4 semantics DEGRADED
requires received peer SAFE evidence; none existed. The transient had no
authority or safety consequence, but it made the emitted state metadata
and every local mode observation wrong during each boot election, and it
violated the evidence rule by an internal transition.

**Correction at GREEN.** The assignment was removed. `start_election()` no
longer modifies the local operational state. Term increment, CANDIDATE role,
self vote, `voted_term`, vote mask, leader identity reset, vote deadline and
VOTE_REQ transmission are unchanged. NOMINAL is not set either: it still
arises only from `become_leader()` or an accepted heartbeat. A healthy
candidate may remain INIT (TC-051 at GREEN: 10 INIT + CANDIDATE node-steps,
0 DEGRADED node-steps without evidence).

This finding is limited to the current four-state host model. It is
registered as ADD-F012.

---

## 9. Finding L4-C2

**RED mechanism.** In `mosaik_on_rx()` the generic higher-term adoption

```
if (msg.term > node->term) {
    become_follower(node, msg.term);
}
```

executed after decoding, the own-frame discard and the SAFE gate, but before
`switch (msg.type)`. It therefore ran for a received `MOSAIK_MSG_SAFE` frame
before the SAFE-specific handler, which itself never used the term.

**RED experiment.** A follower isolated in both directions exhausted three
elections and latched SAFE/no-quorum at term 4, three terms above the
cluster's term 1, exactly as LOT 3 specifies. After reconnection its
genuine periodic SAFE announcement reached the healthy survivors. Before
correction: survivor terms changed from 1 to 4 in the step of the first
SAFE evidence; the valid leader stepped down; a new election occurred; the
observed authority interruption was 397 ms. The message was not forged and
no field was written by the harness: the term was the SAFE node's own.

**Scientific interpretation.** The implementation treated the term field of
an FDIR announcement as consensus epoch evidence. A node that has left
consensus could therefore interrupt the valid leader once per
reconnection, without any leader fault. The LOT 3 report had recorded this
as limitation 5 and left it unchanged; LOT 4 changed it.

**Correction at GREEN.** The condition became

```
if (msg.type != MOSAIK_MSG_SAFE && msg.term > node->term) {
    become_follower(node, msg.term);
}
```

Consensus-bearing types keep the existing adoption: HEARTBEAT (follow a
higher-term leader), VOTE_REQ (adopt, then grant), VOTE_GRANT and ACK
(adopt, then the handler ignores a non-matching term). A SAFE frame reaches
only its evidence handler. SAFE still contributes received FDIR evidence
through `last_safe_rx_ms[]`, `safe_evidence_mask` and the LOT 3 DEGRADED
semantics: it has an FDIR and state effect but no direct consensus-term
authority. The wire format, the sender's term and the received frame are
untouched. This is registered as ADD-F013 (host interpretation, normative
confirmation pending).

---

## 10. Root-cause analysis

| | C1 | C2 |
|---|---|---|
| Defective statement | `start_election()` INIT → DEGRADED assignment | generic term adoption placed before type dispatch |
| Trigger | any election started from INIT (every boot) | any SAFE frame whose term exceeds the receiver's (every no-quorum SAFE node that reconnects) |
| Evidence path violated | DEGRADED without received peer SAFE evidence (INV-MODE-NO-MAGIC) | FDIR metadata used as consensus evidence (INV-SAFE-ANNOUNCE-NO-AUTHORITY-EFFECT) |
| Safety invariants | never violated: at most one valid authority, no term regression | never violated: at most one valid authority, no term regression |
| Liveness effect | none | one avoidable leader step-down and re-election per reconnection |
| Minimal correction | delete the assignment | exclude `MOSAIK_MSG_SAFE` from the adoption condition |
| Second-order effects checked | NOMINAL still from leadership or heartbeat only; INIT + CANDIDATE and INIT + FOLLOWER remain legal; INIT + LEADER unreachable | one vote per term, same-term vote memory (TC-050), stale and replay rejection, ACK quorum evidence, lease renewal, split-brain SAFE, SAFE latch, DEGRADED evidence and expiry, retry backoff, crash and cold restart: code paths untouched, tests pass |

---

## 11. Production corrections, `ae9e408`

One commit, one file: `firmware/core/mosaik_node.c`, 12 insertions and 5
deletions, of which 3 deleted and 1 changed executable lines; the rest are
comments. `git diff 58a1b5db..ae9e408a -- test/` is empty: no test was added,
changed or weakened between RED and GREEN.

| Location at `ae9e408` | Change |
|---|---|
| `mosaik_node.c:153-168` `start_election()` | INIT → DEGRADED assignment removed (C1) |
| `mosaik_node.c:256` `mosaik_on_rx()` | `msg.type != MOSAIK_MSG_SAFE &&` added to the higher-term adoption (C2) |

---

## 12. GREEN evidence, `ae9e408`

| Item | Value |
|---|---|
| Tests, checks, failures | 60, 455, 0 |
| TC-052, TC-053 | green, without any test change |
| TC-053 authority interruption | 397 ms at RED, 0 ms at GREEN, in the tested deterministic scenario |
| Strict build | clean |
| ASan, UBSan | clean; output identical to the strict run |
| Two complete runs | byte-identical |

**TC-052 at GREEN.** First candidacy: node 2 at 312 ms, state INIT, mask
0x00. No DEGRADED without evidence observed. First valid leader node 2, term
1, at 315 ms; cluster recovered at 316 ms; first stable heartbeat at 415 ms.
Election timing is identical to RED: only the state during the candidacy
changed.

**TC-053 at GREEN.** Same scenario, same times up to the SAFE latch
(2940 ms, term 4) and the first SAFE evidence (2942 ms). Survivor terms at
the evidence step: 1 and 1; leader role kept; no VOTE_REQ after restore;
SAFE node 0 VOTE_GRANT and 0 ACK; end states leader DEGRADED, follower
DEGRADED, SAFE node SAFE, masks 0x01 and 0x01; authority interruption 0 ms.
The SAFE evidence and the DEGRADED effect are unchanged from RED; only the
consensus effect disappeared.

**TC-051 at GREEN**, node-steps per state × role (FOLLOWER, CANDIDATE,
LEADER) over the five scenarios: INIT 4762, 10, 0; NOMINAL 24678, 904,
12467; DEGRADED 4926, 2, 4026; SAFE 6725, 0, 0. Illegal pairs 0. Emitted
state bytes INIT 15, NOMINAL 359, DEGRADED 84, SAFE 68, out of range 0;
role bytes out of range 0. DEGRADED node-steps without evidence: 0 (10 at
RED).

**TC-051 through TC-060 check counts** (executed at `ae9e408`):

| ID | Title | Property | Checks | History |
|---|---|---|---|---|
| TC-051 | mode legality guard: state × role pairs and emitted state bytes | INV-MODE-LEGAL | 6 | guard |
| TC-052 | fault-free cold boot: no DEGRADED without peer SAFE evidence | INV-MODE-NO-MAGIC, C1 | 5 | RED at 58a1b5d, 2 failing |
| TC-053 | higher-term SAFE announcement has no authority effect | INV-SAFE-ANNOUNCE-NO-AUTHORITY-EFFECT, C2 | 13 | RED at 58a1b5d, 5 failing |
| TC-054 | heartbeat state metadata is not protocol evidence | INV-MODE-METADATA-NONAUTHORITATIVE | 12 | guard |
| TC-055 | out-of-range state byte with valid CRC rejected, no effect | INV-MODE-METADATA-NONAUTHORITATIVE, REQ-FUNC-0007 | 14 | guard |
| TC-056 | stale old-term heartbeat carrying DEGRADED metadata rejected | Lot 2B semantics | 6 | guard |
| TC-057 | legitimate leader change while DEGRADED | INV-DEGRADED-PERSISTENT, INV-MODE-LEGAL | 13 | guard |
| TC-058 | partition, crash, re-election, cold restart legality | INV-MODE-LEGAL, INV-MODE-NO-MAGIC | 15 | guard |
| TC-059 | lower-term SAFE announcement: evidence only | INV-SAFE-ANNOUNCE-NO-AUTHORITY-EFFECT | 9 | guard |
| TC-060 | determinism of the TC-052 and TC-053 scenarios | reproducibility | 2 | guard |

Total LOT 4 checks: 95; suite total 455.

---

## 13. Regression evidence

The existing deterministic campaign (TC-001 through TC-050) continued to
exercise, and passed at `ae9e408`:

| Behaviour | Tests |
|---|---|
| one vote per term | TC-036, TC-050 |
| same-term vote memory (LOT 2 erratum) | TC-050 |
| stale heartbeat rejection | TC-008, TC-009, TC-056 |
| replay and duplicate rejection | TC-011, TC-026, TC-034, TC-047 |
| received ACK quorum evidence | TC-013, TC-019 |
| lease semantics | TC-007, TC-016 |
| outbound-heartbeat non-renewal of authority | TC-013, TC-019 |
| split-brain SAFE | TC-004, TC-035 |
| SAFE latch | TC-037, TC-041 |
| peer SAFE DEGRADED behaviour | TC-039, TC-045, TC-047, TC-048 |
| SAFE evidence expiry | TC-040, TC-045 |
| retry and backoff | TC-031 through TC-034 |
| crash and cold restart | TC-021 through TC-030, TC-044 |

No regression was observed in the existing deterministic campaign. This is
test evidence, not a proof.

**TC-042.** Its checks still pass. Its 2000 ms diagnostic output changed
from "valid leader node 1" to "valid leader node 3": before C2 the isolated
old leader, latched SAFE at term 4, made the majority leader step down on
reconnection and node 1 was re-elected; after C2 node 3 keeps its authority.
This is the expected consequence of the correction, and the only difference
in the TC-001 through TC-050 output between `58a1b5d` and `ae9e408`.

---

## 14. Determinism

TC-049 (LOT 3 scenario) and TC-060 (TC-052 and TC-053 scenarios, run A
against run B, comparing per-step roles, states, terms, authority,
evidence masks, transmit counts and event times) pass at both `58a1b5d` and
`ae9e408`. TC-060 compares a run with itself and therefore remained
meaningful while its scenarios were RED. Trajectory hashes: TC-052
0x5DE7D8FF at RED, 0x19E7D8FF at GREEN; TC-053 0x70EE8189 at RED,
0xEA9EE0E6 at GREEN; each reproduced within its commit. Two complete
suite runs at `ae9e408` were byte-identical, as were the strict, ASan and
UBSan outputs.

---

## 15. Sanitizer and compiler evidence

| Build | RED `58a1b5d` | GREEN `ae9e408` |
|---|---|---|
| `gcc -std=c99 -Wall -Wextra -Werror -O1` | clean, 455 checks, 7 failures | clean, 455 checks, 0 failures |
| `-fsanitize=address -fno-sanitize-recover=all` | no report, output identical | no report, output identical |
| `-fsanitize=undefined -fno-sanitize-recover=all` | no report, output identical | no report, output identical |

Compiler: gcc 13.3.0, Linux x86-64. The simulation is integer-only.

---

## 16. Traceability table

| Hypothesis / property | Test | RED commit | GREEN commit | Production change | Requirement basis | Finding |
|---|---|---|---|---|---|---|
| C1 election does not degrade | TC-052 | `58a1b5d` (2 failing checks) | `ae9e408` | `start_election()` assignment removed | none in ADD; LOT 3 evidence rule (REQ-SAFE-0004 interpretation) | ADD-F012 |
| C2 SAFE term not adopted | TC-053 | `58a1b5d` (5 failing checks) | `ae9e408` | `MOSAIK_MSG_SAFE` excluded from adoption | REQ-SAFE-0004 (peer SAFE → DEGRADED, unchanged); REQ-FUNC-0001 (one valid leader) | ADD-F013 |
| INV-MODE-LEGAL | TC-051, TC-057, TC-058 | guard, passing at `58a1b5d` | `ae9e408` | none | none (protocol invariant) | — |
| INV-MODE-METADATA-NONAUTHORITATIVE | TC-054, TC-055, TC-056 | guard | `ae9e408` | none | REQ-FUNC-0007 (TC-055, decoder) | — |
| INV-SAFE-ANNOUNCE-NO-AUTHORITY-EFFECT, lower term | TC-059 | guard | `ae9e408` | none | REQ-SAFE-0004 | ADD-F013 |
| DEGRADED persistence across leader change | TC-057 | guard | `ae9e408` | none | REQ-SAFE-0004, REQ-FUNC-0004 | ADD-F011 |
| Cold restart clears evidence | TC-058 | guard | `ae9e408` | none | none (cold-restart property, LOT 2D) | — |
| Determinism | TC-060 | guard | `ae9e408` | none | none | — |

Per-test forward and reverse traces with line references are in
`verification/TRACEABILITY.md` section 8. Requirement rows are in
`docs/ADD-MAPPING.md` section 7. Test rows are in `docs/TEST-PLAN.md`.

---

## 17. Scientific limitations

1. Host software demonstrator only; shared millisecond clock, one-step
   frame delivery, no physical bus, no clock drift.
2. Three nodes, quorum two; the results say nothing about other cluster
   sizes.
3. Each RED and GREEN value is one deterministic trajectory. The 397 ms
   interruption at RED and the 0 ms at GREEN are observations of the tested
   scenario, not bounds. The earlier characterisation of the same mechanism
   with a fixed 2000 ms isolation measured 405 ms; the difference is the
   restore instant, not the mechanism.
4. TC-051 asserts legality over the scenarios it executes. Pairs that those
   scenarios do not reach are not evidenced.
5. TC-054 and TC-055 vary payload byte 3 only over the executable values 0
   to 3 and the two out-of-range values 4 and 5.
6. C2 removes the consensus effect of SAFE frames; it does not add any
   detection of, or reaction to, a SAFE node's term. A SAFE node's term is
   simply ignored for adoption.
7. The bus is trusted by assumption (PROTOCOL.md section 2); no
   authentication exists.
8. ADAPTIVE, PGA, ground arbitration and persistence are not implemented;
   SAFE is latched within one powered node instance only.

---

## 18. Claim boundary

LOT 4 validates the stated properties only within the deterministic
three-node host demonstrator and the executed scenarios. Allowed: "In the
deterministic three-node host demonstrator, a fault-free election no
longer degrades a node, and a received SAFE announcement, including a
genuine higher-term one, has no consensus effect; the tested authority
interruption went from 397 ms to 0 ms."

LOT 4 does not establish:

- correctness for arbitrary cluster size;
- correctness under all schedules;
- correctness under all network faults;
- Byzantine fault tolerance;
- complete MOSAÏK ADD conformance;
- embedded timing guarantees;
- physical CAN or CAN-FD behaviour;
- hardware fault behaviour;
- STM32 or FreeRTOS behaviour;
- flight qualification, flight readiness or any TRL advancement;
- formal verification or exhaustive verification;
- hardware or HIL validation;
- the five-node or six-node MOSAÏK architecture.

Evidence classification for every item in this report: IMPLEMENTED-SIM.

**Relationship to MOSAÏK Advanced (non-normative).** This bench is an
experimental maturation and verification environment. Findings generated
here, including ADD-F012 and ADD-F013, may later inform a separate
ADD-driven implementation, MOSAÏK Advanced. LOT 4 results are not
themselves evidence of complete ADD implementation or conformance, and
nothing of the ADD architecture was imported into this bench for LOT 4.

---

## 19. Reproduction commands

Commit: `ae9e408a50d6f80257f77fa245247741295c0b7d` on
`lot2c-network-adversarial`.

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

Sanitizer builds and runs:

```
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=address -fno-sanitize-recover=all \
    -fno-omit-frame-pointer -Ifirmware/core \
    firmware/core/mosaik_proto.c firmware/core/mosaik_node.c \
    test/test_mosaik.c -o build/test_mosaik_asan
./build/test_mosaik_asan

gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=undefined -fno-sanitize-recover=all \
    -fno-omit-frame-pointer -Ifirmware/core \
    firmware/core/mosaik_proto.c firmware/core/mosaik_node.c \
    test/test_mosaik.c -o build/test_mosaik_ubsan
./build/test_mosaik_ubsan
```

Expected: `455 checks, 0 failures`, exit code 0, no `runtime error` and no
sanitizer report. Expected lines: TC-052 `first candidacy: node 2 at 312
ms, state INIT, SAFE evidence mask 0x00` and `no DEGRADED without evidence
observed`; TC-053 `observed authority interruption 0 ms` and `survivor
terms unchanged after the SAFE announcement`; TC-051 `DEGRADED node-steps
without peer SAFE evidence: 0`. The RED baseline reproduces from
`58a1b5d` with the same commands: `455 checks, 7 failures` in exactly
TC-052 (2) and TC-053 (5), with TC-053 printing `observed authority
interruption 397 ms`. Validated with gcc 13.3.0 on Linux. These results
are not claimed for hardware.

---

## 20. Exact commit chain

| Commit | Message | Tests / checks / failures |
|---|---|---|
| `8177e70242b507d60c3b8479dbefef67bdd016af` | docs: close LOT 3 FDIR SAFE host validation | 50 / 360 / 0 (LOT 4 start) |
| `58a1b5db708ded17dbfd0827cbe307656831a3de` | test: add LOT 4 mode semantics RED baseline | 60 / 455 / 7 (TC-052: 2, TC-053: 5) |
| `ae9e408a50d6f80257f77fa245247741295c0b7d` | fix: separate election and SAFE evidence from degradation and term adoption | 60 / 455 / 0 |
| (this commit) | docs: close LOT 4 mode semantics host validation | 60 / 455 / 0, documentation only |

The RED commit is preserved in history; the sequence is RED, finding,
correction, GREEN, documentation. Only `test/test_mosaik.c` changed at
`58a1b5d`; only `firmware/core/mosaik_node.c` changed at `ae9e408`.

---

## 21. LOT 4 closure status

LOT 4 is closed for the deterministic host demonstrator: requirements
basis, implementation, tests, evidence, regression status and limitations
are documented (ROADMAP governance rule). Status in `ROADMAP.md`:
IMPLEMENTED (host sim). ADAPTIVE and PGA remain not implemented and are
outside this bench. ADD-F012 is RESOLVED for the host demonstrator;
ADD-F013 is OPEN with the host interpretation implemented, pending
architecture review. LOT 5 has not started.
