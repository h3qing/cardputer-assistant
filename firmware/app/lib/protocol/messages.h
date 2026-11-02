// Protocol v0 message catalogue (05 §6.5) as plain value types. The variant order
// of Body equals MsgType, so msg_type() is the variant index. debug.* exist only in
// test firmware (CPA_TEST_HOOKS): the release build neither parses nor names them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>

#include "dest_messages.h"
#include "domain.h"
#include "err_code.h"
#include "lang.h"
#include "mono_id.h"
#include "opt.h"

namespace cpa {
namespace proto {

enum class MsgType : uint8_t {
  Hello,
  Ping,
  Pong,
  Ack,
  Error,
  TimeSet,
  ProjectsSnapshot,
  SessionsSnapshot,
  QueueSnapshot,
  OutboxItem,
  OutboxAck,
  AgentSpawn,
  AgentReply,
  AgentResult,
  QueueDone,
  InboxDismiss,
  FileList,  // v0.3 recording transfer (docs/11 §5.3)
  FileItems,
  FileGet,
  FileData,
  FileDel,
  RecList,  // v0.4.1 recordings app (docs/13 §8, §12.2)
  RecItems,
  RecAct,
  DestsSnapshot,  // v0.5 capture destinations (docs/14 §7.2): decoded, not taken before F5
  DebugKey,  // debug.* last: absent from release builds
  DebugState,
  DebugReboot,
  DebugRec,
  DebugMic,  // v0.3.1
  DebugCodec,  // v0.3.2
  DebugLang,  // v0.4.3 (docs/21 §7)
};

#if CPA_TEST_HOOKS
constexpr uint8_t kMsgTypeCount = 32;
#else
constexpr uint8_t kMsgTypeCount = 25;
#endif

constexpr uint8_t kFileListMax = 4;      // file_list_max
constexpr uint8_t kFileMarksMax = 16;    // file_marks_max
constexpr uint16_t kFileGetMax = 1380;   // file_get_b64_max
constexpr uint8_t kRecListMax = 6;       // rec_list_max (v0.4.1)

// v0.4.1 text fields (protocol/README item 20); capacities are limits.json byte limits.
using DestId = FixedStr<24>;     // dest_max: capture destination such as "todo"
using HostId = FixedStr<32>;     // host_id_pattern {8,32}
using HostLabel = FixedStr<24>;  // host_label_max
using RecCursor = FixedStr<32>;  // rec_cursor_max: opaque, sent back unchanged
using RecTitle = FixedStr<48>;   // rec_title_max

using FieldPath = FixedStr<48>;  // "sessions[3].name"
using ErrText = FixedStr<80>;

struct HostHello {
  FixedStr<32> app;
  AgentStatus agent = AgentStatus::Missing;
  bool cap_spawn = false;
  bool cap_reply = false;
  // v0.4.1 (docs/13 §11.1): unset, null and false all mean "not supported".
  Opt<bool> cap_recs;         // cpad answers rec.list / rec.act
  Opt<bool> cap_todo;         // cpad writes a dest "todo" note as a checkbox
  Opt<HostId> host_id;        // reserved: the device does not act on it before v0.8
  Opt<HostLabel> host_label;  // shown on the control page only
};

// hello.store (v0.2, 09 §6): the primary store chosen at boot. Wire names "sd" / "int" / "none".
enum class StoreKind : uint8_t { Sd, Int, None };

// v0.3 hello.files / pong.files (docs/11 §5.2): absent on devices that cannot serve file.*.
struct FilesInfo {
  uint16_t n = 0;     // committed segments not yet deleted (0..256)
  uint32_t rev = 0;   // 0 = boot recovery scan not finished
  bool live = false;  // the recording state machine is not Idle
  bool operator==(const FilesInfo& o) const { return n == o.n && rev == o.rev && live == o.live; }
};

struct DeviceHello {
  FixedStr<31> fw;   // cpa-fw/<version>
  FixedStr<12> dev;  // MAC, 12 lower-case hex digits
  BootIdStr boot;
  uint32_t up_ms = 0;
  FixedStr<16> rst;
  bool time = false;
  uint16_t pending = 0;
  uint16_t bat_mv = 0;
  Revs revs;
  // v0.2 optional fields: left unset, the encoded hello is byte-identical to v0.1.
  Opt<StoreKind> store;
  Opt<bool> launcher;  // running under the bmorcelli Launcher
  Opt<FilesInfo> files;  // v0.3, after launcher; unset = byte-identical to v0.2
};

struct Hello {
  bool from_host = true;  // role: host (request) or device (reply)
  HostHello host;
  DeviceHello device;
};

struct Ping {};
struct Ack {};
struct DebugReboot {};

struct Pong {
  uint32_t up_ms = 0;
  uint16_t pending = 0;
  Revs revs;
  Opt<FilesInfo> files;  // v0.3, after revs
};

struct ErrorMsg {
  ErrCode code = ErrCode::Internal;
  Opt<FieldPath> field;
  Opt<ErrText> msg;
};

struct OutboxItem {  // envelope id is the MonoId
  ItemKind kind = ItemKind::Note;
  uint32_t up_ms = 0;
  Opt<int64_t> wall_ms;  // absent = device time unknown (sent as null)
  NoteBody note;         // kind == Note
  QueueBody queue;       // kind == Queue
  Opt<DestId> dest;      // v0.4.1, kind == Note: body.dest; unset = the daily note
};

enum class AckStatus : uint8_t { Stored, Duplicate, Rejected };

struct OutboxAck {  // envelope re is the MonoId
  AckStatus status = AckStatus::Stored;
  Opt<ErrCode> code;  // required when Rejected
};

struct AgentSpawn {
  ProjectKey project;
  UserText text;
  Opt<Qid> qid;
};

struct AgentReply {
  Sid sid;
  UserText text;
};

struct AgentResult {
  bool ok = false;
  Opt<ErrCode> code;  // required when !ok
  Opt<SessionName> name;
  Opt<ShortId> short_id;
};

struct QueueDone {
  Qid qid;
};

struct InboxDismiss {
  Sid sid;
};

// ---- v0.3 recording transfer (docs/11 §5.3). Ids stay text: the schema only asks for
// the MonoId pattern, whose 7-digit epoch can exceed the 32 bits a MonoId holds.
struct FileList {  // h2d request
  Opt<MonoIdStr> after;  // exclusive
  Opt<uint8_t> max;      // 1..4, absent = file_list_max
};

struct RecAt {
  uint32_t up_ms = 0;
  Opt<int64_t> wall_ms;  // null when the device did not know the time
};

struct FileItem {
  MonoIdStr id;
  uint32_t size = 0;  // incl. the 44 B WAV header
  Opt<Sha256Hex> sha256;
  MonoIdStr rec;
  uint16_t seg = 0;
  bool last = false;
  RecMode mode = RecMode::Memo;
  uint32_t dur_ms = 0;
  Opt<RecAt> at;  // sent as null when absent
  Opt<ProjectKey> project;
  Opt<uint8_t> slot;
  bool has_stars = false;  // an empty array is still sent
  uint8_t n_stars = 0;
  uint32_t stars[kFileMarksMax] = {};
  Opt<uint16_t> lead_ms;
  bool consent = false;  // "ack"
  Opt<bool> rcv;
  Opt<DestId> dest;  // v0.4.1: memo segment 0 only; unset = the daily note
};

struct FileItems {  // d2h reply
  uint8_t n = 0;
  FileItem items[kFileListMax];
  bool more = false;
};

struct FileGet {  // h2d request
  MonoIdStr fid;
  uint32_t off = 0;
  uint16_t len = 0;  // 1..1380
};

struct FileData {  // d2h reply; b64 on the wire
  MonoIdStr fid;
  uint32_t off = 0;
  uint16_t n = 0;
  uint32_t size = 0;
  uint32_t crc = 0;  // CRC-32/IEEE of bytes[0, n)
  uint8_t bytes[kFileGetMax] = {};
};

struct FileDel {  // h2d request
  MonoIdStr fid;
  uint32_t size = 0;
  Opt<Sha256Hex> sha256;
};

// ---- v0.4.1 recordings app (docs/13 §8.1-§8.4). Only the device's recordings app sends
// rec.list / rec.act, and only after a host hello with caps.recs (protocol/README item 20).
enum class RecFilter : uint8_t { All, Starred, Memo, Meeting };
enum class RecOp : uint8_t { Star, Unstar, Tag, Hide };  // no delete (docs/13 §8.4)
// cpad's sessions.state_of values.
enum class RecState : uint8_t { OnDevice, Pulling, Archived, Processing, Blocked, Done, Transcript, Failed, Stale };

struct RecList {  // d2h request
  RecFilter filter = RecFilter::All;
  uint8_t max = kRecListMax;  // 1..6
  Opt<RecCursor> cursor;      // the previous page's next; unset = first page
};

struct RecRow {  // optional fields unset: unknown, not starred, no project
  MonoIdStr rec;  // session id = id of segment 0
  RecMode mode = RecMode::Memo;
  RecState st = RecState::OnDevice;
  Opt<int64_t> wall_ms;
  Opt<uint32_t> dur_ms;
  Opt<ProjectKey> project;
  Opt<bool> starred;
  Opt<uint16_t> marks;  // stars set while recording
  Opt<RecTitle> title;
  Opt<uint16_t> todo_n;  // unchecked boxes in summary.md
};

struct RecPage {  // h2d reply rec.items: at most max rows, each rec once
  uint8_t n = 0;
  RecRow rows[kRecListMax];
  Opt<RecCursor> next;  // set while another page follows
};

struct RecAct {  // d2h request
  MonoIdStr rec;
  RecOp op = RecOp::Star;
  Opt<ProjectKey> project;  // Tag only, unset = no project (sent as null)
};

struct DebugRec {  // h2d, test firmware only
  uint32_t seg_ms = 0;
};

struct DebugMic {  // h2d, test firmware only (v0.3.1): the gain for the next recordings
  uint8_t pga = 0;  // ES8311 REG14 PGA, 0-10 (3 dB steps)
  uint8_t vol = 0;  // ES8311 REG17 ADC volume, 0-255 (0xBF = 0 dB)
  uint8_t mag = 0;  // M5Unified magnification, 1-64 (2 = unity)
};

struct DebugCodec {  // h2d, test firmware only (v0.3.2, issue #31): the codec as a power-on leaves it
  bool warm = false;  // then the boot's warm-up, as after a real power-on
};

struct DebugLang {  // h2d, test firmware only (v0.4.3, docs/21 §7): the UI language, as the settings page sets it
  Lang lang = Lang::En;
};

struct DebugKey {
  uint8_t n = 0;
  uint8_t keys[3] = {};  // key_names.h indices
  Opt<uint16_t> hold_ms;
};

enum class Overlay : uint8_t { None, Compose, Confirm };

struct DebugProject {
  uint8_t slot = 0;
  uint16_t run = 0;
  uint16_t need = 0;
  uint16_t queued = 0;
};

struct DebugRecState {  // v0.3 debug.state.rec
  bool live = false;
  Opt<RecMode> mode;  // null while idle
  Opt<uint16_t> seg;  // null while idle
  uint16_t files_n = 0;
  uint32_t files_rev = 0;
  // Health of the current recording, or the last one while idle (G-A2, G-A4, G-A6);
  // unset = not measured by this build.
  Opt<uint16_t> ring_high;    // most full mic blocks waiting at once
  Opt<uint32_t> mic_missing;  // capture re-arms that found no free block
  Opt<uint32_t> rot_ms_max;   // slowest segment change
  Opt<uint32_t> heap_free;    // lowest free heap of the recording
  Opt<uint32_t> heap_block;   // lowest largest free block of the recording
};

struct DebugUi {  // v0.4.0 debug.state.ui (docs/13 §12.1); overlay still reports the layer under the menu
  bool menu = false;    // the Tab menu is open
  bool locked = false;  // the device is locked (Opt+L)
  Opt<Lang> lang;       // v0.4.3 (docs/21 §7): the UI language shown; unset: not sent
};

// debug.state.diag (gh #21, protocol README item 21): the device's health counters, in
// schema order. Bytes, microseconds and counts since boot; the loop_* counters wrap.
struct DebugDiag {
  uint32_t heap_free = 0;         // sampled once a second
  uint32_t heap_min = 0;          // lowest since boot
  uint32_t heap_block = 0;        // largest free block, sampled once a second
  uint32_t stack_left = 0;        // loop task stack never used
  uint32_t audio_stack_left = 0;  // audio task's, 0 without it
  uint32_t loop_n = 0;
  uint32_t loop_max_us = 0;
  uint32_t loop_win_max_us = 0;  // the last 15-30 s
  uint32_t gap_max_us = 0;       // pass start to the next pass start
  uint32_t gap_win_max_us = 0;
  uint32_t loop_over_20 = 0;  // passes longer than 20 ms
  uint32_t loop_over_50 = 0;
  uint32_t loop_over_100 = 0;
  uint32_t loop_over_500 = 0;
  uint32_t ghost_n = 0;      // count (0-65535)
  uint32_t kb_reinit_n = 0;  // count (0-65535)
};

struct DebugState {  // request (no fields) or the device's summary reply
  bool summary = false;
  uint32_t up_ms = 0;
  LinkMode link = LinkMode::Detached;
  FixedStr<16> mod;
  Overlay overlay = Overlay::None;
  Opt<uint32_t> overlay_ms;
  Opt<uint32_t> key_ms;
  uint16_t outbox_pending = 0;
  uint16_t outbox_inflight = 0;
  uint16_t outbox_bad = 0;
  Revs revs;
  uint16_t need = 0;
  uint8_t n = 0;
  DebugProject projects[kMaxProjects];
  Opt<DebugRecState> rec;  // v0.3
  Opt<DebugUi> ui;         // v0.4.0; unset: not sent, the line is byte-identical to v0.3.1
  Opt<DebugDiag> diag;     // gh #21; unset: not sent
};

using Body = std::variant<Hello, Ping, Pong, Ack, ErrorMsg, TimeData, ProjectsData, SessionsData, QueueData,
                          OutboxItem, OutboxAck, AgentSpawn, AgentReply, AgentResult, QueueDone, InboxDismiss,
                          FileList, FileItems, FileGet, FileData, FileDel, RecList, RecPage, RecAct, DestsSnapshot,
                          DebugKey, DebugState, DebugReboot, DebugRec, DebugMic, DebugCodec, DebugLang>;

// rec.items is decoded in place like every other message: the page must fit in the space
// file.data already takes, so Message (and the codec's static result) does not grow.
static_assert(sizeof(RecPage) < sizeof(FileData), "rec.items must not grow Message");
static_assert(sizeof(DestsSnapshot) < sizeof(FileData), "dests.snapshot must not grow Message");

struct Message {
  MsgId id;
  Opt<MsgId> re;  // present on replies only
  Body body;
};

inline MsgType msg_type(const Message& m) { return static_cast<MsgType>(m.body.index()); }
Message make_message(const MsgId& id, const Opt<MsgId>& re, const Body& body);

const char* type_name(MsgType t);  // "" for types absent from this build
Opt<MsgType> type_parse(const char* s, size_t n);

}  // namespace proto
}  // namespace cpa
