"""Log setup (05 §7.1, §7.9): JSON lines in $CPA_HOME/logs/cpad.log, 1 MB x 5, redacted by default."""

from __future__ import annotations

import logging
import sys
from logging.handlers import RotatingFileHandler
from pathlib import Path

from cpad.logging.redact import JsonFormatter, RedactFilter

LOGGER = "cpad"


def configure(log_dir: Path, content: bool) -> logging.Logger:
    """content=True (CPA_LOG_CONTENT=1) logs bodies verbatim and warns on stderr."""
    logger = logging.getLogger(LOGGER)
    reset(logger)
    log_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
    handler = RotatingFileHandler(log_dir / "cpad.log", maxBytes=1_000_000, backupCount=5, encoding="utf-8")
    handler.setFormatter(JsonFormatter())
    if not content:
        handler.addFilter(RedactFilter())
    logger.addHandler(handler)
    logger.setLevel(logging.INFO)
    logger.propagate = False
    (log_dir / "cpad.log").chmod(0o600)
    if content:
        print("warning: CPA_LOG_CONTENT=1, note and prompt text is written to the log", file=sys.stderr)
        logger.warning("content logging enabled (CPA_LOG_CONTENT=1)")
    return logger


def reset(logger: logging.Logger) -> None:
    for handler in tuple(logger.handlers):
        handler.close()
        logger.removeHandler(handler)
