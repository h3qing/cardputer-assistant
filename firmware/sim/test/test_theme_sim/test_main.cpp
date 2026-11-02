// The theme on the simulator (docs/19 §3.2, §9.2): the whole v0.4.0 tour drawn on the
// palette canvas has no sentinel pixel (a stray RGB565 constant would show magenta), every
// rounded shape had its own edge colour, and ● is lit in the status bar's left slot (or the
// lock screen's line) on every recording shot. S2: Mando's states (theme_states.cpas) too,
// and every word S2 draws has a glyph in efontCN_12. S3: the motion frames (theme_motion.cpas)
// too; at every moment the panel holds all that was drawn (a tick pushes what it drew); the
// breathing entries show only in the big ●'s rectangle; Mando and the waveform move between
// frames at On and nothing but the clocks moves at Dim; a tick pushes a few small rectangles.
#include <unity.h>

#include <string>

#include "fb.h"
#include "fb_canvas.h"
#include "player.h"
#include "script.h"
#include "sim_device.h"
#include "sim_files.h"
#include "theme.h"
#include "theme_palette.h"

using namespace cpasim;

void setUp() {}
void tearDown() {}

namespace {

PlayResult played(const char* path) {
  const Script s = parse_script(path, read_file);  // pio test runs in firmware/sim
  TEST_ASSERT_EQUAL_STRING("", s.error.c_str());
  return play(s.steps);
}

PlayResult tour() { return played("scripts/v04_tour.cpas"); }

PlayResult motion() { return played("scripts/theme_motion.cpas"); }

const Shot& named(const PlayResult& r, const std::string& name) {
  for (const Shot& s : r.shots) {
    if (s.name == name) return s;
  }
  TEST_FAIL_MESSAGE(name.c_str());
  return r.shots.front();
}

// Pixels that differ between two frames inside a rectangle.
int changed(const Frame& a, const Frame& b, int x0, int y0, int w, int h) {
  int n = 0;
  for (int y = y0; y < y0 + h; ++y) {
    for (int x = x0; x < x0 + w; ++x) n += a.at(x, y) != b.at(x, y) ? 1 : 0;
  }
  return n;
}

// An entry the breath swaps in or the RecDot rim: only the big ● may show these.
bool breathing(uint8_t e) {
  if (e == cpa::theme::rec_dot_edge() || e == entry_of(cpa::Color::RecDot)) return true;
  for (uint8_t s = 0; s < cpa::theme::kBreathSteps; ++s) {
    if (e == cpa::theme::breath_dot(s) || e == cpa::theme::breath_edge(s)) return true;
  }
  return false;
}

void assert_only_palette_entries(const PlayResult& r) {
  TEST_ASSERT_EQUAL_STRING("", r.error.c_str());
  for (const Shot& s : r.shots) {
    for (const uint8_t px : s.frame.pixels()) {
      TEST_ASSERT_FALSE_MESSAGE(cpa::theme::is_sentinel(px), s.name.c_str());
      TEST_ASSERT_TRUE_MESSAGE(cpa::theme::rgb888_of(px) != 0xFF00FF, s.name.c_str());
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s.missing_glyphs, s.name.c_str());
  }
  TEST_ASSERT_EQUAL_UINT32(0, r.shots.back().aa_misses);
}

int count(const Frame& f, int x0, int y0, int w, int h, uint8_t e) {
  int n = 0;
  for (int y = y0; y < y0 + h; ++y) {
    for (int x = x0; x < x0 + w; ++x) n += f.at(x, y) == e ? 1 : 0;
  }
  return n;
}

bool recording(const std::string& name) {
  static const char* const kNames[] = {"menu-memo",          "lock-memo",          "meeting",
                                       "menu-meeting",       "menu-meeting-scroll", "menu-meeting-control",
                                       "menu-meeting-g0",    "meeting-toast",       "menu-meeting-toast",
                                       "lock-meeting",       "lock-meeting-star",   "lock-meeting-card",
                                       "meeting-unlocked",   "meeting-end",         "menu-meeting-end"};
  for (const char* n : kNames) {
    if (name == n) return true;
  }
  return false;
}

}  // namespace

