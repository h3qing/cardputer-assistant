#!/bin/bash
# Remove what scripts/install.sh put on this Mac; keep your data.
#
#   curl -fsSL https://raw.githubusercontent.com/h3qing/cardputer-assistant/main/scripts/uninstall.sh | bash
#
# Removes: the Mac app (~/Applications), the cpad and cpa-stt LaunchAgents, our Claude Code hooks (shows the change,
# asks first), the cpa tool (uv tool uninstall cpad), the marked PATH line in ~/.zprofile, and uv only when
# install.sh fetched it. Keeps: $CPA_HOME (keys, events, config), your notes folder and the models; it lists them.
#
#   --dry-run       say what it would do      --yes   ask nothing (hooks are removed)
#   --home DIR      act as if HOME were DIR   --prefix DIR   where install.sh put cpa (default ~/.local/bin)
#   --purge-data    also delete $CPA_HOME after you type "delete" (never the notes folder)
#   --lang zh|en    the language it prints (default: CPA_LANG, LC_ALL, LC_MESSAGES, LANG; else English)
set -euo pipefail

APP_NAME="Cardputer Assistant"
APP_ID="com.cardputer-assistant.mac"
AGENT="com.cardputer-assistant.cpad"
STT_AGENT="com.cardputer-assistant.cpa-stt"
PATH_MARK="# cardputer-assistant: PATH"
LSREGISTER="${CPA_INSTALL_LSREGISTER:-/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister}"
TTY="${CPA_INSTALL_TTY:-/dev/tty}"

# One language per run: --lang, else the first of CPA_LANG, LC_ALL, LC_MESSAGES, LANG that is set; zh... is
# Chinese, anything else English. Every line goes through msg ZH EN (one argument: the same in both).
msg() { if [[ "$UI_LANG" == zh* ]]; then printf '%s' "$1"; else printf '%s' "${2-$1}"; fi; }
say() { printf '\n\033[1m==> %s\033[0m\n' "$(msg "$@")"; }
note() { printf '    %s\n' "$(msg "$@")"; }
fail() { printf '\033[31m%s%s\033[0m\n' "$(msg "错误：" "Error: ")" "$(msg "$@")" >&2; exit 1; }
run() { if [[ "$DRY" == 1 ]]; then note "(dry-run) $*"; else "$@"; fi; }
can_ask() { (: <"$TTY") 2>/dev/null; }
ask() { local answer=""; printf '%s ' "$(msg "$@")" >&2; read -r answer <"$TTY" || true; printf '%s' "$answer"; }

usage() { # the header above in English, its gist in Chinese
  if [[ "$UI_LANG" == zh* ]]; then
    cat <<'EOF'
删掉 scripts/install.sh 在这台 Mac 上装的东西，留着你的数据。

删：Mac app（~/Applications）、cpad 和 cpa-stt 的 LaunchAgent、我们的 Claude Code hooks（先给你看改动、问过再删）、
cpa 工具（uv tool uninstall cpad）、~/.zprofile 里标记过的 PATH 那一行，uv 只在是 install.sh 下载的时候才删。
留着：$CPA_HOME（钥匙、事件、设置）、笔记目录和模型，并列出它们在哪。

  --dry-run       只说不做                  --yes   什么都不问（hooks 会去掉）
  --home DIR      当作 HOME 是 DIR          --prefix DIR   install.sh 把 cpa 放在哪（默认 ~/.local/bin）
  --purge-data    你输入 delete 以后连 $CPA_HOME 一起删（笔记目录从不删）
  --lang zh|en    用哪种语言（默认看 CPA_LANG、LC_ALL、LC_MESSAGES、LANG；不是中文就用英文）
EOF
  else sed -n '2,/^set /p' "${BASH_SOURCE[0]:-/dev/null}" 2>/dev/null | sed -n 's/^# \{0,1\}//p' || true; fi
}

