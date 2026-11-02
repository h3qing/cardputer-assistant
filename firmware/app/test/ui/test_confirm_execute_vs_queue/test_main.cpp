// Confirm routing (05 §5.6 table, 04 US-09 AC2, US-10 AC2/AC3): execute only when the
// conditions hold, otherwise save a queue item (kind=queue, intent spawn/feedback)
// and say why. Offline execution is never deferred (04 §9 rule).
#include <unity.h>

#include <string>

#include "confirm.h"
#include "core/support/lang_pin.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"
#include "text_rules.h"

using namespace cpa;
using namespace uitest;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

static ConfirmSpec spawn_spec() {
  ConfirmSpec s;
  s.kind = ConfirmKind::Spawn;
  s.slot = 1;
  s.text = UserText::from("Write native tests");
  return s;
}

static ConfirmSpec reply_spec(const char* sid) {
  ConfirmSpec s;
  s.kind = ConfirmKind::Feedback;
  s.slot = 1;
  s.sid = some(Sid::from(sid));
  s.text = UserText::from("looks good");
  return s;
}

static void test_spawn_needs_ready_and_agent() {
  AppState s = demo_state();
  TEST_ASSERT_TRUE(confirm_decide(spawn_spec(), s).route == Route::Queue);
  TEST_ASSERT_TRUE(confirm_decide(spawn_spec(), s).reason == QueueReason::Offline);
  s.link.mode = LinkMode::Attached;
  TEST_ASSERT_TRUE(confirm_decide(spawn_spec(), s).reason == QueueReason::Offline);
  set_ready(s, false);
  TEST_ASSERT_TRUE(confirm_decide(spawn_spec(), s).reason == QueueReason::NoAgent);
  set_ready(s, true);
  TEST_ASSERT_TRUE(confirm_decide(spawn_spec(), s).route == Route::Execute);
  s.link.cap_spawn = false;
  TEST_ASSERT_TRUE(confirm_decide(spawn_spec(), s).route == Route::Queue);
}

static void test_reply_needs_bg_idle_ready_and_caps() {
  AppState s = demo_state();  // 8d3e7a19 tty waiting, 5c2e8a14 bg idle
  set_ready(s, true, true);
  TEST_ASSERT_TRUE(confirm_decide(reply_spec("5c2e8a14"), s).route == Route::Execute);
  TEST_ASSERT_TRUE(confirm_decide(reply_spec("8d3e7a19"), s).reason == QueueReason::Interactive);
  s.sessions.data.rows[1].st = SessionState::Busy;
  TEST_ASSERT_TRUE(confirm_decide(reply_spec("5c2e8a14"), s).reason == QueueReason::NotIdle);
  s.sessions.data.rows[1].st = SessionState::Idle;
  s.link.cap_reply = false;
  TEST_ASSERT_TRUE(confirm_decide(reply_spec("5c2e8a14"), s).reason == QueueReason::NoReply);
  s.link.cap_reply = true;
  s.link.mode = LinkMode::Detached;
  TEST_ASSERT_TRUE(confirm_decide(reply_spec("5c2e8a14"), s).reason == QueueReason::Offline);
  set_ready(s, true, true);
  TEST_ASSERT_TRUE(confirm_decide(reply_spec("deadbeef"), s).reason == QueueReason::SessionGone);
}

static void test_queue_spawn_from_item_is_never_requeued() {
  AppState s = demo_state();
  ConfirmSpec q = spawn_spec();
  q.qid = some(Qid::from("q17"));
  TEST_ASSERT_TRUE(confirm_decide(q, s).route == Route::Unavailable);
  set_ready(s);
  TEST_ASSERT_TRUE(confirm_decide(q, s).route == Route::Execute);
}

