# LOT 5 — Autonomous Membership Reconfiguration, Experimental Validation Report

**Document:** MOSAIK-LOT5-001
**Issue:** 0.1 — 20 September 2026
**Parent:** MOSAIK-ADD-0001 (architectural design document, TRL 3)
**Branch:** `lot2c-network-adversarial`
**Phase 1 RED commit:** `02b27faa55d0e437337cb3d513a1d9683797416a`
**Phase 2 GREEN commit:** `146472f888b3f784fe0de25bd5c8e5cc39a6d5c3`
**Phase 3 adversarial commit:** `9ccd28e867d74cb9667addb5676ad009fa849a07`

**HOST SOFTWARE DEMONSTRATOR ONLY — NO HARDWARE VALIDATION. Do not claim TRL 4.**

Everything in this report was obtained from the deterministic host
demonstrator: three protocol-core instances on a virtual bus in simulated
millisecond time. Every millisecond value below is simulated, not measured.
This repository is an experimental deterministic distributed-protocol and
verification bench; it is not the complete MOSAÏK ADD implementation.

---

## 1. Scope

LOT 5 covers, in the host demonstrator only, autonomous reconfiguration:
quorum reconfiguration and membership changes. It asks whether membership
can be changed at run time without breaking the authority, FDIR and mode
semantics validated in LOT 2, LOT 3 and LOT 4.

It started from the LOT 4 closure commit `7d35cc8` (60 tests, 455 checks,
0 failures) and ends at `9ccd28e` (90 tests, 761 checks, 0 failures,
sanitizers clean).

Out of scope, and kept as future work: cluster sizes other than three,
Byzantine tolerance, cryptographic membership authentication, a
prepare-and-adopt recovery phase for a deadlocked configuration epoch,
persistence in physical memory, and everything hardware (section 31).

---

## 2. Starting baseline

At `7d35cc8`, membership was not a protocol concept at all. The audit of
Phase 1 established the following from the code, not from the documents.

| Property | State before LOT 5 |
|---|---|
| Membership representation | the integer `cfg.cluster_size`, copied once by `mosaik_init()` |
| Mutable through the protocol | no |
| Quorum | `cluster_size / 2 + 1`, evaluated only when a VOTE_GRANT is received |
| Configuration epoch | none; `term` is a leadership epoch only |
| Reconfiguration message | none; five message types on fifteen identifiers, every payload byte consumed |
| Commit mechanism | none |
| Memory of a removed node | none |
| Reconfiguration replay protection | none |

A node could therefore not distinguish "peer temporarily unreachable" from
"peer removed from membership", because it had no notion of membership to
remove a peer from.

---

## 3. Scientific question

Can a safe autonomous membership and quorum reconfiguration be introduced
into this protocol without violating the validated invariants, and in
particular without ever allowing two nodes to hold valid leadership
authority under incompatible configurations?

The frozen safety principle is that **membership is not reachability**.
Loss of communication is not proof that a node has ceased to be a member.
A minority that cannot reach its peers must not be allowed to shrink its
own membership in order to regain a quorum.

---

## 4. Initial fixed-membership architecture

The pre-LOT-5 design was safe partly *because* membership was rigid. With
`cluster_size` fixed at three on every node, quorum was two everywhere, and
the quorum-intersection argument that underpins INV-LEADER-UNIQUE held by
construction. The absence of a reconfiguration capability was therefore not
an oversight to be corrected quietly: it was part of why the existing
safety argument worked, and replacing it required re-establishing that
argument rather than assuming it.

---

## 5. Phase 1 RED methodology

Phase 1 modified only `test/test_mosaik.c`. It characterised the baseline
and expressed the LOT 5 properties as executable observations, so that the
absence of the capability would be visible as genuine test failures rather
than as a compile error or a placeholder.

Rules honoured: no node field written from a test; faults only through the
directional network model, crash and cold restart, frame injection and
delayed delivery; harness knowledge used for assertions only. Two tests
deliberately cold-restarted one node with a different deployed
`cluster_size` through the public initialisation interface, an out-of-band
operator action, in order to measure whether the protocol could detect or
contain an inconsistent configuration. That was labelled as observation of
the product's response, not as a protocol operation.

