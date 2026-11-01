// AppState slice reducers (05 §5.2, §5.3, §5.6): values in, new values out.
#include <unity.h>

#include <initializer_list>

#include "app_state.h"
#include "reducers.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

static ProjectRow project(uint8_t slot, const char* key, const char* name, uint16_t need) {
  ProjectRow r;
  r.slot = slot;
  r.key = ProjectKey::from(key);
  r.name = ProjectName::from(name);
  r.need = need;
  r.ok = true;
  return r;
}

static SessionRow session(const char* sid, uint8_t slot, SessionState st, bool need, uint32_t age_s) {
  SessionRow r;
  r.sid = Sid::from(sid);
  r.slot = slot;
  r.name = SessionName::from("架构设计");
  r.kind = SessionKind::Tty;
  r.st = st;
  r.need = need;
  r.age_s = age_s;
  return r;
}

static SessionsData sessions_of(uint32_t rev, std::initializer_list<SessionRow> rows) {
  SessionsData d;
  d.rev = rev;
  for (const auto& r : rows) d.rows[d.n++] = r;
  d.total = d.n;
  return d;
}

static void test_app_state_fits_budget() {
  TEST_ASSERT_TRUE(sizeof(AppState) <= 4096);
  TEST_ASSERT_TRUE(sizeof(ProjectsView) <= 1600);
  TEST_ASSERT_TRUE(sizeof(SessionsView) <= 1600);
  TEST_ASSERT_TRUE(sizeof(QueueView) <= 1600);
}

static void test_projects_replace_and_report_identity_changes() {
  ProjectsData d;
  d.rev = 42;
  d.n = 2;
  d.rows[0] = project(1, "cpa", "Cardputer 助手", 1);
  d.rows[1] = project(2, "blog", "博客", 0);
  const ProjectsView empty{};
  const auto s1 = reduce_projects(empty, d, 1000);
  TEST_ASSERT_TRUE(s1.state.live);
  TEST_ASSERT_EQUAL_UINT32(42, s1.state.data.rev);
  TEST_ASSERT_EQUAL_UINT32(1000, s1.state.rx_ms);
  TEST_ASSERT_EQUAL_UINT8(2, s1.state.data.n);
  TEST_ASSERT_EQUAL_UINT8(1, s1.effects.count);
  TEST_ASSERT_TRUE(s1.effects.items[0] == ProjectsEvent::IdentityChanged);
  TEST_ASSERT_FALSE(empty.live);  // input untouched

  // counts change, identity does not: no cache write
  ProjectsData d2 = d;
  d2.rev = 43;
  d2.rows[0].need = 0;
  const auto s2 = reduce_projects(s1.state, d2, 2000);
  TEST_ASSERT_EQUAL_UINT8(0, s2.effects.count);
  TEST_ASSERT_EQUAL_UINT16(0, s2.state.data.rows[0].need);

  ProjectsData d3 = d2;
  d3.rows[1].name = ProjectName::from("Blog");
  TEST_ASSERT_EQUAL_UINT8(1, reduce_projects(s2.state, d3, 3000).effects.count);
  const auto slot_of = project_by_slot(s2.state, 2);
  TEST_ASSERT_TRUE(slot_of.has);
  TEST_ASSERT_TRUE(slot_of.value.key.equals("blog"));
  TEST_ASSERT_FALSE(project_by_slot(s2.state, 3).has);
}

static void test_sessions_toast_only_when_need_becomes_true() {
  const auto a = session("8d3e7a19", 1, SessionState::Waiting, true, 540);
  const auto b = session("5c2e8a14", 1, SessionState::Busy, false, 75);
  const auto s1 = reduce_sessions(SessionsView{}, sessions_of(17, {a, b}), 0);
  TEST_ASSERT_EQUAL_UINT8(1, s1.effects.count);
  TEST_ASSERT_EQUAL_UINT8(0, s1.effects.items[0].index);
  TEST_ASSERT_EQUAL_UINT16(1, need_count(s1.state));

  // same need again (re-push after a reconnect): no second toast
  const auto s2 = reduce_sessions(s1.state, sessions_of(17, {a, b}), 5000);
  TEST_ASSERT_EQUAL_UINT8(0, s2.effects.count);

  // b now needs the user too; a goes away
  auto b_need = b;
  b_need.st = SessionState::Idle;
  b_need.need = true;
  const auto s3 = reduce_sessions(s2.state, sessions_of(18, {b_need}), 6000);
  TEST_ASSERT_EQUAL_UINT8(1, s3.effects.count);
  TEST_ASSERT_TRUE(s3.state.data.rows[s3.effects.items[0].index].sid.equals("5c2e8a14"));

  // need -> false -> true again toasts again
  const auto s4 = reduce_sessions(s3.state, sessions_of(19, {b}), 7000);
  const auto s5 = reduce_sessions(s4.state, sessions_of(20, {b_need}), 8000);
  TEST_ASSERT_EQUAL_UINT8(0, s4.effects.count);
  TEST_ASSERT_EQUAL_UINT8(1, s5.effects.count);
}

