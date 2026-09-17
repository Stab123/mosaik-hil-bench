# LOT 3 FDIR / SAFE — Evidence Report

**Document:** MOSAIK-LOT3-001
**Issue:** 0.1 — 16 September 2026
**Parent:** MOSAIK-ADD-0001 (architectural design document, TRL 3)
**Branch:** `lot2c-network-adversarial`
**Validated commit:** `7df0af01d6ae2120bce9a5c6378305e3ef7eeb5c`

**HOST SOFTWARE DEMONSTRATOR ONLY — NO HARDWARE VALIDATION. Do not claim TRL 4.**

Everything in this report was obtained from the deterministic host
demonstrator: three protocol-core instances on a virtual bus in simulated
millisecond time. Every millisecond value below is simulated, not measured.

---

## 1. Scope and baseline

LOT 3 covers, in the host demonstrator only, the executable semantics of the
protocol-core SAFE state, the DEGRADED state, and recovery of the cluster
around a faulted node. It started from the LOT 2 closure commit `76f10d9`
(34 tests, 205 checks, 0 failures) and ends at `7df0af0` (50 tests, 360
checks, 0 failures, sanitizers clean). No new protocol message, no new
timer, no new RNG draw and no harness-to-node information path was added.

Out of scope, and kept as future work: PROTO_ERROR policy, local hardware
fault input, SAFE_ASSERT, PGA / ground arbitration, ADAPTIVE mode,
reconfiguration, persistence, watchdog and discretes, hardware timing,
physical CAN-FD faults, STM32H743 / FreeRTOS, HIL and formal verification
(section 19).

---

## 2. Requirement basis

Only identifiers present in `requirements/SYSTEM-REQUIREMENTS.md` are used.

| ID | Statement (normalised) | LOT 3 use |
|---|---|---|
| REQ-SAFE-0002, REQ-SAF-001, REQ-PERF-0002 | split-brain detected and latched within 10 ms | unchanged, guard |
| REQ-SAFE-0003, REQ-SAF-002 | SAFE irreversible without ground arbitration | SAFE contract formalised and tested within one powered node instance |
| REQ-SAFE-0004, REQ-SAF-003 | cluster enters DEGRADED when a peer enters SAFE | DEGRADED contract implemented; first executable evidence |
| REQ-FUNC-0001, REQ-FUN-001 | one and only one active leader | guard in every LOT 3 test |
| REQ-FUNC-0002, REQ-FUN-002 | quorum required | no authority without quorum, guard |
| REQ-FUNC-0004, REQ-PERF-0001 | re-election under 1 s | recovery around a SAFE node, transient recovery, guard |
| REQ-FUNC-0007 | CRC on critical messages | corrupted frames detected and counted only |

The ROADMAP line for LOT 3 reads "Fault detection, isolation, recovery,
SAFE mode behavior". The transcribed requirements define DEGRADED entry but
not its exit or freshness semantics; that gap is recorded as ADD-F011.

---

## 3. Existing FDIR model before LOT 3

At `76f10d9` the protocol core had four states (INIT, NOMINAL, DEGRADED,
SAFE) and two implemented SAFE causes: SPLIT_BRAIN, when a node holding the
leader role receives a heartbeat carrying its own term, and NO_QUORUM, on the
third consecutive failed candidate vote timeout. PROTO_ERROR existed as a
cause code only. A SAFE node was already gated at the top of the receive
path and in the periodic service, so it ignored all traffic and emitted SAFE
announcements only. DEGRADED was entered on a received SAFE frame and on the
first election from INIT, but every accepted heartbeat set NOMINAL
unconditionally, so followers flapped between DEGRADED and NOMINAL every
heartbeat while a peer was SAFE (28 cycles in 3 s in the characterisation
scenario), and a leader never left DEGRADED at all. No test asserted any
DEGRADED property; the REQ-SAFE-0004 evidence citation pointed at TC-004,
which asserts SAFE entry only.

---

## 4. Specification freeze D1 to D8

| Decision | Outcome |
|---|---|
| D1 DEGRADED | per-peer SAFE evidence from received frames; freshness 3 heartbeat periods derived from `heartbeat_period_ms`; DEGRADED held while any evidence is fresh; return to NOMINAL only with legitimate local leader evidence; authority unaffected. Recorded as ADD-F011. |
| D2 PROTO_ERROR | RESERVED / NOT IMPLEMENTED. Malformed frames increment `decode_errors` only. No threshold, window, class or recovery is defined by the transcription. |
| D3 local fault input | deferred to LOT 11; no software substitute for NODE_FAULT_N. |
| D4 SAFE exit | no API. SAFE is latched for the lifetime of one powered node instance; cold `mosaik_init()` creates a new volatile instance. No frame, term, replay or connectivity change clears SAFE. |
| D5 same-term second vote | reproduced, classified as a LOT 2 erratum, corrected at `034db92` after RED evidence at `af5da87`. |
| D6 isolation budget | existing rule kept: three genuine consecutive failed elections latch SAFE / NO_QUORUM. Observed durations are host observations only. |
| D7 SAFE_ASSERT | deferred to LOT 11. |
| D8 REQ-SAFE-0004 evidence | TC-004 citation marked insufficient; TC-039 and TC-040 are the first executable evidence. |

