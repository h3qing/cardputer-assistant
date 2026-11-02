// Recording overlays (docs/11 §3, §4.1-§4.2): the key tables of §4.1 as the shell sends
// them to the recording core, and what each overlay draws: the memo HUD, the meeting
// consent confirm, the meeting page and the end confirm. Overlays are exclusive: while
// one is up no key reaches the global layer or the module. v0.5 (docs/19 §6.6-§6.7): the
// pages have Mando, a big ● and a Nunito timer; the words of both confirms are unchanged.
#include <unity.h>

#include "core/support/lang_pin.h"
#include "rec_overlay.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"
#include "theme_digits.h"

using namespace cpa;
using namespace uitest;
using rec::RecInput;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

struct Rig {
  FakePort port;
  ProbeModule board{"board"};
  ProbeModule inbox{"inbox"};
  cpa::Module* mods[2] = {&board, &inbox};
  Shell shell{port, mods, 2};
  Rig() {
    port.app = demo_state();
    port.next_refusal = rec::RecRefusal::None;
    shell.begin(0);
  }
  void phase(RecPhase p, RecMode m, uint32_t opened = 0) {
    port.app.rec.avail = true;
    port.app.rec.phase = p;
    port.app.rec.mode = m;
    port.app.rec.opened_ms = opened;
  }
  std::string screen(uint32_t now) {
    FakeCanvas c;
    shell.render(c, now);
    std::string all;
    for (const auto& t : c.texts) all += t.text + "\n";
    return all;
  }
};

// docs/19 §6.6-§6.7: the page's big ● (RecDot) and timer (Nunito) at the mockup's place.
static void assert_clock(const FakeCanvas& c, int16_t dot_x, int16_t clock_x, const char* clock) {
  bool dot = false;
  for (const DotOp& d : c.dots) dot = dot || (d.cx == dot_x && d.cy == 44 && d.diameter == 9 && d.paint == Color::RecDot);
  TEST_ASSERT_TRUE_MESSAGE(dot, c.dump().c_str());
  bool timer = false;
  for (const DigitOp& d : c.digit_ops) {
    timer = timer || (d.x == clock_x && d.y == 33 && d.text == clock && d.font == DigitFont::Timer && d.ink == Color::Ink);
  }
  TEST_ASSERT_TRUE_MESSAGE(timer, c.dump().c_str());
}

// gh #53: the memo page's legend (one row at y 103 for demo_state's two projects); where the
// memo goes is the lit item, the live-words capsule "Save to #cpa" was.
static const TextOp* legend_label(const FakeCanvas& c, const char* label) {
  for (const TextOp& t : c.texts) {
    if (t.text == label && t.y == 103) return &t;
  }
  return nullptr;
}

static bool sent(const Rig& r, RecInput in, uint8_t arg = 0) {
  for (const auto& p : r.port.inputs) {
    if (p.first == in && p.second == arg) return true;
  }
  return false;
}

static void test_global_gestures() {
  Rig r;
  r.board.slot = 3;
  r.shell.on_key(opt('1'), 5);  // look at project 1: the default tag
  r.board.slot = 1;
  r.shell.on_key(g0long(), 10);
  r.shell.on_key(opt('r'), 20);
  TEST_ASSERT_EQUAL_UINT32(2, r.port.gestures.size());
  TEST_ASSERT_TRUE(r.port.gestures[0].first == RecMode::Memo && r.port.gestures[0].second == 1);
  TEST_ASSERT_TRUE(r.port.gestures[1].first == RecMode::Meeting);
  TEST_ASSERT_EQUAL_UINT32(0, r.board.keys.size());
}

