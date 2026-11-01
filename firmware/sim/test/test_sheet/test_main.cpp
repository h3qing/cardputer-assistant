// The contact sheet and the files of `cpa-sim shots`: grid layout and captions (any
// number of shots), file names, scaled PNGs.
#include <unity.h>

#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

#include "contact.h"
#include "fb.h"
#include "png.h"
#include "sim_files.h"

using namespace cpasim;

void setUp() {}
void tearDown() {}

namespace {

const uint8_t kAccent = entry_of(cpa::Color::Accent);  // captions
const uint8_t kSheetBg = entry_of(cpa::Color::Line);

int count(const Frame& f, int x0, int y0, int w, int h, uint8_t px) {
  int n = 0;
  for (int y = y0; y < y0 + h; ++y) {
    for (int x = x0; x < x0 + w; ++x) n += f.at(x, y) == px ? 1 : 0;
  }
  return n;
}

}  // namespace

// 8 px gaps; each cell is the shot with a 14 px caption strip under it.
static void test_grid_layout() {
  const Frame a(10, 4, 0xE0);
  const Frame b(10, 4, 0x1C);
  const Frame sheet = contact_sheet({{&a, "a"}, {&b, "b"}, {&a, "c"}}, 2);
  TEST_ASSERT_EQUAL(8 + 2 * (10 + 8), sheet.w());
  TEST_ASSERT_EQUAL(8 + 2 * (4 + 14 + 8), sheet.h());
  TEST_ASSERT_EQUAL(40, count(sheet, 8, 8, 10, 4, 0xE0));
  TEST_ASSERT_EQUAL(40, count(sheet, 26, 8, 10, 4, 0x1C));
  TEST_ASSERT_EQUAL(40, count(sheet, 8, 34, 10, 4, 0xE0));
  TEST_ASSERT_EQUAL(18 * 26, count(sheet, 26, 34, 18, 26, kSheetBg));  // no fourth cell
}

static void test_empty_sheet_is_one_blank_cell() {
  const Frame sheet = contact_sheet({}, 4);
  TEST_ASSERT_EQUAL(8 + 240 + 8, sheet.w());
  TEST_ASSERT_EQUAL(sheet.w() * sheet.h(), count(sheet, 0, 0, sheet.w(), sheet.h(), kSheetBg));
}

// Past 32767 px down (about 1400 short shots in one column) the captions still land
// where their shots are.
static void test_tall_sheet_keeps_its_captions() {
  const Frame thin(40, 1, 0xFF);
  std::vector<Captioned> shots(1500, Captioned{&thin, "1500"});
  const Frame sheet = contact_sheet(shots, 1);
  const int last_strip = 8 + 1499 * (1 + 14 + 8) + 1;
  TEST_ASSERT_EQUAL(last_strip + 14 + 8, sheet.h());
  TEST_ASSERT_TRUE(count(sheet, 8, last_strip, 40, 14, kAccent) > 20);
  TEST_ASSERT_EQUAL(40, count(sheet, 8, last_strip - 1, 40, 1, 0xFF));
}

static void test_safe_name() {
  TEST_ASSERT_EQUAL_STRING("board-home_2.v1", safe_name("board-home_2.v1").c_str());
  TEST_ASSERT_EQUAL_STRING("a_b_c_d", safe_name("a b/c?d").c_str());
  TEST_ASSERT_EQUAL_STRING("___", safe_name("看").c_str());  // each UTF-8 byte
}

static void test_write_png_scales_by_repeating_pixels() {
  Frame f(3, 2);
  f.fill_rect(1, 1, 1, 1, 0xE0);
  std::string path = (std::filesystem::temp_directory_path() / "cpa-sim-test-XXXXXX").string();
  const int fd = mkstemp(&path[0]);
  TEST_ASSERT_TRUE(fd >= 0);
  close(fd);
  TEST_ASSERT_TRUE(write_png(path, f, 3));
  std::string bytes;
  TEST_ASSERT_TRUE(read_file(path, bytes));
  unlink(path.c_str());
  TEST_ASSERT_TRUE(bytes == png_indexed(scaled(f, 3), panel_palette()));
  TEST_ASSERT_FALSE(write_png("/nonexistent-dir/x.png", f, 1));
}

static void test_backlight_names() {
  TEST_ASSERT_EQUAL_STRING("on", backlight_name(cpa::Backlight::On));
  TEST_ASSERT_EQUAL_STRING("dim", backlight_name(cpa::Backlight::Dim));
  TEST_ASSERT_EQUAL_STRING("off", backlight_name(cpa::Backlight::Off));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_grid_layout);
  RUN_TEST(test_empty_sheet_is_one_blank_cell);
  RUN_TEST(test_tall_sheet_keeps_its_captions);
  RUN_TEST(test_safe_name);
  RUN_TEST(test_write_png_scales_by_repeating_pixels);
  RUN_TEST(test_backlight_names);
  return UNITY_END();
}
