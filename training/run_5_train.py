import os
import glob
import numpy as np
import librosa
import tensorflow as tf
from tensorflow.keras import layers, models

SAMPLE_RATE = 8000
DURATION = 1.0
SAMPLES_PER_WINDOW = int(SAMPLE_RATE * DURATION)

N_MELS = 10
N_FFT = 256
HOP_LENGTH = 160
FMIN = 20.0
FMAX = 4000.0

LABELS = ["noise", "turn_on", "unknown", "wake_up"]
TRAIN_DATASET_PATH = "dataset"
TEST_DATASET_PATH = "dataset_test"  # Extracted from Edge Impulse


def extract_features(audio):
    # CRITICAL: We compute Magnitude (|STFT|) and HTK filterbank (norm=None)
    # to perfectly mirror the C-code's computationally cheaper math on the MCU.
    # Do NOT use librosa.feature.melspectrogram here, as it defaults to Slaney/Power.
    
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

def load_audio_files(folder, expected_label_idx, limit=None):
    if not os.path.exists(folder):
        return [], []
    wav_files = glob.glob(os.path.join(folder, "*.wav"))
    if limit:
        wav_files = wav_files[:limit]
        
    X_list, y_list = [], []
    for file in wav_files:
        audio, _ = librosa.load(file, sr=SAMPLE_RATE, mono=True)
        if len(audio) > SAMPLES_PER_WINDOW:
            start = (len(audio) - SAMPLES_PER_WINDOW) // 2
            audio = audio[start:start+SAMPLES_PER_WINDOW]
        else:
            audio = np.pad(audio, (0, max(0, SAMPLES_PER_WINDOW - len(audio))))
            
        X_list.append(extract_features(audio))
        y_list.append(expected_label_idx)
        
    return X_list, y_list

def load_split(dataset_path, limit_noise=True):
    X, y = [], []
    for idx, label in enumerate(LABELS):
        folder = os.path.join(dataset_path, label)
        limit = 400 if (limit_noise and label in ["noise", "unknown"]) else None
        feat_x, label_y = load_audio_files(folder, idx, limit=limit)
        X.extend(feat_x)
        y.extend(label_y)
    return np.array(X)[..., np.newaxis], np.array(y)

def build_cnn(input_shape):
    model = models.Sequential([
        layers.Input(shape=input_shape),
        layers.Conv2D(32, (3, 3), activation='relu', padding='same'),
        layers.BatchNormalization(),
        layers.MaxPooling2D((2, 2)),
        layers.Dropout(0.25),
        layers.Conv2D(64, (3, 3), activation='relu', padding='same'),
        layers.BatchNormalization(),
        layers.MaxPooling2D((2, 2)),
        layers.Dropout(0.25),
        layers.Conv2D(128, (3, 3), activation='relu', padding='same'),
        layers.BatchNormalization(),
        layers.GlobalAveragePooling2D(),
        layers.Dropout(0.3),
        layers.Dense(64, activation='relu'),
        layers.Dense(len(LABELS), activation='softmax')
    ])
    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=0.001),
        loss='sparse_categorical_crossentropy',
        metrics=['accuracy']
    )
    return model

if __name__ == "__main__":
    print("\n--- Loading SYNTHETIC Training Data ---")
    X_train, y_train = load_split(TRAIN_DATASET_PATH, limit_noise=True)
    
    print("\n--- Loading REAL Testing Data ---")
    # For testing, we randomly split off 20% of the training noise/unknown to use as background padding
    # since all your real mic recordings are just target keywords.
    X_test_real, y_test_real = load_split(TEST_DATASET_PATH, limit_noise=False)
    
    if len(X_test_real) == 0:
        print("ERROR: 'dataset_test' folder empty or missing! Please download your test split from Edge Impulse.")
        exit(1)
        
    # We borrow 20% of the negative classes from the train set to pad the test validation matrix
    from sklearn.model_selection import train_test_split
    neg_idx = np.where((y_train == 0) | (y_train == 2))[0]
    pos_idx = np.where((y_train == 1) | (y_train == 3))[0]
    
    X_train_neg, X_test_neg, y_train_neg, y_test_neg = train_test_split(
        X_train[neg_idx], y_train[neg_idx], test_size=0.2, random_state=42, stratify=y_train[neg_idx]
    )
    
    # Final matrix assembly
    X_train_final = np.concatenate([X_train[pos_idx], X_train_neg])
    y_train_final = np.concatenate([y_train[pos_idx], y_train_neg])
    X_test_final = np.concatenate([X_test_real, X_test_neg])
    y_test_final = np.concatenate([y_test_real, y_test_neg])
    
    # Shuffle
    train_shuf = np.random.permutation(len(X_train_final))
    test_shuf  = np.random.permutation(len(X_test_final))
    X_train_final, y_train_final = X_train_final[train_shuf], y_train_final[train_shuf]
    X_test_final, y_test_final   = X_test_final[test_shuf], y_test_final[test_shuf]

    print(f"\nFinal Train Shape: {X_train_final.shape}")
    print(f"Final Test Shape : {X_test_final.shape}")

    model = build_cnn(input_shape=X_train_final.shape[1:])
    callbacks = [
        tf.keras.callbacks.ReduceLROnPlateau(monitor='val_accuracy', patience=15, factor=0.5, min_lr=1e-5),
        tf.keras.callbacks.EarlyStopping(monitor='val_accuracy', patience=30, restore_best_weights=True)
    ]

    model.fit(
        X_train_final, y_train_final,
        epochs=150,
        batch_size=32,
        validation_data=(X_test_final, y_test_final),
        callbacks=callbacks
    )

    model.save("custom_model.h5")
    print("\nModel successfully evaluated on real microphone samples and saved!")