parse_args() {
  DRY=0 YES=0 PURGE=0 PREFIX="" HELP=0
  UI_LANG="${CPA_LANG:-${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}}"
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --dry-run) DRY=1 ;;
      --yes|-y) YES=1 ;;
      --purge-data) PURGE=1 ;;
      --home) export HOME="${2:?--home needs a folder}"; shift ;;
      --prefix) PREFIX="${2:?--prefix needs a folder}"; shift ;;
      --lang) UI_LANG="${2:?--lang needs zh or en}"; shift ;;
      -h|--help) HELP=1 ;;
      *) fail "不认识的选项：$1" "unknown option: $1" ;;
    esac
    shift
  done
  [[ "$HELP" == 0 ]] || { usage; exit 0; } # after the loop: --help --lang zh is in Chinese too
  [[ "$(id -u)" != 0 ]] || fail "不要用 root 或 sudo 跑" "do not run this as root or with sudo"
  PREFIX="${PREFIX:-$HOME/.local/bin}"
  CPA="$PREFIX/cpa"
  CPA_HOME_DIR="${CPA_HOME:-$HOME/Library/Application Support/CardputerAssistant}"
  NOTES="$(notes_dir)"
}

remove_app() {
  say "Mac app"
  local app="$HOME/Applications/$APP_NAME.app"
  if pgrep -x CardputerAssistant >/dev/null 2>&1; then run osascript -e "quit app id \"$APP_ID\""; fi
  [[ -d "$app" ]] || { note "没装" "not installed"; return; }
  [[ -x "$LSREGISTER" ]] && run "$LSREGISTER" -u "$app"
  run rm -rf "$app"
  note "删了：$app" "removed: $app"
}

remove_agents() {
  say "后台服务" "LaunchAgents"
  local uid; uid="$(id -u)"
  [[ -x "$PREFIX/cpa-stt" ]] && run "$PREFIX/cpa-stt" service uninstall
  [[ -x "$CPA" ]] && run "$CPA" service uninstall
  local label
  for label in "$AGENT" "$STT_AGENT"; do
    local plist="$HOME/Library/LaunchAgents/$label.plist"
    if launchctl print "gui/$uid/$label" >/dev/null 2>&1 || [[ -f "$plist" ]]; then
      run launchctl bootout "gui/$uid/$label" 2>/dev/null || true
      run rm -f "$plist"
    fi
  done
  note "cpad、cpa-stt 不会再自动启动" "cpad and cpa-stt no longer start at login"
}

remove_hooks() {
  say "Claude Code hooks"
  [[ -x "$CPA" ]] || { note "没有 cpa，跳过" "no cpa: skipped"; return; }
  local preview; preview="$("$CPA" hooks uninstall 2>&1 || true)"
  note "$preview"
  case "$preview" in "no cpa hooks"*) return ;; esac
  if [[ "$DRY" == 1 ]]; then return; fi
  if [[ "$YES" == 0 ]]; then
    can_ask || { note "没问到你，留着：cpa hooks uninstall --yes" "not asked, kept: cpa hooks uninstall --yes"; return; }
    case "$(ask "去掉这些 hooks？[y/N]" "Remove them? [y/N]")" in y|Y|yes|YES) ;; *) note "留着" "kept"; return ;; esac
  fi
  "$CPA" hooks uninstall --yes
}

remove_tool() {
  say "cpa / cpad / cpa-stt"
  local uv; uv="$(command -v uv || true)"
  [[ -n "$uv" ]] || uv="$PREFIX/uv"
  if [[ -x "$uv" ]] && UV_TOOL_BIN_DIR="$PREFIX" "$uv" tool list 2>/dev/null | grep -q '^cpad '; then
    run env UV_TOOL_BIN_DIR="$PREFIX" "$uv" tool uninstall cpad
  else
    note "没装" "not installed"
  fi
  local profile="$HOME/.zprofile"
  if [[ -f "$profile" ]] && grep -qF "$PATH_MARK" "$profile"; then
    # grep -v exits 1 when nothing is left (a .zprofile of only our line): that is not an error.
    run sh -c '{ grep -vF "$1" "$2" || true; } >"$2.cpa-tmp" && cat "$2.cpa-tmp" >"$2" && rm -f "$2.cpa-tmp"' sh "$PATH_MARK" "$profile"
    note "~/.zprofile 里 PATH 那一行去掉了" "PATH line removed from ~/.zprofile"
  fi
  if [[ -f "$PREFIX/.cardputer-assistant-installed-uv" ]]; then
    run rm -f "$PREFIX/uv" "$PREFIX/uvx" "$PREFIX/.cardputer-assistant-installed-uv"
    note "install.sh 下载的 uv 也删了" "removed the uv install.sh fetched"
  fi
}

