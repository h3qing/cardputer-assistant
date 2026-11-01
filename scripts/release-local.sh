#!/bin/bash
# Build every asset of a release on this Mac and (only with --publish) upload them with gh. GitHub Actions cannot
# run the macOS part (and is blocked by billing today), so a person runs this on the tag's commit.
#
#   scripts/release-local.sh --tag v0.5.0                  # dry run: build into dist/v0.5.0, upload nothing
#   scripts/release-local.sh --tag v0.5.0 --publish        # also create / update the GitHub release
#
# Assets (what scripts/install.sh reads): cpa-<tag>.bin and cpa-<tag>-app.bin (firmware), cpad-<ver>-py3-none-
# any.whl, pyserial-3.5-py2.py3-none-any.whl (its bytes pinned by daemon/uv.lock), CardputerAssistant-<ver>-arm64.zip
# (ditto, ad-hoc signed), install.sh, uninstall.sh, SHA256SUMS over all of them.
#
#   --out DIR          default dist/<tag>; a folder that exists must be one this script made (it is emptied)
#   --skip-firmware    no PlatformIO build (tests; a release always has firmware)
#   --app PATH         package this .app instead of building one (tests)
#   --release-json     also write release.json shaped like GitHub's API, digests included: install.sh --from DIR
#                      installs from that folder (a USB stick for a Mac without GitHub access) and the tests use it
#   --dev              allow a dirty tree or a HEAD that is not the tag (never with --publish)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MARK=".cardputer-assistant-release"  # in every --out folder this script made: only such a folder is ever emptied
PYSERIAL_URL="https://files.pythonhosted.org/packages/07/bc/587a445451b253b285629263eb51c2d8e9bcea4fc97826266d186f96f558/pyserial-3.5-py2.py3-none-any.whl"

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
fail() { printf 'error: %s\n' "$*" >&2; exit 1; }

