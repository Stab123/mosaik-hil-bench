# MOSAÏK Software Architecture

**Document:** MOSAIK-SWARCH-001  
**Issue:** 1.0 — 15 September 2026  
**Source:** MOSAIK-ADD-0001 firmware architecture  
**Scope:** Target firmware architecture layers and modules

---

## 1. Architecture Layers (ADD Target)

| Layer | Name | Responsibility | Target Implementation |
|-------|------|----------------|----------------------|
| **L0** | Startup | Hardware init, clock config, bootloader handshake | `startup_stm32h743.c`, linker script |
| **L1** | HAL | MCU peripheral abstraction (RCC, GPIO, NVIC, DMA) | STM32 HAL / LL drivers |
| **L2** | BSP | Board-specific config (pinmux, clocks, external components) | `bsp_<board>.c/h` |
| **L3** | Drivers | Peripheral drivers with defined interfaces | `drv_*.c/h` |
| **L4** | Services | Protocol, logging, time, file system, health | `svc_*.c/h` |
| **L5** | RTOS Tasks | FreeRTOS tasks with priorities, stacks, queues | `task_*.c/h` |
| **L6** | Mission | Experiment-specific applications | `mission_*.c/h` |

---

## 2. Target Platform

| Parameter | Value |
|-----------|-------|
| **MCU** | STM32H743 (dual-core Cortex-M7/M4) |
| **RTOS** | FreeRTOS (latest LTS) |
| **Toolchain** | arm-none-eabi-gcc / clang |
| **Debug** | SWO, JTAG, OpenOCD |
| **Bootloader** | Custom secure boot (LOT 11+) |

---

## 3. Driver Layer (L3)

| Driver | Interface | Key Functions | Status |
|--------|-----------|---------------|--------|
| `drv_canfd` | CAN-FD controller (FDCAN) | Init, Tx/Rx queues, filter config, bus-off recovery | DESIGN-ONLY |
| `drv_eth` | Ethernet MAC (GMAC) | Init, DMA descriptors, PTP/IEEE 1588 | DESIGN-ONLY |
| `drv_uart` | UART/USART | DMA Tx/Rx, protocol framing | DESIGN-ONLY |
| `drv_spi` | SPI master | DMA transfers, chip select mgmt | DESIGN-ONLY |
| `drv_i2c` | I2C master | DMA, multi-master arbitration | DESIGN-ONLY |
| `drv_adc` | ADC | DMA, continuous conversion, calibration | DESIGN-ONLY |
| `drv_dma` | DMA controller | Stream config, callbacks, circular buffers | DESIGN-ONLY |

---

## 4. Service Layer (L4)

| Service | Responsibility | Key APIs | Status |
|---------|----------------|----------|--------|
| `svc_mosaik_proto` | MOSAIK wire protocol (encode/decode/state machine) | `mosaik_encode`, `mosaik_decode`, `mosaik_node_t` | **IMPLEMENTED-SIM** (host) |
| `svc_logger` | Structured logging, correlation_id, blackbox | `log_event`, `log_critical`, `blackbox_write` | DESIGN-ONLY |
| `svc_tm` | Telemetry formatting, CCSDS/COMPACT | `tm_pack`, `tm_send` | DESIGN-ONLY |
| `svc_fs` | File system (LittleFS/FATFS) on external flash | `fs_mount`, `fs_write`, `fs_read` | DESIGN-ONLY |
| `svc_time` | Time sync (IEEE 1588, CAN sync), monotonic clock | `time_now`, `time_sync` | DESIGN-ONLY |

---

## 5. RTOS Task Layer (L5)

| Task | Priority | Period/Trigger | Core Function | Status |
|------|----------|----------------|---------------|--------|
| `Safety_Task` | Highest (configMAX_PRIORITIES-1) | 1 ms / event | SAFE monitoring, discretes, watchdog | DESIGN-ONLY |
| `Cluster_Task` | High | 10 ms / event | Leader election, heartbeat, quorum, lease | **IMPLEMENTED-SIM** (host logic) |
| `Mission_Task` | Medium | 100 ms / event | Experiment control loops | DESIGN-ONLY |
| `Logger_Task` | Low | 100 ms / queue | Log persistence, blackbox, GSE export | DESIGN-ONLY |
| `Monitor_Task` | Medium | 1 s | Health, resource usage, thermal | DESIGN-ONLY |
| `Companion_Task` | Low | Event | Companion computer interface | DESIGN-ONLY |

**Stack sizing:** TBD per static analysis (LOT 10/11)

---

## 6. Mission Module Layer (L6)

| Module | Node | Experiment | Status |
|--------|------|------------|--------|
| `mission_en1_fluidic` | EN-1 | Fluid management | DESIGN-ONLY |
| `mission_en2_thermal` | EN-2 | Thermal control | DESIGN-ONLY |
| `mission_en3_optic` | EN-3 | Optical payload | DESIGN-ONLY |

