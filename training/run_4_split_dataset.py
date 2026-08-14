"""
run_5_split_dataset.py
Splits 'custom_keywords' into 'dataset/' (train) and 'dataset_test/' (test).
For EACH label, it identifies real vs synthetic files and guarantees an independent 80/20 train/test split.
"""
import os
import shutil
import random

SOURCE_BASE = "custom_keywords"
TRAIN_BASE  = "dataset"
TEST_BASE   = "dataset_test"
LABELS      = ["wake_up", "turn_on"]
RANDOM_SEED = 42

def is_real(filename):
    """Real recordings won't have 'Neural' in their name but synthetics will."""
    return "Neural" not in filename

def split_files(files, train_ratio=0.80):
    random.shuffle(files)
    cut = max(1, int(len(files) * train_ratio))
    return files[:cut], files[cut:]

def main():
    if not os.path.exists(SOURCE_BASE):
        print(f"ERROR: {SOURCE_BASE}/ folder not found. Run run_3 first.")
        return

    random.seed(RANDOM_SEED)

    # Empty old directories to avoid dataset contamination
    for base in [TRAIN_BASE, TEST_BASE]:
        for label in LABELS:
            folder = os.path.join(base, label)
            if os.path.exists(folder):
                shutil.rmtree(folder)
            os.makedirs(folder)

    for label in LABELS:
        src_dir   = os.path.join(SOURCE_BASE, label)
        train_dir = os.path.join(TRAIN_BASE, label)
        test_dir  = os.path.join(TEST_BASE, label)
        
        if not os.path.exists(src_dir):
            continue

        all_files = [f for f in os.listdir(src_dir) if f.endswith(".wav")]

        real_files      = [f for f in all_files if     is_real(f)]
        synthetic_files = [f for f in all_files if not is_real(f)]

        real_train,      real_test      = split_files(real_files)
        synthetic_train, synthetic_test = split_files(synthetic_files)

        print(f"\n[{label.upper()}]")
        print(f"  Real      : {len(real_files):4d} total → {len(real_train):4d} train | {len(real_test):4d} test")
        print(f"  Synthetic : {len(synthetic_files):4d} total → {len(synthetic_train):4d} train | {len(synthetic_test):4d} test")

        for f in real_train + synthetic_train:
            shutil.copy2(os.path.join(src_dir, f), os.path.join(train_dir, f))

        for f in real_test + synthetic_test:
            shutil.copy2(os.path.join(src_dir, f), os.path.join(test_dir, f))

    print("\nSplit complete!")
    print(f"  Training data → {TRAIN_BASE}/")
    print(f"  Testing  data → {TEST_BASE}/")

if __name__ == "__main__":
    main()
