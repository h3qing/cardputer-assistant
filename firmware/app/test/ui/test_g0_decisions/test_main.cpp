// Where gh #51's G0 (the 0.6 s grace, the double tap, hands free; docs/20 U26) meets docs/20
// D1-D10 (v0.4.1), through the runtime and the audio task on the host:
//   overlays  D4: G0 held under the editor, a confirm box or the Tab menu records a memo of
//             its own. Its grace works there as anywhere: a re-press is the memo's (its tap
//             never submits the note), and the draft or the box is still there after it. The
//             double tap starts where a G0 tap means the quick note: through the open menu
//             (its first tap closes the menu, D9), never on the meeting's start box (D3).
//   lock      D2 locks the dark screen on battery; the double tap from dark lights the card
//             first. A card lit by itself (D7) is no look: no first tap there either. Locked,
//             a press from dark on a hands-free memo only lights the card, so the card never
//             says 没有在录 while a memo records (U3, B1). A hands-free memo never goes dark,
//             so it never locks by itself; once it ended, the screen does (D2).
//   menu      over a hands-free memo: Enter on the G0 row says how to end; Opt+L (D9) closes
//             the menu and locks, the memo goes on; a G0 press ends it and closes the menu.
//   card      the lock card's G0 line fits 240 px in every state.
#include <unity.h>

#include <string>

#include "g0_gesture.h"
#include "lock_screen.h"
#include "rec_core.h"
#include "rec_engine.h"  // named here so the library finder builds rec and shell
#include "runtime.h"
#include "support/fake_canvas.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"

using namespace cpa;
using namespace uitest;
using rec::CmdKind;

void setUp() {}
void tearDown() {}

namespace {

const KeySet kG0 = KeySet{}.with(key::kG0);
const KeySet kTab = KeySet{}.with(key::kTab);
const KeySet kEnter = KeySet{}.with(key::kEnter);
const KeySet kA = KeySet{}.with(layout_index(2, 2));
const KeySet kOptL = KeySet{}.with(key::kOpt).with(layout_index(10, 2));
const KeySet kOptR = KeySet{}.with(key::kOpt).with(layout_index(4, 1));

void run(RecRig& x, uint32_t ms, KeySet keys = KeySet{}) {
  for (uint32_t t = 0; t < ms; t += 10) x.r.step(10, keys);
}

// A press of 60 ms; 40 ms up after it.
void tap(RecRig& x, KeySet keys) {
  run(x, 60, keys);
  run(x, 40);
}

// G0 twice: a hands-free memo, its microphone up.
void hands_free(RecRig& x) {
  tap(x, kG0);
  tap(x, kG0);
  x.port.feed(1000);
  run(x, 200);
}

const RecInfo& rec_of(RecRig& x) { return x.r.rt.state().rec; }
bool composing(RecRig& x) { return x.r.rt.shell().overlay() == proto::Overlay::Compose; }
const FakeCanvas& screen(RecRig& x) { return x.r.disp.canvas_; }

// A memo held 0.6 s, let go 0.2 s (its grace), pressed again for a tap, then let go for good:
// one recording, saved.
void memo_with_a_slip(RecRig& x) {
  x.hold_g0(600);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Recording);
  x.port.feed(1500);
  run(x, 200);
  TEST_ASSERT_TRUE(rec_of(x).grace);
  tap(x, kG0);  // the re-press: a tap, which the overlay never sees
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Recording);
  x.port.feed(300);
  run(x, rec::kMemoGraceMs + 40);
  x.steps();
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Saved);
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Start));
}

}  // namespace

// D4 + grace: under the editor with text in it; the re-press's tap would submit the note.
static void test_memo_under_the_editor_keeps_its_grace() {
  RecRig x;
  tap(x, kG0);
  run(x, kDoubleTapMs);
  TEST_ASSERT_TRUE(composing(x));
  tap(x, kA);
  memo_with_a_slip(x);
  TEST_ASSERT_TRUE(composing(x));
  run(x, 60);
  TEST_ASSERT_TRUE_MESSAGE(screen(x).has("a_"), screen(x).dump().c_str());
  TEST_ASSERT_EQUAL_UINT16(0, x.r.rt.state().outbox.pending);  // the draft was not saved
}

// D4 + grace: under a confirm box, which stays as it was.
static void test_memo_under_a_confirm_box_keeps_its_grace() {
  RecRig x;
  ConfirmSpec spec;
  spec.kind = ConfirmKind::Spawn;
  spec.slot = 1;
  spec.text = UserText::from("修一下登录");
  x.r.rt.shell().confirm(spec);
  run(x, 10);
  memo_with_a_slip(x);
  TEST_ASSERT_TRUE(x.r.rt.shell().overlay() == proto::Overlay::Confirm);
}

