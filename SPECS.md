# Voice Command Assistant on STM32F4

### Project Specification & Build Plan

---

## 1. Objective

Build a self-contained, locally-running voice assistant on the STM32F407G-DISC1 board that:

1. Continuously listens at low cost for a wake word.
2. On detection, opens a short command window and recognizes one of a small set of spoken commands.
3. Executes a physical action (LED, and later relay/servo) and gives an audible confirmation tone.
4. Returns to idle listening, either after acting or after a timeout.

**Guiding principle:** this project prioritizes deterministic real-time behavior over maximum recognition accuracy. Nearly every non-obvious design choice below — the single Inference Task, pointer/index-only queues, the overwrite policy on the Feature Queue, no dynamic allocation in the audio path, Stream Buffers for the ISR handoff — is in service of that one principle, not an arbitrary preference.

This is a learning project. The primary goals — in order of importance — are:

- Practical, correct FreeRTOS architecture (tasks, queues, notifications, timers, state ownership)
- A real embedded audio pipeline (DMA, PDM→PCM, MFCC)
- A working TinyML deployment (pretrained model first, custom model second)
- Everything running locally on the MCU — no cloud, no PC-in-the-loop at runtime

**Explicitly out of scope:** real-time text-to-speech synthesis, multi-word continuous speech recognition, noise cancellation / beamforming, wireless connectivity. Pre-recorded phrase/word playback (talkback using recorded clips) is realistic future work once the onboard audio codec is wired up — see Section 2 and Phase 3.

---

## 2. Hardware

