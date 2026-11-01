// Drawing into the 8-bit palette buffer (docs/19 §3.1, §3.6): clipped fills, rounded
// corners with one baked edge step, remapping a rectangle, pictures with clear pixels, and
// RasterCanvas, which turns tokens into entries through the palette tables.
#include <unity.h>

#include <string>
#include <vector>

#include "raster_canvas.h"
#include "text_fit.h"  // lib/theme is built whole: its canvases need module_api and core
#include "theme_art.h"
#include "theme_palette.h"
#include "theme_raster.h"

using namespace cpa;
using namespace cpa::theme;

void setUp() {}
void tearDown() {}

namespace {

struct Buf {
  std::vector<uint8_t> px;
  Raster r;
  Buf(int16_t w, int16_t h, uint8_t fill = 0) : px(static_cast<size_t>(w) * h, fill), r{px.data(), w, h} {}
  uint8_t at(int x, int y) const { return px[static_cast<size_t>(y) * r.w + x]; }
  int count(uint8_t e) const {
    int n = 0;
    for (uint8_t v : px) n += v == e ? 1 : 0;
    return n;
  }
};

class TestCanvas : public RasterCanvas {
 public:
  explicit TestCanvas(Buf& b) : b_(b) {}
  void text(int16_t, int16_t, const char*, const Style&) override {}
  int16_t textWidth(const char* s) const override { return static_cast<int16_t>(6 * std::string(s).size()); }
  int16_t lineHeight() const override { return 12; }

 protected:
  Raster surface() const override { return b_.r; }

 private:
  Buf& b_;
};

}  // namespace

static void test_fill_is_clipped_and_lgfx_like() {
  Buf b(10, 6);
  fill(b.r, Rect{-3, -2, 5, 4}, 7);
  TEST_ASSERT_EQUAL_HEX8(7, b.at(0, 0));
  TEST_ASSERT_EQUAL_HEX8(7, b.at(1, 1));
  TEST_ASSERT_EQUAL_HEX8(0, b.at(2, 0));
  TEST_ASSERT_EQUAL_HEX8(0, b.at(0, 2));
  fill(b.r, Rect{10, 5, -2, -1}, 9);  // negative sizes extend left / up
  TEST_ASSERT_EQUAL_HEX8(9, b.at(8, 4));
  TEST_ASSERT_EQUAL_HEX8(9, b.at(9, 4));
  TEST_ASSERT_EQUAL(2, b.count(9));
  fill(b.r, Rect{3, 3, 0, 9}, 5);
  fill(b.r, Rect{30, 3, 4, 2}, 5);
  TEST_ASSERT_EQUAL(0, b.count(5));
  const Rect c = clip(b.r, Rect{8, 4, 10, 10});
  TEST_ASSERT_TRUE(c.x == 8 && c.y == 4 && c.w == 2 && c.h == 2);
}

// The circle of a corner: the pixel at the corner point is outside, the one next to the
// centre inside, and the coverage never grows going out.
static void test_corner_cover() {
  TEST_ASSERT_EQUAL_UINT8(16, corner_cover(6, 5, 5));
  TEST_ASSERT_TRUE(corner_cover(6, 0, 0) <= 3);
  TEST_ASSERT_EQUAL_UINT8(13, corner_cover(1, 0, 0));  // a 1 px radius stays square
  for (uint8_t r = 2; r <= 10; ++r) {
    for (uint8_t d = 1; d < r; ++d) TEST_ASSERT_TRUE(corner_cover(r, d, 0) >= corner_cover(r, d - 1, 0));
    TEST_ASSERT_EQUAL_UINT8(corner_cover(r, 0, r - 1), corner_cover(r, r - 1, 0));  // symmetric
  }
}

static void test_round_corners_get_one_edge_step() {
  Buf b(20, 12, 1);
  RoundSpec s;
  s.radius = 6;
  s.paint = 10;
  s.under = 1;
  s.edge = 0x60;
  fill_round(b.r, Rect{2, 0, 16, 12}, s);
  TEST_ASSERT_EQUAL_HEX8(1, b.at(2, 0));     // the corner point: what was under
  TEST_ASSERT_EQUAL_HEX8(1, b.at(17, 11));   // all four
  TEST_ASSERT_EQUAL_HEX8(10, b.at(8, 0));    // the straight top edge
  TEST_ASSERT_EQUAL_HEX8(10, b.at(2, 6));    // the straight left edge
  TEST_ASSERT_EQUAL_HEX8(1, b.at(1, 6));     // nothing outside r
  TEST_ASSERT_TRUE(b.count(0x60) >= 8);      // some edge pixels in every corner
  TEST_ASSERT_EQUAL(b.count(0x60) % 4, 0);   // the same in each
  s.corners = kCornerTL | kCornerBL;         // only the left side (the Tab sheet)
  Buf l(20, 12, 1);
  fill_round(l.r, Rect{2, 0, 16, 12}, s);
  TEST_ASSERT_EQUAL_HEX8(1, l.at(2, 0));
  TEST_ASSERT_EQUAL_HEX8(10, l.at(17, 0));
  TEST_ASSERT_EQUAL(b.count(0x60) / 2, l.count(0x60));
  Buf n(4, 4, 1);
  s.radius = 9;  // cut to half the shorter side
  fill_round(n.r, Rect{0, 0, 4, 2}, s);
  TEST_ASSERT_EQUAL_HEX8(10, n.at(1, 0));
}

