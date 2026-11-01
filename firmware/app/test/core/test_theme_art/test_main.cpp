// Mando and the theme's other pictures (docs/19 §5.1, §7.1): 4-bit frames decoded high nibble
// first into Mando's palette entries, clear pixels and clipping; which pose each act shows and
// its one-word sign; the big ● (the mockups' baked dot) and the lock screen's still glow.
#include <unity.h>

#include <string>
#include <vector>

#include "gen/art_ids.h"
#include "raster_canvas.h"
#include "text_fit.h"  // lib/theme is built whole: its canvases need module_api and core
#include "theme.h"
#include "theme_art.h"
#include "theme_mando.h"
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
  // '#' paint, '+' edge, '.' anything else, row by row.
  std::string map(uint8_t paint, uint8_t edge) const {
    std::string out;
    for (int y = 0; y < r.h; ++y) {
      for (int x = 0; x < r.w; ++x) out += at(x, y) == paint ? '#' : (at(x, y) == edge ? '+' : '.');
      out += '\n';
    }
    return out;
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

ArtId frame46(uint8_t k) { return static_cast<ArtId>(static_cast<uint8_t>(ArtId::Mando46) + k); }
ArtId frame64(uint8_t k) { return static_cast<ArtId>(static_cast<uint8_t>(ArtId::Mando64) + k); }

}  // namespace

static void test_blit4_is_high_nibble_first_with_clear_zero() {
  Buf b(4, 2, 9);
  const uint8_t px[] = {0x12, 0x03, 0xF0, 0x05};
  blit4(b.r, 0, 0, Nibbles{px, 4, 2}, mando_map());
  TEST_ASSERT_EQUAL_HEX8(0x20, b.at(0, 0));
  TEST_ASSERT_EQUAL_HEX8(0x21, b.at(1, 0));
  TEST_ASSERT_EQUAL_HEX8(9, b.at(2, 0));
  TEST_ASSERT_EQUAL_HEX8(0x22, b.at(3, 0));
  TEST_ASSERT_EQUAL_HEX8(0x2E, b.at(0, 1));
  TEST_ASSERT_EQUAL_HEX8(9, b.at(1, 1));
  TEST_ASSERT_EQUAL_HEX8(0x24, b.at(3, 1));
}

static void test_blit4_clips_at_every_edge() {
  Buf b(3, 3, 9);
  const uint8_t px[] = {0x11, 0x11, 0x11, 0x11, 0x10};  // 3 x 3 of colour 1, odd pixel count
  blit4(b.r, -1, -1, Nibbles{px, 3, 3}, mando_map());
  TEST_ASSERT_EQUAL(4, b.count(0x20));
  blit4(b.r, 2, 2, Nibbles{px, 3, 3}, mando_map());
  TEST_ASSERT_EQUAL(5, b.count(0x20));
  blit4(b.r, 5, 0, Nibbles{px, 3, 3}, mando_map());  // wholly outside
  TEST_ASSERT_EQUAL(5, b.count(0x20));
}

static void test_every_mando_frame_is_mando() {
  for (uint8_t k = 0; k < gen::kMando46Frames + gen::kMando64Frames; ++k) {
    const bool small = k < gen::kMando46Frames;
    const ArtId id = small ? frame46(k) : frame64(static_cast<uint8_t>(k - gen::kMando46Frames));
    const ArtView a = art_of(id);
    TEST_ASSERT_TRUE(a.mando4);
    TEST_ASSERT_EQUAL(small ? 26 : 36, a.w);
    TEST_ASSERT_EQUAL(small ? 46 : 64, a.h);
    Buf b(a.w, a.h, 0x01);
    TestCanvas c(b);
    c.art(0, 0, id);
    TEST_ASSERT_EQUAL_HEX8(0x01, b.at(0, 0));  // a clear corner
    int mando = 0;
    for (uint8_t v : b.px) {
      TEST_ASSERT_TRUE(v == 0x01 || (v >= 0x20 && v <= 0x2E));
      mando += v != 0x01 ? 1 : 0;
    }
    TEST_ASSERT_TRUE(mando > a.w * a.h / 3);
  }
  TEST_ASSERT_FALSE(art_of(ArtId::Head).mando4);
  TEST_ASSERT_EQUAL(11, art_of(ArtId::Head).w);
  TEST_ASSERT_EQUAL(0, art_of(ArtId::Count).w);
}

