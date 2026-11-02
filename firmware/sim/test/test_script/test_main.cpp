// Sim scripts (.cpas): one step per line, keys by their debug.key names, protocol
// messages as JSON. The parser is pure: files come through a Reader.
#include <unity.h>

#include <map>
#include <string>

#include "key_set.h"
#include "script.h"

using namespace cpasim;
using cpa::KeySet;
namespace key = cpa::key;

void setUp() {}
void tearDown() {}

namespace {

std::map<std::string, std::string> g_files;

Script parse(const std::string& text) {
  g_files["main.cpas"] = text;
  return parse_script("main.cpas", [](const std::string& path, std::string& out) {
    const auto it = g_files.find(path);
    if (it == g_files.end()) return false;
    out = it->second;
    return true;
  });
}

KeySet keys(std::initializer_list<uint8_t> ks) {
  KeySet s;
  for (const uint8_t k : ks) s = s.with(k);
  return s;
}

constexpr uint8_t kS = 31;      // layout index of 's'
constexpr uint8_t kA = 30;      // 'a'
constexpr uint8_t kSemi = 39;   // ';'
constexpr uint8_t kOne = 1;     // '1'

}  // namespace

static void test_comments_and_blank_lines_are_skipped() {
  const Script s = parse("# a comment\n\n   \nwait 100  # trailing\n");
  TEST_ASSERT_EQUAL_STRING("", s.error.c_str());
  TEST_ASSERT_EQUAL_size_t(1, s.steps.size());
  TEST_ASSERT_TRUE(s.steps[0].op == Op::Wait);
  TEST_ASSERT_EQUAL_UINT32(100, s.steps[0].ms);
  TEST_ASSERT_EQUAL_STRING("main.cpas:4", s.steps[0].where.c_str());
}

static void test_boot_options() {
  const Script s = parse("boot store=internal usb=off bat=3500\nboot\n");
  TEST_ASSERT_EQUAL_STRING("", s.error.c_str());
  TEST_ASSERT_TRUE(s.steps[0].boot.store == cpa::Store::Internal);
  TEST_ASSERT_FALSE(s.steps[0].boot.usb);
  TEST_ASSERT_EQUAL_UINT16(3500, s.steps[0].boot.bat_mv);
  TEST_ASSERT_TRUE(s.steps[1].boot.store == cpa::Store::Sd);
  TEST_ASSERT_TRUE(s.steps[1].boot.usb);
  TEST_ASSERT_TRUE(parse("boot store=none\n").steps[0].boot.store == cpa::Store::None);
  // docs/21: the language in NVS at boot, English unless a script says otherwise (v0.5.2)
  TEST_ASSERT_TRUE(s.steps[0].boot.lang == cpa::Lang::En);
  TEST_ASSERT_TRUE(parse("boot lang=en\n").steps[0].boot.lang == cpa::Lang::En);
  TEST_ASSERT_TRUE(parse("boot lang=zh\n").steps[0].boot.lang == cpa::Lang::Zh);
  TEST_ASSERT_EQUAL_STRING("main.cpas:1: unknown boot option 'lang=fr'", parse("boot lang=fr\n").error.c_str());
}

static void test_press_combos_and_aliases() {
  const Script s = parse("press opt+s enter ; esc up g0 ms=80\n");
  TEST_ASSERT_EQUAL_STRING("", s.error.c_str());
  const Step& p = s.steps[0];
  TEST_ASSERT_TRUE(p.op == Op::Press);
  TEST_ASSERT_EQUAL_UINT32(80, p.ms);
  TEST_ASSERT_EQUAL_size_t(6, p.keys.size());
  TEST_ASSERT_TRUE(p.keys[0] == keys({key::kOpt, kS}));
  TEST_ASSERT_TRUE(p.keys[1] == keys({key::kEnter}));
  TEST_ASSERT_TRUE(p.keys[2] == keys({kSemi}));
  TEST_ASSERT_TRUE(p.keys[3] == keys({key::kFn, key::kGrave}));
  TEST_ASSERT_TRUE(p.keys[4] == keys({kSemi}));  // up: the ; key (arrow legend)
  TEST_ASSERT_TRUE(p.keys[5] == keys({key::kG0}));
  TEST_ASSERT_EQUAL_UINT32(50, parse("press a\n").steps[0].ms);
}

static void test_g0_is_a_key() {
  const Script s = parse("press g0 a\n");
  TEST_ASSERT_EQUAL_size_t(2, s.steps[0].keys.size());
  TEST_ASSERT_TRUE(s.steps[0].keys[0] == keys({key::kG0}));
  TEST_ASSERT_TRUE(s.steps[0].keys[1] == keys({kA}));
}