---

## 6. Phase 1 RED results, `02b27fa`

| Item | Value |
|---|---|
| Tests | 70 |
| Checks | 515 |
| Failed checks | 11 |
| Failing tests | TC-064 (2), TC-065 (5), TC-066 (2), TC-067 (1), TC-068 (1) |
| TC-001..TC-060 | all pass, output byte-identical to `7d35cc8` |
| Strict C99, ASan, UBSan | clean |

What each RED test established:

- **TC-064** no identifier existed for a membership transaction, and after
  5000 ms without one node the survivors still counted it as a voter.
- **TC-065** a participant cold-restarted with a different deployed
  configuration was neither detected nor excluded: its acknowledgement was
  lease evidence and its grant decided the next election. With a larger
  configured size the two live nodes lost all authority.
- **TC-066** a node out of service for 5000 ms rejoined by cold restart as
  a full voter and won the next election.
- **TC-067** heartbeats emitted under two different configured sizes were
  byte-identical: no configuration identity existed on the wire.
- **TC-068** a follower cut off for 600 ms returned with a higher term and
  took authority from the majority that was meant to continue.

Safety invariants held in every RED scenario. The RED evidence is absence
of capability and undetectable inconsistency, not a safety violation of the
then-current design. This history is preserved deliberately: the Phase 2
mechanism did not exist from the beginning, and this report does not
present it as though it had.

---

## 7. The counterexample that invalidated old-majority-only reconfiguration

During the Phase 2 design the obvious rule — authorise the new
configuration with a majority of the old one — was examined and **rejected**
on the following counterexample.

Configuration C(n) = {1,2,3}, leader node 1 at some term, renewing its
lease from node 3's acknowledgements because its link to node 2 is
degraded in the direction that carries acknowledgements.

1. An operator requests C(n+1) = {1,2}, removing node 3.
2. Under old-majority-only, acceptances from nodes 1 and 3 form a majority
   of C(n), so node 1 installs C(n+1) and with it the smaller quorum.
3. The COMMIT is lost. There is no reliable broadcast.
4. Node 2 never promised anything and remains on C(n).
5. If lease evidence is not membership-aware, node 1 keeps renewing from
   node 3, the very node C(n+1) removed.
6. Node 2, still on C(n) with quorum two, times out and elects itself with
   node 3's vote at a higher term.
7. Node 1 is valid under C(n+1) and node 2 is valid under C(n) at the same
   instant, under incompatible committed configurations.

Two separate corrections are each necessary to close this, and neither is
sufficient alone:

- **(a)** authority evidence must be evaluated against the holder's own
  committed membership, so a removed node's acknowledgement cannot renew
  the lease. This kills step 5.
- **(b)** an acceptor must be bound by a joint rule so it cannot complete a
  leader that satisfies only one of the two configurations. This kills
  step 6 when the acceptor is the node whose vote is needed.

The adopted commit rule therefore requires a quorum of the **old** and a
quorum of the **new** configuration among acceptances actually received.
Requiring both makes every node the new configuration needs an acceptor
before the smaller quorum can exist, which removes the step that created
the second authority rather than detecting it afterwards.

This is a counterexample analysis on a three-node model. **It is not a
formal proof.**

Supporting enumeration: over all admissible three-node masks, quorum
intersection was checked exhaustively and no two quorums of any two
admissible configurations are disjoint. That property is specific to three
nodes with a minimum membership of two; the joint rule was adopted so the
mechanism does not depend on it.

---

## 8. Design decisions

