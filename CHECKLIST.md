# Project Checklist — Voice Command Assistant

Detailed, phase-by-phase checklist derived from [SPECS.md](./SPECS.md). Check off items as you complete them.

---

## Setup — Tools & Environment

- [✔] STM32CubeIDE installed and working
- [ ] STM32Cube AI Studio (STEDGEAI-CUBEAI) installed
- [ ] Python environment set up (Python + TensorFlow/Keras + TFLite)
- [✔] CubeIDE project created for STM32F407G-DISC1

---

## Phase 0 — Environment & Bring-Up

> **Goal:** Toolchain works, board boots, Logger Task printing over UART+DMA.

### 0.1 — FreeRTOS Bring-Up
- [✔] Enable FreeRTOS (CMSIS-RTOS v2) in the `.ioc` configurator
- [✔] Configure system tick source (set SysTick for HAL, use a different timer for FreeRTOS — CubeMX will prompt you)
- [✔] Create 2–3 blinky tasks blinking different onboard LEDs (LD3/PD13, LD4/PD12, LD5/PD14, LD6/PD15)
- [✔] Verify tasks run at different rates and priorities behave as expected
- [✔] Build, flash, and confirm LEDs blink independently

### 0.2 — Logger Task (UART + DMA)

**A. Hardware & DMA setup (`.ioc`)**
- [✔] Enable a UART peripheral (e.g. USART2, using a USB-to-TTL adapter) in **Asynchronous** mode
- [✔] Under USART2 → DMA Settings: add a **DMA request for TX**, Direction = Memory to Peripheral, Mode = Normal (not circular)
- [✔] Enable the **USART2 global interrupt** (NVIC tab) — required for the DMA-complete callback
- [✔] Regenerate code

**B. FreeRTOS objects (`.ioc` → Middleware → FreeRTOS)**
- [✔] Create a **Message Queue** named `LogQueue`: item size = `128` (bytes), queue depth = `10`
- [✔] Create a **Binary Semaphore** named `DmaTxSem`
- [✔] Create a new task named `myTaskLogger`, priority = **Low**, default stack
- [✔] Regenerate code — `LogQueueHandle` and `DmaTxSemHandle` will appear in `freertos.c`

**C. Logger Task logic (`freertos.c` → `StartTaskLogger`)**
- [✔] Declare a local `char msgBuffer[128]` inside the task
- [✔] In the `for(;;)` loop: call `osMessageQueueGet(LogQueueHandle, msgBuffer, NULL, osWaitForever)` to sleep until a message arrives
- [✔] On success: call `HAL_UART_Transmit_DMA(&huart2, (uint8_t*)msgBuffer, strlen(msgBuffer))`
- [✔] Immediately after: call `osSemaphoreAcquire(DmaTxSemHandle, osWaitForever)` — the task is locked until DMA is done

**D. DMA-complete ISR callback (`freertos.c` → USER CODE BEGIN Application)**
- [✔] Implement `HAL_UART_TxCpltCallback` — when `huart->Instance == USART2`, call `osSemaphoreRelease(DmaTxSemHandle)` — this unlocks the Logger Task the instant the last byte leaves the wire

**E. `printl()` wrapper (new file: `logger.c` / `logger.h`)**
- [✔] Create `logger.h` declaring `void printl(const char *fmt, ...)`
- [✔] Create `logger.c` implementing `printl`: use `va_list` + `vsnprintf` to format into a 128-byte stack-local buffer, then call `osMessageQueuePut(LogQueueHandle, buffer, 0, 10)` with a 10-tick timeout (drop the message rather than block if the queue is full)
- [✔] Include `logger.h` in `freertos.c` and replace the temporary `printf` calls in the blinky tasks with `printl("Blinking LED 12 and 14\r\n")`

**F. Verify**
- [✔] Build and flash — confirm no compile errors
- [✔] Open a serial terminal (115200 baud) and confirm `printl` output from both blinky tasks appears correctly, in order, with no garbled characters
- [✔] Unplug/replug the ST-Link USB — confirm the board does **not** freeze (unlike USB CDC)

### ✅ Phase 0 Acceptance
- [✔] **LEDs blink independently at correct rates**
- [✔] **`printl()` output from all tasks appears correctly in a serial terminal and unplugging the cable does not freeze the firmware**

