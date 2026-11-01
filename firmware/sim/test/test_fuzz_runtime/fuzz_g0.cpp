// gh #51 (docs/20 U26) for the fuzz user: G0 slipping in a held memo (inside the grace and
// past it), double taps at the window's edges (kDoubleTapMs, kLateTapMs), tap-then-hold,
// the Tab menu's row, and hands-free memos ended by one press or by a double tap, with
// pocket presses, a lock and sometimes the cap between.
#include "fuzz.h"
#include "g0_gesture.h"
#include "menu_model.h"
#include "rec_core.h"

namespace fuzz {
namespace {

constexpr uint8_t kDot = 53;    // . (down in the menu)
constexpr uint8_t kSlash = 54;  // / (the next page)

}  // namespace

// G0 let go mid-memo and pressed again: mostly inside the grace (one memo), sometimes past
// it (the memo ends, the re-press is a new hold).
void Fuzz::g0_slip() {
  held = held.without(K::G0);
  run_ms(chance(0.8) ? 20 + rnd(cpa::rec::kMemoGraceMs - 40) : cpa::rec::kMemoGraceMs + rnd(400));
  held = held.with(K::G0);
  run_ms(100 + rnd(1500));
}

// Two taps, the second inside the window, at its edge, in the late window or past it; now
// and then held on to a hold (tap, then hold).
void Fuzz::double_tap() {
  static const uint32_t kGaps[] = {0, cpa::kDoubleTapMs - 40, cpa::kDoubleTapMs - 10, cpa::kDoubleTapMs + 10,
                                   cpa::kLateTapMs - 40, cpa::kLateTapMs + 20};
  pk({K::G0}, 30 + rnd(300));
  run_ms(kGaps[rnd(6)] + rnd(30));
  if (chance(0.15)) return hold_g0(600 + rnd(2000));
  pk({K::G0}, 30 + rnd(300));
}

// The 去哪 row "双击G0 免提说一段", as a person finds it: Tab, /, down to it, Enter.
void Fuzz::menu_hands_free() {
  if (chance(0.7)) escape();
  pk({K::Tab});
  pk({kSlash});
  const cpa::MenuContent m = cpa::menu_content(cpa::MenuPage::Goto, sh().menu_source(), st(), sh().sys());
  uint8_t at = 0;
  while (at < m.n && m.rows[at].ev.kind != cpa::KeyEventKind::G0Double) ++at;
  if (!sh().menu_open() || at == m.n) return;
  for (uint8_t i = 0; i < at; ++i) pk({kDot});
  pk({K::Enter});
}

// A hands-free memo, talked into with pocket presses, sometimes locked or past the cap, then
// usually ended by one G0 press, now and then by two (escape() ends the rest).
void Fuzz::act_hands_free() {
  ++memos;
  if (chance(0.2)) {
    menu_hands_free();
  } else {
    double_tap();
  }
  run_ms(500 + rnd(4000));
  for (int i = rnd(4); i > 0; --i) pk({static_cast<uint8_t>(rnd(56))});
  if (chance(0.3)) pk({K::Opt, K::L});
  if (chance(0.03)) run_ms(300000);
  if (!chance(0.7)) return;
  pk({K::G0});
  if (chance(0.3)) pk({K::G0}, 30 + rnd(100));  // habit: tapping twice to end
}

}  // namespace fuzz
