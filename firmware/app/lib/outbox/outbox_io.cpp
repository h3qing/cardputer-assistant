#include "outbox_io.h"

#include "protocol_limits.h"
#include "store_commit.h"

namespace cpa {
namespace {

// Records one directory listing into the scan state. It reads no file: decoding here
// nested the whole decode chain under the listing and overflowed the loop stack.
class ScanVisitor : public DirVisitor {
 public:
  ScanVisitor(ScanState& state, bool bad_dir) : state_(state), bad_dir_(bad_dir) {}

  void entry(const char* name, bool is_dir) override {
    const ScanClass c = scan_classify(name, is_dir);
    if (c.kind == ScanName::Dir && !bad_dir_) return;  // bad/ is listed on its own
    const bool ours = c.kind == ScanName::Json || (!bad_dir_ && c.kind == ScanName::Tmp);
    if (!ours) {
      other++;
      return;
    }
    scan_entry(state_, entry_kind(c), c.id);
  }

  uint16_t other = 0;

 private:
  ScanEntry entry_kind(const ScanClass& c) const {
    if (bad_dir_) return ScanEntry::Bad;
    return c.kind == ScanName::Tmp ? ScanEntry::Tmp : ScanEntry::Json;
  }

  ScanState& state_;
  bool bad_dir_;
};

// How /outbox/<id>.json reads back: an outbox.item with that id, bad content, an empty
// file (the fold decides by store kind), or an I/O error (not bad content: 05 §5.9
// quarantines only undecodable or mismatched items).
ScanEntry check_item(FileStore& fs, proto::Codec& codec, const MonoId& id) {
  static char buf[limits::kLineMax + 1];
  const long n = outbox_read(fs, id, buf, sizeof buf);
  if (n < 0) return ScanEntry::JsonUnread;
  if (n == 0) return ScanEntry::JsonEmpty;
  const proto::Decoded& d = codec.decode(buf, static_cast<size_t>(n));
  const bool ok = d.outcome == proto::Outcome::Ok && proto::msg_type(d.msg) == proto::MsgType::OutboxItem &&
                  d.msg.id.equals(mono_format(id).c_str());
  return ok ? ScanEntry::Json : ScanEntry::JsonBad;
}

// Checks every pending item, one after the other at this stack depth.
void check_pending(FileStore& fs, proto::Codec& codec, ScanState& state) {
  uint16_t i = 0;
  while (i < state.core.n) {
    const MonoId id = state.core.ids[i];
    const ScanEntry checked = check_item(fs, codec, id);
    if (checked == ScanEntry::Json) {
      ++i;
    } else {
      scan_entry(state, checked, id);  // removes ids[i]
    }
  }
}

bool run_action(FileStore& fs, const ScanAction& a) {
  switch (a.kind) {
    case ScanActionKind::RemoveTmp: return fs.remove(outbox_path(a.id, OutboxFile::Tmp).c_str());
    case ScanActionKind::MoveToBad: {
      fs.mkdir(kOutboxBadDir);
      return move_file(fs, outbox_path(a.id, OutboxFile::Json).c_str(), outbox_path(a.id, OutboxFile::Bad).c_str());
    }
    case ScanActionKind::RemoveBad: return fs.remove(outbox_path(a.id, OutboxFile::Bad).c_str());
    case ScanActionKind::RemoveJson: return fs.remove(outbox_path(a.id, OutboxFile::Json).c_str());
  }
  return false;
}

}  // namespace

OutboxPath outbox_path(const MonoId& id, OutboxFile file) {
  const auto name = mono_format(id);
  const char* dir = file == OutboxFile::Bad ? "/outbox/bad/" : "/outbox/";
  const char* ext = file == OutboxFile::Tmp ? ".tmp" : ".json";
  return OutboxPath::from(dir).append(name.c_str(), name.size()).append(ext);
}

bool outbox_persist(FileStore& fs, const MonoId& id, const char* line, size_t n) {
  fs.mkdir(kOutboxDir);
  return commit_file(fs, outbox_path(id, OutboxFile::Json).c_str(), outbox_path(id, OutboxFile::Tmp).c_str(), line, n);
}

bool outbox_apply(FileStore& fs, const OutboxEffect& e) {
  switch (e.kind) {
    case OutboxEffectKind::Remove: return fs.remove(outbox_path(e.id, OutboxFile::Json).c_str());
    case OutboxEffectKind::MoveToBad: return run_action(fs, ScanAction{ScanActionKind::MoveToBad, e.id});
    case OutboxEffectKind::RemoveBad: return fs.remove(outbox_path(e.id, OutboxFile::Bad).c_str());
    default: return true;
  }
}

long outbox_read(FileStore& fs, const MonoId& id, char* buf, size_t cap) {
  return fs.read(outbox_path(id, OutboxFile::Json).c_str(), buf, cap);
}

const ScanResult& outbox_boot_scan(FileStore& fs, proto::Codec& codec) {
  static ScanState state;
  static ScanResult result;
  state = ScanState{};
  state.fat = !fs.atomic_rename();
  ScanVisitor top(state, false);
  ScanVisitor bad(state, true);
  fs.list(kOutboxDir, top);
  fs.list(kOutboxBadDir, bad);
  check_pending(fs, codec, state);
  scan_finish(state);
  result.failed = 0;
  for (uint8_t i = 0; i < state.n; ++i) result.failed = static_cast<uint16_t>(result.failed + !run_action(fs, state.actions[i]));
  result.core = state.core;
  result.broken = state.broken;
  result.deferred = state.deferred;
  result.other = static_cast<uint16_t>(top.other + bad.other);
  return result;
}

}  // namespace cpa