notes_dir() {
  local cfg="$CPA_HOME_DIR/config.json" dir=""
  [[ -f "$cfg" ]] && dir="$(plutil -extract notes_dir raw -o - "$cfg" 2>/dev/null || true)"
  printf '%s' "${dir:-~/CardputerNotes}"
}

purge() {
  say "你的数据" "Your data"
  if [[ "$PURGE" == 0 ]]; then
    note "留着：" "kept:"
    note "  设置、钥匙、事件：$CPA_HOME_DIR" "  settings, keys, events: $CPA_HOME_DIR"
    note "  笔记：$NOTES" "  notes: $NOTES"
    note "  模型：$CPA_HOME_DIR/models" "  models: $CPA_HOME_DIR/models"
    note "要连 $CPA_HOME_DIR 一起删，加 --purge-data" "pass --purge-data to delete $CPA_HOME_DIR too"
    return
  fi
  [[ -d "$CPA_HOME_DIR" ]] || { note "没有 $CPA_HOME_DIR" "nothing at $CPA_HOME_DIR"; return; }
  # Only a folder that looks like $CPA_HOME, never HOME, / or a folder above HOME (CPA_HOME may point anywhere).
  local data home notes
  data="$(cd "$CPA_HOME_DIR" && pwd -P)" home="$(cd "$HOME" && pwd -P)"
  if [[ "$data" == / || "$home/" == "$data/"* ]] \
    || [[ ! -f "$CPA_HOME_DIR/config.json" && ! -f "$CPA_HOME_DIR/host_id" ]]; then
    note "$CPA_HOME_DIR 不像数据目录，不删" "$CPA_HOME_DIR does not look like the data folder: kept"; return
  fi
  # The notes folder is never deleted here, so neither is a $CPA_HOME that holds it.
  notes="${NOTES/#\~/$HOME}"
  [[ -d "$notes" ]] && notes="$(cd "$notes" && pwd -P)"
  case "$notes/" in
    "$data/"*|"${CPA_HOME_DIR%/}/"*)
      note "笔记目录在 $CPA_HOME_DIR 里面，不删" "the notes folder is inside $CPA_HOME_DIR: kept"; return ;;
  esac
  if [[ "$DRY" == 1 ]]; then note "(dry-run) 会先问，再 rm -rf $CPA_HOME_DIR" "(dry-run) would ask, then rm -rf $CPA_HOME_DIR"; return; fi
  local typed=""
  can_ask && typed="$(ask "要永久删除 ${CPA_HOME_DIR}（钥匙、事件、设置），输入 delete：" \
    "type delete to erase ${CPA_HOME_DIR} (keys, events, settings) for good:")"
  if [[ "$typed" != delete ]]; then note "没输入 delete，留着" "delete not typed: kept"; return; fi
  rm -rf "$CPA_HOME_DIR"
  note "删了：$CPA_HOME_DIR" "deleted: $CPA_HOME_DIR"
  note "笔记目录从不自动删，要删请在 Finder 里删：$NOTES" "the notes folder is never deleted here: $NOTES"
}

main() {
  parse_args "$@"
  remove_app
  remove_agents
  remove_hooks
  remove_tool
  purge
  say "卸载完了" "Uninstalled"
}

main "$@"
