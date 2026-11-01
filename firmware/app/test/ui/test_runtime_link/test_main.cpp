// The runtime against a fake daemon (05 §5.1, §5.8, §5.9, §6.3-§6.4): handshake and
// replies, snapshot delivery, the outbox round trip, <= 4 inbound lines per loop,
// online requests, the projects cache, render pacing and stats sampling.
#include <unity.h>

#include "runtime.h"
#include "support/fixtures.h"
#include "support/runtime_rig.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

static void test_handshake_reply_and_host_app() {
  RuntimeRig r;
  r.handshake();
  const auto hello = r.tx.of_type("hello");
  TEST_ASSERT_EQUAL_UINT32(1, hello.size());
  const auto& h = std::get<proto::Hello>(hello[0].msg.body);
  TEST_ASSERT_FALSE(h.from_host);
  TEST_ASSERT_TRUE(h.device.fw.equals("cpa-fw/0.5.1"));
  TEST_ASSERT_TRUE(h.device.dev.equals("020000000001"));
  TEST_ASSERT_TRUE(h.device.boot == mono_boot_id(mono_start(test_ticket())));
  TEST_ASSERT_TRUE(h.device.rst.equals("poweron"));
  TEST_ASSERT_TRUE(hello[0].msg.re.value.equals("h1"));
  TEST_ASSERT_TRUE(r.rt.state().link.mode == LinkMode::Ready);
  TEST_ASSERT_TRUE(r.rt.sys().host_app.equals("cpad/0.1.0"));
  TEST_ASSERT_EQUAL_UINT32(1, r.tx.count("ack"));  // the projects snapshot
  TEST_ASSERT_TRUE(r.tx.all_valid());
}

// docs/21 §4.2: the settings page names the Mac from the last hello's host_label; a hello
// without one leaves no old name behind.
static void test_handshake_keeps_the_macs_name() {
  RuntimeRig r;
  TEST_ASSERT_TRUE(r.rt.sys().host_label.empty());
  r.send("hello", "\"role\":\"host\",\"app\":\"cpad/0.4.1\",\"agent\":\"ok\",\"caps\":{\"spawn\":true,"
                  "\"reply\":true},\"host_label\":\"Studio Mac\"");
  r.step();
  TEST_ASSERT_TRUE(r.rt.state().link.mode == LinkMode::Ready);
  TEST_ASSERT_TRUE(r.rt.sys().host_label.equals("Studio Mac"));
  r.handshake();  // the cable to another Mac, whose cpad sends no name
  TEST_ASSERT_TRUE(r.rt.sys().host_label.empty());
}

static void test_not_ready_until_hello() {
  RuntimeRig r;
  r.step();
  TEST_ASSERT_TRUE(r.rt.state().link.mode == LinkMode::Attached);
  r.send("ping");
  r.step();
  const auto err = r.tx.of_type("error");
  TEST_ASSERT_EQUAL_UINT32(1, err.size());
  TEST_ASSERT_TRUE(std::get<proto::ErrorMsg>(err[0].msg.body).code == ErrCode::NotReady);
}

static void test_at_most_four_lines_per_loop() {
  RuntimeRig r;
  r.handshake();
  for (int i = 0; i < 6; ++i) r.send("ping");
  r.step();
  TEST_ASSERT_EQUAL_UINT32(4, r.tx.count("pong"));
  r.step();
  TEST_ASSERT_EQUAL_UINT32(6, r.tx.count("pong"));
  const auto pong = std::get<proto::Pong>(r.tx.of_type("pong").back().msg.body);
  TEST_ASSERT_EQUAL_UINT32(3, pong.revs.projects);
}

static void test_snapshots_reach_the_state_and_need_toasts() {
  RuntimeRig r;
  r.handshake();
  r.send("time.set", "\"epoch_ms\":1792440000000,\"tz_min\":-420");
  r.send("sessions.snapshot",
         "\"rev\":17,\"total\":1,\"sessions\":[{\"sid\":\"8d3e7a19\",\"slot\":1,\"name\":\"架构设计\",\"kind\":\"tty\","
         "\"st\":\"waiting\",\"need\":true,\"age_s\":540}]");
  r.send("queue.snapshot", "\"rev\":9,\"items\":[{\"qid\":\"q17\",\"slot\":1,\"text\":\"add test\",\"intent\":\"todo\"}]");
  r.step();
  const AppState& s = r.rt.state();
  TEST_ASSERT_TRUE(s.time.known);
  TEST_ASSERT_EQUAL_UINT32(17, s.sessions.data.rev);
  TEST_ASSERT_EQUAL_UINT32(9, s.queue.data.rev);
  TEST_ASSERT_EQUAL_UINT32(1 + 3, r.tx.count("ack"));
  r.disp.canvas_.clear();
  r.rt.shell().render(r.disp.canvas_, r.now);
  TEST_ASSERT_TRUE(r.disp.canvas_.has("cpa/架构设计 等你"));
}

