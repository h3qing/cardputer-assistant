// The meeting's two boxes after docs/20 D3 (U1, U23), through the shell and the runtime:
//   start box  Enter (after 400 ms) starts; Opt+L cancels and locks; a letter, a digit or any
//              other key but Enter and Tab cancels; 15 s without a key cancels; a too-fast
//              Enter says "再按一次 Enter"; G0 says the box comes first; it is not a
//              recording (file.* is answered, pong says not live);
//   end box    Enter (after 400 ms) ends; ` keeps recording; Opt+L keeps recording and
//              locks; 10 s without a key goes back to the meeting;
//   meeting    ` no longer opens the end box: it says how to end (Opt+R), as Enter does;
//   idle       both boxes count 15 s / 10 s from the last key, and wait while the Tab menu is
//              open over them (the review's probe: an end box gone under the menu, then
//              the Enter meant to end the meeting fell silently on the meeting page);
//   hint       "先处理确认框" goes with the box, however the box goes.
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
using rec::CmdKind;
using rec::RecInput;

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
    shell.begin(0);
  }
  void phase(RecPhase p, RecMode m = RecMode::Meeting) {
    port.app.rec.avail = true;
    port.app.rec.phase = p;
    port.app.rec.mode = m;
  }
  void key(const KeyEvent& ev, uint32_t at = 1000) {
    if (shell.lock_gate(ev, at)) shell.on_key(ev, at);
  }
  std::string toast(uint32_t at = 1000) {
    FakeCanvas c;
    shell.render(c, at);
    return c.line(kHintRect.y);
  }
};

const KeySet kOptR = KeySet{}.with(key::kOpt).with(layout_index(4, 1));
const KeySet kOptL = KeySet{}.with(key::kOpt).with(layout_index(10, 2));
const KeySet kEnterKey = KeySet{}.with(key::kEnter);
const KeySet kGraveKey = KeySet{}.with(key::kGrave);
const KeySet kTabKey = KeySet{}.with(key::kTab);
const KeySet kLetterE = KeySet{}.with(layout_index(3, 1));  // 'e'

void press(RecRig& x, KeySet ks, uint32_t wait_before = 0) {
  if (wait_before) x.r.step(wait_before);
  x.r.step(10, ks);
  x.r.step(10);
}

std::string screen(RecRig& x) {
  x.r.step(60);
  return x.r.disp.canvas_.dump();
}

}  // namespace

// D3: on the start box every key but Enter and Tab cancels it (a letter typed on the
// way, U5): nothing is left open for a pocket Enter.
static void test_start_box_keys_other_than_enter_cancel() {
  for (const KeyEvent& k : {chr('e'), chr('5'), chr(' '), chr('`'), bksp(), esc(), opt('h'), opt('r'), nav(Nav::Down)}) {
    Rig r;
    r.phase(RecPhase::Confirm);
    r.key(k, 1000);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, r.port.inputs.size(), key_label(k).c_str());
    TEST_ASSERT_TRUE_MESSAGE(r.port.inputs[0].first == RecInput::Cancel, key_label(k).c_str());
    TEST_ASSERT_EQUAL_UINT32(0, r.board.keys.size());
  }
  Rig r;
  r.phase(RecPhase::Confirm);
  r.key(enter(), 1000);
  TEST_ASSERT_TRUE(r.port.inputs.size() == 1 && r.port.inputs[0].first == RecInput::Confirm);
}

// The end box keeps v0.4's keys: a letter does nothing (the meeting goes on either way).
static void test_end_box_ignores_letters() {
  Rig r;
  r.phase(RecPhase::EndConfirm);
  r.key(chr('e'), 1000);
  r.key(chr('5'), 1000);
  TEST_ASSERT_EQUAL_UINT32(0, r.port.inputs.size());
  r.key(chr('`'), 1000);
  TEST_ASSERT_TRUE(r.port.inputs.size() == 1 && r.port.inputs[0].first == RecInput::Cancel);
}

