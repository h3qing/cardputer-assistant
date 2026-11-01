// RecWriter (docs/11 §4.5): one recording session on the card through RecFilePort.
//   start:   take the session id -> open .W (O_TRUNC) -> 44 B placeholder -> hash begin
//   write:   append blocks, hash them, fsync every 1 s; at the segment length rotate:
//            take the next id FIRST, backfill the header, fsync + close, commit .M (with
//            next), then open the next segment. A block that crosses the boundary is
//            split; the session stops exactly at its cap.
//   finish:  like a rotation with last:true and no next segment.
// A .M is the commit (written to its final name, fsync, close). At most one file is open
// at any time. The writer is the audio task's mutable state: it is never copied.
#pragma once

#include <cstddef>
#include <cstdint>

#include "rec_file_port.h"
#include "rec_meta.h"
#include "rec_msgs.h"

namespace cpa {
namespace rec {

constexpr uint32_t kSyncEveryMs = 1000;

struct SessionSpec {
  StartSpec start;
  uint32_t up_ms = 0;        // device clock at the first sample
  uint32_t seg_ms = 300000;  // seg_ms_default
  uint32_t cap_ms = 300000;  // memo 5 min, meeting 3 h
};

class CommitSink {
 public:
  virtual ~CommitSink() = default;
  virtual void committed(const RecMeta& m, uint32_t file_size) = 0;
  virtual void segment(uint16_t seg) = 0;  // a new segment is open
};

struct WriteResult {
  FsErr err = FsErr::None;
  bool capped = false;  // the cap was reached: finish() next
};

class RecWriter {
 public:
  RecWriter(RecFilePort& fs, Sha256Port& sha, MonoCounter& ids, const char* root, const FixedStr<31>& fw);
  RecWriter(const RecWriter&) = delete;
  RecWriter& operator=(const RecWriter&) = delete;

  FsErr start(const SessionSpec& s, uint32_t now);
  WriteResult write(const uint8_t* pcm, size_t n, uint32_t now, CommitSink& sink);
  FsErr finish(CommitSink& sink);
  FsErr discard();  // close and remove the open .W: nothing of the session is kept
  void abandon();   // after a Disk error: forget the session, touch nothing more
  bool mark();      // a star at the current position; false when the segment has 16
  void tag(const Opt<ProjectKey>& project, uint8_t slot);
  // A .M to its final name, fsync, close (09 §3.6 on FAT); also the recovery's new .M.
  FsErr commit_meta(const RecMeta& m);

  bool active() const { return active_; }
  const MonoId& session() const { return session_; }
  uint16_t seg() const { return seg_; }
  uint64_t session_bytes() const { return done_bytes_ + seg_bytes_; }

 private:
  FsErr open_segment(const MonoId& id, uint32_t now);
  FsErr rotate(uint32_t now, CommitSink& sink);
  FsErr seal_and_commit(bool last, const Opt<MonoId>& next, CommitSink& sink);
  RecMeta meta_of(bool last, const Opt<MonoId>& next, const Sha256Hex& sha) const;
  FsErr append(const uint8_t* pcm, size_t n, uint32_t now);
  uint64_t seg_cap() const;
  uint64_t session_cap() const;

  RecFilePort& fs_;
  Sha256Port& sha_;
  MonoCounter& ids_;
  const char* root_;
  FixedStr<31> fw_;
  SessionSpec spec_;
  bool active_ = false;
  MonoId session_;
  MonoId id_;  // the open segment
  uint16_t seg_ = 0;
  uint64_t done_bytes_ = 0;  // PCM bytes of the committed segments of this session
  uint32_t seg_bytes_ = 0;
  uint32_t last_sync_ = 0;
  uint8_t n_stars_ = 0;
  uint32_t stars_[kStarsMax] = {};
};

}  // namespace rec
}  // namespace cpa
