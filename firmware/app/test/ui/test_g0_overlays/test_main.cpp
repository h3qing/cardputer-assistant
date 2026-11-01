// docs/20 D4 (U4): holding G0 records a memo under the editor, its discard prompt, a confirm
// box and the Tab menu, as it does on a page and from the lock card (docs/13 principle 5:
// G0 works everywhere). The draft or the box stays as it was; while something already
// records (a meeting) G0 keeps its own meaning, and the meeting's boxes say they come first.
#include <unity.h>

#include <string>

#include "rec_engine.h"  // named here so the library finder builds rec and shell
#include "runtime.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

namespace {

struct Rig {
  FakePort port;
  ProbeModule board{"board"};
  ProbeModule inbox{"inbox"};
  cpa::Module* mods[2] = {&board, &inbox};
  Shell shell{port, mods, 2};
  Rig() {
    port.app = demo_state();
    set_ready(port.app);
    port.next_refusal = rec::RecRefusal::None;
    board.slot = 1;
    shell.begin(0);
  }
  void key(const KeyEvent& ev, uint32_t at = 1000) {
    if (shell.lock_gate(ev, at)) shell.on_key(ev, at);
  }
};

void editor(Rig& r) {
  r.key(g0tap(), 10);
  r.key(chr('h'), 20);
  r.key(chr('i'), 30);
}

void discard_prompt(Rig& r) {
  editor(r);
  r.key(chr('`'), 40);  // D5: ` asks before dropping text
}

void spawn_confirm(Rig& r) {  // aimed at project 2, away from the page's project 1
  r.key(opt('s'), 10);
  r.key(opt('2'), 15);
  r.key(chr('x'), 20);
  r.key(enter(), 30);
}

void menu_over_editor(Rig& r) {
  editor(r);
  r.key(nav(Nav::Menu), 40);
}

}  // namespace

static void test_hold_records_under_every_overlay() {
  struct Case {
    const char* what;
    void (*prep)(Rig&);
    proto::Overlay after;
    uint8_t slot;  // the memo goes to the project of what is open
  } cases[] = {
      {"editor", editor, proto::Overlay::Compose, 1},
      {"discard prompt", discard_prompt, proto::Overlay::Compose, 1},
      {"spawn confirm", spawn_confirm, proto::Overlay::Confirm, 2},
      {"menu over the editor", menu_over_editor, proto::Overlay::Compose, 1},
  };
  for (const Case& c : cases) {
    Rig r;
    c.prep(r);
    r.key(g0long(), 1000);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, r.port.gestures.size(), c.what);
    TEST_ASSERT_TRUE_MESSAGE(r.port.gestures[0].first == RecMode::Memo, c.what);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(c.slot, r.port.gestures[0].second, c.what);  // the draft's target
    TEST_ASSERT_FALSE_MESSAGE(r.shell.menu_open(), c.what);
    TEST_ASSERT_TRUE_MESSAGE(r.shell.overlay() == c.after, c.what);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.port.notes.size(), c.what);  // nothing saved on the way
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.port.agents.size(), c.what);
  }
}

// A meeting already records: under the menu G0 still stars it (docs/13 §6.1).
static void test_a_meeting_keeps_its_star() {
  Rig r;
  r.port.app.rec.avail = true;
  r.port.app.rec.phase = RecPhase::Recording;
  r.port.app.rec.mode = RecMode::Meeting;
  r.key(nav(Nav::Menu), 10);
  r.key(g0long(), 20);
  TEST_ASSERT_EQUAL_UINT32(0, r.port.gestures.size());
  TEST_ASSERT_TRUE(r.port.inputs.size() == 1 && r.port.inputs[0].first == rec::RecInput::Star);
}

// Through the runtime: a real memo under the editor, and the draft is still there after it.
static void test_memo_under_the_editor_keeps_the_draft() {
  RecRig x;
  x.r.step(10, KeySet{}.with(key::kG0));  // a tap: the editor
  x.r.step(10);
  x.r.step(10, KeySet{}.with(layout_index(2, 2)));  // 'a'
  x.r.step(10);
  TEST_ASSERT_TRUE(x.r.rt.shell().overlay() == proto::Overlay::Compose);
  x.hold_g0(600);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  x.port.feed(2000);
  x.let_go();  // gh #51: the grace runs out
  x.steps();
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Saved);
  TEST_ASSERT_TRUE(x.r.rt.shell().overlay() == proto::Overlay::Compose);
  x.r.step(60);
  TEST_ASSERT_TRUE_MESSAGE(x.r.disp.canvas_.has("a_"), x.r.disp.canvas_.dump().c_str());
  TEST_ASSERT_EQUAL_UINT16(0, x.r.rt.state().outbox.pending);  // the draft was not saved
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_hold_records_under_every_overlay);
  RUN_TEST(test_a_meeting_keeps_its_star);
  RUN_TEST(test_memo_under_the_editor_keeps_the_draft);
  return UNITY_END();
}