static void test_the_tour_draws_only_palette_entries() {
  const PlayResult r = tour();
  TEST_ASSERT_TRUE(r.shots.size() > 60);
  assert_only_palette_entries(r);
}

// docs/19 §7.2: one shot per Mando state; his pixels are his colours, nothing else is new.
static void test_mandos_states_draw_only_palette_entries() {
  const PlayResult r = played("scripts/theme_states.cpas");
  TEST_ASSERT_EQUAL(10, r.shots.size());
  assert_only_palette_entries(r);
  int mando = 0;
  for (uint8_t e = 0x20; e <= 0x2E; ++e) mando += count(r.shots[0].frame, 7, 14, 26, 46, e);
  TEST_ASSERT_TRUE(mando > 400);  // home: Mando at (7, 14)
}

// docs/20 B1-B6: the lock card's results and "Not recording", the recording pages' countdown
// and battery, the status bar's "● Wait" and ※SD, home offline and its faded Mando, the inbox
// and board's per-row words, the confirm keycaps and a selected note row, and (gh #53) the
// recording pages' legend of 1-9 with its lit choice: palette entries only, a glyph for every
// word, an edge colour of its own for every rounded shape (tokens.AA_PAIRS), on every shot.
static void test_the_ux_b_scripts_draw_only_palette_entries() {
  for (const char* path : {"scripts/ux/lock_card.cpas", "scripts/ux/rec_pages.cpas", "scripts/ux/status_bar.cpas",
                           "scripts/ux/home.cpas", "scripts/ux/inbox_board.cpas", "scripts/ux/copy_layout.cpas",
                           "scripts/ux/memo_legend.cpas"}) {
    const PlayResult r = played(path);
    assert_only_palette_entries(r);
    for (const Shot& s : r.shots) TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s.aa_misses, s.name.c_str());
  }
}

static void test_s2_words_have_glyphs() {
  static const char* const kWords[] = {
      "早上好，", "中午好，", "下午好，", "晚上好，", "你好，",   "都处理完了", "1 件事",   "在等你",
      "等你回复", "在呢",     "在听",     "嗅一嗅…",  "等你",     "存好了",     "没听到",   "打盹中",
      "语音便条", "存到",     "未归属",   "保存中…",  "开会",     "第 1 段",    "★ 已标记", "已锁屏",
      "z",        "Z",        "!",        "?",        "● 准备",   "看到 ● REC 再说话"};
  Frame f(cpa::kScreenW, 12);
  FbCanvas c(f);
  for (const char* w : kWords) c.text(0, 0, w, cpa::theme::over(cpa::Color::Ink));
  TEST_ASSERT_EQUAL_UINT32(0, c.missing_glyphs());
}

static void test_the_dot_is_lit_on_every_recording_shot() {
  const PlayResult r = tour();
  const uint8_t rec = entry_of(cpa::Color::Rec);
  int seen = 0;
  for (const Shot& s : r.shots) {
    if (!recording(s.name)) continue;
    ++seen;
    TEST_ASSERT_TRUE_MESSAGE(count(s.frame, 0, 0, 80, 12, rec) >= 20, s.name.c_str());
  }
  TEST_ASSERT_EQUAL(15, seen);
}

// S3: every frame of every animation draws palette entries only, with glyphs and edges.
static void test_the_motion_frames_draw_only_palette_entries() {
  const PlayResult r = motion();
  TEST_ASSERT_EQUAL(41, r.shots.size());
  assert_only_palette_entries(r);
}

// At every loop where the screen is up to date, the panel is the sprite: what a tick or a
// slide frame drew, it pushed (docs/19 §3.6: partial pushes are faithful).
static void test_the_panel_holds_all_that_was_drawn() {
  for (const char* path : {"scripts/theme_motion.cpas", "scripts/v04_tour.cpas"}) {
    const Script s = parse_script(path, read_file);
    int checked = 0, stale = 0;
    const PlayResult r = play(s.steps, [&](const SimDevice& d) {
      if (!d.drawn()) return;
      ++checked;
      stale += d.panel_is_sprite() ? 0 : 1;
    });
    TEST_ASSERT_EQUAL_STRING("", r.error.c_str());
    TEST_ASSERT_TRUE(checked > 1000);
    TEST_ASSERT_EQUAL_MESSAGE(0, stale, path);
  }
}

