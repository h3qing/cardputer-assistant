// The plug-in hint (docs/28 §3.2, D11): 5 s, not in the way; Tab asks, Enter keeps the
// offline recordings from this Mac (2 presses), anything else leaves them to it (0 presses).
#include <unity.h>

#include "runtime.h"  // first: the chain-mode finder then builds what shell needs
#include "file_req.h"
#include "plug_hint.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

namespace {

constexpr KeyEvent kTab{KeyEventKind::Nav, 0, 0, 0, Nav::Menu};
constexpr KeyEvent kEnter{KeyEventKind::Nav, 0, 0, 0, Nav::Enter};
constexpr KeyEvent kBack{KeyEventKind::Char, '`', 0, 0, Nav::None};
constexpr KeyEvent kA{KeyEventKind::Char, 'a', 0, 0, Nav::None};

MonoId id(uint32_t seq) {
  MonoId m;
  m.epoch = 9;
  m.boot = 4;
  m.seq = seq;
  return m;
}

}  // namespace

static void test_shown_for_five_seconds_then_released() {
  const PlugState s = plug_open(3, id(10), 1000);
  TEST_ASSERT_TRUE(s.phase == PlugHint::Shown);
  TEST_ASSERT_TRUE(plug_tick(s, 5999).act == PlugDo::None);
  const PlugStep t = plug_tick(s, 6000);
  TEST_ASSERT_TRUE(t.st.phase == PlugHint::None && t.act == PlugDo::Release);
}

// Review 1, 2: locked or under the Tab menu the bar cannot be seen: its time stands still
// (the segments stay held), for kPlugHiddenMaxMs at most.
static void test_hidden_time_stands_still() {
  PlugState s = plug_open(3, id(10), 1000);
  s = plug_hide(s, 60000);
  TEST_ASSERT_TRUE(plug_tick(s, 61000 + 4999).act == PlugDo::None);
  TEST_ASSERT_TRUE(plug_tick(s, 61000 + 5000).act == PlugDo::Release);
  PlugState a = plug_key(plug_open(3, id(10), 0), kTab, 100).st;
  a = plug_hide(a, 30000);
  TEST_ASSERT_TRUE(plug_tick(a, 30100 + kPlugAskMs - 1).act == PlugDo::None);
}

static void test_hidden_at_most_two_minutes() {
  PlugState s = plug_open(3, id(10), 0);
  s = plug_hide(s, kPlugHiddenMaxMs);
  const PlugStep t = plug_tick(s, kPlugHiddenMaxMs);
  TEST_ASSERT_TRUE(t.act == PlugDo::Release && t.st.phase == PlugHint::None);
  PlugState b = plug_key(plug_key(plug_open(3, id(10), 0), kTab, 0).st, kEnter, 0).st;
  b = plug_hide(b, 500000);  // Busy waits for the ledger, not for the clock
  TEST_ASSERT_TRUE(plug_tick(b, 500000).act == PlugDo::None);
}

static void test_other_keys_pass_while_shown() {
  const PlugStep k = plug_key(plug_open(3, id(10), 0), kA, 100);
  TEST_ASSERT_FALSE(k.taken);
  TEST_ASSERT_TRUE(k.st.phase == PlugHint::Shown);
}

static void test_tab_then_enter_holds_them() {
  const PlugStep ask = plug_key(plug_open(3, id(10), 0), kTab, 100);
  TEST_ASSERT_TRUE(ask.taken && ask.st.phase == PlugHint::Asking);
  TEST_ASSERT_TRUE(plug_tick(ask.st, 6000).act == PlugDo::None);  // asking: held on
  const PlugStep yes = plug_key(ask.st, kEnter, 7000);
  TEST_ASSERT_TRUE(yes.taken && yes.act == PlugDo::Hold && yes.st.phase == PlugHint::Busy);
  TEST_ASSERT_TRUE(plug_done(yes.st).phase == PlugHint::None);
}

static void test_back_or_another_key_lets_them_go() {
  const PlugState ask = plug_key(plug_open(3, id(10), 0), kTab, 100).st;
  const PlugStep no = plug_key(ask, kBack, 200);
  TEST_ASSERT_TRUE(no.taken && no.act == PlugDo::Release && no.st.phase == PlugHint::None);
  const PlugStep other = plug_key(ask, kA, 200);
  TEST_ASSERT_TRUE(!other.taken && other.act == PlugDo::Release);
}

static void test_a_question_nobody_answers_goes_away() {
  const PlugState ask = plug_key(plug_open(3, id(10), 0), kTab, 100).st;
  TEST_ASSERT_TRUE(plug_tick(ask, 100 + kPlugAskMs).act == PlugDo::Release);
}

static void test_offline_counts_recordings_this_mac_may_see() {
  rec::RecIndex idx;
  idx.insert(id(5), 44, 0, true);     // before the mark: online
  idx.insert(id(10), 44, 0, true);    // offline recording 1, two segments
  idx.insert(id(11), 44, 0, false);
  idx.insert(id(14), 44, 0xA1, true); // kept from A already
  const rec::Offline a = rec::file_offline(idx, id(10), 0xA1);
  TEST_ASSERT_EQUAL_UINT16(2, a.segs);
  TEST_ASSERT_EQUAL_UINT16(1, a.recs);
  const rec::Offline b = rec::file_offline(idx, id(10), 0xB2);
  TEST_ASSERT_EQUAL_UINT16(3, b.segs);
  TEST_ASSERT_EQUAL_UINT16(2, b.recs);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_shown_for_five_seconds_then_released);
  RUN_TEST(test_hidden_time_stands_still);
  RUN_TEST(test_hidden_at_most_two_minutes);
  RUN_TEST(test_other_keys_pass_while_shown);
  RUN_TEST(test_tab_then_enter_holds_them);
  RUN_TEST(test_back_or_another_key_lets_them_go);
  RUN_TEST(test_a_question_nobody_answers_goes_away);
  RUN_TEST(test_offline_counts_recordings_this_mac_may_see);
  return UNITY_END();
}
