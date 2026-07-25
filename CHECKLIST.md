# Project Checklist — Voice Command Assistant

Detailed, phase-by-phase checklist derived from [SPECS.md](./SPECS.md). Check off items as you complete them.

---

## Setup — Tools & Environment

- [✔] STM32CubeIDE installed and working
- [✔] STM32Cube AI Studio (STEDGEAI-CUBEAI) installed
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
- [✔] Enable the **USART2 global interrupt** (NVIC tab) — `HAL_UART_TxCpltCallback` is fired by the **UART peripheral interrupt**, not the DMA directly: the DMA finishes → signals the UART peripheral → UART global interrupt fires → callback executes. Without this interrupt enabled, the semaphore is never released and the Logger Task locks forever.
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

**A. Hardware setup (`.ioc`)**
- [✔] Enable **I2S2** under Multimedia → Mode: **Half-Duplex Master**, Operating Mode: **Master Receive**
- [✔] Verify pin assignments: `I2S2_CK` → **PB10**, `I2S2_SD` → **PC3** (matches the MP45DT02 wiring on the Discovery board)
- [✔] Parameter Settings: Standard = **LSB**, Data/Frame Format = **16 Bits Data on 16 Bits Frame**, Audio Frequency = **64 KHz**, Master Clock Output = **Disabled**
  - *Standard Quirk (Philips vs LSB):* The 'I2S Philips' standard imposes a 1-bit clock delay before starting the data frame. Because the MEMS microphone transmits Left/Right channel bits purely mapped to the rising/falling clock edges, a 1-bit delay shifts the entire bit pattern out of phase from the library's expectations. 'LSB' is Right-Justified. Because we are packing exactly 16 bits of data into a 16-bit frame, Right-Justification perfectly packs the bits flush against the WS transition with 0-bit delay (functionally identical to MSB/Left-Justified here, but mathematically vital to bypass the Philips delay).
  - *Note on Clock Quirks:* A 64 KHz audio frequency with 16/16 format produces a physical 2.048 MHz bitclock. The MP45DT02 requires 1.0 MHz to 3.25 MHz. 2.048 MHz puts you directly in the safest, most stable acoustic range.
  - *Why no Master Clock?* The MP45DT02 only needs the Bit Clock (PB10). The MCLK is only required for DAC codecs like the CS43L22 (used in Phase 3).*
- [✔] DMA Settings: Add **SPI2_RX**, Mode = **Circular**. **CRITICAL:** Set Data Width to **Half Word** for both Peripheral and Memory! *(If set to Byte, the DMA will only fill exactly the first half of a `uint16_t` array before looping).*
- **NVIC Note:**
  - **`HAL_I2S_RxHalfCpltCallback` / `HAL_I2S_RxCpltCallback`** are fired **directly by the DMA interrupt** (DMA2 Stream3). Only the DMA interrupt needs to be enabled. The SPI2 global interrupt is NOT needed for these callbacks.
  - **General Callback Rule:** To find which interrupt fires any callback, open `Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_XXX.c`, search for the callback name, and trace it up to its ISR handler — that is the interrupt that must be enabled.
- [✔] DMA2 Stream3 global interrupt is auto-enabled by CubeMX when DMA channel is added — verify in NVIC tab
- [✔] Regenerate code

**B. Application code (`freertos.c`)**
- [✔] Declare `extern I2S_HandleTypeDef hi2s2` and `uint16_t pdm_rx_buffer[128]` in `USER CODE BEGIN Variables`
- [✔] Call `HAL_I2S_Receive_DMA(&hi2s2, pdm_rx_buffer, 128)` before the `for(;;)` in `StartDefaultTask` to start the circular DMA capture
- [✔] Add stub callbacks in `USER CODE BEGIN Application`:
  - `HAL_I2S_RxHalfCpltCallback` — fires when indexes 0–63 are full (first half ready to process)
  - `HAL_I2S_RxCpltCallback` — fires when indexes 64–127 are full (second half ready to process)
  - *(Both stubs are empty for now — we will wire them to the FreeRTOS Stream Buffer in the next step)*

**C. Verify mic is alive**
- [✔] Temporarily add a `printl` inside the `StartDefaultTask` to print raw values from `pdm_rx_buffer`. (Raw PDM looks like random garbage integers jumping around 22000-48000. Pure 0 means hardware failure).
- [✔] Flash and open terminal — confirm values are random and non-zero.
- [✔] Remove the temporary `printl` once verified.