---

## 5. Fault taxonomy

| Fault | Local detection | Isolation | Recovery | SAFE | LOT 3 evidence |
|---|---|---|---|---|---|
| leader or quorum loss | lease non-renewal, follower deadline | authority revoked | re-election with quorum | only if quorum unreachable | TC-035, TC-042, TC-046 |
| peer silence, crash | missing heartbeat or ACK | none needed | election | no | TC-044, TC-046 |
| stale or replayed heartbeat, ACK, VOTE_REQ, VOTE_GRANT | term and sequence checks | rejected | not applicable | no | TC-037, TC-047 |
| asymmetric, delayed, reordered loss | lease and ACK evidence | authority revoked | when contact returns | if minority exhausts | TC-042, TC-047 |
| cold restart | not detected by peers | not applicable | higher-term adoption | no | TC-040 |
| election exhaustion | timer | self, by SAFE | none, cold restart only | NO_QUORUM | TC-041, TC-044 |
| contradictory leadership | same-term heartbeat | self, by SAFE | none | SPLIT_BRAIN | TC-035 to TC-040 |
| malformed frame | decoder | none | not applicable | never (reserved) | TC-043 |
| peer SAFE observation | received SAFE frame | none | evidence expiry | never propagated | TC-038, TC-039, TC-045 |
| same-term vote after demotion | none before LOT 3 | not applicable | corrected | no | TC-050 |
| local internal fault, watchdog | not defined | | | | deferred |

---

## 6. SAFE contract

Demonstrated in the host model (TC-035 to TC-038, TC-041, TC-044):

- SAFE is latched for the lifetime of one powered node instance.
- A SAFE node holds the follower role and never has valid leadership
  authority (INV-SAFE-NO-AUTHORITY).
- It never becomes candidate or leader, grants no vote, sends no ACK, and
  transmits SAFE announcements only, one per heartbeat period.
- It ignores all protocol traffic before any term processing, so old-,
  same- and higher-term frames, grants addressed to it, SAFE replays and
  restored connectivity cannot clear SAFE (INV-SAFE-LATCH).
- Cold `mosaik_init()` creates a new volatile instance and therefore clears
  SAFE, because persistent SAFE state is not implemented. SAFE persistence
  across a power cycle is NOT claimed.
- SAFE is not propagated: a received SAFE frame never moves a peer into SAFE.
- PGA / ground arbitration is NOT implemented (ADD-F009).

Code: `enter_safe()` at `mosaik_node.c:71`; receive gate at
line 245; periodic-service gate in `mosaik_tick()`.

---

## 7. DEGRADED contract

DEGRADED is an operational cluster-awareness state, not authority
revocation. A leader may legitimately remain DEGRADED while holding valid
leadership authority (TC-039). Quorum and voting capability are unchanged.

- Entry: an accepted SAFE frame from a peer moves NOMINAL to DEGRADED;
  DEGRADED stays DEGRADED; INIT stays INIT.
- Persistence: an accepted heartbeat and `become_leader()` assign DEGRADED
  while any peer SAFE evidence is fresh, NOMINAL otherwise. No
  heartbeat-induced flapping (TC-039, TC-047, TC-048).
- Exit: in `mosaik_tick()`, a DEGRADED node returns to NOMINAL only when no
  peer evidence is fresh and legitimate leader evidence exists locally
  (section 10). Candidates and followers in retry backoff never clear
  DEGRADED there.

Code: SAFE handler at `mosaik_node.c:368`; heartbeat state
decision at line 292; `become_leader()` at line 168; exit
block at line 400.

---

## 8. Per-peer SAFE evidence model

`mosaik_node_t` holds `last_safe_rx_ms[4]` and `safe_evidence_mask`. Bit
`src−1` is set once at least one SAFE frame has actually been received from
that peer; the array holds the local receive time of the latest one. Before
indexing, the source id must be non-zero, at most `MOSAIK_MAX_NODES`, and
not the node's own id; the decoder already enforces the range and own frames
are discarded earlier. Only frames actually received count: a dropped SAFE
frame is absence of local evidence, and no remote SAFE state is inferred
from topology or harness knowledge (TC-045). Both fields are zeroed by
`mosaik_init()`.

