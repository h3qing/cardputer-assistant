#include "rec_writer.h"

#include "protocol_limits.h"
#include "rec_paths.h"
#include "wav_header.h"

namespace cpa {
namespace rec {

RecWriter::RecWriter(RecFilePort& fs, Sha256Port& sha, MonoCounter& ids, const char* root, const FixedStr<31>& fw)
    : fs_(fs), sha_(sha), ids_(ids), root_(root), fw_(fw) {}

FsErr RecWriter::start(const SessionSpec& s, uint32_t now) {
  if (active_) return FsErr::Other;
  const Opt<MonoId> id = ids_.next();
  if (!id.has) return FsErr::Other;
  spec_ = s;
  session_ = id.value;
  seg_ = 0;
  done_bytes_ = 0;
  const FsErr err = open_segment(id.value, now);
  active_ = err == FsErr::None;
  return err;
}

FsErr RecWriter::open_segment(const MonoId& id, uint32_t now) {
  fs_.mkdir(root_);  // each exists after the first segment: one lookup per rotation
  fs_.mkdir(rec_dir(root_).c_str());
  fs_.mkdir(rec_epoch_dir(root_, id).c_str());
  FsErr err = fs_.open_trunc(rec_wav_path(root_, id).c_str());
  if (err != FsErr::None) return err;
  const WavHeader ph = wav_placeholder(spec_.start.mode);
  err = fs_.write(ph.b, sizeof ph.b);
  if (err != FsErr::None) {
    fs_.close();
    return err;
  }
  sha_.begin();
  id_ = id;
  seg_bytes_ = 0;
  last_sync_ = now;
  n_stars_ = 0;
  return FsErr::None;
}

uint64_t RecWriter::seg_cap() const { return static_cast<uint64_t>(spec_.seg_ms) * kBytesPerMs; }
uint64_t RecWriter::session_cap() const { return static_cast<uint64_t>(spec_.cap_ms) * kBytesPerMs; }

WriteResult RecWriter::write(const uint8_t* pcm, size_t n, uint32_t now, CommitSink& sink) {
  if (!active_) return WriteResult{FsErr::Other, false};
  size_t at = 0;
  while (at < n && session_bytes() < session_cap()) {
    if (seg_bytes_ >= seg_cap()) {
      const FsErr err = rotate(now, sink);
      if (err != FsErr::None) return WriteResult{err, false};
    }
    const uint64_t seg_room = seg_cap() - seg_bytes_;
    const uint64_t cap_room = session_cap() - session_bytes();
    uint64_t take = n - at;
    take = take < seg_room ? take : seg_room;
    take = take < cap_room ? take : cap_room;
    const FsErr err = append(pcm + at, static_cast<size_t>(take), now);
    if (err != FsErr::None) return WriteResult{err, false};
    at += static_cast<size_t>(take);
  }
  return WriteResult{FsErr::None, session_bytes() >= session_cap()};
}

FsErr RecWriter::append(const uint8_t* pcm, size_t n, uint32_t now) {
  const FsErr err = fs_.write(pcm, n);
  if (err != FsErr::None) return err;
  sha_.update(pcm, n);
  seg_bytes_ += static_cast<uint32_t>(n);
  if (now - last_sync_ < kSyncEveryMs) return FsErr::None;
  last_sync_ = now;
  return fs_.fsync();
}

// The next id first: once this .M names it, a power cut can never orphan the next segment.
FsErr RecWriter::rotate(uint32_t now, CommitSink& sink) {
  const Opt<MonoId> next = ids_.next();
  if (!next.has) return FsErr::Other;
  FsErr err = seal_and_commit(false, next, sink);
  if (err != FsErr::None) return err;
  done_bytes_ += seg_bytes_;
  seg_ = static_cast<uint16_t>(seg_ + 1);
  err = open_segment(next.value, now);
  if (err == FsErr::None) sink.segment(seg_);
  return err;
}

FsErr RecWriter::seal_and_commit(bool last, const Opt<MonoId>& next, CommitSink& sink) {
  const WavHeader h = wav_header(seg_bytes_);
  FsErr err = fs_.pwrite_at0(h.b, sizeof h.b);
  if (err == FsErr::None) err = fs_.fsync();
  const FsErr closed = fs_.close();
  if (err == FsErr::None) err = closed;
  if (err != FsErr::None) return err;
  const RecMeta m = meta_of(last, next, sha_.finish());
  err = commit_meta(m);
  if (err == FsErr::None) sink.committed(m, kHeaderBytes + seg_bytes_);
  return err;
}

// 09 §3.6 on FAT: the final name directly, fsync, close. The .M is the commit.
FsErr RecWriter::commit_meta(const RecMeta& m) {
  const MetaText t = rec_meta_encode(m);
  FsErr err = fs_.open_trunc(rec_meta_path(root_, m.id).c_str());
  if (err != FsErr::None) return err;
  err = fs_.write(reinterpret_cast<const uint8_t*>(t.c_str()), t.size());
  if (err == FsErr::None) err = fs_.fsync();
  const FsErr closed = fs_.close();
  return err != FsErr::None ? err : closed;
}

RecMeta RecWriter::meta_of(bool last, const Opt<MonoId>& next, const Sha256Hex& sha) const {
  RecMeta m;
  m.id = id_;
  m.rec = session_;
  m.seg = seg_;
  m.last = last;
  m.next = next;
  m.mode = spec_.start.mode;
  m.bytes = seg_bytes_;
  m.sha256 = some(sha);
  proto::RecAt at;
  at.up_ms = spec_.up_ms + static_cast<uint32_t>(done_bytes_ / kBytesPerMs);
  if (spec_.start.wall_offset_ms.has) at.wall_ms = some(spec_.start.wall_offset_ms.value + at.up_ms);
  m.at = some(at);
  m.project = spec_.start.project;
  m.slot = spec_.start.project.has ? 0 : spec_.start.slot;
  m.n_stars = n_stars_;
  for (uint8_t i = 0; i < n_stars_; ++i) m.stars[i] = stars_[i];
  if (seg_ == 0) m.lead_ms = some(kLeadMs);
  m.consent = seg_ == 0 && spec_.start.mode == RecMode::Meeting && spec_.start.consent;
  m.fw = fw_;
  return m;
}

FsErr RecWriter::finish(CommitSink& sink) {
  if (!active_) return FsErr::Other;
  active_ = false;
  const FsErr err = seal_and_commit(true, {}, sink);
  done_bytes_ += seg_bytes_;
  seg_bytes_ = 0;
  return err;
}

FsErr RecWriter::discard() {
  if (!active_) return FsErr::Other;
  active_ = false;
  fs_.close();
  return fs_.unlink(rec_wav_path(root_, id_).c_str());
}

void RecWriter::abandon() {
  if (!active_) return;
  active_ = false;
  fs_.close();  // VfsRecFilePort does not touch a dead card again (sd_health)
}

bool RecWriter::mark() {
  if (!active_ || n_stars_ >= kStarsMax) return false;
  stars_[n_stars_++] = seg_bytes_ / kBytesPerMs;
  return true;
}

void RecWriter::tag(const Opt<ProjectKey>& project, uint8_t slot) {
  spec_.start.project = project;
  spec_.start.slot = slot;
}

}  // namespace rec
}  // namespace cpa
