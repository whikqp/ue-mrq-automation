from pathlib import Path

def count_frames(frames_dir: Path) -> int:
    # Count only .png/.exr files; extend as needed
    n = 0
    for ext in ("*.png", "*.exr"):
        n += len(list(frames_dir.glob(ext)))
    return n
