// Canvas (05 §5.4): the only drawing surface modules and the shell see. Four calls:
// fill, text, textWidth, lineHeight. hal/m5_canvas implements it with M5GFX
// (efontCN_12, missing glyphs drawn as □); tests use FakeCanvas, which records the
// drawn text so assertions can look for key strings. Coordinates are pixels on the
// 240 x 135 screen; text y is the top of the line.
// v0.5 (docs/19 §3.3): rounded fills with one baked edge step, small pictures, dimming, the
// big ●, the big digits and the lock glow; lib/theme draws them (theme::RasterCanvas) and
// gives modules cards, keycaps, pills and Mando.
#pragma once

#include <cstdint>
#include <type_traits>

namespace cpa {

struct Rect {
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
};

// Semantic colours (docs/19 §2.1): WhisperWoof's dark theme. The first eight keep their v0.4
// names and meanings with warm values; Ink, Ink3 and Rec are aliases. Each is one entry of
// the device's 256-colour palette (lib/theme), and only a Color can be drawn: an integer,
// such as a v0.4 RGB565 constant, does not convert.
enum class Color : uint8_t {
  Bg, Fg, Dim, Accent, Ok, Warn, Error, Select,
  Sheet, Card, Raised, Line, Cap, Rim, CardRim, Ink2, Accent2, OnAccent, Live,
  OkBg, WarnBg, RecBg, InfoBg, RecDot,
  Ink = Fg, Ink3 = Dim, Rec = Error,
};
constexpr uint8_t kColorCount = 24;
static_assert(!std::is_convertible<uint16_t, Color>::value, "a Color is a token, never an RGB565 value");
static_assert(!std::is_convertible<int, Color>::value, "a Color is a token, never a palette index");

// Corners for Canvas::fillRound.
constexpr uint8_t kCornerTL = 1;
constexpr uint8_t kCornerTR = 2;
constexpr uint8_t kCornerBL = 4;
constexpr uint8_t kCornerBR = 8;
constexpr uint8_t kCornersAll = 15;

// A small picture the theme ships; lib/theme/theme_art.h names them.
enum class ArtId : uint8_t;

// The big digits (docs/19 §2.4): Nunito ExtraBold, tabular, 0-9 and ':' only. Timer is 30 px
// (the recording pages), Clock 34 px (the lock card).
enum class DigitFont : uint8_t { Timer, Clock };

struct Style {
  Color fg = Color::Fg;
  Color bg = Color::Bg;
};

class Canvas {
 public:
  virtual ~Canvas() = default;
  virtual void fill(const Rect& r, Color c) = 0;
  virtual void text(int16_t x, int16_t y, const char* utf8, const Style& s) = 0;
  virtual int16_t textWidth(const char* utf8) const = 0;
  virtual int16_t lineHeight() const = 0;

  // v0.5 (docs/19 §3.3). The defaults draw square corners, a square dot, digits as text, no
  // picture, no dimming and no glow, so a v0.4 Canvas (a test double, say) still builds.
  // r filled with paint; the chosen corners rounded with radius, their edge pixels one
  // baked step between paint and under (what is drawn there already).
  virtual void fillRound(const Rect& r, uint8_t radius, Color paint, Color under, uint8_t corners = kCornersAll) {
    (void)radius;
    (void)under;
    (void)corners;
    fill(r, paint);
  }
  // r filled with the baked edge step between paint and under: the colour fillRound and dot
  // give partly covered pixels (v0.5 S3: the waveform's chipped bar ends).
  virtual void fillEdge(const Rect& r, Color paint, Color under) {
    (void)under;
    fill(r, paint);
  }
  // The picture with its top left at (x, y); its clear pixels leave the screen as it is.
  virtual void art(int16_t x, int16_t y, ArtId id) {
    (void)x;
    (void)y;
    (void)id;
  }
  // Every pixel in r shown dimmed (the page under the Tab sheet).
  virtual void dim(const Rect& r) { (void)r; }
  // A filled circle of `diameter` px centred on the pixel corner (cx, cy), its rim one baked
  // step between paint and under (the recording pages' big ●).
  virtual void dot(int16_t cx, int16_t cy, uint8_t diameter, Color paint, Color under) {
    (void)under;
    const int16_t r = static_cast<int16_t>(diameter / 2);
    fill(Rect{static_cast<int16_t>(cx - r), static_cast<int16_t>(cy - r), diameter, diameter}, paint);
  }
  // The digits and colons of `chars` with the top left of the first cell at (x, y), smoothed
  // from ink to under; any other character is skipped. Returns the width (digitsWidth).
  virtual int16_t digits(int16_t x, int16_t y, const char* chars, DigitFont f, Color ink, Color under) {
    (void)f;
    text(x, y, chars, Style{ink, under});
    return textWidth(chars);
  }
  virtual int16_t digitsWidth(const char* chars, DigitFont f) const {
    (void)f;
    return textWidth(chars);
  }
  // The lock screen's still corner glow, on the pixels in r that are still Bg.
  virtual void glow(const Rect& r) { (void)r; }
};

// Screen layout (05 §5.6): 12 px status bar, 240 x 111 content, 12 px hint bar.
constexpr int16_t kScreenW = 240;
constexpr int16_t kScreenH = 135;
constexpr int16_t kBarH = 12;
constexpr Rect kStatusRect{0, 0, kScreenW, kBarH};
constexpr Rect kContentRect{0, kBarH, kScreenW, kScreenH - 2 * kBarH};
constexpr Rect kHintRect{0, kScreenH - kBarH, kScreenW, kBarH};

}  // namespace cpa
