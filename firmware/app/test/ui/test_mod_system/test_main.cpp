// The device tab of 设置 (04 US-16; docs/21 §4.5): firmware version, dev, boot id, outbox
// counts (with bad items), free / minimum heap, largest block, loop stack left, battery and
// the daemon version; how the device booted, the primary store and its free space, the
// migration counts; one warning row with the storage errors, the storage note, NVS, the
// invariant, the modules and the keyboard's ghost / re-init counts. 8 rows under the tabs,
// in either language. The settings tab and the WiFi page are in test_settings.cpp.
#include <unity.h>

#include <string>
#include <vector>

#include "support/fake_canvas.h"
#include "support/fake_context.h"
#include "support/fixtures.h"
#include "system_logic.h"
#include "system_module.h"
#include "test_system_rig.h"
#include "text_fit.h"
#include "txt_rules.h"

using namespace cpa;
using namespace uitest;

void setUp() { set_lang(Lang::Zh); }
void tearDown() { set_lang(Lang::Zh); }

static std::string page_text(const FakeContext& ctx) {
  const SystemPage p = system_page(ctx.app, ctx.info);
  std::string out;
  for (uint8_t i = 0; i < p.n; ++i) out += std::string(p.lines[i].c_str()) + "\n";
  return out;
}

static SystemPage page_of(const FakeContext& ctx) { return system_page(ctx.app, ctx.info); }

static std::string line_with(const SystemPage& p, const char* needle, bool* warn = nullptr) {
  for (uint8_t i = 0; i < p.n; ++i) {
    const std::string line = p.lines[i].c_str();
    if (line.find(needle) == std::string::npos) continue;
    if (warn) *warn = p.warn[i];
    return line;
  }
  return std::string();
}

static void test_all_us16_fields_are_listed() {
  const std::string t = page_text(filled());
  const char* expected[] = {"cpa-fw/0.1.0", "020000000001", "3QZK1M7-0000C", "待发 3", "在途 1", "坏 2", "180K",
                            "170K",         "120K",         "6.0K",          "幽灵 2", "重置 1", "4150mV", "cpad/0.1.0"};
  for (const char* e : expected) {
    if (t.find(e) == std::string::npos) TEST_FAIL_MESSAGE(e);
  }
}

static void test_unknowns_and_warnings() {
  FakeContext ctx = filled();
  ctx.info.host_app = FixedStr<32>{};
  ctx.info.ids_ok = false;
  ctx.info.outbox_broken = 1;
  ctx.info.modules_disabled = 1;
  const SystemPage p = page_of(ctx);
  const std::string t = page_text(ctx);
  TEST_ASSERT_TRUE(t.find("daemon --") != std::string::npos);
  TEST_ASSERT_TRUE(t.find("NVS") != std::string::npos);
  TEST_ASSERT_TRUE(t.find("不变式") != std::string::npos);
  TEST_ASSERT_TRUE(t.find("禁用模块 1") != std::string::npos);
  TEST_ASSERT_TRUE(p.warn[p.n - 1]);
  TEST_ASSERT_TRUE(p.n <= kSystemLines);
}

static void test_kib_text() {
  TEST_ASSERT_EQUAL_STRING("180K", kib_text(184320).c_str());
  TEST_ASSERT_EQUAL_STRING("6.0K", kib_text(6144).c_str());
  TEST_ASSERT_EQUAL_STRING("0.5K", kib_text(512).c_str());
}

// Each row's label is its first word (key_len bytes); the warning row has none.
static void test_rows_know_their_labels() {
  FakeContext ctx = filled();
  const SystemPage p = page_of(ctx);
  const char* labels[] = {"固件", "dev", "启动", "存储", "outbox", "堆", "电池"};
  TEST_ASSERT_EQUAL_UINT8(8, p.n);  // the keyboard's counts make a warning row
  for (uint8_t i = 0; i < 7; ++i) {
    TEST_ASSERT_EQUAL_STRING(labels[i], std::string(p.lines[i].c_str(), p.key_len[i]).c_str());
  }
  TEST_ASSERT_EQUAL_UINT8(0, p.key_len[7]);
  TEST_ASSERT_EQUAL_STRING("键盘 幽灵 2 重置 1", p.lines[7].c_str());
  TEST_ASSERT_TRUE(p.warn[7]);
}