// docs/20 U14: a session the device started is called "key/slug" already: its toast does not
// say the key twice.
static void test_need_toast_never_doubles_the_key() {
  RuntimeRig r;
  r.handshake();
  r.send("sessions.snapshot",
         "\"rev\":18,\"total\":1,\"sessions\":[{\"sid\":\"5c2e8a14\",\"slot\":1,\"name\":\"cpa/chord-tests\","
         "\"kind\":\"bg\",\"st\":\"idle\",\"need\":true,\"age_s\":5}]");
  r.step();
  r.disp.canvas_.clear();
  r.rt.shell().render(r.disp.canvas_, r.now);
  TEST_ASSERT_TRUE(r.disp.canvas_.has("cpa/chord-tests 等你"));
  TEST_ASSERT_FALSE(r.disp.canvas_.has("cpa/cpa/"));
}

static void test_note_saved_offline_then_sent_and_acked() {
  RuntimeRig r;
  r.tx.is_plugged = false;
  r.step();
  NoteBody n;
  n.text = UserText::from("idea: dedupe by sid");
  TEST_ASSERT_TRUE(r.rt.save_note(n) == SaveResult::Saved);
  TEST_ASSERT_EQUAL_UINT16(1, r.rt.state().outbox.pending);
  TEST_ASSERT_EQUAL_UINT32(1, r.fs.count_in("/outbox", ".json"));
  r.step();
  TEST_ASSERT_EQUAL_UINT32(0, r.tx.count("outbox.item"));  // nothing is sent until Ready
  r.tx.is_plugged = true;
  r.handshake();
  r.step();
  const auto items = r.tx.of_type("outbox.item");
  TEST_ASSERT_EQUAL_UINT32(1, items.size());
  TEST_ASSERT_TRUE(r.tx.classes.back() == TxClass::Resendable);
  const auto& item = std::get<proto::OutboxItem>(items[0].msg.body);
  TEST_ASSERT_TRUE(item.note.text.equals("idea: dedupe by sid"));
  TEST_ASSERT_FALSE(item.wall_ms.has);
  r.reply("outbox.ack", items[0].msg.id.c_str(), "\"status\":\"stored\"");
  r.step();
  TEST_ASSERT_EQUAL_UINT16(0, r.rt.state().outbox.pending);
  TEST_ASSERT_EQUAL_UINT32(0, r.fs.count_in("/outbox", ".json"));
}

// HIL 2026-10-19: with the cable in, HWCDC::isPlugged() can read false for about one
// tick. Such a dropout (one loop, or a few hundred ms of loops) must not leave Ready:
// otherwise the next host message gets E_NOT_READY and in-flight items are lost.
static void test_short_plugged_dropout_keeps_ready() {
  RuntimeRig r;
  r.handshake();
  NoteBody n;
  n.text = UserText::from("x");
  r.rt.save_note(n);
  r.step();
  TEST_ASSERT_EQUAL_UINT8(1, r.rt.outbox().n_inflight);
  r.tx.is_plugged = false;  // one loop sees the dropout
  r.step();
  r.tx.is_plugged = true;
  r.step();
  TEST_ASSERT_TRUE(r.rt.state().link.mode == LinkMode::Ready);
  r.tx.is_plugged = false;
  for (int i = 0; i < 30; ++i) r.step(10);  // 300 ms of false samples
  r.tx.is_plugged = true;
  r.step();
  TEST_ASSERT_TRUE(r.rt.state().link.mode == LinkMode::Ready);
  TEST_ASSERT_EQUAL_UINT8(1, r.rt.outbox().n_inflight);  // not handed back for a resend
  r.send("ping");
  r.step();
  TEST_ASSERT_EQUAL_UINT32(0, r.tx.count("error"));
  TEST_ASSERT_EQUAL_UINT32(1, r.tx.count("pong"));
}