### 1a.2 — PDM → PCM Decimation

**A. Manual Library Integration (`libPDMFilter_CM4F_GCC.a`)**
- [✔] **CRITICAL QUIRK:** In `.ioc`, go to Computing → **CRC** and Activate it. *(The ST PDM library uses hardware CRC for math; if disabled, the MCU will instantly hard-fault!)*
- [✔] Download the `X-CUBE-AUDIO` (or `STSW-STM32068`) zip manually from ST's website.
- [✔] Extract the zip and locate the `pdm2pcm` middleware folder (Middlewares\ST\STM32_Audio\Addons\PDM).
- [✔] Copy **`pdm_filter.h`** into the project's `Core/Inc` folder.
- [✔] Create a new folder `Core/Lib` in CubeIDE and copy **`libPDMFilter_CM4F_GCC.a`** over. (Use `CM4F` because the STM32F407 has a hardware FPU).
- [✔] Link the library in CubeIDE: Right-click Project → Properties → C/C++ Build → Settings → MCU GCC Linker → Libraries.
  - In **Libraries (-l)**, click the little Add icon and type exactly: `PDMFilter_CM4F_GCC` *(Critical GCC rule: remove the "lib" prefix and ".a" suffix!)*
  - In **Library search path (-L)**, click the Add icon, click Workspace, and select the new Core/Lib folder.
- [✔] Add `#include "pdm_filter.h"` in `freertos.c` and confirm it compiles with 0 errors.

**B. Audio Task & Stream Buffer Setup**
- [✔] In `.ioc`, add a new task named `myTaskAudio`, set priority to **Above Normal**, and stack size to **1024 words** (vital for AI tensor BSS footprint).
- [✔] Since CubeMX lacks a Stream Buffer UI in this version, manually declare `#include "stream_buffer.h"` and `StreamBufferHandle_t AudioStreamBuf = NULL;` in `freertos.c`.
- [✔] Manually instantiate the buffer in `MX_FREERTOS_Init` inside `USER CODE BEGIN RTOS_QUEUES`: `AudioStreamBuf = xStreamBufferCreate(256, 128);` *(Total size 256 bytes, trigger task wake at 128 bytes).*

**C. Code Wiring (Data Processing)**
- [✔] In `freertos.c` ISR Callbacks: Use `xStreamBufferSendFromISR()` to push the first half of `pdm_rx_buffer` during Half-Cplt, and the second half during Full-Cplt. 
- [✔] Initialize the filter using `PDMFilter_InitStruct` inside `StartTaskAudio` before the `for(;;)` loop. *(V2.1.0 API Config: 16000 Hz Fs, 10Hz HP / 4000.0f LP)*.
  - *Crucial note:* `Fs = 16000` is required to pass the library binary checks. `LP_HZ = 4000.0f` forces true 8kHz physical anti-aliasing!
- [✔] Inside the `StartTaskAudio` `for(;;)` loop: Call `xStreamBufferReceive()` to sleep until 128 bytes (one half-buffer) arrive from the ISR.
- [✔] Execute `__builtin_bswap16` to correct STM32 Endianness, run `PDM_Filter_64_LSB`, and implement a **2:1 Software Decimation Loop** (`pcm[i*2]`). This pitch-drops hardware 32kHz native into pristine 16kHz for the AI.
- [✔] Pass exactly 8 pristine decimated samples through the Dynamic Silence Gate (`±30 bounds`) into the rolling buffer.

### 1a.3 — MFCC Feature Extraction

**A. CMSIS-DSP Manual Library Integration (`libarm_cortexM4lf_math.a`)**
- [✔] In CubeIDE, try adding the CMSIS-DSP software pack via Help → Manage Embedded Software Packages. If it fails to copy the library, use the manual way below.
- [✔] Find `libarm_cortexM4lf_math.a` inside STM32Cube Repository under `STM32Cube_FW_F4_V1.28.x\Drivers\CMSIS\DSP\Lib\GCC\` (or inside any installed Pack's `Middlewares\Third_Party\ARM\DSP\Lib\`). Copy it to `Core/Lib`.
- [✔] Find all CMSIS-DSP headers inside `STM32Cube_FW_F4_V1.28.x\Drivers\CMSIS\DSP\Include\`. Copy the entire `Include` folder contents into a new `Core/DSP_Inc` folder in your project.
- [✔] Link the library: Project → Properties → C/C++ Build → Settings → MCU GCC Linker → Libraries (-l) → add `arm_cortexM4lf_math` *(strip `lib` prefix and `.a` suffix — same rule as before)*.
- [✔] Add `Core/DSP_Inc` path to the include path in **MCU GCC Compiler → Include paths**, NOT the linker. *(Common mistake: adding the path in the Linker settings instead of the Compiler include paths, which has no effect on header discovery)*
- [✔] Add `#define ARM_MATH_CM4` in `USER CODE BEGIN PD` and `#include "arm_math.h"` in `USER CODE BEGIN Includes` in `freertos.c`. Confirm it compiles with 0 errors.

