from __future__ import annotations
import json
from dataclasses import dataclass
from pathlib import Path
from datetime import datetime
try:
    from zoneinfo import ZoneInfo
    CN_TZ = ZoneInfo("Asia/Shanghai")
except Exception:
    from datetime import timezone, timedelta
    CN_TZ = timezone(timedelta(hours=8))

import psutil
from sqlalchemy.orm import Session
import requests
import time
from ..db.models import Job, JobArtifact
from ..models.status import JobStatus
from ..utils.procs import popen, subprocess
from ..config import settings
from .ue_command import build_ue_cmd
from .ffmpeg import make_concat_file, run_ffmpeg_concat


@dataclass
class RunnerContext:
    job: Job
    work_dir: Path
    frames_dir: Path
    logs_dir: Path
    ue_log: Path
    ffmpeg_log: Path
    out_mp4: Path



def run_job(db: Session, job: Job, template: dict) -> None:
    # Init work dirs
    work = Path(settings.DATA_ROOT) / "jobs" / job.job_id
    frames = work / "frames"
    logs = work / "logs"
    frames.mkdir(parents=True, exist_ok=True)
    logs.mkdir(parents=True, exist_ok=True)

    # payload = json.loads(job.payload)

    ctx = RunnerContext(
        job=job,
        work_dir=work,
        frames_dir=frames,
        logs_dir=logs,
        ue_log=logs / f"ue_{job.job_id}.log",
        ffmpeg_log=logs / f"ffmpeg_{job.job_id}.log",
        out_mp4=work / f"{job.job_id}.mp4",
    )

    job.status = JobStatus.starting.value
    job.started_at = datetime.now(CN_TZ)
    db.commit()

    ue_log_absolute = ctx.ue_log.absolute()

    try:
        req_payload = json.loads(job.payload) if job.payload else {}
    except Exception:
        req_payload = {}

    quality_str = str(req_payload.get("quality", "MEDIUM")).upper()
    _qmap = {"LOW": 0, "MEDIUM": 1, "HIGH": 2, "EPIC": 3}
    quality_num = _qmap.get(quality_str, 1)

    movie_fmt = req_payload.get("format") # "mp4" or "mov"

    ue_cmd = build_ue_cmd(
        map_path=template.get("map_path"),
        level_sequence=template.get("level_sequence"),
        job_id=job.job_id,
        log_path=ue_log_absolute,
        movie_quality=str(quality_num),
        movie_format=movie_fmt
    )

    debug_cmd_str = subprocess.list2cmdline(ue_cmd)
    print("ue_cmd.exe content: " + debug_cmd_str)

    print(f"ue_cmd.exe log path: {ue_log_absolute}")

    proc = popen(ue_cmd, log_path=ue_log_absolute)

    if (proc.pid is None) or (not psutil.pid_exists(proc.pid)):
        job.status = JobStatus.failed.value
        db.commit()
        return 
    
    job.pid = proc.pid
    db.commit()

   
    render_completed = False
    max_wait_time = 300
    wait_start = time.time()

    
    while not render_completed and (time.time() - wait_start) < max_wait_time:
        rc = proc.poll()

        if rc is None:
            print(f"Process {proc.pid} is still running...")
        else:
            print(f"Process {proc.pid} return with code {rc}.")
            if rc == 0:
                job.ended_at = datetime.now(CN_TZ)
                db.commit()

                render_completed = True
            else:
                # Non-zero exit: print UE log tail from last 'error' line to end
                try:
                    lines = Path(ue_log_absolute).read_text(encoding='utf-8', errors='ignore').splitlines()
                    if lines:
                        start_idx = max(0, len(lines) - 200)
                        for i in range(len(lines) - 1, -1, -1):
                            if 'error' in lines[i].lower():
                                start_idx = i
                                break
                        tail = "\n".join(lines[start_idx:])
                        print("---- UE log tail ----")
                        print(tail)
                        print("---- UE log tail ----")
                        print(f"More error info: {ue_log_absolute}")
                except Exception as e:
                    print(f"Read UE log failed: {e}")
                job.status = JobStatus.failed.value
                db.commit()
                return
            break

        db.refresh(job)
        if job.status_enum in [JobStatus.completed, JobStatus.encoding, JobStatus.uploading]:
            render_completed = True

        time.sleep(5)

    if not render_completed:
        job.status = JobStatus.failed.value
        job.ended_at = datetime.now(CN_TZ)
        db.commit()
        return