| Decision | Outcome |
|---|---|
| Membership representation | explicit committed voter mask, not `cluster_size` |
| Configuration identity | `(configuration epoch, membership mask)` |
| Epoch versus term | strictly independent; neither is ever read as the other |
| Transaction | PROPOSE, ACCEPT, COMMIT, with a periodic ANNOUNCE for repair |
| Commit authorisation | quorum of the old **and** of the new configuration |
| Participation versus candidacy | different sets; see section 15 |
| Removed node | passive, not forced into SAFE |
| Persistence | host-model configuration store, committed configuration and acceptance binding only |
| Minimum membership | two; a one-node membership is refused everywhere |
| Wire format | a new frame type on its own identifiers; no existing field changes meaning |

---

## 9. Membership representation

Membership is a three-bit voter mask, bit `n-1` for node `n`. The masks a
node may commit to are the non-empty subsets of the three nodes with at
least two members: `{1,2}`, `{1,3}`, `{2,3}` and `{1,2,3}`. A single-node
membership is refused by the decoder path, by the handler and by the public
request interface, because it would reduce quorum to one and let an
isolated node appoint itself.

`cfg.cluster_size` is retained as a legacy boot parameter and decides
nothing. TC-061 demonstrates this directly: booting every node with
`cluster_size` 1, which previously meant quorum one, leaves the committed
membership at `{1,2,3}` and still requires two counted votes.

Quorum is taken over a mask:

```
quorum(mask) = popcount(mask) / 2 + 1
```

---

## 10. Configuration epoch

A `uint16_t` membership epoch, initial value 1, advancing by exactly one
per committed transaction. It is compared explicitly and never inferred
from the leadership term. Epochs do not wrap: the last representable epoch
refuses a successor rather than rolling round to zero (TC-073).

---

## 11. The PROPOSE / ACCEPT / COMMIT / ANNOUNCE transaction

| Stage | Direction | Meaning |
|---|---|---|
| PROPOSE | proposer to members | this `(epoch+1, mask)` is proposed |
| ACCEPT | member to proposer | acceptance bound to that exact pair, naming the proposer |
| COMMIT | proposer to members | the pair is committed |
| ANNOUNCE | any node, periodically | this is my committed `(epoch, mask)` |

The external command `mosaik_request_reconfiguration()` starts the
transaction. It is accepted only from a node that is a committed member,
holds valid leadership authority, has no transaction pending, and names a
valid mask that contains itself, differs from the committed mask and does
not conflict with an acceptance it has already given. **It commits nothing
by itself.**

A PROPOSE is honoured only from a node in the receiver's committed
membership. A receiver that is itself a member additionally requires the
proposer to be the leader it currently follows; a receiver that is not a
member, one this configuration removed or one being re-admitted, has no
meaningful leader and accepts from any member.

An ACCEPT counts when it comes from a node of **either** configuration of
the transaction. A node the proposal adds is not yet in the committed
membership, yet its acceptance is exactly what the new quorum needs; it can
never contribute to the old quorum because the commit test masks the
acceptance set by each membership separately. This was a defect found by
TC-076 during Phase 2 and corrected before GREEN: without it, no transition
that adds a member could ever reach its new quorum.

---

## 12. Old quorum

A quorum of the committed configuration C(n), taken over the acceptances
actually received. Nothing is inferred from delivery success, topology or
peer liveness.

---

## 13. New quorum

A quorum of the proposed configuration C(n+1), taken over the same set of
received acceptances.

---

## 14. Joint quorum rule

A successor is committed only when

```
|accepts ∩ C(n)|   >= quorum(C(n))
AND
|accepts ∩ C(n+1)| >= quorum(C(n+1))
```

The same conjunction governs elections and the leadership lease while an
acceptance is pending. For `{1,2,3}` to `{1,2}` this means acceptances from
nodes 1 and 2; a majority of the old configuration alone, for instance
nodes 1 and 3, is explicitly not enough.

---

## 15. Participation versus candidacy

These are deliberately different sets, and conflating them was a second
defect found and corrected during Phase 2.

- **Participation** — voting, acknowledging, being counted — uses the
  **union** C(n) ∪ C(n+1) while an acceptance is pending. A node needed by
  either quorum must be able to take part. The intersection would be wrong:
  for `{1,2}` to `{1,3}` it is the single node 1, which would silence the
  very nodes both quorums need.