static void test_refusals_are_shown() {
  const struct {
    rec::RecRefusal why;
    const char* text;
  } cases[] = {{rec::RecRefusal::NoSd, "录音需要 SD 卡"},
               {rec::RecRefusal::CardFull, "SD 卡已满"},
               {rec::RecRefusal::Pending, "请先连 Mac 同步"},
               {rec::RecRefusal::Battery, "电量低"},
               {rec::RecRefusal::NoMemory, "内存不足"},
               {rec::RecRefusal::NoIds, "NVS"}};
  for (const auto& c : cases) {
    Rig r;
    r.port.next_refusal = c.why;
    r.shell.on_key(g0long(), 10);
    TEST_ASSERT_TRUE_MESSAGE(r.screen(20).find(c.text) != std::string::npos, c.text);
  }
}

static void test_memo_hud_keys() {
  Rig r;
  r.phase(RecPhase::Recording, RecMode::Memo);
  r.shell.on_key(chr('7'), 10);
  r.shell.on_key(chr('0'), 11);
  r.shell.on_key(g0longup(), 12);  // gh #51: a memo follows G0's edges (on_g0), not its events
  r.shell.on_key(chr(' '), 13);    // no stars in a memo
  r.shell.on_key(opt('h'), 14);    // globals are off
  r.shell.on_key(opt('r'), 15);
  r.shell.on_key(chr('a'), 16);
  TEST_ASSERT_TRUE(sent(r, RecInput::Tag, 7) && sent(r, RecInput::Tag, 0));
  TEST_ASSERT_EQUAL_UINT32(2, r.port.inputs.size());
  TEST_ASSERT_EQUAL_UINT32(0, r.board.keys.size());
  TEST_ASSERT_EQUAL_UINT32(0, r.port.gestures.size());
  r.phase(RecPhase::Cue, RecMode::Memo);
  r.shell.on_g0(false, 20);  // released before the microphone came up
  TEST_ASSERT_TRUE(r.port.inputs.back().first == RecInput::Release);
}

// gh #51: the memo follows G0 edge by edge (the runtime's key samples). A re-press in the
// grace is the memo's: its events are eaten until it goes up. Held, Space and Opt+L do
// nothing (docs/04 §8.6); a hands-free memo is shell_g0's (test_g0_double_tap).
static void test_memo_follows_g0() {
  Rig r;
  r.phase(RecPhase::Recording, RecMode::Memo);
  r.shell.on_g0(false, 10);
  r.shell.on_g0(true, 11);
  for (const KeyEvent& k : {g0longup(), g0tap(), g0long()}) r.shell.on_key(k, 12);
  r.shell.on_key(chr(' '), 13);
  r.shell.on_key(opt('l'), 14);
  TEST_ASSERT_FALSE(r.shell.locked());
  TEST_ASSERT_EQUAL_UINT32(2, r.port.inputs.size());
  TEST_ASSERT_TRUE(r.port.inputs[0].first == RecInput::Release && r.port.inputs[1].first == RecInput::Press);
  TEST_ASSERT_EQUAL_UINT32(0, r.port.gestures.size());
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::None);
}

// The edges reach a memo (its cue too) and the release a held end waits for (U15), nothing
// else: G0 opens the quick note and stars a meeting through its events.
static void test_g0_edges_reach_only_a_memo() {
  Rig r;
  r.shell.on_g0(true, 10);
  r.shell.on_g0(false, 11);
  r.phase(RecPhase::Recording, RecMode::Meeting);
  r.shell.on_g0(true, 12);
  r.shell.on_g0(false, 13);
  r.phase(RecPhase::Finishing, RecMode::Memo);
  r.shell.on_g0(true, 14);
  r.shell.on_g0(false, 15);
  TEST_ASSERT_EQUAL_UINT32(0, r.port.inputs.size());
  r.phase(RecPhase::Idle, RecMode::Memo);
  r.port.app.rec.held = true;
  r.shell.on_g0(true, 16);
  r.shell.on_g0(false, 17);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.inputs.size());
  TEST_ASSERT_TRUE(r.port.inputs[0].first == RecInput::Release);
  r.port.app.rec.held = false;
  r.phase(RecPhase::Cue, RecMode::Memo);
  r.shell.on_g0(true, 18);
  TEST_ASSERT_TRUE(r.port.inputs.back().first == RecInput::Press);
}

