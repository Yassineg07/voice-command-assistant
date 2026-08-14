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
- [✔] Execute `__builtin_bswap16` to correct STM32 Endianness, run `PDM_Filter_64_LSB`, and implement a **2:1 Software Decimation Loop** (`pcm[i*2]`). This pitch-drops the hardware 32kHz native stream into pristine **8kHz** for the AI pipeline.
- [✔] Accumulate exactly 8 pristine decimated samples per ISR callback into the rolling buffer. Track absolute peak (`abs(s)`) in `print_peak` (UART logger, resets every 8ms) and `frame_peak` (VAD, resets every 20ms/160 samples) — **these must be two separate variables** to prevent the logger from erasing peak data before the VAD reads it.

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
- [✔] Implement a Rolling Buffer (Stride Math) in the Audio Task to continually accumulate 160-sample hops (`hop_length=160`) into a 256-sample window (`MFCC_FRAME_LEN=256`, `N_FFT=256`).
  - **CRITICAL:** FFT work buffers inside `MFCC_Compute` must be declared `static` to live in global BSS memory — declaring them on the stack causes stack overflow.
  - **MicGain:** `PDM_Filter_64_LSB` gain argument is `1`.
  - **Mel Filterbank config:** `sample_rate=8000`, `num_filters=10`, `fmin=0`, `fmax=4000` (Nyquist of 8kHz stream).
  - **Log compression:** `10.0f * log10f()` — matches Python `librosa.power_to_db` exactly.
- [✔] Audio Task pushes one completed 10-float MFCC frame (40 bytes) to `MfccQueueHandle` every 160 samples (every 20ms). AI Task is entirely separate — see 1b.3 for rationale.
- [✔] Dump verified with ASCII VU meter: raw PCM loudness meter shows clear voice vs silence discrimination.

### ✅ Phase 1a Acceptance
- [✔] **Raw PDM captured via DMA, verified on PC**
- [✔] **PDM→PCM decimation verified (clean waveform, VU meter responds to voice)**
- [✔] **MFCC feature pipeline verified (compiles, runs, DC-corrected, no stack overflow)**

---

## Phase 1b — First AI Deployment (Custom Trained Model)

> **Goal:** Deploy your own trained model, build a stable, event-driven inference pipeline, and harden it against real-world edge cases.

### 1b.1 — Model Deployment via STM32Cube AI Studio
- [✔] Trained custom 3-class model (`up`, `go`, `noise`) in Python using Google Speech Commands + custom mic recordings.
- [✔] Exported `.h5` model from Keras and deployed through STM32Cube AI Studio (STEDGEAI), generating optimized C files.
- [✔] Integrated generated STAI C files into CubeIDE. Confirmed `stai_network_init`, `stai_network_run` resolve at link time.
- [✔] Declared STAI buffers (`ai_input_buf`, `ai_output_buf`, `ai_activations`, `ai_network_ctx`) in `USER CODE BEGIN Variables`. Network constants come from generated headers.
- [✔] Created `myTaskAI` in `.ioc` (priority = Below Normal, stack = 512 words).

### 1b.2 — Dedicated AI Task Architecture (`freertos.c`)
- [✔] Created **`myTaskAI`** as a completely separate FreeRTOS task (priority = Below Normal, stack = 512 words) in the `.ioc`.
  - **Why a dedicated task?** `stai_network_run` is a heavy synchronous computation (potentially 50–300ms). Running it inside `StartTaskAudio` would stall the Audio Task's real-time DMA loop, causing the stream buffer to overflow and drop PDM packets. A separate task allows the Audio Task to keep streaming at full speed while the AI processes independently.
  - **Why Below Normal priority?** The Audio Task (Above Normal) and Logger Task must never be preempted by inference. The AI is a background consumer — correctness matters more than latency.
