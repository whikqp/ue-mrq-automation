from __future__ import annotations
import json
import threading, time
from sqlalchemy import select
from sqlalchemy.orm import Session
from ..db.database import session_scope
from ..db.models import Job
from ..models.status import RUNNING_STATUSES, JobStatus
from ..templates.loader import TemplateRegistry
from ..config import settings
from .gpu import query_gpu0
from ..runner.runner import run_job

class Scheduler:
    def __init__(self, registry: TemplateRegistry):
        self.registry = registry
        self._th: threading.Thread | None = None
        self._stop = threading.Event()
        self._workers: list[threading.Thread] = []

    def start(self):
        if self._th and self._th.is_alive():
            return
        self._stop.clear()
        self._th = threading.Thread(target=self._loop, daemon=True)
        self._th.start()

    def stop(self):
        self._stop.set()
        if self._th:
            self._th.join(timeout=1)

    def _loop(self):
        while not self._stop.is_set():
            try:
                self._tick()
            except Exception as e:
                # TODO: add logging
                pass
            self._stop.wait(settings.SCHEDULER_POLL_MS / 1000)

    def _tick(self):
        # check GPU memory
        gpu = query_gpu0()
        if gpu and gpu.free_mb < settings.MIN_FREE_VRAM_MB:
            print(f"GPU memory is low: {gpu.free_mb}MB free")
            return
        with session_scope() as db:
            running = db.execute(select(Job).where(Job.status.in_(list(RUNNING_STATUSES))) ).scalars().all()
            if len(running) >= settings.MAX_CONCURRENCY:
                num_running = len(running)
                num_max = settings.MAX_CONCURRENCY
                print(f"Max concurrency reached. Current running jobs {num_running} MAX_CONCURRENCY={num_max}")
                return
            
            job = db.execute(select(Job).where(Job.status==JobStatus.queued.value).order_by(Job.created_at.asc())).scalars().first()
            if not job:
                return
            template = self.registry.get(job.template_id)
            if not template:
                job.status = JobStatus.failed.value; db.commit(); return

            # set job status to starting, to avoid concurrency window competition
            job.status = JobStatus.starting.value
            db.commit()

            # running job in background thread
            def _worker(job_id: str, tpl: dict):
                from ..db.database import session_scope as _session_scope
                from ..db.models import Job as _Job
                try:
                    with _session_scope() as _db:
                        _job = _db.query(_Job).filter(_Job.job_id == job_id).first()
                        if not _job:
                            return
                        run_job(_db, _job, tpl)
                except Exception:
                    pass

            th = threading.Thread(target=_worker, args=(job.job_id, template), daemon=True)
            th.start()
            self._workers.append(th)