static void test_each_act_shows_its_poses() {
  for (uint8_t p = 0; p < 4; ++p) {
    TEST_ASSERT_TRUE(mando_art(MandoSize::M46, MandoAct::Listen, p) == frame46(gen::kM46Review + p));
    TEST_ASSERT_TRUE(mando_art(MandoSize::M46, MandoAct::Tilt, p) == frame46(gen::kM46Wait + p));
    TEST_ASSERT_TRUE(mando_art(MandoSize::M46, MandoAct::Sniff, p) == frame46(gen::kM46Think + p));
    TEST_ASSERT_TRUE(mando_art(MandoSize::M46, MandoAct::Hop, p) == frame46(gen::kM46Hop + p));
    TEST_ASSERT_TRUE(mando_art(MandoSize::M64, MandoAct::Listen, p) == frame64(gen::kM64Review + p));
    TEST_ASSERT_TRUE(mando_art(MandoSize::M64, MandoAct::Tilt, p) == frame64(gen::kM64Wait + p));
    TEST_ASSERT_TRUE(mando_art(MandoSize::M64, MandoAct::Sniff, p) == frame64(gen::kM64Think + p));
    // the stills: wait 0 (also with "!", mockup 01), wait 12 (the most tilted) for "?", the
    // closed eyes
    TEST_ASSERT_TRUE(mando_art(MandoSize::M46, MandoAct::Idle, p) == frame46(gen::kM46Wait));
    TEST_ASSERT_TRUE(mando_art(MandoSize::M46, MandoAct::NeedsYou, p) == frame46(gen::kM46Wait));
    TEST_ASSERT_TRUE(mando_art(MandoSize::M46, MandoAct::NoVoice, p) == frame46(gen::kM46Wait + 2));
    TEST_ASSERT_TRUE(mando_art(MandoSize::M46, MandoAct::Doze, p) == frame46(gen::kM46Doze));
    // 64 px has no hop and no closed eyes: he sits (docs/19 §7.1)
    TEST_ASSERT_TRUE(mando_art(MandoSize::M64, MandoAct::Hop, p) == frame64(gen::kM64Wait));
    TEST_ASSERT_TRUE(mando_art(MandoSize::M64, MandoAct::Doze, p) == frame64(gen::kM64Wait));
    TEST_ASSERT_TRUE(mando_art(MandoSize::M64, MandoAct::NeedsYou, p) == frame64(gen::kM64Wait));
    TEST_ASSERT_TRUE(mando_art(MandoSize::M64, MandoAct::NoVoice, p) == frame64(gen::kM64Wait + 2));
    TEST_ASSERT_TRUE(mando_art(MandoSize::M64, MandoAct::Idle, p) == frame64(gen::kM64Wait));
  }
  TEST_ASSERT_TRUE(mando_art(MandoSize::M46, MandoAct::Listen, 5) == frame46(gen::kM46Review + 1));  // wraps
  TEST_ASSERT_EQUAL(4, mando_poses(MandoAct::Listen));
  TEST_ASSERT_EQUAL(4, mando_poses(MandoAct::Hop));
  TEST_ASSERT_EQUAL(1, mando_poses(MandoAct::NeedsYou));
}

// Still (nothing animates yet, or the backlight is not On): the poses of mockups 04-07.
static void test_rest_poses_are_the_mockups() {
  TEST_ASSERT_EQUAL(1, mando_rest_pose(MandoAct::Listen));  // review 6
  TEST_ASSERT_EQUAL(1, mando_rest_pose(MandoAct::Hop));     // hop 6
  TEST_ASSERT_EQUAL(2, mando_rest_pose(MandoAct::Sniff));   // think 12
  TEST_ASSERT_EQUAL(0, mando_rest_pose(MandoAct::Tilt));
  TEST_ASSERT_EQUAL(0, mando_rest_pose(MandoAct::Idle));
}

static void test_signs_and_tones() {
  const struct {
    MandoAct a;
    const char* sign;
    Tone t;
  } cases[] = {{MandoAct::Idle, "在呢", Tone::Info},       {MandoAct::Listen, "在听", Tone::Info},
               {MandoAct::Tilt, "在听", Tone::Info},       {MandoAct::Sniff, "嗅一嗅…", Tone::Neutral},
               {MandoAct::NeedsYou, "等你", Tone::Warn},   {MandoAct::Hop, "存好了", Tone::Ok},
               {MandoAct::NoVoice, "没听到", Tone::Neutral}, {MandoAct::Doze, "打盹中", Tone::Neutral}};
  for (const auto& c : cases) {
    TEST_ASSERT_EQUAL_STRING(c.sign, mando_sign(c.a));
    TEST_ASSERT_TRUE(mando_sign_tone(c.a) == c.t);
  }
}