static void test_rejected_item_goes_to_bad_with_toast() {
  RuntimeRig r;
  r.handshake();
  NoteBody n;
  n.text = UserText::from("x");
  r.rt.save_note(n);
  r.step();
  const auto items = r.tx.of_type("outbox.item");
  r.reply("outbox.ack", items[0].msg.id.c_str(), "\"status\":\"rejected\",\"code\":\"E_BAD_FIELD\"");
  r.step();
  TEST_ASSERT_EQUAL_UINT16(1, r.rt.state().outbox.bad);
  TEST_ASSERT_EQUAL_UINT32(1, r.fs.count_in("/outbox/bad", ".json"));
  r.disp.canvas_.clear();
  r.rt.shell().render(r.disp.canvas_, r.now);
  TEST_ASSERT_TRUE(r.disp.canvas_.has("E_BAD_FIELD"));
}

static void test_time_known_items_carry_wall_ms_and_full_outbox_refuses() {
  RuntimeRig r;
  r.handshake();
  r.send("time.set", "\"epoch_ms\":1792440000000,\"tz_min\":0");
  r.step();
  QueueBody q;
  q.text = UserText::from("later");
  q.project = ProjectKey::from("cpa");
  TEST_ASSERT_TRUE(r.rt.save_queue(q) == SaveResult::Saved);
  r.step();
  const auto& item = std::get<proto::OutboxItem>(r.tx.of_type("outbox.item").back().msg.body);
  TEST_ASSERT_TRUE(item.wall_ms.has);
  TEST_ASSERT_TRUE(item.queue.intent == Intent::Todo);
  RuntimeRig full(false);
  full.fs.set_free(1000);  // under 64 KB
  full.begin();
  TEST_ASSERT_TRUE(full.rt.save_queue(q) == SaveResult::Full);
}

// LittleFS usage is a full metadata walk (537 ms with 200 items on the device): it is
// read once at boot, then estimated as one 4 KB block per item, never polled.
static void test_free_space_is_measured_once_and_estimated() {
  RuntimeRig r(false);
  r.fs.set_free(64 * 1024 + 4096 - 1);  // room for exactly one more item
  r.begin();
  NoteBody n;
  n.text = UserText::from("x");
  TEST_ASSERT_TRUE(r.rt.save_note(n) == SaveResult::Saved);
  r.fs.set_free(10 * 1024 * 1024);  // not looked at again
  for (int i = 0; i < 30; ++i) r.step(1000);
  TEST_ASSERT_TRUE(r.rt.save_note(n) == SaveResult::Full);
  TEST_ASSERT_TRUE(r.rt.state().outbox.full);
  r.handshake();
  r.step();
  for (const auto& item : r.tx.of_type("outbox.item")) r.reply("outbox.ack", item.msg.id.c_str(), "\"status\":\"stored\"");
  r.step();
  TEST_ASSERT_TRUE(r.rt.save_note(n) == SaveResult::Saved);  // the acked item's block counts as free again
}

static void test_projects_cache_survives_a_reboot() {
  RuntimeRig r;
  r.handshake();
  TEST_ASSERT_TRUE(r.fs.has("/cfg/projects.json"));
  TEST_ASSERT_FALSE(r.fs.has("/cfg/projects.tmp"));
  // reboot on the same storage: names come back, counts and rev do not
  FakeTransport tx2;
  FakePlatform plat2;
  FakeDisplay disp2;
  ProbeModule board{"board"};
  cpa::Module* mods[] = {&board};
  Runtime rt2(tx2, r.fs, plat2, disp2, mods, 1);
  BootInfo b;
  b.ticket = test_ticket(13);
  rt2.begin(b, 0);
  const ProjectsView& p = rt2.state().projects;
  TEST_ASSERT_FALSE(p.live);
  TEST_ASSERT_EQUAL_UINT8(1, p.data.n);
  TEST_ASSERT_TRUE(p.data.rows[0].key.equals("cpa"));
  TEST_ASSERT_TRUE(p.data.rows[0].name.equals("Cardputer 助手"));
  TEST_ASSERT_EQUAL_UINT32(0, p.data.rev);
  TEST_ASSERT_EQUAL_UINT16(0, p.data.rows[0].run);
}

