// Navigation stack (05 §5.6): at most 4 levels of (module, arg). Opening what is on
// top replaces its arg; a 5th level drops the bottom one; popping the last level
// goes back to the root page. v0.4.0: nav_push adds a level even over the same module
// (the board's overview opening a project), so back returns to the overview.
#pragma once

#include <cstdint>

namespace cpa {

constexpr uint8_t kNavMax = 4;

struct NavEntry {
  uint8_t mod = 0;  // index in the module list
  uint8_t arg = 0;
  bool operator==(const NavEntry& o) const { return mod == o.mod && arg == o.arg; }
};

struct NavStack {
  uint8_t n = 0;
  NavEntry items[kNavMax];
};

NavStack nav_home(uint8_t home_mod);
NavStack nav_open(const NavStack& s, const NavEntry& e);
NavStack nav_push(const NavStack& s, const NavEntry& e);  // e already on top: unchanged
NavStack nav_pop(const NavStack& s, uint8_t home_mod);
NavEntry nav_top(const NavStack& s);

}  // namespace cpa