static void test_remap_only_inside() {
  Buf b(6, 3, 2);
  uint8_t table[256];
  for (int i = 0; i < 256; ++i) table[i] = static_cast<uint8_t>(i + 1);
  remap(b.r, Rect{1, 1, 3, 9}, table);
  TEST_ASSERT_EQUAL_HEX8(2, b.at(0, 1));
  TEST_ASSERT_EQUAL_HEX8(3, b.at(1, 1));
  TEST_ASSERT_EQUAL_HEX8(3, b.at(3, 2));
  TEST_ASSERT_EQUAL_HEX8(2, b.at(4, 2));
  TEST_ASSERT_EQUAL_HEX8(2, b.at(1, 0));
}

static void test_blit_skips_clear_and_clips() {
  Buf b(4, 3, 9);
  const uint8_t pic[] = {0, 5, 6, 0, 7, 8};
  blit(b.r, 2, 1, pic, 3, 2);
  TEST_ASSERT_EQUAL_HEX8(9, b.at(2, 1));  // clear
  TEST_ASSERT_EQUAL_HEX8(5, b.at(3, 1));  // 6 and 8 fall off the right edge
  TEST_ASSERT_EQUAL_HEX8(9, b.at(2, 2));
  TEST_ASSERT_EQUAL_HEX8(7, b.at(3, 2));
  blit(b.r, -1, -1, pic, 3, 2);  // only the bottom row lands, its first pixel off the left
  TEST_ASSERT_EQUAL_HEX8(7, b.at(0, 0));
  TEST_ASSERT_EQUAL_HEX8(8, b.at(1, 0));
}

static void test_canvas_draws_entries_of_tokens() {
  Buf b(40, 20, 0);
  TestCanvas c(b);
  c.fill(Rect{0, 0, 40, 20}, Color::Bg);
  TEST_ASSERT_EQUAL(800, b.count(index_of(Color::Bg).value()));
  c.fillRound(Rect{0, 0, 20, 12}, 6, Color::Card, Color::Bg);
  TEST_ASSERT_TRUE(b.count(aa_of(Color::Card, Color::Bg)) > 0);
  TEST_ASSERT_EQUAL_UINT32(0, c.aa_misses());
  c.fillRound(Rect{0, 12, 20, 8}, 3, Color::Ok, Color::Rim);  // no edge entry of its own
  TEST_ASSERT_EQUAL_UINT32(1, c.aa_misses());
  c.fillRound(Rect{0, 12, 20, 8}, 0, Color::Ok, Color::Rim);  // square: no edge at all
  TEST_ASSERT_EQUAL_UINT32(1, c.aa_misses());
  c.art(30, 0, ArtId::Head);
  TEST_ASSERT_EQUAL_HEX8(0x2C, b.at(31, 0));  // m13, the ear tip
  TEST_ASSERT_EQUAL_HEX8(index_of(Color::Bg).value(), b.at(30, 0));  // clear
  c.dim(Rect{0, 0, 40, 20});
  TEST_ASSERT_EQUAL(0, b.count(index_of(Color::Card).value()));
  TEST_ASSERT_TRUE(b.count(dim_of(index_of(Color::Card).value())) > 0);
  TEST_ASSERT_EQUAL(0, art_of(static_cast<ArtId>(200)).w);
}

// S3: fillEdge paints the baked edge step of (paint, under), clipped (the waveform's chipped
// bar ends); a v0.4 Canvas paints it plain.
static void test_fill_edge_is_the_baked_step() {
  Buf b(8, 4, 0);
  TestCanvas c(b);
  c.fill(Rect{0, 0, 8, 4}, Color::Bg);
  c.fillEdge(Rect{6, 2, 4, 4}, Color::Accent2, Color::Bg);
  TEST_ASSERT_EQUAL(4, b.count(aa_of(Color::Accent2, Color::Bg)));
  TEST_ASSERT_EQUAL_HEX8(aa_of(Color::Accent2, Color::Bg), b.at(7, 3));
  TEST_ASSERT_EQUAL_UINT32(0, c.aa_misses());
  c.fillEdge(Rect{0, 0, 1, 1}, Color::Ok, Color::Rim);  // no edge entry of its own
  TEST_ASSERT_EQUAL_UINT32(1, c.aa_misses());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fill_is_clipped_and_lgfx_like);
  RUN_TEST(test_corner_cover);
  RUN_TEST(test_round_corners_get_one_edge_step);
  RUN_TEST(test_remap_only_inside);
  RUN_TEST(test_blit_skips_clear_and_clips);
  RUN_TEST(test_canvas_draws_entries_of_tokens);
  RUN_TEST(test_fill_edge_is_the_baked_step);
  return UNITY_END();
}
