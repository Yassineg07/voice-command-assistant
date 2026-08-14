import os
import urllib.request
import zipfile
import random
import librosa
import soundfile as sf
import shutil

DATASET_URL = "https://github.com/karolpiczak/ESC-50/archive/master.zip"
BASE_DIR = "dataset"
TEMP_DIR = os.path.join(BASE_DIR, "temp_esc50")
UNKNOWN_DIR = os.path.join(BASE_DIR, "unknown")
NOISE_DIR = os.path.join(BASE_DIR, "noise")

SAMPLES_TO_EXTRACT = 250  # 250 sudden transients gives a strong robustness shield
SAMPLE_RATE = 16000
CHUNK_SECONDS = 1

def ensure_folders():
    for d in [BASE_DIR, TEMP_DIR, UNKNOWN_DIR, NOISE_DIR]:
        if not os.path.exists(d):
            os.makedirs(d)

def download_and_process():
    ensure_folders()
    
    zip_path = os.path.join(BASE_DIR, "esc50.zip")
    
    if not os.path.exists(zip_path) and not os.path.exists(os.path.join(TEMP_DIR, "ESC-50-master")):
        print("Downloading ESC-50 Dataset (600 MB)...")
        urllib.request.urlretrieve(DATASET_URL, zip_path)
        print("Download complete!")
        
    if not os.path.exists(os.path.join(TEMP_DIR, "ESC-50-master")):
        print("Extracting ESC-50...")
        with zipfile.ZipFile(zip_path, 'r') as zip_ref:
            zip_ref.extractall(TEMP_DIR)
        print("Extraction complete!")
        
    audio_dir = os.path.join(TEMP_DIR, "ESC-50-master", "audio")
    all_files = [f for f in os.listdir(audio_dir) if f.endswith(".wav")]
    
    print(f"\nProcessing {SAMPLES_TO_EXTRACT} random environmental transients...")
    
    # Shuffle to get a mix of dogs, rain, typing, glass breaking, etc.
    random.shuffle(all_files)
    subset = all_files[:SAMPLES_TO_EXTRACT]
    
    processed_count = 0
    for wav_file in subset:
        source_path = os.path.join(audio_dir, wav_file)
        
        # Load audio using librosa (this automatically resamples from 44.1kHz to 16kHz)
        y, sr = librosa.load(source_path, sr=SAMPLE_RATE, mono=True)
        
        # ESC-50 files are 5 seconds long. Let's extract the middle 1-second to capture the core transient
        start_sample = 2 * SAMPLE_RATE
        end_sample = start_sample + (CHUNK_SECONDS * SAMPLE_RATE)
        
        y_chunk = y[start_sample:end_sample]
        
        # Save as 16-bit PCM wav
        dest = os.path.join(UNKNOWN_DIR, f"esc50_{wav_file}")
        sf.write(dest, y_chunk, SAMPLE_RATE, subtype='PCM_16')
        processed_count += 1
        
        if processed_count % 50 == 0:
            print(f"  Processed {processed_count}/{SAMPLES_TO_EXTRACT}...")
            
    print("\nESC-50 Transients extracted successfully!")
    print("These have been merged into the 'unknown' directory to act as false-trigger guards.")
    print("You can now safely delete the 'dataset/temp_esc50' folder and the zip file.")

if __name__ == "__main__":
    download_and_process()
