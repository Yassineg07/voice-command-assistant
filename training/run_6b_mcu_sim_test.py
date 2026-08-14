"""
run_6b_mcu_sim_test.py  —  MCU Pipeline Simulator
===================================================
This script is a pixel-perfect Python clone of what the STM32 does.
It intentionally does NOT use librosa's melspectrogram. Instead it re-
implements the exact same steps as mfcc.c:
  1. Per-frame DC removal (mean subtraction)
  2. Symmetric Hanning window (N-1 denominator, matching mfcc.c exactly)
  3. FFT (via numpy, same size as ARM CMSIS FFT)
  4. Magnitude spectrogram (mag, not power — matching arm_cmplx_mag_f32)
     NOTE: makes no difference after Z-score, but we match C exactly.
  5. HTK mel filterbank with NO area normalization (matching mfcc.c)
  6. 10 * log10(energy + 1e-8) — matching mfcc.c
  7. Rolling 49-frame history, same as freertos.c
  8. top_db=80 dynamic clipping
  9. Per-band Z-score normalization
 10. INT8 quantization using scale/zero_point from the TFLite model itself
 11. TFLite INT8 inference
 12. De-quantize outputs and print predictions

PURPOSE: If accuracy here matches librosa (run_6), the MCU C code is
correct. If accuracy here is poor, the C mfcc.c has a math mismatch.
"""

import os
import sys
import numpy as np
import sounddevice as sd
from scipy.fft import rfft

# ─── Constants (must match mfcc.h and freertos.c exactly) ────────────────────
SAMPLE_RATE    = 8000
FRAME_LEN      = 256       # MFCC_FRAME_LEN = MFCC_FFT_LEN
HOP_LEN        = 160       # 20ms hop
N_MELS         = 10        # MFCC_NUM_BANDS
FMIN           = 20.0
FMAX           = 4000.0
HISTORY_FRAMES = 49        # STAI_NETWORK_IN_1_HEIGHT
LABELS         = ["noise", "turn_on", "unknown", "wake_up"]
VAD_LOUD_THRESH = 500      # int16 amplitude — tune to match your MCU
VAD_LOUD_COUNT  = 3        # frames to confirm onset
VAD_CAPTURE_FRAMES = HISTORY_FRAMES  # fire exactly at 49 frames

MODEL_PATH = "custom_model.tflite"

# ─── Build HTK mel filterbank (matches mfcc.c exactly) ───────────────────────
def hz_to_mel_htk(hz):
    return 2595.0 * np.log10(1.0 + hz / 700.0)

def mel_to_hz_htk(mel):
    return 700.0 * (10.0 ** (mel / 2595.0) - 1.0)

def build_mel_filterbank():
    num_bins = FRAME_LEN // 2   # 128 bins
    mel_min  = hz_to_mel_htk(FMIN)
    mel_max  = hz_to_mel_htk(FMAX)
    mel_pts  = np.linspace(mel_min, mel_max, N_MELS + 2)
    hz_pts   = mel_to_hz_htk(mel_pts)
    bin_pts  = (hz_pts / (SAMPLE_RATE / 2.0) * num_bins).astype(int)

    filters = np.zeros((N_MELS, num_bins), dtype=np.float32)
    for b in range(N_MELS):
        start  = bin_pts[b]
        center = bin_pts[b + 1]
        end    = bin_pts[b + 2]
        for i in range(num_bins):
            if start <= i <= center and center != start:
                filters[b, i] = (i - start) / (center - start)
            elif center < i <= end and end != center:
                filters[b, i] = (end - i) / (end - center)
    return filters

# ─── Build Hanning window (matches mfcc.c: denominator = N-1) ────────────────
def build_hanning():
    n = np.arange(FRAME_LEN)
    return (0.5 - 0.5 * np.cos(2.0 * np.pi * n / (FRAME_LEN - 1))).astype(np.float32)

MEL_FILTERS = build_mel_filterbank()
HANNING     = build_hanning()

