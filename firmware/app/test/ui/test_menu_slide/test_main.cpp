// The Tab sheet's motion (docs/19 §6.5, S3): opening, the sheet slides in from the right in
// three frames (t = .45, .85, 1); the first is the full redraw (the page dimmed under it),
// the others redraw only the sheet where it is now and return its rectangle to push. A new
// page slides the knob of the segmented control to it, redrawing only the control; the new
// page's name is on the knob from the first frame, and a name it half covers waits.
#include <unity.h>

#include <string>

#include "menu_view.h"
#include "support/fake_canvas.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

namespace {

MenuSource page() {
  MenuSource src;
  src.layer = Layer::List;
  return src;
}

MenuContent content(MenuPage p) { return menu_content(p, page(), AppState{}, SysInfo{}); }

MenuMotion sheet(uint8_t frame) {
  MenuMotion mo;
  mo.slide = MenuSlide::Sheet;
  mo.frame = frame;
  return mo;
}

MenuMotion knob(MenuPage from, uint8_t frame) {
  MenuMotion mo;
  mo.slide = MenuSlide::Knob;
  mo.from = from;
  mo.frame = frame;
  return mo;
}

// The sheet's left edge: its Card fill below the status bar.
int16_t sheet_left(const FakeCanvas& c) {
  for (const auto& f : c.fills) {
    if (f.second == Color::Card && f.first.y == 13 && f.first.h == 109) return f.first.x;
  }
  return -1;
}

const Rect* knob_fill(const FakeCanvas& c) {
  for (const auto& f : c.fills) {
    if (f.second == Color::Accent && f.first.y == 18 && f.first.h == 13) return &f.first;
  }
  return nullptr;
}

std::string active_tab(const FakeCanvas& c) {
  for (const TextOp& t : c.texts) {
    if (t.y == 19 && t.style.fg == Color::OnAccent) return t.text;
  }
  return "";
}

}  // namespace

static void test_the_sheet_slides_in_from_the_right() {
  const MenuContent m = content(MenuPage::Here);
  FakeCanvas rest;
  menu_render(rest, MenuState{}, m);
  const int16_t sx = sheet_left(rest);
  TEST_ASSERT_EQUAL_INT16(menu_sheet_x(rest, m), sx);
  FakeCanvas first;
  menu_render(first, MenuState{}, m, sheet(0));
  TEST_ASSERT_EQUAL_INT16(theme::slide_at(kScreenW, sx, 0), sheet_left(first));
  TEST_ASSERT_EQUAL_UINT32(1, first.dims.size());  // the full redraw dims the page
  int16_t last = sheet_left(first);
  for (uint8_t f = 1; f < theme::kSlideFrames; ++f) {
    FakeCanvas c;
    const Rect r = menu_render_slide(c, MenuState{}, m, sheet(f));
    TEST_ASSERT_TRUE(sheet_left(c) < last);
    last = sheet_left(c);
    TEST_ASSERT_TRUE(r.x == last && r.y == 13 && r.h == 109 && r.x + r.w == kScreenW);
    TEST_ASSERT_EQUAL_UINT32(0, c.dims.size());                    // the page is dimmed already
    TEST_ASSERT_TRUE(c.line(kScreenH - kBarH).empty());            // the hint is drawn already
  }
  TEST_ASSERT_EQUAL_INT16(sx, last);
}

static void test_the_knob_slides_to_the_new_page() {
  const MenuContent m = content(MenuPage::Goto);
  MenuState st;
  st.page = MenuPage::Goto;
  FakeCanvas rest;
  menu_render(rest, st, m);
  const int16_t to_x = knob_fill(rest)->x;
  const int16_t seg_w = static_cast<int16_t>((kScreenW - menu_sheet_x(rest, m) - 9 - 2) / 3);
  const int16_t from_x = static_cast<int16_t>(to_x - seg_w);  // 本页, one to the left
  FakeCanvas first;
  menu_render(first, st, m, knob(MenuPage::Here, 0));
  TEST_ASSERT_EQUAL_INT16(theme::knob_at(from_x, to_x, 0), knob_fill(first)->x);
  TEST_ASSERT_EQUAL_STRING("去哪", active_tab(first).c_str());  // on the knob at once
  FakeCanvas next;
  const Rect r = menu_render_slide(next, st, m, knob(MenuPage::Here, 1));
  TEST_ASSERT_EQUAL_INT16(theme::knob_at(from_x, to_x, 1), knob_fill(next)->x);
  TEST_ASSERT_TRUE(r.y == 17 && r.h == 15 && r.x == menu_sheet_x(next, m) + 5);
  TEST_ASSERT_FALSE(next.has(m.rows[0].label.c_str()));  // the control alone
  FakeCanvas end;
  menu_render_slide(end, st, m, knob(MenuPage::Here, 2));
  TEST_ASSERT_EQUAL_INT16(to_x, knob_fill(end)->x);
}

// A wrap (控制 -> 本页) crosses 去哪: a name the knob half covers is left out of that frame.
static void test_a_half_covered_name_waits() {
  const MenuContent m = content(MenuPage::Here);
  FakeCanvas c;
  menu_render(c, MenuState{}, m, knob(MenuPage::Control, 0));
  int names = 0;
  for (const TextOp& t : c.texts) names += t.y == 19 ? 1 : 0;
  TEST_ASSERT_EQUAL(1, names);
  TEST_ASSERT_TRUE(c.find("控制") != nullptr && c.find("控制")->style.fg == Color::Ink2);
  FakeCanvas rest;
  menu_render(rest, MenuState{}, m, knob(MenuPage::Control, 2));
  TEST_ASSERT_EQUAL_STRING("本页", active_tab(rest).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_sheet_slides_in_from_the_right);
  RUN_TEST(test_the_knob_slides_to_the_new_page);
  RUN_TEST(test_a_half_covered_name_waits);
  return UNITY_END();
}