// docs/21 §4.5: the English labels are short words, none wider than 36 px, so the value
// column stays at x 46 in both languages.
static void test_english_rows() {
  LangScope en(Lang::En);
  FakeContext ctx = filled();
  ctx.info.store = Store::Sd;
  ctx.info.sd_mib = 488281;
  ctx.app.rec.free_known = true;
  ctx.app.rec.free_mib = 421888;
  const SystemPage p = page_of(ctx);
  const char* lines[] = {"FW cpa-fw/0.1.0  rst poweron", "dev 020000000001  boot 3QZK1M7-0000C",
                         "Boot direct  daemon cpad/0.1.0", "Store SD 476G  free 412G",
                         "outbox pending 3 out 1 bad 2",   "Heap 180K min 170K blk 120K",
                         "Bat 4150mV  stack 6.0K",          "Keys ghost 2 reset 1"};
  TEST_ASSERT_EQUAL_UINT8(8, p.n);
  for (uint8_t i = 0; i < 8; ++i) TEST_ASSERT_EQUAL_STRING(lines[i], p.lines[i].c_str());
  for (uint8_t i = 0; i < 7; ++i) {
    const std::string label(p.lines[i].c_str(), p.key_len[i]);
    TEST_ASSERT_TRUE_MESSAGE(i18n::text_width(label.c_str()) <= 36, label.c_str());
  }
}

static void test_boot_mode_line() {
  FakeContext ctx = filled();
  TEST_ASSERT_EQUAL_STRING("启动 直接  daemon cpad/0.1.0", line_with(page_of(ctx), "启动").c_str());
  ctx.info.launcher = true;
  TEST_ASSERT_EQUAL_STRING("启动 Launcher  daemon cpad/0.1.0", line_with(page_of(ctx), "启动").c_str());
}

static void test_store_line_names_the_store_and_its_size() {
  FakeContext ctx = filled();
  ctx.info.store = Store::Sd;
  ctx.info.sd = SdState::Ok;
  ctx.info.sd_mib = 488281;  // a 512 GB card
  TEST_ASSERT_EQUAL_STRING("存储 SD 476G", line_with(page_of(ctx), "存储").c_str());
  ctx.app.rec.free_known = true;  // the free space, known once recording has looked
  ctx.app.rec.free_mib = 421888;
  TEST_ASSERT_EQUAL_STRING("存储 SD 476G  剩 412G", line_with(page_of(ctx), "存储").c_str());
  ctx.info.sd_mib = 30436;  // 32 GB
  ctx.app.rec.free_known = false;
  TEST_ASSERT_EQUAL_STRING("存储 SD 29G", line_with(page_of(ctx), "存储").c_str());
  ctx.info.sd_mib = 3796;  // 4 GB
  TEST_ASSERT_EQUAL_STRING("存储 SD 3.7G", line_with(page_of(ctx), "存储").c_str());
  ctx.info.store = Store::Internal;
  ctx.info.cpafs_bytes = 0x180000;  // full-flash table
  TEST_ASSERT_EQUAL_STRING("存储 内部 1.5M", line_with(page_of(ctx), "存储").c_str());
  ctx.info.cpafs_bytes = 0x70000;  // created by the Launcher
  TEST_ASSERT_EQUAL_STRING("存储 内部 448K", line_with(page_of(ctx), "存储").c_str());
  ctx.info.store = Store::None;
  bool warn = false;
  TEST_ASSERT_EQUAL_STRING("存储 无", line_with(page_of(ctx), "存储", &warn).c_str());
  TEST_ASSERT_TRUE(warn);
}