// docs/20 U25: an item saved after an Opt+S whose result never came back may already run on the Mac.
static void test_spawn_from_an_uncertain_item_warns_first() {
  AppState s = demo_state();
  QueueRow unsure = queue_item("q18", 1, "write docs", Intent::Spawn);
  unsure.uncertain = true;
  set_queue(s, {queue_item("q17", 1, "add LittleFS fault-injection test", Intent::Spawn), unsure});
  set_ready(s);
  ConfirmSpec q = spawn_spec();
  q.qid = some(Qid::from("q18"));
  const Decision d = confirm_decide(q, s);
  TEST_ASSERT_TRUE(d.route == Route::Execute && d.maybe_open);
  // review: a line is added; the title still says what Enter does, and where
  TEST_ASSERT_EQUAL_STRING("在 cpa 开 session", confirm_title(ConfirmKind::Spawn, d, "cpa").c_str());
  TEST_ASSERT_EQUAL_STRING("可能已开过，先看 Sessions", confirm_note(d));
  TEST_ASSERT_EQUAL_STRING("", confirm_note(confirm_decide(spawn_spec(), s)));
  q.qid = some(Qid::from("q17"));
  TEST_ASSERT_FALSE(confirm_decide(q, s).maybe_open);
  TEST_ASSERT_FALSE(confirm_decide(spawn_spec(), s).maybe_open);
  s.link.mode = LinkMode::Detached;  // offline: Enter only closes the box, nothing to warn about
  q.qid = some(Qid::from("q18"));
  TEST_ASSERT_FALSE(confirm_decide(q, s).maybe_open);
}

static void test_queue_body_carries_intent_project_and_session() {
  const AppState s = demo_state();
  const auto spawn = confirm_queue_body(spawn_spec(), s);
  TEST_ASSERT_TRUE(spawn.has);
  TEST_ASSERT_TRUE(spawn.value.intent == Intent::Spawn);
  TEST_ASSERT_TRUE(spawn.value.project.equals("cpa"));
  TEST_ASSERT_TRUE(spawn.value.text.equals("Write native tests"));
  TEST_ASSERT_FALSE(spawn.value.uncertain);
  const auto reply = confirm_queue_body(reply_spec("8d3e7a19"), s);
  TEST_ASSERT_TRUE(reply.value.intent == Intent::Feedback);
  TEST_ASSERT_TRUE(reply.value.sid.has && reply.value.sid.value.equals("8d3e7a19"));
  ConfirmSpec nowhere = spawn_spec();
  nowhere.slot = 7;  // no such project: nothing to attach the item to
  TEST_ASSERT_FALSE(confirm_queue_body(nowhere, s).has);
}

struct Rig {
  FakePort port;
  ProbeModule board{"board"};
  cpa::Module* mods[1] = {&board};
  Shell shell{port, mods, 1};
  Rig() {
    port.app = demo_state();
    board.slot = 1;
    shell.begin(0);
  }
  void spawn(const char* text, uint32_t t) {
    shell.on_key(opt('s'), t);
    for (const char* p = text; *p; ++p) shell.on_key(chr(*p), t);
    shell.on_key(enter(), t);
  }
};

static void test_shell_offline_spawn_becomes_queue_item() {
  Rig r;
  r.spawn("add a test", 1000);
  FakeCanvas c;
  r.shell.render(c, 1000);
  TEST_ASSERT_TRUE(c.has("离线：只存到 Mac 队列"));  // docs/20 U7: what the queue path does
  r.shell.on_key(enter(), 1500);
  TEST_ASSERT_EQUAL_UINT32(0, r.port.agents.size());
  TEST_ASSERT_EQUAL_UINT32(1, r.port.queued.size());
  TEST_ASSERT_TRUE(r.port.queued[0].intent == Intent::Spawn);
  TEST_ASSERT_TRUE(r.port.queued[0].project.equals("cpa"));
  TEST_ASSERT_TRUE(r.port.queued[0].text.equals("add a test"));
}

static void test_shell_online_spawn_sends_agent_request() {
  Rig r;
  set_ready(r.port.app);
  r.spawn("add a test", 1000);
  FakeCanvas c;
  r.shell.render(c, 1000);
  TEST_ASSERT_TRUE(c.has("开 session"));  // docs/20 U22: one name for the action
  TEST_ASSERT_TRUE(c.has("plan"));
  r.shell.on_key(enter(), 1400);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.agents.size());
  TEST_ASSERT_TRUE(r.port.agents[0].kind == AgentKind::Spawn);
  TEST_ASSERT_TRUE(r.port.agents[0].project.equals("cpa"));
  TEST_ASSERT_EQUAL_UINT32(0, r.port.queued.size());
}

