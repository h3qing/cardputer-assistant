# PlatformIO extra script for env:native: SCons puts --coverage from build_flags into
# CCFLAGS only, so the link step would miss the profile runtime (same as the app's
# test/core/support/coverage_link.py).
Import("env")  # noqa: F821  (provided by SCons)

env.Append(LINKFLAGS=["--coverage"])  # noqa: F821
