import inspect
import logging
from typing import Optional, Tuple


_THIS_MODULE = __name__


def _caller_info() -> Tuple[str, int, int]:
    """Find first frame outside this module.

    Returns (short_module_name, lineno, wrappers_count).
    wrappers_count is the number of functions within this module on the call
    stack above the user call. Use stacklevel = wrappers_count + 1 when
    calling logger.log().
    """
    f = inspect.currentframe()
    # Step into our caller (log -> maybe info/debug -> user)
    if f is not None:
        f = f.f_back
    wrappers = 1  # at least one wrapper: log()
    while f is not None:
        mod = inspect.getmodule(f)
        modname = mod.__name__ if mod and hasattr(mod, "__name__") else ""
        if modname == _THIS_MODULE:
            wrappers += 1
            f = f.f_back
            continue
        short = modname.rsplit(".", 1)[-1] if modname else "app"
        lineno = f.f_lineno
        return short, lineno, wrappers
    return "app", 0, wrappers


def _ensure_logging_configured() -> None:
    # If no handlers on root, try to initialize our unified logging once.
    if not logging.getLogger().handlers:
        try:
            from .logging import setup_logging  # lazy import to avoid cycles

            setup_logging()
        except Exception:
            pass


def log(
    msg: str,
    *args,
    level: int = logging.INFO,
    category: Optional[str] = None,
    logger: Optional[logging.Logger] = None,
    **kwargs,
) -> None:
    """Lightweight logging helper with correct caller attribution.

    - Uses caller module name as logger (short form, e.g., "jobs").
    - Preserves the caller's file/line via stacklevel so the formatter shows
      "<timestamp> | <LEVEL> <module>:<lineno> - <message>".
    - Optional category is prefixed as "[Category] " to the message.
    """
    _ensure_logging_configured()

    if logger is None:
        name, src_lineno, wrappers = _caller_info()
        logger = logging.getLogger(name)
    else:
        # If logger is explicitly passed, still compute wrappers so we can set stacklevel
        _, src_lineno, wrappers = _caller_info()

    if category:
        msg = f"[{category}] {msg}"

    # Python 3.8+: logging supports stacklevel to attribute to caller site.
    try:
        # Inject caller info for stable formatting, and set stacklevel so
        # built-in attributes (e.g., pathname) are close to the user site.
        extra = {"src_module": logger.name, "src_lineno": src_lineno}
        logger.log(level, msg, *args, stacklevel=wrappers + 1, extra=extra, **kwargs)
    except TypeError:
        # Fallback for older Pythons without stacklevel support
        extra = {"src_module": logger.name, "src_lineno": src_lineno}
        logger.log(level, msg, *args, extra=extra, **kwargs)


def debug(msg: str, *args, **kwargs) -> None:
    log(msg, *args, level=logging.DEBUG, **kwargs)


def info(msg: str, *args, **kwargs) -> None:
    log(msg, *args, level=logging.INFO, **kwargs)


def warning(msg: str, *args, **kwargs) -> None:
    log(msg, *args, level=logging.WARNING, **kwargs)


def error(msg: str, *args, **kwargs) -> None:
    log(msg, *args, level=logging.ERROR, **kwargs)


def critical(msg: str, *args, **kwargs) -> None:
    log(msg, *args, level=logging.CRITICAL, **kwargs)


__all__ = [
    "log",
    "debug",
    "info",
    "warning",
    "error",
    "critical",
]

