#include "outbox_scan.h"

#include <cstring>

namespace cpa {
namespace {

bool ends_with(const char* s, size_t n, const char* suffix) {
  const size_t k = std::strlen(suffix);
  return n >= k && std::memcmp(s + n - k, suffix, k) == 0;
}

// Records a cleanup action; false (counted as deferred) when the list is full.
bool add_action(ScanState& s, ScanActionKind kind, const MonoId& id) {
  if (s.n >= kScanActionsMax) {
    s.deferred++;
    return false;
  }
  s.actions[s.n].kind = kind;
  s.actions[s.n].id = id;
  s.n++;
  return true;
}

void add_bad(ScanState& s, const MonoId& id) {
  const auto dropped = outbox_insert_bad(s.core, id);
  if (dropped.has) add_action(s, ScanActionKind::RemoveBad, dropped.value);
}

// Keeps the oldest kOutboxCapacity ids; a newer one is left on disk (deferred).
void add_pending(ScanState& s, const MonoId& id) {
  if (outbox_insert_pending(s.core, id)) return;
  s.deferred++;
  s.json_deferred = true;
  if (!(id < s.core.ids[s.core.n - 1])) return;
  s.core.n = static_cast<uint16_t>(s.core.n - 1);  // the newest stays on disk instead
  outbox_insert_pending(s.core, id);
}

void add_tmp(ScanState& s, const MonoId& id) {
  if (s.n_tmp >= kScanTmpMax) {
    s.deferred++;
    return;
  }
  s.tmp[s.n_tmp++] = id;
}

// Bad content: never sent; moved to bad/ unless the action list is full, in which case
// it stays in /outbox untracked (not counted in bad/) and every .tmp is kept.
void reject_json(ScanState& s, const MonoId& id) {
  outbox_forget(s.core, id);
  if (add_action(s, ScanActionKind::MoveToBad, id)) {
    add_bad(s, id);
  } else {
    s.json_deferred = true;
  }
}

// FAT only: never committed, so never shown as saved -> delete it.
void drop_empty_json(ScanState& s, const MonoId& id) {
  outbox_forget(s.core, id);
  if (!add_action(s, ScanActionKind::RemoveJson, id)) s.json_deferred = true;
}

void defer_json(ScanState& s, const MonoId& id) {
  outbox_forget(s.core, id);
  s.deferred++;
  s.json_deferred = true;
}

bool moved_to_bad(const ScanState& s, const MonoId& id) {
  for (uint8_t i = 0; i < s.n; ++i) {
    if (s.actions[i].kind == ScanActionKind::MoveToBad && s.actions[i].id == id) return true;
  }
  return false;
}

}  // namespace

ScanClass scan_classify(const char* name, bool is_dir) {
  ScanClass c;
  const size_t n = std::strlen(name);
  if (is_dir) {
    c.kind = std::strcmp(name, "bad") == 0 ? ScanName::Dir : ScanName::Other;
    return c;
  }
  const bool json = ends_with(name, n, ".json");
  const bool tmp = ends_with(name, n, ".tmp");
  const auto id = (json || tmp) ? mono_parse(name, n - (json ? 5 : 4)) : Opt<MonoId>{};
  if (!id.has) return c;
  c.kind = json ? ScanName::Json : ScanName::Tmp;
  c.id = id.value;
  return c;
}

void scan_entry(ScanState& s, ScanEntry entry, const MonoId& id) {
  switch (entry) {
    case ScanEntry::Json: return add_pending(s, id);
    case ScanEntry::Tmp: return add_tmp(s, id);
    case ScanEntry::Bad: return add_bad(s, id);
    case ScanEntry::JsonBad: return reject_json(s, id);
    case ScanEntry::JsonUnread: return defer_json(s, id);
    case ScanEntry::JsonEmpty: return s.fat ? drop_empty_json(s, id) : reject_json(s, id);
  }
}

void scan_finish(ScanState& s) {
  for (uint8_t i = 0; i < s.n_tmp; ++i) {
    const MonoId id = s.tmp[i];
    if (s.fat || outbox_contains(s.core, id) || moved_to_bad(s, id)) {
      s.broken++;
    } else if (s.json_deferred) {
      s.deferred++;
    } else {
      add_action(s, ScanActionKind::RemoveTmp, id);
    }
  }
}

}  // namespace cpa