static void test_dot_is_the_mockups_baked_circle() {
  Buf nine(12, 12, 0x01);
  dot(nine.r, 6, 6, 9, 0x19, 0x1A);
  TEST_ASSERT_EQUAL_STRING(
      "............\n"
      ".....++.....\n"
      "...######...\n"
      "..########..\n"
      "..########..\n"
      ".+########+.\n"
      ".+########+.\n"
      "..########..\n"
      "..########..\n"
      "...######...\n"
      ".....++.....\n"
      "............\n",
      nine.map(0x19, 0x1A).c_str());
  Buf five(8, 8, 0x01);
  dot(five.r, 4, 4, 5, 0x19, 0x1A);
  TEST_ASSERT_EQUAL_STRING(
      "........\n"
      "...++...\n"
      "..####..\n"
      ".+####+.\n"
      ".+####+.\n"
      "..####..\n"
      "...++...\n"
      "........\n",
      five.map(0x19, 0x1A).c_str());
  Buf edge(4, 4, 0x01);
  dot(edge.r, 0, 0, 9, 0x19, 0x1A);  // clipped, no wrap-around
  TEST_ASSERT_EQUAL_HEX8(0x19, edge.at(0, 0));
}

static void test_canvas_dot_uses_the_rec_edge() {
  Buf b(12, 12, index_of(Color::Bg).value());
  TestCanvas c(b);
  c.dot(6, 6, 9, Color::RecDot, Color::Bg);
  TEST_ASSERT_EQUAL(60, b.count(index_of(Color::RecDot).value()));
  TEST_ASSERT_EQUAL(8, b.count(aa_of(Color::RecDot, Color::Bg)));
}

static void test_glow_paints_only_bg_and_fades_from_the_corner() {
  const uint8_t bg = index_of(Color::Bg).value();
  Buf b(kScreenW, kScreenH, bg);
  TestCanvas c(b);
  c.fill(Rect{12, 8, 216, 94}, Color::Sheet);
  c.glow(Rect{0, 0, kScreenW, kScreenH});
  const uint8_t sheet = index_of(Color::Sheet).value();
  TEST_ASSERT_EQUAL(216 * 94, b.count(sheet));  // the card is untouched
  const GlowSpec g = glow_spec();
  TEST_ASSERT_TRUE(b.at(0, 0) >= g.base + 3 && b.at(0, 0) < g.base + g.steps);
  TEST_ASSERT_EQUAL_HEX8(bg, b.at(239, 134));
  int corner = 0, middle = 0;  // brighter steps near the centre, on average
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      corner += b.at(x, y) == bg ? 0 : b.at(x, y) - g.base + 1;
      middle += b.at(100 + x, 104 + y) == bg ? 0 : b.at(100 + x, 104 + y) - g.base + 1;
    }
  }
  TEST_ASSERT_TRUE(corner > middle);
}

static void test_glow_follows_its_table() {
  const uint8_t bg = 1;
  Buf b(4, 4, bg);
  b.px[5] = 7;  // not bg: left alone
  const uint32_t q[16 * 2] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
                              17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17};
  GlowSpec g;
  g.cx = 0;
  g.cy = 0;
  g.q = q;
  g.base = 0x40;
  g.steps = 2;
  glow(b.r, Rect{0, 0, 4, 4}, bg, g);
  TEST_ASSERT_EQUAL_HEX8(0x41, b.at(0, 0));  // q = 0 < 17
  TEST_ASSERT_EQUAL_HEX8(0x41, b.at(1, 0));  // q = 16 < 17
  TEST_ASSERT_EQUAL_HEX8(0x40, b.at(2, 0));  // q = 64
  TEST_ASSERT_EQUAL_HEX8(0x40, b.at(0, 1));  // q = 25
  TEST_ASSERT_EQUAL_HEX8(7, b.at(1, 1));
  TEST_ASSERT_EQUAL_HEX8(bg, b.at(3, 3));    // q = 144 + 225
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_blit4_is_high_nibble_first_with_clear_zero);
  RUN_TEST(test_blit4_clips_at_every_edge);
  RUN_TEST(test_every_mando_frame_is_mando);
  RUN_TEST(test_each_act_shows_its_poses);
  RUN_TEST(test_rest_poses_are_the_mockups);
  RUN_TEST(test_signs_and_tones);
  RUN_TEST(test_dot_is_the_mockups_baked_circle);
  RUN_TEST(test_canvas_dot_uses_the_rec_edge);
  RUN_TEST(test_glow_paints_only_bg_and_fades_from_the_corner);
  RUN_TEST(test_glow_follows_its_table);
  return UNITY_END();
}