- **Candidacy** — standing for election — requires membership of **every**
  configuration that might currently be in force: the committed membership
  and, while pending, the accepted successor.

The candidacy restriction was added because a node the successor removes
would otherwise spend its whole election budget on attempts that the
pending commit would immediately undo, and latch SAFE for want of a quorum
it was never entitled to assemble. Safety comes from the quorum test, not
from either set.

---

## 16. Election semantics during a transition

A node grants a vote only within the participation set, and only within the
one-vote-per-term rule inherited from LOT 2. A vote from outside the
participation set is never counted. A candidate wins only on the joint
quorum of section 14. A node that commits a configuration excluding it
drops any candidacy immediately, without its term or vote memory being
touched.

---

## 17. Lease and authority semantics during a transition

`mosaik_has_quorum_ack_evidence()` reports valid evidence when
acknowledgements actually received, in the leader's current term and
younger than one heartbeat period, form a quorum of the committed
membership and, while pending, of the accepted successor, the leader itself
included. Outbound traffic never counts.

The pre-LOT-5 harness rule "one acknowledgement from any peer is a
majority" is gone; the caller now asks the node's own membership-aware
predicate. A node that commits a configuration excluding it loses valid
authority in the same step, never leaving authority behind in a
configuration that excludes its holder.

The freshness boundary was measured on the production predicate against the
clock the predicate itself uses: evidence counts at age 99 ms and stops at
exactly 100 ms, the heartbeat period (TC-087).

---

## 18. Removed-node semantics

After a committed removal, the node is **passive**: it neither stands for
election nor retries one, and its heartbeats, acknowledgements, vote
requests, vote grants and any higher or stale leadership term are all
refused by the remaining members as coming from outside the committed
membership.

It is **not** forced into SAFE. Exclusion from the membership and FDIR are
different concepts, and conflating them would misreport a healthy node as
faulted. It keeps its timers, term and vote memory and stands again once a
configuration that includes it is in force.

Its SAFE announcements remain fault evidence and are still recorded by
peers. This preserves the LOT 4 discipline that FDIR evidence and consensus
evidence must not be conflated: membership gates consensus, not fault
reporting.

---

## 19. Re-admission semantics

A re-admitted node becomes a member again, but nothing it left behind
becomes fresh evidence. TC-084 isolates the act of re-admission to a single
step and shows that it refreshes no acknowledgement evidence, changes no
vote memory and changes no heartbeat replay state. On re-admission the node
rearms its election deadline locally, so it waits for the cluster's
heartbeat instead of opening an election at a term it knows to be stale.

---

## 20. Replay, idempotence and conflict handling

| Case | Behaviour |
|---|---|
| Epoch below the committed one | refused as superseded |
| Epoch above `committed + 1` | refused; no transition context |
| Same epoch, same mask | idempotent duplicate |
| Same epoch, different mask | recorded as an observed inconsistency, refused |
| Second, different successor for a bound epoch | refused as a conflict |
| Duplicate ACCEPT at the proposer | counted once |

One binding per epoch, persisted. Two different successors of one epoch can
never both be agreed: each needs a quorum of the old configuration, any two
such quorums intersect, and the intersecting node binds only one successor.

---

## 21. Host-model persistence

**HOST-MODEL CONFIGURATION STORE ONLY.**

Each node owns a store holding exactly what that node itself wrote before
crashing: its committed `(epoch, mask)` and its single acceptance binding
for the next epoch. It is handed back to the node at cold restart. No other
node can read it, and nothing in it is derived from topology.

An empty, malformed or single-node store is reset to the initial
configuration rather than being trusted (TC-077).

This is **not**: physical non-volatile memory validation; STM32 or FRAM
persistence; a power-loss atomicity argument; flight persistence. **Term,
vote and SAFE state are still not persisted**, and a cold restart still
clears them, exactly as in LOT 2D and LOT 3. Only the configuration store
described here survives a restart.

---

## 22. Partial-COMMIT repair

