# Voice Command Assistant — STM32 TinyML Keyword Spotting (KWS)

A fully local, real-time Voice Command Assistant running on the **STM32F407G-DISC1** board. The system captures audio via an onboard MEMS microphone, extracts MFCC features in real-time under FreeRTOS, and classifies keywords locally using an INT8-quantized Convolutional Neural Network (TinyML) with **0% cloud dependency**.

---

## Key Features & Highlights

- **Embedded Real-Time DSP Pipeline**: Converts raw PDM microphone data to 16 kHz PCM using ST's PDM Filter library, downsamples 2:1 to 8 kHz PCM, applies a 256-point CMSIS-DSP FFT, computes 10 Mel-filterbank energies, and performs Z-score normalization per frame.
- **Decoupled Dual-Task FreeRTOS Architecture**:
  - **Audio Task** (*Above Normal Priority*): Non-blocking real-time audio acquisition, decimation, and MFCC computation at 50 FPS (20 ms stride).
  - **AI Task** (*Below Normal Priority*): Asynchronous VAD detection, spectrogram matrix buffering (49×10 INT8 input tensor), neural network inference via STEDGEAI / X-CUBE-AI, and diagnostic LED control.
- **Adaptive VAD (Voice Activity Detection)**:
  - Exponential Moving Average (EMA) ambient room noise tracking.
  - 10-frame quiet end-of-word detection for natural multi-syllable phrase capture without hard lockout penalties.
- **Python-to-C Mathematical Parity**: Custom Python simulation pipeline (`run_6b_mcu_sim_test.py`) that bit-exactly mirrors C-side CMSIS-DSP math, eliminating the gap between desktop model evaluation and microcontroller execution.
- **Automated Local ML Pipeline (`training/`)**:
  - Multi-voice synthetic keyword generation using Azure Neural TTS (16 diverse voice actors).
  - Isolated dataset partitioning (80/20 train/test split).
  - Custom Keras CNN trained directly on HTK Magnitude MFCC features.
  - TFLite INT8 full integer quantization with representative dataset calibration.
  - One-click C-array auto-deployment to STM32 firmware.
- **Thread-Safe DMA UART Logging**: Non-blocking `printl()` logging framework using a FreeRTOS Queue and UART DMA.
- **Visual Hardware Diagnostics**: Onboard Discovery LEDs (`PD12` Green for `wake_up`, `PD13` Orange for `turn_on`, `PD15` Blue for listening/idle).

---

## Hardware & System Specs

| Component | Specification |
| :--- | :--- |
| **Target MCU** | STM32F407VGT6 (ARM Cortex-M4 @ 168 MHz, FPU, 192 KB RAM, 1 MB Flash) |
| **Microphone** | Onboard MP45DT02 MEMS Microphone (PDM over I2S2 DMA) |
| **Sampling Rate** | 64 kHz PDM → 32 kHz PCM → 16 kHz PCM → 8 kHz PCM (Decimated) |
| **FFT Window** | 256 samples (32 ms @ 8 kHz) |
| **Hop Length** | 160 samples (20 ms stride / 50 FPS) |
| **Spectrogram Input** | 49 frames × 10 Mel Bands (1-second temporal context window) |
| **Inference Engine** | STEDGEAI / X-CUBE-AI (INT8 Quantized CNN model) |
| **Target Classes** | 4 Classes: `{noise, turn_on, unknown, wake_up}` |
| **RTOS Engine** | FreeRTOS (CMSIS-RTOS v2 API) |

---

## Architecture Overview

```
 [MEMS Mic] ---> (I2S DMA) ---> [Audio Task (AboveNormal)]
                                       |
                                       +--> PDM Decimation & 2:1 Downsample (8kHz)
                                       +--> CMSIS-DSP FFT (256-point)
                                       +--> Mel Filterbank (10 Bands)
                                       |
                                (MfccQueue & VolQueue)
                                       |
                                       v
                                [AI Task (BelowNormal)]
                                       |
                                       +--> Adaptive EMA Noise Floor & VAD
                                       +--> 49-Frame History Spectrogram
                                       +--> Z-Score Normalization
                                       +--> INT8 Quantization
                                       +--> STEDGEAI Model Execution
                                       +--> Command Output / Diagnostic LEDs
```