---

## Phase 1a — Audio Pipeline (on MCU)

> **Goal:** Mic capture → PCM → MFCC fully working on hardware.

### 1a.1 — Mic Bring-Up (I2S2 + DMA)
- [ ] Configure I2S2 for the onboard MP45DT02 PDM mic in `.ioc`
- [ ] Configure DMA for I2S2 RX (circular mode)
- [ ] Add the PDM2PCM middleware library to the project
- [ ] Initialize the mic with `BSP_AUDIO_IN_Init` (or manual I2S+DMA init)
- [ ] Create the Audio Capture Task — DMA ISR pushes raw data to a Stream Buffer (`xStreamBufferSendFromISR`)
- [ ] Dump raw PDM samples over UART, visually verify on PC (clap → visible spike)

### 1a.2 — PDM → PCM Decimation
- [ ] Apply ST's PDM2PCM filter to convert raw PDM → 16 kHz PCM
- [ ] Dump PCM waveform over UART, plot on PC
- [ ] Verify it looks like recognizable audio, not noise

### 1a.3 — MFCC Feature Extraction
- [ ] Add CMSIS-DSP library to the project
- [ ] Implement the Feature Extraction Task: windowing (30ms window, 20ms stride) → FFT → Mel filterbank → MFCC
- [ ] Feature queue: use pointer/index-based queue items (`{buffer_index, length}`), **not** full buffer copies
- [ ] Push 49×10 MFCC feature matrices to the Feature Queue
- [ ] Dump a feature matrix over UART for a known sound — verify shape and values (not all-zero, not NaN)

### 1a.4 — Sample Rate Verification
- [ ] Sanity-check actual sample rate (scope, logic analyzer, or known-frequency tone) — don't trust CubeMX config alone

### ✅ Phase 1a Acceptance
- [ ] **Raw PDM captured via DMA, verified on PC**
- [ ] **PDM→PCM decimation verified (clean waveform)**
- [ ] **MFCC feature matrices verified (correct shape, sane values)**

---

## Phase 1b — First AI Deployment (pretrained model)

> **Goal:** End-to-end flat voice-controlled LED with a pretrained model. Zero ML risk.

### 1b.1 — Model Deployment
- [ ] Download a pretrained Google Speech Commands TFLite model (or grab the TF Lite Micro KWS example model)
- [ ] Open STM32Cube AI Studio → import `.tflite` → select STM32F407 target → generate C files
- [ ] Add generated C files to the CubeIDE project
- [ ] Create the Inference Task — runs model on every MFCC feature matrix from the Feature Queue
- [ ] Verify: say a recognized word → correct label + confidence prints over UART

### 1b.2 — Action Task (LED control)
- [ ] Create the Action Task
- [ ] Map recognized commands to onboard LEDs (e.g. "on"/"off" toggles LD4)
- [ ] Verify: end-to-end voice → LED toggle works reliably

### ✅ Phase 1b Acceptance
- [ ] **Pretrained KWS model running, flat pipeline, LED reacts to voice**
- [ ] **Pointer-based queues confirmed (no buffer copies)**

---

## Phase 2a — Train Your Own Models (Python on PC)

> **Goal:** Replace pretrained model with models you trained yourself.

### 2a.1 — Command Model
- [ ] Collect and prepare dataset (Google Speech Commands + your own voice recordings)
- [ ] Design and train command model (DS-CNN, Keras)
- [ ] Convert to `.tflite` with INT8 quantization
- [ ] Deploy via STM32Cube AI Studio → C files → CubeIDE
- [ ] Verify accuracy is comparable to or better than pretrained model on your own voice

### 2a.2 — Wake-Word Model
- [ ] Record wake-word samples (few hundred utterances, varied distance/angle)
- [ ] Augment with background noise
- [ ] Train tiny wake-word model (3 classes: silence / unknown / wake-word)
- [ ] Convert to `.tflite` with INT8 quantization
- [ ] Deploy via STM32Cube AI Studio → C files → CubeIDE

---

## Phase 2b — Intelligence Layer (on MCU)

> **Goal:** Full wake-word + command cascade with a proper state machine.