// D3: Opt+L on the start box cancels it and locks; on the end box it keeps recording (back
// to the meeting page) and locks, as Opt+L on the meeting page does.
static void test_opt_l_on_the_boxes() {
  Rig start;
  start.phase(RecPhase::Confirm);
  start.key(opt('l'), 1000);
  TEST_ASSERT_TRUE(start.shell.locked());
  TEST_ASSERT_TRUE(start.port.inputs.size() == 1 && start.port.inputs[0].first == RecInput::Cancel);
  Rig end;
  end.phase(RecPhase::EndConfirm);
  end.key(opt('l'), 1000);
  TEST_ASSERT_TRUE(end.shell.locked());
  TEST_ASSERT_TRUE(end.port.inputs.size() == 1 && end.port.inputs[0].first == RecInput::Cancel);
  TEST_ASSERT_TRUE(end.shell.locks(opt('l')) == false);  // locked now: Opt+L lights the card
}

// D4 / U4: G0 on a meeting box records nothing and says why.
static void test_g0_on_a_box_says_the_box_comes_first() {
  for (RecPhase p : {RecPhase::Confirm, RecPhase::EndConfirm}) {
    Rig r;
    r.phase(p);
    r.key(g0long(), 1000);
    r.key(g0longup(), 1500);
    TEST_ASSERT_EQUAL_UINT32(0, r.port.gestures.size());
    TEST_ASSERT_EQUAL_UINT32(0, r.port.inputs.size());
    TEST_ASSERT_TRUE_MESSAGE(r.toast(1500).find("先处理确认框") != std::string::npos, r.toast(1500).c_str());
  }
}

// D3 / U23: ` on the meeting page no longer opens the end box; it says how to end. So does
// Enter: one meant for an end box that went away is never silent.
static void test_grave_on_the_meeting_page_only_hints() {
  for (const KeyEvent& k : {chr('`'), esc(), enter()}) {
    Rig r;
    r.phase(RecPhase::Recording);
    r.key(k, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, r.port.inputs.size());
    TEST_ASSERT_TRUE_MESSAGE(r.toast().find("开会中：Opt+R 结束") != std::string::npos, r.toast().c_str());
  }
  Rig r;
  r.phase(RecPhase::Recording);
  r.key(chr('`'), 1000);
  r.key(opt('r'), 1100);
  TEST_ASSERT_TRUE(r.port.inputs.size() == 1 && r.port.inputs[0].first == RecInput::End);
  // the page's hint is about the page: gone with the end box, so what the box says is seen at once
  TEST_ASSERT_TRUE_MESSAGE(r.toast(1100).find("开会中") == std::string::npos, r.toast(1100).c_str());
}

// -- through the runtime -------------------------------------------------------------

// U1: the habit "Opt+R, Enter, Opt+L" done briskly. The Enter inside 400 ms says so; the
// Opt+L cancels the box and locks, so a later pocket Enter starts nothing.
static void test_brisk_habit_leaves_nothing_open() {
  RecRig x;
  press(x, kOptR);
  press(x, kEnterKey, 150);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Confirm);
  TEST_ASSERT_TRUE_MESSAGE(screen(x).find("再按一次 Enter") != std::string::npos, screen(x).c_str());
  press(x, kOptL, 100);
  TEST_ASSERT_TRUE(x.r.rt.shell().locked());
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  press(x, kEnterKey, 60000);
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_EQUAL_UINT(0, x.port.count(CmdKind::Start));
}

// U1: a box left alone goes away: the start box after 15 s, the end box after 10 s (the
// meeting goes on, no 3-hour recording by accident of an unseen box).
static void test_boxes_left_alone_go_away() {
  RecRig x;
  press(x, kOptR);
  x.r.step(rec::kStartBoxMs - 100);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Confirm);
  x.r.step(200);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  press(x, kOptR);
  press(x, kEnterKey, 450);
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  x.port.feed(2000);
  press(x, kOptR);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::EndConfirm);
  x.r.step(rec::kEndBoxMs + 100);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  TEST_ASSERT_EQUAL_UINT(0, x.port.count(CmdKind::Stop));
  press(x, kGraveKey);  // ` only hints now
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
}

// A meeting on the page, its end box open (Opt+R) at the end.
void end_box(RecRig& x) {
  press(x, kOptR);
  press(x, kEnterKey, 450);
  x.steps();
  x.port.feed(2000);
  press(x, kOptR);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::EndConfirm);
}