static void test_meeting_page_keys() {
  Rig r;
  r.phase(RecPhase::Recording, RecMode::Meeting);
  for (const KeyEvent& k : {g0tap(), g0long(), chr(' ')}) r.shell.on_key(k, 10);
  r.shell.on_key(chr('3'), 11);
  r.shell.on_key(opt('r'), 12);
  r.shell.on_key(chr('`'), 13);  // docs/20 D3: no longer ends, only says how
  r.shell.on_key(chr('a'), 14);
  r.shell.on_key(opt('i'), 15);
  r.shell.on_key(g0longup(), 16);
  r.shell.on_key(enter(), 17);
  size_t stars = 0;
  for (const auto& p : r.port.inputs) stars += p.first == RecInput::Star ? 1 : 0;
  TEST_ASSERT_EQUAL_UINT32(3, stars);
  TEST_ASSERT_TRUE(sent(r, RecInput::Tag, 3));
  size_t ends = 0;
  for (const auto& p : r.port.inputs) ends += p.first == RecInput::End ? 1 : 0;
  TEST_ASSERT_EQUAL_UINT32(1, ends);
  TEST_ASSERT_EQUAL_UINT32(5, r.port.inputs.size());
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  TEST_ASSERT_EQUAL_UINT32(0, r.board.keys.size());
}

// docs/20 D3: on the start box Space (any key but Enter and Tab) cancels too; the end box
// ignores it.
static void test_confirm_keys() {
  for (RecPhase p : {RecPhase::Confirm, RecPhase::EndConfirm}) {
    Rig r;
    r.phase(p, RecMode::Meeting);
    r.shell.on_key(enter(), 10);
    r.shell.on_key(chr('`'), 11);
    r.shell.on_key(esc(), 12);
    r.shell.on_key(chr(' '), 13);
    r.shell.on_key(g0long(), 14);
    TEST_ASSERT_TRUE(sent(r, RecInput::Confirm) && sent(r, RecInput::Cancel));
    TEST_ASSERT_EQUAL_UINT32(p == RecPhase::Confirm ? 4 : 3, r.port.inputs.size());
  }
  Rig f;
  f.phase(RecPhase::Finishing, RecMode::Meeting);
  f.shell.on_key(enter(), 10);
  f.shell.on_key(chr('`'), 11);
  TEST_ASSERT_EQUAL_UINT32(0, f.port.inputs.size());
}

// The cue page is the only "start talking" signal (v0.3.1: no beep): it never promises
// a sound, and its hint fits one 240 px line.
static void test_memo_cue_page_is_silent() {
  Rig r;
  r.phase(RecPhase::Cue, RecMode::Memo);
  FakeCanvas c;
  r.shell.render(c, 100);
  bool hint = false;
  for (const auto& t : c.texts) {
    TEST_ASSERT_TRUE_MESSAGE(t.text.find("嘀") == std::string::npos, t.text.c_str());
    TEST_ASSERT_TRUE_MESSAGE(t.text.find("声") == std::string::npos, t.text.c_str());
    if (t.text != "看到 ● REC 再说话") continue;
    hint = true;
    TEST_ASSERT_TRUE(t.x + c.textWidth(t.text.c_str()) <= kScreenW);
  }
  TEST_ASSERT_TRUE(hint);
}

