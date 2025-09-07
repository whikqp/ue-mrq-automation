from pydantic import BaseModel, Field
from typing import Literal, Optional, Dict, Any
from .status import JobStatus

class CreateJobRequest(BaseModel):
    template_id: str
    params: Dict[str, Any]
    quality: Literal["LOW","MEDIUM","HIGH","EPIC"] = "HIGH"
    format: Literal["mp4","mov"] = "mp4"
    session_id: Optional[str] = None

class Progress(BaseModel):
    percent: float = 0.0
    eta_seconds: int | None = None

class JobResponse(BaseModel):
    session_id: Optional[str]
    job_id: str
    status: JobStatus
    progress: Progress | None = None
    artifacts: dict | None = None
    template_id: str
    timestamps: dict | None = None
    params: Optional[Dict[str, Any]] = None

class UEJobResponse(BaseModel):
    """
    Response model for Unreal Engine requests.
    """
    job_id: str
    artifacts: dict | None = None
    payload: Optional[Dict[str, Any]] = None

class CancelResponse(BaseModel):
    session_id: Optional[str]
    job_id: str
    status: JobStatus
    message: str

class JobNoParamsResponse(BaseModel):
    """
    Response model for jobs without parameters.
    """
    session_id: Optional[str]
    job_id: str
    status: JobStatus
    progress: Progress | None = None
    artifacts: dict | None = None
    template_id: str
    timestamps: dict | None = None

class JobParamsResponse(BaseModel):
    """
    Response model for jobs with parameters.
    """
    params: Optional[Dict[str, Any]] = None