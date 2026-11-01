#!/bin/bash
# Install (or update) Cardputer Assistant on this Mac from a GitHub release, checked byte for byte.
#
#   curl -fsSL https://raw.githubusercontent.com/h3qing/cardputer-assistant/main/scripts/install.sh | bash
#   (a private fork works too: log in with gh, or export a read-only GITHUB_TOKEN; see "Access" below)
#   USB stick:     bash /Volumes/USB/v0.5.0/install.sh --from /Volumes/USB/v0.5.0   (release-local.sh --release-json)
#   Options:       ... | bash -s -- --dry-run          (run the same line again to update)
#
# It installs cpa / cpad / cpa-stt (uv tool, links in ~/.local/bin), the Mac app into ~/Applications, and the
# cpad LaunchAgent after asking. Every download is checked against the SHA-256 GitHub publishes for it and the
# release's SHA256SUMS; anything off and nothing is installed. No sudo, no Gatekeeper changes, no models, never
# writes ~/.claude, config.json, your notes or $CPA_HOME (cpa service install keeps your config). docs/27.
#
#   --dry-run          say what it would do; write nothing
#   --yes              ask nothing (services get installed)
#   --version vX.Y.Z   that release (default: the latest)
#   --prefix DIR       where cpa, cpa-stt (and uv, when it has to fetch it) go (default ~/.local/bin)
#   --home DIR         act as if HOME were DIR (tests)
#   --from DIR         install from a folder release-local.sh --release-json made (no network, no GitHub)
#   --no-app           skip the Mac app        --no-service   skip the LaunchAgent
#   --no-modify-path   leave ~/.zprofile alone --firmware     also fetch cpa-<tag>.bin into ~/Downloads
#   --lang zh|en       the language it prints (default: CPA_LANG, LC_ALL, LC_MESSAGES, LANG; else English)
#   -h, --help
#
# Access: --from, then gh (logged in), then $GITHUB_TOKEN (never on argv, never in a file: curl reads it on stdin),
# then plain curl. Test hooks (a red line names each one that is set): CPA_INSTALL_BASE_URL=file:///dir (or /dir,
# copied with its extended attributes) serves <dir>/release.json and the assets next to it; CPA_INSTALL_REPO,
# CPA_INSTALL_LSREGISTER, CPA_INSTALL_UV_URL, CPA_INSTALL_UV_SHA256, CPA_INSTALL_TTY, CPA_INSTALL_RECHECK_S (seconds
# before step 9 asks cpa status again; default 3).
set -euo pipefail

# The GitHub repository (owner/name) the releases come from: set here, nowhere else.
REPO_SLUG="h3qing/cardputer-assistant"
REPO="${CPA_INSTALL_REPO:-$REPO_SLUG}"
APP_NAME="Cardputer Assistant"
APP_ID="com.cardputer-assistant.mac"
AGENT="com.cardputer-assistant.cpad"
STT_AGENT="com.cardputer-assistant.cpa-stt"
PATH_MARK="# cardputer-assistant: PATH"
UV_VERSION="0.11.16"
UV_SHA256_ARM64="2b25be1af546be330b340b0a76b99f989daa6d92678fdffb87438e661e9d88fb"
UV_SHA256_X86="6b91ae3de155f51bd1f5b74814821c79f016a176561f252cd9ddfb976939af2e"
HOOKS="CPA_INSTALL_BASE_URL CPA_INSTALL_REPO CPA_INSTALL_LSREGISTER CPA_INSTALL_UV_URL CPA_INSTALL_UV_SHA256 CPA_INSTALL_TTY
  CPA_INSTALL_RECHECK_S"
RECHECK_S="${CPA_INSTALL_RECHECK_S:-3}"
LSREGISTER="${CPA_INSTALL_LSREGISTER:-/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister}"
TTY="${CPA_INSTALL_TTY:-/dev/tty}"