---

## 9. Freshness semantics

A peer's evidence is fresh when its mask bit is set and
`(uint32_t)(now_ms − last_safe_rx_ms[peer]) < 3u * (uint32_t)cfg.heartbeat_period_ms`.
The window is derived from the heartbeat period, not a separate
configuration parameter, because SAFE nodes re-announce every heartbeat
period; with the default 100 ms period it is 300 ms. The unsigned
subtraction is wrap-safe for any elapsed time below 2^32 ms. Expired bits
are left set because freshness is time-derived. The helper
`peer_safe_evidence_fresh()` at `mosaik_node.c:128` uses only local
state, the local clock and the configured heartbeat period.

---

## 10. Recovery semantics

System recovery and SAFE-node recovery are distinct.

- The cluster recovers around a SAFE node: with three nodes, one SAFE node
  leaves quorum reachable, and the survivors elected a valid leader 452 ms
  after the split-brain latch in TC-035.
- The SAFE node itself never recovers without a cold restart (TC-035,
  TC-037, TC-040, TC-041).
- Leader evidence for the DEGRADED exit is: for a leader,
  `mosaik_has_valid_leadership_authority()`; for a follower, a non-zero
  `leader_id` and a heartbeat deadline still in the future under the
  existing signed-difference convention.
- Transient faults recover automatically when the existing timers permit:
  in TC-042, 600 ms and 900 ms leader isolations recovered and satisfied the
  recovered predicate (one valid authority, every running non-SAFE node
  following it inside its lease window, no candidate); 1200 ms and longer
  latched SAFE. In TC-046 three 800 ms isolations recovered without any
  failed election. These are observations under the current deterministic
  configuration, not worst-case guarantees.
- Restored connectivity after SAFE never recovers the node (TC-041).

---

## 11. Safety invariants

| Invariant | Meaning | Evidence at `7df0af0` |
|---|---|---|
| INV-LEADER-UNIQUE | max concurrent valid authorities ≤ 1 | asserted in every LOT 3 test; observed 1 |
| INV-SAFE-NO-AUTHORITY | SAFE node never holds valid authority | 0 violations over all 50 tests |
| INV-SAFE-LATCH | no implicit SAFE exit | 0 exits without cold restart |
| INV-NO-STALE-RECOVERY | old-term traffic cannot restore authority or clear a fault | TC-037, TC-047 |
| INV-TERM-MONOTONIC | term never decreases | 0 regressions, cold restart excluded by design |
| INV-FDIR-NO-MAGIC | decisions from received frames and local state only | code review of the four LOT 3 sites; TC-045 |
| INV-DEGRADED-PERSISTENT | DEGRADED held while peer SAFE evidence is fresh | TC-039, TC-047, TC-048 |
| INV-ONE-VOTE-PER-TERM | vote memory never erased by a role change within the same term | TC-050 |

---

## 12. Liveness properties

Faults shorter than the existing timer budget recover automatically; leader
crash with a surviving quorum recovers; the cluster recovers around a SAFE
node; a SAFE node never recovers without cold restart; DEGRADED never blocks
election, voting or authority. Three genuine consecutive failed elections
latch SAFE / NO_QUORUM by existing rule; no new timing requirement is
introduced.

---

## 13. TC-035 through TC-050

Executed check counts at `7df0af0`; all pass. "RED at af5da87" marks the
tests that failed before the corrections, as required evidence.

