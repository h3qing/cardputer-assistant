# PlatformIO extra script for env:native. SCons ParseFlags puts --coverage from
# build_flags into CCFLAGS only, so the link step misses the gcov runtime
# (libgcov on GCC, libclang_rt.profile on clang). Add it to LINKFLAGS as well.
Import("env")  # noqa: F821  (provided by SCons)

env.Append(LINKFLAGS=["--coverage"])  # noqa: F821
