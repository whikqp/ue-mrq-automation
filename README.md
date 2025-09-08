# UE MRQ Automation

Backend + Unreal Engine integration for automating Movie Render Pipeline (MRQ) jobs.

This project has two parts:
- Python backend service that exposes REST APIs to list video templates and create render jobs, schedules work, and tracks results.
- Unreal Engine side that runs headless rendering via a custom Movie Pipeline Executor, receives job context, and posts progress/results back to the server.


## Overview

Automate UE5 renders from any client (UI, CLI, or CI) by submitting jobs to a small HTTP service. The service queues jobs, launches `UnrealEditor-Cmd` with the desired sequence/map and executor class, and collects progress plus final artifacts.

```
+-----------------------+        HTTP        +------------------------+      spawns       +-----------------------------------+
| Client (UI/CLI/CI)    |  ───────────────▶  | Python MRQ Server      |  ───────────────▶ | UE5 Renderer (Custom Executor)    |
| - list templates      |                   | - queue + scheduler     |                   | - parse JobId + payload           |
| - submit jobs         |                   | - SQLite + logs         |                   | - render headless (Editor-Cmd)    |
| - poll job status     |                   | - optional OSS upload   |                   | - POST progress + completion      |
+-----------------------+                   +------------┬-----------+                   +-----------------┬-----------------+
                                                           ▲                                             |
                                                           └───────── HTTP callbacks (progress/results) ─┘
```


## Repository Layout

- `ue-mrq-server/`: FastAPI-based backend server, job queue, UE invocation, and callbacks.
- `mrq_cli_demo/`: Minimal UE5 project for local testing of MRQ command-line rendering.
- `configs/` inside server: `templates.json` describing available render templates.


## Features

- REST endpoints to list templates and submit MRQ jobs.
- Headless UE5 rendering with `UnrealEditor-Cmd` and custom Movie Pipeline Executor.
- In-process scheduler with simple GPU VRAM check and concurrency limit.
- Job tracking with SQLite; artifacts persisted (paths/URLs) and logs per job.
- UE-to-server HTTP callbacks for progress and completion.
- Optional artifact upload (e.g., Alibaba Cloud OSS) and signed URL return.


## Quick Start

Prerequisites:
- Python 3.10+
- Unreal Engine 5.4+ installed (path required)
- FFmpeg installed and on PATH (or set absolute path)
- Windows or Linux (paths in examples use Windows)

1) Configure the backend (.env at `ue-mrq-server/.env`):

```
# Unreal Engine + tools
UE_ROOT=D:/InstallVersion_UE/UE_5.4
UPROJECT=D:/Path/To/YourProject/YourProject.uproject
FFMPEG=D:/Install/ffmpeg/bin/ffmpeg.exe
EXECUTOR_CLASS=/Script/MoviePipelineExt.MoviePipelineNativeHostExecutor
GAME_MODE_CLASS=/Script/MovieRenderPipelineCore.MoviePipelineGameMode

# Data + logs
DATA_ROOT=./data
LOG_ROOT=./data/logs

# Scheduler
MAX_CONCURRENCY=2
MIN_FREE_VRAM_MB=4096
SCHEDULER_POLL_MS=1500

# (Optional) Object Storage for uploads
OSS_ENDPOINT=
OSS_BUCKET=
OSS_ACCESS_KEY_ID=
OSS_ACCESS_KEY_SECRET=
OSS_STS_TOKEN=
OSS_SIGNED_URL_EXPIRE=604800
```

2) Define your templates (`ue-mrq-server/configs/templates.json`):

```
{
  "templates": [
    {
      "template_id": "Seq1",
      "template_name": "Example Sequence",
      "template_desc": "Demo render",
      "template_thumbnail": "",
      "map_name": "Map0",
      "map_path": "/Game/Maps/Map0.Map0",
      "level_sequence": "/Game/Seqs/Seq1.Seq1",
      "params": {}
    }
  ]
}
```

3) Install and run the server:

```
cd ue-mrq-server
python -m venv .venv && .venv/Scripts/activate  # Windows PowerShell: .venv\Scripts\Activate.ps1
pip install -r requirements.txt
python run.py
```

The server listens on `http://127.0.0.1:8080`. Interactive docs: `http://127.0.0.1:8080/docs`.


## API Overview

Base URL: `http://127.0.0.1:8080`

- GET `/templates`
  - Returns a filtered list of available templates with id/name/desc/thumbnail.

