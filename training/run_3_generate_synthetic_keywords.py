import os
import random
import asyncio
import librosa
import edge_tts
import soundfile as sf
import numpy as np

WORDS = ["wake up", "turn on"]
BASE_DIR = "custom_keywords"
SAMPLE_RATE = 8000
CHUNK_SECONDS = 1
AUGMENT_COUNT = 75  # ~1200 high-quality samples per keyword

VOICES = [
    "en-US-AriaNeural", "en-US-GuyNeural", "en-US-AnaNeural", "en-US-ChristopherNeural",
    "en-GB-SoniaNeural", "en-GB-RyanNeural", "en-AU-NatashaNeural", "en-AU-WilliamNeural",
    "en-CA-ClaraNeural", "en-CA-LiamNeural", "en-IE-ConnorNeural", "en-IE-EmilyNeural",
    "en-IN-NeerjaNeural", "en-IN-PrabhatNeural", "en-NZ-MitchellNeural", "en-ZA-LukeNeural"
]

def ensure_folders():
    for word in WORDS:
        os.makedirs(os.path.join(BASE_DIR, word.replace(" ", "_")), exist_ok=True)

async def generate_base_tts():
    print("Downloading ultra-realistic Microsoft Neural Voices...")
    for word in WORDS:
        for voice in VOICES:
            word_dir = os.path.join(BASE_DIR, word.replace(" ", "_"))
            filename = os.path.join(word_dir, f"base_{voice}.mp3")
            if not os.path.exists(filename):
                communicate = edge_tts.Communicate(word, voice)
                await communicate.save(filename)

def process_and_augment():
    print("\nApplying Librosa augmentations (pitch & speed variance)...")
    for word in WORDS:
        word_dir = os.path.join(BASE_DIR, word.replace(" ", "_"))
        base_files = [f for f in os.listdir(word_dir) if f.startswith("base_") and f.endswith(".mp3")]
        
        generated_count = 0
        for base_file in base_files:
            source_path = os.path.join(word_dir, base_file)
            y, sr = librosa.load(source_path, sr=SAMPLE_RATE, mono=True)
            y_trimmed, _ = librosa.effects.trim(y, top_db=40)
            
            for i in range(AUGMENT_COUNT):
                speed_factor = random.uniform(0.85, 1.15)
                n_steps = random.uniform(-3, 3)
                
                y_aug = librosa.effects.time_stretch(y_trimmed, rate=speed_factor)
                y_aug = librosa.effects.pitch_shift(y_aug, sr=SAMPLE_RATE, n_steps=n_steps)
                
                target_len = SAMPLE_RATE * CHUNK_SECONDS
                y_final = np.zeros(target_len, dtype=np.float32)
                
                if len(y_aug) > target_len:
                    start = (len(y_aug) - target_len) // 2
                    y_final = y_aug[start:start + target_len]
                else:
                    start = (target_len - len(y_aug)) // 2
                    y_final[start:start + len(y_aug)] = y_aug
                    
                voice_name = base_file.replace("base_", "").replace(".mp3", "")
                out_name = os.path.join(word_dir, f"{word.replace(' ', '_')}_{voice_name}_aug_{i:03d}.wav")
                sf.write(out_name, y_final, SAMPLE_RATE, subtype='PCM_16')
                generated_count += 1
                
        for base_file in base_files:
            os.remove(os.path.join(word_dir, base_file))
            
        print(f"Generated {generated_count} synthetic samples for '{word}'.")

if __name__ == "__main__":
    ensure_folders()
    asyncio.run(generate_base_tts())
    process_and_augment()
    print(f"\nSynthetic generation complete! Check {BASE_DIR}/ folder.")