| ID | Title | Property | Checks | History |
|---|---|---|---|---|
| TC-035 | SAFE node: no authority, SAFE-only transmission, cluster recovers around it | SAFE contract, system recovery | 9 | guard |
| TC-036 | SAFE node grants no vote, sends no ACK, never becomes candidate | SAFE non-participation | 8 | guard |
| TC-037 | SAFE latch against old/same/higher-term traffic, grants, replays, connectivity | INV-SAFE-LATCH | 7 | guard |
| TC-038 | SAFE frames do not propagate SAFE | no propagation | 6 | guard |
| TC-039 | DEGRADED persists while peer SAFE evidence is fresh, no flapping | REQ-SAFE-0004 | 12 | RED at af5da87 |
| TC-040 | evidence expiry, return to NOMINAL, SAFE node recovers only by cold restart | REQ-SAFE-0004 | 10 | RED at af5da87 |
| TC-041 | election exhaustion terminal, restore does not recover | INV-SAFE-LATCH, REQ-FUNC-0002 | 14 | guard |
| TC-042 | leader isolation boundary 600 to 2000 ms with recovered predicate | REQ-FUNC-0004, D6 | 18 | guard |
| TC-043 | malformed frames detection-only | REQ-FUNC-0007, D2 | 9 | guard |
| TC-044 | survivor crash during collision recovery | REQ-FUNC-0002 | 8 | guard |
| TC-045 | replayed SAFE evidence bounded, local evidence only | REQ-SAFE-0004 | 8 | RED at af5da87 |
| TC-046 | three transient isolations recover without SAFE | REQ-FUNC-0004 | 15 | guard |
| TC-047 | leader SAFE + stale heartbeat replay + one-way drop during election | REQ-SAFE-0004, REQ-FUNC-0001 | 8 | RED at af5da87 |
| TC-048 | leader crash + natural collision + delayed SAFE frame during backoff | REQ-SAFE-0004, REQ-FUNC-0004 | 13 | RED at af5da87 |
| TC-049 | deterministic reproducibility | reproducibility | 1 | guard |
| TC-050 | one vote per term across a same-term step-down | LOT 2 erratum | 9 | RED at af5da87, green at 034db92 |

Total LOT 3 checks: 155; suite total 360. No test writes a protocol
internal. Fault injection uses the directional network model, crash and cold
restart, frame injection, delayed delivery, and TC-004 style targeted
delivery of one adversarial frame. Two observation-only helpers were added
to the harness: per-node transmit counters inside `bus_tx()` and a per-step
trajectory observer. Neither is readable by a node.

---

## 14. RED baseline evidence, `af5da87`

Commit `af5da87`, "test: capture LOT 3 FDIR SAFE and same-term vote
failures", test file only. Result: 50 tests, 360 checks, 13 failed check
lines, failed test set exactly TC-039, TC-040, TC-045, TC-047, TC-048,
TC-050. TC-001 to TC-034 output byte-identical to `76f10d9`.

Observed defects: followers flapped 26 times in 3 s while a peer was SAFE
(TC-039); a leader stayed DEGRADED after the SAFE peer was gone (TC-040);
a replayed SAFE frame degraded the receiver for 15 ms instead of a full
window (TC-045); DEGRADED runs of 16 to 17 ms across leadership acquisition
(TC-048); and a former leader granted a same-term vote after its lease-expiry
demotion (TC-050).

---

## 15. LOT 2 erratum D5 discovery

LOT 3 adversarial testing exposed a same-term vote-memory defect after
lease-expiry demotion. `become_follower()` erased `voted_for` while
preserving `voted_term`; the lease-expiry path called it with the node's own
term, so a same-term demotion erased the vote for that term and the one-vote
check, keyed on `voted_term == msg.term && voted_for != 0`, admitted a second
vote. Reproduced through legitimate events only: ACK paths dropped by the
network model, lease expiry at 2417 ms, vote memory (2,1) before and (0,1)
after the demotion, a same-term VOTE_REQ delivered through the bus, and a
VOTE_GRANT emitted. Concurrent double authority was NOT observed and is not
reachable with the current 500 ms lease and 150 ms vote timeout; the
documented one-vote-per-term rule was nevertheless violated in state.
Historical LOT 2 evidence is not rewritten: LOT 2 did not test this case.

---

## 16. Phase 2a correction, `034db92`

"fix: preserve vote memory across same-term demotion". The unconditional
`voted_for = 0` in `become_follower()` was removed and the header comment
updated. Eligibility to vote in a strictly higher term follows from
`voted_term` differing, so higher-term voting remains allowed (TC-050
contrast phase). Result: TC-050 green; remaining RED exactly TC-039,
TC-040, TC-045, TC-047, TC-048; TC-001 to TC-034 and TC-035 to TC-049
output byte-identical to `af5da87`; sanitizers clean; no DEGRADED behaviour
changed.

---

## 17. Phase 2b correction, `7df0af0`

"fix: persist DEGRADED state from fresh peer SAFE evidence". Two new fields,
the freshness helper, the SAFE handler evidence record, the heartbeat and
`become_leader()` state decisions, the DEGRADED exit in `mosaik_tick()`, and
initialisation. 64 insertions, 3 deletions, in the two core files only.
`rng_next()` call sites unchanged at 4. Result: 360 checks, 0 failures;
TC-001 to TC-034 byte-identical to `034db92`; DEGRADED runs of exactly
300 ms in TC-045 and TC-048; 0 flapping transitions in TC-039 and TC-047.

---

## 18. Final validation

