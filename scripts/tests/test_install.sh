#!/bin/bash
# End-to-end tests of scripts/install.sh and scripts/uninstall.sh in a sandbox (docs/27 §5; plan cases 1-12, review fixes):
# a temp HOME, stub launchctl / open / pgrep / lsregister, and a file:// release that scripts/release-local.sh
# builds from this checkout (the real wheel, pyserial pinned by uv.lock, a tiny signed stand-in app).
#
#   scripts/tests/test_install.sh            # every case; exit 0 when all pass
#   scripts/tests/test_install.sh fresh dry  # only these
#
# Needs: macOS, uv, cc and codesign (Command Line Tools), network once for the pyserial wheel (then cached).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

AGENT_PLIST="Library/LaunchAgents/com.cardputer-assistant.cpad.plist"
APP_DIR="Applications/Cardputer Assistant.app"
CPA_HOME_REL="Library/Application Support/CardputerAssistant"

case_fresh() {
  echo "fresh install, then again (idempotent)"
  local h="$T/h-fresh"
  run_install "$h" "$REL" --yes || { bad "install exits 0"; show_log 30; return; }
  ok "install exits 0"
  check "cpa is $VERSION" [ "$("$h/.local/bin/cpa" --version)" == "cpa $VERSION" ]
  check "cpa-stt is $VERSION" [ "$("$h/.local/bin/cpa-stt" --version)" == "cpa-stt $VERSION" ]
  check "the app is in ~/Applications" test -x "$h/$APP_DIR/Contents/MacOS/CardputerAssistant"
  check "the app is not quarantined" absent_quarantine "$h/$APP_DIR"
  check "the LaunchAgent runs this cpa's Python" plist_points_into "$h/$AGENT_PLIST" "$h/.local/share/uv/tools/cpad"
  check "launchctl bootstrap was asked for (stub)" contains "$T/stub.log" "launchctl bootstrap gui/"
  check "the app was opened (stub)" contains "$T/stub.log" "open $h/$APP_DIR"
  check "one PATH line in ~/.zprofile" [ "$(count_of "$h/.zprofile" "# cardputer-assistant: PATH")" == 1 ]
  check "the version line names the tag" contains "$T/out.log" "Cardputer Assistant $TAG"
  check "it prints the setup checklist: only what is left" contains "$T/out.log" "To do"
  check "next steps say how to register a project" contains "$T/out.log" "cpa project add"
  check "next steps say the app is in the menu bar" contains "$T/out.log" "menu bar"
  check "no line of only spaces in the last screen" no_blank_indent "$T/out.log"
  check "a test hook in the environment is called out" contains "$T/out.log" "test hook: CPA_INSTALL_BASE_URL"
  check "English by default (LANG=en_US)" contains "$T/out.log" "[1/9] Checking this Mac"
  check "and no Chinese in the installer's own lines" no_han_own "$T/out.log"
  check "writes only where the plan said" only_expected "$h"
  check "no temp files left" [ -z "$(ls -A "$T/tmp" | grep '^cpa-install' || true)" ]
  local before; before="$(tree_of "$h")"
  cp "$h/$CPA_HOME_REL/config.json" "$T/config.first"
  run_install "$h" "$REL" --yes || { bad "second install exits 0"; show_log 30; return; }
  ok "second install exits 0"
  check "the same files after a second run" [ "$before" == "$(tree_of "$h")" ]
  check "config.json unchanged by the second run" cmp -s "$T/config.first" "$h/$CPA_HOME_REL/config.json"
  check "still one PATH line" [ "$(count_of "$h/.zprofile" "# cardputer-assistant: PATH")" == 1 ]
  FRESH_HOME="$h"
}

case_service_race() {
  echo "an upgrade over a running cpad whose bootout is slow (#82): wait, verify the new pid, re-check"
  local h="$T/h-race" label="com.cardputer-assistant.cpad" old new
  run_install "$h" "$REL" --yes || { bad "first install exits 0"; show_log 30; return; }
  old="$(launchd_pid "$label")"
  check "the first install started cpad (stub pid)" [ -n "$old" ]
  echo 2 >"$T/bootout.delay"
  : >"$T/stub.log"
  TEST_RECHECK_S=1 run_install "$h" "$REL" --yes || { bad "upgrade exits 0"; show_log 30; return; }
  rm -f "$T/bootout.delay"
  new="$(launchd_pid "$label")"
  check "cpad is loaded after the upgrade, with a new pid" test -n "$new" -a "$new" != "$old"
  check "bootstrap was not refused: only one, after the old label was gone" \
    [ "$(count_of "$T/stub.log" "launchctl bootstrap gui/")" == 1 ]
  check "cpa reported the verified pid" contains "$T/out.log" "cpad running: pid $new"
  check "step 9 says it runs" contains "$T/out.log" "✓ cpad is running"
  check "and re-checked cpa status a moment later" contains "$T/out.log" "1 s later"
}

case_service_dead() {
  echo "a bootstrap that starts nothing: step 9 does not claim cpad runs"
  local h="$T/h-dead"
  : >"$T/bootstrap.dead"
  run_install "$h" "$REL" --yes || { bad "install exits 0"; show_log 30; rm -f "$T/bootstrap.dead"; return; }
  rm -f "$T/bootstrap.dead"
  ok "install exits 0"
  check "no ✓ for cpad" bash -c '! grep -qF "✓ cpad is running" "$1"' _ "$T/out.log"
  check "says cpad did not start" contains "$T/out.log" "cpad did not start"
  check "and how to try again" contains "$T/out.log" "cpa service install"
  check "it tried twice" [ "$(count_of "$T/stub.log" "launchctl bootstrap gui/")" == 2 ]
}

# The installer's own lines: "==>" headings, notes, red lines, errors and next steps; not what cpa prints (the
# checklist after "Done", the lines of cpa service install), which is cpa's to translate.
own_lines() {
  sed $'s/\033\\[[0-9;]*m//g' "$1" \
    | awk '/^==> (Done|好了)/ { skip = 1; print; next } /^(Next steps|接下来)/ { skip = 0 }
      !skip && /^(==>|    |Error|错误|Next steps|接下来)/'
}
has_han() { /usr/bin/perl -CSD -ne '$f = 1 if /\p{Han}/; END { exit !$f }'; }
no_han_own() { ! own_lines "$1" | has_han; }
# Every heading of both scripts in English, and their commonest notes.
EN_LINES='Checking this Mac|The plan|Reaching GitHub|Finding the release|Downloading and verifying|Getting uv|Installing cpa'
EN_LINES+='|Background service|Done:|Next steps|is running|==> LaunchAgents|not installed|removed|Your data|kept|Uninstalled'
no_english_own() { ! own_lines "$1" | grep -qE "$EN_LINES"; }

absent_quarantine() { local q; q="$(xattr -lr "$1" 2>/dev/null || true)"; [[ "$q" != *com.apple.quarantine* ]]; }
no_blank_indent() { ! grep -qE '^ +$' "$1"; }
plist_points_into() { /usr/libexec/PlistBuddy -c 'Print :ProgramArguments:0' "$1" | grep -qF "$2"; }
only_expected() {
  local extra; extra="$(cd "$1" && ls -A | grep -vxE '\.local|Applications|Library|\.zprofile|\.cache|\.zsh_history' || true)"
  [[ -z "$extra" ]] || { echo "      unexpected: $extra"; return 1; }
  extra="$(cd "$1/Library" && ls -A | grep -vxE 'LaunchAgents|Application Support' || true)"
  [[ -z "$extra" ]] || { echo "      unexpected in Library: $extra"; return 1; }
}

case_checksum() {
  echo "a wrong checksum stops before anything is installed"
  local rel="$T/rel-bad" h="$T/h-bad"
  cp -R "$REL" "$rel"
  printf 'x' >>"$rel/cpad-$VERSION-py3-none-any.whl"   # the bytes changed, release.json still has the old digest
  if run_install "$h" "$rel" --yes; then bad "install refuses"; else ok "install refuses"; fi
  check "says corrupted or tampered" contains "$T/out.log" "corrupted or tampered"
  check "no cpa installed" absent "$h/.local/bin/cpa"
  check "no app installed" absent "$h/Applications"
  check "no LaunchAgent" absent "$h/Library"
  resum "$rel"
  printf '0000000000000000000000000000000000000000000000000000000000000000  cpad-%s-py3-none-any.whl\n' "$VERSION" \
    >"$rel/SHA256SUMS.new"
  grep -v "cpad-" "$rel/SHA256SUMS" >>"$rel/SHA256SUMS.new"
  mv "$rel/SHA256SUMS.new" "$rel/SHA256SUMS"
  local sums; sums="$(shasum -a 256 "$rel/SHA256SUMS" | awk '{print $1}')"
  sed -i '' -E "s/(\"SHA256SUMS\", \"digest\": \"sha256:)[0-9a-f]+/\1$sums/" "$rel/release.json"
  if run_install "$h" "$rel" --yes; then bad "a SHA256SUMS mismatch refuses"; else ok "a SHA256SUMS mismatch refuses"; fi
  check "no cpa installed" absent "$h/.local/bin/cpa"
}

case_quarantine() {
  echo "a quarantined download is refused (AirDrop or a browser would add the flag; curl never does)"
  local rel="$T/rel-q" h="$T/h-q"
  cp -R "$REL" "$rel"
  xattr -w com.apple.quarantine "0081;00000000;Safari;" "$rel/CardputerAssistant-$VERSION-arm64.zip"
  if TEST_BASE="$rel" run_install "$h" "$rel" --yes; then bad "install refuses"; else ok "install refuses"; fi
  check "says quarantined" contains "$T/out.log" "quarantined"
  check "no app installed" absent "$h/$APP_DIR"
  check "no cpa installed" absent "$h/.local/bin/cpa"
}

case_config() {
  echo "an existing config.json keeps every field but spawn_env and claude_bin"
  local h="$T/h-cfg" cfg
  cfg="$h/$CPA_HOME_REL/config.json"
  mkdir -p "$(dirname "$cfg")"
  printf '%s\n' '{"notes_dir": "~/Notes Elsewhere", "device": {"serial": "02:00:00:00:00:01"}, "host_label": "书房", "poll_s": 5, "rec_pull": false}' >"$cfg"
  cp "$cfg" "$T/cfg.before"
  printf 'export FOO=1\n' >"$h/.zprofile"
  run_install "$h" "$REL" --yes || { bad "install exits 0"; show_log 30; return; }
  check "every other field keeps its value and order" same_but_spawn "$T/cfg.before" "$cfg"
  check "the old ~/.zprofile line is kept" contains "$h/.zprofile" "export FOO=1"
  check "notes folder not created by the installer" absent "$h/Notes Elsewhere"
}

same_but_spawn() {
  /usr/bin/python3 -I -c '
import json, sys
a, b = (json.load(open(p)) for p in sys.argv[1:])
drop = lambda d: {k: v for k, v in d.items() if k not in ("spawn_env", "claude_bin")}
sys.exit(0 if drop(a) == drop(b) else f"{drop(a)} != {drop(b)}")' "$1" "$2"
}

case_path_line() {
  echo "a PATH line already there is not added twice"
  local h="$T/h-path"
  mkdir -p "$h"
  printf 'export PATH="%s/.local/bin:$PATH" # cardputer-assistant: PATH\n' "$h" >"$h/.zprofile"
  run_install "$h" "$REL" --yes --no-app || { bad "install exits 0"; show_log; return; }
  check "still one PATH line" [ "$(count_of "$h/.zprofile" "# cardputer-assistant: PATH")" == 1 ]
  check "--no-app installs no app" absent "$h/Applications"
}

case_dry() {
  echo "--dry-run writes nothing"
  local h="$T/h-dry"
  mkdir -p "$h"
  run_install "$h" "$REL" --dry-run || { bad "dry run exits 0"; show_log; return; }
  check "dry run exits 0" true
  check "HOME is still empty" [ -z "$(ls -A "$h")" ]
  check "it names every step" contains "$T/out.log" "(dry-run)"
  check "no launchctl, no open" [ ! -s "$T/stub.log" ]
}

case_spaces() {
  echo "a HOME with spaces"
  local h="$T/home with  spaces"
  run_install "$h" "$REL" --yes || { bad "install exits 0"; show_log 30; return; }
  check "cpa runs" [ "$("$h/.local/bin/cpa" --version)" == "cpa $VERSION" ]
  check "the app is there" test -d "$h/$APP_DIR"
  check "the LaunchAgent is there" test -f "$h/$AGENT_PLIST"
}

case_no_tty() {
  echo "no terminal and no --yes: no LaunchAgent, and it says how; n and y on a terminal"
  local h="$T/h-tty"
  run_install "$h" "$REL" || { bad "install exits 0"; show_log; return; }
  check "no LaunchAgent" absent "$h/$AGENT_PLIST"
  check "prints the command to run later" contains "$T/out.log" "cpa service install"
  printf 'n\n' >"$T/tty-n"
  TEST_TTY="$T/tty-n" run_install "$h" "$REL" || { bad "install with n exits 0"; show_log; return; }
  check "answering n installs no LaunchAgent" absent "$h/$AGENT_PLIST"
  printf 'y\n' >"$T/tty-y"
  TEST_TTY="$T/tty-y" run_install "$h" "$REL" || { bad "install with y exits 0"; show_log; return; }
  check "answering y installs it" test -f "$h/$AGENT_PLIST"
}

case_root() {
  echo "root is refused"
  local h="$T/h-root"
  mkdir -p "$T/root-stubs"
  printf '#!/bin/sh\n[ "$1" = -u ] && { echo 0; exit 0; }\nexec /usr/bin/id "$@"\n' >"$T/root-stubs/id"
  chmod +x "$T/root-stubs/id"
  if TEST_PATH="$T/root-stubs:$T/stubs:/usr/bin:/bin:/usr/sbin:/sbin" run_install "$h" "$REL" --yes; then
    bad "install refuses root"
  else ok "install refuses root"; fi
  check "says not as root" contains "$T/out.log" "root"
  check "nothing written" [ -z "$(ls -A "$h")" ]
}

case_truncated() {
  echo "half a script (a cut download) does nothing"
  local h="$T/h-cut" size
  mkdir -p "$h"
  size="$(wc -c <"$ROOT/scripts/install.sh")"
  head -c $((size / 2)) "$ROOT/scripts/install.sh" >"$T/half.sh"
  run_script "$T/half.sh" "$h" "$REL" --yes || true
  check "HOME is still empty" [ -z "$(ls -A "$h")" ]
  head -c $((size - 20)) "$ROOT/scripts/install.sh" >"$T/almost.sh"
  run_script "$T/almost.sh" "$h" "$REL" --yes || true
  check "all but the last line: HOME is still empty" [ -z "$(ls -A "$h")" ]
}

case_uv_bootstrap() {
  echo "no uv on PATH: the pinned uv archive, checked first"
  local h="$T/h-uv" good="$T/uv-good.tar.gz"
  mkdir -p "$T/uvpkg/uv-aarch64-apple-darwin"
  printf '#!/bin/sh\nexec "%s" "$@"\n' "$UV_REAL" >"$T/uvpkg/uv-aarch64-apple-darwin/uv"
  cp "$T/uvpkg/uv-aarch64-apple-darwin/uv" "$T/uvpkg/uv-aarch64-apple-darwin/uvx"
  chmod +x "$T/uvpkg/uv-aarch64-apple-darwin/"*
  tar -czf "$good" -C "$T/uvpkg" uv-aarch64-apple-darwin
  mkdir -p "$T/stubs-nouv"
  cp "$T/stubs/"{launchctl,open,pgrep,lsregister} "$T/stubs-nouv/"
  local nouv="$T/stubs-nouv:/usr/bin:/bin:/usr/sbin:/sbin"
  if TEST_PATH="$nouv" TEST_ENV="CPA_INSTALL_UV_URL=file://$good CPA_INSTALL_UV_SHA256=$(printf '0%.0s' {1..64})" \
    run_install "$h" "$REL" --yes --no-app; then bad "a wrong uv checksum refuses"; else ok "a wrong uv checksum refuses"; fi
  check "no uv installed" absent "$h/.local/bin/uv"
  TEST_PATH="$nouv" TEST_ENV="CPA_INSTALL_UV_URL=file://$good CPA_INSTALL_UV_SHA256=$(shasum -a 256 "$good" | awk '{print $1}')" \
    run_install "$h" "$REL" --yes --no-app || { bad "install with a fetched uv exits 0"; show_log 30; return; }
  check "uv is in the prefix, marked as ours" test -f "$h/.local/bin/.cardputer-assistant-installed-uv"
  check "cpa installed with it" [ "$("$h/.local/bin/cpa" --version)" == "cpa $VERSION" ]
  UV_HOME="$h"
}

case_bad_prefix() {
  echo "a --prefix that would become code in ~/.zprofile is refused"
  local h="$T/h-prefix" p
  for p in "$T/a\"b" "$T/a\$(id)" "$T/a\`id\`" "$T/a\\b"; do
    if run_install "$h" "$REL" --yes --prefix "$p"; then bad "refuses --prefix $p"; else ok "refuses --prefix $p"; fi
  done
  check "nothing written" [ -z "$(ls -A "$h")" ]
}

case_token() {
  echo "GITHUB_TOKEN: never on argv, never in a file, checked first; the real error is shown"
  local h="$T/h-token" stubs="$T/stubs-token" tok="github_pat_TESTONLY0123456789abcdefABCDEF_xyz"
  mkdir -p "$stubs"
  cp "$T/stubs/"{launchctl,open,pgrep,lsregister,gh,ioreg} "$stubs/"
  cat >"$stubs/curl" <<STUB
#!/bin/sh
echo "\$*" >>"$T/curl.argv"
for a in "\$@"; do [ "\$a" = - ] && cat >>"$T/curl.stdin"; done
echo "curl: (6) Could not resolve host: api.github.com" >&2
exit 6
STUB
  chmod +x "$stubs/curl"
  : >"$T/curl.argv"; : >"$T/curl.stdin"
  if TEST_BASE=none TEST_PATH="$stubs:/usr/bin:/bin:/usr/sbin:/sbin" TEST_ENV="GITHUB_TOKEN=$tok" \
    run_install "$h" "$REL" --yes --no-app; then bad "fails without GitHub"; else ok "fails without GitHub"; fi
  check "the token went to curl on stdin" contains "$T/curl.stdin" "Authorization: Bearer $tok"
  check "the token is not on curl's argv" [ "$(count_of "$T/curl.argv" "$tok")" == 0 ]
  check "curl is held to https" contains "$T/curl.argv" "--proto =https"
  check "the real error is shown" contains "$T/out.log" "Could not resolve host"
  check "no token file left" [ -z "$(grep -rl "$tok" "$T/tmp" 2>/dev/null || true)" ]
  : >"$T/curl.argv"
  if TEST_BASE=none TEST_PATH="$stubs:/usr/bin:/bin:/usr/sbin:/sbin" TEST_ENV='GITHUB_TOKEN=abc"url=x' \
    run_install "$h" "$REL" --yes --no-app; then bad "a token with odd characters is refused"
  else ok "a token with odd characters is refused"; fi
  check "curl never ran with it" [ ! -s "$T/curl.argv" ]
}

case_from() {
  echo "--from DIR installs from a folder (a USB stick), no network"
  local h="$T/h-from"
  TEST_BASE=none run_install "$h" "$REL" --yes --no-app --from "$REL" || { bad "install --from exits 0"; show_log 30; return; }
  ok "install --from exits 0"
  check "cpa is $VERSION" [ "$("$h/.local/bin/cpa" --version)" == "cpa $VERSION" ]
  check "no test hook warning for --from" [ "$(count_of "$T/out.log" "test hook: CPA_INSTALL_BASE_URL")" == 0 ]
  if TEST_BASE=none run_install "$T/h-from2" "$REL" --yes --from "$T/nowhere"; then bad "a missing --from folder refuses"
  else ok "a missing --from folder refuses"; fi
}

case_long_quarantine() {
  echo "an app whose every file is quarantined is refused (a long xattr listing)"
  local rel="$T/rel-lq" h="$T/h-lq" src="$T/lq-src" app i
  cp -R "$REL" "$rel"
  app="$(make_fake_app "$src")"
  mkdir -p "$app/Contents/Resources"
  for i in $(seq 1 1500); do
    printf 'x\n' >"$app/Contents/Resources/f$i"
    xattr -w com.apple.quarantine "0081;00000000;Safari;" "$app/Contents/Resources/f$i"
  done
  (cd "$src" && ditto -c -k --keepParent "Cardputer Assistant.app" "$rel/CardputerAssistant-$VERSION-arm64.zip")
  resum "$rel"
  if TEST_BASE="$rel" run_install "$h" "$rel" --yes; then bad "install refuses"; else ok "install refuses"; fi
  check "says quarantined" contains "$T/out.log" "quarantined"
  check "no app installed" absent "$h/$APP_DIR"
}

case_release_out() {
  echo "release-local.sh never deletes a folder it did not make"
  local keep="$T/precious"
  mkdir -p "$keep"
  printf 'keep me\n' >"$keep/notes.txt"
  if CPA_RELEASE_PYSERIAL="$(pyserial_wheel)" "$ROOT/scripts/release-local.sh" --tag "$TAG" --out "$keep" \
    --skip-firmware --app "$(make_fake_app "$T/app-out")" --dev >"$T/out.log" 2>&1; then bad "refuses --out of a full folder"
  else ok "refuses --out of a full folder"; fi
  check "the folder is untouched" contains "$keep/notes.txt" "keep me"
  check "says why" contains "$T/out.log" "--out"
}

case_device_now() {
  echo "a board plugged in now: the service question says so"
  local h="$T/h-dev"
  printf '  "idProduct" = 4097\n  "idVendor" = 12346\n' >"$T/ioreg.out"
  printf 'n\n' >"$T/tty-n"
  TEST_TTY="$T/tty-n" run_install "$h" "$REL" --no-app || { bad "install exits 0"; show_log; rm -f "$T/ioreg.out"; return; }
  rm -f "$T/ioreg.out"
  check "the question says the device is plugged in" contains "$T/out.log" "plugged in now"
  check "answering n installs no LaunchAgent" absent "$h/$AGENT_PLIST"
}

case_uninstall() {
  echo "uninstall removes the install and keeps the data"
  local h="${FRESH_HOME:?run fresh first}" data="$FRESH_HOME/$CPA_HOME_REL"
  printf 'keep me\n' >"$data/events.jsonl"
  run_uninstall "$h" --dry-run || { bad "uninstall --dry-run exits 0"; show_log; return; }
  check "--dry-run keeps the app" test -d "$h/$APP_DIR"
  run_uninstall "$h" --yes || { bad "uninstall exits 0"; show_log 30; return; }
  ok "uninstall exits 0"
  check "the app is gone" absent "$h/$APP_DIR"
  check "the LaunchAgent is gone" absent "$h/$AGENT_PLIST"
  check "cpa is gone" absent "$h/.local/bin/cpa"
  check "the PATH line is gone" [ "$(count_of "$h/.zprofile" "# cardputer-assistant: PATH")" == 0 ]
  check "the data is kept" contains "$data/events.jsonl" "keep me"
  check "it lists what it kept" contains "$T/out.log" "$data"
  check "in English by default" contains "$T/out.log" "==> Uninstalled"
  check "with no Chinese in its own lines" no_han_own "$T/out.log"
  printf 'nope\n' >"$T/tty-nope"
  TEST_TTY="$T/tty-nope" run_uninstall "$h" --purge-data || { bad "purge exits 0"; show_log; return; }
  check "--purge-data without typing delete keeps it" test -f "$data/events.jsonl"
  printf 'delete\n' >"$T/tty-delete"
  TEST_TTY="$T/tty-delete" TEST_ENV="CPA_HOME=$h" run_uninstall "$h" --purge-data || { bad "purge exits 0"; show_log; return; }
  check "CPA_HOME pointing at HOME is never purged" test -f "$data/events.jsonl"
  cp "$data/config.json" "$T/config.keep"
  plutil -replace notes_dir -string "~/$CPA_HOME_REL/notes" "$data/config.json"
  TEST_TTY="$T/tty-delete" run_uninstall "$h" --purge-data || { bad "purge exits 0"; show_log; return; }
  check "a notes folder inside CPA_HOME keeps CPA_HOME" test -f "$data/events.jsonl"
  cp "$T/config.keep" "$data/config.json"
  TEST_TTY="$T/tty-delete" run_uninstall "$h" --purge-data || { bad "purge exits 0"; show_log; return; }
  check "typing delete erases \$CPA_HOME" absent "$data"
  local z="$T/h-zp"
  mkdir -p "$z"
  printf 'export PATH="%s/.local/bin:$PATH" # cardputer-assistant: PATH\n' "$z" >"$z/.zprofile"
  run_uninstall "$z" --yes || { bad "uninstall with only the PATH line exits 0"; show_log; }
  check "the PATH line is gone" [ "$(count_of "$z/.zprofile" "# cardputer-assistant: PATH")" == 0 ]
  check "no temp file left" absent "$z/.zprofile.cpa-tmp"
  if [[ -n "${UV_HOME:-}" ]]; then
    TEST_PATH="$T/stubs-nouv:/usr/bin:/bin:/usr/sbin:/sbin" run_uninstall "$UV_HOME" --yes || { bad "uninstall (own uv) exits 0"; show_log; }
    check "the uv install.sh fetched is removed" absent "$UV_HOME/.local/bin/uv"
  fi
}

case_lang() {
  echo "one language: --lang, then CPA_LANG, LC_ALL, LC_MESSAGES, LANG, else English; the checklist gets it too"
  local h="$T/h-zh"
  run_install "$h" "$REL" --yes --lang zh || { bad "install --lang zh exits 0"; show_log 30; return; }
  ok "install --lang zh exits 0"
  check "the steps are in Chinese" contains "$T/out.log" "[1/9] 检查这台 Mac"
  check "so are the next steps" contains "$T/out.log" "接下来："
  check "and none of the English lines" no_english_own "$T/out.log"
  check "cpa setup check --todo is asked in Chinese (CPA_LANG=zh under LANG=en_US)" contains "$T/out.log" "还要做"
  TEST_ENV="LANG=zh_CN.UTF-8" run_uninstall "$h" --yes || { bad "uninstall exits 0"; show_log 30; return; }
  check "uninstall follows LANG=zh_CN" contains "$T/out.log" "==> 卸载完了"
  check "and says nothing in English itself" no_english_own "$T/out.log"
  local want env args s
  while IFS='|' read -r want env args; do
    for s in install uninstall; do
      check "$s --help: $env${args:+ $args} -> $want" [ "$(help_lang "$s" "$env" $args)" == "$want" ]
    done
  done <<'EOF'
en|LANG=en_US.UTF-8|
en|LANG=|
en|LANG=C|
zh|LANG=zh_CN.UTF-8|
zh|LANG=zh_CN.UTF-8 LC_ALL=|
en|LANG=zh_CN.UTF-8 LC_ALL=en_US.UTF-8|
zh|LC_MESSAGES=zh_TW.UTF-8|
en|LANG=zh_CN.UTF-8 LC_MESSAGES=C|
en|LANG=zh_CN.UTF-8 CPA_LANG=en|
zh|LC_ALL=en_US.UTF-8 CPA_LANG=zh|
zh|CPA_LANG=en|--lang zh
en|LANG=zh_CN.UTF-8|--lang en
EOF
}

help_lang() { # SCRIPT ENV [args]: zh or en, as the --help screen comes out under that environment
  local script="$1" env="$2"; shift 2
  TEST_ENV="$env" run_script "$ROOT/scripts/$script.sh" "$T/h-help" "$REL" "$@" --help || { echo failed; return; }
  if has_han <"$T/out.log"; then echo zh; else echo en; fi
}

main() {
  make_stubs
  REL="$T/release"
  echo "building the local release $TAG from this checkout"
  make_release "$REL"
  local cases="${*:-fresh checksum quarantine long_quarantine config path_line dry spaces no_tty device_now root truncated
    bad_prefix token from release_out uv_bootstrap uninstall service_race service_dead lang}" c
  for c in $cases; do
    : >"$T/stub.log"
    rm -rf "$T/launchd"
    "case_$c"
  done
}

main "$@"