static void test_memo_hud_screen() {
  Rig r;
  r.phase(RecPhase::Cue, RecMode::Memo);
  TEST_ASSERT_TRUE(r.screen(100).find("● 准备") != std::string::npos);
  r.phase(RecPhase::Recording, RecMode::Memo);
  r.port.app.rec.started_ms = 1000;
  r.port.app.rec.slot = 1;
  r.port.app.rec.level = 16;  // the room
  FakeCanvas page;
  r.shell.render(page, 13000);
  assert_clock(page, 66, 75, "0:12");
  TEST_ASSERT_TRUE(page.find("● 0:12")->style.fg == Color::Rec);  // the status bar's slot
  TEST_ASSERT_TRUE(page.find("在听") != nullptr && legend_label(page, "cpa")->style.fg == Color::Accent);
  TEST_ASSERT_EQUAL_STRING("1|cpa|2|blog|0|不归属", page.row(103).c_str());  // gh #53: what 1-9 and 0 are
  TEST_ASSERT_TRUE(page.arts[0].id == theme::mando_art(theme::MandoSize::M64, theme::MandoAct::Tilt, 0));  // quiet
  TEST_ASSERT_TRUE(legend_label(page, "blog")->style.fg == Color::Ink2);
  // mockup 04: [G0]let go: stop [1-9]project ("0 none" is the legend's, gh #53 review)
  TEST_ASSERT_EQUAL_STRING("G0 松开结束 1-9 选项目", page.line(kHintRect.y).c_str());
  // the cue says "Speak once you see ● REC" (v0.3.1): REC stands right of the timer, in Rec
  const TextOp* rec = page.find("REC");
  TEST_ASSERT_NOT_NULL(rec);
  TEST_ASSERT_TRUE(rec->style.fg == Color::Rec && rec->y == 38);
  TEST_ASSERT_EQUAL_INT16(75 + theme::digits_width("0:12", DigitFont::Timer) + 5, rec->x);
  r.port.app.rec.slot = 0;
  FakeCanvas none;
  r.shell.render(none, 13000);
  TEST_ASSERT_TRUE(legend_label(none, "不归属")->style.fg == Color::Accent);
  TEST_ASSERT_TRUE(legend_label(none, "cpa")->style.fg == Color::Ink2);
  // saving: Mando's sign says so once (the hint is empty), and the waveform stays, in Live
  r.phase(RecPhase::Finishing, RecMode::Memo);
  FakeCanvas saving;
  r.shell.render(saving, 13000);
  TEST_ASSERT_TRUE(saving.has("保存中…") && !saving.has("REC"));  // it no longer records
  TEST_ASSERT_EQUAL_STRING("", saving.line(kHintRect.y).c_str());
  int live = 0;
  for (const auto& f : saving.fills) {
    if (f.first.w != 3 || f.first.y < 63 || f.first.y + f.first.h > 88) continue;
    TEST_ASSERT_TRUE(f.second == Color::Live);
    ++live;
  }
  TEST_ASSERT_EQUAL_INT(35, live);
}