There is no reliable broadcast. A COMMIT may reach nobody, one participant
or all. Every node announces its committed configuration every five
heartbeat periods, and a member's announcement for `epoch + 1` is
admissible, so a lost COMMIT is repaired by the next announcement from a
node that has it. A node removed by a configuration cannot propagate that
configuration, because an announcement must name a mask containing its
sender; repair therefore comes from the remaining members, which is where
it is available.

---

## 23. Phase 2 implementation, `146472f`

One commit, production changes confined to the protocol core and its
header, plus the codec for the new frame type.

| File | Change |
|---|---|
| `mosaik_proto.h` | `MOSAIK_MSG_CONFIG`, `MOSAIK_ID_CONFIG_BASE`, the four stages, two message fields |
| `mosaik_proto.c` | encode and decode of the CONFIG frame with its own validation |
| `mosaik_node.h` | configuration store type, node configuration state, five public functions |
| `mosaik_node.c` | quorum over masks, joint rules, the transaction handler, the membership gates, proposer maintenance and the periodic announcement |

Three defects were found by the Phase 2 tests and fixed before GREEN: the
ACCEPT source rule (section 11), the candidacy rule (section 15), and a
node committed out of its membership retaining the leader role
(section 17).

---

## 24. Phase 2 results, `146472f`

| Item | Value |
|---|---|
| Tests | 78 |
| Checks | 667 |
| Failures | 0 |
| TC-001..TC-060 | byte-identical to the RED baseline |
| Strict C99, ASan, UBSan | clean |
| Two complete runs | byte-identical |

TC-061 through TC-070 were rewritten from Phase-1 "capability absent"
observations into executable properties; TC-071 through TC-078 were added
for conflicting successors, idempotence, malformed memberships, proposer
failure including partial commit, removed-node traffic, every permitted
transition, the configuration store, and interaction with SAFE, DEGRADED,
elections and lease expiry.

---

## 25. Phase 3 methodology

Phase 3 attacked the protocol committed at `146472f`. **Phase 3 modified
`test/test_mosaik.c` only.** The firmware is byte-identical between
`146472f` and `9ccd28e`, and TC-001 through TC-078 are unchanged in text
and in output.

The scientific priority was counterexample over green: production code was
frozen during discovery, and any genuine safety failure would have been
preserved as a RED checkpoint rather than fixed.

---

## 26. Continuous invariant oracle

Every Phase-3 scenario runs under an oracle evaluated at **each simulated
millisecond**, not only at the end. It is harness-omniscient by
construction and used for assertions only: no node reads it, and no
topology, drop decision or peer liveness is turned into protocol evidence.

It watches INV-LEADER-UNIQUE, INV-SAFE-NO-AUTHORITY, INV-SAFE-LATCH,
INV-NO-STALE-RECOVERY, INV-TERM-MONOTONIC, INV-ONE-VOTE-PER-TERM and the
LOT 5 invariants. Its critical clauses are: no two nodes may hold valid
leadership authority at the same instant; none may hold it while outside
its own committed membership; and no two running nodes may hold different
memberships at the same configuration epoch. It also records, as an
observation rather than a violation, any instant at which a node holds
authority under a configuration that a strictly newer committed one
excludes it from.

---

## 27. Adversarial campaign

Committed, reproducible from `9ccd28e`: twelve tests, TC-079 through
TC-090, 1091 added test lines, **6236 adversarial schedules executed
in-suite**.

| Category | Schedules | Test |
|---|---|---|
| Partial COMMIT delivery: 2 removals x 8 PROPOSE subsets x 8 COMMIT subsets x 4 partitions x 3 crash points | 1536 | TC-079 |
| Bounded deterministic schedule explorer: the above crossed with 3 heal delays | 4608 | TC-090 |
| Proposer crash: 10 transaction points x 4 restart conditions | 40 | TC-081 |
| Acceptor crash around its acceptance | 6 | TC-082 |
| Stuck / missed-epoch cases | 10 | TC-083 |
| CONFIG loss, duplication, delay, reordering, and COMMIT before its own PROPOSE | targeted | TC-080 |
| Removed-node attacks: 5 configuration and 5 consensus frames | targeted | TC-083 |
| Re-admission isolated to a single step, plus restarts either side | targeted | TC-084 |
| Conflicting successors of one epoch | targeted | TC-085 |
| Term and configuration-epoch cross product | targeted | TC-086 |
| Lease evidence attacks and the exact freshness boundary | targeted | TC-087 |
| SAFE and DEGRADED at four transaction stages | targeted | TC-088 |
| One-millisecond timer boundaries | 6 | TC-089 |

