// Slice reducers for the snapshot-driven parts of AppState (05 §5.2, §5.6).
// Each takes the current slice by const reference and returns a new one.
#pragma once

#include <cstdint>

#include "app_state.h"
#include "step.h"

namespace cpa {

// Emitted when slot/key/name changed: the shell rewrites /cfg/projects.json.
enum class ProjectsEvent : uint8_t { IdentityChanged };

// Emitted for each session whose need became true: toast "<key>/<name> 等你".
struct NeedToast {
  uint8_t index = 0;  // row in the new SessionsView
};

Step<ProjectsView, ProjectsEvent> reduce_projects(const ProjectsView& v, const ProjectsData& d, uint32_t now);
Step<SessionsView, NeedToast> reduce_sessions(const SessionsView& v, const SessionsData& d, uint32_t now);
QueueView reduce_queue(const QueueView& v, const QueueData& d, uint32_t now);
TimeInfo reduce_time(const TimeInfo& t, const TimeData& d, uint32_t now);

Opt<ProjectRow> project_by_slot(const ProjectsView& v, uint8_t slot);
uint16_t need_count(const SessionsView& v);
// Sessions running in every project and in none ("跑N" on home and in the Tab menu); 0 while
// the board is only the cached names (no counts yet).
uint32_t running_count(const ProjectsView& v);
uint8_t queue_count_for_slot(const QueueView& v, uint8_t slot);
// age_s as sent, plus the time since the snapshot arrived (never bumps rev).
uint32_t session_age_s(const SessionsView& v, uint8_t index, uint32_t now);
// docs/20 U19: the moment what the Mac said still holds: now while the link is Ready, else
// its last line (ages stop there: away from the Mac nothing is known to have moved on).
uint32_t data_as_of(const AppState& s, uint32_t now);
inline uint32_t data_age_s(const AppState& s, uint32_t now) { return (now - data_as_of(s, now)) / 1000; }
// docs/20 U19: how much the screens know of the Mac. Live: Ready with a sessions snapshot.
// Never: nothing since boot. NoCpad: the cable is in, cpad does not answer. Other: it answers
// in another version. Stale: the cable is out; the screens show what the Mac last said.
enum class DataLink : uint8_t { Live, Never, NoCpad, Other, Stale };
DataLink data_link(const AppState& s);
Opt<int64_t> wall_ms(const TimeInfo& t, uint32_t now);

// docs/20 U14: "key/name" as every screen names a session; a name that already starts with
// "key/" (one the device started) is not given the key twice. No project: "?/name".
using SessionLabel = FixedStr<48>;
SessionLabel session_label(const ProjectsView& p, const SessionRow& r);
// docs/20 U14: the sid's first 4 characters, or all 8 when another listed session shares them.
FixedStr<8> short_sid(const SessionsView& v, const Sid& sid);
// docs/20 U7: Enter on this session sends the reply to it (else it is only stored on the Mac):
// the link is Ready, cpad takes replies, and the session runs in the background, idle. The
// confirm box decides the same (confirm_decide).
bool reply_reaches(const AppState& s, const SessionRow& r);
Revs revs_of(const AppState& s);

}  // namespace cpa
