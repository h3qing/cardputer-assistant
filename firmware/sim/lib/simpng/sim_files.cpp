#include "sim_files.h"

#include <fstream>
#include <sstream>

#include "contact.h"
#include "png.h"

namespace cpasim {

bool read_file(const std::string& path, std::string& bytes) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  bytes = ss.str();
  return true;
}

std::string safe_name(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (!alnum && c != '-' && c != '_' && c != '.') c = '_';
  }
  return out;
}

bool write_png(const std::string& path, const Frame& f, int scale) {
  return write_file(path, png_indexed(scale == 1 ? f : scaled(f, scale), panel_palette()));
}

const char* backlight_name(cpa::Backlight b) {
  return b == cpa::Backlight::Off ? "off" : (b == cpa::Backlight::Dim ? "dim" : "on");
}

}  // namespace cpasim
