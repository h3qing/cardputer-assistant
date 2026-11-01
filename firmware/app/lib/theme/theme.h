// The public theme (docs/19 §4): sizes, tones and the pieces every page is built from
// (cards, keycaps, pills, tiles, the hint bar and toasts), drawn on any Canvas. The shell,
// the modules and third-party apps (docs/16) use these instead of picking colours, so the
// look stays one. Pure: no Arduino, M5 or esp headers.
#pragma once

#include <cstdint>

#include "canvas.h"
#include "hint_layout.h"

namespace cpa {
namespace theme {

// Sizes (docs/19 §2.4).
constexpr int16_t kGutter = 4;  // screen edge to content
constexpr int16_t kCapH = 12;   // keycaps, status bar capsules, count pills
constexpr int16_t kSignH = 13;  // Mando's sign, the lock card's chips
constexpr uint8_t kRadiusTile = 5;
constexpr uint8_t kRadiusCard = 6;
constexpr uint8_t kRadiusSheet = 8;

// Text drawn over a theme shape: the glyphs only (fg == bg draws no cell background), so
// the shape's rounded edges stay.
constexpr Style over(Color ink) { return Style{ink, ink}; }

// A tone is one (fill, ink) pair; each reads at >= 4.5:1 (test_theme_palette).
enum class Tone : uint8_t { Neutral, Info, Ok, Warn, Rec, Accent };
struct ToneInk {
  Color fill;
  Color ink;
};
// Neutral Raised/Ink2 · Info InfoBg/Accent · Ok OkBg/Ok · Warn WarnBg/Warn · Rec RecBg/Rec ·
// Accent Accent/OnAccent.
ToneInk tone_ink(Tone t);

// A card: a rounded fill with an edge that shows on the panel (a fill alone, 1.15:1 on Bg,
// does not): Plain is Card with a lighter 1 px top edge (CardRim), Selected is Select inside
// a Live outline, Sheet is the lock card's Sheet with a Rim top edge.
enum class CardKind : uint8_t { Plain, Selected, Sheet };
Color card_fill(CardKind k);  // what text on the card is drawn over
void card(Canvas& c, const Rect& r, CardKind k = CardKind::Plain, Color under = Color::Bg,
          uint8_t radius = kRadiusCard);

// [Enter]: a key on a Cap. G0 is Accent in hint bars; selected rows use Accent2 / OnAccent.
struct Keycap {
  Color cap = Color::Cap;
  Color ink = Color::Ink2;
  Color under = Color::Bg;
  uint8_t radius = 2;
  int16_t pad = 2;    // each side of the key
  int16_t min_w = 0;  // a narrower cap is widened and its key centred
};
int16_t keycap_width(const Canvas& c, const char* key, const Keycap& k = Keycap{});
int16_t keycap(Canvas& c, int16_t x, int16_t y, const char* key, const Keycap& k = Keycap{});  // its width

// A capsule with a word in it (跑1, !2, 等你 1); taller than kCapH, the text sits a pixel
// lower, as in the mockups. Returns its width.
struct PillSize {
  int16_t h = kCapH;
  int16_t pad = 4;  // each side of the text
};
int16_t pill_width(const Canvas& c, const char* text, PillSize s = PillSize{});
int16_t pill(Canvas& c, int16_t x, int16_t y, const char* text, Tone t, Color under, PillSize s = PillSize{});

// A one-word sign centred on xc (Mando's 在听 / 打盹中, docs/19 §7): a kSignH capsule,
// moved in to stay kSignEdge px inside the screen (docs/21 §6.9). Returns where it was drawn.
constexpr int16_t kSignEdge = 2;
Rect sign(Canvas& c, int16_t xc, int16_t y, const char* text, Tone t, Color under = Color::Bg);

// WhisperWoof's live words: something that has not landed yet (存到 [未归属]), a 14 px
// Select capsule in a Live outline with Accent text. Returns its width.
constexpr int16_t kLiveH = 14;
int16_t live_capsule(Canvas& c, int16_t x, int16_t y, const char* text, Color under = Color::Bg);

// The 3 px bar left of a card that needs you (never colour alone: docs/19 §2.5).
void stripe(Canvas& c, int16_t x, int16_t y, int16_t h, Tone t, Color under);

// A page title, bold by drawing it twice a pixel apart. Returns its width.
int16_t title(Canvas& c, int16_t x, int16_t y, const char* utf8, Color under = Color::Bg);

// A home tile (kHomeTiles; docs/16's AppTile): [key] label and a right-aligned badge.
struct TileSpec {
  const char* key;
  const char* label;
  const char* badge;  // "" for none
  Color badge_ink;
  bool muted = false;  // it cannot act now (docs/20 U16): its label in Ink3, as offline items are
};
void tile(Canvas& c, const Rect& r, const TileSpec& t, bool selected);

// The hint bar (kHintRect): the hint() string as keycaps (hint_layout.h), one-character keys
// on 12 px caps, at the roomiest of three spacings that fits; a hint that does not fit is
// drawn as v0.4 drew it, one line of Ink3. hint_fits says which (docs/16: an app's hint must
// fit as keycaps).
void hint_bar(Canvas& c, const char* hint);
bool hint_fits(const Canvas& c, const char* hint);
// The same for a structured hint (hint_line, docs/21 §6.6): its parts as keycaps at the
// roomiest spacing that fits, else the text as one line of Ink3.
void hint_bar(Canvas& c, const HintLine& line);
bool hint_fits(const Canvas& c, const HintLine& line);

// A line in the hint format anywhere ("Enter 开始    ` 取消" in a confirm card): the keys as
// caps on `under`, the words in `ink`, at the roomiest spacing. Returns its width.
int16_t keys_text(Canvas& c, int16_t x, int16_t y, const char* text, Color ink, Color under);

// A toast over the hint bar: one capsule in the tone's colours, 2 px in from each side, its
// text 6 px in from each end; a longer text is cut ("…"), so toasts are written to fit
// kToastTextW (docs/20 B6, test_text_width).
constexpr int16_t kToastX = 2;
constexpr int16_t kToastPad = 6;
constexpr int16_t kToastTextW = kScreenW - 2 * kToastX - 2 * kToastPad;  // 224
void toast_bar(Canvas& c, const char* text, Tone t);

}  // namespace theme
}  // namespace cpa
