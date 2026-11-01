// Files of `cpa-sim shots` and `cpa-sim serve`: scripts and WAVs in, screenshots out.
#pragma once

#include <string>

#include "fb.h"
#include "platform.h"

namespace cpasim {

bool read_file(const std::string& path, std::string& bytes);  // false when it cannot be read
std::string safe_name(const std::string& s);  // a file name: anything but [A-Za-z0-9._-] -> '_'
bool write_png(const std::string& path, const Frame& f, int scale);  // scale 1..8, pixels repeated
const char* backlight_name(cpa::Backlight b);  // "on" / "dim" / "off"

}  // namespace cpasim
