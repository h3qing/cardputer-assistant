#include "reducers.h"

#include <cstring>

namespace cpa {
namespace {

bool same_identity(const ProjectsData& a, const ProjectsData& b) {
  if (a.n != b.n) return false;
  for (uint8_t i = 0; i < a.n; ++i) {
    const auto& x = a.rows[i];
    const auto& y = b.rows[i];
    if (x.slot != y.slot || x.key != y.key || x.name != y.name) return false;
  }
  return true;
}

bool needed_before(const SessionsView& v, const Sid& sid) {
  for (uint8_t i = 0; i < v.data.n; ++i) {
    if (v.data.rows[i].sid == sid) return v.data.rows[i].need;
  }
  return false;
}

}  // namespace

Step<ProjectsView, ProjectsEvent> reduce_projects(const ProjectsView& v, const ProjectsData& d, uint32_t now) {
  Step<ProjectsView, ProjectsEvent> out{v, {}};
  out.state.live = true;
  out.state.rx_ms = now;
  out.state.data = d;
  if (!same_identity(v.data, d)) out.effects = out.effects.with(ProjectsEvent::IdentityChanged);
  return out;
}

Step<SessionsView, NeedToast> reduce_sessions(const SessionsView& v, const SessionsData& d, uint32_t now) {
  Step<SessionsView, NeedToast> out{v, {}};
  out.state.live = true;
  out.state.rx_ms = now;
  out.state.data = d;
  for (uint8_t i = 0; i < d.n; ++i) {
    if (d.rows[i].need && !needed_before(v, d.rows[i].sid)) {
      NeedToast t;
      t.index = i;
      out.effects = out.effects.with(t);
    }
  }
  return out;
}

QueueView reduce_queue(const QueueView& v, const QueueData& d, uint32_t now) {
  QueueView out = v;
  out.live = true;
  out.rx_ms = now;
  out.data = d;
  return out;
}

TimeInfo reduce_time(const TimeInfo& t, const TimeData& d, uint32_t now) {
  TimeInfo out = t;
  out.known = true;
  out.offset_ms = d.epoch_ms - static_cast<int64_t>(now);
  out.tz_min = d.tz_min;
  return out;
}

Opt<ProjectRow> project_by_slot(const ProjectsView& v, uint8_t slot) {
  for (uint8_t i = 0; i < v.data.n; ++i) {
    if (v.data.rows[i].slot == slot) return some(v.data.rows[i]);
  }
  return {};
}

uint16_t need_count(const SessionsView& v) {
  uint16_t n = 0;
  for (uint8_t i = 0; i < v.data.n; ++i) n = static_cast<uint16_t>(n + (v.data.rows[i].need ? 1 : 0));
  return n;
}

uint32_t running_count(const ProjectsView& v) {
  if (!v.live) return 0;
  uint32_t n = v.data.other.run;
  for (uint8_t i = 0; i < v.data.n; ++i) n += v.data.rows[i].run;
  return n;
}

uint8_t queue_count_for_slot(const QueueView& v, uint8_t slot) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < v.data.n; ++i) n = static_cast<uint8_t>(n + (v.data.rows[i].slot == slot ? 1 : 0));
  return n;
}

uint32_t session_age_s(const SessionsView& v, uint8_t index, uint32_t now) {
  if (index >= v.data.n) return 0;
  return v.data.rows[index].age_s + (now - v.rx_ms) / 1000u;
}

uint32_t data_as_of(const AppState& s, uint32_t now) {
  if (s.link.mode == LinkMode::Ready) return now;
  const uint32_t last = s.link.last_rx_ms;
  return last - s.sessions.rx_ms < 0x80000000u ? last : s.sessions.rx_ms;  // never before the snapshot
}

DataLink data_link(const AppState& s) {
  if (s.link.mode == LinkMode::Ready) return s.sessions.live ? DataLink::Live : DataLink::Never;
  if (s.link.mode == LinkMode::Attached) return s.link.version_mismatch ? DataLink::Other : DataLink::NoCpad;
  return s.sessions.live ? DataLink::Stale : DataLink::Never;
}

SessionLabel session_label(const ProjectsView& p, const SessionRow& r) {
  const Opt<ProjectRow> project = project_by_slot(p, r.slot);
  const char* key = project.has ? project.value.key.c_str() : "?";
  const size_t n = project.has ? project.value.key.size() : 1;
  const bool keyed = r.name.size() > n && std::strncmp(r.name.c_str(), key, n) == 0 && r.name.c_str()[n] == '/';
  if (keyed) return SessionLabel::from(r.name.c_str(), r.name.size());
  return SessionLabel::from(key, n).append("/").append(r.name.c_str(), r.name.size());
}

FixedStr<8> short_sid(const SessionsView& v, const Sid& sid) {
  constexpr size_t kShort = 4;
  for (uint8_t i = 0; i < v.data.n; ++i) {
    const Sid& other = v.data.rows[i].sid;
    if (!(other == sid) && std::strncmp(other.c_str(), sid.c_str(), kShort) == 0) return FixedStr<8>::from(sid.c_str());
  }
  return FixedStr<8>::from(sid.c_str(), sid.size() < kShort ? sid.size() : kShort);
}

bool reply_reaches(const AppState& s, const SessionRow& r) {
  return s.link.mode == LinkMode::Ready && s.link.cap_reply && r.kind == SessionKind::Bg && r.st == SessionState::Idle;
}

Opt<int64_t> wall_ms(const TimeInfo& t, uint32_t now) {
  if (!t.known) return {};
  return some(static_cast<int64_t>(now) + t.offset_ms);
}

Revs revs_of(const AppState& s) {
  Revs r;
  r.projects = s.projects.data.rev;
  r.sessions = s.sessions.data.rev;
  r.queue = s.queue.data.rev;
  return r;
}

}  // namespace cpa
