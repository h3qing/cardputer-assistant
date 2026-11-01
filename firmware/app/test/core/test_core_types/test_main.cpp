// FixedStr, Opt, Result, Effects/Step: value semantics, truncation, capacity.
// v0.4.0 shared interface (docs/13 §10.1): AppState.ui, the new enum values, and the
// Module / ModuleContext additions whose defaults keep v0.3.1 modules unchanged.
#include <unity.h>

#include <cstring>

#include "app_state.h"
#include "fixed_str.h"
#include "keymap.h"
#include "module.h"
#include "opt.h"
#include "result.h"
#include "step.h"
#include "ui/support/fake_context.h"

using cpa::Effects;
using cpa::FixedStr;
using cpa::Opt;
using cpa::Result;
using cpa::Step;

void setUp() {}
void tearDown() {}

static void test_fixed_str_from_and_compare() {
  const auto a = FixedStr<8>::from("hello");
  TEST_ASSERT_EQUAL_STRING("hello", a.c_str());
  TEST_ASSERT_EQUAL_UINT(5, a.size());
  TEST_ASSERT_FALSE(a.empty());
  TEST_ASSERT_TRUE(a == FixedStr<8>::from("hello"));
  TEST_ASSERT_FALSE(a == FixedStr<8>::from("hell"));
  TEST_ASSERT_TRUE(a.equals("hello"));
  TEST_ASSERT_FALSE(a.equals("hello!"));
  TEST_ASSERT_TRUE(FixedStr<8>().empty());
  TEST_ASSERT_EQUAL_UINT(8, (FixedStr<8>::capacity()));
}

static void test_fixed_str_truncates_and_keeps_embedded_bytes() {
  const auto t = FixedStr<4>::from("abcdef");
  TEST_ASSERT_EQUAL_STRING("abcd", t.c_str());
  TEST_ASSERT_TRUE(FixedStr<4>::fits(4));
  TEST_ASSERT_FALSE(FixedStr<4>::fits(5));
  const char raw[] = {'a', '\0', 'b'};
  const auto z = FixedStr<8>::from(raw, sizeof raw);
  TEST_ASSERT_EQUAL_UINT(3, z.size());
  TEST_ASSERT_EQUAL_INT(0, std::memcmp(raw, z.data(), 3));
}

static void test_fixed_str_append_returns_new_value() {
  const auto a = FixedStr<6>::from("ab");
  const auto b = a.append("cd", 2);
  const auto c = b.append("efgh", 4);
  TEST_ASSERT_EQUAL_STRING("ab", a.c_str());
  TEST_ASSERT_EQUAL_STRING("abcd", b.c_str());
  TEST_ASSERT_EQUAL_STRING("abcdef", c.c_str());
  TEST_ASSERT_EQUAL_STRING("abcdefx", (FixedStr<7>::from("abcdef").append("x")).c_str());
}

static void test_opt_and_result() {
  const Opt<int> none{};
  const auto some = cpa::some(7);
  TEST_ASSERT_FALSE(none.has);
  TEST_ASSERT_TRUE(some.has);
  TEST_ASSERT_EQUAL_INT(7, some.value);
  TEST_ASSERT_EQUAL_INT(3, none.value_or(3));
  TEST_ASSERT_EQUAL_INT(7, some.value_or(3));
  TEST_ASSERT_TRUE(some == cpa::some(7));
  TEST_ASSERT_FALSE(some == none);
  const auto ok = Result<int, int>::success(5);
  const auto bad = Result<int, int>::failure(9);
  TEST_ASSERT_TRUE(ok.ok);
  TEST_ASSERT_EQUAL_INT(5, ok.value);
  TEST_ASSERT_FALSE(bad.ok);
  TEST_ASSERT_EQUAL_INT(9, bad.error);
}

