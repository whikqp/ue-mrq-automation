from __future__ import annotations
from enum import Enum

class JobStatus(str, Enum):
    queued = "queued"
    starting = "starting"
    rendering = "rendering"
    encoding = "encoding"
    uploading = "uploading"
    completed = "completed"
    failed = "failed"
    canceling = "canceling"
    canceled = "canceled"

RUNNING_STATUSES = {
    JobStatus.starting.value,
    JobStatus.rendering.value,
    JobStatus.encoding.value,
}

TERMINAL_STATUSES = {
    JobStatus.completed.value,
    JobStatus.failed.value,
    JobStatus.canceled.value,
}

__all__ = ["JobStatus", "RUNNING_STATUSES", "TERMINAL_STATUSES"]