// Only the big ● breathes: its entries never show outside its 12 x 12 rectangle.
static void test_only_the_big_dot_breathes() {
  for (const PlayResult& r : {motion(), tour()}) {
    int lit = 0;
    for (const Shot& s : r.shots) {
      for (int y = 0; y < s.frame.h(); ++y) {
        for (int x = 0; x < s.frame.w(); ++x) {
          if (!breathing(s.frame.at(x, y))) continue;
          ++lit;
          const bool memo = x >= 60 && x < 72, meeting = x >= 46 && x < 58;
          TEST_ASSERT_TRUE_MESSAGE((memo || meeting) && y >= 38 && y < 50, s.name.c_str());
        }
      }
    }
    TEST_ASSERT_TRUE(lit > 0);
  }
  const PlayResult m = motion();
  bool stepped = false;  // the memo frames catch the ● at more than one step
  for (int i = 2; i <= 4; ++i) {
    stepped = stepped || changed(named(m, "memo-voice-1").frame, named(m, "memo-voice-" + std::to_string(i)).frame, 60,
                                 38, 12, 12) > 0;
  }
  TEST_ASSERT_TRUE(stepped);
}

// On: Mando and the waveform move tick by tick, a tick pushes small rectangles, no full frame
// between the once-a-second ones. Dim: no partial push, Mando, the timeline and the ● as they
// were; only the clocks moved.
static void test_on_it_moves_and_at_dim_it_holds_still() {
  const PlayResult r = motion();
  const Frame& a = named(r, "memo-voice-1").frame;
  const Frame& b = named(r, "memo-voice-2").frame;
  TEST_ASSERT_TRUE(changed(a, b, 62, 64, 173, 23) > 50);  // the waveform scrolled
  int nod = 0;
  for (int i = 2; i <= 4; ++i) nod += changed(a, named(r, "memo-voice-" + std::to_string(i)).frame, 10, 19, 36, 64);
  TEST_ASSERT_TRUE(nod > 50);  // he nodded
  for (int i = 2; i <= 4; ++i) {
    const Pushes p = named(r, "memo-voice-" + std::to_string(i)).pushes;
    TEST_ASSERT_TRUE(p.full <= 1 && p.partial >= 2 && p.partial <= 4);
    TEST_ASSERT_TRUE(p.partial_px <= 36 * 64 + 173 * 23 + 12 * 12);
  }
  const Frame& d1 = named(r, "meeting-dim-1").frame;
  for (int i = 2; i <= 3; ++i) {
    const std::string name = "meeting-dim-" + std::to_string(i);  // a named string: GCC 13 -Wdangling-reference
    const Shot& d = named(r, name);
    TEST_ASSERT_TRUE(d.backlight == cpa::Backlight::Dim);
    TEST_ASSERT_EQUAL_UINT32(0, d.pushes.partial);
    TEST_ASSERT_EQUAL(0, changed(d1, d.frame, 0, 12, 60, 111));    // Mando, his sign, the ●
    TEST_ASSERT_EQUAL(0, changed(d1, d.frame, 46, 73, 194, 34));   // the timeline
  }
  TEST_ASSERT_TRUE(changed(d1, named(r, "meeting-dim-3").frame, 60, 30, 120, 25) > 0);  // the timer
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_tour_draws_only_palette_entries);
  RUN_TEST(test_the_dot_is_lit_on_every_recording_shot);
  RUN_TEST(test_mandos_states_draw_only_palette_entries);
  RUN_TEST(test_s2_words_have_glyphs);
  RUN_TEST(test_the_ux_b_scripts_draw_only_palette_entries);
  RUN_TEST(test_the_motion_frames_draw_only_palette_entries);
  RUN_TEST(test_the_panel_holds_all_that_was_drawn);
  RUN_TEST(test_only_the_big_dot_breathes);
  RUN_TEST(test_on_it_moves_and_at_dim_it_holds_still);
  return UNITY_END();
}
