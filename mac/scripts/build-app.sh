#!/bin/bash
# Assembles "build/Cardputer Assistant.app" (docs/24 §7): a release build of CardputerAssistant, the
# Info.plist (version from the repo's tags), the icon made from Mando's sprite (scripts/export_mando.py,
# then sips and iconutil), an ad-hoc signature, checked.
# Command Line Tools only; no Xcode, no sandbox, no notarisation (D12). Writes only under mac/build.
# The binary must not name the machine that built it: source paths are mapped to relative ones, symbols are
# stripped, and check_build_paths.sh fails the build if the checkout or $HOME still shows up.
set -euo pipefail
cd "$(dirname "$0")/.."

# The version is the repo's: the newest v* tag behind HEAD (v0.4.0 -> 0.4.0), 0.0.0 when there is none
# (a shallow clone without tags).
# CPA_APP_VERSION (scripts/release-local.sh sets it to the tag being built) wins.
version="${CPA_APP_VERSION:-$(git describe --tags --abbrev=0 --match 'v[0-9]*' 2>/dev/null || true)}"
version="${version#v}"
[[ "$version" =~ ^[0-9]+(\.[0-9]+){0,2}$ ]] || version="0.0.0"

app="build/Cardputer Assistant.app"

root="$(cd .. && pwd -P)"
swift build -c release --product CardputerAssistant \
  -Xswiftc -file-prefix-map -Xswiftc "$root=." -Xswiftc -debug-prefix-map -Xswiftc "$root=."
bin="$(swift build -c release --show-bin-path)/CardputerAssistant"

rm -rf "$app" build/AppIcon.iconset
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$bin" "$app/Contents/MacOS/CardputerAssistant"
strip -S -x "$app/Contents/MacOS/CardputerAssistant"
scripts/check_build_paths.sh "$app/Contents/MacOS/CardputerAssistant" "$root" "$(cd .. && pwd)" "$HOME"
python3 -I scripts/export_mando.py --iconset build/AppIcon.iconset
iconutil -c icns build/AppIcon.iconset -o "$app/Contents/Resources/AppIcon.icns"

cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleIdentifier</key><string>com.cardputer-assistant.mac</string>
  <key>CFBundleName</key><string>Cardputer Assistant</string>
  <key>CFBundleDisplayName</key><string>Cardputer Assistant</string>
  <key>CFBundleExecutable</key><string>CardputerAssistant</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${version}</string>
  <key>CFBundleVersion</key><string>${version}</string>
  <key>CFBundleDevelopmentRegion</key><string>zh_CN</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>LSUIElement</key><false/>
  <key>LSApplicationCategoryType</key><string>public.app-category.productivity</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>Copyright (c) 2026 Heqing Huang. MIT License.</string>
</dict>
</plist>
PLIST
plutil -lint "$app/Contents/Info.plist" >/dev/null

codesign --force --sign - --timestamp=none "$app"
codesign --verify --strict --verbose=1 "$app"
echo "$PWD/$app"