Symmetry reduction used by the matrices: the boot leader is deterministic,
so the two peers are enumerated explicitly as the removal target rather
than assumed interchangeable; they are not symmetric, having different
identifiers and different random seeds. Partitions are enumerated as the
four distinct 1|2 topologies including none.

Exploratory development work outside the repository is **not** included in
this count. Roughly 4700 additional schedules were run in scratch programs
during discovery; they are **NON-REPOSITORY and NOT REPRODUCIBLE FROM THE
COMMIT** and constitute no part of the evidence.

---

## 28. Phase 3 results, `9ccd28e`

| Item | Value |
|---|---|
| Tests | 90 |
| Checks | 761 |
| Failures | 0 |
| Safety counterexamples found | none |
| TC-001..TC-078 | unchanged, output byte-identical |
| Strict C99, ASan, UBSan | clean |
| Two complete runs | byte-identical |
| Firmware changed in Phase 3 | none |

Six failures appeared during Phase 3 development. All six were errors in
the test bookkeeping, not protocol failures, and none was an invariant
violation: two wrong schedule-count constants, one stale reporting
variable, one re-admission check that straddled 1.5 s of unrelated traffic,
and a freshness-boundary test whose premise was wrong twice, because
injected acknowledgements bypass the caller's lease path and because the
predicate uses the node's own clock, which trails the bus clock within a
step. Corrected against the production predicate, the boundary is exact.

---

## 29. Known liveness findings

These are observed limitations of the implemented mechanism. They are
recorded by tests rather than hidden, and they are **not harmless**: each
costs availability, and neither is repaired automatically.

**Finding A — a node left behind by more than one configuration epoch is
not automatically caught up.** Admission of a committed configuration is
restricted to `epoch + 1`, so a node that misses an entire epoch while the
cluster advances rejects later announcements as unsupported future epochs
and remains on its old configuration. If the live configuration excludes
it, the remaining members refuse its traffic, it exhausts its elections and
latches SAFE through genuine election exhaustion. Recovery requires an
operator. Evidenced by TC-083 over a ten-case matrix.

**Finding B — conflicting acceptance bindings for one epoch can deadlock
that epoch permanently.** If one proposer binds successor X on some nodes
and fails, and a new leader that never bound X proposes a different
successor Y, then X cannot reach its quorum because the Y-binders refuse
it, and Y cannot reach its quorum because the X-binders refuse it. The
cluster continues to operate normally under the current configuration, but
the configuration epoch can never advance. There is no prepare-and-adopt
phase by which a new proposer would adopt the highest accepted value.
Evidenced by TC-085.

A third, structural consequence is worth stating with them: while a
transaction is unresolved and the proposer is absent, no leader can be
formed, and a membership of two tolerates no failure from the moment it is
accepted. Past the election budget the remaining nodes report the absence
of quorum by latching SAFE (TC-074 D).

Neither Finding A nor Finding B produced a safety invariant violation in
any executed scenario.

---

## 30. Untested state space

LOT 5 did **not** establish, and this report does not claim:

- time wraparound safety;
- heartbeat sequence wraparound safety;
- clusters larger than three nodes;
- Byzantine tolerance;
- cryptographic membership authentication;
- unbounded schedule exploration;
- formal correctness;
- physical persistence;
- physical CAN or CAN-FD behaviour;
- STM32 behaviour;
- FreeRTOS behaviour;
- flight readiness;
- TRL 4.

