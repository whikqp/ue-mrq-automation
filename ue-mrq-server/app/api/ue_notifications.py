from fastapi import APIRouter, BackgroundTasks, Request
from sqlalchemy.orm import Session
from ..db.database import session_scope
from ..db.models import Job, JobArtifact
from ..models.status import JobStatus
from datetime import datetime
from ..utils.time import now_cn
import json

router = APIRouter(prefix="/ue-notifications", tags=["ue-notifications"])

@router.post("/job/{job_id}/progress")
async def update_job_progress(job_id: str, request: Request):
    """接收UE5推送的渲染进度更新"""
    data = await request.json()

    with session_scope() as db:
        job = db.query(Job).filter(Job.job_id == job_id).first()
        if not job:
            return {"error": "Job not found"}
        
        # 更新进度信息 
        job.progress_percent = data.get("progress_percent", job.progress_percent)
        status_str = data.get("status")
        if status_str is not None:
            try:
                job.status = JobStatus(status_str).value
            except ValueError:
                return {"error": "Invalid status"}
            
        percentage = job.progress_percent * 100 if job.progress_percent is not None else 0
        print(f"JobId {job.job_id} progress: {percentage:.0f}%")
        db.commit()
    
    return {"status": "success"}

@router.post("/job/{job_id}/render-complete")
async def render_complete(job_id: str, request: Request):
    """接收UE5推送的渲染完成通知"""
    data = await request.json()
    print(f"Received data from UE5: {data}")

    with session_scope() as db:
        job = db.query(Job).filter(Job.job_id == job_id).first()
        if not job: 
            return {"error": "Job not found"}
        
        job.status = JobStatus.completed.value
        job.ended_at = now_cn()
        
        created_artifact = False
        if job.artifacts is None:
            job.artifacts = JobArtifact(job_id=job.job_id)
            created_artifact = True
        
        if "video_path" in data:
            job.artifacts.video_path = data["video_path"]

        if created_artifact:
            db.add(job.artifacts)

        db.commit()

    return {"status": "success"}


@router.post("/job/{job_id}/encoding-status")
async def encoding_status(job_id: str, request: Request):
    """接收UE5推送的视频编码状态更新""" 
    data = await request.json()

    with session_scope() as db: 
        job = db.query(Job).filter(Job.job_id == job_id).first()
        if not job:
            return {"error": "Job not found"}
        
        status_str = data.get("status", JobStatus.encoding.value)
        try:
            status = JobStatus(status_str)
        except ValueError:
            return {"error": "Invalid status"}
        
        if status == JobStatus.completed:
            job.progress_percent = 100.0
            job.ended_at = now_cn()

            if "video_url" in data:
                if job.artifacts is None:
                    job.artifacts = JobArtifact(job_id=job.job_id)
                job.artifacts.video_url = data["video_url"]

        db.commit()
        
    return {"status": "success"}