import glob
import os
import shutil

Import("env")

project_dir = env.get("PROJECT_DIR")
data_dir = os.path.join(project_dir, "data")
os.makedirs(data_dir, exist_ok=True)

for existing in glob.glob(os.path.join(data_dir, "*.wav")):
    os.remove(existing)

wav_paths = sorted(glob.glob(os.path.join(project_dir, "*.wav")))
for index, wav_path in enumerate(wav_paths):
    destination = os.path.join(data_dir, f"sound_{index:02d}.wav")
    shutil.copy2(wav_path, destination)
