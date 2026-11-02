#!/bin/bash
# Shared by scripts/tests/test_*.sh: a sandbox per run (temp HOME, stub launchctl / open / pgrep / lsregister / gh
# that only log their argv, a local file:// "release" built by scripts/release-local.sh), and tiny asserts.
# Never the real HOME: every installer run gets --home and a scrubbed environment (env -i), and the real
# ~/Library/LaunchAgents is compared before and after the whole run.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REAL_HOME="$HOME"
T="$(mktemp -d -t cpa-install-test)"
T="$(cd "$T" && pwd -P)"
CACHE="${TMPDIR:-/tmp}/cpa-install-test-cache"
VERSION="$(uv version --short --project "$ROOT/daemon")"
TAG="v$VERSION"
UV_REAL="$(command -v uv)"
UV_PY_DIR="$(uv python dir)"
FAILS=0
PASSES=0

agents_fingerprint() { ls -la "$REAL_HOME/Library/LaunchAgents" 2>/dev/null; stat -f '%m' "$REAL_HOME/Library/LaunchAgents" 2>/dev/null || true; }
REAL_AGENTS_BEFORE="$(agents_fingerprint)"

cleanup() {
  local after; after="$(agents_fingerprint)"
  if [[ "$after" != "$REAL_AGENTS_BEFORE" ]]; then echo "FATAL: the real ~/Library/LaunchAgents changed"; FAILS=$((FAILS + 1)); fi
  if [[ -n "${KEEP:-}" ]]; then echo "kept: $T"; else chmod -R u+w "$T" 2>/dev/null || true; rm -rf "$T"; fi
  echo
  echo "passed: $PASSES  failed: $FAILS"
  [[ "$FAILS" == 0 ]]
}
trap 'cleanup' EXIT

ok() { PASSES=$((PASSES + 1)); printf '  ok   %s\n' "$*"; }
bad() { FAILS=$((FAILS + 1)); printf '  FAIL %s\n' "$*"; }
check() { local what="$1"; shift; if "$@"; then ok "$what"; else bad "$what"; fi; }
absent() { [[ ! -e "$1" ]]; }
contains() { grep -qF -- "$2" "$1"; }
count_of() { grep -cF -- "$2" "$1" 2>/dev/null || true; }

make_stubs() {
  mkdir -p "$T/stubs" "$T/tmp"
  local name
  for name in launchctl open pgrep lsregister gh ioreg; do
    cat >"$T/stubs/$name" <<EOF
#!/bin/sh
echo "$name \$*" >>"$T/stub.log"
case "$name \$1" in
  "pgrep "*|"gh "*) exit 1 ;;
  "ioreg "*) [ -f "$T/ioreg.out" ] && cat "$T/ioreg.out" ;;
esac
exit 0
EOF
    chmod +x "$T/stubs/$name"
  done
  make_launchctl_stub
  ln -s "$UV_REAL" "$T/stubs/uv"
}

# launchctl that keeps one state file per label under $T/launchd (#82): print answers 113 for a label that is not
# loaded, else "state = running" and its pid; bootstrap refuses (5) while the label is still loaded and gives each
# start a new pid; bootout takes $T/bootout.delay seconds (default 0) to unload. $T/bootstrap.dead: bootstraps
# load the label but run nothing. Never touches the real launchd.
make_launchctl_stub() {
  { printf '#!/bin/sh\nT=%q\n' "$T"; cat <<'EOF2'
echo "launchctl $*" >>"$T/stub.log"
st="$T/launchd"
mkdir -p "$st"
settle() { if [ -f "$st/$1.gone" ] && [ "$(date +%s)" -ge "$(cat "$st/$1.gone")" ]; then rm -f "$st/$1" "$st/$1.gone"; fi; }
case "$1" in
  print)
    l="${2##*/}"; settle "$l"
    [ -f "$st/$l" ] || exit 113
    pid="$(cat "$st/$l")"
    if [ -n "$pid" ]; then printf '%s = {\n\tstate = running\n\tpid = %s\n}\n' "$2" "$pid"
    else printf '%s = {\n\tstate = not running\n}\n' "$2"; fi ;;
  bootout)
    l="${2##*/}"; settle "$l"
    [ -f "$st/$l" ] || exit 3
    echo $(( $(date +%s) + $(cat "$T/bootout.delay" 2>/dev/null || echo 0) )) >"$st/$l.gone"
    settle "$l" ;;
  bootstrap)
    l="$(basename "$3" .plist)"; settle "$l"
    [ -f "$st/$l" ] && exit 5
    if [ -f "$T/bootstrap.dead" ]; then : >"$st/$l"; exit 0; fi
    n="$(cat "$st/next" 2>/dev/null || echo 5000)"
    echo "$n" >"$st/$l"; echo $((n + 1)) >"$st/next" ;;
