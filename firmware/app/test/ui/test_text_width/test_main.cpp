// One row is 240 px (docs/20 U7, A8): every confirm title fits it, and every fixed toast is
// whole in the theme's toast capsule, which leaves its text 224 px (docs/20 B6), so nothing
// the user must read is cut (FakeCanvas: ASCII 6 px, everything else 12 px, the widest
// efontCN_12 can be). The confirm titles say plainly what happens: a reply that cannot be
// delivered is only stored on the Mac ("只存到 Mac 队列"), never "v0.4" or "正在运行" for a
// session that waits.
#include <unity.h>

#include <string>

#include "compose.h"
#include "confirm.h"
#include "rec_overlay.h"
#include "rec_result.h"
#include "support/fake_canvas.h"
#include "support/fixtures.h"
#include "theme.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

static void fits(const std::string& s) {
  FakeCanvas c;
  TEST_ASSERT_TRUE_MESSAGE(c.textWidth(s.c_str()) <= kScreenW, s.c_str());
}

// Drawn by the theme's toast bar, not cut ("…").
static void toast_fits(const std::string& s) {
  FakeCanvas c;
  TEST_ASSERT_TRUE_MESSAGE(c.textWidth(s.c_str()) <= theme::kToastTextW, s.c_str());
  theme::toast_bar(c, s.c_str(), theme::Tone::Warn);
  TEST_ASSERT_TRUE_MESSAGE(c.has(s), s.c_str());
}

static const QueueReason kReasons[] = {QueueReason::Offline,     QueueReason::NoAgent, QueueReason::Interactive,
                                       QueueReason::NotIdle,     QueueReason::NoReply, QueueReason::SessionGone,
                                       QueueReason::Waiting};

static void confirm_titles_fit() {
  for (QueueReason r : kReasons) {
    for (Route route : {Route::Queue, Route::Unavailable}) {
      for (ConfirmKind k : {ConfirmKind::Spawn, ConfirmKind::Feedback}) fits(confirm_title(k, Decision{route, r}, "").c_str());
    }
  }
  // The toasts after Enter (shell.cpp accept_confirm, execute): a new session is queued or
  // stays a queue item only offline or without Claude Code (confirm_decide), or for want of
  // a project or with a request in flight; a reply's toast is always the same.
  for (QueueReason r : {QueueReason::Offline, QueueReason::NoAgent}) {
    toast_fits(confirm_queued_text(ConfirmKind::Spawn, queue_reason_text(r)).c_str());
    toast_fits(confirm_unrun_text(r).c_str());
  }
  const bool en = lang() == Lang::En;  // shell.cpp's own reasons (kUnknownProject, kBusy)
  for (const char* why : {en ? "Unknown project" : "未知项目", en ? "Request pending" : "已有请求在途"}) {
    toast_fits(confirm_queued_text(ConfirmKind::Spawn, why).c_str());
  }
  toast_fits(confirm_queued_text(ConfirmKind::Feedback, "").c_str());
  fits(confirm_title(ConfirmKind::Spawn, Decision{Route::Execute, QueueReason::None}, "abcdefghijkl").c_str());
  fits(confirm_title(ConfirmKind::Spawn, Decision{Route::Execute, QueueReason::None, true}, "abcdefghijkl").c_str());
}

static void fixed_toasts_fit() {
  for (ComposeResult r : {ComposeResult::EmptyText, ComposeResult::NeedProject, ComposeResult::DashText,
                          ComposeResult::Full}) {
    toast_fits(compose_result_text(r));
  }
  for (uint8_t w = 0; w <= static_cast<uint8_t>(rec::RecRefusal::Timeout); ++w) {
    toast_fits(rec_refusal_text(static_cast<rec::RecRefusal>(w)));
  }
  rec::RecEffect e;
  e.kind = rec::RecFx::Ended;
  e.ms = 10800000;
  for (RecMode m : {RecMode::Memo, RecMode::Meeting}) {
    e.mode = m;
    for (uint8_t w = 0; w <= static_cast<uint8_t>(rec::RecRefusal::Timeout); ++w) {
      e.why = static_cast<rec::RecRefusal>(w);
      e.saved = true;
      toast_fits(rec_end_text(e).c_str());
      e.saved = false;
      toast_fits(rec_end_text(e).c_str());
    }
  }
  toast_fits(rec_bat_warn_text());
  AppState s;
  set_projects(s, {});
  toast_fits(need_project_text(s.projects).c_str());
  s.projects.live = false;
  toast_fits(need_project_text(s.projects).c_str());
  s = demo_state();
  toast_fits(no_slot_text(s.projects, 9).c_str());
}

