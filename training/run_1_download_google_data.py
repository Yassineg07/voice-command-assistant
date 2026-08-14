import os
import urllib.request
import tarfile
import random
import wave
import shutil

DATASET_URL = "http://storage.googleapis.com/download.tensorflow.org/data/speech_commands_v0.02.tar.gz"
BASE_DIR = "dataset"
TEMP_DIR = os.path.join(BASE_DIR, "temp_google")
UNKNOWN_DIR = os.path.join(BASE_DIR, "unknown")
NOISE_DIR = os.path.join(BASE_DIR, "noise")

# The full Google dataset is 2.3 GB and ~105,000 files. Edge Impulse free tier
# restricts total dataset sizes (typically ~4-6 hours or ~14,000 to ~21,000 one-second samples).
# Taking 100 samples from each of the 35 words provides 3,500 highly phonetically diverse 
# 'unknown' samples without overwhelmingly crushing our custom keywords mathematically.
SAMPLES_PER_WORD = 100
SAMPLE_RATE = 16000
CHUNK_SECONDS = 1

def ensure_folders():
    for d in [BASE_DIR, TEMP_DIR, UNKNOWN_DIR, NOISE_DIR]:
        if not os.path.exists(d):
            os.makedirs(d)

def chunk_noise_wav(source_path, dest_dir, prefix):
    """
    Google's background noise files are long multi-minute continuous WAVs.
    This slices them into perfect 1-second (16000 samples) chunks.
    """
    print(f"Chunking noise: {prefix}.wav")
    with wave.open(source_path, 'rb') as w:
        frames = w.getnframes()
        rate = w.getframerate()
        channels = w.getnchannels()
        sampwidth = w.getsampwidth()
        
        # We enforce 16kHz Mono just to be safe
        if rate != SAMPLE_RATE or channels != 1:
            print(f"  Skipping {prefix} (not 16kHz mono)")
            return
            
        chunk_frames = rate * CHUNK_SECONDS
        total_chunks = frames // chunk_frames
        
        for i in range(total_chunks):
            chunk_data = w.readframes(chunk_frames)
            out_filename = os.path.join(dest_dir, f"{prefix}_chunk_{i:04d}.wav")
            with wave.open(out_filename, 'wb') as out_w:
                out_w.setnchannels(channels)
                out_w.setsampwidth(sampwidth)
                out_w.setframerate(rate)
                out_w.writeframes(chunk_data)

def download_and_extract():
    ensure_folders()
    
    tar_path = os.path.join(BASE_DIR, "speech_commands_v0.02.tar.gz")
    
    if not os.path.exists(tar_path) and not os.path.exists(os.path.join(TEMP_DIR, "yes")):
        print("Downloading Google Speech Commands V2 (Full Dataset 2.3 GB)...")
        print("This might take a few minutes depending on your connection.")
        urllib.request.urlretrieve(DATASET_URL, tar_path)
        print("Download complete!")
        
    if not os.path.exists(os.path.join(TEMP_DIR, "yes")):
        print("Extracting tar.gz... (this is I/O heavy)")
        with tarfile.open(tar_path, "r:gz") as tar:
            tar.extractall(path=TEMP_DIR)
        print("Extraction complete!")
        
    # Process the extracted data
    print("\nProcessing background noise...")
    bg_noise_dir = os.path.join(TEMP_DIR, "_background_noise_")
    if os.path.exists(bg_noise_dir):
        for file in os.listdir(bg_noise_dir):
            if file.endswith(".wav"):
                source_path = os.path.join(bg_noise_dir, file)
                prefix = os.path.splitext(file)[0]
                chunk_noise_wav(source_path, NOISE_DIR, prefix)
    
    print("\nProcessing 'unknown' words...")
    folders = [d for d in os.listdir(TEMP_DIR) if os.path.isdir(os.path.join(TEMP_DIR, d)) and not d.startswith("_")]
    
    total_unknown_copied = 0
    
    for folder in folders:
        folder_path = os.path.join(TEMP_DIR, folder)
        # Avoid including any existing target wake words if they happen to share a name (e.g. 'up' or 'on')
        if folder in ['up', 'on']:
            continue
            
        wav_files = [f for f in os.listdir(folder_path) if f.endswith(".wav")]
        random.shuffle(wav_files)
        
        subset = wav_files[:SAMPLES_PER_WORD]
        
        for wav in subset:
            source = os.path.join(folder_path, wav)
            # Prefix with the folder name to avoid any collision
            dest = os.path.join(UNKNOWN_DIR, f"{folder}_{wav}")
            # Ensure it's a 1-second file exactly (16000 frames * 2 bytes)
            if os.path.getsize(source) == (16000 * 2) + 44: # 44 bytes wav header
                shutil.copy2(source, dest)
                total_unknown_copied += 1
                
    print(f"\nDone! Extracted {total_unknown_copied} isolated 'unknown' words.")
    print("Background noise has been chunked securely into 1-second pieces.")
    print("\nNext step: You can delete 'dataset/temp_google' entirely to free up space.")

if __name__ == "__main__":
    download_and_extract()
