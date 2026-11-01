// Domain vocabulary shared by the AppState slices and the protocol messages
// (05 §5.3, §6.5, §6.7). Capacities here are mirrored and checked against
// protocol/limits.json by protocol/limits.h and test_limits_match_json.
#pragma once

#include <cstdint>

#include "fixed_str.h"
#include "opt.h"

namespace cpa {

constexpr uint8_t kMaxProjects = 9;      // snapshot_projects_max, slots 1..9
constexpr uint8_t kMaxSessions = 10;     // snapshot_sessions_max
constexpr uint8_t kMaxQueueItems = 12;   // snapshot_queue_max
constexpr uint8_t kSlotMin = 1;
constexpr uint8_t kSlotMax = 9;
constexpr uint32_t kAgentTimeoutMs = 30000;

enum class LinkMode : uint8_t { Detached, Attached, Ready };
enum class AgentStatus : uint8_t { Ok, Missing, Error };
enum class SessionKind : uint8_t { Tty, Bg };
enum class SessionState : uint8_t { Busy, Shell, Idle, Waiting, Unknown };
enum class Intent : uint8_t { Todo, Spawn, Feedback };
enum class ItemKind : uint8_t { Note, Queue };
enum class RecMode : uint8_t { Memo, Meeting };  // v0.3 recording (docs/11 §3)

using ProjectKey = FixedStr<12>;
using ProjectName = FixedStr<24>;
using SessionName = FixedStr<32>;
using Sid = FixedStr<8>;
using ShortId = FixedStr<16>;
using Qid = FixedStr<11>;
using QueueText = FixedStr<80>;
using UserText = FixedStr<500>;
using MsgId = FixedStr<24>;
using Sha256Hex = FixedStr<64>;  // v0.3: 64 lower-case hex digits

struct Revs {
  uint32_t projects = 0;
  uint32_t sessions = 0;
  uint32_t queue = 0;
};

struct ProjectRow {
  uint8_t slot = 0;
  ProjectKey key;
  ProjectName name;
  uint16_t run = 0;
  uint16_t need = 0;
  uint16_t queued = 0;
  bool ok = false;
};

struct OtherCount {
  uint16_t run = 0;
  uint16_t n = 0;
};

struct ProjectsData {  // payload of projects.snapshot
  uint32_t rev = 0;
  uint8_t n = 0;
  ProjectRow rows[kMaxProjects];
  OtherCount other;
};

struct SessionRow {
  Sid sid;
  uint8_t slot = 0;
  SessionName name;
  SessionKind kind = SessionKind::Tty;
  SessionState st = SessionState::Unknown;
  bool need = false;
  uint32_t age_s = 0;
  Opt<ShortId> short_id;
};

struct SessionsData {  // payload of sessions.snapshot
  uint32_t rev = 0;
  uint16_t total = 0;
  uint8_t n = 0;
  SessionRow rows[kMaxSessions];
};

struct QueueRow {
  Qid qid;
  uint8_t slot = 0;
  QueueText text;
  Intent intent = Intent::Todo;
  bool uncertain = false;  // v0.4.1 (docs/20 U25): saved after an unknown agent result; fills padding
  Opt<Sid> sid;
};

struct QueueData {  // payload of queue.snapshot
  uint32_t rev = 0;
  uint8_t n = 0;
  QueueRow rows[kMaxQueueItems];
};

struct TimeData {  // payload of time.set
  int64_t epoch_ms = 0;
  int16_t tz_min = 0;
};

struct NoteBody {
  UserText text;
  Opt<ProjectKey> project;  // absent = unassigned
};

struct QueueBody {
  UserText text;
  ProjectKey project;
  Intent intent = Intent::Todo;
  Opt<Sid> sid;            // required when intent == Feedback
  bool uncertain = false;  // saved after an unknown agent result
};

}  // namespace cpa