# ─── MFCC computation (exact C clone) ────────────────────────────────────────
def mcu_mfcc(frame_int16: np.ndarray) -> np.ndarray:
    """Compute one MFCC frame from 256 int16_t samples, exactly as mfcc.c."""
    # DC removal per-frame (mean subtraction)
    f = frame_int16.astype(np.float32)
    f -= f.mean()

    # Hanning window
    f *= HANNING

    # FFT -> magnitude spectrum (matches arm_cmplx_mag_f32)
    spec = np.abs(rfft(f, n=FRAME_LEN))[:FRAME_LEN // 2].astype(np.float32)

    # Mel filterbank dot product + log
    energies = MEL_FILTERS @ spec
    log_mel  = 10.0 * np.log10(energies + 1e-8)
    return log_mel

# ─── Load TFLite model + extract quant params ────────────────────────────────
def load_tflite(path):
    try:
        import tflite_runtime.interpreter as tflite
        interp = tflite.Interpreter(model_path=path)
    except ImportError:
        import tensorflow as tf
        interp = tf.lite.Interpreter(model_path=path)
    interp.allocate_tensors()
    inp  = interp.get_input_details()[0]
    out  = interp.get_output_details()[0]
    return interp, inp, out

def run_inference(interp, inp_detail, out_detail, feature_matrix: np.ndarray):
    """Quantize float features → INT8, run network, de-quantize outputs."""
    in_scale  = inp_detail['quantization'][0]
    in_zp     = inp_detail['quantization'][1]
    out_scale = out_detail['quantization'][0]
    out_zp    = out_detail['quantization'][1]

    # Z-score per band (axis=0 = time axis, same as Python training)
    mean = feature_matrix.mean(axis=0)
    std  = feature_matrix.std(axis=0) + 1e-8
    normed = (feature_matrix - mean) / std  # shape (49, 10)

    # INT8 quantization
    q = np.round(normed / in_scale).astype(np.int32) + in_zp
    q = np.clip(q, -128, 127).astype(np.int8)

    # STAI input tensor shape: (1, 49, 10, 1)
    tensor = q[np.newaxis, ..., np.newaxis]
    interp.set_tensor(inp_detail['index'], tensor)
    interp.invoke()
    raw_out = interp.get_tensor(out_detail['index'])[0]  # shape (4,)

    # De-quantize
    probs = (raw_out.astype(np.float32) - out_zp) * out_scale
    return probs

# ─── Main streaming loop ──────────────────────────────────────────────────────
def main():
    if not os.path.exists(MODEL_PATH):
        print(f"ERROR: {MODEL_PATH} not found. Run run_7_convert_tflite.py first.")
        sys.exit(1)

    print("Loading TFLite model...")
    interp, inp_detail, out_detail = load_tflite(MODEL_PATH)
    print(f"  Input  dtype={inp_detail['dtype'].__name__}  quant={inp_detail['quantization']}")
    print(f"  Output dtype={out_detail['dtype'].__name__}  quant={out_detail['quantization']}")

    print("\n" + "=" * 60)
    print("  MCU PIPELINE SIMULATOR  (custom HTK mel, INT8 TFLite)")
    print("  This mirrors STM32 mfcc.c + freertos.c EXACTLY")
    print("=" * 60)
    print("\nWaiting for speech... (Ctrl+C to stop)\n")

    # Rolling 256-sample int16 buffer (simulates rolling_buffer in freertos.c)
    rolling_buf = np.zeros(FRAME_LEN, dtype=np.int16)
    samples_collected = 0

    # 49-frame mel history  (simulates raw_mel_history in freertos.c)
    history = np.full((HISTORY_FRAMES, N_MELS), -80.0, dtype=np.float32)

    # VAD state
    vad_state       = "IDLE"
    loud_count      = 0
    capture_count   = 0
    warmup_frames   = 0
    WARMUP_NEEDED   = HISTORY_FRAMES  # wait until history is filled with real audio

    def audio_callback(indata, frames, time_info, status):
        nonlocal rolling_buf, samples_collected, history
        nonlocal vad_state, loud_count, capture_count, warmup_frames

        # Convert float32 PC audio → int16 range (simulate MEMS mic range)
        pcm = (indata[:, 0] * 32767.0).astype(np.int16)

        for sample in pcm:
            # Feed into rolling buffer one sample at a time
            rolling_buf = np.roll(rolling_buf, -1)
            rolling_buf[-1] = sample
            samples_collected += 1

            if samples_collected < HOP_LEN:
                continue
            samples_collected = 0

            # Compute MFCC frame
            frame_log_mel = mcu_mfcc(rolling_buf.copy())

            # Shift history left, insert new frame at right
            history = np.roll(history, -1, axis=0)
            history[-1] = frame_log_mel

            # Warmup guard
            warmup_frames += 1
            if warmup_frames < WARMUP_NEEDED:
                return

            # VAD peak (abs max in this frame approximation)
            peak = int(np.max(np.abs(rolling_buf)))

            if vad_state == "IDLE":
                if peak >= VAD_LOUD_THRESH:
                    loud_count += 1
                    if loud_count >= VAD_LOUD_COUNT:
                        vad_state = "CAPTURING"
                        capture_count = 0
                        loud_count = 0
                        print("\r\n[VAD] Start of word detected...")
                else:
                    loud_count = 0

            elif vad_state == "CAPTURING":
                capture_count += 1
                if peak > 30:
                    fmts = " ".join([str(int(x*100)) for x in frame_log_mel])
                    print(f"mfcc: {fmts}")

                if capture_count >= VAD_CAPTURE_FRAMES:
                    vad_state = "IDLE"
                    loud_count = 0
                    capture_count = 0

                    # ── top_db clipping (matches freertos.c) ──
                    h = history.copy()
                    top = h.max()
                    h = np.clip(h, top - 80.0, None)

                    # ── Inference ──
                    probs = run_inference(interp, inp_detail, out_detail, h)
                    pcts  = (probs * 100).astype(int)

                    top_idx   = int(np.argmax(probs))
                    top_score = int(pcts[top_idx])
                    top_label = LABELS[top_idx]

                    line = f"noise:{pcts[0]:3d}% | turn_on:{pcts[1]:3d}% | unknown:{pcts[2]:3d}% | wake_up:{pcts[3]:3d}%"
                    if top_score < 75 or top_label in ("noise", "unknown"):
                        print(f"{line}  ->  [REJECTED: {top_label.upper()} {top_score}%]")
                    else:
                        print(f"{line}  ->  [{top_label.upper()}]  ✓")

    try:
        with sd.InputStream(
            samplerate=SAMPLE_RATE,
            channels=1,
            dtype="float32",
            blocksize=HOP_LEN,
            callback=audio_callback,
        ):
            print(f"Listening (VAD threshold: {VAD_LOUD_THRESH} / 32767)...")
            while True:
                sd.sleep(1000)
    except KeyboardInterrupt:
        print("\nStopped.")

if __name__ == "__main__":
    main()