---

## 7. Current Host Demonstrator vs. Target Architecture

**Reading note.** This table records the **delta** between the ADD target software architecture and what this bench implements. It is a description, not a work list for this repository. `mosaik-hil-bench` is an experimental HIL bench; building the complete layered architecture — the full driver and service sets, the RTOS task set and the mission modules — belongs to the separate future project MOSAÏK Advanced. This repository implements only what the HIL experiment needs (`ROADMAP.md` §2, §4, §6).

| Layer | Target (ADD) | Current Repository | Delta (informative) |
|-------|--------------|-------------------|-----|
| L0 Startup | STM32H743 boot | `main()` in test harness | Complete redesign |
| L1 HAL | STM32 HAL/LL | None (host libc) | Full port |
| L2 BSP | Board-specific | None | Board bring-up |
| L3 Drivers | 7 drivers | None (virtual CAN bus) | All drivers |
| L4 Services | 5 services | `svc_mosaik_proto` only (host) | 4 services; the bench adds only the time, logging and health services its run chronology needs (LOT 7, `ROADMAP.md` §6.2) |
| L5 RTOS Tasks | 6 tasks | Single-threaded sim loop | FreeRTOS port |
| L6 Mission | 3 modules | None | Mission modules — MOSAÏK Advanced. The bench introduces only a minimal experimental function abstraction, for redistribution testing (LOT 8, `ROADMAP.md` §6.3) |

**Critical distinction:** The current repository implements **only** `svc_mosaik_proto` (protocol + node state machine) as a **host-hosted, single-threaded, deterministic simulation**. It does not contain any STM32, FreeRTOS, or hardware-specific code.

---

## 8. Module Dependency Graph (Target)

```
L6 Mission Modules
    │
    ▼
L5 RTOS Tasks ──► FreeRTOS (queues, semaphores, timers)
    │
    ▼
L4 Services ◄──┬── svc_mosaik_proto ◄── Cluster_Task
    │          ├── svc_logger ◄── Logger_Task, all tasks
    │          ├── svc_tm ◄── COMN_Task
    │          ├── svc_fs ◄── Logger_Task
    │          └── svc_time ◄── all tasks
    ▼
L3 Drivers ◄── drv_canfd, drv_eth, drv_uart, drv_spi, drv_i2c, drv_adc, drv_dma
    │
    ▼
L2 BSP ◄── Pinmux, clocks, external components
    │
    ▼
L1 HAL ◄── STM32 HAL / LL
    │
    ▼
L0 Startup ◄── Vector table, clock tree, bootloader
```

---

## 9. Porting Notes for LOT 11+

1. **Clock model:** Replace simulated `now_ms` with `svc_time` (FreeRTOS tick + PTP offset)
2. **CAN transport:** Replace virtual bus callback with `drv_canfd` Tx/Rx queues. The driver must also report this node's own controller error-confinement state through `mosaik_set_transport_status()` (`docs/ICD-HIL.md` §3). The protocol core trusts that report: a driver that reports UP while the controller is bus-off defeats every Lot 6A guarantee, so validating the report is part of LOT 6B.
3. **Concurrency:** Protect `mosaik_node_t` with mutex or run in single `Cluster_Task`
4. **Persistence:** Add `svc_fs` for term/vote persistence across reset — NOT implemented. The LOT 2D host demonstrator restarts cold through `mosaik_init()` and loses all volatile term/vote state by design (see `LOT2D_CRASH_RECOVERY_REPORT.md` §3, §13); a target Lot for persistence is not yet assigned
5. **Discretes:** Implement `Safety_Task` reading/wiring GPIO for SAFE_ASSERT, etc.
6. **Memory:** Static allocation only; no heap after init (LOT 10 verification)
7. **Determinism:** Preserve deterministic RNG for reproducible tests. Reproducibility is a HIL requirement (`ROADMAP.md` §6.1); any hardware-entropy source for a flight build is a MOSAÏK Advanced concern

---

## 10. Compliance Statement

**This document describes the TARGET ADD ARCHITECTURE.**  
The current repository implements a **host demonstrator** covering only a subset of `svc_mosaik_proto` and `Cluster_Task` logic. No claim is made that STM32/FreeRTOS/HIL code exists. All target modules are DESIGN-ONLY here.

**This document is an architectural reference, not an obligation on this repository.** DESIGN-ONLY does not mean "scheduled for implementation in `mosaik-hil-bench`". The HIL bench implements only what its experimental objective requires; the complete ADD software architecture is the scope of the separate future project MOSAÏK Advanced. See `ROADMAP.md` §2.