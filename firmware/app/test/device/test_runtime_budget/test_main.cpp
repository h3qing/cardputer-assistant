// On-device budgets (06 WP4, G6, 05 §5.13):
//   - 8 KB RX burst while the loop does not read for 1 s: 0 bytes lost (RX 8192 set
//     before Serial.begin; the host sends it on CPA_RX_READY);
//   - the whole firmware (M5Cardputer, 8 bpp sprite + efontCN_12, runtime with the
//     modules of src/module_list.cpp) driven through its deepest paths with maximal
//     snapshots in Chinese, with the SD card mounted as the primary store
//     (docs/09-m2-design.md §5, §9.2; needs the
//     FAT32 card inserted) and one rejected outbox.item moved to bad/ on the card
//     (move_file: read, commit, remove): loop stack never used >= 4 KB, free heap
//     >= 150 KB, largest block >= 64 KB.
// v0.3 (docs/11 §4.7, G-A0): the audio task runs with its buffers allocated at boot (the
// chosen mode), scanning the test root, so the heap numbers include its ~38 KB.
// v0.4.0 (docs/13 §5.6, §6.3, §7.1-§7.4, §19): the home launcher is registered as in
// src/module_list.cpp, and every new screen is drawn with its real keys -- home, the three
// Tab menu pages, the lock card, the meeting confirm -- plus the locked ● lines (a meeting
// and a memo) from a copy of the state, since nothing records here; missing_glyphs must
// stay 0 (a new character outside efontCN_12 would be drawn as □).
// v0.5 (docs/19 §10): the runtime's longest full redraw and animation tick (the Tab sheet's
// slide over home and over the meeting consent card) and a full meeting page are timed and
// printed on the CPA_BUDGET line; each stays under 50 ms. gh #53: so is a full memo page with
// its legend at its fullest (nine 12-character keys: three rows, the waveform lowered), and
// one of its animation ticks (the waveform, in the room the legend leaves it).
// docs/21: the same screens, pages and ticks again in English (set_lang only: NVS is not
// written), with the redraw times started afresh and printed on CPA_BUDGET_EN; each < 50 ms.
// The SD store uses the test root /cpabudget, removed at the start and the end, so the
// real /cpa outbox is never touched; cpafs is not mounted (the firmware unmounts it once
// the card is primary). Items the workload saves use a test epoch.
#include <Arduino.h>
#include <M5Cardputer.h>
#include <dirent.h>
#include <unity.h>

#include <esp_timer.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "audio_task.h"
#include "board_module.h"
#include "i2c_lock.h"
#include "usb_transport.h"  // listed here so the dependency finder sees it
#include "device_harness.h"
#include "hal_platform.h"
#include "home_module.h"
#include "inbox_module.h"
#include "lock_screen.h"
#include "m5_display.h"
#include "outbox_io.h"
#include "protocol_limits.h"
#include "rec_overlay.h"
#include "lang.h"
#include "runtime.h"
#include "sd_card.h"
#include "sd_store.h"
#include "shell_hints.h"
#include "status_bar.h"
#include "system_module.h"
#include "theme_levels.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

