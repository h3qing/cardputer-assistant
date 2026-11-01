// SimDisplay (gh #32): the Display port of hal/m5_display on the host. The runtime draws
// into a 240 x 135 sprite that is never cleared between frames (M5Display::begin fills
// it with Bg once); present() copies it to the panel, which keeps the last pushed frame.
// The runtime redraws the whole screen for every frame (Shell::render), so the □ drawn
// since the previous present() are the □ in the frame presented.
// v0.5 S3 (docs/19 §3.4, §3.6): present(r) copies only r, as the device's clipped push does,
// and breathe() changes what the RecDot and RecDotEdge entries show from then on. The device's
// panel keeps the colour each pixel had when it was pushed, so the panel here keeps the
// breath step's own entries (theme::breath_dot / breath_edge) for those pixels: a shot shows
// what the panel shows, with one palette.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fb.h"
#include "fb_canvas.h"
#include "platform.h"
#include "theme_palette.h"

namespace cpasim {

// Pushes since the last call of SimDisplay::take_pushes (the SPI work, docs/19 §7.5).
struct Pushes {
  uint32_t full = 0;
  uint32_t partial = 0;
  uint32_t partial_px = 0;
};

class SimDisplay : public cpa::Display {
 public:
  SimDisplay()
      : sprite_(cpa::kScreenW, cpa::kScreenH, entry_of(cpa::Color::Bg)),
        canvas_(sprite_),
        panel_(cpa::kScreenW, cpa::kScreenH, entry_of(cpa::Color::Bg)) {}
  SimDisplay(const SimDisplay&) = delete;
  SimDisplay& operator=(const SimDisplay&) = delete;

  cpa::Canvas& canvas() override { return canvas_; }
  void present() override {
    push(cpa::Rect{0, 0, cpa::kScreenW, cpa::kScreenH});
    ++presents_;
    ++pushes_.full;
    ++total_.full;
    panel_missing_ = canvas_.missing_glyphs() - drawn_missing_;
    drawn_missing_ = canvas_.missing_glyphs();
    frame_offscreen_ = canvas_.offscreen_texts() - offscreen_mark_;
    offscreen_mark_ = canvas_.offscreen_texts();
  }
  void present(const cpa::Rect& r) override {
    const int px = push(r);
    ++pushes_.partial;
    pushes_.partial_px += static_cast<uint32_t>(px);
    ++total_.partial;
    total_.partial_px += static_cast<uint32_t>(px);
    offscreen_mark_ = canvas_.offscreen_texts();  // a slide's frames may start off screen
  }
  void breathe(uint8_t step) override { breath_ = static_cast<uint8_t>(step % cpa::theme::kBreathSteps); }

  const Frame& panel() const { return panel_; }  // what the screen shows
  const Frame& sprite() const { return sprite_; }
  uint8_t breath() const { return breath_; }
  // The panel is the sprite as last pushed: every pixel drawn since was pushed too.
  bool panel_is_sprite() const {
    for (size_t i = 0; i < sprite_.pixels().size(); ++i) {
      if (panel_.pixels()[i] != shown(sprite_.pixels()[i])) return false;
    }
    return true;
  }
  uint32_t presents() const { return presents_; }
  const Pushes& total() const { return total_; }  // since boot
  Pushes take_pushes() {
    const Pushes p = pushes_;
    pushes_ = Pushes{};
    return p;
  }
  uint32_t missing_glyphs() const { return panel_missing_; }  // □ in the frame on the panel
  uint32_t han_drawn() const { return canvas_.han_glyphs(); }          // since boot (FbCanvas)
  // Texts past the screen's edge in the last full frame (a slide's partial frames excepted).
  uint32_t frame_offscreen() const { return frame_offscreen_; }
  const std::string& last_offscreen() const { return canvas_.last_offscreen(); }
  uint32_t aa_misses() const { return canvas_.aa_misses(); }   // since boot (RasterCanvas)

 private:
  // The entry the panel gets for a sprite entry: RecDot and its rim as the breath step shows them.
  uint8_t shown(uint8_t e) const {
    if (e == entry_of(cpa::Color::RecDot)) return cpa::theme::breath_dot(breath_);
    return e == cpa::theme::rec_dot_edge() ? cpa::theme::breath_edge(breath_) : e;
  }
  // r cut to the screen, copied; returns the pixels pushed.
  int push(const cpa::Rect& r) {
    const int x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
    const int x1 = r.x + r.w > sprite_.w() ? sprite_.w() : r.x + r.w;
    const int y1 = r.y + r.h > sprite_.h() ? sprite_.h() : r.y + r.h;
    const std::vector<uint8_t>& in = sprite_.pixels();
    uint8_t* out = panel_.data();
    for (int y = y0; y < y1; ++y) {
      for (int x = x0; x < x1; ++x) out[y * sprite_.w() + x] = shown(in[static_cast<size_t>(y * sprite_.w() + x)]);
    }
    return x1 > x0 && y1 > y0 ? (x1 - x0) * (y1 - y0) : 0;
  }

  Frame sprite_;
  FbCanvas canvas_;
  Frame panel_;
  uint8_t breath_ = 0;
  uint32_t presents_ = 0;
  Pushes pushes_;
  Pushes total_;
  uint32_t drawn_missing_ = 0;  // the canvas's count at the last present()
  uint32_t panel_missing_ = 0;
  uint32_t offscreen_mark_ = 0;
  uint32_t frame_offscreen_ = 0;
};

}  // namespace cpasim
