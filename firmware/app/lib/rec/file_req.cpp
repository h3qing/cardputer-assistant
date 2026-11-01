#include "file_req.h"

#include "protocol_limits.h"

namespace cpa {
namespace rec {
namespace {

FileVerdict error(ErrCode code, const char* msg = nullptr, const char* field = nullptr) {
  FileVerdict v;
  v.answer = FileAnswer::Error;
  v.code = code;
  if (msg) v.msg = some(proto::ErrText::from(msg));
  if (field) v.field = some(proto::FieldPath::from(field));
  return v;
}

FileVerdict answer(FileAnswer a) {
  FileVerdict v;
  v.answer = a;
  return v;
}

const RecIndex::Entry* entry_of(const MonoIdStr& fid, const RecIndex& idx, MonoId& id, const Viewer& v) {
  const Opt<MonoId> parsed = mono_parse(fid.c_str(), fid.size());
  if (!parsed.has) return nullptr;
  id = parsed.value;
  const RecIndex::Entry* e = idx.find(id);
  return e && file_visible(*e, idx, v) ? e : nullptr;
}

}  // namespace

FileVerdict file_gate(bool index_ready, const FileGate& g) {
  if (g.live) return error(ErrCode::Busy, "live");
  if (!index_ready) return error(ErrCode::NotReady);
  if (g.inflight >= limits::kFileInflightMax) return error(ErrCode::Busy, "full");
  return answer(FileAnswer::Queue);
}

bool file_visible(const RecIndex::Entry& e, const RecIndex& idx, const Viewer& v) {
  if (v.hold && !(e.id < v.hold_from)) return false;
  return idx.for_host(e, v.host);
}

Offline file_offline(const RecIndex& idx, const MonoId& from, uint32_t host) {
  Offline o;
  for (uint16_t i = 0; i < idx.count(); ++i) {
    const RecIndex::Entry& e = idx.at(i);
    if (e.id < from || e.id.epoch != from.epoch || !idx.for_host(e, host)) continue;
    ++o.segs;
    o.recs = static_cast<uint16_t>(o.recs + (e.first ? 1 : 0));
  }
  if (o.segs > 0 && o.recs == 0) o.recs = 1;
  return o;
}

uint16_t file_visible_count(const RecIndex& idx, const Viewer& v) {
  uint16_t n = 0;
  for (uint16_t i = 0; i < idx.count(); ++i) n += file_visible(idx.at(i), idx, v) ? 1 : 0;
  return n;
}

FileVerdict file_check_get(const proto::FileGet& r, const RecIndex& idx) { return file_check_get(r, idx, Viewer{}); }
FileVerdict file_check_del(const proto::FileDel& r, const RecIndex& idx) { return file_check_del(r, idx, Viewer{}); }
ListPlan file_plan_list(const proto::FileList& r, const RecIndex& idx) { return file_plan_list(r, idx, Viewer{}); }

FileVerdict file_check_get(const proto::FileGet& r, const RecIndex& idx, const Viewer& who) {
  MonoId id;
  const RecIndex::Entry* e = entry_of(r.fid, idx, id, who);
  if (!e) return error(ErrCode::UnknownItem);
  FileVerdict v = answer(r.off >= e->size ? FileAnswer::Data0 : FileAnswer::Queue);
  v.id = id;
  v.size = e->size;
  const uint32_t room = r.off < e->size ? e->size - r.off : 0;
  v.len = static_cast<uint16_t>(r.len < room ? r.len : room);
  return v;
}

FileVerdict file_check_del(const proto::FileDel& r, const RecIndex& idx, const Viewer& who) {
  MonoId id;
  const RecIndex::Entry* e = entry_of(r.fid, idx, id, who);
  if (!e) return answer(FileAnswer::Ack);
  if (e->size != r.size) return error(ErrCode::BadField, nullptr, "size");
  FileVerdict v = answer(FileAnswer::Queue);
  v.id = id;
  v.size = e->size;
  return v;
}

ListPlan file_plan_list(const proto::FileList& r, const RecIndex& idx, const Viewer& v) {
  ListPlan p;
  const uint8_t max = r.max.has ? r.max.value : proto::kFileListMax;
  uint16_t at = idx.first_after(r.after.has ? r.after.value.c_str() : nullptr);
  for (; at < idx.count() && p.n < max && p.n < proto::kFileListMax; ++at) {
    if (!file_visible(idx.at(at), idx, v)) continue;
    p.ids[p.n] = idx.at(at).id;
    p.sizes[p.n] = idx.at(at).size;
    ++p.n;
  }
  while (at < idx.count() && !file_visible(idx.at(at), idx, v)) ++at;
  p.more = at < idx.count();
  return p;
}

uint8_t file_fit_page(uint8_t n, PageFits fits, void* ctx) {
  uint8_t k = n;
  while (k > 0 && !fits(ctx, k)) --k;
  return k;
}

}  // namespace rec
}  // namespace cpa
