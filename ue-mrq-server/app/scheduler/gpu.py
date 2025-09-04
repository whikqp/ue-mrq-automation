from __future__ import annotations
from dataclasses import dataclass

try:
    import pynvml
    pynvml.nvmlInit()
    _NVML_OK = True
except Exception:
    _NVML_OK = False

@dataclass
class GpuStatus:
    total_mb: int
    used_mb: int
    free_mb: int


def query_gpu0() -> GpuStatus | None:
    if not _NVML_OK:
        return None
    h = pynvml.nvmlDeviceGetHandleByIndex(0)
    mem = pynvml.nvmlDeviceGetMemoryInfo(h)
    total_mb = int(mem.total) // (1024 *1024)
    used_mb = int(mem.used) // (1024 *1024)
    free_mb = int(mem.free) // (1024 *1024)

    return GpuStatus(total_mb=total_mb, used_mb=used_mb, free_mb=free_mb)