// D4 + grace: the hold closes the Tab menu and records; after it no note opens.
static void test_memo_under_the_menu_keeps_its_grace() {
  RecRig x;
  tap(x, kTab);
  TEST_ASSERT_TRUE(x.r.rt.shell().menu_open());
  memo_with_a_slip(x);
  TEST_ASSERT_FALSE(x.r.rt.shell().menu_open());
  run(x, kLateTapMs);
  TEST_ASSERT_FALSE(composing(x));
}

// The double tap through the open menu: its first tap closes the menu (it replays G0, D9's
// "G0 does what it does"), the second starts hands free; no note.
static void test_a_double_tap_through_the_open_menu() {
  RecRig x;
  tap(x, kTab);
  hands_free(x);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Recording && rec_of(x).latched);
  TEST_ASSERT_FALSE(x.r.rt.shell().menu_open());
  run(x, kLateTapMs);
  TEST_ASSERT_FALSE(composing(x));
}

// D3: the meeting's start box comes first. G0 there says so; tapped twice it records nothing.
static void test_the_start_box_takes_no_double_tap() {
  RecRig x;
  tap(x, kOptR);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Confirm);
  tap(x, kG0);
  tap(x, kG0);
  run(x, kLateTapMs);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Confirm);
  TEST_ASSERT_EQUAL_UINT(0, x.port.count(CmdKind::Start));
  TEST_ASSERT_FALSE(composing(x));
  TEST_ASSERT_TRUE_MESSAGE(screen(x).has("先处理确认框"), screen(x).dump().c_str());
}

// D2 + the double tap: dark on battery is locked; a tap from dark lights the card and is no
// first tap; then tap, tap records hands free under the lock, the card dark again.
static void test_auto_locked_the_double_tap_lights_the_card_first() {
  RecRig x;
  x.r.unplug();
  run(x, 61000);
  TEST_ASSERT_TRUE(x.r.rt.shell().locked());
  tap(x, kG0);
  tap(x, kG0);
  TEST_ASSERT_EQUAL_UINT(0, x.port.count(CmdKind::Start));
  tap(x, kG0);
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Start));
  x.port.feed(1000);
  run(x, 200);
  TEST_ASSERT_TRUE(rec_of(x).latched && x.r.rt.shell().locked());
  TEST_ASSERT_FALSE(x.r.rt.shell().lock_lit(x.r.now));
  TEST_ASSERT_TRUE_MESSAGE(screen(x).has("● 免提 0:0"), screen(x).dump().c_str());
}

// D7 + the double tap: a card lit by itself (the needs-you count rose) may light an empty
// pocket, so a tap on it is no first tap, as from dark; the card a key lit takes one.
static void test_a_card_lit_by_itself_takes_no_first_tap() {
  RecRig x;
  tap(x, kOptL);
  run(x, kLockFlashMs + 100);
  x.r.rt.shell().lock_flash(x.r.now, kNeedsFlashMs);
  TEST_ASSERT_TRUE(x.r.rt.shell().lock_lit(x.r.now) && !x.r.rt.shell().lock_seen(x.r.now));
  tap(x, kG0);
  tap(x, kG0);
  TEST_ASSERT_EQUAL_UINT(0, x.port.count(CmdKind::Start));
  tap(x, kG0);
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Start));
  TEST_ASSERT_TRUE(rec_of(x).latched);
}

// U3 / B1 + hands free: locked, a hold from dark only lights the card. It is no swallowed
// memo: never 没有在录 (a memo records), and the card says how to end it. A press on the lit
// card ends it and the card shows the result (D7).
static void test_a_hold_from_dark_on_a_locked_hands_free_memo() {
  RecRig x;
  hands_free(x);
  tap(x, kOptL);
  run(x, kLockFlashMs + 100);
  TEST_ASSERT_FALSE(x.r.rt.shell().lock_lit(x.r.now));
  x.port.feed(3000);
  x.hold_g0(1500);
  TEST_ASSERT_TRUE(x.r.rt.shell().lock_lit(x.r.now));
  TEST_ASSERT_FALSE(x.r.rt.state().ui.lock_held);
  TEST_ASSERT_FALSE_MESSAGE(screen(x).has("没有在录") || screen(x).has("没在录"), screen(x).dump().c_str());
  TEST_ASSERT_TRUE_MESSAGE(screen(x).has("免提录音中：按"), screen(x).dump().c_str());
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Recording && rec_of(x).latched);
  x.release();
  run(x, 200);
  tap(x, kG0);  // on the lit card: the end
  x.steps();
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Saved);
  run(x, 100);
  TEST_ASSERT_TRUE(x.r.rt.shell().locked() && x.r.rt.shell().lock_lit(x.r.now));
  TEST_ASSERT_TRUE_MESSAGE(screen(x).has("\xE2\x88\x9A 已存"), screen(x).dump().c_str());
}

