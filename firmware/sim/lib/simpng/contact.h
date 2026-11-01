// Contact sheet (gh #32): the screenshots of a run in a grid, each with its caption on
// a black strip under it, on a grey background, drawn with the device's font.
#pragma once

#include <string>
#include <vector>

#include "fb.h"

namespace cpasim {

struct Captioned {
  const Frame* frame;
  std::string caption;
};

Frame contact_sheet(const std::vector<Captioned>& shots, int columns);

}  // namespace cpasim