- POST `/jobs`
  - Create a job using a template.
  - Request body example:

    ```json
    {
      "template_id": "Seq1",
      "params": { "character_name": "Alice" },
      "quality": "HIGH",                  // LOW | MEDIUM | HIGH | EPIC
      "format": "mp4",                    // mp4 | mov
      "session_id": "demo-session-001"
    }
    ```

  - Response example:

    ```json
    {
      "session_id": "demo-session-001",
      "job_id": "<uuid>",
      "status": "queued",
      "queue_position": 1,
      "template_id": "Seq1"
    }
    ```

- GET `/jobs/{job_id}`
  - Returns full job state, progress, timestamps, and artifacts.
  - If called by UE, set header `X-Client: ue5` to receive a UE-focused payload shape `{ job_id, artifacts, payload }`.

- GET `/jobs/{job_id}/progress`
  - Lightweight progress + status without params.

- GET `/jobs/{job_id}/params`
  - Returns only the submitted `params` object.

- POST `/jobs/{job_id}/cancel`
  - Attempts to cancel a running job (best effort; marks canceled when terminated).

- UE callbacks (sent by the UE executor during/after rendering):
  - POST `/ue-notifications/job/{job_id}/progress`
    - Body: `{ "progress_percent": 0.42, "progress_eta_seconds": 120, "status": "rendering" }`
  - POST `/ue-notifications/job/{job_id}/render-complete`
    - Body: `{ "video_directory": "C:/.../Saved/MovieRenders/Seq1/<job_id>" }`
  - POST `/ue-notifications/job/{job_id}/encoding-status`
    - Body: `{ "status": "completed", "video_url": "https://.../file.mp4" }` (if uploaded)


## How Rendering Works (UE5)

The server builds and launches an `UnrealEditor-Cmd` command using your `.env` and the selected template fields. Key flags include:

- `-MoviePipelineLocalExecutorClass=<EXECUTOR_CLASS>` e.g. `/Script/MoviePipelineExt.MoviePipelineNativeHostExecutor`
- `-LevelSequence=<template.level_sequence>` and map via `<template.map_path>`
- `-MovieQuality=<0..3>` mapped from `LOW..EPIC`
- `-MovieFormat=<mp4|mov>`
- `-JobId=<job_id>` used by the executor to fetch job context
- `-RenderOffscreen -Unattended -NOSPLASH -NoLoadingScreen -notexturestreaming`

Expected executor behavior (in your UE project/plugin):
- Parse `JobId` from command line.
- GET `/jobs/{job_id}` with `X-Client: ue5` to retrieve `payload`.
- Report progress to the server during rendering.
- On completion, notify `render-complete`, then optionally upload and report `encoding-status` with a URL.

Note: This repository includes a minimal UE project (`mrq_cli_demo/`) to test command-line rendering. The custom executor class referenced by `EXECUTOR_CLASS` (e.g., `MoviePipelineExt.MoviePipelineNativeHostExecutor`) must be available in your UE project/plugins.


## Configuration Reference

`ue-mrq-server/.env` (environment variables override these at runtime):

- `UE_ROOT`: UE install root (directory that contains `Engine/Binaries/...`).
- `UPROJECT`: Absolute path to your `.uproject`.
- `FFMPEG`: FFmpeg binary path (used if post-processing/upload requires it).
- `EXECUTOR_CLASS`: Full class path for Movie Pipeline Executor.
- `GAME_MODE_CLASS`: Optional game mode for render sessions.
- `DATA_ROOT`, `LOG_ROOT`: Directories for work, logs, and outputs.
- `MAX_CONCURRENCY`, `MIN_FREE_VRAM_MB`, `SCHEDULER_POLL_MS`: Scheduler controls.
- `OSS_*`: Optional object storage configuration for uploading artifacts.

Templates: `ue-mrq-server/configs/templates.json`
- `template_id`, `template_name`, `template_desc`, `template_thumbnail`
- `map_path` (preferred) or `map_name`
- `level_sequence`: Level sequence asset path
- `params`: Arbitrary JSON object passed to UE via job payload


## Development

- Server code entry: `ue-mrq-server/run.py` (`uvicorn app.main:app`).
- API docs: `http://127.0.0.1:8080/docs` (Swagger UI).
- DB: SQLite file under `ue-mrq-server/data/jobs.sqlite` (created at first run).
- Logs and per-job outputs under `ue-mrq-server/data/jobs/<job_id>/`.


## Contributing

Issues and PRs are welcome. Please discuss significant changes first by opening an issue.


## License

MIT — see `LICENSE` for details.


## Acknowledgements

- Unreal Engine Movie Render Pipeline
- FastAPI / Pydantic / SQLAlchemy / Uvicorn
