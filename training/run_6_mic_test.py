import os
import numpy as np
import librosa
import sounddevice as sd
import tensorflow as tf

SAMPLE_RATE = 8000
DURATION = 1.0
SAMPLES_PER_WINDOW = int(SAMPLE_RATE * DURATION)

N_MELS = 10
N_FFT = 256
HOP_LENGTH = 160
FMIN = 20.0
FMAX = 4000.0

LABELS = ["noise", "turn_on", "unknown", "wake_up"]
CONFIDENCE_THRESHOLD = 0.75  # raise/lower to tune sensitivity

def extract_features(audio):
    """Must match run_5_train.py EXACTLY."""
    # 1. Compute Magnitude STFT (NOT Power)
    stft = librosa.stft(y=audio, n_fft=N_FFT, hop_length=HOP_LENGTH, center=False)
    mag = np.abs(stft)
    
    # 2. Build HTK Mel Filterbank (no area normalization)
    mel_basis = librosa.filters.mel(
        sr=SAMPLE_RATE, n_fft=N_FFT, n_mels=N_MELS, 
        fmin=FMIN, fmax=FMAX, htk=True, norm=None
    )
    
    # 3. Apply Filterbank and Log
    energies = np.dot(mel_basis, mag)
    log_S = 10.0 * np.log10(energies + 1e-8)
    
    # 4. Z-Score Normalization
    features = log_S.T
    features = (features - features.mean(axis=0)) / (features.std(axis=0) + 1e-8)
    return features


if __name__ == "__main__":
    if not os.path.exists("custom_model.h5"):
        print("ERROR: custom_model.h5 not found!")
        exit(1)

    # Disable TF logging spam
    os.environ['TF_CPP_MIN_LOG_LEVEL'] = '2'
    tf.get_logger().setLevel('ERROR')

    model = tf.keras.models.load_model("custom_model.h5")

    # ---- Fix TensorFlow Lazy Initialization ----
    print("Warming up TensorFlow engine (capturing 1s of live audio)...")
    warmup_audio = sd.rec(SAMPLES_PER_WINDOW, samplerate=SAMPLE_RATE, channels=1, dtype='float32')
    sd.wait()
    warmup_features = extract_features(warmup_audio[:, 0])
    warmup_input = warmup_features[np.newaxis, ..., np.newaxis]
    model.predict(warmup_input, verbose=0)
    print("Engine ready!\n")

    print("\n" + "=" * 55)
    print("  LIVE 4-CLASS KWS TEST (.h5 Model)")
    print("  Labels: 'wake_up', 'turn_on', 'unknown', 'noise'")
    print("=" * 55)
    print(f"  Confidence threshold : {int(CONFIDENCE_THRESHOLD * 100)}%")
    print("\n  Say a command...\n")

    def audio_callback(indata, frames, time_info, status):

        audio = indata[:, 0].astype(np.float32)

        # KILL Mic DC Offset (Hardware bias that causes false 'unknown' detections)
        audio = audio - np.mean(audio)

        # Silence gate: PC audio is normalised -1.0..+1.0, threshold 0.05 = 5% of max
        max_amp = float(np.max(np.abs(audio)))
        if max_amp < 0.05:
            print(f"... (silence) | max_amp={max_amp:.4f}")
            return

        features = extract_features(audio)
        features = features[np.newaxis, ..., np.newaxis]
        pred = model.predict(features, verbose=0)[0]

        top_idx   = int(np.argmax(pred))
        top_score = float(pred[top_idx])
        label     = LABELS[top_idx]

        # Reject low-confidence predictions and negative classes
        if top_score < CONFIDENCE_THRESHOLD or label in ["noise", "unknown"]:
            print(f"... ({int(top_score*100)}% {label} — below threshold, treating as UNKNOWN)")
            return

        scores = " | ".join(
            f"{LABELS[i]}: {int(p*100):3d}%" for i, p in enumerate(pred)
        )
        # Show loudness alongside prediction for easy STM32 comparison
        print(f"{scores}  →  [{label.upper()}]  | max_amp={max_amp:.4f}")

    try:
        with sd.InputStream(
            samplerate=SAMPLE_RATE,
            blocksize=SAMPLES_PER_WINDOW,
            channels=1,
            callback=audio_callback,
            dtype="float32",
        ):
            print("Listening... (Ctrl+C to stop)")
            while True:
                sd.sleep(1000)
    except KeyboardInterrupt:
        print("\nStopped.")
