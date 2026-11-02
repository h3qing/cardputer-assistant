// The ● rule (docs/13 §7.2): while RecPhase is Cue, Recording, EndConfirm or Finishing the
// screen shows ● and the timer and the backlight is at least dimmed, whatever the lock,
// the menu or a toast. Locked, a meeting shows only "●mm:ss ★n" at the lowest backlight
// and a memo a minimal ● HUD. Checked on the shell for every {memo, meeting} x {locked,
// menu, toast}, and through the runtime (screen and backlight) for the reachable ones.
// v0.5 (docs/19 §9.2): on the palette canvas the ● is Rec (Warn while the microphone comes
// up), never the breathing RecDot, and reads at >= 4.5:1 on whatever it is drawn on; the
// pages' big ● (S2) is RecDot on Bg. S3: the big ● breathes only while the backlight is On,
// never below 4.5:1; dimmed it stands at full Rec.
#include <unity.h>

#include <string>

#include "core/support/lang_pin.h"
#include "rec_engine.h"  // named here so the library finder builds rec and shell
#include "runtime.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"
#include "theme_palette.h"

using namespace cpa;
using namespace uitest;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

namespace {

enum class Cover : uint8_t { Locked, Menu, Toast };

constexpr uint8_t kL = 38;  // layout index of 'l'
constexpr uint8_t kR = 18;  // 'r'

struct Rig {
  FakePort port;
  ProbeModule board{"board"};
  ProbeModule inbox{"inbox"};
  cpa::Module* mods[2] = {&board, &inbox};
  Shell shell{port, mods, 2};
  Rig() {
    port.app = demo_state();
    port.app.rec.avail = true;
    shell.begin(0);
  }
  void key(const KeyEvent& ev, uint32_t now) {
    if (shell.lock_gate(ev, now)) shell.on_key(ev, now);
  }
};

// The cover first, then the recording (a memo under the lock started from the lit
// card; one under the menu cannot start, the state is forced here).
void cover(Rig& r, Cover c, RecMode mode, RecPhase phase) {
  if (c == Cover::Locked) r.key(opt('l'), 10);
  if (c == Cover::Menu) r.key(nav(Nav::Menu), 10);
  r.port.app.rec.phase = phase;
  r.port.app.rec.mode = mode;
  r.port.app.rec.started_ms = 0;
  r.port.app.rec.stars = 2;
  if (c == Cover::Toast) r.shell.toast(ToastLevel::Warn, "提示一下");
}

KeySet keys(std::initializer_list<uint8_t> ks) {
  KeySet s;
  for (uint8_t k : ks) s = s.with(k);
  return s;
}

void press(RuntimeRig& r, KeySet ks) {
  r.step(10, ks);
  r.step(10);
}

// A fresh frame, with `held` still down (a memo stops when G0 is released).
void assert_visible(RuntimeRig& r, const char* what, KeySet held = KeySet{}) {
  r.step(1000, held);
  const Backlight lv = r.plat.levels.empty() ? Backlight::On : r.plat.levels.back();
  TEST_ASSERT_TRUE_MESSAGE(lv != Backlight::Off, what);
  TEST_ASSERT_TRUE_MESSAGE(r.disp.canvas_.has("●"), (std::string(what) + "\n" + r.disp.canvas_.dump()).c_str());
}

void start_meeting(RecRig& x) {
  press(x.r, keys({key::kOpt, kR}));
  x.r.step(500);
  press(x.r, keys({key::kEnter}));
  x.steps();
  x.port.feed(1000);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
}

}  // namespace

static void test_every_cover_shows_the_dot() {
  for (RecMode mode : {RecMode::Memo, RecMode::Meeting}) {
    for (Cover c : {Cover::Locked, Cover::Menu, Cover::Toast}) {
      for (RecPhase p : {RecPhase::Cue, RecPhase::Recording, RecPhase::EndConfirm, RecPhase::Finishing}) {
        Rig r;
        cover(r, c, mode, p);
        FakeCanvas canvas;
        r.shell.render(canvas, 65000);
        const std::string what = std::to_string(static_cast<int>(mode)) + "/" + std::to_string(static_cast<int>(c)) +
                                 "/" + std::to_string(static_cast<int>(p)) + "\n" + canvas.dump();
        TEST_ASSERT_TRUE_MESSAGE(canvas.has("●"), what.c_str());
        // Cue: the microphone is not up yet; locked, "● Wait" stands in for the clock, as
        // on the unlocked pages (the status bar's ●0:00 is not drawn under the lock)
        const char* clock = p != RecPhase::Cue ? "1:05" : (c == Cover::Locked ? "● 准备" : "0:00");
        TEST_ASSERT_TRUE_MESSAGE(canvas.has(clock), what.c_str());
      }
    }
  }
}