static void test_every_store_note_has_its_text() {
  const struct {
    StoreNote note;
    const char* zh;
    const char* en;
  } cases[] = {
      {StoreNote::NoCard, "SD：未检测到卡", "SD: no card"},
      {StoreNote::SdUnusable, "SD 卡不能用：在 Mac 上格式化成 FAT32", "SD unusable: format FAT32 on a Mac"},
      {StoreNote::NoInternal, "无内部存储，只用 SD", "No flash store: SD only"},
      {StoreNote::LegacyTable, "分区表是 v0.1 的，请整片刷写", "v0.1 partition table: flash it all"},
      {StoreNote::NoStorage, "没有可用存储：请插入 FAT32 SD 卡", "No storage: insert a FAT32 SD card"},
  };
  for (const auto& c : cases) {
    FakeContext ctx = filled();
    ctx.app.device.ghost_n = 0;
    ctx.app.device.kb_reinit_n = 0;
    ctx.info.note = c.note;
    bool warn = false;
    if (line_with(page_of(ctx), c.zh, &warn).empty()) TEST_FAIL_MESSAGE(c.zh);
    TEST_ASSERT_TRUE_MESSAGE(warn, c.zh);
    LangScope en(Lang::En);
    if (line_with(page_of(ctx), c.en, &warn).empty()) TEST_FAIL_MESSAGE(c.en);
  }
  FakeContext none = filled();  // StoreNote::None (also the nosd build): no note at all
  const std::string all = page_text(none);
  TEST_ASSERT_TRUE(all.find("SD：") == std::string::npos);
  TEST_ASSERT_TRUE(all.find("请") == std::string::npos);
}

// docs/21 §4.5: at a boot that migrated, the counts take the free space's place.
static void test_migration_and_store_error_counts() {
  FakeContext ctx = filled();
  ctx.info.store = Store::Sd;
  ctx.info.sd_mib = 488281;
  ctx.app.rec.free_known = true;
  ctx.app.rec.free_mib = 421888;
  ctx.info.migrated = 3;
  bool warn = true;
  TEST_ASSERT_EQUAL_STRING("存储 SD 476G  已迁移 3", line_with(page_of(ctx), "存储", &warn).c_str());
  TEST_ASSERT_FALSE(warn);
  ctx.info.migrate_failed = 2;
  TEST_ASSERT_EQUAL_STRING("存储 SD 476G  已迁移 3  迁移失败 2", line_with(page_of(ctx), "存储", &warn).c_str());
  TEST_ASSERT_TRUE(warn);
  TEST_ASSERT_TRUE(page_text(ctx).find("存储错误") == std::string::npos);
  ctx.info.store_errors = 4;
  TEST_ASSERT_FALSE(line_with(page_of(ctx), "存储错误 4，请重启", &warn).empty());
  TEST_ASSERT_TRUE(warn);
  LangScope en(Lang::En);
  TEST_ASSERT_EQUAL_STRING("Store SD 476G  moved 3  failed 2", line_with(page_of(ctx), "Store").c_str());
}

static void expect_whole_characters(const SystemPage& p) {
  for (uint8_t i = 0; i < p.n; ++i) {
    const std::string line = p.lines[i].c_str();
    size_t at = 0;
    while (at < line.size()) {
      const Utf8Char ch = utf8_at(line.c_str(), line.size(), at);
      TEST_ASSERT_TRUE_MESSAGE(ch.cp != 0xFFFD, line.c_str());
      at += ch.len;
    }
  }
}