esac
exit 0
EOF2
  } >"$T/stubs/launchctl"
  chmod +x "$T/stubs/launchctl"
}

launchd_pid() { cat "$T/launchd/$1" 2>/dev/null; } # LABEL: the stub's pid for it, empty when none

make_fake_app() { # DIR: a tiny signed "Cardputer Assistant.app"
  local app="$1/Cardputer Assistant.app"
  mkdir -p "$app/Contents/MacOS"
  printf 'int main(void){return 0;}\n' | cc -x c - -o "$app/Contents/MacOS/CardputerAssistant"
  cat >"$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>com.cardputer-assistant.mac</string>
<key>CFBundleExecutable</key><string>CardputerAssistant</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$VERSION</string>
</dict></plist>
EOF
  codesign --force --sign - --timestamp=none "$app" 2>/dev/null
  printf '%s' "$app"
}

pyserial_wheel() {
  mkdir -p "$CACHE"
  local whl="$CACHE/pyserial-3.5-py2.py3-none-any.whl"
  [[ -s "$whl" ]] || curl -fsSL -o "$whl" "https://files.pythonhosted.org/packages/07/bc/587a445451b253b285629263eb51c2d8e9bcea4fc97826266d186f96f558/pyserial-3.5-py2.py3-none-any.whl"
  printf '%s' "$whl"
}

make_release() { # OUT [app]: the local release install.sh reads through CPA_INSTALL_BASE_URL
  local app="${2:-$(make_fake_app "$T/app-src")}"
  CPA_RELEASE_PYSERIAL="$(pyserial_wheel)" "$ROOT/scripts/release-local.sh" --tag "$TAG" --out "$1" --skip-firmware \
    --app "$app" --release-json --dev >"$T/release-build.log" 2>&1 || { cat "$T/release-build.log"; return 1; }
  printf 'firmware for tests\n' >"$1/cpa-$TAG.bin"
  resum "$1"
}

resum() { # DIR: rewrite SHA256SUMS and release.json after a test changed an asset on purpose
  (cd "$1" && rm -f SHA256SUMS release.json && shasum -a 256 -- * >SHA256SUMS)
  local f first=1
  {
    printf '{"tag_name": "%s", "target_commitish": "0123456789abcdef0123456789abcdef01234567", "assets": [' "$TAG"
    for f in "$1"/*; do
      [[ "$first" == 1 ]] || printf ','
      first=0
      printf '\n {"name": "%s", "digest": "sha256:%s"}' "$(basename "$f")" "$(shasum -a 256 "$f" | awk '{print $1}')"
    done
    printf ']}\n'
  } >"$1/release.json.tmp"
  mv "$1/release.json.tmp" "$1/release.json"
}

# run_script SCRIPT HOME RELEASE [args...]: the script in a scrubbed environment; output in $T/out.log.
# TEST_BASE=none: no CPA_INSTALL_BASE_URL (gh / token / public access, or --from).
run_script() {
  local script="$1" home="$2" rel="$3"; shift 3
  mkdir -p "$home"
  local base=()
  [[ "${TEST_BASE:-}" == none ]] || base=(CPA_INSTALL_BASE_URL="${TEST_BASE:-file://$rel}")
  env -i HOME="$home" USER="$USER" LOGNAME="$USER" SHELL=/bin/zsh LANG=en_US.UTF-8 TMPDIR="$T/tmp/" \
    PATH="${TEST_PATH:-$T/stubs:/usr/bin:/bin:/usr/sbin:/sbin}" \
    ${base[@]+"${base[@]}"} CPA_INSTALL_LSREGISTER="$T/stubs/lsregister" \
    CPA_INSTALL_TTY="${TEST_TTY:-$T/no-tty}" UV_PYTHON_INSTALL_DIR="$UV_PY_DIR" UV_PYTHON_DOWNLOADS=never \
    UV_CACHE_DIR="$T/uv-cache" CPA_INSTALL_RECHECK_S="${TEST_RECHECK_S:-0}" ${TEST_ENV:-} \
    /bin/bash "$script" --home "$home" "$@" >"$T/out.log" 2>&1 || return
  if grep -qE 'unbound variable|command not found|syntax error' "$T/out.log"; then
    bad "$(basename "$script") hit a shell error"; show_log; return 1
  fi
}

run_install() { run_script "$ROOT/scripts/install.sh" "$@"; }
run_uninstall() { local home="$1"; shift; run_script "$ROOT/scripts/uninstall.sh" "$home" "$T/none" "$@"; }

tree_of() { (cd "$1" && find . -path ./.cache -prune -o -print | LC_ALL=C sort); }
show_log() { sed 's/^/      | /' "$T/out.log" | tail -"${1:-15}"; }
