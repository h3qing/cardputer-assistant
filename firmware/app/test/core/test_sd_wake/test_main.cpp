// sd_wake (docs/07 §6.7 #3): a software reset during recording can leave the powered card
// inside the transfer it cut off, where CMD0 gets no answer and the mount sees no card.
// Before the driver's first CMD0, hal asks CMD0 itself; only a card that does not answer
// gets the transfer ended (sd_wake.h) and CMD0 again. Each state a reset can leave the
// card in (FakeSdCard) must end idle without a command into a busy or answering card; a
// healthy card costs one CMD0; an absent card must say false quickly.
#include <unity.h>

#include "fake_sd_card.h"
#include "sd_wake.h"

using cpa::rec::sd_wake;
using sdtest::FakeSdCard;
using St = FakeSdCard::St;

void setUp() {}
void tearDown() {}

static void expect_idle(FakeSdCard card, uint32_t violations = 0) {
  TEST_ASSERT_TRUE(sd_wake(card));
  TEST_ASSERT_TRUE(card.state() == St::Idle);
  TEST_ASSERT_EQUAL_UINT32(violations, card.violations);
}

static void test_a_healthy_card_gets_one_cmd0() {
  FakeSdCard card = FakeSdCard::ready();
  TEST_ASSERT_TRUE(sd_wake(card));
  TEST_ASSERT_TRUE(card.state() == St::Idle);
  TEST_ASSERT_TRUE(card.clocks() <= 16);  // ready, CMD0, R1, line high: nothing else
}

static void test_a_multi_block_write_between_blocks_is_stopped() { expect_idle(FakeSdCard::write_wait_token()); }

static void test_a_write_cut_inside_a_block_is_finished_and_stopped() {
  for (const uint16_t left : {514, 513, 300, 16, 2}) {  // right after the token .. a CRC byte
    expect_idle(FakeSdCard::write_in_block(left, true));
    expect_idle(FakeSdCard::write_in_block(left, false));
  }
  // One byte left: the first byte of step 0 ends the block, so its CMD0 meets the data
  // response. The card ignores it (nothing tells a receiving card from an idle one).
  expect_idle(FakeSdCard::write_in_block(1, true), 1);
  expect_idle(FakeSdCard::write_in_block(1, false), 1);
}

static void test_a_busy_card_is_waited_out() {
  expect_idle(FakeSdCard::busy(2300, St::WaitToken));  // a block programmed, more to come
  expect_idle(FakeSdCard::busy(2300, St::Ready));
}

static void test_a_multi_block_read_is_stopped_with_cmd12() {
  for (const uint16_t pos : {0, 1, 2, 300, 513, 515}) expect_idle(FakeSdCard::read_stream(pos));
}

static void test_no_card_is_false_within_a_few_hundred_bytes() {
  FakeSdCard card = FakeSdCard::absent(0xFF);
  TEST_ASSERT_FALSE(sd_wake(card));
  TEST_ASSERT_TRUE(card.clocks() <= 600);  // about 12 ms at the 400 kHz init clock
}

static void test_a_line_held_low_gives_up_within_the_budget() {
  FakeSdCard card = FakeSdCard::absent(0x00);
  TEST_ASSERT_FALSE(sd_wake(card, 500));
  TEST_ASSERT_TRUE(card.ms() <= 520);
}

static void test_a_card_busy_past_the_budget_is_left_alone() {
  FakeSdCard card = FakeSdCard::write_wait_token();
  card.busy_bytes = 30000;  // 600 ms
  TEST_ASSERT_FALSE(sd_wake(card, 500));
  TEST_ASSERT_TRUE(card.ms() <= 520);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_healthy_card_gets_one_cmd0);
  RUN_TEST(test_a_multi_block_write_between_blocks_is_stopped);
  RUN_TEST(test_a_write_cut_inside_a_block_is_finished_and_stopped);
  RUN_TEST(test_a_busy_card_is_waited_out);
  RUN_TEST(test_a_multi_block_read_is_stopped_with_cmd12);
  RUN_TEST(test_no_card_is_false_within_a_few_hundred_bytes);
  RUN_TEST(test_a_line_held_low_gives_up_within_the_budget);
  RUN_TEST(test_a_card_busy_past_the_budget_is_left_alone);
  return UNITY_END();
}