static void test_shell_link_drop_between_open_and_enter_queues() {
  Rig r;
  set_ready(r.port.app);
  r.spawn("add a test", 1000);
  r.port.app.link.mode = LinkMode::Attached;  // daemon stopped answering meanwhile
  r.shell.on_key(enter(), 1400);
  TEST_ASSERT_EQUAL_UINT32(0, r.port.agents.size());
  TEST_ASSERT_EQUAL_UINT32(1, r.port.queued.size());
}

static void test_shell_busy_agent_request_is_queued() {
  Rig r;
  set_ready(r.port.app);
  r.port.agent_ok = false;  // one already in flight
  r.spawn("add a test", 1000);
  r.shell.on_key(enter(), 1400);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.queued.size());
}

static void test_shell_reply_to_tty_session_is_queued_with_reason() {
  Rig r;
  set_ready(r.port.app, true, true);
  ComposeSpec fb;
  fb.kind = ComposeKind::Feedback;
  fb.slot = 1;
  fb.sid = some(Sid::from("8d3e7a19"));
  r.shell.compose(fb);
  r.shell.on_key(chr('o'), 1000);
  r.shell.on_key(chr('k'), 1000);
  r.shell.on_key(enter(), 1000);
  FakeCanvas c;
  r.shell.render(c, 1000);
  // docs/20 U7: no stale "not until v0.4" (this is v0.4); a terminal session takes no reply
  TEST_ASSERT_TRUE(c.has("终端里的 session：只存到 Mac 队列"));
  r.shell.on_key(enter(), 1400);
  TEST_ASSERT_EQUAL_UINT32(0, r.port.agents.size());
  TEST_ASSERT_EQUAL_UINT32(1, r.port.queued.size());
  TEST_ASSERT_TRUE(r.port.queued[0].intent == Intent::Feedback);
  TEST_ASSERT_TRUE(r.port.queued[0].sid.value.equals("8d3e7a19"));
}

static void test_shell_reply_to_idle_bg_session_executes() {
  Rig r;
  set_ready(r.port.app, true, true);
  ComposeSpec fb;
  fb.kind = ComposeKind::Feedback;
  fb.slot = 1;
  fb.sid = some(Sid::from("5c2e8a14"));
  r.shell.compose(fb);
  r.shell.on_key(chr('k'), 1000);
  r.shell.on_key(enter(), 1000);
  FakeCanvas c;
  r.shell.render(c, 1000);
  TEST_ASSERT_TRUE(c.has("直接发送"));
  r.shell.on_key(enter(), 1400);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.agents.size());
  TEST_ASSERT_TRUE(r.port.agents[0].kind == AgentKind::Reply);
  TEST_ASSERT_TRUE(r.port.agents[0].sid.equals("5c2e8a14"));
}

// 04 US-09 AC1 / US-14 AC2: the preview is the first 60 characters, cut and wrapped on
// character boundaries only (queue items from the daemon can be Chinese).
static std::string draw_confirm_for(const char* text) {
  Rig r;
  set_ready(r.port.app);
  ConfirmSpec q;
  q.kind = ConfirmKind::Spawn;
  q.slot = 1;
  q.qid = some(Qid::from("q17"));
  q.text = UserText::from(text);
  r.shell.confirm(q);
  FakeCanvas c;
  r.shell.render(c, 1000);
  std::string preview;
  for (const TextOp& t : c.texts) {
    TEST_ASSERT_TRUE_MESSAGE(proto::utf8_valid(t.text.data(), t.text.size()), c.dump().c_str());
    TEST_ASSERT_TRUE_MESSAGE(t.x + c.textWidth(t.text.c_str()) <= kScreenW, t.text.c_str());
    if (t.y > kContentRect.y + 2 * 12 && t.y < kContentRect.y + 7 * 12) preview += t.text;
  }
  return preview;
}