// docs/21 §9.2: both languages, the widest arguments.
static void test_every_confirm_title_fits_one_row() {
  for (const Lang l : kLangs) {
    LangScope in(l);
    confirm_titles_fit();
  }
}

static void test_fixed_toasts_fit_the_toast_bar() {
  for (const Lang l : kLangs) {
    LangScope in(l);
    fixed_toasts_fit();
  }
}

// docs/21: the English need-project toast counts the rest too.
static void test_need_project_in_english() {
  LangScope en(Lang::En);
  AppState s = demo_state();
  set_projects(s, {project(1, "cardputer-as", "a"), project(2, "acmeshop-api", "b"), project(3, "webhooks-svc", "c"),
                   project(4, "landing-page", "d"), project(5, "mobile-app-x", "e")});
  const std::string five = need_project_text(s.projects).c_str();
  toast_fits(five);
  TEST_ASSERT_TRUE_MESSAGE(five.find("Opt+digit: 1 cardputer-as") == 0, five.c_str());
  TEST_ASSERT_TRUE_MESSAGE(five.find("5 in all") != std::string::npos, five.c_str());
  TEST_ASSERT_EQUAL_STRING("No project 9 (only 5)", no_slot_text(s.projects, 9).c_str());
}

// Review: the "pick a project" hint names the key (in the editor a plain digit is typed),
// and a list too long for the row ends with how many there are instead of being cut at
// the screen's edge.
static void test_need_project_names_the_key_and_fits() {
  AppState s = demo_state();  // cpa, blog
  const std::string two = need_project_text(s.projects).c_str();
  TEST_ASSERT_TRUE_MESSAGE(two.find("Opt+") == 0, two.c_str());
  TEST_ASSERT_TRUE_MESSAGE(two.find("1 cpa · 2 blog") != std::string::npos, two.c_str());
  toast_fits(two);
  set_projects(s, {project(1, "cardputer-as", "a"), project(2, "acmeshop-api", "b"), project(3, "webhooks-svc", "c"),
                   project(4, "landing-page", "d"), project(5, "mobile-app-x", "e"), project(6, "infra-deploy", "f"),
                   project(7, "data-pipelin", "g"), project(8, "docs-website", "h"), project(9, "design-syste", "i")});
  const std::string nine = need_project_text(s.projects).c_str();
  toast_fits(nine);
  TEST_ASSERT_TRUE_MESSAGE(nine.find("Opt+") == 0, nine.c_str());
  TEST_ASSERT_TRUE_MESSAGE(nine.find("1 cardputer-as") != std::string::npos, nine.c_str());
  TEST_ASSERT_TRUE_MESSAGE(nine.find("共 9 个") != std::string::npos, nine.c_str());
}

static void test_reply_titles_say_what_happens() {
  AppState s = demo_state();  // 8d3e7a19 tty waiting, 5c2e8a14 bg idle
  set_ready(s, true, true);
  ConfirmSpec r;
  r.kind = ConfirmKind::Feedback;
  r.slot = 1;
  r.sid = some(Sid::from("5c2e8a14"));
  s.sessions.data.rows[1].st = SessionState::Waiting;  // a background session waiting for you
  const Decision d = confirm_decide(r, s);
  TEST_ASSERT_TRUE(d.route == Route::Queue && d.reason == QueueReason::Waiting);
  const std::string title = confirm_title(ConfirmKind::Feedback, d, "").c_str();
  TEST_ASSERT_TRUE_MESSAGE(title.find("正在运行") == std::string::npos, title.c_str());
  TEST_ASSERT_TRUE_MESSAGE(title.find("只存到 Mac 队列") != std::string::npos, title.c_str());
  for (QueueReason q : kReasons) {
    TEST_ASSERT_TRUE(std::string(queue_reason_text(q)).find("v0.4") == std::string::npos);
  }
  const std::string toast = confirm_queued_text(ConfirmKind::Feedback, "").c_str();
  TEST_ASSERT_TRUE_MESSAGE(toast.find("不会发给 session") != std::string::npos, toast.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_confirm_title_fits_one_row);
  RUN_TEST(test_fixed_toasts_fit_the_toast_bar);
  RUN_TEST(test_reply_titles_say_what_happens);
  RUN_TEST(test_need_project_names_the_key_and_fits);
  RUN_TEST(test_need_project_in_english);
  return UNITY_END();
}
