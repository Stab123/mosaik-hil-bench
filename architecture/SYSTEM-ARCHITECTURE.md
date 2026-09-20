# MOSAÏK System Architecture

**Document:** MOSAIK-SYSARCH-001  
**Issue:** 1.0 — 15 September 2026  
**Source:** MOSAIK-ADD-0001, Issue 1 / Rev 1, dated 24 April 2026  
**Scope:** Physical system architecture per ADD V1

---

## 1. System Nodes (ADD V1)

| Node ID | Role | Description | Key Functions |
|---------|------|-------------|---------------|
| **EN-1** | Experimental Node — Fluidics | Fluid management experiment | Fluidic control, sensing, actuation |
| **EN-2** | Experimental Node — Thermal | Thermal management experiment | Thermal control, heaters, sensors |
| **EN-3** | Experimental Node — Optical | Optical payload experiment | Optical sensing, calibration |
| **CN-1** | Coordination Node | Cluster leadership, safety arbitration | Leader election, quorum, SAFE arbitration, blackbox |
| **COMN-1** | Communication Node | Ground link, data routing | GSE comms, log export, time sync |
| **GSE-1** | Ground Support Equipment / MCC | Ground station | Mission control, telemetry, commanding |

**Total:** 6 nodes (3 EN + CN + COMN + GSE)

---

## 2. System Backbones

| Backbone | Technology | Bitrate | Purpose | Nodes Connected |
|----------|------------|---------|---------|-----------------|
| **Primary** | CAN-FD | Arbitration: 500 kbit/s<br>Data: 2 Mbit/s | Safety-critical coordination, heartbeats, votes, SAFE | All 6 nodes |
| **Secondary** | Ethernet | TBD (100/1000 Mbit/s) | High-rate payload data, logs, updates | EN-1, EN-2, EN-3, CN-1, COMN-1 |
| **Safety Discretes** | Wired GPIO | DC / low frequency | Hard safety signals | All nodes → CN, GSE |
| **Power** | 24 V distributed | DC | Node power with individual enables | All nodes |

**Note:** CAN-FD bitrates per ADD Section 3. The bench has **no physical bus at all**: its frames exist only on a host virtual bus. Its *planned* physical configuration is CAN-FD at 500 kbit/s arbitration and 2 Mbit/s data, PLANNED and UNVALIDATED (`docs/ICD-HIL.md` §5). The logical frame — 11-bit identifier, DLC 8 — is transport-independent and is not widened by adopting FD (`docs/ICD-HIL.md` §5.1). See ADD-F004, which remains open.

---

## 3. Safety Discretes

| Signal | Direction | Type | Description |
|--------|-----------|------|-------------|
| **SAFE_ASSERT** | CN → All | Active-low, latched | Commands cluster into SAFE mode |
| **E_STOP** | GSE → CN, ENs | Active-low, momentary | Emergency stop from ground |
| **NODE_FAULT_N** | Each node → CN | Active-low, per-node | Node self-reported fault |
| **POWER_EN** | CN → Each node | Active-high, per-node | Power enable from CN |
| **LEADER_ASSERT** | CN → COMN, GSE | Active-high | Indicates valid leadership authority |

**Discrepancy note:** Current host demonstrator implements SAFE latching in software only, within one powered node instance. No physical discretes, no software substitute for them (LOT 3 decision D3/D7; LOT 11). See ADD-F003.

---

## 4. System Modes (ADD Baseline)

| Mode | Description | Entry Conditions | Exit Conditions |
|------|-------------|------------------|-----------------|
| **INIT** | Power-on, self-test, bus initialization | Power-up, reset | Self-test pass, bus up |
| **NOMINAL** | Normal operation, leader elected, all nodes healthy | INIT complete, leader valid | Fault detected, partition |
| **ADAPTIVE** | Reconfiguration in progress, reduced capability | Single node loss, partition detected | Reconfiguration complete or SAFE |
| **DEGRADED** | Operating with reduced redundancy (e.g., 1 EN lost) | ADAPTIVE complete, quorum maintained | Further fault → SAFE, or recovery → NOMINAL |
| **SAFE** | Safety latched, all actuators safe, await ground | Split-brain, no quorum, critical fault | Ground arbitration (PGA) only |
| **PGA** | Pending Ground Arbitration | SAFE entered | Ground command → INIT or POWER_OFF |

**Discrepancy note:** Current host demonstrator uses: INIT, NOMINAL, DEGRADED, SAFE. ADAPTIVE and PGA not implemented. Terminology differences documented in ADD-F003.

---

## 5. Leadership & Authority Model

