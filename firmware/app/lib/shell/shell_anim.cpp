// Shell motion (docs/19 §3.5, §6.5, §7.3-§7.5): one animation tick every theme::kAnimTickMs
// (Mando, the memo's waveform or the meeting's timeline, the big ●'s breath, the lock card's
// z's, or the page's own Module::animate), or a frame of the Tab sheet's or its knob's slide.
// Only while the backlight is On and the screen is up to date (a pending full redraw comes
// first). Each draws only what moved, over the frame on screen, and returns the rectangles to
// push; nothing here marks the screen dirty or touches the power state, and render() draws
// what the last tick showed, so at Dim and dark nothing moves.
#include "lock_screen.h"
#include "rec_overlay.h"
#include "shell.h"

namespace cpa {
namespace {

void add(AnimFrame& f, const Rect& r) {
  if (r.w <= 0 || r.h <= 0 || f.n == AnimFrame::kMax) return;
  f.rects[f.n++] = r;
}

// The big ● breathes while the recording runs; once it is saving it is drawn still.
bool breathes(RecPhase p) { return p == RecPhase::Recording || p == RecPhase::EndConfirm; }
bool dot_drawn(RecPhase p) { return breathes(p) || p == RecPhase::Finishing; }

}  // namespace

void Shell::set_backlight(Backlight lv) {
  light_ = lv;
  if (lv == Backlight::On) return;
  breath_ = 0;              // the runtime puts the ● back to full Rec
  menu_mo_ = MenuMotion{};  // a slide jumps to its end
}

void Shell::on_level(uint16_t rms) { live_ = theme::level_push(live_, rms, state().rec.stars); }

void Shell::rec_started() {
  live_ = theme::LevelHistory{};
  shown_ = live_;
}

// From frame 0, which the full redraw draws; at rest at once unless the backlight is On.
void Shell::slide(MenuSlide kind, MenuPage from) {
  menu_mo_ = MenuMotion{};
  if (light_ != Backlight::On) return;
  menu_mo_.slide = kind;
  menu_mo_.from = from;
  menu_mo_.frame = 0;
  menu_mo_at_ = now_;
}

AnimFrame Shell::animate(Canvas& c, uint32_t now) {
  AnimFrame f;
  if (dirty_ || light_ != Backlight::On) return f;
  if (menu_open_ && !lock_.locked) {
    f = slide_frame(c, now);
  } else if (theme::tick_due(tick_at_, now)) {
    tick_at_ = now;
    if (lock_.locked) {
      f = doze_tick(c, now);
    } else if (rec_up()) {
      f = rec_tick(c, now);
    } else if (overlay_ == proto::Overlay::None) {
      f = module_tick(c, now);
    }
  }
  // Nothing on screen shows RecDot undimmed: its entry goes back to full Rec unpushed.
  const bool dot = !lock_.locked && !menu_open_ && rec_up() && dot_drawn(state().rec.phase);
  if (breath_ != 0 && !dot) {
    breath_ = 0;
    f.breath = 0;
  }
  return f;
}

AnimFrame Shell::slide_frame(Canvas& c, uint32_t now) {
  AnimFrame f;
  if (menu_mo_.slide == MenuSlide::None) return f;
  const uint8_t due = theme::slide_frame(now - menu_mo_at_);
  if (due <= menu_mo_.frame) return f;
  menu_mo_.frame = due;
  add(f, render_menu_slide(c));
  if (due == theme::kSlideFrames - 1) menu_mo_ = MenuMotion{};  // at rest
  return f;
}

AnimFrame Shell::doze_tick(Canvas& c, uint32_t now) {
  AnimFrame f;
  if (!lock_lit(now) || lock_card_act(state(), now) != theme::MandoAct::Doze) return f;  // only his z's move
  const theme::MandoView was = mando_.shown;
  mando_ = theme::mando_tick(mando_, theme::MandoAct::Doze);
  if (!theme::same_view(was, mando_.shown)) add(f, lock_z_part(c, state(), mando_.shown.z, now));
  return f;
}

// Mando (nodding while the level history hears a voice), the waveform (Warn in a memo's last
// 30 s, docs/20 U15) or the timeline from the level history as it is now, in the room the
// legend left them (gh #53: asked only when they moved), and the big ●'s breath.
AnimFrame Shell::rec_tick(Canvas& c, uint32_t now) {
  AnimFrame f;
  const RecInfo& r = state().rec;
  if (r.phase == RecPhase::Confirm || r.held) return f;  // the held-G0 screen (docs/20 U15) is still
  const theme::MandoView was = mando_.shown;
  mando_ = theme::mando_tick(mando_, rec_mando_act(r.phase, theme::level_voice(live_)));
  if (!theme::same_view(was, mando_.shown)) add(f, rec_mando_part(c, r, mando_.shown));
  if (r.phase == RecPhase::Recording && r.mode == RecMode::Memo && !theme::same_levels(shown_, live_)) {
    add(f, rec_wave_part(c, live_, rec_memo_late(r, now), rec_room(c, state())));
  } else if (r.phase == RecPhase::Recording && r.mode == RecMode::Meeting && !theme::same_line(shown_, live_)) {
    const RecParts parts = rec_line_part(c, shown_, live_, rec_room(c, state()));
    for (uint8_t i = 0; i < parts.n; ++i) add(f, parts.r[i]);
  }
  shown_ = live_;
  rec_breath(f, r);
  return f;
}

// One step on while it breathes; back to full Rec, pushed, once it is drawn still.
void Shell::rec_breath(AnimFrame& f, const RecInfo& r) {
  if (breathes(r.phase)) {
    breath_ = theme::breath_next(breath_);
  } else if (breath_ != 0 && dot_drawn(r.phase)) {
    breath_ = 0;
  } else {
    return;
  }
  f.breath = static_cast<int8_t>(breath_);
  add(f, rec_dot_rect(r));
}

AnimFrame Shell::module_tick(Canvas& c, uint32_t now) {
  AnimFrame f;
  if (Module* m = top()) add(f, m->animate(c, kContentRect, now));
  return f;
}

}  // namespace cpa
