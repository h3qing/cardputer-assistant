// Frame: the device's palette_8bit sprite on the host. Each token is drawn as its palette
// entry (lib/theme), and the panel shows that entry's RGB565.
#include <unity.h>

#include "fb.h"
#include "fb_canvas.h"
#include "sim_display.h"

using namespace cpasim;
using cpa::Color;

constexpr int kW = 240;

void setUp() {}
void tearDown() {}

static void test_tokens_are_their_palette_entries() {
  TEST_ASSERT_EQUAL_HEX8(0x01, entry_of(Color::Bg));
  TEST_ASSERT_EQUAL_HEX8(0x02, entry_of(Color::Fg));
  TEST_ASSERT_EQUAL_HEX8(0x03, entry_of(Color::Dim));
  TEST_ASSERT_EQUAL_HEX8(0x04, entry_of(Color::Accent));
  TEST_ASSERT_EQUAL_HEX8(0x05, entry_of(Color::Ok));
  TEST_ASSERT_EQUAL_HEX8(0x06, entry_of(Color::Warn));
  TEST_ASSERT_EQUAL_HEX8(0x07, entry_of(Color::Error));
  TEST_ASSERT_EQUAL_HEX8(0x08, entry_of(Color::Select));
  TEST_ASSERT_EQUAL_HEX8(0x11, entry_of(Color::Ink2));
}

static void expect_rgb(Rgb want, Rgb got) {
  TEST_ASSERT_EQUAL_UINT8(want.r, got.r);
  TEST_ASSERT_EQUAL_UINT8(want.g, got.g);
  TEST_ASSERT_EQUAL_UINT8(want.b, got.b);
}

// The entry's RGB888 truncated to RGB565 as M5GFX pushes the sprite, then 8 bits per channel.
static void test_panel_rgb() {
  expect_rgb(Rgb{16, 12, 8}, panel_rgb(entry_of(Color::Bg)));       // #120e0b shows as #100c08
  expect_rgb(Rgb{247, 239, 231}, panel_rgb(entry_of(Color::Fg)));   // #f3ede6
  expect_rgb(Rgb{148, 138, 123}, panel_rgb(entry_of(Color::Dim)));  // ink3 #96897b
  expect_rgb(Rgb{255, 0, 255}, panel_rgb(0x00));                     // a sentinel: magenta
  expect_rgb(Rgb{255, 0, 255}, panel_rgb(0xE0));
}

static void test_frame_starts_filled() {
  const Frame f(4, 3, 0x12);
  TEST_ASSERT_EQUAL(4, f.w());
  TEST_ASSERT_EQUAL(3, f.h());
  TEST_ASSERT_EQUAL_size_t(12, f.pixels().size());
  TEST_ASSERT_EQUAL_HEX8(0x12, f.at(3, 2));
  TEST_ASSERT_EQUAL_HEX8(0x00, f.at(4, 0));  // outside
  TEST_ASSERT_EQUAL_HEX8(0x00, f.at(-1, 0));
}

static void test_fill_rect_is_clipped() {
  Frame f(10, 10);
  f.fill_rect(-5, -5, 10, 10, 0xFF);
  TEST_ASSERT_EQUAL_HEX8(0xFF, f.at(0, 0));
  TEST_ASSERT_EQUAL_HEX8(0xFF, f.at(4, 4));
  TEST_ASSERT_EQUAL_HEX8(0x00, f.at(5, 4));
  TEST_ASSERT_EQUAL_HEX8(0x00, f.at(4, 5));
  f.fill_rect(8, 8, 50, 50, 0x1C);
  TEST_ASSERT_EQUAL_HEX8(0x1C, f.at(9, 9));
  TEST_ASSERT_EQUAL_HEX8(0x00, f.at(7, 9));
}

