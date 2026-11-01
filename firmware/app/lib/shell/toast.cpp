#include "toast.h"

#include <cstring>

#include "text_fit.h"

namespace cpa {
namespace {

// Longest prefix of s that fits ToastText without splitting a character.
size_t fitting(const char* s) {
  const size_t n = std::strlen(s);
  size_t i = 0;
  while (i < n) {
    const Utf8Char ch = utf8_at(s, n, i);
    if (i + ch.len > ToastText::capacity()) break;
    i += ch.len;
  }
  return i;
}

ToastQueue drop_head(const ToastQueue& q, uint32_t now) {
  ToastQueue out = q;
  out.n = 0;
  for (uint8_t i = 1; i < q.n; ++i) out.items[out.n++] = q.items[i];
  if (out.n > 0) out.items[0].shown_ms = now;
  return out;
}

// q without the toasts of one kind; a new head is shown from now.
ToastQueue without(const ToastQueue& q, ToastKind kind, uint32_t now) {
  ToastQueue out = q;
  out.n = 0;
  for (uint8_t i = 0; i < q.n; ++i) {
    if (q.items[i].kind != kind) out.items[out.n++] = q.items[i];
  }
  if (out.n > 0 && q.items[0].kind == kind) out.items[0].shown_ms = now;
  return out;
}

Toast make(ToastLevel level, const char* utf8, uint32_t now, ToastKind kind) {
  Toast t;
  t.level = level;
  t.text = ToastText::from(utf8, fitting(utf8));
  t.shown_ms = now;
  t.at_ms = now;
  t.kind = kind;
  return t;
}

uint8_t first_held(const ToastQueue& q) {
  uint8_t i = 0;
  while (i < q.n && !q.items[i].held) ++i;
  return i;
}

// Room for one more: the oldest toast held while the screen was not seen goes first (it
// is stale), else the head.
ToastQueue make_room(const ToastQueue& q, uint32_t now) {
  if (q.n < kToastMax) return q;
  const uint8_t h = first_held(q);
  if (h == 0 || h >= q.n) return drop_head(q, now);
  ToastQueue out = q;
  out.n = 0;
  for (uint8_t i = 0; i < q.n; ++i) {
    if (i != h) out.items[out.n++] = q.items[i];
  }
  return out;
}

// t goes last, or (fresh: the screen is seen) before the first toast held while it was
// not. A new head is shown from now.
ToastQueue append(const ToastQueue& q, ToastLevel level, const char* utf8, uint32_t now, ToastKind kind) {
  const ToastQueue base = make_room(q, now);
  const uint8_t at = base.unseen || kind == ToastKind::Notice ? base.n : first_held(base);
  ToastQueue out = base;
  out.n = 0;
  for (uint8_t i = 0; i <= base.n; ++i) {
    if (i == at) out.items[out.n++] = make(level, utf8, now, kind);
    if (i < base.n) out.items[out.n++] = base.items[i];
  }
  return out;
}

// The held result to keep once seen: the latest pushed that is not an error (errors stay).
int latest_held_result(const ToastQueue& q) {
  int best = -1;
  for (uint8_t i = 0; i < q.n; ++i) {
    const Toast& t = q.items[i];
    const bool candidate = t.held && t.kind == ToastKind::Result && t.level != ToastLevel::Error;
    if (candidate && (best < 0 || t.at_ms - q.items[best].at_ms < 0x80000000u)) best = i;
  }
  return best;
}

bool queued(const ToastQueue& q, const char* utf8) {
  for (uint8_t i = 0; i < q.n; ++i) {
    if (q.items[i].text.equals(utf8)) return true;
  }
  return false;
}

}  // namespace

ToastQueue toast_push(const ToastQueue& q, ToastLevel level, const char* utf8, uint32_t now) {
  return append(q, level, utf8, now, ToastKind::Notice);
}

ToastQueue toast_result(const ToastQueue& q, ToastLevel level, const char* utf8, uint32_t now) {
  return append(without(q, ToastKind::Notice, now), level, utf8, now, ToastKind::Result);
}

ToastQueue toast_hint(const ToastQueue& q, ToastLevel level, const char* utf8, uint32_t now) {
  const bool recent = q.last_hint.equals(utf8) && now - q.last_hint_ms < kHintRepeatMs;
  if (recent || queued(q, utf8)) return q;
  ToastQueue out = append(without(q, ToastKind::Notice, now), level, utf8, now, ToastKind::Hint);
  out.last_hint = ToastText::from(utf8, fitting(utf8));
  out.last_hint_ms = now;
  return out;
}

// A retracted hint may come back at once: its condition is new.
ToastQueue toast_retract(const ToastQueue& q, uint32_t now) {
  ToastQueue out = without(q, ToastKind::Hint, now);
  out.last_hint = ToastText{};
  return out;
}

ToastQueue toast_hold(const ToastQueue& q, uint32_t now) {
  ToastQueue out = q;
  out.unseen = true;
  for (uint8_t i = 0; i < out.n; ++i) out.items[i].held = true;
  if (out.n > 0) out.items[0].shown_ms = now;
  return out;
}

// Seen again: of what was held, the errors and the latest other result stay (the lock
// card's RecLast keeps a recording's outcome anyway); held notices and hints, and older
// results, are stale and go. What was pushed while seen stays as it is.
ToastQueue toast_seen(const ToastQueue& q, uint32_t now) {
  if (!q.unseen) return q;
  const int keep = latest_held_result(q);
  ToastQueue out = q;
  out.unseen = false;
  out.n = 0;
  for (uint8_t i = 0; i < q.n; ++i) {
    const Toast& t = q.items[i];
    const bool error = t.kind == ToastKind::Result && t.level == ToastLevel::Error;
    if (!t.held || error || i == keep) out.items[out.n++] = t;
  }
  if (out.n > 0) out.items[0].shown_ms = now;
  return out;
}

ToastQueue toast_tick(const ToastQueue& q, uint32_t now) {
  if (q.n == 0) return q;
  const uint32_t lasts = q.items[0].level == ToastLevel::Error ? kToastErrorMs : kToastMs;
  return now - q.items[0].shown_ms < lasts ? q : drop_head(q, now);
}

const Toast* toast_current(const ToastQueue& q) { return q.n > 0 ? &q.items[0] : nullptr; }

}  // namespace cpa
