// PNG writer for Frames (gh #32), standard library only: 8-bit indexed colour with a
// 256-entry palette (index = the theme's palette entry, so pixels are exactly the sprite's), the
// image data in zlib "stored" blocks (no compression: about 33 KB per 240 x 135 shot),
// CRC-32 from lib/rec.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "fb.h"

namespace cpasim {

using Palette = std::array<Rgb, 256>;

Palette panel_palette();  // panel_rgb() of every palette entry
uint32_t adler32(const uint8_t* p, size_t n);
std::string png_indexed(const Frame& f, const Palette& pal);
bool write_file(const std::string& path, const std::string& bytes);

}  // namespace cpasim