---

## Firmware Code Structure (`Voice_Command_Assistant_STM32/`)

- `Core/Src/freertos.c`: Main RTOS task entry points, real-time queues, audio loop, VAD state machine, and inference invocation.
- `Core/Src/mfcc.c` & `Core/Inc/mfcc.h`: CMSIS-DSP accelerated feature extraction (Hanning windowing, FFT, Mel filterbank).
- `Core/Src/network.c` & `network_data.c`: STEDGEAI runtime model execution and INT8 weight tables.
- `Core/Src/logger.c`: Thread-safe, non-blocking DMA UART formatting wrapper (`printl`).

---

## Machine Learning Pipeline (`training/`)

The model training and deployment workflow is organized into a modular 8-step script pipeline:

```
training/
├── run_1_install_dependencies.py      # Environment setup (librosa, tensorflow, sounddevice, etc.)
├── run_2_record_samples.py            # User audio recording CLI for real-voice dataset capture
├── run_3_generate_synthetic_keywords.py # Multi-speaker Azure TTS dataset generator (16 voices)
├── run_4_verify_dataset.py            # Dataset validator & 80/20 train/test split isolator
├── run_5_train.py                     # Keras CNN model training (HTK/Magnitude MFCC + Z-score)
├── run_6_mic_test.py                  # Live PC microphone inference & testing CLI
├── run_6b_mcu_sim_test.py             # Bit-exact MCU DSP pipeline simulator for PC testing
├── run_7_convert_tflite.py            # TFLite INT8 quantization & calibration script
└── run_8_deploy_to_stm32.py           # Automated model export & C array bridge to STM32 project
```

---

## Key Technical Problems Solved

1. **Audio Interrupt / Inference Lockup**:
   - *Issue*: Running heavy synchronous ML inference (~100 ms) inside the audio callback caused I2S DMA overrun and dropped audio frames.
   - *Fix*: Decoupled the pipeline into an `AboveNormal` Audio Task (pure feature extraction) and a `BelowNormal` AI Task (async inference triggered via FreeRTOS Queues).
2. **PDM Endianness Standing Wave Distortion**:
   - *Issue*: Raw I2S byte order mismatches with the ST PDM filter library caused standing wave artifacts.
   - *Fix*: Applied `__builtin_bswap16` byte-swapping with `PDM_Filter_64_LSB` to align endianness.
3. **VAD Race Conditions**:
   - *Issue*: Share peak volume variables were cleared by logger prints before VAD could inspect them.
   - *Fix*: Separated volume tracking into dedicated atomic queues (`VolQueueHandle` & `MfccQueueHandle`) synchronized 1:1 per frame.
4. **Z-Score Numerical Instability**:
   - *Issue*: Single-pass variance calculations suffered catastrophic floating-point cancellation in single-precision floats on MCU.
   - *Fix*: Implemented a two-pass variance calculation algorithm in C to ensure numerical stability.
5. **Feature Extraction Parity (Python vs MCU)**:
   - *Issue*: Python default `librosa` settings used Slaney-normalized Power spectrums while MCU used HTK Magnitude spectrums, creating feature space shifts.
   - *Fix*: Aligned both pipelines onto matching HTK Mel filterbanks and Magnitude spectrums.

---

## Current Status & Roadmaps

- [x] Hardware audio bring-up (PDM decimation, DMA, FreeRTOS queues).
- [x] CMSIS-DSP accelerated MFCC pipeline with Z-Score normalization.
- [x] Adaptive EMA VAD with multi-frame phrase capture.
- [x] Local Keras CNN training & STEDGEAI INT8 model deployment.
- [x] Numerical and functional parity between MCU C math and Python simulation.
- [ ] **Next Steps**:
  - Expand training dataset with MEMS microphone real-world voice clips across diverse room acoustic environments.
  - Extend keyword capture window from 1.0s to 1.5s/2.0s for multi-syllable phrases.
  - Optimize model decision boundaries for high background noise conditions.

---

## License

This project is licensed under the MIT License - see the `LICENSE` file for details.