static void test_hold_is_one_long_press() {
  const Script s = parse("hold g0 600\n");
  TEST_ASSERT_TRUE(s.steps[0].op == Op::Press);
  TEST_ASSERT_EQUAL_UINT32(600, s.steps[0].ms);
  TEST_ASSERT_TRUE(s.steps[0].keys[0] == keys({key::kG0}));
}

static void test_type_uses_shift_for_shifted_characters() {
  const Script s = parse("type aA !\n");
  const Step& t = s.steps[0];
  TEST_ASSERT_TRUE(t.op == Op::Press);
  TEST_ASSERT_EQUAL_size_t(4, t.keys.size());
  TEST_ASSERT_TRUE(t.keys[0] == keys({kA}));
  TEST_ASSERT_TRUE(t.keys[1] == keys({key::kShift, kA}));
  TEST_ASSERT_TRUE(t.keys[2] == keys({key::kSpace}));
  TEST_ASSERT_TRUE(t.keys[3] == keys({key::kShift, kOne}));
}

static void test_down_up_wait_usb_bat_mic() {
  const Script s = parse("down g0\nup g0\nusb off\nusb on\nbat 3350\nmic 3000 4000\nmic 500\n");
  TEST_ASSERT_EQUAL_STRING("", s.error.c_str());
  TEST_ASSERT_TRUE(s.steps[0].op == Op::Down);
  TEST_ASSERT_TRUE(s.steps[0].keys[0] == keys({key::kG0}));
  TEST_ASSERT_TRUE(s.steps[1].op == Op::Up);
  TEST_ASSERT_EQUAL_UINT32(0, s.steps[2].value);
  TEST_ASSERT_EQUAL_UINT32(1, s.steps[3].value);
  TEST_ASSERT_EQUAL_UINT32(3350, s.steps[4].value);
  TEST_ASSERT_EQUAL_UINT32(3000, s.steps[5].ms);
  TEST_ASSERT_EQUAL_UINT32(4000, s.steps[5].value);
  TEST_ASSERT_EQUAL_UINT32(2000, s.steps[6].value);  // default level
}

static void test_host_and_reply_keep_the_fields() {
  const Script s = parse(
      "host time.set {\"epoch_ms\":1792440000000,\"tz_min\":480}\n"
      "host ping\n"
      "reply ack outbox.item\n"
      "reply agent.result agent.spawn {\"ok\":true,\"name\":\"助手\"}\n");
  TEST_ASSERT_EQUAL_STRING("", s.error.c_str());
  TEST_ASSERT_TRUE(s.steps[0].op == Op::Host);
  TEST_ASSERT_EQUAL_STRING("time.set", s.steps[0].type.c_str());
  TEST_ASSERT_EQUAL_STRING("\"epoch_ms\":1792440000000,\"tz_min\":480", s.steps[0].text.c_str());
  TEST_ASSERT_EQUAL_STRING("", s.steps[1].text.c_str());
  TEST_ASSERT_TRUE(s.steps[2].op == Op::Reply);
  TEST_ASSERT_EQUAL_STRING("ack", s.steps[2].type.c_str());
  TEST_ASSERT_EQUAL_STRING("outbox.item", s.steps[2].re_type.c_str());
  TEST_ASSERT_EQUAL_STRING("\"ok\":true,\"name\":\"助手\"", s.steps[3].text.c_str());
}

static void test_shot_name_and_caption() {
  const Script s = parse("shot board-home The board, two projects\nshot plain\n");
  TEST_ASSERT_TRUE(s.steps[0].op == Op::Shot);
  TEST_ASSERT_EQUAL_STRING("board-home", s.steps[0].type.c_str());
  TEST_ASSERT_EQUAL_STRING("The board, two projects", s.steps[0].text.c_str());
  TEST_ASSERT_EQUAL_STRING("plain", s.steps[1].text.c_str());
}

// v0.5 S3: a voice for the microphone, and frames: n shots ms apart, the microphone optional.
static void test_speech_and_frames() {
  const Script s = parse("mic 800 speech\nframes memo 4 200 mic=speech Mando nods\nframes menu 3 50\n"
                         "frames q 2 200 mic=150\n");
  TEST_ASSERT_EQUAL_STRING("", s.error.c_str());
  TEST_ASSERT_TRUE(s.steps[0].op == Op::Mic && s.steps[0].speech && s.steps[0].ms == 800);
  const Step& f = s.steps[1];
  TEST_ASSERT_TRUE(f.op == Op::Frames && f.mic && f.speech);
  TEST_ASSERT_EQUAL_STRING("memo", f.type.c_str());
  TEST_ASSERT_EQUAL_UINT32(4, f.count);
  TEST_ASSERT_EQUAL_UINT32(200, f.ms);
  TEST_ASSERT_EQUAL_STRING("Mando nods", f.text.c_str());
  TEST_ASSERT_TRUE(!s.steps[2].mic && s.steps[2].count == 3 && s.steps[2].text == "menu");
  TEST_ASSERT_TRUE(s.steps[3].mic && !s.steps[3].speech && s.steps[3].value == 150);
  for (const char* bad : {"frames x 0 200\n", "frames x 2 5\n", "frames x 2 200 mic=loud\n", "frames\n", "mic 10 hum\n"}) {
    TEST_ASSERT_TRUE_MESSAGE(!parse(bad).error.empty(), bad);
  }
}

