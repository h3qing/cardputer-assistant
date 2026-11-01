#include "theme_mando.h"

#include "txt.h"

namespace cpa {
namespace theme {
namespace {

constexpr int16_t kBubbleW = 9;
constexpr int16_t kBubbleH = 11;  // and a 2 px tail
constexpr int16_t kBubbleIn = 2;  // how far the bubble overlaps the sprite's right edge

struct Poses {
  uint8_t first;  // frame in kMando46 / kMando64
  uint8_t n;
};

constexpr uint8_t kTilted = 2;  // wait 12, the most tilted head: he heard nothing

// NeedsYou is the calm wait 0 with its "!" (mockup 01), so it does not look like NoVoice.
Poses poses46(MandoAct a) {
  switch (a) {
    case MandoAct::Listen: return Poses{gen::kM46Review, gen::kMandoPoses};
    case MandoAct::Tilt: return Poses{gen::kM46Wait, gen::kMandoPoses};
    case MandoAct::Sniff: return Poses{gen::kM46Think, gen::kMandoPoses};
    case MandoAct::Hop: return Poses{gen::kM46Hop, gen::kMandoPoses};
    case MandoAct::NoVoice: return Poses{gen::kM46Wait + kTilted, 1};
    case MandoAct::Doze: return Poses{gen::kM46Doze, 1};
    default: return Poses{gen::kM46Wait, 1};
  }
}

Poses poses64(MandoAct a) {
  switch (a) {
    case MandoAct::Listen: return Poses{gen::kM64Review, gen::kMandoPoses};
    case MandoAct::Tilt: return Poses{gen::kM64Wait, gen::kMandoPoses};
    case MandoAct::Sniff: return Poses{gen::kM64Think, gen::kMandoPoses};
    case MandoAct::NoVoice: return Poses{gen::kM64Wait + kTilted, 1};
    default: return Poses{gen::kM64Wait, 1};
  }
}

int16_t sprite_w(MandoSize s) { return s == MandoSize::M46 ? gen::kMando46W : gen::kMando64W; }
int16_t sprite_h(MandoSize s) { return s == MandoSize::M46 ? gen::kMando46H : gen::kMando64H; }

}  // namespace

uint8_t mando_poses(MandoAct a) { return poses46(a).n; }

uint8_t mando_rest_pose(MandoAct a) {
  switch (a) {
    case MandoAct::Listen:
    case MandoAct::Hop: return 1;  // review 6, hop 6
    case MandoAct::Sniff: return 2;  // think 12
    default: return 0;
  }
}

ArtId mando_art(MandoSize s, MandoAct a, uint8_t pose) {
  const bool small = s == MandoSize::M46;
  const Poses p = small ? poses46(a) : poses64(a);
  const uint8_t base = static_cast<uint8_t>(small ? ArtId::Mando46 : ArtId::Mando64);
  return static_cast<ArtId>(base + p.first + pose % p.n);
}

// docs/21 §6.7, appendix A: his signs, one short word, 48 px at most (SignTxt).
constexpr SignTxt kListening{"在听", "Listen"};
constexpr SignTxt kSniffing{"嗅一嗅…", "Sniff…"};
constexpr SignTxt kNeedsYou{"等你", "You!"};
constexpr SignTxt kSaved{"存好了", "Saved"};
constexpr SignTxt kNoVoice{"没听到", "Huh?"};
constexpr SignTxt kDozing{"打盹中", "Napping"};
constexpr SignTxt kOffline{"离线", "Offline"};
constexpr SignTxt kHere{"在呢", "Here"};

const char* mando_sign(MandoAct a) {
  switch (a) {
    case MandoAct::Listen:
    case MandoAct::Tilt: return tr(kListening);
    case MandoAct::Sniff: return tr(kSniffing);
    case MandoAct::NeedsYou: return tr(kNeedsYou);
    case MandoAct::Hop: return tr(kSaved);
    case MandoAct::NoVoice: return tr(kNoVoice);
    case MandoAct::Doze: return tr(kDozing);
    case MandoAct::Offline: return tr(kOffline);
    default: return tr(kHere);
  }
}

Tone mando_sign_tone(MandoAct a) {
  switch (a) {
    case MandoAct::Idle:
    case MandoAct::Listen:
    case MandoAct::Tilt: return Tone::Info;
    case MandoAct::NeedsYou: return Tone::Warn;
    case MandoAct::Hop: return Tone::Ok;
    default: return Tone::Neutral;
  }
}

char mando_mark(MandoAct a) {
  if (a == MandoAct::NeedsYou) return '!';
  return a == MandoAct::NoVoice ? '?' : 0;
}

void bubble(Canvas& c, int16_t x, int16_t y, char mark, Tone t, Color under, uint8_t step) {
  const Color fill = tone_ink(t).ink;
  const int16_t top = static_cast<int16_t>(step == 0 ? y + 2 : y);
  c.fillRound(Rect{x, top, kBubbleW, kBubbleH}, 4, fill, under);
  c.fill(Rect{static_cast<int16_t>(x + 1), static_cast<int16_t>(top + kBubbleH), 1, 1}, fill);
  c.fill(Rect{x, static_cast<int16_t>(top + kBubbleH + 1), 1, 1}, fill);
  const char text[2] = {mark, '\0'};
  c.text(static_cast<int16_t>(x + 2), static_cast<int16_t>(top - 1), text, over(Color::OnAccent));
}

void heart(Canvas& c, MandoSize s, int16_t x, int16_t y, Color under, uint8_t step) {
  const int16_t bx = static_cast<int16_t>(x + sprite_w(s) - kBubbleIn);
  const int16_t top = static_cast<int16_t>(step == 0 ? y + 2 : y);
  const Color fill = tone_ink(Tone::Rec).ink;
  c.fillRound(Rect{bx, top, kBubbleW, kBubbleH}, 4, fill, under);
  c.fill(Rect{static_cast<int16_t>(bx + 1), static_cast<int16_t>(top + kBubbleH), 1, 1}, fill);
  c.fill(Rect{bx, static_cast<int16_t>(top + kBubbleH + 1), 1, 1}, fill);
  // .X.X.  XXXXX  .XXX.  ..X..
  static const struct {
    int8_t dx, dy, w;
  } kRows[] = {{3, 3, 1}, {5, 3, 1}, {2, 4, 5}, {3, 5, 3}, {4, 6, 1}};
  for (const auto& r : kRows) {
    c.fill(Rect{static_cast<int16_t>(bx + r.dx), static_cast<int16_t>(top + r.dy), r.w, 1}, Color::OnAccent);
  }
}

Rect mando_box(MandoSize s, int16_t x, int16_t y) {
  const int16_t w = sprite_w(s);
  return Rect{x, static_cast<int16_t>(y - 1), static_cast<int16_t>(w - kBubbleIn + kBubbleW),
              static_cast<int16_t>(sprite_h(s) + 1)};
}

Rect mando_sprite(MandoSize s, int16_t x, int16_t y) { return Rect{x, y, sprite_w(s), sprite_h(s)}; }

Rect mando(Canvas& c, int16_t x, int16_t y, MandoSize s, MandoAct a, uint8_t pose, Color under, uint8_t step) {
  c.art(x, y, mando_art(s, a, pose));
  if (a == MandoAct::Offline) c.dim(mando_sprite(s, x, y));  // Bg stays Bg: only he fades
  const char mark = mando_mark(a);
  if (mark != 0) {
    const Tone t = a == MandoAct::NeedsYou ? Tone::Warn : Tone::Neutral;
    bubble(c, static_cast<int16_t>(x + sprite_w(s) - kBubbleIn), y, mark, t, under, step);
  }
  return mando_box(s, x, y);
}

}  // namespace theme
}  // namespace cpa