static void test_confirm_preview_never_splits_a_character() {
  const char* mixed = "fix Acme Web 登录页面改版的设置页面和字体修复问题";
  TEST_ASSERT_EQUAL_STRING((std::string("\"") + mixed + "\"").c_str(), draw_confirm_for(mixed).c_str());
}

static void test_confirm_preview_is_first_60_characters() {
  std::string cjk;
  for (int i = 0; i < 70; ++i) cjk += (i < 60 ? "录" : "缺");
  std::string first60;
  for (int i = 0; i < 60; ++i) first60 += "录";
  TEST_ASSERT_EQUAL_STRING(("\"" + first60 + "...\"").c_str(), draw_confirm_for(cjk.c_str()).c_str());
  std::string ascii(80, 'a');
  TEST_ASSERT_EQUAL_STRING(("\"" + std::string(60, 'a') + "...\"").c_str(), draw_confirm_for(ascii.c_str()).c_str());
}

static void test_notes_and_queue_items_skip_confirm() {
  Rig r;
  r.shell.on_key(g0tap(), 1000);
  r.shell.on_key(chr('n'), 1000);
  r.shell.on_key(enter(), 1000);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::None);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.notes.size());
  TEST_ASSERT_TRUE(r.port.notes[0].project.has && r.port.notes[0].project.value.equals("cpa"));
  r.shell.on_key(opt('q'), 1000);
  r.shell.on_key(chr('q'), 1000);
  r.shell.on_key(g0tap(), 1000);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.queued.size());
  TEST_ASSERT_TRUE(r.port.queued[0].intent == Intent::Todo);
}

// Review (U25): the uncertain item's box keeps "New session in cpa" on its first row and
// adds the warning on a row of its own, above the quoted text.
static void test_shell_uncertain_spawn_box_keeps_its_title() {
  Rig r;
  set_ready(r.port.app);
  QueueRow unsure = queue_item("q18", 1, "write docs", Intent::Spawn);
  unsure.uncertain = true;
  set_queue(r.port.app, {unsure});
  ConfirmSpec q = spawn_spec();
  q.qid = some(Qid::from("q18"));
  r.shell.confirm(q);
  FakeCanvas c;
  r.shell.render(c, 1000);
  const TextOp* title = c.find("在 cpa 开 session");
  const TextOp* note = c.find("可能已开过，先看 Sessions");
  TEST_ASSERT_NOT_NULL_MESSAGE(title, c.dump().c_str());
  TEST_ASSERT_NOT_NULL_MESSAGE(note, c.dump().c_str());
  TEST_ASSERT_TRUE(note->y > title->y && note->style.fg == Color::Warn);
  TEST_ASSERT_EQUAL_STRING("Enter 开始 ` 取消", c.line(kContentRect.y + 7 * 12).c_str());  // docs/20 B6: keycaps
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_spawn_needs_ready_and_agent);
  RUN_TEST(test_reply_needs_bg_idle_ready_and_caps);
  RUN_TEST(test_queue_spawn_from_item_is_never_requeued);
  RUN_TEST(test_spawn_from_an_uncertain_item_warns_first);
  RUN_TEST(test_queue_body_carries_intent_project_and_session);
  RUN_TEST(test_shell_offline_spawn_becomes_queue_item);
  RUN_TEST(test_shell_online_spawn_sends_agent_request);
  RUN_TEST(test_shell_link_drop_between_open_and_enter_queues);
  RUN_TEST(test_shell_busy_agent_request_is_queued);
  RUN_TEST(test_shell_reply_to_tty_session_is_queued_with_reason);
  RUN_TEST(test_shell_reply_to_idle_bg_session_executes);
  RUN_TEST(test_notes_and_queue_items_skip_confirm);
  RUN_TEST(test_confirm_preview_never_splits_a_character);
  RUN_TEST(test_confirm_preview_is_first_60_characters);
  RUN_TEST(test_shell_uncertain_spawn_box_keeps_its_title);
  return UNITY_END();
}
