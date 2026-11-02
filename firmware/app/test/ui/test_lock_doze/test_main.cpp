// The lit lock card's z's (docs/19 §6.8, §7.3, S3): Mando dozes with up to three z's lit,
// one more every 600 ms. A tick redraws only the z's cells: the glow above the card, its Rim
// edge, the card and the edge of Mando under them, then the lit z's; the card moves down
// clear of the ● line while recording, and so do the cells.
#include <unity.h>

#include <string>

#include "core/support/lang_pin.h"
#include "lock_screen.h"
#include "support/fake_canvas.h"
#include "support/fixtures.h"
#include "theme_mando.h"

using namespace cpa;
using namespace uitest;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

namespace {

int zs(const FakeCanvas& c) {
  int n = 0;
  for (const TextOp& t : c.texts) n += t.text == "z" || t.text == "Z" ? 1 : 0;
  return n;
}

}  // namespace

static void test_the_card_shows_the_zs_lit() {
  const AppState s = demo_state();
  for (uint8_t z = 0; z <= 3; ++z) {
    FakeCanvas c;
    render_lock_screen(c, s, true, 1000, z);
    TEST_ASSERT_EQUAL(z, zs(c));
  }
  FakeCanvas whole;
  render_lock_screen(whole, s, true, 1000);  // still: all three (mockup 06)
  TEST_ASSERT_EQUAL(3, zs(whole));
  TEST_ASSERT_TRUE(whole.find("Z")->style.fg == Color::Ink);
}

static void test_a_tick_redraws_only_the_z_cells() {
  AppState s = demo_state();
  FakeCanvas c;
  Rect r = lock_z_part(c, s, 1, 1000);
  TEST_ASSERT_TRUE(r.x == 47 && r.y == 7 && r.w == 18 && r.h == 27);
  TEST_ASSERT_EQUAL(1, zs(c));
  TEST_ASSERT_EQUAL_UINT32(1, c.glows.size());  // the glow above the card, in the cells only
  TEST_ASSERT_TRUE(c.glows[0].x == r.x && c.glows[0].w == r.w);
  TEST_ASSERT_TRUE(c.filled_at(8, Color::Rim) && c.filled_at(20, Color::Sheet));
  TEST_ASSERT_TRUE(c.arts.size() == 1 && c.arts[0].id == theme::mando_art(theme::MandoSize::M46, theme::MandoAct::Doze, 0));
  for (const TextOp& t : c.texts) TEST_ASSERT_TRUE(t.x >= r.x && t.x + 6 <= r.x + r.w && t.y >= r.y);
  s.rec.phase = RecPhase::Recording;  // the card is lower, clear of the ● line
  FakeCanvas d;
  r = lock_z_part(d, s, 3, 1000);
  TEST_ASSERT_TRUE(r.y == 13 && r.y > 11);
  TEST_ASSERT_EQUAL(3, zs(d));
}

static bool overlaps(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// The judges' readability notes (docs/19 §6.8): locked while recording, the ● line is the
// status bar's slot, Rec on a RecBg capsule at its left (4.8:1), lit card or dark, and the
// glow keeps 2 px clear of it (a hard edge of black cut out of the glow read as a glitch).
static void test_the_rec_line_sits_in_its_capsule() {
  AppState s = demo_state();
  s.rec.phase = RecPhase::Recording;
  s.rec.mode = RecMode::Meeting;
  s.rec.started_ms = 0;
  s.rec.stars = 3;
  for (bool card : {true, false}) {
    for (RecMode mode : {RecMode::Meeting, RecMode::Memo}) {
      s.rec.mode = mode;
      FakeCanvas c;
      render_lock_screen(c, s, card, 9000);
      const TextOp* mark = c.find(mode == RecMode::Meeting ? "●0:09 ★3" : "● REC 0:09");
      TEST_ASSERT_NOT_NULL_MESSAGE(mark, c.dump().c_str());
      TEST_ASSERT_TRUE(mark->x == 4 && mark->y == 0 && mark->style.fg == Color::Rec);
      Rect capsule{};
      for (const auto& f : c.fills) {
        if (f.second == Color::RecBg) capsule = f.first;
      }
      TEST_ASSERT_TRUE(capsule.x == 1 && capsule.y == 0 && capsule.h == theme::kCapH);
      TEST_ASSERT_EQUAL_INT16(c.textWidth(mark->text.c_str()) + 6, capsule.w);
      const Rect clear{0, 0, static_cast<int16_t>(capsule.x + capsule.w + 2), static_cast<int16_t>(capsule.h + 2)};
      for (const Rect& g : c.glows) TEST_ASSERT_FALSE(overlaps(g, clear));
      TEST_ASSERT_EQUAL_UINT32(card ? 2 : 0, c.glows.size());  // below it, and right of it
    }
  }
  s.rec.phase = RecPhase::Cue;
  FakeCanvas cue;
  render_lock_screen(cue, s, false, 9000);
  TEST_ASSERT_TRUE(cue.find("● 准备")->style.fg == Color::Warn && cue.filled_at(0, Color::WarnBg));
  s.rec.phase = RecPhase::Recording;
  FakeCanvas z;
  const Rect r = lock_z_part(z, s, 3, 9000);
  for (const Rect& g : z.glows) TEST_ASSERT_TRUE(g.y >= 14 || g.x >= 60);  // clear of the capsule too
  TEST_ASSERT_TRUE(r.y == 13);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_card_shows_the_zs_lit);
  RUN_TEST(test_a_tick_redraws_only_the_z_cells);
  RUN_TEST(test_the_rec_line_sits_in_its_capsule);
  return UNITY_END();
}
