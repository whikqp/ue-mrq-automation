from pydantic_settings import BaseSettings
from pydantic import Field
from pathlib import Path

class Settings(BaseSettings):
    # Unreal & tools
    UE_ROOT: str = Field(..., description="UE install root directory")
    UPROJECT: str = Field(..., description="uproject absolute path")
    FFMPEG: str = Field("ffmpeg", description="ffmpeg executable path")
    EXECUTOR_CLASS: str = Field("MoviePipelineNativeHostExecutor", description="Movie Pipeline Executor reference path")
    GAME_MODE_CLASS: str = Field("Map gamemode", description="Movie render pipeline job game mode")

    # Paths
    DATA_ROOT: Path = Field(default=Path("./data"))
    LOG_ROOT: Path = Field(default=Path("./data/logs"))

    # Scheduler
    MAX_CONCURRENCY: int = 2
    MIN_FREE_VRAM_MB: int = 4096
    SCHEDULER_POLL_MS: int = 1500

    # OSS
    OSS_ENDPOINT: str | None = None
    OSS_BUCKET: str | None = None
    OSS_ACCESS_KEY_ID: str | None = None
    OSS_ACCESS_KEY_SECRET: str | None = None
    OSS_STS_TOKEN: str | None = None
    OSS_SIGNED_URL_EXPIRE: int = 7 * 24 * 3600

    # Misc
    API_KEY: str | None = None
    MRQ_SERVER_BASE_URL: str | None = None

    class Config:
        env_file = ".env"

settings = Settings() # pyright: ignore[reportCallIssue] - BaseSettings reads values from env/.env at runtime
settings.DATA_ROOT.mkdir(parents=True, exist_ok=True)
settings.LOG_ROOT.mkdir(parents=True, exist_ok=True)