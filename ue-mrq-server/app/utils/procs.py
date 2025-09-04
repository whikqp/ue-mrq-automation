import psutil, os, signal, subprocess, sys
from pathlib import Path
from typing import Sequence

CREATE_NO_WINDOW = 0x08000000 if sys.platform == 'win32' else 0


def popen(
        cmd: Sequence[str], 
        cwd: str | None = None, 
        env: dict | None = None, 
        log_path: Path | None = None
    ) -> psutil.Popen:
    
    stdout = subprocess.PIPE if log_path is None else open(log_path, 'ab')
    stderr = subprocess.STDOUT

    # 不传递stdout参数，否则子进程会通过标准输出抢占UE-cmd.exe写日志，造成写冲突
    return psutil.Popen(cmd, cwd=cwd, env=env, stderr=stderr, creationflags=CREATE_NO_WINDOW)


def kill_tree(pid: int, sig=signal.SIGTERM, timeout: float = 5.0) -> None:
    try:
        proc = psutil.Process(pid)
    except psutil.NoSuchProcess:
        return
    children = proc.children(recursive=True)
    for p in children:
        try: p.send_signal(sig)
        except Exception: pass
    try: proc.send_signal(sig)
    except Exception: pass
    gone, alive = psutil.wait_procs([proc, *children], timeout=timeout)
    for p in alive:
        try: p.kill()
        except Exception: pass