static void test_session_age_grows_locally() {
  const auto s = reduce_sessions(SessionsView{}, sessions_of(17, {session("8d3e7a19", 1, SessionState::Idle, false, 75)}), 10000);
  TEST_ASSERT_EQUAL_UINT32(75, session_age_s(s.state, 0, 10000));
  TEST_ASSERT_EQUAL_UINT32(75, session_age_s(s.state, 0, 10999));
  TEST_ASSERT_EQUAL_UINT32(76, session_age_s(s.state, 0, 11000));
  TEST_ASSERT_EQUAL_UINT32(135, session_age_s(s.state, 0, 70000));
  TEST_ASSERT_EQUAL_UINT32(0, session_age_s(s.state, 5, 70000));  // out of range
  TEST_ASSERT_EQUAL_UINT32(17, s.state.data.rev);                 // age never bumps rev
}

static void test_toasts_are_capped_by_effects_capacity() {
  SessionsData d;
  d.rev = 1;
  const char* sids[] = {"00000000", "00000001", "00000002", "00000003", "00000004", "00000005"};
  for (const char* sid : sids) d.rows[d.n++] = session(sid, 1, SessionState::Waiting, true, 1);
  const auto s = reduce_sessions(SessionsView{}, d, 0);
  TEST_ASSERT_EQUAL_UINT8(4, s.effects.count);
  TEST_ASSERT_TRUE(s.effects.overflow);
  TEST_ASSERT_EQUAL_UINT16(6, need_count(s.state));
}

static void test_queue_replaces_view() {
  QueueData d;
  d.rev = 9;
  d.n = 1;
  d.rows[0].qid = Qid::from("q17");
  d.rows[0].slot = 1;
  d.rows[0].text = QueueText::from("add LittleFS fault-injection test");
  const auto v = reduce_queue(QueueView{}, d, 1234);
  TEST_ASSERT_TRUE(v.live);
  TEST_ASSERT_EQUAL_UINT32(9, v.data.rev);
  TEST_ASSERT_EQUAL_UINT32(1234, v.rx_ms);
  TEST_ASSERT_TRUE(v.data.rows[0].qid.equals("q17"));
  TEST_ASSERT_EQUAL_UINT8(1, queue_count_for_slot(v, 1));
  TEST_ASSERT_EQUAL_UINT8(0, queue_count_for_slot(v, 2));
}

static void test_time_and_wall_clock() {
  const TimeInfo unknown{};
  TEST_ASSERT_FALSE(wall_ms(unknown, 5000).has);
  TimeData t;
  t.epoch_ms = 1792440000000LL;
  t.tz_min = -420;
  const auto known = reduce_time(unknown, t, 5000);
  TEST_ASSERT_TRUE(known.known);
  TEST_ASSERT_EQUAL_INT16(-420, known.tz_min);
  const auto w = wall_ms(known, 6000);
  TEST_ASSERT_TRUE(w.has);
  TEST_ASSERT_TRUE(w.value == 1792440001000LL);
}

static void test_revs_follow_the_views() {
  AppState s{};
  s.projects.data.rev = 42;
  s.sessions.data.rev = 17;
  s.queue.data.rev = 9;
  const Revs r = revs_of(s);
  TEST_ASSERT_EQUAL_UINT32(42, r.projects);
  TEST_ASSERT_EQUAL_UINT32(17, r.sessions);
  TEST_ASSERT_EQUAL_UINT32(9, r.queue);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_app_state_fits_budget);
  RUN_TEST(test_projects_replace_and_report_identity_changes);
  RUN_TEST(test_sessions_toast_only_when_need_becomes_true);
  RUN_TEST(test_session_age_grows_locally);
  RUN_TEST(test_toasts_are_capped_by_effects_capacity);
  RUN_TEST(test_queue_replaces_view);
  RUN_TEST(test_time_and_wall_clock);
  RUN_TEST(test_revs_follow_the_views);
  return UNITY_END();
}