namespace {

constexpr size_t kBurst = 8192;
constexpr char kRoot[] = "/cpabudget";
constexpr char kVfsRoot[] = "/sd/cpabudget";
constexpr uint32_t kTestEpoch = 0x7E571234u;

// The snapshots' Chinese text, each exactly at its protocol limit in UTF-8 bytes: one byte
// more and the device rejects the whole snapshot, and the budget is measured without it.
constexpr char kProjectName[] = "设置页面字体修复";
constexpr char kSessionName[] = "M5Stack 登录页面改版设计";
constexpr char kQueueText[] = "写一个很长的排队项文本用来测试截断和渲染路径是否正常OK";
static_assert(sizeof kProjectName - 1 == cpa::limits::kProjectNameMax, "project name: exactly project_name_max");
static_assert(sizeof kSessionName - 1 == cpa::limits::kSessionNameMax, "session name: exactly session_name_max");
static_assert(sizeof kQueueText - 1 == cpa::limits::kQueueTextMax, "queue text: exactly queue_text_max");

// The daemon side of the workload: lines queued by the test, everything sent dropped.
class ScriptTransport : public cpa::Transport {
 public:
  std::string rx;
  uint32_t sent = 0;
  bool plugged() const override { return true; }
  size_t read(char* buf, size_t cap) override {
    const size_t n = rx.size() < cap ? rx.size() : cap;
    rx.copy(buf, n);
    rx.erase(0, n);
    return n;
  }
  bool send(const char*, size_t, cpa::TxClass) override {
    ++sent;
    return true;
  }
  void tick(uint32_t) override {}
};

cpa::UsbTransport g_usb;
cpa::VfsFatPort g_sd_port;
cpa::SdStore g_fs(g_sd_port, kRoot);
cpa::M5Display g_display;
cpa::HalPlatform g_platform(g_display);
ScriptTransport g_script;
cpa::HomeModule g_home;
cpa::BoardModule g_board;
cpa::InboxModule g_inbox;
cpa::SystemModule g_system;
cpa::Module* const g_mods[] = {&g_home, &g_board, &g_inbox, &g_system};  // as src/module_list.cpp
cpa::Runtime g_rt(g_script, g_fs, g_platform, g_display, g_mods, sizeof(g_mods) / sizeof(g_mods[0]));
cpa::AudioTask g_audio;
uint32_t g_now = 0;
int g_host_id = 1;

void host(const std::string& t, const std::string& fields = "") {
  g_script.rx += "{\"v\":0,\"t\":\"" + t + "\",\"id\":\"h" + std::to_string(g_host_id++) + "\"" +
                 (fields.empty() ? "" : "," + fields) + "}\n";
}

void run(int passes) {
  for (int i = 0; i < passes; ++i) {
    g_now += 10;
    g_rt.loop(g_now, cpa::KeySet{});
  }
}

void key(const char* names, int hold = 30) {
  host("debug.key", std::string("\"keys\":[") + names + "],\"hold_ms\":" + std::to_string(hold));
  run(12);
}

std::string projects_json() {
  std::string s = "\"rev\":7,\"projects\":[";
  for (int i = 1; i <= 9; ++i) {
    s += std::string(i > 1 ? "," : "") + "{\"slot\":" + std::to_string(i) + ",\"key\":\"proj" + std::to_string(i) +
         "\",\"name\":\"" + kProjectName + "\",\"run\":3,\"need\":2,\"queued\":4,\"ok\":true}";
  }
  return s + "],\"other\":{\"run\":1,\"n\":2}";
}

std::string sessions_json() {
  std::string s = "\"rev\":9,\"total\":14,\"sessions\":[";
  const char* st[] = {"waiting", "idle", "busy", "shell", "unknown"};
  for (int i = 0; i < 10; ++i) {
    char sid[9];
    std::snprintf(sid, sizeof sid, "%08x", 0xabc00000 + i);
    s += std::string(i ? "," : "") + "{\"sid\":\"" + sid + "\",\"slot\":" + std::to_string(i % 9 + 1) +
         ",\"name\":\"" + kSessionName + "\",\"kind\":\"" + (i % 2 ? "bg" : "tty") + "\",\"st\":\"" + st[i % 5] +
         "\",\"need\":" + (i % 5 < 2 ? "true" : "false") + ",\"age_s\":" + std::to_string(60 * i) +
         (i % 2 ? ",\"short\":\"a1b2c3d4\"" : "") + "}";
  }
  return s + "]";
}

std::string queue_json() {
  std::string s = "\"rev\":5,\"items\":[";
  for (int i = 0; i < 12; ++i) {
    s += std::string(i ? "," : "") + "{\"qid\":\"q" + std::to_string(100 + i) + "\",\"slot\":" + std::to_string(i / 4 + 1) +
         ",\"text\":\"" + kQueueText + "\",\"intent\":\"spawn\"}";
  }
  return s + "]";
}

// The workload's screens, through their keys (again in English).
void screens() {
  const char* keys[] = {"\"opt\",\"i\"", "\"opt\",\"m\"", "\"opt\",\"1\"", "\"/\"", "\"opt\",\"h\""};
  for (const char* s : keys) {
    key(s);
    run(10);
  }
}

void workload() {
  host("hello", "\"role\":\"host\",\"app\":\"cpad/0.1.0\",\"agent\":\"ok\",\"caps\":{\"spawn\":true,\"reply\":true}");
  host("time.set", "\"epoch_ms\":1792440000000,\"tz_min\":-420");
  host("projects.snapshot", projects_json());
  host("sessions.snapshot", sessions_json());
  host("queue.snapshot", queue_json());
  run(10);
  key("\"g0\"");  // quick note: type, then submit with Enter (encode + persist chain)
  key("\"h\"");
  key("\"i\"");
  key("\"enter\"");
  key("\"opt\",\"1\"");  // a project page first: since v0.4.0 the root is home, which has no project
  key("\"opt\",\"s\"");  // spawn: compose, confirm, agent.spawn, failure -> queue item
  key("\"x\"");
  key("\"enter\"");
  run(45);
  key("\"enter\"");
  host("agent.result", "\"re\":\"d5\",\"ok\":false,\"code\":\"E_SPAWN\"");  // id may not match: harmless
  host("ping");
  host("debug.state");
  run(20);
  screens();
  run(200);
}

// The locked screen's ● line while recording (docs/13 §7.2): "●42:10 ★2" for a meeting, with
// the card lit too, and "● REC" for a memo. Drawn from a copy of the state with a live
// recording; the copy is static, so its 3.5 KB are not on the loop stack.
void draw_locked_recording() {
  static cpa::AppState s;
  s = g_rt.state();
  s.rec.phase = cpa::RecPhase::Recording;
  s.rec.mode = cpa::RecMode::Meeting;
  s.rec.started_ms = g_now - 2530000u;  // 42:10
  s.rec.stars = 2;
  cpa::render_lock_screen(g_display, s, false, g_now);
  cpa::render_lock_screen(g_display, s, true, g_now);
  s.rec.mode = cpa::RecMode::Memo;
  cpa::render_lock_screen(g_display, s, false, g_now);
  g_display.present();
}

// v0.5 (docs/19 §10): the meeting page at its fullest (a merged timeline, a dozen stars,
// the status bar and the hint), drawn from a copy of the state as draw_locked_recording does,
// and timed with what it takes to push: a full redraw of the heaviest recording page.
uint32_t time_meeting_page(const char* hint) {
  static cpa::AppState s;
  static cpa::theme::LevelHistory h;
  s = g_rt.state();
  s.rec.phase = cpa::RecPhase::Recording;
  s.rec.mode = cpa::RecMode::Meeting;
  s.rec.started_ms = g_now - 2530000u;  // 42:10
  s.rec.stars = 12;
  h = cpa::theme::LevelHistory{};
  for (uint32_t i = 0; i < 300; ++i) {
    h = cpa::theme::level_push(h, static_cast<uint16_t>((i * 997) % 8192), static_cast<uint8_t>(i / 25));
  }
  const uint32_t t0 = static_cast<uint32_t>(esp_timer_get_time());
  cpa::Canvas& c = g_display.canvas();
  c.fill(cpa::Rect{0, 0, cpa::kScreenW, cpa::kScreenH}, cpa::Color::Bg);
  cpa::render_status_bar(c, s, g_now);
  const cpa::RecLook look{&h, cpa::theme::MandoView{}};
  cpa::render_meeting_page(c, s, look, g_now);
  cpa::render_hint_bar(c, hint);
  g_display.present();
  return static_cast<uint32_t>(esp_timer_get_time()) - t0;
}

// gh #53: the memo page with nine 12-character keys (the key pattern's longest), the last
// one chosen, a waveform of 40 blocks, the status bar and the hint; then a tick's waveform.
struct MemoTimes {
  uint32_t page_us;
  uint32_t tick_us;
};

MemoTimes time_memo_page(const char* hint) {
  static cpa::AppState s;
  static cpa::theme::LevelHistory h;
  s = g_rt.state();
  for (uint8_t i = 0; i < s.projects.data.n; ++i) {
    char key[24];  // "project-0001": 12 characters
    std::snprintf(key, sizeof key, "project-%04u", static_cast<unsigned>(i + 1));
    s.projects.data.rows[i].key = cpa::ProjectKey::from(key);
  }
  s.rec.phase = cpa::RecPhase::Recording;
  s.rec.mode = cpa::RecMode::Memo;
  s.rec.started_ms = g_now - 12000u;
  s.rec.slot = 9;
  h = cpa::theme::LevelHistory{};
  for (uint32_t i = 0; i < 40; ++i) h = cpa::theme::level_push(h, static_cast<uint16_t>((i * 997) % 4096), 0);
  cpa::Canvas& c = g_display.canvas();
  const uint32_t t0 = static_cast<uint32_t>(esp_timer_get_time());
  c.fill(cpa::Rect{0, 0, cpa::kScreenW, cpa::kScreenH}, cpa::Color::Bg);
  cpa::render_status_bar(c, s, g_now);
  const cpa::RecLook look{&h, cpa::theme::MandoView{}};
  cpa::render_memo_page(c, s, look, g_now);
  cpa::render_hint_bar(c, hint);
  g_display.present();
  const uint32_t t1 = static_cast<uint32_t>(esp_timer_get_time());
  const cpa::Rect wave = cpa::rec_wave_part(c, h, false, cpa::rec_room(c, s));
  g_display.present(wave);
  const uint32_t t2 = static_cast<uint32_t>(esp_timer_get_time());
  return MemoTimes{t1 - t0, t2 - t1};
}

// v0.4.0's screens through the real keys (header). Opt+R only opens the confirm: the
// recording would start on Enter, so ` cancels it once the 400 ms guard is over. v0.5: the
// Tab sheet slides in over home and over the confirm too (timed by the runtime's hooks).
void v040_screens() {
  key("\"opt\",\"h\"");
  TEST_ASSERT_EQUAL_STRING("home", g_rt.shell().top_id());
  key("\"tab\"");  // Here
  TEST_ASSERT_TRUE(g_rt.state().ui.menu);
  key("\"/\"");  // Go
  key("\"/\"");  // Control
  key("\"tab\"");
  TEST_ASSERT_FALSE(g_rt.state().ui.menu);
  key("\"opt\",\"l\"");  // locked and dark
  key("\"opt\",\"l\"");  // the lock card, and the 3 s unlock window
  TEST_ASSERT_TRUE(g_rt.state().ui.locked);
  key("\"enter\"");
  TEST_ASSERT_FALSE(g_rt.state().ui.locked);
  key("\"opt\",\"r\"");
  TEST_ASSERT_TRUE(g_rt.state().rec.phase == cpa::RecPhase::Confirm);
  key("\"tab\"");  // the menu over the consent card: a confirm box's two pages
  TEST_ASSERT_TRUE(g_rt.state().ui.menu);
  run(20);          // its slide
  key("\"tab\"");
  TEST_ASSERT_FALSE(g_rt.state().ui.menu);
  run(50);
  key("\"`\"");
  TEST_ASSERT_TRUE(g_rt.state().rec.phase == cpa::RecPhase::Idle);
  draw_locked_recording();
  run(20);
}

struct LangTimes {
  cpa::Runtime::RenderTimes runtime;
  uint32_t meeting_us;
  MemoTimes memo;
};

// docs/21: the English pass (header).
LangTimes english_times() {
  cpa::set_lang(cpa::Lang::En);
  g_rt.reset_render_times();
  g_rt.shell().mark_dirty();
  screens();
  v040_screens();
  LangTimes t{};
  t.meeting_us = time_meeting_page(cpa::tr(cpa::txt::kMeetingHint));
  t.memo = time_memo_page(cpa::tr(cpa::txt::kMemoHint));
  t.runtime = g_rt.render_times();
  cpa::set_lang(cpa::Lang::Zh);
  return t;
}

void check_times(const cpa::Runtime::RenderTimes& times, uint32_t meeting_us, const MemoTimes& memo) {
  TEST_ASSERT_TRUE(times.max_render_us > 0 && times.max_render_us < 50000u);  // docs/19 §10: every redraw
  TEST_ASSERT_TRUE(times.max_tick_us > 0 && times.max_tick_us < 50000u);      // and every tick < 50 ms
  TEST_ASSERT_TRUE(meeting_us < 50000u);
  TEST_ASSERT_TRUE(memo.page_us < 50000u && memo.tick_us < 50000u);  // gh #53
}

// The test item the workload saved first, as its MonoId text ("" when none).
std::string first_test_item() {
  const cpa::OutboxCore& ob = g_rt.outbox();
  for (uint16_t i = 0; i < ob.n; ++i) {
    if (ob.ids[i].epoch == kTestEpoch) return cpa::mono_format(ob.ids[i]).c_str();
  }
  return std::string();
}

// The daemon rejects one item: the runtime moves it to bad/ on the card (move_file).
void reject_one_item() {
  const std::string id = first_test_item();
  TEST_ASSERT_FALSE(id.empty());
  host("outbox.ack", "\"re\":\"" + id + "\",\"status\":\"rejected\",\"code\":\"E_BAD_FIELD\"");
  run(10);
}

// rm -r below a VFS path, bounded depth; returns the entries it could not delete.
int remove_tree(const std::string& path, int depth = 0) {
  DIR* d = ::opendir(path.c_str());
  if (!d) return 0;
  int failed = 0;
  while (const dirent* e = ::readdir(d)) {
    if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
    const std::string child = path + "/" + e->d_name;
    if (e->d_type == DT_DIR && depth < 4) failed += remove_tree(child, depth + 1);
    failed += (e->d_type == DT_DIR ? ::rmdir(child.c_str()) : ::unlink(child.c_str())) == 0 ? 0 : 1;
  }
  ::closedir(d);
  return failed;
}

}  // namespace