### 2b.1 — Orchestrator Task
- [ ] Create the Orchestrator Task — sole owner of system state
- [ ] Implement state machine: `LISTENING → AWAKE → ACTING → LISTENING`
- [ ] Add a one-shot software timer for command-window timeout (`xTimerStart`, **not** `HAL_Delay`)
- [ ] Timer is one-shot (not auto-reload): started on entering AWAKE, stopped/reset if command found before it fires

### 2b.2 — Inference Task Mode Switching
- [ ] Add mode-based model selection in Inference Task (task notification from Orchestrator: `0 = wake, 1 = command`)
- [ ] **No** second inference task, **no** suspend/resume
- [ ] Feature→Inference handoff: depth-1 queue with `xQueueOverwrite()` (newest frame always wins)

### 2b.3 — Wake-Word Debouncing
- [ ] Implement confidence floor (e.g. > 0.8)
- [ ] Implement sliding-window majority vote (e.g. ≥ 3 of last 5 frames agree)
- [ ] Both conditions must be met before declaring a trigger

### 2b.4 — Logger Task integration
- [ ] Have Inference, Orchestrator, and Action Tasks call `printl()` to push structured log lines (state, confidence, inference time, recognized command)
- [ ] (Logger Task itself is already built in Phase 0 — no structural changes needed here)

### 2b.5 — Display Task & OLED
- [ ] Bring up I2C2 for OLED (separate from codec's I2C1 bus)
- [ ] Create dedicated Display Queue (NOT a second consumer on the Log Queue)
- [ ] Create Display Task — renders to OLED
- [ ] Show three lines: current state, last recognized word, last action taken

### ✅ Phase 2 Acceptance
- [ ] **Wake word transitions LISTENING→AWAKE (visible LED color change)**
- [ ] **Recognized command within window fires the action**
- [ ] **Silence or unrecognized command times out back to LISTENING**
- [ ] **OLED displays live state/word/action**

---

## Phase 3 — Production Polish

### 3.1 — Audio Output (CS43L22 Codec)
- [ ] Bring up CS43L22 via I2S3 + DMA (`BSP_AUDIO_OUT_Init` / `Play`)
- [ ] Add confirmation tone on wake-word detection
- [ ] Add distinct tone on command recognition
- [ ] Playback via onboard jack → active speakers (or PAM8403 + passive speaker)

### 3.2 — Self-Feedback Guard
- [ ] Add ~500ms refractory period after returning to LISTENING
- [ ] During refractory: wake-word detections are ignored

### 3.3 — Diagnostic Build Mode
- [ ] Add `#define DIAG_MODE 1` compile-time flag
- [ ] When enabled: bypass ML models, print MFCC timing + queue occupancy, dump raw confidence scores, map LEDs to pipeline stages

### 3.4 — Instrumentation
- [ ] Track and log: audio-to-feature latency, MFCC compute time, inference time, end-to-end response time
- [ ] Monitor queue occupancy (find bottlenecks)
- [ ] Log stack high-water marks (`uxTaskGetStackHighWaterMark`)
- [ ] Log heap usage
- [ ] Profile CPU% per task using FreeRTOS runtime stats

### 3.5 — Optional Extras
- [ ] Relay/servo actions (swap out LED for real actuator)
- [ ] External I2S mic (INMP441) if mic placement becomes an issue
- [ ] Pre-recorded talkback phrases (PCM clips in flash, played via codec path)

### ✅ Phase 3 Acceptance
- [ ] **CS43L22 codec audio output working (tones play through jack)**
- [ ] **Latency / CPU / stack instrumentation in place**
- [ ] **Diagnostic build mode functional**

---

## Memory Budget (fill in with real values as you go)

| Category | Item | Flash | RAM |
|----------|------|-------|-----|
| Code | Application code | | |
| Code | FreeRTOS | | |
| Code | CMSIS-DSP | | |
| Code | STM32Cube AI inference code | | |
| Weights | Wake-word model | | |
| Weights | Command model | | |
| RAM | Task stacks | | |
| RAM | DMA buffers | | |
| RAM | Stream Buffer (ISR→Feature) | | |
| RAM | Feature buffers (MFCC) | | |
| RAM | AI tensor arena | | |
| RAM | Log Queue + Display Queue | | |
| **Total** | | | |