static void test_include_is_relative_and_located() {
  g_files["common/state.cpas"] = "wait 5\nbogus\n";
  g_files["common/inner.cpas"] = "wait 7\n";
  g_files["common/outer.cpas"] = "include inner.cpas\n";
  const Script ok = parse("include common/outer.cpas\nwait 1\n");
  TEST_ASSERT_EQUAL_STRING("", ok.error.c_str());
  TEST_ASSERT_EQUAL_size_t(2, ok.steps.size());
  TEST_ASSERT_EQUAL_UINT32(7, ok.steps[0].ms);
  TEST_ASSERT_EQUAL_STRING("common/inner.cpas:1", ok.steps[0].where.c_str());
  const Script bad = parse("wait 1\ninclude common/state.cpas\n");
  TEST_ASSERT_EQUAL_STRING("common/state.cpas:2: unknown step 'bogus'", bad.error.c_str());
}

static void test_errors_name_the_line() {
  TEST_ASSERT_EQUAL_STRING("main.cpas:2: unknown key 'nokey'", parse("wait 1\npress nokey\n").error.c_str());
  TEST_ASSERT_EQUAL_STRING("main.cpas:1: 'type' cannot type '\xE5\x8A\xA9'",  // 助
                           parse("type \xE5\x8A\xA9\n").error.c_str());
  TEST_ASSERT_EQUAL_STRING("main.cpas:1: fields must be one JSON object {...}", parse("host ping x\n").error.c_str());
  TEST_ASSERT_EQUAL_STRING("main.cpas:1: 'wait' needs a number of ms", parse("wait soon\n").error.c_str());
  TEST_ASSERT_EQUAL_STRING("main.cpas:1: unknown boot option 'disk=sd'", parse("boot disk=sd\n").error.c_str());
  TEST_ASSERT_EQUAL_STRING("main.cpas:1: 'shot' needs a name", parse("shot\n").error.c_str());
  TEST_ASSERT_EQUAL_STRING("main.cpas:1: cannot read 'missing.cpas'", parse("include missing.cpas\n").error.c_str());
  TEST_ASSERT_EQUAL_STRING("main.cpas:1: 'usb' takes on or off", parse("usb maybe\n").error.c_str());
}

static void test_include_loop_is_an_error() {
  g_files["loop.cpas"] = "include loop.cpas\n";
  TEST_ASSERT_EQUAL_STRING("loop.cpas:1: includes nested too deep", parse("include loop.cpas\n").error.c_str());
}

// In `type` text too, '#' after a blank starts a comment, unless an odd number of '"'
// comes before it (README: type " #" as `press space shift+3`).
static void test_type_text_and_comments() {
  const Script s = parse("type a #b\ntype a#b\ntype \"x #y\n");
  TEST_ASSERT_EQUAL_STRING("", s.error.c_str());
  TEST_ASSERT_EQUAL_size_t(1, s.steps[0].keys.size());
  TEST_ASSERT_EQUAL_size_t(3, s.steps[1].keys.size());
  TEST_ASSERT_EQUAL_size_t(5, s.steps[2].keys.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_comments_and_blank_lines_are_skipped);
  RUN_TEST(test_type_text_and_comments);
  RUN_TEST(test_boot_options);
  RUN_TEST(test_press_combos_and_aliases);
  RUN_TEST(test_g0_is_a_key);
  RUN_TEST(test_hold_is_one_long_press);
  RUN_TEST(test_type_uses_shift_for_shifted_characters);
  RUN_TEST(test_down_up_wait_usb_bat_mic);
  RUN_TEST(test_host_and_reply_keep_the_fields);
  RUN_TEST(test_shot_name_and_caption);
  RUN_TEST(test_speech_and_frames);
  RUN_TEST(test_include_is_relative_and_located);
  RUN_TEST(test_errors_name_the_line);
  RUN_TEST(test_include_loop_is_an_error);
  return UNITY_END();
}
