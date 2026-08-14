# AGENTS.md — Voice Command Assistant

## Project structure

```
Voice_Command_Assistant_STM32/   # CubeIDE firmware (STM32F407G-DISC1)
  Core/Src/freertos.c            # All FreeRTOS tasks + ISR callbacks
  Core/Src/mfcc.c                # MFCC feature extraction (CMSIS-DSP)
  Core/Src/logger.c              # printl() — DMA UART logging wrapper
  Core/Src/network.c + .h        # STM32Cube AI Studio generated inference
  Core/Src/network_data.c/.h     # Model weights + activation buffers
  Core/Lib/                      # Third-party .a libs (PDMFilter, CMSIS-DSP)
training/                        # Python ML pipeline (run_1 ... run_8)
mic_test/                        # Standalone mic bring-up test project
```

## Tasks (FreeRTOS, from freertos.c)

| Task                 | Priority    | Role                                                                      |
| -------------------- | ----------- | ------------------------------------------------------------------------- |
| `StartTaskAudio`   | AboveNormal | PDM→PCM decimation → MFCC → push to MfccQueue + VolQueue               |
| `StartTaskAI`      | BelowNormal | VAD (adaptive EMA) + inference (`stai_network_run`) + LED state machine |
| `StartTaskLogger`  | Low         | Drain LogQueue → UART via DMA                                            |
| `StartDefaultTask` | Normal      | Starts I2S DMA, then idles                                                |
| `StartTaskLed`     | Low         | Boot LED animation, then idles                                            |

## Queues (CubeMX-registered, survive .ioc regeneration)

- `LogQueue` — 10×128 bytes, `printl()` → Logger
- `MfccQueue` — 16×40 bytes, Audio → AI (10-float MFCC frames)
- `VolQueue` — 16×2 bytes, Audio → AI (int16_t peak, paired 1:1 with MfccQueue)
- `AudioStreamBuf` — Stream Buffer (2048 bytes, trigger 1024), DMA ISR → Audio Task (manually declared in code, no CubeMX UI)

## Key pipeline constants

| Constant                   | Value          | Source                                                   |
| -------------------------- | -------------- | -------------------------------------------------------- |
| FFT window                 | 256 samples    | `mfcc.h:MFCC_FRAME_LEN` (matches Python `N_FFT=256`) |
| Mel bands                  | 10             | `mfcc.h:MFCC_NUM_BANDS`                                |
| Hop length                 | 160 samples    | `StartTaskAudio` rolling buffer stride                 |
| Sample rate (post-decimal) | 8000 Hz        | MFCC config (`sample_rate=8000`, `fmax=4000`)        |
| Raw I2S rate               | 64000 Hz       | `.ioc` → becomes 32kHz PCM before 2:1 decimation      |
| Input tensor               | 49×10×1 INT8 | `network.h:STAI_NETWORK_IN_1_HEIGHT/WIDTH`             |
| Output tensor              | 4 classes INT8 | `network.h:STAI_NETWORK_OUT_1_CHANNEL`                 |
| Activation arena           | 17232 bytes    | `network.h:STAI_NETWORK_ACTIVATIONS_SIZE_BYTES`        |

## Non-obvious gotchas

- **I2S standard must be LSB, not Philips.** Philips adds a 1-bit clock delay that shifts the PDM bit pattern out of phase.
- **CRC must be enabled** in `.ioc` (Computing → CRC) or the PDM filter library hard-faults.
- **Library naming for CubeIDE linker:** strip `lib` prefix and `.a` suffix (e.g. `libPDMFilter_CM4F_GCC.a` → `-lPDMFilter_CM4F_GCC`).
- **`__builtin_bswap16`** required on raw I2S data before passing to `PDM_Filter_64_LSB` (STM32 little-endian vs filter expectations).
- **FFT work buffers in `MFCC_Compute` must be `static`** (live in BSS, not stack). Stack overflow otherwise.
- **Two separate peak trackers** in `StartTaskAudio`: `print_peak` (logger, resets every 8 chunks) and `frame_peak` (VAD, per MFCC frame). Never share one variable.
- **MFCC history pre-filled to -80.0f** (log-mel silence floor) at boot and scrubbed after every inference.
- **2-second boot delay + queue flush** in `StartTaskAI` to discard hardware initialization noise.
- **STAI INT8 dequantization:** `real = (q - zero_point) * scale` — for both input quantization and output dequantization.
- **PDM filter config:** `Fs=16000` (passes library checks), `LP_HZ=4000.0f` (matches 8kHz Nyquist after decimation).

- **VAD Lockout removed:** The 2-second refractory period (`refractory_frames`) and `timeout_triggered` lockout were removed as they caused the neural network to miss words or chop them up by making the VAD "deaf".
- **Filterbank matching is critical:** The C code and Python training code must mathematically mirror each other perfectly. We corrected a mismatch where Python used Slaney-normalized Power (`mag²`) spectrums while C used HTK unnormalized Magnitude (`mag`) spectrums, which caused the MCU model to predict everything as "turn on". We standardized both to use HTK + Magnitude.

## Training pipeline (`training/`)

```powershell
.venv\Scripts\activate
pip install -r requirements.txt
# Then run scripts in order: run_1 → run_2 → ... → run_8
```

Each script is self-contained. `run_8_deploy_to_stm32.py` copies STM32Cube AI Studio output into the firmware project.

## Current project state

Phase 1b complete (custom 4-class KWS model `{noise, unknown, wake_up, turn_on}` deployed, VAD + LED state machine working). Phase 2a: training pipeline complete, wake-word model still needed (CHECKLIST.md 2a.2). Phase 2b+ (Orchestrator, wake-word cascade, OLED) not started.

## Reference docs

- `SPECS.md` — full architecture, task table, phase plans
- `CHECKLIST.md` — detailed progress per phase
- `CHANGELOG.md` — historical bug fixes and decisions
- `AGENT.md` — instructions for *this project's learning mentor role* (do NOT write code for user unless asked)