# One language per run: --lang, else the first of CPA_LANG, LC_ALL, LC_MESSAGES, LANG that is set; zh... is
# Chinese, anything else English. Every line goes through msg ZH EN (one argument: the same in both).
msg() { if [[ "$UI_LANG" == zh* ]]; then printf '%s' "$1"; else printf '%s' "${2-$1}"; fi; }
say() { printf '\033[1m==> %s\033[0m\n' "$(msg "$@")"; }
step() { printf '\n\033[1m==> [%s] %s\033[0m\n' "$1" "$(msg "$2" "$3")"; }
note() { printf '    %s\n' "$(msg "$@")"; }
fail() { printf '\033[31m%s%s\033[0m\n' "$(msg "错误：" "Error: ")" "$(msg "$@")" >&2; exit 1; }
run() { if [[ "$DRY" == 1 ]]; then note "(dry-run) $*"; else "$@"; fi; }

usage() {
  printf '%s\n' "$(msg "Cardputer Assistant 安装脚本（再跑一次就是升级）" "Cardputer Assistant installer (run it again to update)")"
  cat <<EOF
  curl -fsSL https://raw.githubusercontent.com/${REPO_SLUG}/main/scripts/install.sh | bash
  bash /Volumes/USB/v0.5.0/install.sh --from /Volumes/USB/v0.5.0
EOF
  printf '%s\n' "$(msg "选项：" "Options:")"
  cat <<'EOF'
  --dry-run --yes --version vX.Y.Z --prefix DIR --home DIR --from DIR --no-app --no-service --no-modify-path
  --firmware --lang zh|en
EOF
}