static void test_effects_are_bounded_values() {
  const Effects<int, 2> e0{};
  const auto e1 = e0.with(1);
  const auto e2 = e1.with(2);
  const auto e3 = e2.with(3);
  TEST_ASSERT_EQUAL_UINT8(0, e0.count);
  TEST_ASSERT_EQUAL_UINT8(1, e1.count);
  TEST_ASSERT_EQUAL_UINT8(2, e3.count);
  TEST_ASSERT_FALSE(e2.overflow);
  TEST_ASSERT_TRUE(e3.overflow);
  TEST_ASSERT_EQUAL_INT(2, e3.items[1]);
  const Step<int, int, 2> s{4, e2};
  TEST_ASSERT_EQUAL_INT(4, s.state);
  TEST_ASSERT_EQUAL_UINT8(2, s.effects.count);
}

// sizeof(AppState) at v0.3.1 (host and device alike: no pointers inside); v0.5 adds Mando's
// moment to UiFlags (docs/19 §4.3: +8 B).
constexpr size_t kAppStateBytesV031 = 3488;
// docs/20 U6, U15: RecInfo.held, .bat_low and .last (how the last recording ended).
constexpr size_t kRecResultBytes = 16;
constexpr size_t kMomentBytes = 8;
// docs/20 D8, U3: UiFlags.memo_ready ("● 准备") and .lock_held ("没有在录").
constexpr size_t kUxFlagsBytes = 4;
// v0.5.0 (docs/04, hidden keys): UiFlags.pat, Opt+W's pats (count, time, line: 12 B, 16 padded).
constexpr size_t kPatBytes = 16;
// docs/28 S1a: RecInfo.kept, the kept copies and the plug-in hint.
constexpr size_t kKeptBytes = 20;

static void test_app_state_budget_and_ui_flags() {
  TEST_ASSERT_TRUE(sizeof(cpa::AppState) <= 4096);
  TEST_ASSERT_TRUE(sizeof(cpa::AppState) <= kAppStateBytesV031 + kRecResultBytes + 4 + kMomentBytes + kUxFlagsBytes + kPatBytes + kKeptBytes);
  const cpa::AppState s{};
  TEST_ASSERT_FALSE(s.ui.menu);
  TEST_ASSERT_FALSE(s.ui.locked);
  TEST_ASSERT_TRUE(s.ui.moment == cpa::Moment::None);
  const cpa::UiFlags locked{false, true};
  TEST_ASSERT_TRUE(locked.locked);
  TEST_ASSERT_FALSE(locked.menu);
}

static void test_v040_enum_values_come_after_v031() {
  TEST_ASSERT_TRUE(cpa::Nav::Menu > cpa::Nav::Bksp);
  TEST_ASSERT_TRUE(cpa::Layer::System > cpa::Layer::RecPage);
  TEST_ASSERT_TRUE(cpa::Layer::Lock > cpa::Layer::System);
  TEST_ASSERT_TRUE(cpa::Action::Menu > cpa::Action::RecEnd);
  TEST_ASSERT_TRUE(cpa::Action::Lock > cpa::Action::Menu);
  TEST_ASSERT_TRUE(cpa::Action::Unlock > cpa::Action::Lock);
  TEST_ASSERT_TRUE(cpa::Action::Projects > cpa::Action::Unlock);
  const cpa::KeyEvent tab{cpa::KeyEventKind::Nav, 0, 0, 0, cpa::Nav::Menu};
  TEST_ASSERT_FALSE(tab == (cpa::KeyEvent{cpa::KeyEventKind::Nav, 0, 0, 0, cpa::Nav::Back}));
}

