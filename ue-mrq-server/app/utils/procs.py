import psutil, os, signal, subprocess, sys
from pathlib import Path
from typing import Sequence

CREATE_NO_WINDOW = 0x08000000 if sys.platform == 'win32' else 0


def popen(
        cmd: Sequence[str],
        cwd: str | None = None,
        env: dict | None = None,
        log_path: Path | None = None,
    ) -> psutil.Popen:
    """
    Start a process without piping stdio; child handles its own logging.

    Logging should be configured via the child process arguments (e.g. UE
    with "-stdout" and "ABSLOG=<path>"). We keep the `log_path` parameter for
    API compatibility but do not capture stdout/stderr here to avoid missing
    lines or pipe-related issues.
    """

    # Ensure log directory exists if a path is provided (child writes to it)
    if log_path is not None:
        try:
            os.makedirs(os.fspath(Path(log_path).parent), exist_ok=True)
        except Exception:
            pass

    return psutil.Popen(
        cmd,
        cwd=cwd,
        env=env,
        creationflags=CREATE_NO_WINDOW,
    )


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
