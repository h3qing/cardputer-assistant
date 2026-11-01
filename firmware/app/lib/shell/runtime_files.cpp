// Runtime: the file.* requests (docs/11 §5.3-§5.4), debug.rec, debug.mic and debug.codec. Requests are checked
// against the index here; the reads, .M reads and deletes run on the audio task, one at
// a time. Every request gets exactly one reply.
#include <cstring>

#include "file_req.h"
#include "protocol_limits.h"
#include "runtime.h"

namespace cpa {

using proto::Decoded;
using proto::MsgType;

namespace {

bool is_file_type(MsgType t) { return t == MsgType::FileList || t == MsgType::FileGet || t == MsgType::FileDel; }

rec::RecCmd file_cmd(rec::CmdKind kind, const rec::FileVerdict& v) {
  rec::RecCmd c;
  c.kind = kind;
  c.fid = v.id;
  c.size = v.size;
  c.len = v.len;
  return c;
}

#if CPA_TEST_HOOKS
bool is_debug_rec_type(MsgType t) {
  return t == MsgType::DebugRec || t == MsgType::DebugMic || t == MsgType::DebugCodec;
}

// debug.rec sets the segment length, debug.mic the gain; both for the next recordings.
// debug.codec puts the codec into its power-on state (issue #31) before the next one.
rec::RecCmd debug_cmd(const proto::Body& b) {
  rec::RecCmd c;
  if (const auto* r = std::get_if<proto::DebugRec>(&b)) {
    c.kind = rec::CmdKind::SegMs;
    c.seg_ms = r->seg_ms;
    return c;
  }
  if (const auto* k = std::get_if<proto::DebugCodec>(&b)) {
    c.kind = rec::CmdKind::Codec;
    c.warm = k->warm;
    return c;
  }
  const proto::DebugMic& m = std::get<proto::DebugMic>(b);
  c.kind = rec::CmdKind::MicGain;
  c.gain = MicGain{m.pga, m.vol, m.mag};
  return c;
}
#endif

}  // namespace

// file.*, debug.rec, debug.mic and debug.codec are answered here, not by the link core, once the
// link is Ready and the device can record; otherwise the link core answers (E_NOT_READY,
// E_UNKNOWN_TYPE). debug.codec never reaches a live recording: E_BUSY "live" instead.
bool Runtime::rec_request(const Decoded& d, uint32_t now) {
  if (!rec_ || d.outcome != proto::Outcome::Ok || !d.reply_to.has || !link_ready(link_)) return false;
  const MsgType t = proto::msg_type(d.msg);
#if CPA_TEST_HOOKS
  if (is_debug_rec_type(t)) {
    link_.info.last_rx_ms = now;
    if (t == MsgType::DebugCodec && rec::rec_live(rec_core_)) {
      send_error(d.reply_to.value, ErrCode::Busy, {}, some(proto::ErrText::from("live")));
      return true;
    }
    post(debug_cmd(d.msg.body));
    send_ack(d.reply_to.value);
    return true;
  }
#endif
  if (!is_file_type(t) || !rec_avail()) return false;
  link_.info.last_rx_ms = now;  // valid traffic for the link timeout
  file_request(d);
  return true;
}

void Runtime::file_request(const Decoded& d) {
  const MsgId& re = d.reply_to.value;
  rec::FileGate gate;
  gate.live = rec::rec_live(rec_core_);
  gate.inflight = rec::rec_inflight(rec_core_);
  // until IndexReady the audio task still fills the index: not ready, and not read here
  const rec::FileVerdict g = rec::file_gate(rec_core_.ready, gate);
  if (g.answer == rec::FileAnswer::Error) return send_error(re, g.code, g.field, g.msg);
  if (const auto* get = std::get_if<proto::FileGet>(&d.msg.body)) return file_get(re, *get);
  if (const auto* del = std::get_if<proto::FileDel>(&d.msg.body)) return file_del(re, *del);
  file_list(re, std::get<proto::FileList>(d.msg.body));
}

void Runtime::file_get(const MsgId& re, const proto::FileGet& get) {
  const rec::FileVerdict v = rec::file_check_get(get, rec_->index(), viewer());
  if (v.answer == rec::FileAnswer::Error) return send_error(re, v.code, v.field, v.msg);
  if (v.answer == rec::FileAnswer::Data0) return send_data0(re, get, v.size);
  PendingFile p;
  p.re = re;
  p.type = MsgType::FileGet;
  p.cmd = file_cmd(rec::CmdKind::Get, v);
  p.cmd.off = get.off;
  queue_file(p);
}

void Runtime::file_del(const MsgId& re, const proto::FileDel& del) {
  const rec::FileVerdict v = rec::file_check_del(del, rec_->index(), viewer());
  if (v.answer == rec::FileAnswer::Ack) return send_ack(re);
  if (v.answer == rec::FileAnswer::Error) return send_error(re, v.code, v.field, v.msg);
  PendingFile p;
  p.re = re;
  p.type = MsgType::FileDel;
  p.cmd = file_cmd(rec::CmdKind::Del, v);
  p.cmd.sha256 = del.sha256;
  p.cmd.ledger.host = kept_.host;  // docs/28: who receives it, kept for "deliver again"
  queue_file(p);
}

void Runtime::file_list(const MsgId& re, const proto::FileList& list) {
  const rec::ListPlan plan = rec::file_plan_list(list, rec_->index(), viewer());
  if (plan.n == 0) {  // nothing on the card after `after`: an empty page at once
    rec::RecEvt empty;  // (the request with the audio task keeps its own reply)
    empty.kind = rec::EvtKind::Items;
    return send_items(empty, re, plan.more);
  }
  PendingFile p;
  p.re = re;
  p.type = MsgType::FileList;
  p.cmd.kind = rec::CmdKind::List;
  p.cmd.n = plan.n;
  for (uint8_t i = 0; i < plan.n; ++i) {
    p.cmd.ids[i] = plan.ids[i];
    p.cmd.sizes[i] = plan.sizes[i];
  }
  p.more = plan.more;
  queue_file(p);
}

void Runtime::queue_file(const PendingFile& p) {
  RecSide& rs = rec_side_;
  if (rs.n >= 4) return send_error(p.re, ErrCode::Busy, {}, some(proto::ErrText::from("full")));
  rs.pend[(rs.head + rs.n) % 4] = p;
  rs.n = static_cast<uint8_t>(rs.n + 1);
  rec_apply(rec::rec_file_queued(rec_core_));
}

void Runtime::post_next_file() {
  RecSide& rs = rec_side_;
  if (rs.n == 0) return;
  rs.task = rs.pend[rs.head];
  rs.head = static_cast<uint8_t>((rs.head + 1) % 4);
  rs.n = static_cast<uint8_t>(rs.n - 1);
  if (rs.task.cmd.kind == rec::CmdKind::Del) rs.task.cmd.ledger.wall_s = wall_now_s();  // as it is handed over
  if (rec_->post(rs.task.cmd)) return;
  send_error(rs.task.re, ErrCode::Busy, {}, some(proto::ErrText::from("full")));
  rec_apply(rec::rec_file_done(rec_core_));
}

void Runtime::flush_busy() {
  RecSide& rs = rec_side_;
  for (; rs.n > 0; rs.n = static_cast<uint8_t>(rs.n - 1)) {
    send_error(rs.pend[rs.head].re, ErrCode::Busy, {}, some(proto::ErrText::from("live")));
    rs.head = static_cast<uint8_t>((rs.head + 1) % 4);
  }
}

void Runtime::file_reply(const rec::RecEvt& e) {
  const PendingFile& t = rec_side_.task;
  if (e.kind == rec::EvtKind::Items) send_items(e, t.re, t.more);
  if (e.kind == rec::EvtKind::Data && !send_data(e)) {
    rec_side_.held = true;  // waits for room in the TX ring (pump_rec)
    rec_side_.held_evt = e;
    return;
  }
  if (e.kind == rec::EvtKind::DelDone) {
    if (e.code == ErrCode::None) {
      rec_->index().remove(e.id);
      if (rec_side_.free_known && !e.kept) rec_side_.free_bytes += t.cmd.size;
      kept_.tidy_due = kept_.tidy_due || e.kept;  // docs/28: tidied once the Mac is gone
      send_ack(t.re);
    } else {
      const Opt<proto::FieldPath> field = e.field_sha ? some(proto::FieldPath::from("sha256")) : Opt<proto::FieldPath>{};
      send_error(t.re, e.code, field, {});
    }
  }
  rec_apply(rec::rec_file_done(rec_core_));
}

bool Runtime::send_data(const rec::RecEvt& e) {
  const PendingFile& t = rec_side_.task;
  if (e.code != ErrCode::None) {
    send_error(t.re, e.code, {}, {});
    return true;
  }
  tx_.tick(now_);  // drains the line queue first: room for a whole line then means it is empty
  if (plat_.tx_room() < limits::kLineMax) return false;
  begin_reply(t.re);
  auto& d = out_.body.emplace<proto::FileData>();
  d.fid = mono_format(e.id);
  d.off = e.off;
  d.n = e.n;
  d.size = e.size;
  d.crc = e.crc;
  std::memcpy(d.bytes, rec_->xfer().data, e.n);
  transmit(TxClass::Reply);
  return true;
}

void Runtime::send_data0(const MsgId& re, const proto::FileGet& g, uint32_t size) {
  begin_reply(re);
  auto& d = out_.body.emplace<proto::FileData>();
  d.fid = g.fid;
  d.off = g.off;
  d.size = size;
  transmit(TxClass::Reply);
}

// The page shrinks until its line fits (docs/11 §5.4); never in practice: four items with
// every field at its maximum are 1,988 B.
void Runtime::send_items(const rec::RecEvt& e, const MsgId& re, bool more) {
  if (e.code != ErrCode::None) return send_error(re, e.code, {}, {});
  begin_reply(re);
  auto& items = out_.body.emplace<proto::FileItems>();
  const uint8_t n = static_cast<uint8_t>(e.n < proto::kFileListMax ? e.n : proto::kFileListMax);
  for (uint8_t i = 0; i < n; ++i) items.items[i] = rec_->xfer().items[i];
  const uint8_t k = rec::file_fit_page(n, &Runtime::items_fit, this);
  items.n = k;
  items.more = more || k < n;
  transmit(TxClass::Reply);
}

// file_fit_page's probe: out_ holds the page; does it encode with k items?
bool Runtime::items_fit(void* self, uint8_t k) {
  Runtime& rt = *static_cast<Runtime*>(self);
  auto& items = std::get<proto::FileItems>(rt.out_.body);
  const uint8_t n = items.n;
  items.n = k;
  const bool ok = rt.codec_.encode(rt.out_).ok;
  items.n = n;
  return ok;
}

void Runtime::begin_reply(const MsgId& re) {
  const IdIssue id = link_issue_id(link_);
  link_ = id.core;
  out_.id = id.id;
  out_.re = some(re);
}

void Runtime::send_error(const MsgId& re, ErrCode code, const Opt<proto::FieldPath>& field,
                         const Opt<proto::ErrText>& msg) {
  begin_reply(re);
  auto& err = out_.body.emplace<proto::ErrorMsg>();
  err.code = code;
  err.field = field;
  err.msg = msg;
  transmit(TxClass::Reply);
}

void Runtime::send_ack(const MsgId& re) {
  begin_reply(re);
  out_.body.emplace<proto::Ack>();
  transmit(TxClass::Reply);
}

}  // namespace cpa