// docs/21 §9.2: every warning source on, recording possible, migration counts: still 8
// rows at most, the last one the warning row; no row is cut inside a character.
static void test_worst_case_fits_the_screen() {
  for (Lang l : kLangs) {
    LangScope s(l);
    FakeContext ctx = filled();
    ctx.info.store = Store::Internal;
    ctx.info.cpafs_bytes = 0x180000;
    ctx.info.note = StoreNote::SdUnusable;
    ctx.info.migrated = 12;
    ctx.info.migrate_failed = 12;
    ctx.info.store_errors = 65535;
    ctx.info.ids_ok = false;
    ctx.info.outbox_broken = 3;
    ctx.info.modules_disabled = 1;
    ctx.app.rec.avail = true;
    ctx.app.rec.files_n = 99;
    ctx.app.outbox.full = true;
    const SystemPage p = page_of(ctx);
    TEST_ASSERT_TRUE(p.n <= kSystemLines);
    TEST_ASSERT_EQUAL_UINT8(0, p.key_len[p.n - 1]);
    TEST_ASSERT_TRUE(p.warn[p.n - 1]);
    TEST_ASSERT_FALSE(line_with(p, "65535").empty());
    TEST_ASSERT_FALSE(line_with(p, "daemon cpad/0.1.0").empty());
    expect_whole_characters(p);
  }
}

static void test_size_text() {
  TEST_ASSERT_EQUAL_STRING("448K", size_text(0x70000).c_str());
  TEST_ASSERT_EQUAL_STRING("1.5M", size_text(0x180000).c_str());
  TEST_ASSERT_EQUAL_STRING("476G", size_text(488281ull << 20).c_str());
  TEST_ASSERT_EQUAL_STRING("0.5K", size_text(512).c_str());
  TEST_ASSERT_EQUAL_STRING("2.0T", size_text(2ull << 40).c_str());
}

// docs/21 §4.5 (docs/19 §6.9): the tabs, then label / value rows: the label in Ink3 at x 4,
// the value in Ink at x 46, single-spaced; a Line divider between who the device is and
// how it is doing; the warning row whole, in Warn; the last row ends on the hint bar.
static void test_device_tab_is_labelled_rows() {
  FakeContext ctx = filled();
  ctx.info.ids_ok = false;
  SystemModule m;
  FakeCanvas c = render_tab(m, ctx, SysTab::Device);
  const TextOp* fw = c.find("固件");
  TEST_ASSERT_TRUE(fw != nullptr && fw->x == 4 && fw->y == kContentRect.y + 12 && fw->style.fg == Color::Ink3);
  const TextOp* dev = c.find("dev");
  TEST_ASSERT_TRUE(dev->x == 4 && dev->style.fg == Color::Ink3);
  const TextOp* value = c.find("020000000001");
  TEST_ASSERT_TRUE(value->x == 46 && value->style.fg == Color::Ink && value->y == dev->y);
  TEST_ASSERT_EQUAL_STRING("020000000001 boot 3QZK1M7-0000C", value->text.c_str());
  TEST_ASSERT_TRUE(c.filled_at(kContentRect.y + 49, Color::Line));
  // below the tabs (docs/28: 存储 is also the storage tab's pill)
  TEST_ASSERT_EQUAL_INT16(kContentRect.y + 51, c.find_below("存储", kContentRect.y + 20)->y);
  const TextOp* warn = c.find("NVS 错误");
  TEST_ASSERT_TRUE(warn->x == 4 && warn->style.fg == Color::Warn);
  TEST_ASSERT_EQUAL_INT16(kHintRect.y - 12, warn->y);  // the 8th row
  expect_inside(c);
}

static void test_device_tab_fits_in_english() {
  LangScope en(Lang::En);
  FakeContext ctx = filled();
  ctx.info.store = Store::Internal;
  ctx.info.cpafs_bytes = 0x180000;
  ctx.info.migrated = 12;
  ctx.info.migrate_failed = 12;
  ctx.info.launcher = true;
  ctx.info.host_app = FixedStr<32>::from("cpad/0.4.1");
  SystemModule m;
  FakeCanvas c = render_tab(m, ctx, SysTab::Device);
  TEST_ASSERT_TRUE(c.has("FW"));
  TEST_ASSERT_TRUE(c.has("Flash 1.5M moved 12 failed 12"));
  expect_inside(c);
}

