import logging
from .logging_formatters import ColorFormatter, DIM, RESET, CYAN


def setup_logging(level: int = logging.INFO) -> None:
    """Configure root logger with the same colored format used by uvicorn.

    Safe to call multiple times: if handlers already exist on root, do nothing.
    """
    root = logging.getLogger()
    if root.handlers:
        return

    handler = logging.StreamHandler()
    fmt = (
        f"{DIM}%(asctime)s{RESET} | %(levelname_colored)s {CYAN}%(src_module)s:%(src_lineno)d{RESET} - %(message)s"
    )
    handler.setFormatter(ColorFormatter(fmt=fmt, datefmt="%Y-%m-%d %H:%M:%S"))
    root.setLevel(level)
    root.addHandler(handler)


logger = logging.getLogger("ue_mrq_server")
