#!/bin/bash
# Tests of check_build_paths.sh, the gate build-app.sh runs on the release binary: a binary that still names the
# folder it was built in (or the builder's home) must fail, a clean one must pass.
#   scripts/test_check_build_paths.sh            # exit 0 when every case passes
set -euo pipefail
cd "$(dirname "$0")"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
FAILS=0
expect() {  # expect <0|1> <name> <args...>
  local want="$1" name="$2"; shift 2
  if ./check_build_paths.sh "$@" >/dev/null 2>&1; then got=0; else got=1; fi
  if [[ "$got" == "$want" ]]; then echo "ok   $name"; else echo "FAIL $name (exit $got, want $want)"; FAILS=$((FAILS + 1)); fi
}
root="$T/some build root/cardputer-assistant"
home="$T/home of builder"
printf 'a\0b\0Sources/CardputerCore/X.swift\0' >"$T/clean"
printf 'a\0%s/mac/Sources/CardputerCore/X.swift\0' "$root" >"$T/root"
printf 'a\0%s/Library/Caches/x\0' "$home" >"$T/home"
expect 0 "a binary with relative paths passes" "$T/clean" "$root" "$home"
expect 1 "a binary naming the build root fails" "$T/root" "$root" "$home"
expect 1 "a binary naming the builder's home fails" "$T/home" "$root" "$home"
expect 1 "a missing binary fails" "$T/nope" "$root" "$home"
expect 1 "no root given fails" "$T/clean"
[[ "$FAILS" == 0 ]] && echo "all passed" || { echo "$FAILS failed"; exit 1; }