namespace {

const cpa::KeyEvent kOptP{cpa::KeyEventKind::Chord, 0, cpa::kModOpt, 'p', cpa::Nav::None};
const cpa::KeyEvent kSpace{cpa::KeyEventKind::Char, ' ', 0, 0, cpa::Nav::None};

// A v0.3.1 module: overrides only the pure virtuals; on 'p' it asks for Opt+P.
class PlainModule : public cpa::Module {
 public:
  cpa::ModuleContext* ctx = nullptr;
  bool pressed = false;
  const char* id() const override { return "plain"; }
  bool begin(cpa::ModuleContext& c) override {
    ctx = &c;
    return true;
  }
  bool onKey(const cpa::KeyEvent& k) override {
    if (!(k == cpa::KeyEvent{cpa::KeyEventKind::Char, 'p', 0, 0, cpa::Nav::None})) return false;
    pressed = ctx->press(kOptP);
    return true;
  }
  void render(cpa::Canvas&, const cpa::Rect&) const override {}
};

// A v0.3.1 context: overrides only the pure virtuals.
class PlainContext : public cpa::ModuleContext {
 public:
  cpa::AppState app;
  cpa::SysInfo info;
  const cpa::AppState& state() const override { return app; }
  void compose(const cpa::ComposeSpec&) override {}
  void confirm(const cpa::ConfirmSpec&) override {}
  bool request(const cpa::OnlineRequest&) override { return false; }
  void open(const char*, uint8_t) override {}
  void toast(cpa::ToastLevel, const char*) override {}
  uint32_t nowMs() const override { return 0; }
  const cpa::SysInfo& sys() const override { return info; }
};

const cpa::HelpRow kRows[] = {{kSpace, "空格", "加 ★"}, {cpa::KeyEvent{}, "1-9", "归到项目"}};

class HelpModule : public PlainModule {
 public:
  uint8_t help(const cpa::HelpRow** rows) const override {
    *rows = kRows;
    return 2;
  }
};

}  // namespace

static void test_module_defaults_keep_v031_behaviour() {
  PlainModule m;
  PlainContext ctx;
  TEST_ASSERT_TRUE(m.begin(ctx));
  const cpa::HelpRow* rows = kRows;
  TEST_ASSERT_EQUAL_UINT8(0, m.help(&rows));
  TEST_ASSERT_NULL(rows);
  TEST_ASSERT_EQUAL_STRING("", m.hint());
  TEST_ASSERT_EQUAL_UINT8(0, m.contextSlot());
  TEST_ASSERT_TRUE(m.onKey(cpa::KeyEvent{cpa::KeyEventKind::Char, 'p', 0, 0, cpa::Nav::None}));
  TEST_ASSERT_FALSE(m.pressed);  // the default context replays nothing
  TEST_ASSERT_FALSE(m.onKey(kSpace));
  const cpa::HelpRow blank{};
  TEST_ASSERT_TRUE(blank.ev.kind == cpa::KeyEventKind::None);
  TEST_ASSERT_EQUAL_STRING("", blank.keys);
  TEST_ASSERT_EQUAL_STRING("", blank.label);
}

static void test_fake_context_round_trips_press() {
  uitest::FakeContext ctx;
  PlainModule m;
  m.begin(ctx);
  TEST_ASSERT_TRUE(m.onKey(cpa::KeyEvent{cpa::KeyEventKind::Char, 'p', 0, 0, cpa::Nav::None}));
  TEST_ASSERT_TRUE(m.pressed);
  TEST_ASSERT_EQUAL_UINT(1, ctx.pressed.size());
  TEST_ASSERT_TRUE(ctx.pressed[0] == kOptP);
  ctx.accept_press = false;
  TEST_ASSERT_FALSE(ctx.press(kSpace));
  TEST_ASSERT_EQUAL_UINT(1, ctx.pressed.size());
}

static void test_help_rows_are_read_back() {
  TEST_ASSERT_EQUAL_UINT(0, uitest::help_rows(PlainModule{}).size());
  const auto rows = uitest::help_rows(HelpModule{});
  TEST_ASSERT_EQUAL_UINT(2, rows.size());
  TEST_ASSERT_TRUE(rows[0].ev == kSpace);
  TEST_ASSERT_EQUAL_STRING("空格", rows[0].keys);
  TEST_ASSERT_EQUAL_STRING("归到项目", rows[1].label);
  TEST_ASSERT_TRUE(rows[1].ev.kind == cpa::KeyEventKind::None);  // a range row: shown, not replayed
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fixed_str_from_and_compare);
  RUN_TEST(test_fixed_str_truncates_and_keeps_embedded_bytes);
  RUN_TEST(test_fixed_str_append_returns_new_value);
  RUN_TEST(test_opt_and_result);
  RUN_TEST(test_effects_are_bounded_values);
  RUN_TEST(test_app_state_budget_and_ui_flags);
  RUN_TEST(test_v040_enum_values_come_after_v031);
  RUN_TEST(test_module_defaults_keep_v031_behaviour);
  RUN_TEST(test_fake_context_round_trips_press);
  RUN_TEST(test_help_rows_are_read_back);
  return UNITY_END();
}