- [✔] AI Task idles at 0% CPU blocked on `osMessageQueueGet(MfccQueueHandle, ...)` during silence.
- [✔] On each 20ms wakeup: shifts the 49×10 history matrix left by one row, inserts the new 10-float MFCC frame on the right. This keeps a continuously sliding 1-second acoustic snapshot of audio in real time.
- [✔] AI history matrix pre-filled with `-80.0f` (log-mel silence floor, matching Python's silent padding baseline) at boot and reset to `-80.0f` after every inference.

### 1b.3 — Pipeline Math (`mfcc.h` / `mfcc.c`)
- [✔] `MFCC_FRAME_LEN = 256` — FFT window size, must match Python `N_FFT=256`.
- [✔] `MFCC_NUM_BANDS = 10` — number of Mel filter bands.
- [✔] `hop_length = 160` — rolling buffer stride, must match Python `hop_length=160`.
- [✔] Mel Filterbank: `sample_rate=8000`, `fmax=4000` (Nyquist of the 8kHz decimated stream).
- [✔] Log compression: `10.0f * log10f(power)` — mathematically identical to Python `librosa.power_to_db`.
- [✔] Z-Score normalization applied per-band across the 49 time steps before inference, matching Python training preprocessing.

### 1b.4 — VAD Security Guard Architecture (`freertos.c` → `StartTaskAI`)
- [✔] VAD state machine lives entirely inside the **AI Task** (not Audio Task) so it never blocks the real-time audio stream.
- [✔] Volume per frame (`frame_peak`, an `int16_t`) is pushed into `VolQueueHandle` by the Audio Task alongside each MFCC frame. The AI Task pops both queues in lock-step — volume is always guaranteed to match the exact MFCC slice being evaluated.
- [✔] **Adaptive Gravity EMA tracking:** The VAD tracks ambient room volume continuously during IDLE using `noise_floor = noise_floor * 0.95 + current_vol * 0.05`.
- [✔] VAD states:
  - **IDLE:** Evaluates against `dynamic_threshold = noise_floor + vad_margin(10)`. Physically bounded strictly by `vad_min_floor(20)`. Increments `loud_count` if breached. 3 consecutive breaches → enter CAPTURING.
  - **CAPTURING:** Tracking math freezes. 3 consecutive quiet frames (`< dynamic_threshold`) → concludes word and runs inference.
- [✔] On inference trigger: Z-Score normalize the 49×10 matrix per-band → `stai_network_run` → print `up:XX% go:XX% noise:XX%` → scrub history to `-80.0f` → return to IDLE.

### 1b.4 — Edge Case Hardening (in `freertos.c`)
- [✔] **Race Condition Fixed:** Volume passed through dedicated `VolQueueHandle` (not a volatile global). Volume is always atomically paired to its exact MFCC frame, regardless of inference lag.
- [✔] **Dual Queue Architecture in `.ioc`:** `MfccQueue` (16 × 40 bytes) and `VolQueue` (16 × 2 bytes) both GUI-visible in the FreeRTOS configurator tab — survives all future code regenerations.
- [✔] **Infinite Sentence Overflow Guard & Instant Snap:** `capture_frame_count` in VAD. If CAPTURING exceeds **45 frames (~900ms)**, inference is force-triggered. To survive sudden loud continuous noise, the baseline `noise_floor` is instantly snapped upwards to match the extreme noise level.
- [✔] **Refractory Lockout Penalty:** Upon hitting an overflow snap, the VAD tracking logic is forcefully locked out for 2 full seconds (100 frames), using that time to let the EMA mathematically stabilize onto the new loud baseline before re-arming.
- [✔] **Echo / Contamination Guard:** Full 49×10 history matrix scrubbed to `-80.0f` after every inference. Prevents acoustic tail of previous word bleeding into the next inference window.
- [✔] **Boot-Noise Flush:** 2-second `osDelay` at boot followed by queue flushes prevents the AI from evaluating hardware initialization static.

### 1b.5 — LED State Machine (Diagnostic)
- [✔] **Idle State:** Blue LED ON when listening.
- [✔] **"up" Event:** Green LED ON for 2 seconds (Blue OFF).
- [✔] **"go" Event:** Orange LED ON for 2 seconds (Blue OFF).
- [✔] **Override:** If a new word is detected while a word LED is active, the active LED switches immediately and the 2-second timer restarts.
- [✔] **Timer Mechanics:** Piggybacks on the 20ms AI block loop. No extra RTOS tasks or delays used.

### ✅ Phase 1b Acceptance
- [✔] **Custom 3-class KWS model running on STM32F407 via STAI engine**
- [✔] **Serial prints: `up:XX% go:XX% noise:XX%` on every detected word**
- [✔] **VAD correctly gates: `[VAD] Start...` → `[VAD] End...` → predictions**
- [✔] **Near-zero false positives during continuous silence**
- [✔] **Asynchronous race condition eliminated via dual-queue design**
- [✔] **Infinite-sentence overflow guarded via 900ms capture timeout**
- [✔] **LED State Machine wired and visually reacting to NN predictions**

> ⚠️ **Future Expansion — Long Words (e.g. "Hey Google", "Wake Up"):**
> To support commands longer than ~900ms, changes must begin **at the Python training level**, not just in C:
> 1. In Python: change audio window from 1.0s → 2.0s. Matrix grows from `49×10` → `99×10`.
> 2. Train a new model on the wider matrix. Re-export via STAI to CubeIDE.
> 3. `STAI_NETWORK_IN_1_HEIGHT` updates automatically in generated headers.
> 4. Only then update the C timeout from `capture_frame_count > 45` → `> 90`.
> ⚠️ Changing only the C timeout without retraining causes early audio to fall off the matrix edge before inference — guaranteed mispredictions.

---

## Phase 2a — Train Your Own Models (Python on PC)

> **Goal:** Replace pretrained model with models you trained yourself.

### 2a.1 — Custom KWS Pipeline Setup
- [✔] Standardize Python scripts into portable `run_1` to `run_7` pipeline
- [✔] Integrate Edge-TTS for synthetic voice augmentation (16+ neural voices)
- [✔] Compute a strict 80/20 training/validation split for Synth & Real datasets
- [✔] Train custom 4-class Keras model (`noise`, `unknown`, `wake_up`, `turn_on`)
- [✔] Validate model on Live Microphone Script (STM32 DSP clones)
- [✔] Convert to fully-quantized INT8 `ei_model.tflite`
- [✔] Deploy via STM32Cube AI Studio → C files → CubeIDE
- [x] Automate code-gen deployment directly to MCU (`run_8_deploy_to_stm32.py`)
- [ ] Verify parity mapping against MCU BSS arrays

### 2a.2 — Wake-Word Model
- [ ] Record wake-word samples (few hundred utterances, varied distance/angle)
- [ ] Augment with background noise
- [ ] Train tiny wake-word model (3 classes: silence / unknown / wake-word)
- [ ] Convert to `.tflite` with INT8 quantization
- [ ] Deploy via STM32Cube AI Studio → C files → CubeIDE

---

## Phase 2b — Intelligence Layer (on MCU)

> **Goal:** Full wake-word + command cascade with a proper state machine.

### 2b.1 — Orchestrator Task & Voice State Machine
- [ ] Create the Orchestrator Task — sole owner of system state
- [ ] Implement advanced state machine: `LISTENING (Sleep) → AWAKE → ACTING → LISTENING`
- [ ] **Sleep Mode:** Continuously listen for Wake Word. On detect → turn on indicator LEDs → switch to AWAKE.
- [ ] **Awake Mode:** Listen for Command Word with a 10-second timeout.
- [ ] **Action/Timeout:** If command detected → fire action LEDs for 2s → reset to Sleep. If 10s timeout → reset to Sleep.
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