// gh #51: the grace and hands free on the memo page, in the theme: a Warn capsule, or an
// Info capsule that says plainly how it ends, in place of the title row; Mando listens on,
// ● REC stays. Each state has its hint bar, as keycaps.
static void test_memo_grace_and_hands_free_screens() {
  Rig r;
  r.phase(RecPhase::Recording, RecMode::Memo);
  r.port.app.rec.started_ms = 1000;
  r.port.app.rec.level = kVoiceLevel;
  r.port.app.rec.slot = 1;
  r.port.app.rec.grace = true;
  FakeCanvas g;
  r.shell.render(g, 13000);
  const TextOp* t = g.find("松开了 · 0.6 秒内按回继续");
  TEST_ASSERT_NOT_NULL_MESSAGE(t, g.dump().c_str());
  TEST_ASSERT_TRUE(t->style.fg == Color::Warn && t->x >= 60 && t->y < 33);
  TEST_ASSERT_TRUE(t->x + g.textWidth(t->text.c_str()) <= kScreenW - theme::kGutter);
  TEST_ASSERT_TRUE(g.has("在听") && g.has("REC") && !g.has("语音便条"));
  TEST_ASSERT_TRUE(rec_mando(r.port.app.rec) == theme::MandoAct::Listen);  // he listens on
  TEST_ASSERT_EQUAL_STRING("G0 按回继续 1-9 选项目", g.line(kHintRect.y).c_str());
  r.port.app.rec.grace = false;
  r.port.app.rec.latched = true;
  FakeCanvas l;
  r.shell.render(l, 13000);
  t = l.find("免提录音 · 按 G0 结束");
  TEST_ASSERT_NOT_NULL_MESSAGE(t, l.dump().c_str());
  TEST_ASSERT_TRUE(t->style.fg == Color::Accent && t->x >= 60 && t->y < 33);
  TEST_ASSERT_TRUE(t->x + l.textWidth(t->text.c_str()) <= kScreenW - theme::kGutter);
  TEST_ASSERT_TRUE(l.has("在听") && l.has("REC") && !l.has("语音便条"));
  TEST_ASSERT_TRUE(legend_label(l, "cpa")->style.fg == Color::Accent);
  TEST_ASSERT_TRUE(rec_mando(r.port.app.rec) == theme::MandoAct::Listen);
  // review of gh #53: "0 none" is the legend's, not the bar's; "[1-9]project" whole again
  TEST_ASSERT_EQUAL_STRING("G0 结束 1-9 选项目 Opt+L 锁屏", l.line(kHintRect.y).c_str());
  r.port.app.rec.latched = false;
  FakeCanvas h;
  r.shell.render(h, 13000);
  TEST_ASSERT_TRUE(h.has("语音便条"));
  TEST_ASSERT_EQUAL_STRING("G0 松开结束 1-9 选项目", h.line(kHintRect.y).c_str());
}

static void test_meeting_screens() {
  Rig r;
  r.phase(RecPhase::Confirm, RecMode::Meeting);
  std::string s = r.screen(100);
  TEST_ASSERT_TRUE(s.find("开始会议录音？") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("请先告知在场的人") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("Enter\n开始") != std::string::npos);  // [Enter]start
  r.phase(RecPhase::Recording, RecMode::Meeting);
  r.port.app.rec.started_ms = 0;
  r.port.app.rec.stars = 2;
  r.port.app.rec.seg = 2;
  r.port.app.rec.slot = 1;
  s = r.screen(751000);
  FakeCanvas page;
  r.shell.render(page, 751000);
  assert_clock(page, 52, 60, "12:31");
  TEST_ASSERT_TRUE(page.find("在听") != nullptr && page.find("开会") != nullptr);
  TEST_ASSERT_TRUE(s.find("★2") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("第 3 段") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("#cpa") != std::string::npos);
  r.port.app.rec.star_ms = 751000;  // a star just now: the page flashes it
  TEST_ASSERT_TRUE(r.screen(751100).find("★ 已标记") != std::string::npos);
  TEST_ASSERT_TRUE(r.screen(752000).find("★ 已标记") == std::string::npos);
  r.phase(RecPhase::EndConfirm, RecMode::Meeting);
  s = r.screen(752000);
  TEST_ASSERT_TRUE(s.find("结束录音？") != std::string::npos);
  FakeCanvas behind;
  r.shell.render(behind, 752000);
  TEST_ASSERT_EQUAL_STRING("Enter 结束 ` 继续录", behind.line(90).c_str());  // the card's keys as keycaps
  assert_clock(behind, 52, 60, "12:32");  // still recording behind the box
}

