#include "system_logic.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "system_texts.h"

namespace cpa {
namespace {

void add(SystemPage& p, bool warn, const char* fmt, ...) {
  if (p.n >= kSystemLines) return;
  char buf[SystemLine::capacity() + 1];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  p.lines[p.n] = SystemLine::from(buf);
  p.warn[p.n] = warn;
  const char* space = std::strchr(p.lines[p.n].c_str(), ' ');
  p.key_len[p.n] = static_cast<uint8_t>(space ? space - p.lines[p.n].c_str() : 0);
  p.n++;
}

// "180K" from tenths of a unit: one decimal below 10, whole units from 10 up.
FixedStr<16> tenths_text(uint64_t tenths, char unit) {
  char buf[32];
  const unsigned long whole = static_cast<unsigned long>(tenths / 10);
  if (tenths >= 100) {
    std::snprintf(buf, sizeof buf, "%lu%c", whole, unit);
  } else {
    std::snprintf(buf, sizeof buf, "%lu.%lu%c", whole, static_cast<unsigned long>(tenths % 10), unit);
  }
  return FixedStr<16>::from(buf);
}

// line + "  " + piece, or line unchanged when the whole piece does not fit: a row is
// never cut inside a character.
SystemLine join(const SystemLine& line, const char* piece) {
  const size_t n = std::strlen(piece);
  const size_t sep = line.empty() ? 0 : 2;
  if (n == 0 || line.size() + sep + n > SystemLine::capacity()) return line;
  return (sep ? line.append("  ") : line).append(piece, n);
}

// "<words><n>", e.g. "moved 3".
FixedStr<32> counted(const Txt& words, unsigned n) {
  char buf[40];
  std::snprintf(buf, sizeof buf, "%s%u", tr(words), n);
  return FixedStr<32>::from(buf);
}

FixedStr<24> store_text(const SysInfo& info) {
  using Text = FixedStr<24>;
  const uint64_t sd_bytes = static_cast<uint64_t>(info.sd_mib) << 20;
  switch (info.store) {
    case Store::Sd: return sd_bytes ? Text::from("SD ").append(size_text(sd_bytes).c_str()) : Text::from("SD");
    case Store::Internal: {
      const Text label = Text::from(tr(txt::kInternal));
      return info.cpafs_bytes ? label.append(" ").append(size_text(info.cpafs_bytes).c_str()) : label;
    }
    default: return Text::from(tr(txt::kNoStore));
  }
}

// Store SD 476G  free 412G, or what this boot's migration did in place of the free space.
void add_store(SystemPage& p, const AppState& s, const SysInfo& info) {
  SystemLine line = SystemLine::from(tr(txt::kStore)).append(store_text(info).c_str());
  const bool migrated = info.migrated > 0 || info.migrate_failed > 0;
  if (info.migrated > 0) line = join(line, counted(txt::kMoved, info.migrated).c_str());
  if (info.migrate_failed > 0) line = join(line, counted(txt::kMoveFailed, info.migrate_failed).c_str());
  if (!migrated && info.store == Store::Sd && s.rec.free_known) {
    const FixedStr<16> free = size_text(static_cast<uint64_t>(s.rec.free_mib) << 20);
    line = join(line, FixedStr<24>::from(tr(txt::kFree)).append(free.c_str()).c_str());
  }
  add(p, info.store == Store::None || info.migrate_failed > 0, "%s", line.c_str());
}

// One piece of the warning row from a "%u" pair (or two: the keyboard's).
FixedStr<48> piece(const Txt& t, unsigned a, unsigned b = 0) {
  char buf[64];
  std::snprintf(buf, sizeof buf, tr(t), a, b);
  return FixedStr<48>::from(buf);
}

// The one warning row, most urgent first; the keyboard's counts last (docs/21 §4.5).
SystemLine warning_line(const SysInfo& info, const DeviceInfo& d) {
  SystemLine line;
  if (info.store_errors > 0) line = join(line, piece(txt::kStoreErrors, info.store_errors).c_str());
  line = join(line, store_note_text(info.note));
  if (!info.ids_ok) line = join(line, tr(txt::kNvsError));
  if (info.outbox_broken > 0) line = join(line, piece(txt::kInvariant, info.outbox_broken).c_str());
  if (info.modules_disabled > 0) line = join(line, piece(txt::kDisabledModules, info.modules_disabled).c_str());
  if (d.ghost_n + d.kb_reinit_n > 0) line = join(line, piece(txt::kKeyboard, d.ghost_n, d.kb_reinit_n).c_str());
  return line;
}

}  // namespace

FixedStr<16> kib_text(uint32_t bytes) { return tenths_text((static_cast<uint64_t>(bytes) * 10) / 1024, 'K'); }

FixedStr<16> size_text(uint64_t bytes) {
  static const char kUnits[] = "KMGT";
  uint64_t tenths = bytes / 1024 * 10 + (bytes % 1024) * 10 / 1024;
  uint8_t unit = 0;
  while (tenths >= 10240 && unit < 3) {
    tenths /= 1024;
    ++unit;
  }
  return tenths_text(tenths, kUnits[unit]);
}

const char* store_note_text(StoreNote n) {
  switch (n) {
    case StoreNote::NoCard: return tr(txt::kNoCard);
    case StoreNote::SdUnusable: return tr(txt::kSdUnusable);
    case StoreNote::NoInternal: return tr(txt::kNoInternal);
    case StoreNote::LegacyTable: return tr(txt::kLegacyTable);
    case StoreNote::NoStorage: return tr(txt::kNoStorage);
    default: return "";
  }
}

SystemPage system_page(const AppState& s, const SysInfo& info) {
  SystemPage p;
  const DeviceInfo& d = s.device;
  add(p, false, tr(txt::kFwRow), info.fw.c_str(), info.rst.c_str());
  add(p, false, "dev %s  boot %s", info.dev.c_str(), d.boot.c_str());
  add(p, false, tr(txt::kBootRow), info.launcher ? "Launcher" : tr(txt::kDirect),
      info.host_app.empty() ? "--" : info.host_app.c_str());
  add_store(p, s, info);
  add(p, s.outbox.bad > 0, tr(txt::kOutboxRow), s.outbox.pending, s.outbox.inflight, s.outbox.bad,
      s.outbox.full ? tr(txt::kOutboxFull) : "");
  add(p, false, tr(txt::kHeapRow), kib_text(d.heap_free).c_str(), kib_text(d.heap_min).c_str(),
      kib_text(d.heap_max_block).c_str());
  add(p, false, tr(txt::kBatRow), d.bat_mv, kib_text(d.stack_hwm).c_str());
  const SystemLine warn = warning_line(info, d);
  if (!warn.empty()) {
    add(p, true, "%s", warn.c_str());
    p.key_len[p.n - 1] = 0;  // a sentence, not a label and a value
  }
  return p;
}

}  // namespace cpa
