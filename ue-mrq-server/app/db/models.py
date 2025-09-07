from sqlalchemy import String, Integer, Float, Text, ForeignKey, DateTime
from sqlalchemy.orm import Mapped, mapped_column, relationship, validates
from .database import Base
from ..models.status import JobStatus
from datetime import datetime
from ..utils.time import now_cn

class Job(Base):
    __tablename__ = "jobs"
    job_id: Mapped[str] = mapped_column(String(36), primary_key=True)
    session_id: Mapped[str | None] = mapped_column(String(64))
    template_id: Mapped[str] = mapped_column(String(64))

    status: Mapped[str] = mapped_column(String(24), default=JobStatus.queued.value)
    

    payload: Mapped[str] = mapped_column(Text)  # JSON string

    progress_percent: Mapped[float] = mapped_column(Float, default=0.0)
    progress_eta_seconds: Mapped[int | None] = mapped_column(Integer, default=None)

    pid: Mapped[int | None] = mapped_column(Integer)

    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=now_cn)
    updated_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=now_cn, onupdate=now_cn)
    started_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), default=None)
    ended_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), default=None)

    artifacts = relationship("JobArtifact", back_populates="job", cascade="all, delete-orphan", uselist=False)

    @validates("status")
    def _validate_status(self, key: str, val: str) -> str:
        if val not in JobStatus._value2member_map_:
            raise ValueError(f"Invalid status: {val}")
        return val
    
    @property
    def status_enum(self) -> JobStatus:
        return JobStatus(self.status)

class JobArtifact(Base):
    __tablename__ = "job_artifacts"
    job_id: Mapped[str] = mapped_column(ForeignKey("jobs.job_id"), primary_key=True)
    video_path: Mapped[str | None] = mapped_column(String(512))
    video_url: Mapped[str | None] = mapped_column(String(1024))
    ue_log: Mapped[str | None] = mapped_column(String(512))
    ffmpeg_log: Mapped[str | None] = mapped_column(String(512))

    job = relationship("Job", back_populates="artifacts")