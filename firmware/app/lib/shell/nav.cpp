#include "nav.h"

namespace cpa {

NavStack nav_home(uint8_t home_mod) {
  NavStack s;
  s.items[0] = NavEntry{home_mod, 0};
  s.n = 1;
  return s;
}

NavStack nav_open(const NavStack& s, const NavEntry& e) {
  NavStack out = s;
  if (out.n > 0 && out.items[out.n - 1].mod == e.mod) {
    out.items[out.n - 1] = e;  // same module on top: replace its arg
    return out;
  }
  return nav_push(s, e);
}

NavStack nav_push(const NavStack& s, const NavEntry& e) {
  NavStack out = s;
  if (out.n > 0 && out.items[out.n - 1] == e) return out;
  if (out.n >= kNavMax) {
    for (uint8_t i = 1; i < kNavMax; ++i) out.items[i - 1] = out.items[i];
    out.n = static_cast<uint8_t>(kNavMax - 1);
  }
  out.items[out.n++] = e;
  return out;
}

NavStack nav_pop(const NavStack& s, uint8_t home_mod) {
  if (s.n <= 1) return nav_home(home_mod);
  NavStack out = s;
  out.n = static_cast<uint8_t>(s.n - 1);
  return out;
}

NavEntry nav_top(const NavStack& s) { return s.n > 0 ? s.items[s.n - 1] : NavEntry{}; }

}  // namespace cpa
