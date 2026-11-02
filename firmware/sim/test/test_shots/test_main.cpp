// play(): sim scripts on the real runtime and modules, checked on the screenshots' pixels,
// which are palette entries (lib/theme): entry_of(Color) for tokens, 0x20-0x2E for Mando's
// colours (the status bar's head while online).
#include <unity.h>

#include <string>
#include <vector>

#include "fb.h"
#include "fb_canvas.h"
#include "lang.h"
#include "player.h"
#include "runtime.h"
#include "script.h"

using namespace cpasim;

void setUp() { cpa::set_lang(cpa::Lang::Zh); }
void tearDown() { cpa::set_lang(cpa::Lang::Zh); }

namespace {

#define HELLO \
  "host hello {\"role\":\"host\",\"app\":\"cpad/0.3.1\",\"agent\":\"ok\",\"caps\":{\"spawn\":true,\"reply\":true}}\n"

const char* kState =
    HELLO
    "host projects.snapshot {\"rev\":3,\"projects\":[{\"slot\":1,\"key\":\"acmeshop\",\"name\":\"Acmeshop\","
    "\"run\":1,\"need\":0,\"queued\":0,\"ok\":true}],\"other\":{\"run\":0,\"n\":0}}\n"
    "wait 1200\n";

// These scripts were written for the Chinese UI: every boot is in Chinese, as with
// `cpa-sim shots --lang zh` (English is the default since v0.5.2).
PlayResult run(const std::string& text) {
  const Script s = parse_script("main", [&](const std::string&, std::string& out) {
    out = text;
    return true;
  });
  TEST_ASSERT_EQUAL_STRING("", s.error.c_str());
  std::vector<Step> steps = s.steps;
  for (Step& st : steps) st.boot.lang = cpa::Lang::Zh;
  return play(steps);
}

int count(const Frame& f, int x0, int y0, int w, int h, uint8_t px) {
  int n = 0;
  for (int y = y0; y < y0 + h; ++y) {
    for (int x = x0; x < x0 + w; ++x) n += f.at(x, y) == px ? 1 : 0;
  }
  return n;
}

int count(const Frame& f, int x0, int y0, int w, int h, cpa::Color c) { return count(f, x0, y0, w, h, entry_of(c)); }

// Pixels of Mando's colours in a rectangle.
int mando(const Frame& f, int x0, int y0, int w, int h) {
  int n = 0;
  for (uint8_t e = 0x20; e <= 0x2E; ++e) n += count(f, x0, y0, w, h, e);
  return n;
}

// The head the status bar shows while online.
int head(const Frame& f) { return mando(f, 0, 0, 30, 12); }

// The text, drawn alone at the top left of a line in fg on Bg, appears somewhere on f.
bool shows(const Frame& f, const char* text, cpa::Color fg, cpa::Color bg = cpa::Color::Bg) {
  Frame probe(1, 1);
  const int w = FbCanvas(probe).textWidth(text);
  Frame t(w, 12);
  FbCanvas(t).text(0, 0, text, cpa::Style{fg, bg});
  for (int y = 0; y + 12 <= f.h(); ++y) {
    for (int x = 0; x + w <= f.w(); ++x) {
      bool same = true;
      for (int i = 0; same && i < w * 12; ++i) same = f.at(x + i % w, y + i / w) == t.at(i % w, i / w);
      if (same) return true;
    }
  }
  return false;
}

}  // namespace

// v0.4.0: home is the root page; Opt+P opens the board.
static void test_board_online() {
  const PlayResult r = run(std::string("boot\n") + kState + "press opt+p\nshot board The board\n");
  TEST_ASSERT_EQUAL_STRING("", r.error.c_str());
  TEST_ASSERT_EQUAL_size_t(1, r.shots.size());
  const Shot& s = r.shots[0];
  TEST_ASSERT_EQUAL_STRING("board", s.name.c_str());
  TEST_ASSERT_EQUAL_STRING("The board", s.caption.c_str());
  TEST_ASSERT_EQUAL_STRING("board", s.top.c_str());
  TEST_ASSERT_EQUAL(240, s.frame.w());
  TEST_ASSERT_EQUAL(135, s.frame.h());
  TEST_ASSERT_TRUE(head(s.frame) > 40);  // online: Mando's head
  TEST_ASSERT_TRUE(count(s.frame, 4, 29, 232, 28, cpa::Color::Select) > 2000);  // the selected project card
  TEST_ASSERT_TRUE(count(s.frame, 4, 29, 232, 28, cpa::Color::Live) > 400);     // in its outline
  TEST_ASSERT_EQUAL_UINT32(0, s.missing_glyphs);
}