// docs/19 §7.2: Mando nods while someone talks, tilts his head in a pause, sits before the
// microphone is up and while asked to end, sniffs while saving; his sign says so.
static void test_mando_listens() {
  RecInfo r;
  r.phase = RecPhase::Cue;
  TEST_ASSERT_TRUE(rec_mando(r) == theme::MandoAct::Idle);
  TEST_ASSERT_NULL(rec_sign(r));
  r.phase = RecPhase::Recording;
  r.level = kVoiceLevel;
  TEST_ASSERT_TRUE(rec_mando(r) == theme::MandoAct::Listen);
  r.level = kVoiceLevel - 1;
  TEST_ASSERT_TRUE(rec_mando(r) == theme::MandoAct::Tilt);
  // The same -50 dBFS a tick's voice starts at (theme_levels.h).
  TEST_ASSERT_EQUAL_UINT8(theme::kVoiceOn, theme::level_q(kVoiceLevel));
  TEST_ASSERT_TRUE(theme::level_q(kVoiceLevel - 1) < theme::kVoiceOn);
  TEST_ASSERT_EQUAL_STRING("在听", rec_sign(r));
  r.phase = RecPhase::EndConfirm;
  TEST_ASSERT_TRUE(rec_mando(r) == theme::MandoAct::Idle);
  TEST_ASSERT_EQUAL_STRING("在听", rec_sign(r));
  r.phase = RecPhase::Finishing;
  TEST_ASSERT_TRUE(rec_mando(r) == theme::MandoAct::Sniff);
  TEST_ASSERT_EQUAL_STRING("保存中…", rec_sign(r));
}

// docs/11 §3.2: the consent text is v0.4.0's, word for word and row for row, on a card.
static void test_meeting_confirm_text_is_unchanged() {
  Rig r;
  r.phase(RecPhase::Confirm, RecMode::Meeting);
  FakeCanvas c;
  r.shell.render(c, 100);
  TEST_ASSERT_EQUAL_STRING("开始会议录音？", c.row(24).c_str());
  TEST_ASSERT_EQUAL_STRING("请先告知在场的人", c.row(48).c_str());
  // the same line, its keys drawn as keycaps (docs/19 §6.7); docs/20 B2: the hint no longer
  // repeats it, it names Opt+L (test_rec_pages_ux)
  TEST_ASSERT_EQUAL_STRING("Enter 开始 ` 取消", c.line(84).c_str());
  TEST_ASSERT_EQUAL_STRING("Enter 开始    ` 取消", tr(kMeetingConfirmKeys));
  TEST_ASSERT_TRUE(c.line(kHintRect.y) != c.line(84));
  TEST_ASSERT_TRUE(c.find("开始会议录音？")->style.fg == Color::Warn);
  TEST_ASSERT_TRUE(c.filled_at(48, Color::Card));
  TEST_ASSERT_TRUE(c.dots.empty());  // nothing recording yet
  // Mando sits at the card's right, clear of the words
  TEST_ASSERT_EQUAL_UINT32(1, c.arts.size());
  TEST_ASSERT_TRUE(c.arts[0].id == theme::mando_art(theme::MandoSize::M46, theme::MandoAct::Idle, 0));
  for (const TextOp& t : c.texts) {
    if (t.y > kBarH && t.y < kHintRect.y) TEST_ASSERT_TRUE(t.x + c.textWidth(t.text.c_str()) < c.arts[0].x);
  }
}

static void test_no_overlay_when_idle() {
  Rig r;
  const std::string s = r.screen(100);
  TEST_ASSERT_TRUE(s.find("REC") == std::string::npos);
  TEST_ASSERT_TRUE(s.find("board") != std::string::npos);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_global_gestures);
  RUN_TEST(test_refusals_are_shown);
  RUN_TEST(test_memo_hud_keys);
  RUN_TEST(test_memo_follows_g0);
  RUN_TEST(test_g0_edges_reach_only_a_memo);
  RUN_TEST(test_meeting_page_keys);
  RUN_TEST(test_confirm_keys);
  RUN_TEST(test_memo_cue_page_is_silent);
  RUN_TEST(test_memo_hud_screen);
  RUN_TEST(test_memo_grace_and_hands_free_screens);
  RUN_TEST(test_meeting_screens);
  RUN_TEST(test_no_overlay_when_idle);
  RUN_TEST(test_mando_listens);
  RUN_TEST(test_meeting_confirm_text_is_unchanged);
  return UNITY_END();
}
