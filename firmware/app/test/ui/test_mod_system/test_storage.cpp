// docs/28 S1a: System -> Storage. "Send again" and "Clear kept" open a box whose cursor is
// on "Cancel", so Enter alone never sends anything again nor clears (review 4); "To this Mac"
// undoes a deliver again for the Mac on the link (review 6).
#include <unity.h>

#include "test_system_rig.h"

using namespace cpa;
using namespace uitest;

namespace {

FakeContext kept_state() {
  FakeContext ctx = filled();
  ctx.app.rec.avail = true;
  ctx.app.rec.files_n = 3;
  ctx.app.rec.free_known = true;
  ctx.app.rec.free_mib = 489472;  // 478G
  ctx.app.rec.kept.segs = 12;
  ctx.app.rec.kept.batch_recs = 2;
  ctx.app.rec.kept.batch_host = 0x3F2A91C4;
  ctx.app.rec.kept.oldest_s = 1800000000 - 5 * 86400 - 100;
  ctx.app.rec.kept.held_here = 4;
  ctx.app.time.known = true;
  ctx.app.time.offset_ms = 1800000000000LL - ctx.now;
  return ctx;
}

struct StoreTab {
  FakeContext ctx = kept_state();
  SystemModule m;
  StoreTab() {
    m.begin(ctx);
    m.onEnter(0);
    m.onKey(chr(','));
  }
};

}  // namespace

void test_storage_rows_and_facts_in_chinese() {
  StoreTab s;
  FakeCanvas c;
  s.m.render(c, kContentRect);
  TEST_ASSERT_TRUE(c.has("再交一次"));
  TEST_ASSERT_TRUE(c.has("\xC2\xB7" "3F2A 的 2 次"));  // review 3: from whom, not to whom
  TEST_ASSERT_TRUE(c.has("给这台 Mac"));
  TEST_ASSERT_TRUE(c.has("4 段"));
  TEST_ASSERT_TRUE(c.has("清掉留底"));
  TEST_ASSERT_TRUE(c.has("12 段"));
  TEST_ASSERT_TRUE(c.has("留底 12 段 · 最旧 5 天"));
  TEST_ASSERT_TRUE(c.has("待传 3 · 空闲 478G"));
  TEST_ASSERT_TRUE(c.has("留底未加密"));
  expect_inside(c);
}

void test_storage_rows_and_facts_in_english() {
  LangScope en(Lang::En);
  StoreTab s;
  FakeCanvas c;
  s.m.render(c, kContentRect);
  TEST_ASSERT_TRUE(c.has("Send again"));
  TEST_ASSERT_TRUE(c.has("from \xC2\xB7" "3F2A: 2"));
  TEST_ASSERT_TRUE(c.has("To this Mac"));
  TEST_ASSERT_TRUE(c.has("Kept 12 files · oldest 5 d"));
  TEST_ASSERT_TRUE(c.has("To sync 3 · free 478G"));
  expect_inside(c);
  s.ctx.app.rec.kept.segs = 1;  // one: no plural
  FakeCanvas one;
  s.m.render(one, kContentRect);
  TEST_ASSERT_TRUE(one.has("Kept 1 file ·") && one.has("1 file") && !one.has("1 files"));
}

// Review 4: Opt+M , Enter used to send a batch again at once; now a box on "Cancel".
void test_send_again_opens_a_box_on_cancel() {
  StoreTab s;
  TEST_ASSERT_TRUE(s.m.onKey(enter()));
  TEST_ASSERT_TRUE(s.m.ui().confirm && !s.m.ui().yes);
  TEST_ASSERT_TRUE(s.ctx.kept_asks.empty());
  FakeCanvas c;
  s.m.render(c, kContentRect);
  TEST_ASSERT_TRUE(c.has("把 2 次再交给别的 Mac？"));
  TEST_ASSERT_TRUE(c.has("\xC2\xB7" "3F2A 不会再拿到"));
  TEST_ASSERT_FALSE(c.has("待传 3"));  // the facts below the box are not half drawn under its edge
  expect_inside(c);
  s.m.onKey(enter());  // Cancel
  TEST_ASSERT_TRUE(s.ctx.kept_asks.empty());
  s.m.onKey(enter());
  s.m.onKey(chr('/'));
  s.m.onKey(enter());
  TEST_ASSERT_EQUAL_UINT32(1, s.ctx.kept_asks.size());
  TEST_ASSERT_TRUE(s.ctx.kept_asks[0] == KeptAsk::Again);
}

// Review 6: the undo, for the Mac on the link; with nothing kept from it, it says so.
void test_give_to_this_mac() {
  StoreTab s;
  s.m.onKey(chr('.'));
  TEST_ASSERT_TRUE(s.m.ui().store == StoreRow::Give);
  s.m.onKey(enter());
  TEST_ASSERT_EQUAL_UINT32(1, s.ctx.kept_asks.size());
  TEST_ASSERT_TRUE(s.ctx.kept_asks[0] == KeptAsk::Give);
  s.ctx.app.rec.kept.held_here = 0;
  s.m.onKey(enter());
  TEST_ASSERT_EQUAL_UINT32(1, s.ctx.kept_asks.size());
  TEST_ASSERT_EQUAL_UINT32(1, s.ctx.hints.size());
}

