# Changelog — Voice Command Assistant

All notable progress on the project is recorded here.

---

## [2026-07-14] — Phase 0 Complete

- Configured FreeRTOS (CMSIS-RTOS v2) successfully.
- Implemented two independent blinky tasks at different rates, proving the RTOS scheduler.
- Set up a highly optimized, non-blocking Thread-Safe Logger Task using a FreeRTOS Message Queue and UART2 with DMA.
- Implemented a custom `printl` wrapper function (`logger.c`/`logger.h`) to safely format strings and push them to the DMA Logger Task.
- **Status:** Phase 0 complete. Moving to Phase 1a (Audio Pipeline).

## [2026-07-12] — Project Kickoff

- Created the STM32CubeIDE project (`Voice_Command_Assistant_STM32`) targeting the STM32F407G-DISC1.
- Wrote the comprehensive spec document (`SPECS.md`) covering architecture, task table, memory budget, phases, and acceptance criteria.
- Created `CHECKLIST.md` and `CHANGELOG.md` for project tracking.
- **Status:** Fresh CubeIDE project — no pins configured, no FreeRTOS enabled, no code written yet. Ready to begin Phase 0.
