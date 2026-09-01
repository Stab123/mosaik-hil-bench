# MOSAIK HIL Bench — Test Plan

**Document:** MOSAIK-HIL-TP-001
**Issue:** 0.1 — 1 September 2026

## 1. Two levels of verification

**Level 1 — logic verification on host.** The protocol core is compiled for the
host and driven by a virtual bus in simulated time. This verifies state machine
behaviour and message handling. It produces no timing evidence: the millisecond
values printed by the suite are simulated, not measured.

**Level 2 — timing measurement on hardware.** Three nodes on a physical bus,
observed by an external logger. This produces the measured latencies. Not yet
executed; hardware not yet procured.

Nothing in this repository claims a measured result until Level 2 has run and
the traces are committed under `results/`.

## 2. Level 1 test cases

| ID | Title | Requirement | Result |
|---|---|---|---|
| TC-001 | A single leader is elected and held | REQ-002 | pass |
| TC-002 | No two leaders at any instant over 20 s | REQ-002 | pass |
| TC-003 | New leader elected after leader power loss | REQ-004 | pass |
| TC-004 | SAFE latched on same-term dual leader | REQ-005, REQ-002 | pass |
| TC-005 | Isolated node cannot self-appoint, latches SAFE | REQ-003 | pass |
| TC-006 | Frame codec round-trip, corrupted frames rejected | — | pass |

Run with `make test`. The suite returns a non-zero exit code on any failure and
is executed on every push by the CI workflow.

## 3. Level 2 measurement method

**Instrumentation.** A USB-CAN adapter in listen-only mode logs every frame
with a kernel timestamp (`candump -ta`). Each node additionally drives one GPIO
high while holding leadership and one GPIO high while in SAFE; both are captured
by a logic analyser.

Serial output over USB is used for diagnostics only. It is not a measurement
path: the USB polling interval is of the same order as the intervals being
measured.

**Fault injection.** The leader's supply is interrupted by a MOSFET switch
driven from the host, giving a repeatable and representative loss-of-node
event. Injecting a "kill" frame instead would bypass timeout-based detection
and is therefore not used for REQ-004.

**Measurands.**

| Requirement | Measurand | Source |
|---|---|---|
| REQ-004 | interval from last heartbeat of the failed leader to first heartbeat of the new leader | bus timestamps |
| REQ-004 | interval between leadership GPIO falling on the old leader and rising on the new one | logic analyser |
| REQ-005 | interval from injected frame on the bus to SAFE GPIO rising | logic analyser |
| — | bus load at 500 kbit/s under nominal traffic | bus timestamps |

**Sample size.** Thirty repetitions per measurand. Reporting minimum, median,
maximum and standard deviation. A single figure is not reported.

**Pass criteria.** REQ-004: all thirty failover intervals below 1000 ms.
REQ-005: all thirty detection-to-SAFE intervals below 10 ms. Any outlier is
reported, not discarded.

## 4. Known limitations of the bench

- Three nodes, so quorum is two and a single loss is the only tolerable fault.
- Bench conducted at ambient conditions. No vibration, thermal or EMC
  environment. Results carry no environmental qualification claim.
- Commercial development boards, not representative hardware. No radiation,
  derating or parts-quality argument is available.
- Clock drift between nodes is not characterised.
- Results are valid for the timing parameters of PROTOCOL.md section 6 only.
