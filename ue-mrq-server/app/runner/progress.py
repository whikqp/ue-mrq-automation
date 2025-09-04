from pathlib import Path

def count_frames(frames_dir: Path) -> int:
    # 仅统计 .png / .exr，可扩展
    n = 0
    for ext in ("*.png", "*.exr"):
        n += len(list(frames_dir.glob(ext)))
    return n