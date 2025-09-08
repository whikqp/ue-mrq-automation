UE MRQ Server

FastAPI service to trigger Unreal Engine Movie Render Queue (MRQ) jobs and manage results.

Prerequisites
- Python 3.10+
- Unreal Engine installed (path required in config)
- FFmpeg installed (path required in config)

Setup
```
# Create and activate a virtual environment (Conda example)
conda create -n ue-mrq python=3.10 -y
conda activate ue-mrq

# Install dependencies
pip install -r requirements.txt
```

Configuration
- Edit configuration in the `.env` file at the repository root. This is the recommended way to configure the service.
- Key settings include (examples only):
  - UE_ROOT=D:/InstallVersion_UE/UE_5.4
  - UPROJECT=D:/Path/To/YourProject/YourProject.uproject
  - FFMPEG=D:/Install/ffmpeg/bin/ffmpeg.exe
  - EXECUTOR_CLASS=/Script/MoviePipelineExt.MoviePipelineNativeHostExecutor
  - GAME_MODE_CLASS=/Script/MovieRenderPipelineCore.MoviePipelineGameMode
  - DATA_ROOT=./data
  - LOG_ROOT=./data/logs

Note: Environment variables with the same names will override `.env` values. Prefer setting values in `.env` unless you intentionally override via OS env.

Run
```
python run.py
```
The server listens on `127.0.0.1:8080` by default.

API
- Open interactive API docs at: http://127.0.0.1:8080/docs
- Use the docs to explore and test endpoints (e.g., templates listing and job submission).

