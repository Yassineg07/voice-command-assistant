# Changelog — Voice Command Assistant

All notable progress on the project is recorded here.

---

## [2026-07-24] — Audio Pipeline Math Perfected & Silence Gate

- Conquered the PDM Filter Library standing wave bug. Solved by aligning STM32's Little-Endian I2S raw data with ST's filter expectations (`__builtin_bswap16` + `PDM_Filter_64_LSB`), producing a perfectly leveled waveform.
- Designed a flawless "Dynamic Silence Gate" State Machine in `mic_test` that tracks the 8ms envelope floor and surgically clamps electrical DC hum (±30 bounds) strictly into 0 before it reaches the AI pipeline!
- Caught the "Chipmunk Pitch-Shifting Flaw": Hardware generates native 32kHz PCM (`I2S_AudioFreq=64000`), but the AI expects 16kHz. Implemented a pristine `2:1 Software Decimation` loop to physically pitch-drop the signal down to 16kHz safely, blocked by a physical 8kHz anti-aliasing ST IIR sweep (`Filter.LP_HZ=8000.0f`).
- Found total parity between `run_4_custom_2word_train.py` and `freertos.c` by adjusting Z-Score math (fixing MCU clamp to match Python's `1e-8f` floor) and scaling `librosa.power_to_db` into MCU matching `10.0f * log10f()`.
- Resized the AI task stack (`512 -> 1024 words`) and correctly shifted MFCC bounds (`MFCC_FRAME_LEN=512`) to support full 16kHz Neural Network mapping.
- **Status:** Core audio extraction and preprocessing is now mathematically perfect. Training the native 16kHz AI model.

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