void setUp() {}
void tearDown() {}

static void test_rx_burst_8k_is_not_lost() {
  while (Serial.available() > 0) Serial.read();
  Serial.println("CPA_RX_READY");
  char first = '\n';
  const uint32_t t0 = millis();
  while (first == '\n' && millis() - t0 < 15000) {  // skip nudges until the burst starts
    (void)static_cast<bool>(Serial);
    if (Serial.available() > 0) first = static_cast<char>(Serial.read());
  }
  delay(1000);  // the loop is "blocked": the 8 KB must wait in the RX buffer
  static char buf[kBurst];
  buf[0] = first;
  size_t got = first == '\n' ? 0 : 1;
  const uint32_t t1 = millis();
  while (got < kBurst && millis() - t1 < 3000) got += g_usb.read(buf + got, kBurst - got);
  size_t bad = 0;
  for (size_t i = 0; i < got; ++i) bad += buf[i] != static_cast<char>('A' + i % 26) ? 1 : 0;
  Serial.printf("CPA_RX_DONE got=%u bad=%u\n", static_cast<unsigned>(got), static_cast<unsigned>(bad));
  TEST_ASSERT_EQUAL_UINT32(kBurst, got);
  TEST_ASSERT_EQUAL_UINT32(0, bad);
}

static void test_firmware_stack_and_heap_budgets() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 0;
  M5Cardputer.begin(cfg, true);
  TEST_ASSERT_TRUE(g_display.begin());
  cpa::hal_i2c_lock_begin();
  const cpa::SdFacts sd = cpa::hal_sd_mount(g_fs);
  TEST_ASSERT_TRUE_MESSAGE(sd.state == cpa::SdState::Ok, "insert the FAT32 card");
  TEST_ASSERT_EQUAL_INT(0, remove_tree(kVfsRoot));  // leftovers of an earlier run
  TEST_ASSERT_TRUE(g_fs.prepare());
  cpa::StorageBoot storage;
  storage.plan = cpa::StoragePlan{cpa::Store::Sd, false, cpa::StoreNote::NoInternal};
  storage.sd = sd;
  cpa::BootInfo boot;
  const cpa::BootRecord rec{kTestEpoch, 1};
  boot.ticket = cpa::boot_ticket(rec, rec).value;
  boot.dev = cpa::hal_dev_id();
  boot.rst = cpa::hal_reset_reason();
  const cpa::RecBoot rb = g_audio.prepare(true);
  TEST_ASSERT_TRUE(rb.buffers);
  g_rt.attach_rec(g_audio, rb);
  g_rt.begin(boot, g_now, storage);
  g_audio.start(g_rt.ids(), cpa::FixedStr<31>::from("cpa-fw/test"), kRoot);
  workload();
  TEST_ASSERT_EQUAL_UINT8(9, g_rt.state().projects.data.n);  // every snapshot was accepted
  TEST_ASSERT_EQUAL_UINT8(10, g_rt.state().sessions.data.n);
  TEST_ASSERT_EQUAL_UINT8(12, g_rt.state().queue.data.n);
  v040_screens();
  const uint32_t meeting_us = time_meeting_page("G0加★ 1-9项目 Opt+R结束 Tab菜单");
  const MemoTimes memo = time_memo_page("G0松开结束 1-9选项目");
  const cpa::Runtime::RenderTimes zh_times = g_rt.render_times();
  const LangTimes en = english_times();
  reject_one_item();
  const uint32_t stack_left = uxTaskGetStackHighWaterMark(nullptr);
  const uint32_t heap = ESP.getFreeHeap();
  const uint32_t block = ESP.getMaxAllocHeap();
  const cpa::Runtime::RenderTimes& times = zh_times;
  Serial.printf("CPA_BUDGET stack_left=%lu heap_free=%lu heap_min=%lu max_block=%lu sent=%lu pending=%u bad=%u "
                "store_errors=%u missing_glyphs=%lu rec=%u audio_stack_left=%lu max_render_us=%lu max_tick_us=%lu "
                "meeting_us=%lu memo_us=%lu memo_tick_us=%lu\n",
                static_cast<unsigned long>(stack_left), static_cast<unsigned long>(heap),
                static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(block),
                static_cast<unsigned long>(g_script.sent), g_rt.state().outbox.pending, g_rt.state().outbox.bad,
                g_rt.sys().store_errors, static_cast<unsigned long>(g_display.missing_glyphs()),
                g_rt.state().rec.ready ? 1u : 0u, static_cast<unsigned long>(g_audio.stack_left()),
                static_cast<unsigned long>(times.max_render_us), static_cast<unsigned long>(times.max_tick_us),
                static_cast<unsigned long>(meeting_us), static_cast<unsigned long>(memo.page_us),
                static_cast<unsigned long>(memo.tick_us));
  Serial.printf("CPA_BUDGET_EN max_render_us=%lu max_tick_us=%lu meeting_us=%lu memo_us=%lu memo_tick_us=%lu\n",
                static_cast<unsigned long>(en.runtime.max_render_us), static_cast<unsigned long>(en.runtime.max_tick_us),
                static_cast<unsigned long>(en.meeting_us), static_cast<unsigned long>(en.memo.page_us),
                static_cast<unsigned long>(en.memo.tick_us));
  const int left = remove_tree(kVfsRoot);
  ::rmdir(kVfsRoot);
  TEST_ASSERT_EQUAL_INT(0, left);
  TEST_ASSERT_TRUE(g_rt.sys().store == cpa::Store::Sd);
  TEST_ASSERT_EQUAL_UINT16(1, g_rt.state().outbox.bad);  // moved to bad/ on the card
  TEST_ASSERT_EQUAL_UINT16(0, g_rt.sys().store_errors);
  TEST_ASSERT_TRUE(g_rt.state().link.mode == cpa::LinkMode::Ready);
  TEST_ASSERT_TRUE(g_rt.state().rec.avail && g_rt.state().rec.ready);
  TEST_ASSERT_TRUE(g_script.sent > 10);
  TEST_ASSERT_EQUAL_UINT32(0, g_display.missing_glyphs());
  check_times(times, meeting_us, memo);
  check_times(en.runtime, en.meeting_us, en.memo);  // docs/21: English too
  TEST_ASSERT_TRUE(stack_left >= 4096);
  TEST_ASSERT_TRUE(heap >= 150u * 1024u);
  TEST_ASSERT_TRUE(block >= 64u * 1024u);
}

void setup() {
  devtest::begin(g_usb);
  UNITY_BEGIN();
  RUN_TEST(test_rx_burst_8k_is_not_lost);
  RUN_TEST(test_firmware_stack_and_heap_budgets);
  UNITY_END();
}

void loop() { devtest::idle(); }