// LGFX fillRect: a negative width or height extends left / up; zero draws nothing.
static void test_fill_rect_negative_and_empty() {
  Frame f(10, 2);
  f.fill_rect(10, 0, -3, 1, 0xE0);
  TEST_ASSERT_EQUAL_HEX8(0x00, f.at(6, 0));
  TEST_ASSERT_EQUAL_HEX8(0xE0, f.at(7, 0));
  TEST_ASSERT_EQUAL_HEX8(0xE0, f.at(9, 0));
  f.fill_rect(0, 1, 0, 1, 0xFF);
  f.fill_rect(0, 1, 5, 0, 0xFF);
  TEST_ASSERT_EQUAL_HEX8(0x00, f.at(0, 1));
}

static void test_blit_copies_and_clips() {
  Frame src(3, 2, 0x1F);
  Frame dst(4, 4);
  dst.blit(src, 2, 3);
  TEST_ASSERT_EQUAL_HEX8(0x1F, dst.at(2, 3));
  TEST_ASSERT_EQUAL_HEX8(0x1F, dst.at(3, 3));
  TEST_ASSERT_EQUAL_HEX8(0x00, dst.at(1, 3));
  TEST_ASSERT_EQUAL_HEX8(0x00, dst.at(2, 2));
}

static void test_scaled_repeats_pixels() {
  Frame f(2, 1);
  f.fill_rect(1, 0, 1, 1, 0xFF);
  const Frame s = scaled(f, 3);
  TEST_ASSERT_EQUAL(6, s.w());
  TEST_ASSERT_EQUAL(3, s.h());
  TEST_ASSERT_EQUAL_HEX8(0x00, s.at(2, 2));
  TEST_ASSERT_EQUAL_HEX8(0xFF, s.at(3, 0));
  TEST_ASSERT_EQUAL_HEX8(0xFF, s.at(5, 2));
}

// missing_glyphs(): the □ in the frame on the panel, taken at each present(); the
// runtime redraws the whole screen for every frame.
static void test_display_counts_boxes_per_frame() {
  SimDisplay d;
  d.canvas().text(0, 0, "\xE2\x9C\x93 \xE2\x9C\x93", cpa::Style{});
  TEST_ASSERT_EQUAL_UINT32(0, d.missing_glyphs());  // nothing presented yet
  d.present();
  TEST_ASSERT_EQUAL_UINT32(2, d.missing_glyphs());
  d.canvas().text(0, 0, "\xE2\x9C\x93", cpa::Style{});
  TEST_ASSERT_EQUAL_UINT32(2, d.missing_glyphs());  // the panel still shows the first frame
  d.present();
  TEST_ASSERT_EQUAL_UINT32(1, d.missing_glyphs());
  d.present();
  TEST_ASSERT_EQUAL_UINT32(0, d.missing_glyphs());
}

// docs/21 §9.2: text whose ink lights the frame's last column runs into the edge, even
// when its advance does not pass it ("now" flush right: the w's stroke at x = 239).
static void test_ink_in_the_last_column_is_past_the_edge() {
  Frame f(kW, 40, entry_of(Color::Bg));
  FbCanvas c(f);
  const cpa::Style ink{Color::Ink, Color::Bg};
  c.text(static_cast<int16_t>(kW - 2 - c.textWidth("now")), 0, "now", ink);
  TEST_ASSERT_EQUAL_UINT32(0, c.offscreen_texts());  // 2 px inside
  c.text(static_cast<int16_t>(kW - c.textWidth("now")), 20, "now", ink);
  TEST_ASSERT_EQUAL_UINT32(1, c.offscreen_texts());
  c.text(static_cast<int16_t>(kW - c.textWidth("now")), 20, "now", ink);  // over itself
  TEST_ASSERT_EQUAL_UINT32(2, c.offscreen_texts());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tokens_are_their_palette_entries);
  RUN_TEST(test_panel_rgb);
  RUN_TEST(test_frame_starts_filled);
  RUN_TEST(test_fill_rect_is_clipped);
  RUN_TEST(test_fill_rect_negative_and_empty);
  RUN_TEST(test_blit_copies_and_clips);
  RUN_TEST(test_scaled_repeats_pixels);
  RUN_TEST(test_display_counts_boxes_per_frame);
  RUN_TEST(test_ink_in_the_last_column_is_past_the_edge);
  return UNITY_END();
}