// Without a hello the USB host is "no response"; with one, the pings keep the link up.
static void test_pings_keep_the_link() {
  const PlayResult quiet = run("boot\nwait 1200\nshot a\n");
  TEST_ASSERT_TRUE(count(quiet.shots[0].frame, 0, 0, 60, 12, cpa::Color::Warn) > 20);  // "◇ no reply" in Warn
  TEST_ASSERT_EQUAL(0, head(quiet.shots[0].frame));
  const PlayResult r = run(std::string("boot\n") + kState + "wait 30000\nshot b\n");
  TEST_ASSERT_TRUE(head(r.shots[0].frame) > 40);
  TEST_ASSERT_TRUE(r.shots[0].at_ms >= 31200);
}

static void test_memo_hud() {
  const PlayResult r = run(std::string("boot\n") + kState + "down g0\nwait 600\nmic 2000 454\nshot memo\nup g0\n");
  TEST_ASSERT_EQUAL_STRING("", r.error.c_str());
  const Frame& f = r.shots[0].frame;
  TEST_ASSERT_TRUE(count(f, 0, 0, 40, 12, cpa::Color::Rec) > 20);       // ●0:02 in the status bar
  int dot = 0;  // docs/19 §6.6: the big ●, at whatever breath step was pushed last (S3; 3 is 1)
  for (uint8_t step = 0; step < 3; ++step) dot += count(f, 60, 38, 12, 12, cpa::theme::breath_dot(step));
  TEST_ASSERT_EQUAL(60, dot);
  TEST_ASSERT_TRUE(count(f, 75, 33, 61, 21, cpa::Color::Ink) > 150);    // the timer in Nunito
  TEST_ASSERT_TRUE(mando(f, 10, 19, 36, 64) > 900);                     // Mando, 64 px, listening
  // S3: the waveform, newest on the right: a steady RMS 454, the median word of the user's
  // memos (-37 dBFS), draws 15 px bars with chipped ends; blocks not heard yet are 3 px dots on
  // the left.
  TEST_ASSERT_EQUAL(15 * 3 - 4, count(f, 232, 63, 3, 25, cpa::Color::Accent2));
  TEST_ASSERT_EQUAL(4, count(f, 232, 63, 3, 25, cpa::theme::aa_of(cpa::Color::Accent2, cpa::Color::Bg)));
  TEST_ASSERT_EQUAL(3 * 3, count(f, 62, 63, 3, 25, cpa::Color::Accent));
}

static void test_no_card_refuses_recording() {
  const PlayResult r = run(std::string("boot store=internal\n") + kState + "hold g0 600\nshot refused\n");
  TEST_ASSERT_TRUE(count(r.shots[0].frame, 0, 123, 240, 12, cpa::Color::RecBg) > 1500);  // the red toast capsule
  TEST_ASSERT_TRUE(count(r.shots[0].frame, 0, 123, 240, 12, cpa::Color::Rec) > 100);    // and its words
}

static void test_reply_needs_a_device_message() {
  const PlayResult r = run(std::string("boot\n") + kState + "reply outbox.ack outbox.item {\"status\":\"stored\"}\n");
  TEST_ASSERT_EQUAL_STRING("main:5: the device has sent no outbox.item to answer", r.error.c_str());
}

static void test_capture_and_ack() {
  const PlayResult r = run(std::string("boot\n") + kState +
                           "press g0\ntype hi\npress enter\nshot syncing\n"
                           "reply outbox.ack outbox.item {\"status\":\"stored\"}\nwait 300\nshot synced\n");
  TEST_ASSERT_EQUAL_STRING("", r.error.c_str());
  TEST_ASSERT_TRUE(count(r.shots[0].frame, 14, 0, 12, 12, cpa::Color::Accent) > 8);  // the head and ◎: syncing
  TEST_ASSERT_TRUE(head(r.shots[0].frame) > 40);
  TEST_ASSERT_EQUAL(0, count(r.shots[1].frame, 14, 0, 12, 12, cpa::Color::Accent));  // the head alone again
}

