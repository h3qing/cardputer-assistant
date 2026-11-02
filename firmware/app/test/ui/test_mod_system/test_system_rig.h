// Shared by the Settings tests: a filled context, a tab drawn, and "every text is on
// the screen" measured with efontCN_12's real widths (txt_rules.h).
#pragma once

#include <unity.h>

#include "support/fake_canvas.h"
#include "support/fake_context.h"
#include "support/fixtures.h"
#include "system_module.h"
#include "txt_rules.h"

namespace uitest {

inline FakeContext filled() {
  FakeContext ctx;
  ctx.app = demo_state();
  ctx.app.device.boot = cpa::BootIdStr::from("3QZK1M7-0000C");
  ctx.app.device.heap_free = 184320;
  ctx.app.device.heap_min = 174080;
  ctx.app.device.heap_max_block = 122880;
  ctx.app.device.stack_hwm = 6144;
  ctx.app.device.ghost_n = 2;
  ctx.app.device.kb_reinit_n = 1;
  ctx.app.device.bat_mv = 4150;
  ctx.app.outbox.pending = 3;
  ctx.app.outbox.inflight = 1;
  ctx.app.outbox.bad = 2;
  ctx.app.link.mode = cpa::LinkMode::Ready;
  ctx.app.link.last_rx_ms = 10000;
  ctx.info.fw = cpa::FixedStr<31>::from("cpa-fw/0.1.0");
  ctx.info.dev = cpa::FixedStr<12>::from("020000000001");
  ctx.info.rst = cpa::FixedStr<16>::from("poweron");
  ctx.info.host_app = cpa::FixedStr<32>::from("cpad/0.1.0");
  ctx.info.ids_ok = true;
  ctx.now = 15000;
  return ctx;
}

// m begun on ctx and entered, on `tab`, drawn.
inline FakeCanvas render_tab(cpa::SystemModule& m, FakeContext& ctx, cpa::SysTab tab) {
  m.begin(ctx);
  m.onEnter(0);
  if (tab == cpa::SysTab::Device) m.onKey(chr('/'));
  if (tab == cpa::SysTab::Storage) m.onKey(chr(','));  // docs/28: one `,` from Settings
  FakeCanvas c;
  m.render(c, cpa::kContentRect);
  return c;
}

// Every text inside the content area, 4 px from the right edge, in real font px.
inline void expect_inside(const FakeCanvas& c) {
  for (const TextOp& t : c.texts) {
    const int32_t right = t.x + cpa::i18n::text_width(t.text.c_str());
    TEST_ASSERT_TRUE_MESSAGE(right <= cpa::kScreenW - 4, t.text.c_str());
    TEST_ASSERT_TRUE_MESSAGE(t.y >= cpa::kContentRect.y, t.text.c_str());
    TEST_ASSERT_TRUE_MESSAGE(t.y + 12 <= cpa::kHintRect.y, t.text.c_str());
  }
}

}  // namespace uitest