// What a text is drawn on: its own cell background, or for glyphs only (fg == bg, over a
// theme shape) the last fill under its middle.
static Color ground_of(const FakeCanvas& c, const TextOp& t) {
  if (t.style.bg != t.style.fg) return t.style.bg;
  Color g = Color::Bg;
  for (const auto& f : c.fills) {
    const Rect& r = f.first;
    if (r.x <= t.x + 3 && t.x + 3 < r.x + r.w && r.y <= t.y + 6 && t.y + 6 < r.y + r.h) g = f.second;
  }
  return g;
}

// A dim() covers the text (the page under the Tab sheet; no ● is drawn on the sheet): it
// shows dimmed, so its colours say nothing about how it reads.
static bool dimmed(const FakeCanvas& c, const TextOp& t) {
  for (const Rect& d : c.dims) {
    if (d.x <= t.x && t.x < d.x + d.w && d.y <= t.y && t.y < d.y + d.h) return true;
  }
  return false;
}

// Every ● that is not dimmed reads at >= 4.5:1, and at least one does: under the menu that
// is the status bar's, which the menu never dims.
static void test_the_dot_reads_on_its_ground() {
  for (RecMode mode : {RecMode::Memo, RecMode::Meeting}) {
    for (Cover c : {Cover::Locked, Cover::Menu, Cover::Toast}) {
      for (RecPhase p : {RecPhase::Cue, RecPhase::Recording, RecPhase::EndConfirm, RecPhase::Finishing}) {
        Rig r;
        cover(r, c, mode, p);
        FakeCanvas canvas;
        r.shell.render(canvas, 65000);
        int dots = 0;
        for (const TextOp& t : canvas.texts) {
          if (t.text.rfind("●", 0) != 0 || dimmed(canvas, t)) continue;  // not "Speak once you see ● REC"
          ++dots;
          const bool cue = t.text.find("准备") != std::string::npos;
          TEST_ASSERT_TRUE_MESSAGE(t.style.fg == (cue ? Color::Warn : Color::Rec), t.text.c_str());
          TEST_ASSERT_TRUE_MESSAGE(theme::contrast(t.style.fg, ground_of(canvas, t)) >= 4.5, t.text.c_str());
        }
        TEST_ASSERT_TRUE_MESSAGE(dots > 0, canvas.dump().c_str());
      }
    }
  }
}

// v0.5 (docs/19 §2.3, §6.6-§6.7): the pages' big ● is RecDot on Bg, where it reads at
// >= 4.5:1; it never replaces the status bar's ●mm:ss. S3: the meeting timeline's end dot is
// Rec, which does not breathe.
static void test_the_big_dot_reads_on_bg() {
  for (RecMode mode : {RecMode::Memo, RecMode::Meeting}) {
    for (RecPhase p : {RecPhase::Recording, RecPhase::EndConfirm, RecPhase::Finishing}) {
      if (mode == RecMode::Memo && p == RecPhase::EndConfirm) continue;
      Rig r;
      cover(r, Cover::Toast, mode, p);
      FakeCanvas canvas;
      r.shell.render(canvas, 65000);
      int big = 0;
      for (const DotOp& d : canvas.dots) {
        TEST_ASSERT_TRUE(d.under == Color::Bg);
        TEST_ASSERT_TRUE(d.paint == Color::RecDot || (d.paint == Color::Rec && d.diameter == 5));
        big += d.paint == Color::RecDot && d.diameter == 9 ? 1 : 0;
      }
      TEST_ASSERT_EQUAL(1, big);
      TEST_ASSERT_TRUE(canvas.find("● 1:05") != nullptr);
    }
  }
  TEST_ASSERT_TRUE(theme::contrast(Color::RecDot, Color::Bg) >= 4.5);
}