| Check | Result at `7df0af0` |
|---|---|
| Strict build, `-std=c99 -Wall -Wextra -Werror -O1` | clean |
| Suite | 50 tests, 360 checks, 0 failures |
| AddressSanitizer + UndefinedBehaviorSanitizer | 0 findings |
| Max concurrent valid authorities | 1 |
| Term regressions | 0 |
| SAFE node with valid authority, with leader role, role change after entry, non-SAFE transmission, exit without restart | 0 each, over all 50 tests |
| PROTO_ERROR cause ever set | 0 |
| DEGRADED to NOMINAL transitions while a peer is still SAFE | 4, all after that peer's SAFE frames stopped reaching the node (crash or isolation) |

---

## 19. Deferred functionality

NOT IMPLEMENTED, kept at their existing roadmap assignments: PROTO_ERROR
policy (no normative semantics); local hardware fault input and
NODE_FAULT_N (LOT 11); SAFE_ASSERT physical and software meaning (LOT 11);
PGA / ground arbitration (LOT 8, ADD-F009); ADAPTIVE mode (LOT 4);
reconfiguration (LOT 5); persistent term, vote or SAFE storage (no lot
assigned); watchdog and discretes (LOT 11); hardware timing (LOT 14);
physical CAN-FD faults (LOT 6, 12, 14); STM32H743 / FreeRTOS validation
(LOT 11); HIL (LOT 12); formal verification (LOT 10).

---

## 20. Limitations

1. Host software demonstrator only; shared millisecond clock, one-step frame
   delivery, no physical bus, no clock drift.
2. SAFE is irreversible within one powered node instance only; a reboot
   clears it because persistence is not implemented.
3. The split-brain check trusts the frame term and does not validate the
   role byte; the bus is trusted by assumption (PROTOCOL.md section 2).
4. A SAFE node whose announcements are dropped by a partition is invisible
   to peers; their DEGRADED evidence expires. This is the intended
   locally-received-evidence rule, not a detection of the remote state.
5. A NO_QUORUM SAFE node carries a term above the cluster's; when its SAFE
   frames reach the cluster again, the existing higher-term adoption makes
   the leader step down once before a new election. Safety holds; this is a
   LOT 2 behaviour observed during characterisation and not changed in
   LOT 3. (Changed in LOT 4, commit `ae9e408`: the SAFE announcement term
   is no longer adopted; see `LOT4_MODE_SEMANTICS_REPORT.md`, finding
   L4-C2.)
6. Isolation-to-SAFE durations are host observations, not bounds.
7. Three nodes, quorum two; a single loss is the only tolerable fault.
8. No cryptographic anti-replay; stale rejection is semantic (Lot 2B).

---

## 21. Reproducibility

Commit: `7df0af01d6ae2120bce9a5c6378305e3ef7eeb5c` on
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

Sanitizer build and run:

```
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    -fno-omit-frame-pointer -Ifirmware/core \
    firmware/core/mosaik_proto.c firmware/core/mosaik_node.c \
    test/test_mosaik.c -o build/test_mosaik_san
./build/test_mosaik_san
```

Expected: `360 checks, 0 failures`, exit code 0, no `runtime error`,
AddressSanitizer or LeakSanitizer report. Expected lines: TC-039
`survivors DEGRADED->NOMINAL transitions: node1=0 node3=0`; TC-045
`receiver node 3 DEGRADED run 300 ms`; TC-048 `DEGRADED runs 300/300 ms`;
TC-050 `vote (2,1) before step-down, (2,1) after; same-term grants 0;
higher-term grants 1`. The RED baseline reproduces from `af5da87` with the
same commands: `360 checks, 13 failures` in exactly TC-039, TC-040, TC-045,
TC-047, TC-048, TC-050. Validated with gcc 13.3.0 on Linux; the simulation
is integer-only and deterministic. These results are not claimed for
hardware.

---

## 22. Scientific claim boundary

Allowed: "LOT 3 FDIR/SAFE behaviour is implemented and validated in the
deterministic host demonstrator." Demonstrated properties: the SAFE
contract; no SAFE authority; SAFE latch within one powered node instance;
DEGRADED persistence from fresh, locally received peer SAFE evidence;
bounded evidence expiry; cluster recovery around a SAFE node; transient
recovery under the tested deterministic conditions; the one-vote-per-term
erratum closed; INV-LEADER-UNIQUE preserved.

NOT claimed: hardware FDIR; physical CAN-FD fault handling; HIL; STM32 or
FreeRTOS behaviour; TRL 4; flight readiness; formal proof; probabilistic
reliability; radiation tolerance; persistent SAFE across a power cycle;
Byzantine tolerance; ground arbitration; actuator or sensor safing.

Evidence classification for every item in this report: IMPLEMENTED-SIM.
