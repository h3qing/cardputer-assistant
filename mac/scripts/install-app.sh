#!/bin/bash
# Puts "build/Cardputer Assistant.app" into ~/Applications (docs/24 §7): copied under a temporary name
# first, then swapped in, so a half-copied app never replaces a working one. No sudo.
set -euo pipefail
cd "$(dirname "$0")/.."

src="build/Cardputer Assistant.app"
dest_dir="$HOME/Applications"
dest="$dest_dir/Cardputer Assistant.app"
[ -d "$src" ] || { echo "build it first: scripts/build-app.sh" >&2; exit 1; }

mkdir -p "$dest_dir"
tmp="$dest_dir/.Cardputer Assistant.app.new"
rm -rf "$tmp"
ditto "$src" "$tmp"
codesign --verify --strict "$tmp"
if [ -d "$dest" ]; then
  old="$dest_dir/.Cardputer Assistant.app.old"
  rm -rf "$old"
  mv "$dest" "$old"
  mv "$tmp" "$dest"
  rm -rf "$old"
else
  mv "$tmp" "$dest"
fi
echo "$dest"
