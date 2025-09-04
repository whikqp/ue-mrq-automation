import json, uuid, math
from datetime import datetime
from typing import Optional, Union
from fastapi import APIRouter, HTTPException, Depends, Header
from sqlalchemy.orm import Session
from sqlalchemy import select
from ..db.database import session_scope
from ..db.models import Job
from ..models.schemas import CreateJobRequest, JobResponse, Progress, CancelResponse, UEJobResponse, JobNoParamsResponse, JobParamsResponse
from ..models.status import JobStatus
from ..utils.time import to_cn_iso
from ..deps import get_registry
from ..templates.loader import TemplateRegistry

router = APIRouter(prefix="/jobs", tags=["jobs"])

@router.post("")
async def create_job(req: CreateJobRequest, registry: TemplateRegistry = Depends(get_registry)):
    tpl = registry.get(req.template_id)
    if not tpl:
        raise HTTPException(status_code=400, detail={"code":"TEMPLATE_NOT_FOUND","message":"unknown template_id"})
    
    job_id = str(uuid.uuid4())
    
    try:
        payload = json.dumps(req.model_dump(), ensure_ascii=False)
    except AttributeError:
        payload = json.dumps(req.dict(), ensure_ascii=False)

    with session_scope() as db:
        job = Job(job_id=job_id, session_id=req.session_id, template_id=req.template_id, payload=payload)
        db.add(job)
        db.commit()
        # 计算队列位置
        pos = db.execute(select(Job).where(Job.status==JobStatus.queued.value).order_by(Job.created_at.asc())).scalars().all()
        queue_pos = next((i for i, j in enumerate(pos) if j.job_id == job_id), len(pos)) + 1
    return {"session_id": req.session_id, "job_id": job_id, "status": JobStatus.queued.value, "queue_position": queue_pos, "template_id": req.template_id}

@router.get("/{job_id}", response_model=Union[JobResponse, UEJobResponse])
async def get_job(job_id: str, x_client: Optional[str] = Header(default=None)):
    with session_scope() as db:
        job = db.get(Job, job_id)
        if not job:
            raise HTTPException(status_code=404, detail={"code":"JOB_NOT_FOUND"})
        
        progress = Progress(percent=job.progress_percent)
        artifacts = None
        if job.artifacts:
            artifacts = {"video_url": job.artifacts.video_url}

        ts = {
            "queued_at": to_cn_iso(job.created_at),
            "started_at": to_cn_iso(job.started_at),
            "updated_at": to_cn_iso(job.updated_at),
        }

        client = (x_client or "").lower()
        if client == "ue5":
            return UEJobResponse(
                job_id=job.job_id,
                artifacts=artifacts,
                payload=json.loads(job.payload) if job.payload else None,
            )
        else:
            return JobResponse(
                session_id=job.session_id, 
                job_id=job.job_id, 
                status=JobStatus(job.status),
                progress=progress, 
                artifacts=artifacts, 
                template_id=job.template_id, 
                timestamps=ts,
                params=json.loads(job.payload).get("params")
            )
        
@router.get("/{job_id}/progress", response_model=JobNoParamsResponse)
async def get_job_progress(job_id: str):
    with session_scope() as db:
        job = db.get(Job, job_id)
        if not job:
            raise HTTPException(status_code=404, detail={"code": "JOB_NOT_FOUND"})
        
        progress = Progress(percent=job.progress_percent)
        artifacts = None
        if job.artifacts:
            artifacts = {"video_url": job.artifacts.video_url}
        
        ts = {
            "queued_at": to_cn_iso(job.created_at),
            "started_at": to_cn_iso(job.started_at),
            "updated_at": to_cn_iso(job.updated_at),
        }

        return JobNoParamsResponse(
            session_id=job.session_id,
            template_id=job.template_id,
            job_id=job.job_id,
            status=JobStatus(job.status),
            progress=progress,
            artifacts=artifacts,
            timestamps=ts
        )

@router.get("/{job_id}/params", response_model=JobParamsResponse)
async def get_job_params(job_id: str):
    with session_scope() as db:
        job = db.get(Job, job_id)
        if not job:
            raise HTTPException(status_code=404, detail={"code": "JOB_NOT_FOUND"})
        
        params_obj = None
        if job.payload:
            params_obj = json.loads(job.payload).get("params")

        return JobParamsResponse(params=params_obj)
    

@router.post("/{job_id}/cancel", response_model=CancelResponse)
async def cancel_job(job_id: str):
    from ..utils.procs import kill_tree
    with session_scope() as db:
        job = db.get(Job, job_id)
        if not job:
            raise HTTPException(status_code=404, detail={"code":"JOB_NOT_FOUND"})
        if job.status_enum in (JobStatus.completed, JobStatus.failed, JobStatus.canceled):
            raise HTTPException(status_code=400, detail={"code":"JOB_NOT_CANCELABLE"})
        
        job.status = JobStatus.canceling.value
        db.commit()
        if job.pid:
            kill_tree(job.pid)
        job.status = JobStatus.canceled.value
        db.commit()
        return CancelResponse(session_id=job.session_id, job_id=job.job_id, status=JobStatus(job.status), message="cancellation requested")