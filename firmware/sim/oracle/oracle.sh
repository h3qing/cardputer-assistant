#!/usr/bin/env bash
# The M5GFX oracle (gh #32), opt-in: builds M5GFX itself for the host (its SDL back end,
# only the sprite is used) next to firmware/sim's FbCanvas, and compares what the two draw
# for every case of test/test_efont/oracle_cases.h, pixel for pixel.
#   firmware/sim/oracle/oracle.sh            compare; exit 1 if any case differs
#   firmware/sim/oracle/oracle.sh --golden   rewrite test/test_efont/oracle_golden.h
# Needs SDL2 (brew install sdl2 / apt install libsdl2-dev) and the M5GFX that
# firmware/app/platformio.ini pins (as gen_font.py: $CPA_M5GFX_DIR, else firmware/app's
# libdeps). M5GFX's objects are cached in firmware/sim/.pio/oracle/<version>.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
SIM=$(dirname "$HERE")
APP=$(cd "$SIM/../app" && pwd)
CXX=${CXX:-c++}
CC=${CC:-cc}

want=$(sed -n 's/.*m5stack\/M5GFX@\([0-9.]*\).*/\1/p' "$APP/platformio.ini" | head -n 1)
[ -n "$want" ] || { echo "oracle: no m5stack/M5GFX@x.y.z pin in $APP/platformio.ini" >&2; exit 2; }

version_of() {
  [ -f "$1/library.json" ] || return 0
  python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["version"])' "$1/library.json"
}

M5GFX=""
libdeps="$APP/.pio/libdeps/cardputer-adv"
for c in "${CPA_M5GFX_DIR:-}" "$libdeps/M5GFX@$want" "$libdeps/M5GFX"; do
  if [ -n "$c" ] && [ "$(version_of "$c")" = "$want" ]; then M5GFX=$c; break; fi
done
[ -n "$M5GFX" ] || { echo "oracle: M5GFX $want not found; run: pio pkg install -d firmware/app -e cardputer-adv" >&2; exit 2; }
command -v sdl2-config > /dev/null || { echo "oracle: needs SDL2 (brew install sdl2 / apt install libsdl2-dev)" >&2; exit 2; }

G="$M5GFX/src"
OUT="$SIM/.pio/oracle"
OBJ="$OUT/$want"
mkdir -p "$OBJ"
read -r -a SDL_CFLAGS <<< "$(sdl2-config --cflags) -I$(sdl2-config --prefix)/include"
read -r -a SDL_LIBS <<< "$(sdl2-config --libs)"
GFX_FLAGS=(-std=c++17 -O1 -w -I"$G" "${SDL_CFLAGS[@]}")

# M5GFX, once per version: the lgfx sources, its utilities and every font lgfx_fonts.cpp names.
if [ ! -f "$OBJ/done" ]; then
  for f in "$G"/lgfx/v1/*.cpp; do "$CXX" "${GFX_FLAGS[@]}" -c "$f" -o "$OBJ/$(basename "$f" .cpp).o"; done
  for f in "$G"/lgfx/utility/*.c "$G"/lgfx/Fonts/efont/*.c "$G"/lgfx/Fonts/IPA/*.c; do
    "$CC" -O0 -w -I"$G" -c "$f" -o "$OBJ/c_$(basename "$f" .c).o"
  done
  touch "$OBJ/done"
fi

# The comparison: oracle_cases.h, the M5GFX side, FbCanvas and what it needs from firmware/app.
INC=(-I"$SIM/test/test_efont" -I"$SIM/lib/simfb" -I"$SIM/lib/efont_cn12/include" -I"$APP/lib/module_api"
     -I"$APP/lib/core" -I"$APP/lib/shell" -I"$APP/lib/theme")
SIM_FLAGS=(-std=c++17 -O1 -Wall -Wextra -Werror "${INC[@]}")
rm -f "$OUT"/*.o
"$CXX" "${GFX_FLAGS[@]}" "${INC[@]}" -c "$HERE/m5gfx_side.cpp" -o "$OUT/m5gfx_side.o"
"$CXX" "${SIM_FLAGS[@]}" -DORACLE_M5GFX_VERSION="\"$want\"" -c "$HERE/main.cpp" -o "$OUT/main.o"
"$CXX" "${SIM_FLAGS[@]}" -c "$HERE/fbcanvas_side.cpp" -o "$OUT/fbcanvas_side.o"
for f in "$SIM/lib/simfb/fb.cpp" "$SIM/lib/simfb/efont.cpp" "$SIM/lib/simfb/fb_canvas.cpp" \
         "$APP/lib/module_api/text_fit.cpp" "$APP/lib/theme/theme_palette.cpp" "$APP/lib/theme/theme_raster.cpp" \
         "$APP/lib/theme/raster_canvas.cpp" "$APP/lib/theme/theme_art.cpp"; do
  "$CXX" "${SIM_FLAGS[@]}" -c "$f" -o "$OUT/sim_$(basename "$f" .cpp).o"
done
# FbCanvas reads lgfx_efont_cn_12 from M5GFX's own lgfx_efont_cn.c here, not gen_font.py's copy.
"$CXX" "$OUT"/*.o "$OBJ"/*.o "${SDL_LIBS[@]}" -o "$OUT/oracle"

if [ "${1:-}" = "--golden" ]; then
  "$OUT/oracle" --golden > "$SIM/test/test_efont/oracle_golden.h"
  echo "oracle: wrote test/test_efont/oracle_golden.h from M5GFX $want"
else
  "$OUT/oracle"
fi