|Component|Choice|Notes|
|---|---|---|
|MCU board|STM32F407G-DISC1|Cortex-M4F, hardware FPU — useful for CMSIS-DSP/CMSIS-NN|
|Audio input|**Onboard MP45DT02 PDM MEMS mic**|Already wired to I2S2 on this board. No external wiring needed. Use ST's `BSP_AUDIO_IN_*` driver + PDM2PCM decimation library.|
|Audio output|**Onboard audio jack via CS43L22 codec** (I2S3 + DMA, ST's `BSP_AUDIO_OUT_*` driver)|Real PCM streaming, not just a single tone — needed for any future talkback. Mirrors the mic's `BSP_AUDIO_IN_*` pattern on the output side.|
|Speaker — primary|**Active (powered) desktop speakers**, 3.5mm plug into the board's audio jack|These have their own built-in amp and accept a line-level signal — plug and play, no extra circuitry. Best option for development and desktop use.|
|Speaker — compact alternative|**Passive laptop speaker** + PAM8403 class-D amp module|For a smaller/more self-contained build. The PAM8403 takes line-level audio from the jack and DC power (USB 5V, available on the board) and drives the 4–8Ω passive coil properly. Passive speakers have no electronics inside — the amp is not optional, it's the only correct way to drive them. Wiring: audio in from jack, 5V power in, speaker wires out.|
|Display|0.96" 128×64 I2C OLED (SSD1306)|Shows live state / last recognized word / last action. Put it on its own I2C peripheral (e.g. I2C2), separate from the codec's I2C1 control bus, to avoid bus-sharing complexity.|
|State/action indicators (Phase 1)|Onboard user LEDs (LD3 orange/PD13, LD4 green/PD12, LD5 red/PD14, LD6 blue/PD15)|Zero wiring. Map to system states for early debugging.|
|Action actuators (Phase 2+, optional)|Servo and/or relay|Add once the core pipeline is proven.|
|Debug output|UART over ST-Link VCP, or USB CDC|For the logger task.|

**Mic decision rationale:** the onboard PDM mic removes a wiring unknown and has first-party ST support and example projects for this exact board. The only cost is one extra DSP stage (PDM decimation), which the library already implements. Revisit only if mic placement becomes a real problem later.

---

## 3. Software Stack

### Tools to install on PC

**STM32CubeIDE** — single main IDE for everything firmware-related: writing C code, building, flashing, and debugging. It also has the CubeMX peripheral configurator built in as a tab (the `.ioc` file) — no need to install CubeMX separately. FreeRTOS, CMSIS-DSP, and the ST audio middleware are not separate programs; they are libraries that live inside the CubeIDE firmware project, added and configured through the `.ioc` file.

**STM32Cube AI Studio (STEDGEAI-CUBEAI)** — a separate standalone desktop app, only used at the AI deployment step. It takes a trained model natively, targets the STM32F407, and generates optimized C files that are manually dropped into the CubeIDE project. Not a plugin, not part of CubeIDE — a separate install. _(Do not confuse with: X-CUBE-AI, the old CubeMX plugin it replaces; STEDGEAI, the underlying CLI engine not meant for direct use; or STM32Cube.AI Developer Cloud, the browser-based equivalent for cloud deployments.)_

---

### Libraries inside the CubeIDE firmware project

These are not separate programs — they are code that compiles into the firmware:

- **FreeRTOS (CMSIS-RTOS v2 API)** — the real-time OS. Enabled and configured via the `.ioc` tab in CubeIDE; code generated automatically into the project.
- **ST BSP + PDM2PCM middleware** — ST's board support package for the Discovery board, including the audio drivers (`BSP_AUDIO_IN_*`, `BSP_AUDIO_OUT_*`) and the PDM-to-PCM software decimation filter. Added via the `.ioc` tab or manually from the STM32CubeF4 firmware package.
- **CMSIS-DSP** — ARM's optimized DSP library for Cortex-M. Provides the FFT and filter functions used to build the MFCC feature extractor. Added as a library to the CubeIDE project.
- **STM32Cube AI Studio generated C code** — the output of STM32Cube AI Studio: a set of `.c`/`.h` files containing the model weights and the inference API. These files are copied into the CubeIDE project and called from the Inference Task.

---

### ML work (offline only, never runs on the MCU)

- **Python + TensorFlow/Keras** — dataset preparation, model architecture, training, and evaluation. Runs entirely on the PC.
- **TFLite converter** (part of TensorFlow) — converts the trained Keras model to `.tflite` format and applies post-training INT8 quantization.
- **STM32Cube AI Studio** — takes the `.tflite` output and generates the STM32-optimized C code for the firmware. (Same tool as above, used at two points in the workflow: once for a pretrained model in Phase 1, once for custom trained models in Phase 2.)

---

## 3a. Estimated Memory Budget

To be filled in with real measured values once Phase 1 is running — but plan the categories now:

**Flash**

- Application code
- FreeRTOS
- CMSIS-DSP
- STM32Cube AI Studio generated inference code
- Wake-word model (weights)
- Command model (weights)

**RAM**

- FreeRTOS task stacks
- DMA buffers
- Stream Buffer (audio ISR → Feature Extraction handoff)
- Feature buffers (MFCC matrices)
- AI tensor arena (STM32Cube AI Studio activation buffer)
- Log queue and Display queue

**Target:** no dynamic allocation after system initialization. Heap is reserved only for FreeRTOS object creation at startup (task/queue/timer creation), not for anything in the steady-state audio path.

---

## 3b. Error Handling Philosophy

- Audio capture failure is logged and recovery is attempted without rebooting.
- ML inference failure never crashes the system — a bad/low-confidence prediction is just discarded, not treated as a fault.
- Queue overruns drop data rather than block the real-time pipeline (this is exactly what the Feature Queue's overwrite policy already does).
- Fatal initialization failures (clock, DMA, microphone) may halt startup, signaled with an LED error pattern rather than failing silently.

---

## 4. Final System Architecture

```
                  PDM (I2S2 + DMA)
                         │
                         ▼
               Audio Capture Task
                         │
                  Circular Buffer
                         │
                         ▼
              Feature Extraction Task
            (PDM→PCM decimation, then MFCC)
                         │
                 Feature Queue (single consumer)
                         │
                         ▼
         Inference Task  (single task, model-mode switch:
                           wake_model in LISTENING state,
                           command_model in ACTIVE state)
                         │
            reports WAKE_DETECTED / COMMAND_FOUND / TIMEOUT
                         ▼
            Orchestrator Task (sole owner of system state,
                       software timer for command-window timeout)
                         │
                         ▼
                   Action Task
                         │
              ┌──────────┴──────────┐
              ▼                     ▼
        LED / Relay / Servo     CS43L22 codec (I2S3+DMA) → jack → amp → speaker

  Log Queue     (fed by Inference, Orchestrator, Action) ──▶ Logger Task ──▶ UART
  Display Queue (fed by the same producers)              ──▶ Display Task ──▶ OLED (I2C2)
```

**Rules this architecture follows (lessons from the design discussion):**

- Only **one** task ever consumes the Feature Queue (the Inference Task). No multi-consumer queues, anywhere.
- Only the **Orchestrator** writes system state. Every other task communicates by sending events, never by touching shared state directly. No mutex needed for state.
- The Logger Task is **not** on the audio/feature data path. It has its own dedicated Log Queue that other tasks push small messages into.
- The Display Task follows the same rule as the Logger Task — its **own** Display Queue, not a second consumer hung off the Log Queue. Producers push to both queues when something noteworthy happens; the two tasks never share one queue.
- Wake word and commands share **one** inference engine and **one** MFCC pipeline — the wake-word cascade is a mode switch, not a second pipeline.
- The handoff from the DMA ISR to the Feature Extraction Task (raw PCM data) is single-producer/single-consumer — use FreeRTOS's **Stream Buffer** API (`xStreamBufferSendFromISR` / `xStreamBufferReceive`) for this specific link rather than a regular queue. It's purpose-built for exactly this ISR-to-task byte-stream pattern and avoids hand-rolling a lock-free ring buffer.

---

## 5. FreeRTOS Task Table

|Task|Priority|Trigger|Responsibility|
|---|---|---|---|
|Audio Capture|Highest|DMA half/full-complete interrupt|Move raw PDM bitstream into circular buffer. No processing.|
|Feature Extraction|High|New audio block available|PDM→PCM decimation, windowing, FFT, Mel filterbank, MFCC. Pushes feature vector to Feature Queue.|
|Inference|High|Feature Queue has data|Runs whichever model is currently active (wake or command, per Orchestrator's last notification). Reports result via event.|
|Orchestrator|Medium|Events from Inference Task; software timer|Owns state machine (`BOOT → LISTENING → AWAKE → ACTING → LISTENING`). Starts/stops command-window timer. Tells Inference Task which model to run.|
|Action|Medium|Event from Orchestrator|Drives LED/relay/servo, triggers audio feedback via the CS43L22 codec.|
|Logger|Low|Log Queue has data|Drains log messages, prints over UART. Never touches audio data.|
|Display|Low|Display Queue has data|Drains state/word/action updates, renders to OLED over I2C2. Own queue — never shares the Log Queue.|

---

## 6. Audio & Model Parameters

- Sample rate: **16 kHz** mono (matches both the PDM decimation library's standard rate and the Google Speech Commands dataset — convenient overlap).
- Analysis window: 30 ms, stride 20 ms → a 1-second utterance yields **49 frames**.
- Features: **10 MFCC coefficients per frame** → a 49×10 feature matrix per utterance (standard reference config used in TF Lite Micro's speech examples).
- Models: INT8-quantized after training on PC. Wake-word model: tiny (silence/unknown/wake-word, 3 classes). Command model: small multi-class (5–10 words).
- Initial vocabulary (from Google Speech Commands, for fast bring-up before recording custom data): `yes, no, on, off, left, right, up, down, stop`.

---

## 7. Project Workflow

```
SETUP
Install STM32CubeIDE + STM32Cube AI Studio + Python + TensorFlow
        │
        ▼
PHASE 0 — Environment
Create CubeIDE project for STM32F407G-DISC1
Configure FreeRTOS via .ioc tab
Run LED blink tasks at different priorities → confirm scheduler works
Bring up UART (DMA mode) + FreeRTOS Logger Task + printl wrapper → confirm output is readable
        │
        ▼
PHASE 1a — Audio Pipeline  (on the MCU, in CubeIDE)
Configure I2S2 + DMA for onboard PDM mic via .ioc tab
Audio Capture Task: DMA fills stream buffer
Feature Extraction Task: PDM→PCM → windowing → FFT → MFCC
Verify each stage: dump raw audio, then PCM, then MFCC matrices over UART
        │
        ▼
PHASE 1b — First AI Deployment  (PC → MCU)
[PC] Download a pretrained Google Speech Commands TFLite model
[PC] Open STM32Cube AI Studio → import .tflite → select STM32F407 target → generate C files
[CubeIDE] Add generated C files to project
[MCU] Inference Task runs the model on every MFCC frame
[MCU] Action Task maps recognized words to onboard LEDs
→ System has a working flat voice-controlled LED. End to end. Flat pipeline, no wake word yet.
        │
        ▼
PHASE 2a — Train Custom Models  (Python on PC)
[PC] Collect and prepare dataset (Google Speech Commands + custom recordings)
[PC] Train command model (DS-CNN, Keras) → convert to .tflite → INT8 quantize
[PC] Record wake-word samples (few hundred utterances) → train wake-word model → convert
[PC] STM32Cube AI Studio → generate C files for both models
[CubeIDE] Replace pretrained model with custom model
        │
        ▼
PHASE 2b — Intelligence Layer  (on the MCU, in CubeIDE)
Add Orchestrator Task: state machine LISTENING → AWAKE → ACTING → LISTENING
Add software one-shot timer for command-window timeout
Add mode-switching to Inference Task (wake model vs command model, per Orchestrator)
Add wake-word debouncing (confidence floor + sliding-window majority vote)
Add Display Task (own queue) + OLED bring-up on I2C2
→ System now has full wake-word + command cascade with state machine and live display
        │
        ▼
PHASE 3 — Polish
Bring up CS43L22 codec audio output (I2S3 + DMA) → confirmation tones through jack
Add 500ms refractory period on return to LISTENING (self-feedback guard)
Instrument latency, queue occupancy, stack watermarks, CPU% per task
Optional: relay/servo actions, pre-recorded talkback phrases
```

---

## 8. Phase-by-Phase Build Plan

### Phase 0 — Environment & Bring-Up

**Goal: toolchain works, board boots, Logger Task printing over UART+DMA.**

1. Create a new CubeIDE project for the STM32F407G-DISC1. Enable FreeRTOS (CMSIS-RTOS v2) via the built-in `.ioc` configurator tab.
2. Create 2–3 simple tasks blinking different onboard LEDs at different rates/priorities — confirm scheduler is alive and priorities behave as expected.
3. Bring up UART in DMA mode. Build a dedicated Logger Task with its own FreeRTOS Message Queue (items: 128-byte char arrays, depth: 10) and a Binary Semaphore (`DmaTxSem`) as the DMA-complete signal. The Logger Task blocks on the queue, fires `HAL_UART_Transmit_DMA`, then immediately blocks on the semaphore. `HAL_UART_TxCpltCallback` releases the semaphore from the ISR.
4. Implement `printl()` — a variadic wrapper around `vsprintf` that formats into a stack-local buffer and pushes it to the Log Queue with a short timeout. This is the only print function used anywhere in the firmware. Never call `printf` directly.

_Acceptance criteria:_ LEDs blink independently at correct rates; `printl()` output from each task appears correctly ordered in a serial terminal; UART never blocks the calling task.

---

### Phase 1 — Prove the Pipeline (flat, no wake word, pretrained model)

**Goal: zero ML risk. Prove audio capture, DMA, MFCC, and inference all work before custom training.**

1. **Mic bring-up.** Configure I2S2 + DMA for the onboard PDM mic via `BSP_AUDIO_IN_Init`. Stream raw PDM data into a circular buffer via the Audio Capture Task.
    - _Acceptance:_ dump raw samples over UART (or to a PC plotting script) and visually confirm real audio capture (e.g. clapping produces a visible spike).
2. **PDM→PCM decimation.** Apply ST's PDM2PCM filter to get clean 16 kHz PCM samples.
    - _Acceptance:_ plot the PCM waveform on PC — should look like recognizable speech/audio, not noise.
3. **Feature Extraction Task.** Implement windowing + FFT + Mel filterbank + MFCC using CMSIS-DSP. Push 49×10 feature matrices to the Feature Queue.
    - _Acceptance:_ dump a feature matrix for a known sound and sanity-check it's not all-zero or NaN; compare shape to expectation.
4. **Run a pretrained KWS model.** Use an official TensorFlow Lite Micro keyword-spotting example model (or train a quick one on Speech Commands on the PC) and deploy via STM32Cube AI Studio (import model → generate C code → add to CubeIDE project). Inference Task runs it on every feature matrix, always — no wake word yet, no mode switching.
    - _Acceptance:_ say a recognized word, see the correct label print over UART with a sane confidence value.
5. **Action Task.** Map recognized commands to onboard LEDs (e.g. "on"/"off" toggles LD4).
    - _Acceptance:_ end-to-end working flat voice-controlled LED, on actual hardware.
6. **Use pointer/index-based queues, not buffer copies.** Queue items should be `{buffer_index, length}` or similar, not full audio buffers by value.
7. **Sanity-check the actual sample rate** (oscilloscope, logic analyzer, or a known-frequency test tone) rather than trusting the CubeMX config alone — PLLI2S clock misconfiguration is a common silent bug here.

_Phase 1 complete when:_ mic → MFCC → pretrained model → LED is reliable and every stage is understood.

---

### Phase 2 — Make It Smart (custom model + wake-word cascade + orchestrator)

1. **Train Custom Command Model** on the PC, replacing the pretrained one. Same MFCC pipeline, same deployment path (STM32Cube AI Studio). Quantize to INT8.
    - _Acceptance:_ accuracy comparable to or better than the pretrained model on custom voice.
2. **Add the wake-word model** (tiny DS-CNN, 3 classes: silence/unknown/wake-word). Record custom wake-word samples (a few hundred utterances, varied distance/angle, plus background-noise augmentation).
3. **Add the Orchestrator Task.** State machine: `LISTENING → AWAKE → ACTING → LISTENING`, software timer for the command-window timeout (replace any `HAL_Delay` with `xTimerStart`/timeout callback). The timer must be **one-shot**, started on entering AWAKE and stopped/reset if a command is found before it fires — not auto-reload, which would keep firing repeatedly.
4. **Switch Inference Task to mode-based model selection**, driven by a task notification from the Orchestrator (`0 = wake model, 1 = command model`). No second inference task, no suspend/resume.
    - **Frame-drop policy:** make the Feature→Inference handoff a depth-1 queue and use `xQueueOverwrite()` instead of `xQueueSend()`. If inference ever falls behind feature arrival rate, this always keeps the newest frame and silently discards the stale one — no custom drop logic needed, no blocking, no backlog.
5. **Add debouncing** on wake-word detection: don't trigger on a single confident frame. Use a confidence floor **and** a sliding-window majority vote — e.g. confidence > 0.8 **and** at least 3 of the last 5 frames agree — before declaring a trigger. This tolerates one noisy/dropped frame, unlike a strict consecutive-run requirement.
6. **Add the Logger Task** with its own dedicated Log Queue. Have Inference, Orchestrator, and Action push structured log lines (state, confidence, inference time, recognized command).
7. **Add the Display Task.** Bring up the OLED on its own I2C peripheral (I2C2), separate from the codec's I2C1 control bus. Give it its own Display Queue, fed by the same producers as the Logger — never let it consume the Log Queue. Show three lines: current state, last recognized word, last action taken.

_Acceptance criteria:_ saying the wake word transitions LISTENING→AWAKE (visible via LED color change); a recognized command within the window fires the action; silence or an unrecognized command times out back to LISTENING.

---

### Phase 3 — Production Polish

1. **Audio output.** Bring up the CS43L22 codec via I2S3 + DMA (`BSP_AUDIO_OUT_Init`/`Play`). Add a confirmation tone on wake-word detection and a distinct tone on command recognition, played as short PCM clips through the onboard jack. This same path is what makes pre-recorded talkback phrases realistic later — see Section 1.
    - **Primary speaker option:** plug active desktop speakers directly into the jack — no extra wiring, clean output, best for development.
    - **Compact alternative:** passive laptop speaker + PAM8403 module (5V from board, audio from jack, speaker wires out). Use this for a self-contained build without a powered speaker nearby.
    - **Self-feedback guard:** the mic is always listening, including while audio tones/beeps play. Add a ~500ms refractory period after returning to LISTENING (post-action or post-timeout) during which wake-word detections are ignored, so the system doesn't pick up its own audio output or its reverb as a fresh trigger. Note this is _not_ needed to prevent re-triggering during AWAKE itself — the wake model isn't even running then, since Inference Task has switched to the command model for that state.
2. **Instrumentation.** Track and log: audio-to-feature latency, MFCC compute time, inference time, end-to-end response time, queue occupancy (is any queue chronically near-full — that's your bottleneck), stack high-water marks (`uxTaskGetStackHighWaterMark`), heap usage.
3. **Power/CPU tuning.** Confirm the wake-word path is genuinely cheap to run continuously; profile CPU% per task using FreeRTOS runtime stats.
4. **Optional hardware upgrades:** swap onboard LED actions for a real relay or servo; if mic placement is awkward in an enclosure, swap to an external I2S mic (e.g. INMP441) — should be a localized change since the MFCC/inference pipeline downstream doesn't change.
5. **Optional talkback:** record short phrase clips, store as PCM in flash, play them back through the existing codec path per state/command — no new audio hardware needed, just content and a small playback dispatcher in the Action Task.

---

## 8. Debugging: Diagnostic Build Mode

Add a compile-time flag (e.g. `#define DIAG_MODE 1`) that, when enabled:

- Bypasses the ML model(s) entirely — feeds dummy/passthrough output instead of running inference.
- Prints MFCC compute time and Feature Queue occupancy over UART each cycle.
- Dumps raw confidence scores for every frame, not just triggers.
- Maps each onboard LED to a pipeline stage (e.g. one blinks on audio capture, one on MFCC complete, one on inference complete) so stage execution is visible without a debugger attached.

The point is to be able to isolate "is this an audio problem, a DSP problem, an inference problem, or a state-machine problem" independently, without touching the architecture itself. This pays for itself many times over the first time something doesn't work and the faulty task is unknown.

---

## 9. Milestone Checklist

- [ ] FreeRTOS blinky + UART logging running
- [ ] Raw PDM mic audio captured via DMA, verified on PC
- [ ] PDM→PCM decimation verified
- [ ] MFCC feature matrices verified (correct shape, sane values)
- [ ] Pretrained KWS model running, flat pipeline, LED reacts to voice
- [ ] Pointer-based queues (no buffer copies) confirmed
- [ ] Own trained command model deployed and working
- [ ] Wake-word model trained and deployed
- [ ] Orchestrator state machine + software timer working
- [ ] Single Inference Task with mode switch (no dual-consumer queues anywhere)
- [ ] Debouncing on wake-word trigger
- [ ] Logger Task on its own dedicated queue
- [ ] Display Task on its own dedicated queue, OLED showing state/word/action
- [ ] Diagnostic build mode (ML bypass, timing/queue/confidence dump, per-stage LED mapping)
- [ ] CS43L22 codec audio output wired and working (tones now, talkback-ready later)
- [ ] Latency/CPU/stack instrumentation in place