parse_args() {
  TAG="" OUT="" PUBLISH=0 SKIP_FW=0 APP="" RELEASE_JSON=0 DEV=0
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --tag) TAG="${2:?}"; shift ;;
      --out) OUT="${2:?}"; shift ;;
      --publish) PUBLISH=1 ;;
      --skip-firmware) SKIP_FW=1 ;;
      --app) APP="${2:?}"; shift ;;
      --release-json) RELEASE_JSON=1 ;;
      --dev) DEV=1 ;;
      -h|--help) sed -n '2,19p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
      *) fail "unknown option: $1" ;;
    esac
    shift
  done
  [[ "$TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "--tag vX.Y.Z is required"
  VERSION="${TAG#v}"
  OUT="${OUT:-$ROOT/dist/$TAG}"
  if [[ "$PUBLISH" == 1 && ( "$DEV" == 1 || "$SKIP_FW" == 1 || -n "$APP" ) ]]; then
    fail "--publish builds everything from the tag: no --dev, --skip-firmware or --app"
  fi
}

# --out is emptied before the build: refuse a folder this script did not make (HOME, ".", a USB stick's root).
check_out() {
  [[ -n "$OUT" && "$OUT" != / ]] || fail "--out cannot be /"
  [[ -e "$OUT" ]] || return 0
  [[ -d "$OUT" ]] || fail "--out $OUT is a file"
  if [[ -n "$(ls -A "$OUT")" && ! -f "$OUT/$MARK" ]]; then
    fail "--out $OUT already holds files this script did not make; pick a new or empty folder"
  fi
}

check_tree() {
  local declared; declared="$(uv version --short --project "$ROOT/daemon")"
  [[ "$declared" == "$VERSION" ]] || fail "daemon is $declared, the tag says $VERSION: uv version $VERSION --project daemon"
  [[ -s "$ROOT/docs/releases/$TAG.md" || "$DEV" == 1 ]] || fail "docs/releases/$TAG.md is missing"
  [[ "$DEV" == 1 ]] && return
  [[ -z "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]] || fail "the tree has changes (or pass --dev)"
  # uv build and swift build take whatever is in the folder: a stray .env or scratch file would ship.
  local stray; stray="$(git -C "$ROOT" status --porcelain --untracked-files=all -- daemon mac firmware \
    | sed -n 's/^?? //p' | grep -vE '(^|/)\.DS_Store$' || true)"
  [[ -z "$stray" ]] || fail "files git does not track would ship (git clean -n -- daemon mac firmware): $stray"
  [[ "$(git -C "$ROOT" rev-parse HEAD)" == "$(git -C "$ROOT" rev-parse "$TAG^{commit}" 2>/dev/null)" ]] \
    || fail "HEAD is not $TAG (git checkout $TAG, or pass --dev)"
}

build_wheels() {
  say "wheel cpad $VERSION"
  local tmp; tmp="$(mktemp -d)"
  uv build --project "$ROOT/daemon" --wheel --out-dir "$tmp" >/dev/null
  cp "$tmp/cpad-$VERSION-py3-none-any.whl" "$OUT/"
  rm -rf "$tmp"
  say "pyserial 3.5 (hash from daemon/uv.lock)"
  local want; want="$(awk '/^name = "pyserial"/{p=1} p && /py2.py3-none-any.whl/{match($0,/sha256:[0-9a-f]+/); print substr($0,RSTART+7,RLENGTH-7); exit}' "$ROOT/daemon/uv.lock")"
  [[ ${#want} == 64 ]] || fail "no pyserial wheel hash in daemon/uv.lock"
  local cached="${CPA_RELEASE_PYSERIAL:-}"
  if [[ -n "$cached" ]]; then cp "$cached" "$OUT/pyserial-3.5-py2.py3-none-any.whl"
  else curl -fsSL --proto '=https' --proto-redir '=https' --tlsv1.2 -o "$OUT/pyserial-3.5-py2.py3-none-any.whl" "$PYSERIAL_URL"; fi
  [[ "$(shasum -a 256 "$OUT/pyserial-3.5-py2.py3-none-any.whl" | awk '{print $1}')" == "$want" ]] \
    || fail "pyserial wheel does not match daemon/uv.lock"
}

build_app() {
  local app="$APP"
  if [[ -z "$app" ]]; then
    say "Mac app (swift build, ad-hoc signed)"
    app="$(CPA_APP_VERSION="$VERSION" "$ROOT/mac/scripts/build-app.sh" | tail -1)"
  fi
  codesign --verify --strict "$app" || fail "$app does not verify"
  local q; q="$(xattr -lr "$app" 2>/dev/null || true)" # not "| grep -q": SIGPIPE + pipefail would miss it
  [[ "$q" != *com.apple.quarantine* ]] || fail "$app is quarantined"
  local stage; stage="$(mktemp -d)"
  ditto "$app" "$stage/Cardputer Assistant.app"
  (cd "$stage" && ditto -c -k --keepParent "Cardputer Assistant.app" "$OUT/CardputerAssistant-$VERSION-arm64.zip")
  rm -rf "$stage"
}

build_firmware() {
  [[ "$SKIP_FW" == 1 ]] && return
  say "firmware $TAG (PlatformIO)"
  local fw="$ROOT/firmware/app" tmp; tmp="$(mktemp -d)"
  (cd "$fw" && pio run -e cardputer-adv >/dev/null && python3 scripts/check_budget.py .pio/build/cardputer-adv \
    && python3 scripts/check_partitions.py .pio/build/cardputer-adv/partitions.bin \
    && python3 scripts/make_image.py --env cardputer-adv --tag "$TAG" --out "$tmp")
  if grep -a -q -E 'debug\.(key|state|reboot|rec|mic|codec)' "$fw/.pio/build/cardputer-adv/firmware.bin"; then
    fail "the release firmware carries test hooks"
  fi
  cp "$tmp/cpa-$TAG.bin" "$tmp/cpa-$TAG-app.bin" "$OUT/"
  rm -rf "$tmp"
}

write_sums() {
  cp "$ROOT/scripts/install.sh" "$ROOT/scripts/uninstall.sh" "$OUT/"
  (cd "$OUT" && rm -f SHA256SUMS release.json && shasum -a 256 -- * >SHA256SUMS && shasum -a 256 -c SHA256SUMS >/dev/null)
  say "SHA256SUMS"
  cat "$OUT/SHA256SUMS"
}

# release.json: the fields install.sh reads from the GitHub API, with the digest GitHub computes (sha256:...).
write_release_json() {
  [[ "$RELEASE_JSON" == 1 ]] || return 0
  local commit; commit="$(git -C "$ROOT" rev-parse HEAD)"
  {
    printf '{"tag_name": "%s", "target_commitish": "%s", "assets": [' "$TAG" "$commit"
    local first=1 f
    for f in "$OUT"/*; do
      [[ "$(basename "$f")" == release.json ]] && continue
      [[ "$first" == 1 ]] || printf ','
      first=0
      printf '\n  {"name": "%s", "url": "-", "browser_download_url": "-", "digest": "sha256:%s"}' \
        "$(basename "$f")" "$(shasum -a 256 "$f" | awk '{print $1}')"
    done
    printf '\n]}\n'
  } >"$OUT/release.json"
}

publish() {
  if [[ "$PUBLISH" == 0 ]]; then
    say "dry run: nothing uploaded. To publish: $0 --tag $TAG --publish"
    return
  fi
  say "uploading to the GitHub release $TAG"
  local files=()
  local f
  for f in "$OUT"/*; do files+=("$f"); done
  if gh release view "$TAG" >/dev/null 2>&1; then
    gh release upload "$TAG" "${files[@]}" --clobber
  else
    gh release create "$TAG" "${files[@]}" --title "$TAG" --notes-file "$ROOT/docs/releases/$TAG.md" --verify-tag
  fi
}

main() {
  parse_args "$@"
  check_out
  check_tree
  rm -rf "$OUT"
  mkdir -p "$OUT"
  : >"$OUT/$MARK"
  build_firmware
  build_wheels
  build_app
  write_sums
  write_release_json
  publish
}

main "$@"