// Storage as the device's setup() plans it (storage_plan.h): an empty 64 GB card with
// cpafs mounted (so the boot migration runs, with nothing to move), or cpafs alone, which
// is "no card".
static void test_sd_store_shows_the_card() {
  const PlayResult r = run(std::string("boot store=sd\n") + kState + "press opt+m\npress /\nshot system\n");
  TEST_ASSERT_EQUAL_STRING("system", r.shots[0].top.c_str());
  // docs/19 §6.9: label / value rows, the label in Ink3, the value single-spaced in Ink
  TEST_ASSERT_TRUE(shows(r.shots[0].frame, "存储", cpa::Color::Ink3));
  TEST_ASSERT_TRUE(shows(r.shots[0].frame, "SD 59G", cpa::Color::Fg));
  TEST_ASSERT_TRUE(shows(r.shots[0].frame, "SD 59G 剩 59G", cpa::Color::Fg));  // docs/21 §4.5: and its free space
}

static void test_internal_store_warns_no_card() {
  const PlayResult r = run(std::string("boot store=internal\n") + kState + "press opt+m\npress /\nshot system\n");
  TEST_ASSERT_TRUE(shows(r.shots[0].frame, "内部 1.5M", cpa::Color::Fg));
  TEST_ASSERT_TRUE(shows(r.shots[0].frame, "SD：未检测到卡", cpa::Color::Warn));
}

static void test_no_store_warns_no_storage() {
  const PlayResult r = run(std::string("boot store=none\n") + kState + "press opt+m\npress /\nshot system\n");
  TEST_ASSERT_TRUE(shows(r.shots[0].frame, "无", cpa::Color::Warn));
  TEST_ASSERT_TRUE(shows(r.shots[0].frame, "没有可用存储：请插入 FAT32 SD 卡", cpa::Color::Warn));
}

// debug.reboot: 200 ms after its ack the device starts again, as boot 2 after a software
// reset; the daemon has to say hello again, so the link is down until it does.
static void test_debug_reboot_restarts_the_device() {
  const PlayResult r = run(std::string("boot\n") + kState + "host debug.reboot\nwait 2000\npress opt+m\npress /\nshot after\n");
  TEST_ASSERT_EQUAL_STRING("", r.error.c_str());
  const Frame& f = r.shots[0].frame;
  const std::string fw = std::string(cpa::kFwVersion) + " rst software";  // follows the release bump
  TEST_ASSERT_TRUE(shows(f, fw.c_str(), cpa::Color::Fg));
  TEST_ASSERT_EQUAL(0, head(f));  // not online
}

// docs/21 §9.4: debug.lang takes the settings page's path (the toast in both scripts) and
// the language is kept in the simulated NVS, so the device shows it again after debug.reboot.
static void test_the_language_survives_debug_reboot() {
  const PlayResult r = run(std::string("boot\n") + kState +
                           "host debug.lang {\"lang\":\"en\"}\nwait 300\nshot switched\n"
                           "host debug.reboot\nwait 2000\npress opt+m\npress /\nshot after\n");
  TEST_ASSERT_EQUAL_STRING("", r.error.c_str());
  TEST_ASSERT_TRUE(shows(r.shots[0].frame, "English · Enter again: 中文", cpa::Color::Accent, cpa::Color::InfoBg));
  TEST_ASSERT_EQUAL_UINT32(0, r.shots[0].missing_glyphs);
  const std::string fw = std::string(cpa::kFwVersion) + " rst software";
  TEST_ASSERT_TRUE(shows(r.shots[1].frame, fw.c_str(), cpa::Color::Fg));  // boot 2
  TEST_ASSERT_TRUE(cpa::lang() == cpa::Lang::En);  // which read it back
}

