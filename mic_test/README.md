# Dynamic Silence Gate (Acoustic Hysteresis Architecture)

This document mathematically and logically details the operation of the Dynamic Silence Gate implemented inside this project's microphone pipeline loop. 

## The Acoustic Problem
When designing neural network pipelines (like MFCC generators) for extreme low-power MCUs, raw unfiltered PCM data is highly destructive.
MEMS microphones have a natural DC electrical hum ("silicone static"), often jumping wildly between random low integers (e.g., `-30` to `+30`).
If this static goes straight into an MFCC normaliser, the normaliser mathematically expands this tiny hum into massive spectral energies. This generates "hallucinations" in the neural network which mathematically mimic short phonetics like "UP" or "P/B/K" plosives.

## The Engineering Solution: Envelope-Based Hysteresis
Instead of looking at the PCM electrical zero-crossings (16,000 times a second), the gate mathematically tracks the 8-millisecond **Volume Envelope** of the room. By relying on envelope stability rather than waveform frequency, it creates a studio-grade professional Noise Gate perfectly calibrated to the room's noise floor in real-time.

---

### Phase 1: The Stability Tracker (Finding the Floor)
The system constantly tracks the absolute peak volume of every 8ms block from the microphone.
* If the peaks between consecutive blocks drift by less than a strict tolerance (`OBSERVE_TOLERANCE = 5`), the room is considered "stable".
* If the room remains flawlessly stable for `50 consecutive blocks` (400ms), the gate immediately locks onto the loudest hum observed in that window and saves it as the `silence_bar`.
* **Result:** The gate mathematically discovers the physical structure of the noise floor to extreme accuracy, and immediately begins zeroing out anything underneath it.

### Phase 2: Acoustic Hysteresis (Detecting Speech)
If DC noise micro-spikes (e.g., jumps from 9 to 14), a standard gate will falsely open, letting in a pop of static to the AI.
* To open *this* gate, the incoming audio envelope must breach structurally above the `silence_bar` and **sustain that breach for 3 consecutive blocks (24ms)**. 
* A random micro-spike only lasts 1 block, so it fails the sustain check. The gate ignores it, clamping it to 0.
* A human voice starts with phenomenal, sustained acoustic power, effortlessly triggering the 24ms sustain-lock. 
* **Result:** The gate is completely immune to electrical pops, but maintains a flawlessly sharp 24ms vocal attack time!
* **Bypass:** Once opened, the gate is disabled. The raw, 100%-untouched 16-bit PCM waveform passes straight through to the neural network identically as the microphone heard it.

### Phase 3: The Fallback Timeout (Closing the Gate)
When human speech concludes, the room returns to static hum. However, the static hum might be too erratic (jumping wildly between 0 and 20) to satisfy the strict `±5` tolerance required by Phase 1. If it can't find Phase 1 stability, how does the gate close?
* The fallback monitor constantly checks if the incoming audio drops perfectly below the *last known* `silence_bar`.
* If the audio stays entirely submerged beneath the old bar for 400ms, the gate assumes the speech event is over.
* **Result:** It mechanically snaps shut, resuming the zeroing out of the noise floor, even if the hum is actively too erratic to trigger a new floor calculation.

---

## Configuration Variables
These Macros are heavily documented at the top of the `mic_test` `main.c` pipeline if you ever need to adjust the gate's feel.

| Macro | Default | Function |
| :--- | :---: | :--- |
| `SILENCE_BAR_INIT` | `0` | Base boot state. If `0`, fully auto-detects first floor. If `>0`, pre-seeds the floor (e.g. `30`). |
| `OBSERVE_CYCLE_BLOCKS`| `50` | How many 8ms blocks are required to confirm stability (50 = 400ms). |
| `OBSERVE_TOLERANCE` | `5` | Maximum value deviation permitted between blocks to remain "stable". |

This strict, block-based noise gate flawlessly eliminates Neural Network hallucinations caused by silence.
