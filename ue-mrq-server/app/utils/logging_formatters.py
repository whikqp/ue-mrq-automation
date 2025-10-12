import logging
import os
import sys

try:
    # Optional: helps ANSI on Windows legacy consoles; VS Code terminal already supports ANSI.
    import colorama  # type: ignore

    colorama.just_fix_windows_console()
except Exception:
    pass


# Toggle ANSI colors based on TTY and env var
USE_COLOR = (
    os.getenv("LOG_COLOR", "1").lower() not in {"0", "false", "no"}
    and hasattr(sys.stdout, "isatty")
    and sys.stdout.isatty()
)


def code(s: str) -> str:
    return s if USE_COLOR else ""


# Basic ANSI color codes
RESET = code("\x1b[0m")
DIM = code("\x1b[90m")
BLUE = code("\x1b[34m")
CYAN = code("\x1b[36m")
GREEN = code("\x1b[32m")
YELLOW = code("\x1b[33m")
RED = code("\x1b[31m")
MAGENTA = code("\x1b[35m")


def color_for_level(levelno: int) -> str:
    if not USE_COLOR:
        return ""
    if levelno >= logging.CRITICAL:
        return MAGENTA
    if levelno >= logging.ERROR:
        return RED
    if levelno >= logging.WARNING:
        return YELLOW
    if levelno >= logging.INFO:
        return GREEN
    return BLUE  # DEBUG & below


class ColorFormatter(logging.Formatter):
    """Formatter that adds colored level name and normalized logger name.

    Adds:
      - levelname_colored: colored, non-padded level name (no trailing spaces)
      - logger_name: normalized name (maps 'uvicorn.error' -> 'uvicorn')
    """

    def __init__(self, fmt: str | None = None, datefmt: str | None = None):
        super().__init__(fmt=fmt, datefmt=datefmt)

    def format(self, record: logging.LogRecord) -> str:
        # Non-padded level text with color
        level_color = color_for_level(record.levelno)
        record.levelname_colored = f"{level_color}{record.levelname}{RESET}"

        # Normalize uvicorn.error -> uvicorn to avoid confusion
        record.logger_name = (
            "uvicorn" if record.name == "uvicorn.error" else record.name
        )

        # Provide src_module/src_lineno used by our format strings,
        # falling back to logger name and record.lineno when not provided.
        if not hasattr(record, "src_module"):
            record.src_module = record.logger_name.rsplit(".", 1)[-1]
        if not hasattr(record, "src_lineno"):
            record.src_lineno = record.lineno

        return super().format(record)


try:
    # Only import uvicorn at runtime if available
    from uvicorn.logging import AccessFormatter as UvicornAccessFormatter  # type: ignore
except Exception:  # pragma: no cover - for environments without uvicorn
    UvicornAccessFormatter = logging.Formatter  # type: ignore


class ColorAccessFormatter(UvicornAccessFormatter):
    """Access formatter that preserves uvicorn's status code coloring
    and injects our colored, non-padded level name and normalized logger name.
    """

    def __init__(self, fmt: str | None = None, datefmt: str | None = None):
        super().__init__(fmt=fmt, datefmt=datefmt)

    def format(self, record: logging.LogRecord) -> str:
        level_color = color_for_level(record.levelno)
        record.levelname_colored = f"{level_color}{record.levelname}{RESET}"
        record.logger_name = (
            "uvicorn" if record.name == "uvicorn.access" else record.name
        )
        if not hasattr(record, "src_module"):
            record.src_module = record.logger_name.rsplit(".", 1)[-1]
        if not hasattr(record, "src_lineno"):
            record.src_lineno = record.lineno
        return super().format(record)
