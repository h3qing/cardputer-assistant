// Mirror of protocol/limits.json (05 §6.1-§6.4, §6.7). test_limits_match_json checks
// that every key of the JSON file appears here with the same value.
// (Named protocol_limits.h, not limits.h, so it can never shadow the C <limits.h>.)
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "domain.h"
#include "messages.h"

namespace cpa {
namespace limits {

constexpr int kProtocolVersion = 0;

constexpr size_t kLineMax = 2048;
constexpr size_t kIdMax = 24;
constexpr const char kIdPattern[] = "^[A-Za-z0-9-]{1,24}$";
constexpr const char kTypePattern[] = "^[a-z][a-z0-9_.]{1,31}$";
constexpr const char kMonoIdPattern[] =
    "^[0-9A-HJKMNP-TV-Z]{7}-[0-9A-HJKMNP-TV-Z]{5}-[0-9A-HJKMNP-TV-Z]{4}$";
constexpr const char kBootPattern[] = "^[0-9A-HJKMNP-TV-Z]{7}-[0-9A-HJKMNP-TV-Z]{5}$";

constexpr const char kProjectKeyPattern[] = "^[a-z0-9][a-z0-9-]{0,11}$";
constexpr uint8_t kSlotMin = 1;
constexpr uint8_t kSlotMax = 9;
constexpr size_t kProjectNameMax = 24;
constexpr size_t kSessionNameMax = 32;
constexpr size_t kQueueTextMax = 80;
constexpr size_t kTextMin = 1;
constexpr size_t kTextMax = 500;
constexpr uint8_t kDeviceCharMin = 32;
constexpr uint8_t kDeviceCharMax = 126;
constexpr const char kSidPattern[] = "^[0-9a-f]{8}$";
constexpr size_t kShortMax = 16;
constexpr const char kQidPattern[] = "^q[0-9]{1,10}$";

constexpr int64_t kEpochMsMin = 1735689600000LL;
constexpr int16_t kTzMinMin = -840;
constexpr int16_t kTzMinMax = 840;

constexpr uint8_t kSnapshotProjectsMax = 9;
constexpr uint8_t kSnapshotSessionsMax = 10;
constexpr uint8_t kSnapshotQueueMax = 12;
constexpr uint8_t kSnapshotQueuePerProjectMax = 4;

constexpr uint32_t kHostInflightMax = 4096;
constexpr uint32_t kHostReplyMax = 256;
constexpr uint32_t kDeviceRxBuffer = 8192;
constexpr uint8_t kDeviceOutboxInflightMax = 4;
constexpr uint8_t kDeviceAgentInflightMax = 1;
constexpr uint8_t kDeviceQueueInboxInflightMax = 1;
constexpr uint8_t kDeviceLinesPerLoop = 4;
constexpr uint8_t kDeviceErrorsPerS = 5;

constexpr uint32_t kHelloRetryMs = 2000;
constexpr uint8_t kHelloAttempts = 3;
constexpr uint32_t kPingIntervalMs = 5000;
constexpr uint32_t kCreditReclaimMs = 5000;
constexpr uint32_t kLinkTimeoutMs = 15000;
constexpr uint32_t kAgentTimeoutMs = 30000;

// v0.3 recording transfer (docs/11 §5.5).
constexpr uint8_t kFileListMax = 4;
constexpr uint16_t kFileGetB64Max = 1380;
constexpr uint8_t kFileInflightMax = 4;
constexpr uint16_t kFilePendingMax = 256;
constexpr uint8_t kFileMarksMax = 16;
constexpr uint16_t kFileSegMax = 9999;
constexpr const char kSha256Pattern[] = "^[0-9a-f]{64}$";
constexpr uint16_t kLeadMsMax = 2000;
constexpr uint32_t kRecMinMs = 1000;
constexpr uint32_t kMemoMaxMs = 300000;
constexpr uint32_t kMeetingMaxMs = 10800000;
constexpr uint32_t kSegMsDefault = 300000;
constexpr uint32_t kSegMsMin = 10000;
constexpr uint32_t kSegMsMax = 600000;

// v0.4.1 recordings app and capture destinations (docs/13 §12.2).
constexpr uint8_t kRecListMax = 6;
constexpr size_t kRecTitleMax = 48;
constexpr size_t kRecCursorMax = 32;
constexpr const char kRecCursorPattern[] = "^[A-Za-z0-9_-]{1,32}$";
constexpr size_t kHostLabelMax = 24;
constexpr const char kHostIdPattern[] = "^[a-z0-9-]{8,32}$";
constexpr const char kDestPattern[] = "^[a-z][a-z0-9-]{0,11}(\\.[a-z0-9-]{1,12}){0,2}$";
constexpr size_t kDestMax = 24;

// Derived bounds used by the codec (JSON Schema "uint32", "count", "epochMs" maximum).
constexpr int64_t kU32Max = 4294967295LL;
constexpr int64_t kCountMax = 65535;
constexpr int64_t kJsonSafeIntMax = 9007199254740991LL;
constexpr int64_t kWavHeaderBytes = 44;
constexpr int64_t kPcmBytesPerMs = 32;  // 16 kHz, 16 bit, mono
constexpr int64_t kFileSizeMax = kWavHeaderBytes + kSegMsMax * kPcmBytesPerMs;  // fileSize maximum
constexpr size_t kB64Max = 4 * ((kFileGetB64Max + 2) / 3);                    // 1840 characters

static_assert(kSnapshotProjectsMax == kMaxProjects, "core capacity drifted from limits.json");
static_assert(kSnapshotSessionsMax == kMaxSessions, "core capacity drifted from limits.json");
static_assert(kSnapshotQueueMax == kMaxQueueItems, "core capacity drifted from limits.json");
static_assert(kSlotMin == cpa::kSlotMin && kSlotMax == cpa::kSlotMax, "slot range drifted");
static_assert(kAgentTimeoutMs == cpa::kAgentTimeoutMs, "agent timeout drifted");
static_assert(ProjectName::capacity() == kProjectNameMax && SessionName::capacity() == kSessionNameMax,
              "name capacities drifted");
static_assert(QueueText::capacity() == kQueueTextMax && UserText::capacity() == kTextMax, "text capacities drifted");
static_assert(MsgId::capacity() == kIdMax && ShortId::capacity() == kShortMax, "id capacities drifted");
static_assert(kFileListMax == proto::kFileListMax && kFileMarksMax == proto::kFileMarksMax &&
                  kFileGetB64Max == proto::kFileGetMax,
              "file.* capacities drifted");
static_assert(kRecListMax == proto::kRecListMax && proto::RecTitle::capacity() == kRecTitleMax &&
                  proto::RecCursor::capacity() == kRecCursorMax && proto::HostLabel::capacity() == kHostLabelMax &&
                  proto::DestId::capacity() == kDestMax && proto::HostId::capacity() == 32,
              "v0.4.1 capacities drifted");

struct Mirror {
  const char* key;
  int64_t number;
  const char* pattern;  // non-null for *_pattern keys
};

inline constexpr Mirror kMirrors[] = {
    {"protocol_version", kProtocolVersion, nullptr},
    {"line_max", kLineMax, nullptr},
    {"id_max", kIdMax, nullptr},
    {"id_pattern", 0, kIdPattern},
    {"type_pattern", 0, kTypePattern},
    {"monoid_pattern", 0, kMonoIdPattern},
    {"boot_pattern", 0, kBootPattern},
    {"project_key_pattern", 0, kProjectKeyPattern},
    {"slot_min", kSlotMin, nullptr},
    {"slot_max", kSlotMax, nullptr},
    {"project_name_max", kProjectNameMax, nullptr},
    {"session_name_max", kSessionNameMax, nullptr},
    {"queue_text_max", kQueueTextMax, nullptr},
    {"text_min", kTextMin, nullptr},
    {"text_max", kTextMax, nullptr},
    {"device_char_min", kDeviceCharMin, nullptr},
    {"device_char_max", kDeviceCharMax, nullptr},
    {"sid_pattern", 0, kSidPattern},
    {"short_max", kShortMax, nullptr},
    {"qid_pattern", 0, kQidPattern},
    {"epoch_ms_min", kEpochMsMin, nullptr},
    {"tz_min_min", kTzMinMin, nullptr},
    {"tz_min_max", kTzMinMax, nullptr},
    {"snapshot_projects_max", kSnapshotProjectsMax, nullptr},
    {"snapshot_sessions_max", kSnapshotSessionsMax, nullptr},
    {"snapshot_queue_max", kSnapshotQueueMax, nullptr},
    {"snapshot_queue_per_project_max", kSnapshotQueuePerProjectMax, nullptr},
    {"host_inflight_max", kHostInflightMax, nullptr},
    {"host_reply_max", kHostReplyMax, nullptr},
    {"device_rx_buffer", kDeviceRxBuffer, nullptr},
    {"device_outbox_inflight_max", kDeviceOutboxInflightMax, nullptr},
    {"device_agent_inflight_max", kDeviceAgentInflightMax, nullptr},
    {"device_queue_inbox_inflight_max", kDeviceQueueInboxInflightMax, nullptr},
    {"device_lines_per_loop", kDeviceLinesPerLoop, nullptr},
    {"device_errors_per_s", kDeviceErrorsPerS, nullptr},
    {"hello_retry_ms", kHelloRetryMs, nullptr},
    {"hello_attempts", kHelloAttempts, nullptr},
    {"ping_interval_ms", kPingIntervalMs, nullptr},
    {"credit_reclaim_ms", kCreditReclaimMs, nullptr},
    {"link_timeout_ms", kLinkTimeoutMs, nullptr},
    {"agent_timeout_ms", kAgentTimeoutMs, nullptr},
    {"file_list_max", kFileListMax, nullptr},
    {"file_get_b64_max", kFileGetB64Max, nullptr},
    {"file_inflight_max", kFileInflightMax, nullptr},
    {"file_pending_max", kFilePendingMax, nullptr},
    {"file_marks_max", kFileMarksMax, nullptr},
    {"file_seg_max", kFileSegMax, nullptr},
    {"sha256_pattern", 0, kSha256Pattern},
    {"lead_ms_max", kLeadMsMax, nullptr},
    {"rec_min_ms", kRecMinMs, nullptr},
    {"memo_max_ms", kMemoMaxMs, nullptr},
    {"meeting_max_ms", kMeetingMaxMs, nullptr},
    {"seg_ms_default", kSegMsDefault, nullptr},
    {"seg_ms_min", kSegMsMin, nullptr},
    {"seg_ms_max", kSegMsMax, nullptr},
    {"rec_list_max", kRecListMax, nullptr},
    {"rec_title_max", kRecTitleMax, nullptr},
    {"rec_cursor_max", kRecCursorMax, nullptr},
    {"rec_cursor_pattern", 0, kRecCursorPattern},
    {"host_label_max", kHostLabelMax, nullptr},
    {"host_id_pattern", 0, kHostIdPattern},
    {"dest_pattern", 0, kDestPattern},
    {"dest_max", kDestMax, nullptr},
};

inline constexpr size_t kMirrorCount = sizeof(kMirrors) / sizeof(kMirrors[0]);

inline const Mirror* find_mirror(const char* key) {
  for (const Mirror& m : kMirrors) {
    if (std::strcmp(m.key, key) == 0) return &m;
  }
  return nullptr;
}

}  // namespace limits
}  // namespace cpa