**B. MFCC Feature Extraction Implementation**
- [✔] Create `mfcc.h` and `mfcc.c` to perform the 4-step pipeline:
  - **1. Windowing:** Overlap frames to prevent missing data (stride), then apply Hanning curve to smooth edges.
  - **2. FFT:** Convert the time-domain waveform into raw frequency magnitudes using `arm_rfft_fast_f32`.
  - **3. Mel Filterbank:** Group raw frequencies into 10 buckets weighted by how human ears perceive pitch.
  - **4. Log Compression:** Apply `10.0f * log10f()` to flatten massive volume differences (matching Python `librosa` mathematically exactly).
- [✔] Implement a Rolling Buffer (Stride Math) in the Audio Task to continually feed 512-sample frames (`MFCC_FRAME_LEN=512`) to the MFCC.
  - **CRITICAL:** FFT work buffers inside `MFCC_Compute` must be declared `static` to live in global BSS memory.
  - **MicGain:** `PDM_Filter_64_LSB` gain argument is currently `1`. The Dynamic Signal Gate completely filters out static DC hum before the feature space sees it.
- [✔] Feature queue: simplified — the MFCC runs in-task on the rolling buffer rather than a separate feature queue for Phase 1. Feature queues will be added in Phase 2 when the inference task is decoupled.
- [✔] Dump verified with ASCII VU meter: raw PCM loudness meter shows clear voice vs silence discrimination.

### ✅ Phase 1a Acceptance
- [✔] **Raw PDM captured via DMA, verified on PC**
- [✔] **PDM→PCM decimation verified (clean waveform, VU meter responds to voice)**
- [✔] **MFCC feature pipeline verified (compiles, runs, DC-corrected, no stack overflow)**

---

## Phase 1b — First AI Deployment (Pretrained Model)
Goal: Use a pretrained TF Lite Micro model (Google Speech Commands) to implement a "Wake Word -> Action" state machine.

### 1b.1 — Model Integration
- [ ] Obtain a pretrained `.tflite` model trained on the Google Speech Commands dataset (which includes 'up', 'on', 'off', etc.).
- [ ] Import the model into the STM32 project. We will use either standard TFLite Micro source files OR ST's X-CUBE-AI (which efficiently compiles the `.tflite` directly into optimized C code).

### 1b.2 — Inference Task
- [x] Gather 49 sequential MFCC frames in the Audio Task (equivalent to exactly 1 second of audio).
- [x] Pass the 49x10 2D array to the AI Inference Task.
- [x] Run the model and read the `softmax` probability array to get the top prediction.

### 1b.3 — Voice State Machine
- [ ] **State 1 (Sleep):** Continuously listen for the Wake Word (`"up"`).
- [ ] **Wake Event:** When `"up"` logic > threshold, turn on indicator LEDs, and switch state.
- [ ] **State 2 (Awake):** Listen for Action Words (`"on"`) with a 10-second timeout.
- [ ] **Action Event:** When `"on"` > threshold, fire all 4 LEDs for 2 seconds, then reset to State 1.
- [ ] **Timeout Event:** If 10 seconds pass without a recognized word, turn off indicator LEDs and reset to State 1.
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
- [x] Collect and prepare dataset (Google Speech Commands + your own voice recordings)
- [x] Design and train command model (DS-CNN, Keras)
- [x] Convert to `.tflite` with INT8 quantization
- [x] Deploy via STM32Cube AI Studio → C files → CubeIDE
- [x] Verify parity mapping: Python `1e-8f` dataset generation meticulously mapped against MCU BSS arrays!

### 2a.2 — Wake-Word Model
- [x] Record wake-word samples (few hundred utterances, varied distance/angle)
- [x] Augment with background noise
- [x] Train tiny wake-word model (3 classes: silence / unknown / wake-word)
- [x] Convert to `.tflite` with INT8 quantization
- [x] Deploy via STM32Cube AI Studio → C files → CubeIDE

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