In addition: multi-transaction interleavings were bounded, exercised to a
depth of five consecutive epochs; delay values were enumerated rather than
continuous; and concurrent proposers in genuinely disjoint partitions
cannot arise in a three-node cluster with a two-node quorum, so that region
is unreachable here rather than tested.

---

## 31. Claim boundary

The maximum claim supported by this work is:

> In the deterministic three-node host demonstrator, and within the bounded
> adversarial state space executed by TC-061 through TC-090, no safety
> counterexample was observed after implementation of the LOT 5
> reconfiguration mechanism.

The following wordings are **not** supported and are not used: proved safe;
formally verified; guaranteed safe; complete verification; flight safe;
validated for arbitrary cluster sizes.

Evidence classification for every item in this report: IMPLEMENTED-SIM.

---

## 32. Reproducibility

Commit `9ccd28e867d74cb9667addb5676ad009fa849a07` on
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

Determinism is checked by running the suite twice and comparing:

```
./build/test_mosaik > run_a.txt
./build/test_mosaik > run_b.txt
diff run_a.txt run_b.txt
```

Expected: `761 checks, 0 failures`, exit code 0, no sanitizer report, and
an empty diff. Expected lines include TC-079 `1536 schedules`, TC-087
`evidence last counted at age 99 ms, first stale at age 100 ms`, and TC-090
`4608 schedules explored, 0 violating`. The Phase 1 RED baseline reproduces
from `02b27fa` with the same commands: `515 checks, 11 failures` in exactly
TC-064, TC-065, TC-066, TC-067 and TC-068.

Validated with gcc 13.3.0 on Linux; the simulation is integer-only and
deterministic. The complete suite runs in roughly three seconds.

The repository contains a workflow at `.github/workflows/ci.yml` that runs
`make test` on push and pull request. **No external continuous-integration
status check is attached to commit `9ccd28e` at the time of writing.** Every
result in this report was produced by running the commands above directly;
nothing here was independently reproduced by a hosted service, and the
presence of the workflow file is not evidence that it did so.

---

## 33. Commit lineage

| Commit | Message | What changed | Result |
|---|---|---|---|
| `7d35cc8` | docs: close LOT 4 mode semantics host validation | LOT 5 starting point | 60 / 455 / 0 |
| `02b27fa` | test: add LOT 5 autonomous reconfiguration RED baseline | `test/test_mosaik.c` only | 70 / 515 / 11 in TC-064..TC-068 |
| `146472f` | fix: implement LOT 5 safe membership reconfiguration | firmware core, header, codec, and tests | 78 / 667 / 0 |
| `9ccd28e` | test: harden LOT 5 reconfiguration adversarial coverage | `test/test_mosaik.c` only; **no firmware change** | 90 / 761 / 0 |
| (this commit) | docs: close LOT 5 autonomous reconfiguration validation | documentation only | 90 / 761 / 0 |

`git diff 146472f..9ccd28e -- firmware/` is empty, and
`git diff 9ccd28e..HEAD -- firmware/ test/` is empty.

---

## 34. Conclusion

LOT 5 replaced a rigid, implicitly safe membership with an explicit
committed membership that can be changed at run time by a distributed
transaction, without weakening any validated invariant of LOT 2, LOT 3 or
LOT 4. The decisive design result is negative: authorising a new
configuration with a majority of the old one alone is not sufficient, and
the counterexample of section 7 shows why. The adopted rule requires a
quorum of both configurations, evaluates authority against the holder's own
committed membership, and separates who may take part in consensus from who
may stand for election.

Against a bounded adversarial campaign of 6236 committed schedules under a
continuous per-millisecond invariant oracle, no safety counterexample was
observed. Two liveness limitations were found, recorded and left
unrepaired, because repairing them belongs to a prepare-and-adopt recovery
mechanism that LOT 5 deliberately did not introduce.

LOT 5 is closed for the deterministic three-node host demonstrator:
requirements basis, implementation, tests, evidence, regression status and
limitations are documented, as the ROADMAP governance rule requires. It is
not a proof, not a hardware result, and not a statement about any cluster
other than the one tested.
