#!/bin/bash
# Fails when a built binary still contains an absolute path of the machine that built it: the build root (the
# checkout) or the builder's home. A release must not carry the builder's user name or folder layout.
#   scripts/check_build_paths.sh BINARY ROOT [HOME]
set -euo pipefail
bin="${1:?usage: check_build_paths.sh BINARY ROOT [HOME]}"
[[ -f "$bin" ]] || { echo "check_build_paths: no such file: $bin" >&2; exit 1; }
shift
[[ $# -gt 0 ]] || { echo "check_build_paths: give the build root (and the builder's home)" >&2; exit 1; }
for path in "$@"; do
  [[ -n "$path" ]] || continue
  if LC_ALL=C grep -a -q -F -- "$path" "$bin"; then
    echo "check_build_paths: $bin names the build machine's path $path" >&2
    exit 1
  fi
done
