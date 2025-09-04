from pathlib import Path
from ..config import settings
from ..utils.procs import popen


def make_concat_file(frames_dir: Path, out_txt: Path) -> None:
    # 按文件名排序写入 concat 列表
    frames = sorted(list(frames_dir.glob("*.png")))
    with open(out_txt, 'w', encoding='utf-8') as f:
        for p in frames:
            f.write(f"file '{p.as_posix()}'\n")


def run_ffmpeg_concat(concat_txt: Path, fps: int, out_mp4: Path, log_path: Path, bitrate="20M", maxrate="30M") -> int:
    cmd = [
        settings.FFMPEG, "-hide_banner", "-y", "-loglevel", "error",
        "-f", "concat", "-safe", "0", "-r", str(fps), "-i", str(concat_txt),
        "-c:v", "h264_nvenc", "-preset", "p4", "-rc", "vbr", "-b:v", bitrate, "-maxrate", maxrate,
        "-pix_fmt", "yuv420p", "-movflags", "+faststart",
        "-c:a", "aac", "-b:a", "192k",
        str(out_mp4)
    ]
    proc = popen(cmd, log_path=log_path)
    return proc.wait()