// docs/21 §4.3, §9.4: Opt+M . Enter switches to English (three presses), the settings page
// and the WiFi page have no missing glyph in either language, and Opt+M . Enter switches back.
static void test_settings_switch_in_three_presses() {
  const PlayResult r = run(std::string("boot store=sd\n") + kState +
                           "press opt+m\nshot zh\npress .\npress enter\nwait 300\nshot en\n"
                           "press `\npress opt+m\npress enter\nshot wifi\npress `\npress /\nshot device\n"
                           "press opt+m\npress .\npress enter\nwait 300\nshot back\n");
  TEST_ASSERT_EQUAL_STRING("", r.error.c_str());
  TEST_ASSERT_EQUAL_size_t(5, r.shots.size());
  for (const auto& s : r.shots) {
    TEST_ASSERT_EQUAL_STRING("system", s.top.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, s.missing_glyphs);
  }
  TEST_ASSERT_TRUE(shows(r.shots[0].frame, "无 · 只用 USB 线", cpa::Color::Fg, cpa::Color::Select));
  TEST_ASSERT_TRUE(shows(r.shots[1].frame, "None · USB only", cpa::Color::Fg));
  TEST_ASSERT_TRUE(shows(r.shots[2].frame, "No WiFi in this firmware", cpa::Color::Fg));
  TEST_ASSERT_TRUE(shows(r.shots[3].frame, "Store", cpa::Color::Ink3));
  TEST_ASSERT_TRUE(shows(r.shots[4].frame, "中文", cpa::Color::Fg, cpa::Color::Select));
  TEST_ASSERT_TRUE(cpa::lang() == cpa::Lang::Zh);
}

// A shot waits at most 500 ms; one taken while host lines are still unread says so.
static void test_a_shot_that_cannot_settle_says_so() {
  std::string busy = "boot\n";
  for (int i = 0; i < 400; ++i) busy += "host ping\n";
  const PlayResult r = run(busy + "shot busy\nwait 2000\nshot calm\n");
  TEST_ASSERT_EQUAL_size_t(2, r.shots.size());
  TEST_ASSERT_FALSE(r.shots[0].settled);
  TEST_ASSERT_TRUE(r.shots[1].settled);
}

// missing_glyphs counts the □ in the shot's frame, not every □ drawn since boot.
static void test_missing_glyphs_are_per_frame() {
  const PlayResult r = run(
      "boot\n" HELLO
      "host projects.snapshot {\"rev\":1,\"projects\":[{\"slot\":1,\"key\":\"k\",\"name\":\"ok \xE2\x9C\x93\","
      "\"run\":0,\"need\":0,\"queued\":0,\"ok\":true}],\"other\":{\"run\":0,\"n\":0}}\n"
      "wait 1200\npress opt+p\nshot first\nwait 3000\nshot later\n");  // the name shows on the board
  TEST_ASSERT_EQUAL_STRING("", r.error.c_str());
  TEST_ASSERT_TRUE(r.shots[0].missing_glyphs > 0);
  TEST_ASSERT_EQUAL_UINT32(r.shots[0].missing_glyphs, r.shots[1].missing_glyphs);
}

// v0.5 S3: frames: n shots ms apart, named and captioned by number, the first once settled.
static void test_frames_shoot_every_few_ms() {
  const PlayResult r = run(std::string("boot\n") + kState + "frames home 3 200 主页\n");
  TEST_ASSERT_EQUAL_STRING("", r.error.c_str());
  TEST_ASSERT_EQUAL_size_t(3, r.shots.size());
  TEST_ASSERT_EQUAL_STRING("home-2", r.shots[1].name.c_str());
  TEST_ASSERT_EQUAL_STRING("主页 2/3", r.shots[1].caption.c_str());
  TEST_ASSERT_EQUAL_UINT32(r.shots[0].at_ms + 200, r.shots[1].at_ms);
  TEST_ASSERT_EQUAL_UINT32(r.shots[1].at_ms + 200, r.shots[2].at_ms);
  TEST_ASSERT_TRUE(r.shots[0].settled);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_board_online);
  RUN_TEST(test_pings_keep_the_link);
  RUN_TEST(test_memo_hud);
  RUN_TEST(test_no_card_refuses_recording);
  RUN_TEST(test_reply_needs_a_device_message);
  RUN_TEST(test_capture_and_ack);
  RUN_TEST(test_sd_store_shows_the_card);
  RUN_TEST(test_internal_store_warns_no_card);
  RUN_TEST(test_no_store_warns_no_storage);
  RUN_TEST(test_debug_reboot_restarts_the_device);
  RUN_TEST(test_the_language_survives_debug_reboot);
  RUN_TEST(test_settings_switch_in_three_presses);
  RUN_TEST(test_a_shot_that_cannot_settle_says_so);
  RUN_TEST(test_missing_glyphs_are_per_frame);
  RUN_TEST(test_frames_shoot_every_few_ms);
  return UNITY_END();
}
