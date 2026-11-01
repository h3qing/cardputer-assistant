#include "theme_anim.h"

#include "theme_palette.h"

namespace cpa {
namespace theme {
namespace {

constexpr uint8_t kLoopPoses = 4;
constexpr uint8_t kSlideT[kSlideFrames] = {45, 85, 100};  // percent of the way
constexpr uint8_t kKnobT[kSlideFrames] = {75, 90, 100};

int16_t eased(int16_t from, int16_t to, uint8_t frame, const uint8_t (&t)[kSlideFrames]) {
  if (frame >= kSlideFrames - 1) return to;
  return static_cast<int16_t>(from + (to - from) * t[frame] / 100);
}

bool listening(MandoAct a) { return a == MandoAct::Listen || a == MandoAct::Tilt; }

// Listen and Tilt are one act with two faces: switching keeps the ticks and the phase.
bool same_act(MandoAct a, MandoAct b) { return a == b || (listening(a) && listening(b)); }

uint8_t pose_of(MandoAct a, uint16_t ticks, uint32_t phase) {
  const uint8_t loop = static_cast<uint8_t>(phase % kLoopPoses);
  switch (a) {
    case MandoAct::Listen:
    case MandoAct::Tilt: return loop;
    case MandoAct::Sniff: return ticks < kSniffTicks ? loop : 0;
    case MandoAct::Hop: return static_cast<uint8_t>(ticks < kLoopPoses - 1 ? ticks : kLoopPoses - 1);
    default: return 0;
  }
}

}  // namespace

bool tick_due(uint32_t last, uint32_t now) { return now - last >= kAnimTickMs; }

bool same_view(const MandoView& a, const MandoView& b) {
  return a.act == b.act && a.pose == b.pose && a.bubble == b.bubble && a.z == b.z;
}

MandoMotion mando_tick(const MandoMotion& m, MandoAct act) {
  MandoMotion out = m;
  const bool same = same_act(m.shown.act, act);
  out.ticks = same ? static_cast<uint16_t>(m.ticks < 0xFFFF ? m.ticks + 1 : m.ticks) : 0;
  out.phase = m.phase + 1;
  out.shown.act = act;
  out.shown.pose = pose_of(act, out.ticks, out.phase);
  // Only what shows: a bubble on the acts that have one, z's on the dozing one.
  out.shown.bubble = mando_mark(act) != 0 && out.ticks > 0 ? 1 : 0;
  out.shown.z = act == MandoAct::Doze ? static_cast<uint8_t>((out.ticks / kZTicks + kZs) % (kZs + 1)) : kZs;
  return out;
}

MandoView mando_view(const MandoMotion& m, MandoAct act) {
  if (same_act(m.shown.act, act)) return m.shown;
  MandoView v;
  v.act = act;
  v.pose = act == MandoAct::Hop ? 0 : mando_rest_pose(act);
  return v;
}

Rect mando(Canvas& c, int16_t x, int16_t y, MandoSize s, const MandoView& v, Color under) {
  return mando(c, x, y, s, v.act, v.pose, under, v.bubble);
}

uint8_t breath_next(uint8_t step) { return static_cast<uint8_t>((step + 1) % kBreathSteps); }

int16_t slide_at(int16_t from, int16_t to, uint8_t frame) { return eased(from, to, frame, kSlideT); }

int16_t knob_at(int16_t from, int16_t to, uint8_t frame) { return eased(from, to, frame, kKnobT); }

uint8_t slide_frame(uint32_t since_ms) {
  const uint32_t f = since_ms / kSlideMs;
  return static_cast<uint8_t>(f < kSlideFrames - 1 ? f : kSlideFrames - 1);
}

}  // namespace theme
}  // namespace cpa
