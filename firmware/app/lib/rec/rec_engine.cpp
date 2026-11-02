// RecEngine: sessions and file.* work (rec_engine.h). The boot scan is in rec_scan.cpp.
#include "rec_engine.h"

#include "mic_start.h"
#include "protocol_limits.h"
#include "wav_header.h"

namespace cpa {
namespace rec {
namespace {

RecFail fail_of(FsErr e) {
  switch (e) {
    case FsErr::Full: return RecFail::Full;
    case FsErr::Disk: return RecFail::Disk;
    default: return RecFail::Internal;
  }
}

RecEvt event(EvtKind kind) {
  RecEvt e;
  e.kind = kind;
  return e;
}

ErrCode storage_code(FsErr e) { return e == FsErr::NotFound ? ErrCode::UnknownItem : ErrCode::Storage; }

}  // namespace

RecEngine::RecEngine(const EngineDeps& d)
    : d_(d), writer_(*d.fs, *d.sha, *d.ids, d.root, d.fw), seg_ms_(limits::kSegMsDefault) {}

void RecEngine::committed(const RecMeta& m, uint32_t file_size) {
  RecEvt e = event(EvtKind::Committed);
  e.id = m.id;
  e.size = file_size;
  e.seg = m.seg;
  sink_->emit(e);
}

void RecEngine::segment(uint16_t seg) {
  RecEvt e = event(EvtKind::Segment);
  e.seg = seg;
  sink_->emit(e);
}

void RecEngine::start(const StartSpec& s, uint32_t up_ms, uint32_t now, EvtSink& out) {
  close_transfer();
  sink_ = &out;
  hpf_ = LevelHpf{};  // the level's high-pass starts with the recording
  SessionSpec spec;
  spec.start = s;
  spec.up_ms = up_ms;
  spec.seg_ms = seg_ms_;
  spec.cap_ms = s.mode == RecMode::Memo ? limits::kMemoMaxMs : limits::kMeetingMaxMs;
  const FsErr err = writer_.start(spec, now);
  if (err != FsErr::None) return end_session(fail_of(err), false, out);
  RecEvt e = event(EvtKind::Started);
  e.id = writer_.session();
  e.up_ms = up_ms;
  out.emit(e);
}

void RecEngine::arm(const StartSpec& s, uint32_t up_ms, uint32_t now) {
  close_transfer();
  arm_.on = true;
  arm_.spec = s;
  arm_.up_ms = up_ms;
  arm_.at = now;
  arm_.muted = 0;
}

// Armed: a muted block is dropped until the wait is over; the first other block opens the
// session at its first live sample, a muted one after the wait opens it whole (counted).
void RecEngine::gate(const uint8_t* pcm, size_t n, uint32_t now, EvtSink& out) {
  const bool muted = block_muted(pcm, n);
  if (muted && now - arm_.at < kMuteWaitMs) {
    arm_.muted += static_cast<uint32_t>(n);
    return;
  }
  if (muted) ++mute_timeouts_;
  const size_t from = muted ? 0 : muted_lead(pcm, n);
  open_session(arm_.muted + static_cast<uint32_t>(from), now, out);
  if (from < n) write(pcm + from, n - from, now, out);
}

void RecEngine::tick(uint32_t now, EvtSink& out) {
  if (!arm_.on || now - arm_.at < kMuteWaitMs) return;
  ++mute_timeouts_;
  open_session(arm_.muted, now, out);
}

// The first written sample comes `skipped` bytes after the microphone's first one.
void RecEngine::open_session(uint32_t skipped, uint32_t now, EvtSink& out) {
  arm_.on = false;
  start(arm_.spec, arm_.up_ms + dur_ms_of(skipped), now, out);
}

void RecEngine::write(const uint8_t* pcm, size_t n, uint32_t now, EvtSink& out) {
  if (arm_.on) return gate(pcm, n, now, out);
  if (!writer_.active()) return;
  sink_ = &out;
  const WriteResult r = writer_.write(pcm, n, now, *this);
  const LevelOut lv = level_of(hpf_, pcm, n);  // the level only: the WAV has the bytes as they came
  hpf_ = lv.hpf;
  RecEvt level = event(EvtKind::Level);
  level.level = lv.rms;
  out.emit(level);
  if (r.err == FsErr::Full) {
    writer_.finish(*this);  // the segment so far is kept (docs/11 §4.3)
    return end_session(RecFail::Full, false, out);
  }
  if (r.err != FsErr::None) {
    writer_.abandon();  // left for the boot recovery (§4.6)
    return end_session(fail_of(r.err), false, out);
  }
  if (!r.capped) return;
  const FsErr err = writer_.finish(*this);
  end_session(err == FsErr::None ? RecFail::None : fail_of(err), true, out);
}

void RecEngine::stop(EvtSink& out) {
  if (arm_.on) {  // the codec never let go: nothing was written (B5)
    arm_.on = false;
    return out.emit(event(EvtKind::Discarded));
  }
  if (!writer_.active()) return;
  sink_ = &out;
  if (writer_.session_bytes() < bytes_of_ms(limits::kRecMinMs)) {
    writer_.discard();
    return out.emit(event(EvtKind::Discarded));
  }
  const FsErr err = writer_.finish(*this);
  end_session(err == FsErr::None ? RecFail::None : fail_of(err), false, out);
}

void RecEngine::tag(const Opt<ProjectKey>& project, uint8_t slot) {
  if (!arm_.on) return writer_.tag(project, slot);
  arm_.spec.project = project;  // a meeting's cue takes tags (RecCore)
  arm_.spec.slot = slot;
}

void RecEngine::end_session(RecFail fail, bool auto_stop, EvtSink& out) {
  RecEvt e = event(fail == RecFail::None ? EvtKind::Saved : EvtKind::Failed);
  e.fail = fail;
  e.auto_stop = auto_stop;
  out.emit(e);
}

void RecEngine::close_transfer() {
  if (!xfer_open_) return;
  xfer_open_ = false;
  d_.fs->close();
}

bool RecEngine::read_meta(const MonoId& id, RecMeta& out) {
  RecFilePort& fs = *d_.fs;
  if (fs.open_read(rec_meta_path(d_.root, id).c_str()) != FsErr::None) return false;
  FsErr err = FsErr::None;
  const long n = fs.read_at(0, reinterpret_cast<uint8_t*>(meta_buf_), kMetaMax, err);
  fs.close();
  if (n <= 0) return false;
  return rec_meta_decode_into(meta_buf_, static_cast<size_t>(n), id, out);
}

void RecEngine::list(const RecCmd& c, EvtSink& out) {
  close_transfer();
  RecEvt e = event(EvtKind::Items);
  RecMeta m;
  for (uint8_t i = 0; i < c.n && i < proto::kFileListMax; ++i) {
    if (!read_meta(c.ids[i], m)) {
      e.code = ErrCode::Storage;
      return out.emit(e);
    }
    d_.xfer->items[i] = rec_item_of(m, c.sizes[i]);
  }
  e.n = c.n;
  out.emit(e);
}

void RecEngine::get(const RecCmd& c, EvtSink& out) {
  RecEvt e = event(EvtKind::Data);
  e.id = c.fid;
  e.off = c.off;
  e.size = c.size;
  if (!xfer_open_ || xfer_id_ != c.fid) {
    close_transfer();
    const FsErr err = d_.fs->open_read(rec_wav_path(d_.root, c.fid).c_str());
    if (err != FsErr::None) {
      e.code = storage_code(err);
      return out.emit(e);
    }
    xfer_open_ = true;
    xfer_id_ = c.fid;
  }
  const uint32_t room = c.size > c.off ? c.size - c.off : 0;
  const size_t want = c.len < room ? c.len : room;
  FsErr err = FsErr::None;
  const long n = d_.fs->read_at(c.off, d_.xfer->data, want, err);
  if (n < 0) {
    close_transfer();
    e.code = storage_code(err);
    return out.emit(e);
  }
  e.n = static_cast<uint16_t>(n);
  e.crc = d_.crc(d_.xfer->data, static_cast<size_t>(n));
  out.emit(e);
}

// docs/11 §5.4: only when the size (checked by the loop) and the recorded sha256 match;
// .W first, then .M, so a power cut in between leaves a .M the scan removes. docs/28 S1a:
// kept instead when it may be (keep_on_del: DELIV appended and fsynced before the reply).
void RecEngine::del(const RecCmd& c, EvtSink& out) {
  close_transfer();
  RecEvt e = event(EvtKind::DelDone);
  e.id = c.fid;
  RecMeta m;
  if (!read_meta(c.fid, m)) {
    e.code = ErrCode::Storage;
    return out.emit(e);
  }
  const bool sha_known = m.sha256.has && !m.rcv && c.size == kHeaderBytes + m.bytes;
  if (sha_known && !(c.sha256.has && c.sha256.value == m.sha256.value)) {
    e.code = ErrCode::BadField;
    e.field_sha = true;
    return out.emit(e);
  }
  if (keep_on_del(m, c, e)) {
    out.emit(e);
    if (e.kept) emit_kept(out);
    return;
  }
  const FsErr w = d_.fs->unlink(rec_wav_path(d_.root, c.fid).c_str());
  const FsErr mm = w == FsErr::None || w == FsErr::NotFound ? d_.fs->unlink(rec_meta_path(d_.root, c.fid).c_str()) : w;
  const bool ok = (w == FsErr::None || w == FsErr::NotFound) && (mm == FsErr::None || mm == FsErr::NotFound);
  if (!ok) e.code = ErrCode::Storage;
  out.emit(e);
}

}  // namespace rec
}  // namespace cpa