void test_settings_tab_opens_first_on_the_wifi_row();
void test_dot_and_semicolon_move_between_the_two_rows();
void test_comma_and_slash_switch_tabs();
void test_device_tab_ignores_the_row_keys();
void test_enter_on_wifi_opens_the_wifi_page();
void test_enter_on_language_switches_through_the_shell();
void test_every_visit_starts_on_the_wifi_row();
void test_settings_tab_layout_in_chinese();
void test_settings_tab_layout_in_english();
void test_link_row_names_the_mac();
void test_link_row_cuts_a_long_name_and_keeps_the_state();
void test_link_row_after_the_cable_is_pulled();
void test_sync_row();
void test_sync_row_widest_fits();
void test_wifi_page_words();
void test_wifi_page_without_the_launcher();
void test_wifi_page_fits_in_both_languages();
void test_help_rows_by_page();
void test_hint_keys_by_page();
void test_tabs_share_one_y();
void test_storage_rows_and_facts_in_chinese();
void test_storage_rows_and_facts_in_english();
void test_send_again_opens_a_box_on_cancel();
void test_give_to_this_mac();
void test_storage_facts_do_not_touch();
void test_clear_opens_a_box_on_cancel();
void test_back_and_stray_keys_never_clear();
void test_nothing_kept_and_busy_say_so();
void test_storage_box_fits_in_english();
void test_send_again_without_a_batch_says_so();

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boot_mode_line);
  RUN_TEST(test_store_line_names_the_store_and_its_size);
  RUN_TEST(test_every_store_note_has_its_text);
  RUN_TEST(test_migration_and_store_error_counts);
  RUN_TEST(test_worst_case_fits_the_screen);
  RUN_TEST(test_size_text);
  RUN_TEST(test_all_us16_fields_are_listed);
  RUN_TEST(test_unknowns_and_warnings);
  RUN_TEST(test_kib_text);
  RUN_TEST(test_rows_know_their_labels);
  RUN_TEST(test_english_rows);
  RUN_TEST(test_device_tab_is_labelled_rows);
  RUN_TEST(test_device_tab_fits_in_english);
  RUN_TEST(test_settings_tab_opens_first_on_the_wifi_row);
  RUN_TEST(test_dot_and_semicolon_move_between_the_two_rows);
  RUN_TEST(test_comma_and_slash_switch_tabs);
  RUN_TEST(test_device_tab_ignores_the_row_keys);
  RUN_TEST(test_enter_on_wifi_opens_the_wifi_page);
  RUN_TEST(test_enter_on_language_switches_through_the_shell);
  RUN_TEST(test_every_visit_starts_on_the_wifi_row);
  RUN_TEST(test_settings_tab_layout_in_chinese);
  RUN_TEST(test_settings_tab_layout_in_english);
  RUN_TEST(test_link_row_names_the_mac);
  RUN_TEST(test_link_row_cuts_a_long_name_and_keeps_the_state);
  RUN_TEST(test_link_row_after_the_cable_is_pulled);
  RUN_TEST(test_sync_row);
  RUN_TEST(test_sync_row_widest_fits);
  RUN_TEST(test_wifi_page_words);
  RUN_TEST(test_wifi_page_without_the_launcher);
  RUN_TEST(test_wifi_page_fits_in_both_languages);
  RUN_TEST(test_help_rows_by_page);
  RUN_TEST(test_hint_keys_by_page);
  RUN_TEST(test_tabs_share_one_y);
  RUN_TEST(test_storage_rows_and_facts_in_chinese);
  RUN_TEST(test_storage_rows_and_facts_in_english);
  RUN_TEST(test_send_again_opens_a_box_on_cancel);
  RUN_TEST(test_give_to_this_mac);
  RUN_TEST(test_storage_facts_do_not_touch);
  RUN_TEST(test_clear_opens_a_box_on_cancel);
  RUN_TEST(test_back_and_stray_keys_never_clear);
  RUN_TEST(test_nothing_kept_and_busy_say_so);
  RUN_TEST(test_storage_box_fits_in_english);
  RUN_TEST(test_send_again_without_a_batch_says_so);
  return UNITY_END();
}
