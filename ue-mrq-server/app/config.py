from pydantic_settings import BaseSettings
from pydantic import Field
from pathlib import Path

class Settings(BaseSettings):
    # Unreal & tools
    UE_ROOT: str = Field(..., description="UE 安装根目录")
    UPROJECT: str = Field(..., description="uproject 绝对路径")
    FFMPEG: str = Field("ffmpeg", description="ffmpeg 可执行路径")
    EXECUTOR_CLASS: str = Field("MoviePipelineNativeHostExecutor", description="Movie Pipeline 执行器类")

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

    class Config:
        env_file = ".env"

settings = Settings() # pyright: ignore[reportCallIssue] - BaseSettings reads values from env/.env at runtime
settings.DATA_ROOT.mkdir(parents=True, exist_ok=True)
settings.LOG_ROOT.mkdir(parents=True, exist_ok=True)