// D3 "10 s idle" (the review's probe): Tab at +1 s, the menu read 9.5 s, Tab, Enter ends the
// meeting; the box waits while the menu is open over it, however long.
static void test_the_end_box_waits_under_the_menu() {
  RecRig x;
  end_box(x);
  press(x, kTabKey, 1000);
  x.r.step(9500);
  press(x, kTabKey);
  TEST_ASSERT_FALSE(x.r.rt.shell().menu_open());
  press(x, kEnterKey, 100);
  x.steps();
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Stop));
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase != RecPhase::Recording);
  RecRig y;
  end_box(y);
  press(y, kTabKey, 500);
  y.r.step(60000);
  TEST_ASSERT_TRUE(y.r.rt.state().rec.phase == RecPhase::EndConfirm);
  press(y, kTabKey);
  y.r.step(rec::kEndBoxMs + 100);  // the menu closed: 10 s from the last key
  TEST_ASSERT_TRUE(y.r.rt.state().rec.phase == RecPhase::Recording);
}

// D3 "15 s / 10 s idle": a key on a box starts its time again (a letter the end box ignores),
// and the start box read under the menu still starts the meeting with Enter.
static void test_a_key_keeps_a_box() {
  RecRig x;
  end_box(x);
  press(x, kLetterE, 8000);
  x.r.step(8000);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::EndConfirm);
  x.r.step(rec::kEndBoxMs - 8000);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  RecRig y;
  press(y, kOptR);
  press(y, kTabKey, 500);
  y.r.step(20000);
  press(y, kTabKey);
  press(y, kEnterKey, 100);
  y.steps();
  TEST_ASSERT_TRUE(y.r.rt.state().rec.phase == RecPhase::Recording);
}

// G0 held on the start box (the habit of holding G0 to talk) says "先处理确认框"; the hint
// is about the box and goes with it: `, Opt+R, or Enter starting the meeting.
static void test_the_box_hint_goes_with_the_box() {
  for (const KeySet& closer : {kGraveKey, kOptR, kEnterKey}) {
    RecRig x;
    press(x, kOptR);
    x.r.step(600);
    x.hold_g0(2000);
    x.release();
    TEST_ASSERT_TRUE_MESSAGE(screen(x).find("先处理确认框") != std::string::npos, screen(x).c_str());
    press(x, closer);
    x.steps();
    TEST_ASSERT_TRUE(x.r.rt.state().rec.phase != RecPhase::Confirm);
    TEST_ASSERT_TRUE_MESSAGE(screen(x).find("先处理确认框") == std::string::npos, screen(x).c_str());
  }
}

// D3: the start box is not a recording: the Mac's pull goes on (no E_BUSY "live").
static void test_start_box_lets_the_mac_pull() {
  RecRig x;
  x.memo(3000);
  const std::string fid = mono_format(x.port.idx.at(0).id).c_str();
  press(x, kOptR);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Confirm);
  const size_t errors = x.r.tx.count("error");
  x.r.send("file.get", "\"fid\":\"" + fid + "\",\"off\":0,\"len\":100");
  x.r.step();
  x.steps();
  TEST_ASSERT_EQUAL_UINT(errors, x.r.tx.count("error"));
  TEST_ASSERT_EQUAL_UINT(1, x.r.tx.count("file.data"));
  x.r.send("ping");
  x.r.step();
  TEST_ASSERT_FALSE(std::get<proto::Pong>(x.last("pong").msg.body).files.value.live);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_start_box_keys_other_than_enter_cancel);
  RUN_TEST(test_end_box_ignores_letters);
  RUN_TEST(test_opt_l_on_the_boxes);
  RUN_TEST(test_g0_on_a_box_says_the_box_comes_first);
  RUN_TEST(test_grave_on_the_meeting_page_only_hints);
  RUN_TEST(test_brisk_habit_leaves_nothing_open);
  RUN_TEST(test_boxes_left_alone_go_away);
  RUN_TEST(test_start_box_lets_the_mac_pull);
  RUN_TEST(test_the_end_box_waits_under_the_menu);
  RUN_TEST(test_a_key_keeps_a_box);
  RUN_TEST(test_the_box_hint_goes_with_the_box);
  return UNITY_END();
}