// Review 8: the facts step by a row, so the Chinese lines never touch.
void test_storage_facts_do_not_touch() {
  StoreTab s;
  FakeCanvas c;
  s.m.render(c, kContentRect);
  const TextOp* kept = c.find("留底 12 段");
  const TextOp* wait = c.find("待传 3");
  const TextOp* note = c.find("留底未加密");
  TEST_ASSERT_TRUE(kept && wait && note);
  TEST_ASSERT_TRUE(wait->y - kept->y >= 14 && note->y - wait->y >= 14);
}

void test_clear_opens_a_box_on_cancel() {
  StoreTab s;
  s.m.onKey(chr('.'));
  s.m.onKey(chr('.'));
  s.m.onKey(enter());
  TEST_ASSERT_TRUE(s.m.ui().confirm && !s.m.ui().yes);
  s.m.onKey(enter());  // Cancel
  TEST_ASSERT_FALSE(s.m.ui().confirm);
  TEST_ASSERT_TRUE(s.ctx.kept_asks.empty());
  s.m.onKey(enter());
  s.m.onKey(chr('/'));
  TEST_ASSERT_TRUE(s.m.ui().yes);
  FakeCanvas c;
  s.m.render(c, kContentRect);
  TEST_ASSERT_TRUE(c.has("清掉 12 段留底？"));
  TEST_ASSERT_TRUE(c.has("Mac 上已存的不受影响"));
  expect_inside(c);
  s.m.onKey(enter());
  TEST_ASSERT_EQUAL_UINT32(1, s.ctx.kept_asks.size());
  TEST_ASSERT_TRUE(s.ctx.kept_asks[0] == KeptAsk::Clear);
  TEST_ASSERT_FALSE(s.m.ui().confirm);
}

void test_back_and_stray_keys_never_clear() {
  StoreTab s;
  s.m.onKey(chr('.'));
  s.m.onKey(chr('.'));
  s.m.onKey(enter());
  s.m.onKey(chr('/'));
  TEST_ASSERT_TRUE(s.m.onKey(chr('q')));  // taken: nothing leaves the box by accident
  TEST_ASSERT_TRUE(s.m.ui().confirm);
  s.m.onKey(chr('`'));
  TEST_ASSERT_FALSE(s.m.ui().confirm);
  TEST_ASSERT_TRUE(s.ctx.kept_asks.empty());
}

void test_nothing_kept_and_busy_say_so() {
  StoreTab s;
  s.ctx.app.rec.kept = KeptView{};
  s.m.onKey(chr('.'));
  s.m.onKey(chr('.'));
  s.m.onKey(enter());
  TEST_ASSERT_FALSE(s.m.ui().confirm);
  TEST_ASSERT_EQUAL_UINT32(1, s.ctx.hints.size());
  s.ctx.app.rec.kept = kept_state().app.rec.kept;
  s.ctx.app.rec.kept.busy = true;
  s.m.onKey(chr(';'));
  s.m.onKey(chr(';'));
  s.m.onKey(enter());
  s.m.onKey(chr('/'));
  s.m.onKey(enter());
  TEST_ASSERT_TRUE(s.ctx.kept_asks.empty());
  TEST_ASSERT_EQUAL_UINT32(2, s.ctx.hints.size());
  s.ctx.app.rec.kept.held_here = 0;
  FakeCanvas c;
  s.m.render(c, kContentRect);
  TEST_ASSERT_TRUE(c.has("没有"));
}

// No batch (or one handed to a Mac without a host_id): Enter on "Send again" says so, asks nothing.
void test_send_again_without_a_batch_says_so() {
  StoreTab s;
  s.ctx.app.rec.kept.batch_host = 0;
  s.m.onKey(enter());
  TEST_ASSERT_TRUE(s.ctx.kept_asks.empty());
  TEST_ASSERT_EQUAL_UINT32(1, s.ctx.hints.size());
}

void test_storage_box_fits_in_english() {
  LangScope en(Lang::En);
  StoreTab s;
  s.ctx.app.rec.kept.segs = 1264;
  s.m.onKey(chr('.'));
  s.m.onKey(chr('.'));
  s.m.onKey(enter());
  FakeCanvas c;
  s.m.render(c, kContentRect);
  TEST_ASSERT_TRUE(c.has("Clear 1264 kept files?"));
  expect_inside(c);
  s.m.onKey(chr('`'));
  s.m.onKey(chr(';'));
  s.m.onKey(chr(';'));
  s.m.onKey(enter());
  FakeCanvas a;
  s.m.render(a, kContentRect);
  TEST_ASSERT_TRUE(a.has("Send 2 to another Mac?"));
  expect_inside(a);
}
