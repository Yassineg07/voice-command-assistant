import os
import glob
import numpy as np
import librosa
import tensorflow as tf

SAMPLE_RATE = 8000
DURATION = 1.0
SAMPLES_PER_WINDOW = int(SAMPLE_RATE * DURATION)

N_MELS = 10
N_FFT = 256
HOP_LENGTH = 160
FMIN = 20.0
FMAX = 4000.0

DATASET_PATH = "dataset"
LABELS = ["noise", "turn_on", "unknown", "wake_up"]

def extract_features(audio):
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

def representative_data_gen():
    """Yields 100 random samples from the dataset to calibrate the INT8 quantization boundaries."""
    samples = []
    
    for label in LABELS:
        folder = os.path.join(DATASET_PATH, label)
        if not os.path.exists(folder):
            continue
            
        wav_files = glob.glob(os.path.join(folder, "*.wav"))[:25] # 25 per class
        for f in wav_files:
            audio, _ = librosa.load(f, sr=SAMPLE_RATE, mono=True)
            if len(audio) > SAMPLES_PER_WINDOW:
                start = (len(audio) - SAMPLES_PER_WINDOW) // 2
                audio = audio[start:start+SAMPLES_PER_WINDOW]
            else:
                audio = np.pad(audio, (0, max(0, SAMPLES_PER_WINDOW - len(audio))))
            
            feat = extract_features(audio)
            samples.append(feat[np.newaxis, ..., np.newaxis])
            
    np.random.shuffle(samples)
    for sample in samples:
        yield [sample.astype(np.float32)]

if __name__ == "__main__":
    if not os.path.exists("custom_model.h5"):
        print("ERROR: custom_model.h5 not found! Run run_5_train.py first.")
        exit(1)

    print("Loading H5 model...")
    model = tf.keras.models.load_model("custom_model.h5")

    print("Configuring INT8 Full Integer Quantization (Edge Impulse Parity)...")
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_data_gen
    
    # Enforce strictly INT8 inputs and outputs to match MCU DSP types perfectly
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8

    print("Converting... (this may take a moment)")
    tflite_model = converter.convert()

    output_name = "custom_model.tflite"
    with open(output_name, "wb") as f:
        f.write(tflite_model)

    print(f"\nSUCCESS: Exported {output_name} ({len(tflite_model)/1024:.1f} KB)")
    print("Drop this file into STM32Cube AI Studio to generate your C arrays!")