// docs/13 §7.2: locked, a meeting shows "●mm:ss ★n" and nothing else; v0.5 (docs/19 §6.8):
// in the status bar's slot, a RecBg capsule at the left.
static void test_locked_meeting_shows_only_the_dot_line() {
  Rig r;
  cover(r, Cover::Locked, RecMode::Meeting, RecPhase::Recording);
  FakeCanvas c;
  r.shell.render(c, 65000);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, c.texts.size(), c.dump().c_str());
  TEST_ASSERT_EQUAL_STRING("●1:05 ★2", c.texts[0].text.c_str());
  TEST_ASSERT_TRUE(c.texts[0].style.fg == Color::Error);
  TEST_ASSERT_TRUE(c.texts[0].x == 4 && c.texts[0].y == 0);
  TEST_ASSERT_TRUE(ground_of(c, c.texts[0]) == Color::RecBg);
}

static void test_runtime_memo_keeps_the_dot() {
  RecRig locked;
  press(locked.r, keys({key::kOpt, kL}));
  press(locked.r, keys({key::kG0}));  // a tap lights the card
  locked.hold_g0(600);                // a memo from the lit card
  assert_visible(locked.r, "memo, locked", keys({key::kG0}));
  RecRig menu;
  press(menu.r, keys({key::kTab}));
  TEST_ASSERT_TRUE(menu.r.rt.shell().menu_open());
  menu.hold_g0(600);  // G0 closes the menu and records
  TEST_ASSERT_FALSE(menu.r.rt.shell().menu_open());
  assert_visible(menu.r, "memo, menu", keys({key::kG0}));
  RecRig toast;
  toast.hold_g0(600);
  toast.r.rt.shell().toast(ToastLevel::Warn, "提示一下");
  assert_visible(toast.r, "memo, toast", keys({key::kG0}));
  TEST_ASSERT_TRUE(toast.r.disp.canvas_.has("提示一下"));
}

static void test_runtime_meeting_keeps_the_dot() {
  RecRig locked;
  start_meeting(locked);
  press(locked.r, keys({key::kOpt, kL}));
  TEST_ASSERT_TRUE(locked.r.rt.shell().locked());
  assert_visible(locked.r, "meeting, locked");
  RecRig menu;
  start_meeting(menu);
  press(menu.r, keys({key::kTab}));
  TEST_ASSERT_TRUE(menu.r.rt.shell().menu_open());
  assert_visible(menu.r, "meeting, menu");
  RecRig toast;
  start_meeting(toast);
  toast.r.rt.shell().toast(ToastLevel::Warn, "提示一下");
  assert_visible(toast.r, "meeting, toast");
}

// S3 (docs/19 §1, §7.4): On, the big ● breathes; dimmed it goes back to full Rec at once
// (breathe(0)) and stays there. The status bar's ● is Rec all along, never RecDot.
static void test_the_dot_stands_full_when_dimmed() {
  RecRig x;
  start_meeting(x);
  for (int i = 0; i < 100; ++i) x.r.step(10);
  TEST_ASSERT_TRUE(x.r.disp.breaths.size() >= 4);  // On: a step every tick
  x.r.tx.is_plugged = false;
  for (int i = 0; i < 6200; ++i) x.r.step(10);  // 62 s on battery without a key
  TEST_ASSERT_TRUE(x.r.plat.levels.back() == Backlight::Dim);
  TEST_ASSERT_EQUAL_UINT8(0, x.r.disp.breaths.back());
  const size_t n = x.r.disp.breaths.size();
  for (int i = 0; i < 500; ++i) x.r.step(10);
  TEST_ASSERT_EQUAL_UINT(n, x.r.disp.breaths.size());  // frozen
  bool seen = false;
  for (const TextOp& t : x.r.disp.canvas_.texts) {
    if (t.text.find("●") == std::string::npos) continue;
    seen = true;
    TEST_ASSERT_TRUE(t.style.fg == Color::Rec);
  }
  TEST_ASSERT_TRUE(seen);
  for (uint8_t step = 0; step < theme::kBreathSteps; ++step) {
    TEST_ASSERT_TRUE(theme::contrast_of(theme::breath_dot(step), theme::index_of(Color::Bg).value()) >= 4.5);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_cover_shows_the_dot);
  RUN_TEST(test_the_dot_reads_on_its_ground);
  RUN_TEST(test_the_big_dot_reads_on_bg);
  RUN_TEST(test_locked_meeting_shows_only_the_dot_line);
  RUN_TEST(test_runtime_memo_keeps_the_dot);
  RUN_TEST(test_runtime_meeting_keeps_the_dot);
  RUN_TEST(test_the_dot_stands_full_when_dimmed);
  return UNITY_END();
}
