"""Mac daemon (cpad) and CLI (cpa) for Cardputer Assistant.

The version is the installed wheel's (docs/27 §5.3): `uv version X.Y.Z` sets it from the release tag, so every
release is a new wheel to uv's cache (#57) and cpa, cpad and cpa-stt report the same number.
"""

from importlib import metadata


def installed_version() -> str:
    try:
        return metadata.version("cpad")
    except metadata.PackageNotFoundError:
        return "0+unknown"


__version__ = installed_version()