static void test_queue_done_request_one_at_a_time() {
  RuntimeRig r;
  OnlineRequest q;
  q.kind = RequestKind::QueueDone;
  q.qid = Qid::from("q17");
  TEST_ASSERT_FALSE(r.rt.send_request(q));  // not Ready
  r.handshake();
  TEST_ASSERT_TRUE(r.rt.send_request(q));
  OnlineRequest d;
  d.kind = RequestKind::InboxDismiss;
  d.sid = Sid::from("8d3e7a19");
  TEST_ASSERT_FALSE(r.rt.send_request(d));  // one in flight
  const std::string id = r.last_id_of("queue.done");
  r.reply("error", id, "\"code\":\"E_UNKNOWN_ITEM\"");
  r.step();
  TEST_ASSERT_TRUE(r.rt.send_request(d));
  TEST_ASSERT_EQUAL_UINT32(1, r.tx.count("inbox.dismiss"));
  r.disp.canvas_.clear();
  r.rt.shell().render(r.disp.canvas_, r.now);
  TEST_ASSERT_TRUE(r.disp.canvas_.has("E_UNKNOWN_ITEM"));
  r.step(kSmallReqTimeoutMs);  // no answer: the slot frees up
  TEST_ASSERT_TRUE(r.rt.send_request(q));
}

static void test_render_is_paced_and_refreshed() {
  RuntimeRig r;
  r.step(1);
  const int first = r.disp.frames;
  TEST_ASSERT_EQUAL_INT(1, first);
  r.rt.shell().mark_dirty();
  r.step(10);  // < 50 ms since the last frame
  TEST_ASSERT_EQUAL_INT(1, r.disp.frames);
  r.step(40);
  TEST_ASSERT_EQUAL_INT(2, r.disp.frames);
  r.step(500);  // clean: no frame
  TEST_ASSERT_EQUAL_INT(2, r.disp.frames);
  r.step(600);  // a second after the last frame: clock refresh
  TEST_ASSERT_EQUAL_INT(3, r.disp.frames);
}

static void test_stats_are_sampled_once_a_second() {
  RuntimeRig r;
  r.plat.next.heap_free = 180000;
  r.plat.next.stack_hwm = 6000;
  r.plat.next.bat_mv = 4100;
  r.step(1000);
  TEST_ASSERT_EQUAL_UINT32(180000, r.rt.state().device.heap_free);
  TEST_ASSERT_EQUAL_UINT32(6000, r.rt.state().device.stack_hwm);
  TEST_ASSERT_EQUAL_UINT16(4100, r.rt.state().device.bat_mv);
  r.plat.next.heap_free = 1;
  r.step(10);
  TEST_ASSERT_EQUAL_UINT32(180000, r.rt.state().device.heap_free);
}

static void test_failed_nvs_means_no_ids_and_failed_saves() {
  RuntimeRig r(false);
  BootInfo b;  // invalid ticket
  r.rt.begin(b, 0);
  TEST_ASSERT_FALSE(r.rt.sys().ids_ok);
  NoteBody n;
  n.text = UserText::from("x");
  TEST_ASSERT_TRUE(r.rt.save_note(n) == SaveResult::Failed);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_handshake_reply_and_host_app);
  RUN_TEST(test_handshake_keeps_the_macs_name);
  RUN_TEST(test_not_ready_until_hello);
  RUN_TEST(test_at_most_four_lines_per_loop);
  RUN_TEST(test_snapshots_reach_the_state_and_need_toasts);
  RUN_TEST(test_need_toast_never_doubles_the_key);
  RUN_TEST(test_note_saved_offline_then_sent_and_acked);
  RUN_TEST(test_short_plugged_dropout_keeps_ready);
  RUN_TEST(test_rejected_item_goes_to_bad_with_toast);
  RUN_TEST(test_time_known_items_carry_wall_ms_and_full_outbox_refuses);
  RUN_TEST(test_free_space_is_measured_once_and_estimated);
  RUN_TEST(test_projects_cache_survives_a_reboot);
  RUN_TEST(test_queue_done_request_one_at_a_time);
  RUN_TEST(test_render_is_paced_and_refreshed);
  RUN_TEST(test_stats_are_sampled_once_a_second);
  RUN_TEST(test_failed_nvs_means_no_ids_and_failed_saves);
  return UNITY_END();
}