parse_args() {
  DRY=0 YES=0 WANT="" PREFIX="" NO_APP=0 NO_SERVICE=0 NO_PATH=0 FIRMWARE=0 FROM="" HELP=0
  UI_LANG="${CPA_LANG:-${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}}"
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --dry-run) DRY=1 ;;
      --yes|-y) YES=1 ;;
      --version) WANT="${2:?--version needs vX.Y.Z}"; shift ;;
      --prefix) PREFIX="${2:?--prefix needs a folder}"; shift ;;
      --home) export HOME="${2:?--home needs a folder}"; shift ;;
      --from) FROM="${2:?--from needs a folder}"; shift ;;
      --no-app) NO_APP=1 ;;
      --no-service) NO_SERVICE=1 ;;
      --no-modify-path) NO_PATH=1 ;;
      --firmware) FIRMWARE=1 ;;
      --lang) UI_LANG="${2:?--lang needs zh or en}"; shift ;;
      -h|--help) HELP=1 ;;
      *) fail "不认识的选项：$1" "unknown option: $1" ;;
    esac
    shift
  done
  [[ "$HELP" == 0 ]] || { usage; exit 0; } # after the loop: --help --lang zh is in Chinese too
  [[ -z "$WANT" || "$WANT" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "--version 要写成 v0.5.0 这样" "--version looks like v0.5.0"
  [[ -d "$HOME" ]] || fail "HOME 不存在：$HOME" "HOME does not exist: $HOME"
  PREFIX="${PREFIX:-$HOME/.local/bin}"
  # It goes into ~/.zprofile as "export PATH=..."; a quote, $, backtick or backslash there would run at every login.
  [[ "$PREFIX" != *[\"\$\`\\]* ]] || fail "--prefix 里不能有 \" \$ \` \\：$PREFIX" "--prefix cannot hold \" \$ \` or \\: $PREFIX"
  if [[ -n "$FROM" ]]; then
    [[ -f "$FROM/release.json" ]] || fail "$FROM 里没有 release.json（用 release-local.sh --release-json 造）" \
      "no release.json in $FROM (release-local.sh --release-json makes one)"
    FROM="$(cd "$FROM" && pwd -P)"
  fi
  APPS="$HOME/Applications"
  AGENTS="$HOME/Library/LaunchAgents"
}

# The terminal, even under curl | bash (stdin is the script then). Empty when there is none.
can_ask() { [[ "$YES" == 0 ]] && (: <"$TTY") 2>/dev/null; }
ask() { local answer=""; printf '%s ' "$(msg "$@")" >&2; read -r answer <"$TTY" || true; printf '%s' "$answer"; }

warn_hooks() { # a leftover export (direnv, an old shell) must not weaken the checks silently
  local name
  for name in $HOOKS; do
    [[ -z "${!name:-}" ]] || printf '\033[31m    %s%s=%s\033[0m\n' "$(msg "测试开关：" "test hook: ")" "$name" "${!name}"
  done
}

preflight() {
  step 1/9 "检查这台 Mac" "Checking this Mac"
  warn_hooks
  [[ "$(uname -s)" == "Darwin" ]] || fail "只支持 macOS" "macOS only"
  [[ "$(id -u)" != 0 ]] || fail "不要用 root 或 sudo 跑" "do not run this as root or with sudo"
  ARM=0
  if [[ "$(uname -m)" == "arm64" || "$(sysctl -n hw.optional.arm64 2>/dev/null || true)" == 1 ]]; then ARM=1; fi
  local major; major="$(sw_vers -productVersion | cut -d. -f1)"
  if [[ "$NO_APP" == 0 && ( "$ARM" == 0 || "$major" -lt 14 ) ]]; then
    note "Mac app 要 Apple 芯片和 macOS 14+，这次跳过" "the app needs Apple silicon and macOS 14+: skipped"
    NO_APP=1
  fi
  note "macOS $(sw_vers -productVersion) · $([[ "$ARM" == 1 ]] && echo arm64 || echo x86_64) · HOME=$HOME"
}

plan_screen() {
  step 2/9 "要做的事" "The plan"
  note "cpa、cpad、cpa-stt → uv tool（链接在 ${PREFIX}）" "cpa, cpad, cpa-stt → uv tool (links in ${PREFIX})"
  [[ "$NO_APP" == 1 ]] || note "Mac app → $APPS/$APP_NAME.app"
  [[ "$NO_SERVICE" == 1 ]] || note "后台服务 → $AGENTS/$AGENT.plist（先问你）" "LaunchAgent → $AGENTS/$AGENT.plist (asks first)"
  [[ "$NO_PATH" == 1 ]] || note "PATH：缺的话在 ~/.zprofile 加一行" "PATH: one marked line in ~/.zprofile if needed"
  note "不碰：~/.claude、config.json、~/CardputerNotes、模型、Gatekeeper、sudo" \
    "never touches: ~/.claude, config.json, ~/CardputerNotes, models, Gatekeeper, sudo"
  if can_ask && [[ "$DRY" == 0 ]]; then ask "回车继续，Ctrl-C 取消" "Press Enter to continue, Ctrl-C to cancel" >/dev/null; fi
}

# --- access: --from, gh, $GITHUB_TOKEN, then plain curl; CPA_INSTALL_BASE_URL for a test "release" ----------
pick_access() {
  step 3/9 "连上 GitHub" "Reaching GitHub"
  BASE="${FROM:-${CPA_INSTALL_BASE_URL:-}}"
  if [[ -n "$BASE" ]]; then ACCESS=base; note "本地发布：$BASE" "local release: $BASE"
  elif command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then ACCESS=gh; note "用 gh 的登录" "using gh"
  elif [[ -n "${GITHUB_TOKEN:-}" ]]; then
    # A quote or newline would add curl directives (url=, output=) to the config curl reads on stdin.
    [[ "$GITHUB_TOKEN" =~ ^[A-Za-z0-9_]+$ ]] || fail "GITHUB_TOKEN 里有奇怪的字符" "GITHUB_TOKEN holds unexpected characters"
    ACCESS=token; note "用 GITHUB_TOKEN" "using GITHUB_TOKEN"
  else ACCESS=public; note "不登录（公开仓库）" "anonymous (public repo)"
  fi
}

base_get() { # NAME OUT: from the local release; a folder is copied as it is (extended attributes too)
  case "$BASE" in
    /*) cp "${BASE%/}/$1" "$2" ;;
    *) curl_get "${BASE%/}/$1" "$2" ;;
  esac
}

# URL OUT [extra curl args]: silent unless --progress-bar is among them. https only, redirects too. With a token,
# curl reads the header from stdin (printf is a builtin: the token never reaches argv or a file).
curl_get() {
  local url="$1" out="$2" quiet=-s a safe=(); shift 2
  for a in "$@"; do [[ "$a" != --progress-bar ]] || quiet=-S; done
  [[ "$url" != https://* ]] || safe=(--proto '=https' --proto-redir '=https' --tlsv1.2)
  if [[ "$ACCESS" == token ]]; then
    [[ "$url" == https://* ]] || fail "不是 https：$url" "not https: $url"
    printf 'header = "Authorization: Bearer %s"\n' "$GITHUB_TOKEN" \
      | curl -fSL "$quiet" "${safe[@]}" -K - "$@" -o "$out" "$url"
  else curl -fSL "$quiet" ${safe[@]+"${safe[@]}"} "$@" -o "$out" "$url"; fi
}

release_json() {
  local api="repos/$REPO/releases/latest" out="$1"
  [[ -z "$WANT" ]] || api="repos/$REPO/releases/tags/$WANT"
  case "$ACCESS" in
    base) base_get release.json "$out" ;;
    gh) gh api "$api" >"$out" ;;
    *) curl_get "https://api.github.com/$api" "$out" -H 'Accept: application/vnd.github+json' ;;
  esac
}

# tag, commit, then one line per asset: name<TAB>api url<TAB>download url<TAB>sha256 ("-" when GitHub has none).
# JavaScript for Automation ships with every Mac; python3 would prompt a fresh Mac for the developer tools.
parse_release() {
  osascript -l JavaScript -e '
  ObjC.import("Foundation");
  function run(argv) {
    const text = $.NSString.stringWithContentsOfFileEncodingError(argv[0], $.NSUTF8StringEncoding, null).js;
    const r = JSON.parse(text);
    const lines = [r.tag_name || "", /^[0-9a-f]{40}$/.test(r.target_commitish || "") ? r.target_commitish : "-"];
    for (const a of r.assets || []) {
      const digest = (a.digest || "").replace(/^sha256:/, "") || "-";
      lines.push([a.name, a.url || "-", a.browser_download_url || "-", digest].join("\t"));
    }
    return lines.join("\n");
  }' "$1"
}

resolve() {
  step 4/9 "找版本" "Finding the release"
  META="$(mktemp -t cpa-install-release)"
  local err
  if ! err="$(release_json "$META" 2>&1 >/dev/null)"; then
    [[ -z "$err" ]] || note "$(printf '%s\n' "$err" | grep -v '^ *$' | tail -1)"
    fail "取不到发布信息（看上面一行）。仓库是私有的话先 gh auth login（或设 GITHUB_TOKEN）" \
      "cannot read the release (see the line above); for the private repo run gh auth login (or set GITHUB_TOKEN) first"
  fi
  ASSETS="$(parse_release "$META")" || fail "发布信息读不懂" "cannot parse the release"
  TAG="$(printf '%s\n' "$ASSETS" | sed -n 1p)"
  COMMIT="$(printf '%s\n' "$ASSETS" | sed -n 2p)"
  ASSETS="$(printf '%s\n' "$ASSETS" | sed -n '3,$p')"
  [[ "$TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "没有可用的版本：'$TAG'" "no usable release tag: '$TAG'"
  VERSION="${TAG#v}"
  WHEEL="cpad-$VERSION-py3-none-any.whl"
  APP_ZIP="CardputerAssistant-$VERSION-arm64.zip"
  DEP_WHEEL="$(asset_names | grep -E '^pyserial-[0-9.]+-py2\.py3-none-any\.whl$' | head -1 || true)"
  for need in SHA256SUMS "$WHEEL" "$DEP_WHEEL"; do
    if [[ -z "$need" ]] || ! asset_field "$need" 1 >/dev/null; then
      fail "$TAG 里缺 ${need:-pyserial wheel}（v0.5.0 起才有 Mac 端的包）" \
        "the release lacks: ${need:-pyserial wheel} (Mac assets start at v0.5.0)"
    fi
  done
  [[ "$NO_APP" == 1 ]] || asset_field "$APP_ZIP" 1 >/dev/null || fail "$TAG 里缺 $APP_ZIP" "the release lacks: $APP_ZIP"
  note "$TAG$([[ "$COMMIT" == - ]] || echo " (commit ${COMMIT:0:7})")"
}

asset_names() { printf '%s\n' "$ASSETS" | cut -f1; }
asset_field() { # NAME FIELD(1 api url, 2 download url, 3 sha256): prints it, fails when the asset is missing
  local line; line="$(printf '%s\n' "$ASSETS" | awk -F'\t' -v n="$1" '$1 == n { print; exit }')"
  [[ -n "$line" ]] || return 1
  printf '%s\n' "$line" | cut -f"$(( $2 + 1 ))"
}

fetch_asset() { # NAME -> $WORK/NAME; a progress bar for the big ones (the app, the firmware)
  local name="$1" out="$WORK/$1" bar=() url
  case "$name" in *.zip|*.bin) bar=(--progress-bar) ;; esac
  case "$ACCESS" in
    base) base_get "$name" "$out" ;;
    gh) gh release download "$TAG" -R "$REPO" -p "$name" -D "$WORK" --clobber >/dev/null ;;
    token)
      url="$(asset_field "$name" 1)"
      # The token goes only to this repo's asset API, whatever release.json says.
      [[ "$url" == "https://api.github.com/repos/$REPO/releases/assets/"* ]] || fail "奇怪的下载地址：$url" "unexpected asset URL: $url"
      curl_get "$url" "$out" -H 'Accept: application/octet-stream' ${bar[@]+"${bar[@]}"} ;;
    *) curl_get "$(asset_field "$name" 2)" "$out" ${bar[@]+"${bar[@]}"} ;;
  esac
}

sha256() { shasum -a 256 "$1" | awk '{print $1}'; }

verify_asset() { # NAME: the API digest, SHA256SUMS (except for itself), and no quarantine flag
  local name="$1" file="$WORK/$1" want listed actual
  actual="$(sha256 "$file")"
  want="$(asset_field "$name" 3)"
  [[ "$want" != "-" ]] || fail "GitHub 没给 $name 的 SHA-256" "GitHub has no digest for $name"
  [[ "$actual" == "$want" ]] || fail "下载损坏或被篡改：$name" "corrupted or tampered: $name"
  if [[ "$name" != SHA256SUMS ]]; then
    listed="$(awk -v n="$name" '$2 == n || $2 == "*" n { print $1; exit }' "$WORK/SHA256SUMS")"
    [[ "$listed" == "$actual" ]] || fail "下载损坏或被篡改（SHA256SUMS 对不上）：$name" "corrupted or tampered (SHA256SUMS): $name"
  fi
  if xattr -p com.apple.quarantine "$file" >/dev/null 2>&1; then fail "$name 带隔离属性" "$name is quarantined"; fi
}

download() {
  step 5/9 "下载并核对" "Downloading and verifying"
  local list="SHA256SUMS $WHEEL $DEP_WHEEL" name
  [[ "$NO_APP" == 1 ]] || list="$list $APP_ZIP"
  [[ "$FIRMWARE" == 0 ]] || list="$list cpa-$TAG.bin"
  if [[ "$DRY" == 1 ]]; then note "(dry-run) $list"; return; fi
  for name in $list; do
    fetch_asset "$name" || fail "下载失败：$name" "download failed: $name"
    verify_asset "$name"
    note "✓ $name"
  done
}

uv_ok() { local v; v="$("$1" --version 2>/dev/null | awk '{print $2}')"; [[ "$v" =~ ^0\.(1[1-9]|[2-9][0-9])\. || "$v" =~ ^[1-9] ]]; }

ensure_uv() {
  step 6/9 "准备 uv" "Getting uv"
  UV="$(command -v uv || true)"
  [[ -z "$UV" ]] && [[ -x "$PREFIX/uv" ]] && UV="$PREFIX/uv"
  if [[ -n "$UV" ]] && uv_ok "$UV"; then note "$UV ($("$UV" --version))"; return; fi
  local triple="aarch64-apple-darwin" want="$UV_SHA256_ARM64"
  [[ "$ARM" == 1 ]] || { triple="x86_64-apple-darwin"; want="$UV_SHA256_X86"; }
  local url="${CPA_INSTALL_UV_URL:-https://github.com/astral-sh/uv/releases/download/$UV_VERSION/uv-$triple.tar.gz}"
  want="${CPA_INSTALL_UV_SHA256:-$want}"
  if [[ "$DRY" == 1 ]]; then note "(dry-run) uv $UV_VERSION ← $url"; UV="$PREFIX/uv"; return; fi
  (ACCESS=public; curl_get "$url" "$WORK/uv.tar.gz") || fail "下载 uv 失败" "cannot download uv" # never with the token
  [[ "$(sha256 "$WORK/uv.tar.gz")" == "$want" ]] || fail "uv 下载损坏或被篡改" "uv: corrupted or tampered"
  tar -xzf "$WORK/uv.tar.gz" -C "$WORK"
  mkdir -p "$PREFIX"
  install -m 755 "$WORK/uv-$triple/uv" "$PREFIX/uv"
  install -m 755 "$WORK/uv-$triple/uvx" "$PREFIX/uvx"
  : >"$PREFIX/.cardputer-assistant-installed-uv"
  UV="$PREFIX/uv"
  note "uv $UV_VERSION → $PREFIX/uv"
}

install_cpa() {
  step 7/9 "装 cpa" "Installing cpa"
  export UV_TOOL_BIN_DIR="$PREFIX"
  run mkdir -p "$PREFIX"
  if [[ "$DRY" == 0 ]] && ! "$UV" python find 3.12 >/dev/null 2>&1; then
    note "首次会下载 Python 3.12（约 70 MB，要一会儿）" "first run downloads Python 3.12 (~70 MB, takes a while)"
  fi
  run "$UV" tool install -q --force --reinstall --python 3.12 --no-index --find-links "$WORK" "$WORK/$WHEEL" >/dev/null
  [[ "$DRY" == 1 ]] && return
  local got; got="$("$PREFIX/cpa" --version 2>/dev/null || true)"
  [[ "$got" == "cpa $VERSION" ]] || fail "装好的 cpa 报 '$got'，应该是 cpa $VERSION" "installed cpa says '$got', not cpa $VERSION"
  note "$got · $("$PREFIX/cpa-stt" --version)"
}

quit_app() {
  if pgrep -x CardputerAssistant >/dev/null 2>&1; then
    osascript -e "quit app id \"$APP_ID\"" >/dev/null 2>&1 || true
    sleep 2
  fi
}

install_app() {
  step 8/9 "装 Mac app" "Installing the Mac app"
  if [[ "$NO_APP" == 1 ]]; then note "跳过" "skipped"; return; fi
  local src="$WORK/app/$APP_NAME.app" dest="$APPS/$APP_NAME.app" tmp="$APPS/.$APP_NAME.app.new" old="$APPS/.$APP_NAME.app.old"
  if [[ "$DRY" == 1 ]]; then note "(dry-run) $APP_ZIP → $dest"; return; fi
  ditto -x -k "$WORK/$APP_ZIP" "$WORK/app"
  [[ -d "$src" ]] || fail "$APP_ZIP 里没有 $APP_NAME.app" "$APP_ZIP does not hold $APP_NAME.app"
  # Not "xattr | grep -q": grep stops at the first hit, xattr dies of SIGPIPE, and pipefail reads it as "clean".
  local q; q="$(xattr -lr "$src" 2>/dev/null || true)"
  [[ "$q" != *com.apple.quarantine* ]] || fail "app 带隔离属性" "the app is quarantined"
  codesign --verify --strict "$src" 2>/dev/null || fail "app 的签名对不上" "the app's signature does not verify"
  quit_app
  mkdir -p "$APPS"
  rm -rf "$tmp" "$old"
  ditto "$src" "$tmp"
  if [[ -d "$dest" ]]; then mv "$dest" "$old"; fi
  mv "$tmp" "$dest"
  rm -rf "$old"
  # LaunchServices must not open the unpacked copy in the temp folder later (a ghost or stale launch).
  [[ -x "$LSREGISTER" ]] && { "$LSREGISTER" -u "$src" >/dev/null 2>&1 || true; "$LSREGISTER" -f "$dest" >/dev/null 2>&1 || true; }
  note "app $(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$dest/Contents/Info.plist") → $dest"
}

add_path() {
  [[ "$NO_PATH" == 1 ]] && return
  case ":$(/bin/zsh -lc 'printf %s "$PATH"' 2>/dev/null || true):" in *":$PREFIX:"*) return ;; esac
  local profile="$HOME/.zprofile" line
  line="export PATH=\"$PREFIX:\$PATH\" $PATH_MARK"
  if [[ -f "$profile" ]] && grep -qF "$PATH_MARK" "$profile"; then return; fi
  run sh -c 'printf "\n%s\n" "$1" >>"$2"' sh "$line" "$profile"
  NEW_PATH=1
  note "PATH：加进了 ~/.zprofile" "PATH: added to ~/.zprofile"
}

# A cpad that starts and stops at once (a bad config, a missing permission) shows only a moment later (#82).
recheck_service() {
  sleep "$RECHECK_S"
  if "$PREFIX/cpa" status --json 2>/dev/null | grep -qF '"running": true'; then
    note "✓ $RECHECK_S 秒后还在跑" "✓ still running $RECHECK_S s later"
  else
    note "$RECHECK_S 秒后 cpa status 说没在跑：cpa setup check" "cpa status says not running $RECHECK_S s later: cpa setup check"
  fi
}

# A Cardputer (Espressif USB JTAG/serial, 303a:1001) on USB right now: the USB list only, no serial port opened.
device_now() {
  local usb; usb="$(ioreg -p IOUSB -l -w 0 2>/dev/null || true)"
  [[ "$usb" == *'"idVendor" = 12346'* && "$usb" == *'"idProduct" = 4097'* ]]
}

install_service() {
  step 9/9 "后台服务" "Background service"
  add_path
  if [[ "$NO_SERVICE" == 1 ]]; then note "跳过（--no-service）" "skipped (--no-service)"; return; fi
  if [[ ! -f "$AGENTS/$AGENT.plist" ]]; then
    if [[ "$YES" == 0 ]] && ! can_ask; then
      note "没问到你（没有终端），先不装。在主力 Mac 上同步过设备以后：cpa service install" \
        "Not asked (no terminal): after syncing the device on your main Mac, run: cpa service install"
      return
    fi
    if [[ "$YES" == 0 ]]; then
      note "装上以后，插上设备就会把设备上还没传的笔记和录音存到这台 Mac、从设备上删掉。" \
        "Once installed, plugging the device in moves its unsent notes and recordings to this Mac."
      if device_now; then
        note "设备现在就插着：答 y 它马上就会接管" "the device is plugged in now: answering y takes it over right away"
      fi
      case "$(ask "在主力 Mac 上同步过了（↑ 和 录 都是 0）？[y/N]" "Synced on your main Mac first? [y/N]")" in
        y|Y|yes|YES) ;;
        *) note "先不装。准备好了再跑：cpa service install" "Later: cpa service install"; return ;;
      esac
    fi
  fi
  # Only cpa's own verdict counts: it waits for the old cpad to go and sees the new one run (#82).
  local started=0
  run "$PREFIX/cpa" service install && started=1
  [[ "$DRY" == 1 ]] && return
  if [[ "$started" == 1 ]]; then
    note "✓ cpad 在跑" "✓ cpad is running"
    recheck_service
  else
    note "cpad 没起来：再跑一次 cpa service install；还不行就到 系统设置 → 通用 → 登录项 打开它" \
      "cpad did not start: run cpa service install again; if it still fails, allow it in System Settings → General → Login Items"
  fi
  if [[ -f "$AGENTS/$STT_AGENT.plist" ]]; then
    launchctl kickstart -k "gui/$(id -u)/$STT_AGENT" >/dev/null 2>&1 || true
    note "cpa-stt 重启了" "cpa-stt restarted"
  fi
}

finish() {
  local what; what="Cardputer Assistant $TAG$([[ "$COMMIT" == - ]] || echo " (commit ${COMMIT:0:7})")"
  say "好了：$what" "Done: $what"
  [[ "$DRY" == 1 ]] && return
  printf '\n'
  CPA_LANG="$(msg zh en)" "$PREFIX/cpa" setup check --todo || true # the one list of what is left, in this language
  if [[ "$NO_APP" == 0 ]]; then open "$APPS/$APP_NAME.app" >/dev/null 2>&1 || true; fi
  [[ "$FIRMWARE" == 0 ]] || { mkdir -p "$HOME/Downloads"; cp "$WORK/cpa-$TAG.bin" "$HOME/Downloads/"; }
  next_steps
}

next_steps() { # only what the checklist cannot know
  printf '\n%s\n' "$(msg "接下来：" "Next steps:")"
  [[ "${NEW_PATH:-0}" == 0 ]] || note "exec zsh -l    # 让新 PATH 生效" "exec zsh -l    # pick up the new PATH"
  if [[ -z "$("$PREFIX/cpa" project ls 2>/dev/null || true)" ]]; then
    note "cpa project add --folder ~/code/某项目 -- 名字   # 或在 app 的「项目」页" \
      "cpa project add --folder ~/code/a-project -- name   # or the app's Projects pane"
    note "  登记项目，设备看板上才有它" "  register a project so the device's board shows it"
  fi
  [[ "$NO_APP" == 1 ]] || note "app 在屏幕右上角的菜单栏里" "the app lives in the menu bar (top right)"
  if [[ "$FIRMWARE" == 1 ]]; then
    note "固件：~/Downloads/cpa-$TAG.bin。在主力 Mac 上先同步（↑ 和 录 都是 0），cpa pause 120，" \
      "firmware: ~/Downloads/cpa-$TAG.bin. Sync on the main Mac first, then cpa pause 120,"
    note "  再照 README「安装方式 A」写到 0x0；脚本不碰串口" \
      "  and flash it at 0x0 as the README's install A says; this script never opens the serial port"
  fi
  note "第一次插设备时 macOS 会问\"允许配件连接\"，点允许" "macOS asks to allow the accessory the first time: allow it"
}

# bash 3.2 reports 0 to an EXIT trap after an unbound variable: only a run that reached the end exits 0.
on_exit() {
  local rc=$?
  rm -rf "${WORK:-}" "${META:-}"
  [[ "$rc" != 0 || "$DONE" == 1 ]] || rc=1
  exit "$rc"
}

main() {
  parse_args "$@"
  WORK="" META="" BASE="" ACCESS="" NEW_PATH=0 DONE=0
  trap on_exit EXIT
  trap 'exit 130' INT # bash 3.2 runs no EXIT trap on a signal: turn it into an exit, so the temp files go
  trap 'exit 143' TERM HUP
  preflight
  plan_screen
  pick_access
  resolve
  [[ "$DRY" == 1 ]] || WORK="$(mktemp -d -t cpa-install)"
  download
  ensure_uv
  install_cpa
  install_app
  install_service
  finish
  DONE=1
}

main "$@"
