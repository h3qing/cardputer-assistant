// Status bar and hint bar (04 US-11, 05 §5.6). Link glyphs as specified, except that
// efontCN_12 has no ⟳ (U+27F3), ◐ (U+25D0) or ⚠ (U+26A0): the bar uses ◎, ◇ and ※,
// which the font does have (checked against M5GFX 0.2.33's lgfx_efont_cn_12 table).
//   ● online (Ready)   ◎ sync (Ready, outbox not empty)   ◇ no reply (USB host, no daemon)
//   ○ offline   ◇ mismatch (USB host whose hello had another major version: 04 US-04 AC3, E7)
// v0.3 (docs/11 §4.2): ●mm:ss while recording, RN while N segments wait for sync.
// v0.5 (docs/19 §6.1): "● mm:ss" sits in a fixed left slot (a RecBg capsule); online is
// Mando's head instead of "● online" (syncing: the head and ◎), so the bar shows ● only while
// recording; !N and RN only when not zero; the weekday left of the clock when it fits. The
// hint bar draws the hint as keycaps (theme::hint_bar).
// docs/20 B3: no count at zero (↑0 neither); ※k for kKbWarnShowMs after its count rose; ※SD
// while the device records but has no usable card; the battery yellow under 30 %, red from
// 17 %; the left group gets what the right one leaves, the weekday giving way first, then
// RN, ↑N, ※k, ※SD (●, the link, !N, the clock and the battery always stay). D8: "● Wait"
// in Warn in the ● slot from the press of a G0 that would record.
#pragma once

#include <cstdint>

#include "module.h"

namespace cpa {

enum class LinkGlyph : uint8_t { Online, Syncing, NoDaemon, Incompatible, Offline };

// docs/20 B3: ※k shows this long after its count rose; the battery is yellow under
// kBatWarnPct (the 3.55 V warning reads 29 %) and red from kBatStopPct (the 3.45 V stop line).
constexpr uint32_t kKbWarnShowMs = 10000;
constexpr int kBatWarnPct = 30;
constexpr int kBatStopPct = 17;

LinkGlyph link_glyph(const AppState& s);
const char* link_glyph_text(LinkGlyph g);
FixedStr<8> clock_text(const TimeInfo& t, uint32_t now);  // "13:42" or "--:--"
int battery_pct(uint16_t mv);                              // -1 when unknown
Color battery_ink(int pct);                                // Ink3, Warn under 30 %, Error from 17 %

void render_status_bar(Canvas& c, const AppState& s, uint32_t now);
void render_hint_bar(Canvas& c, const char* hint);

}  // namespace cpa
