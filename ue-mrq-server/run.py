import uvicorn
import logging
from app.utils.logging_formatters import (
    ColorFormatter,
    ColorAccessFormatter,
    DIM,
    RESET,
    CYAN,
    YELLOW,
)

# Colorized formats with our non-padded colored level name
DEFAULT_FMT = (
    f"{DIM}%(asctime)s{RESET} | %(levelname_colored)s {CYAN}%(src_module)s:%(src_lineno)d{RESET} - %(message)s"
)
ACCESS_FMT = (
    f"{DIM}%(asctime)s{RESET} | %(levelname_colored)s {YELLOW}%(client_addr)s{RESET} - \"%(request_line)s\" %(status_code)s"
)

log_config = {
    "version": 1,
    "disable_existing_loggers": False,
    "formatters": {
        "default": {
            "()": "app.utils.logging_formatters.ColorFormatter",
            "format": DEFAULT_FMT,
            "datefmt": "%Y-%m-%d %H:%M:%S",
        },
        "access": {
            "()": "app.utils.logging_formatters.ColorAccessFormatter",
            "format": ACCESS_FMT,
            "datefmt": "%Y-%m-%d %H:%M:%S",
        },
    },
    "handlers": {
        "default": {
            "formatter": "default",
            "class": "logging.StreamHandler",
            "stream": "ext://sys.stdout",
        },
        "access": {
            "formatter": "access",
            "class": "logging.StreamHandler",
            "stream": "ext://sys.stdout",
        },
        "console": {
            "class": "logging.StreamHandler",
            "formatter": "default",
        },
    },
    "loggers": {
        "uvicorn": {"handlers": ["default"], "level": "INFO", "propagate": False},
        "uvicorn.error": {"handlers": ["default"], "level": "INFO", "propagate": False},
        "uvicorn.access": {"handlers": ["access"], "level": "INFO", "propagate": False},
        # Silence reload watcher info noise; allow warnings+ if needed
        "watchfiles": {"level": "WARNING", "propagate": False, "handlers": []},
        "watchfiles.main": {"level": "WARNING", "propagate": False, "handlers": []},
    },
}

if __name__ == "__main__":
    uvicorn.run("app.main:app", host="0.0.0.0", port=8080, reload=True, 
                access_log=True, log_config=log_config)
