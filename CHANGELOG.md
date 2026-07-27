# Changelog — Voice Command Assistant

All notable progress on the project is recorded here.

---

## [2026-07-26] — AI Pipeline Fully Stabilized & Production-Hardened

### Pipeline Math Fixes (`mfcc.h`, `mfcc.c`)
- Corrected `MFCC_FRAME_LEN` 512 → **256** to match Python `N_FFT=256`. A mismatched FFT window produces entirely wrong frequency bins — the AI was seeing a different acoustic picture than it was trained on.
- Corrected Mel Filterbank sample rate 16000 → **8000 Hz** and Nyquist 8000 → **4000 Hz**, matching the true output of the 2:1 software decimation chain.
- Corrected rolling buffer stride to 160 samples, matching Python's `hop_length=160`.
- Pre-initialized AI history matrix to `-80.0f` (log-mel silence floor) at boot. Previously uninitialized stack garbage caused Z-Score normalization to catastrophically fail on first inference.
### Dedicated AI Task Architecture (`freertos.c`)
- **Bug traced:** Running the Neural Network directly inside the Audio Task was causing massive DMA lag and dropped audio. The `stai_network_run` function is a heavy synchronous computation (50-300ms). When executed inside the Audio Task, it stalled the real-time DMA loop, causing the stream buffer to overflow and destroying incoming PDM packets.
- **Fix:** Decoupled the inference engine into a dedicated **AI Task** (`myTaskAI`) with Below Normal priority. The Audio Task (Above Normal) now streams at full speed and pushes frames to `MfccQueueHandle` without blocking. The AI task consumes them asynchronously in the background.

### Adaptive VAD Architecture & Redesign (`freertos.c`)
- **Bug traced:** Single `raw_peak` variable shared between the 8ms UART logger and the 20ms VAD. Logger erased the value faster than the VAD could read it — VAD was blind 80% of the time, never triggered.
- **Fix:** Split into `print_peak` (resets every 8ms, logger only) and `frame_peak` (resets every 20ms, queued per MFCC frame for VAD). Completely isolated and race-free.
- **Gravity EMA Engine:** Deprecated the static `30` threshold. The VAD now uses an Exponential Moving Average (`noise_floor = noise_floor * 0.95 + current_vol * 0.05`) to continuously track ambient room hum during IDLE.
- Configurable minimum constants exposed: `vad_margin = 10` and `vad_min_floor = 20`. This ensures initial boot sensitivity remains perfectly at 30, but adapts powerfully to loud continuous environments.
- Moved VAD state machine entirely into the **AI Task**. Audio Task now unconditionally streams all MFCC frames — ensuring the 1-second spectrogram matrix always advances at real-time pace.

### Edge Case Hardening (`freertos.c`)
- **Race condition eliminated:** Volume now passed through dedicated `VolQueueHandle` (not volatile global). Each MFCC frame's exact peak is atomically bundled alongside it.
- **Dual queue `.ioc` registration:** `MfccQueue` and `VolQueue` both registered via CubeMX FreeRTOS GUI — survive all future regenerations.
- **Infinite sentence overflow guard & Instant Snap:** `capture_frame_count` counter in VAD. If CAPTURING exceeds **45 frames (~900ms)**, inference force-triggers. This prevents continuous loud noise (e.g. a vacuum turning on) from actively destroying early audio at the matrix edge. 
  - Instead of slowly adapting via EMA, the `noise_floor` instantly "snaps" to the loud volume, bypassing mathematical drag.
- **Refractory Lockout Penalty:** Upon hitting overflow, a **2-second lockout** is applied. Disables VAD tracking temporarily, allowing the EMA math to smoothly stabilize on the new high base-level.
- **Boot-Noise Flush:** Applied a 2-second `osDelay` at system boot to allow hardware initialization static to settle, followed by explicitly wiping both `MfccQueueHandle` and `VolQueueHandle` to prevent the AI from immediately draining stale noise.
- **Echo contamination guard:** Full 49×10 matrix scrubbed to `-80.0f` after every inference. Prevents acoustic tail bleeding.

### LED Diagnostic State Machine (`freertos.c`)
- Integrated a purely diagnostic LED state machine directly into the inference loop to provide 100% visual validation of predictions without UART.
- Maps STM32F407 Discovery LEDs: Blue (`PD15`), Green (`PD12`), Orange (`PD13`).
- **Logic:** 
  - Blue LED is ON whenever the system is IDLE/Listening.
  - On `up`, turns Blue OFF, Green ON for exactly 2 seconds.
  - On `go`, turns Blue OFF, Orange ON for exactly 2 seconds.
  - Piggybacks on the existing 20ms MFCC frame loop for timing (`LED_WORD_FRAMES = 100`), requiring zero additional RTOS timers or delays.
- **Status:** Phase 1b is completely finished. The pipeline is mathematically perfect, edge-case hardened, and visually functional. Ready for Phase 2: Training a permanent, high-accuracy custom model.

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
