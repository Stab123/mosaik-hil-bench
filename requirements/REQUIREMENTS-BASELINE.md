# MOSAÏK Requirements Baseline — Namespace Control

**Document:** MOSAIK-REQBASE-001  
**Issue:** 1.0 — 15 September 2026  
**Purpose:** Defines which requirement namespace controls implementation and documents the dual-namespace finding

---

## 1. Authoritative Namespace Declaration

| Namespace | Source | Authority | Prefix Pattern | Status |
|-----------|--------|-----------|----------------|--------|
| **ROOT SYSTEM REQUIREMENTS** | MOSAIK-ADD-0001 Section 10 | **PRIMARY** — controls implementation, verification, and acceptance | REQ-FUNC-xxxx, REQ-SAFE-xxxx, REQ-PERF-xxxx, REQ-ENV-xxxx, REQ-IF-xxxx, REQ-LOG-xxxx | **ACTIVE** |
| **DERIVED / DETAILED REQUIREMENTS** | MOSAIK-ADD-0001 Section 8.2 | **SECONDARY** — traceability to tests/implementation only | REQ-FUN-xxx, REQ-SAF-xxx, REQ-PERF-xxx, REQ-ICD-xxx, etc. | **ACTIVE** |

**Rule:** Until formally resolved by the architecture authority, ROOT requirements (Section 10) are the controlling requirements for implementation decisions. DERIVED requirements (Section 8.2) provide test-level traceability. All mappings must be explicit in `ADD-MAPPING.md` and `TRACEABILITY.md`.

---

## 2. Namespace Comparison

### 2.1 Prefix Mapping

| ROOT (Section 10) | DERIVED (Section 8.2) | Notes |
|-------------------|----------------------|-------|
| REQ-FUNC-xxxx | REQ-FUN-xxx | Functional |
| REQ-SAFE-xxxx | REQ-SAF-xxx | Safety |
| REQ-PERF-xxxx | REQ-PERF-xxx | Performance (same prefix, different numbering) |
| REQ-ENV-xxxx | (not observed) | Environmental |
| REQ-IF-xxxx | REQ-ICD-xxx | Interface/ICD |
| REQ-LOG-xxxx | (not observed) | Logging |

### 2.2 Numbering Scheme Difference

- **ROOT:** 4-digit suffix (e.g., REQ-FUNC-0001)
- **DERIVED:** 3-digit suffix (e.g., REQ-FUN-001)

**Impact:** Direct string matching fails. Explicit mapping table required.

---

## 3. Confirmed Mappings (Initial)

| ROOT Requirement | DERIVED Requirement(s) | Mapping Status |
|------------------|------------------------|----------------|
| REQ-FUNC-0001 (unique leader) | REQ-FUN-001, REQ-FUN-002 | PROVISIONAL |
| REQ-FUNC-0002 (quorum) | REQ-FUN-002 | PROVISIONAL |
| REQ-FUNC-0003 (heartbeat 100 ms) | REQ-FUN-003 | PROVISIONAL |
| REQ-FUNC-0004 (election < 1 s) | REQ-PERF-001 | PROVISIONAL |
| REQ-FUNC-0007 (CRC + correlation_id) | REQ-ICD-001, REQ-ICD-002 | PROVISIONAL |
| REQ-SAFE-0002 (split-brain < 10 ms) | REQ-SAF-001, REQ-PERF-002 | PROVISIONAL |
| REQ-SAFE-0003 (SAFE latch) | REQ-SAF-002 | PROVISIONAL |
| REQ-SAFE-0004 (DEGRADED on peer SAFE) | REQ-SAF-003 | PROVISIONAL |
| REQ-PERF-0001 (election < 1 s) | REQ-PERF-001 | PROVISIONAL |
| REQ-PERF-0002 (SAFE < 10 ms) | REQ-PERF-002 | PROVISIONAL |

**All mappings marked PROVISIONAL** — require architecture review. See `ADD-FINDINGS.md` ADD-F001.

---

## 4. Conflict Detection Rules

The following conditions trigger an ADD finding:

1. **Same prefix, different statement** — e.g., REQ-PERF-xxxx in both namespaces with different text
2. **Scope mismatch** — ROOT says "at all times", DERIVED says "outside declared partition"
3. **Missing counterpart** — ROOT requirement has no DERIVED trace, or vice versa
4. **Numbering collision** — Same numeric ID means different things in each namespace
5. **Verification method disagreement** — ROOT says CbT, DERIVED says CbA

---

## 5. Implementation Control Policy

| Decision | Controlled By |
|----------|---------------|
| What to implement | ROOT requirements (Section 10) |
| How to test | DERIVED requirements (Section 8.2) + test plan |
| Acceptance criteria | ROOT requirements + ADD-FINDINGS resolutions |
| Evidence claims | CbD/CbA/CbT per `TRACEABILITY.md` |
| Limitations documentation | Per-lot evidence reports |

**No implementation decision shall be based solely on DERIVED requirements without ROOT trace.**

---

## 6. Change Control

| Action | Requires |
|--------|----------|
| Add new ROOT requirement | Architecture Change Request (ACR) |
| Modify ROOT requirement text | ACR + impact analysis |
| Add DERIVED requirement | Traceability to ROOT |
| Modify DERIVED requirement | Traceability update + test update |
| Resolve namespace conflict | Architecture authority decision → ADD-FINDINGS closure |

---

## 7. Current Repository Requirements (Host Demonstrator)

The following are **repository-local test requirement IDs** (not ADD requirements):

| Repo Test ID | Description | Traces to ROOT | Traces to DERIVED |
|--------------|-------------|----------------|-------------------|
| TC-001 | Single leader elected | REQ-FUNC-0001 | REQ-FUN-001 |
| TC-002 | No split-brain 20 s | REQ-FUNC-0001 | REQ-FUN-001, REQ-SAF-001 |
| TC-003 | Failover < 1 s | REQ-FUNC-0004 | REQ-PERF-001 |
| TC-004 | SAFE on split-brain < 10 ms | REQ-SAFE-0002 | REQ-SAF-001, REQ-PERF-002 |
| TC-005 | Lone node no quorum → SAFE | REQ-FUNC-0002, REQ-SAFE-0003 | REQ-FUN-002, REQ-SAF-002 |
| TC-006 | Codec + CRC | REQ-FUNC-0007 | REQ-ICD-002 |
| TC-007 | 2+1 partition lease expiry | REQ-FUNC-0001 | REQ-FUN-001, REQ-FUN-002 |

**Note:** Repo TC-xxx IDs are distinct from ADD requirement IDs. Mapping is explicit in `TRACEABILITY.md`.