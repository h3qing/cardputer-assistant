# PlatformIO pre-script for env:sim: `pio run` links the simulator as
# .pio/build/sim/cpa-sim. `pio test` links each test as .pio/build/sim/program, so a test
# run never replaces the binary that tools/sim starts.
# cpa-sim is linked on every `pio run`, also when no object changed (a comment edit), so
# its mtime says it is current: tools/sim's tests refuse a binary older than its sources.
from SCons.Script import COMMAND_LINE_TARGETS  # noqa: F401  (provided by SCons)

Import("env")  # noqa: F821  (provided by SCons)

if "__test" not in COMMAND_LINE_TARGETS:
    env.Replace(PROGNAME="cpa-sim")  # noqa: F821
    env.AlwaysBuild(env.File("$BUILD_DIR/${PROGNAME}${PROGSUFFIX}"))  # noqa: F821