// D2 + hands free on battery: a memo never goes dark (dimmed, ● stays), so it never locks by
// itself; its end lights the screen (review 8), which then goes dark and locks.
static void test_hands_free_on_battery_never_locks_by_itself() {
  RecRig x;
  x.r.unplug();
  hands_free(x);
  for (int i = 0; i < 9; ++i) {
    x.port.feed(10000);
    run(x, 10000);
  }
  TEST_ASSERT_FALSE(x.r.rt.shell().locked());
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Recording && rec_of(x).latched);
  TEST_ASSERT_TRUE(x.r.plat.levels.back() == Backlight::Dim);
  tap(x, kG0);
  x.steps();
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Saved);
  TEST_ASSERT_TRUE(x.r.plat.levels.back() == Backlight::On);
  run(x, 61000);
  TEST_ASSERT_TRUE(x.r.rt.shell().locked());
}

// The Tab menu over a hands-free memo: Enter on its G0 row says how to end and ends nothing;
// Opt+L, a chord the menu shows (D9), closes it and locks, the memo going on.
static void test_the_menu_over_a_hands_free_memo_locks_it() {
  RecRig x;
  hands_free(x);
  tap(x, kTab);
  TEST_ASSERT_TRUE(x.r.rt.shell().menu_open());
  TEST_ASSERT_TRUE_MESSAGE(screen(x).has("免提录音"), screen(x).dump().c_str());
  tap(x, kEnter);
  TEST_ASSERT_TRUE_MESSAGE(screen(x).has("按一下 G0 结束并保存"), screen(x).dump().c_str());
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Recording && rec_of(x).latched);
  tap(x, kTab);
  tap(x, kOptL);
  TEST_ASSERT_TRUE(x.r.rt.shell().locked() && !x.r.rt.shell().menu_open());
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Recording && rec_of(x).latched);
}

// A G0 press over the open menu ends the hands-free memo and closes the menu; its tap opens
// no note.
static void test_g0_over_the_menu_ends_a_hands_free_memo() {
  RecRig x;
  hands_free(x);
  tap(x, kTab);
  tap(x, kG0);
  x.steps();
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Saved);
  TEST_ASSERT_FALSE(x.r.rt.shell().menu_open());
  run(x, kLateTapMs);
  TEST_ASSERT_FALSE(composing(x));
}

// The lock card's second line, below the unlock line, fits the screen in every state.
static void test_the_lock_card_g0_line_fits() {
  struct Case {
    RecPhase phase;
    RecMode mode;
    bool latched;
    const char* last;  // the line's last words
  } kCases[] = {{RecPhase::Idle, RecMode::Memo, false, "再按住说 · 点两下免提"},
                {RecPhase::Recording, RecMode::Memo, true, "结束"},
                {RecPhase::Recording, RecMode::Meeting, false, "加 ★"},
                {RecPhase::Finishing, RecMode::Memo, false, "保存中…"}};
  for (const Case& k : kCases) {
    AppState s;
    s.rec.phase = k.phase;
    s.rec.mode = k.mode;
    s.rec.latched = k.latched;
    FakeCanvas c;
    render_lock_screen(c, s, true, 12000);
    const TextOp* t = c.find(k.last);
    TEST_ASSERT_NOT_NULL_MESSAGE(t, c.dump().c_str());
    TEST_ASSERT_TRUE_MESSAGE(t->y > 100 && t->x + c.textWidth(k.last) <= kScreenW, k.last);
    const TextOp* first = c.find(c.line(t->y).substr(0, 3));
    TEST_ASSERT_TRUE(first != nullptr && first->x >= 0);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_memo_under_the_editor_keeps_its_grace);
  RUN_TEST(test_memo_under_a_confirm_box_keeps_its_grace);
  RUN_TEST(test_memo_under_the_menu_keeps_its_grace);
  RUN_TEST(test_a_double_tap_through_the_open_menu);
  RUN_TEST(test_the_start_box_takes_no_double_tap);
  RUN_TEST(test_auto_locked_the_double_tap_lights_the_card_first);
  RUN_TEST(test_a_card_lit_by_itself_takes_no_first_tap);
  RUN_TEST(test_a_hold_from_dark_on_a_locked_hands_free_memo);
  RUN_TEST(test_hands_free_on_battery_never_locks_by_itself);
  RUN_TEST(test_the_menu_over_a_hands_free_memo_locks_it);
  RUN_TEST(test_g0_over_the_menu_ends_a_hands_free_memo);
  RUN_TEST(test_the_lock_card_g0_line_fits);
  return UNITY_END();
}