| Concept | Definition |
|---------|------------|
| **Leader** | Node holding `ROLE_LEADER` (elected by quorum) |
| **Valid Leadership Authority** | Leader with active lease (500 ms) and quorum connectivity |
| **Lease** | 500 ms nominal, renewed only on acknowledgements actually received from a quorum of peers in the current term (Lot 2C, host model); outbound heartbeat delivery alone never renews |
| **Quorum** | Majority of cluster (4 of 6 for full; 2 of 3 for bench subset) |
| **Split-brain** | Two nodes claiming leadership in same term → immediate SAFE |
| **Transport (host, Lot 6A)** | A node's own controller state — UP, DEGRADED (error-passive, still transmits), BUS_OFF, RECOVERING. Local evidence about that node only. BUS_OFF and RECOVERING revoke leadership authority and suppress transmission; neither latches SAFE. Software-facing contract only; hardware detection and recovery timing are LOT 6B (`PROTOCOL.md` §11) |
| **SAFE (host)** | Latched for the lifetime of one powered node instance; follower role, no authority, no participation, SAFE announcements only; cold restart clears it (no persistence); no PGA (Lot 3) |
| **DEGRADED (host)** | Cluster-awareness state held while a peer's SAFE frame actually received is fresh (3 heartbeat periods); does not revoke authority (Lot 3, ADD-F011) |

---

## 6. Current Host Demonstrator vs. ADD Architecture

**Reading note.** This table records the **delta** between the ADD reference architecture and what this bench implements. It is a description, not a work list: this repository is an experimental HIL bench and is **not** required to converge structurally to the ADD. Deltas that amount to building the complete architecture — the EN/CN/COMN/GSE node set, the six-mode model, power control, flight-representative hardware — are deferred to MOSAÏK Advanced. See `ROADMAP.md` §2.

| Aspect | ADD V1 (Target) | Current Repository (Host Sim) | Delta (informative) |
|--------|-----------------|-------------------------------|-----|
| Nodes | 6 (3 EN, CN, COMN, GSE) | 3 generic coordination nodes | Node set differs by design — MOSAÏK Advanced |
| CAN-FD | 500k/2M bit/s, FD frames | Classical CAN 2.0B, 500k arbitration | FD data phase, bitrate |
| Ethernet | Secondary backbone | Not implemented | LOT 6 |
| Safety discretes | 5 wired signals | Software-only SAFE latch (LOT 3 SAFE contract; no software discrete substitute) | LOT 11 |
| Power control | 24V distributed with enables | Not implemented | MOSAÏK Advanced (bench hardware needs only what LOT 13 requires) |
| System modes | 6 (INIT, NOM, ADAP, DEG, SAFE, PGA) | 4 (INIT, NOM, DEG, SAFE) | ADAPTIVE and PGA are ADD modes — MOSAÏK Advanced (ADD-F003, ADD-F009) |
| Leadership lease | 500 ms (target) | 500 ms (simulated) | LOT 2A host only |
| Blackbox logging | CN-authoritative | Not implemented | CN-authoritative blackbox — MOSAÏK Advanced; HIL run chronology — LOT 7 |
| Time sync | IEEE 1588 / CAN sync | Simulated monotonic clock | LOT 6, 11 |

---

## 7. Architecture Diagram (Textual)

```
                    +------------------+
                    |     GSE-1        |  (Ground Station / MCC)
                    |  Telemetry/Command |
                    +--------+---------+
                             |
                    Ethernet (secondary)
                             |
         +-------------------+-------------------+
         |                   |                   |
    +----v----+         +----v----+         +----v----+
    | EN-1    |         | EN-2    |         | EN-3    |
    | Fluidics|         | Thermal |         | Optical |
    +----+----+         +----+----+         +----+----+
         |                   |                   |
         +-------------------+-------------------+
                             |
                      CAN-FD (primary)
                             |
                      +------v------+
                      |    CN-1     |  (Coordination Node)
                      |  Leadership |
                      |  Quorum     |
                      |  Blackbox   |
                      +------+------+
                             |
                      +------v------+
                      |  COMN-1     |  (Communication Node)
                      |  GSE Link   |
                      |  Log Export |
                      +-------------+

Safety Discretes (wired, parallel to CAN):
  SAFE_ASSERT  ← CN to all nodes
  E_STOP       ← GSE to CN, ENs
  NODE_FAULT_N ← Each node to CN
  POWER_EN     ← CN to each node
  LEADER_ASSERT← CN to COMN, GSE
```

---

## 8. Terminology Mapping

| ADD Term | Repository Term | Status |
|----------|-----------------|--------|
| INIT | MOSAIK_STATE_INIT | MATCH |
| NOMINAL | MOSAIK_STATE_NOMINAL | MATCH |
| ADAPTIVE | (not implemented) | GAP |
| DEGRADED | MOSAIK_STATE_DEGRADED | MATCH (host exit/freshness semantics are an interpretation, ADD-F011) |
| SAFE | MOSAIK_STATE_SAFE | MATCH |
| PGA | (not implemented) | GAP |
| Leader | MOSAIK_ROLE_LEADER | MATCH |
| Valid Leadership Authority | mosaik_has_valid_leadership_authority() | LOT 2A |
| Split-brain | MOSAIK_SAFE_SPLIT_BRAIN | MATCH |
| Quorum | `floor(n/2)+1` | MATCH |

See `ADD-FINDINGS.md` ADD-F003 for mode terminology details.