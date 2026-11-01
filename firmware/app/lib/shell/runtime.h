// Runtime (05 §5.1): the single-threaded main loop around the pure cores. It owns the
// AppState double buffer (copy the current buffer, replace one slice, flip; the old
// state is never changed in place), the link, the outbox, the codec and the shell, and
// moves bytes between them and the ports. One loop() pass:
//   kick TX -> <= 4 inbound lines -> keys -> link timer -> agent timer -> outbox ->
//   stats (1 s) -> toasts -> render (dirty, <= 20 fps, and once a second; v0.5 S3: between
//   them, while the backlight is On, an animation tick or slide frame pushes small rects)
// Nothing here blocks; the only storage writes are one outbox item or the small
// /cfg/projects.json cache. Storage (docs/09-m2-design.md §3.1): the primary store is
// the FileStore given to the constructor, chosen by setup() with plan_storage(); with
// the SD card primary, begin() first moves cpafs's pending items to it.
#pragma once

#include <cstdint>

#include "file_req.h"
#include "file_store.h"
#include "ghost_filter.h"
#include "kb_watch.h"
#include "line_framer.h"
#include "link_session.h"
#include "mono_id.h"
#include "outbox_core.h"
#include "ledger_policy.h"
#include "outbox_migrate.h"
#include "platform.h"
#include "plug_hint.h"
#include "plug_filter.h"
#include "power.h"
#include "rec_core.h"
#include "rec_port.h"
#include "shell.h"
#include "shell_port.h"
#include "storage_plan.h"
#include "transport.h"
#if CPA_TEST_HOOKS
#include "debug_inject.h"
#endif

// Keeps a helper's large temporaries (a 2.9 KB OutboxStep, a 2 KB Line) in a frame of
// its own instead of the caller's, so they are never on the loop stack together
// (scripts/stack_budget.py checks the chains).
#define CPA_NOINLINE __attribute__((noinline))

namespace cpa {

constexpr char kFwVersion[] = "cpa-fw/0.5.1";  // bumped by the Lead at release (v0.2 tests pin it)
constexpr uint32_t kRenderMinMs = 50;     // <= 20 fps
constexpr uint32_t kRenderEveryMs = 1000; // clock, ages
constexpr uint32_t kStatsEveryMs = 1000;
// LittleFS usage is a full metadata walk (537 ms with 200 items, measured on the
// device): free space is read once at boot and then estimated, one block per item.
constexpr uint32_t kItemBlockBytes = 4096;
constexpr uint32_t kSmallReqTimeoutMs = 10000;
constexpr size_t kRxChunk = 256;

struct BootInfo {
  BootTicket ticket;  // invalid when the NVS boot counter could not be advanced
  FixedStr<12> dev;
  FixedStr<16> rst;
};

// What setup() learned about storage (09 §3.1). The default is v0.1's: the store given
// to the constructor is the internal one, full flash, nothing to migrate.
struct StorageBoot {
  StoragePlan plan = StoragePlan{Store::Internal, false, StoreNote::None};
  SdFacts sd;
  PartFacts part;
  FileStore* migrate_from = nullptr;    // cpafs, when plan.migrate
  MigrateObserver* observer = nullptr;  // after each migrated item: kick USB, progress
};

// v0.3 file.* (docs/11 §4.7): a request the loop accepted, waiting for or with the
// audio task. At most file_inflight_max of them.
struct PendingFile {
  MsgId re;
  proto::MsgType type = proto::MsgType::FileGet;
  rec::RecCmd cmd;
  bool more = false;  // list: further pages after this one
};

struct RecSide {
  PendingFile pend[4];
  uint8_t head = 0;
  uint8_t n = 0;
  PendingFile task;      // the request with the audio task
  bool held = false;     // a file.data waiting for room in the TX ring
  rec::RecEvt held_evt;
  bool stop_due = false;  // a Stop the full command queue refused: posted again each tick
  bool free_known = false;
  uint64_t free_bytes = 0;
  RecLast last;  // docs/20 U6: the last outcome, until the screen was seen lit
};

// docs/28 S1a: the kept copies' loop side (runtime_kept.cpp).
struct KeptSide {
  uint32_t host = 0;       // the Mac on the link: its host_id's CRC-32 (0: none)
  rec::ClockRef clock;     // D16: the last wall clock seen
  bool clock_ok = false;   // ... and whether it may age kept copies
  bool tidy_due = false;   // something was kept since the last tidy
  bool busy = false;       // a Ledger command is with the audio task
  rec::LedgerAsk ask = rec::LedgerAsk::Tidy;
  bool incomplete = false;  // D15
  rec::KeptInfo info;
  PlugState plug;
  bool plug_due = false;   // a hello: look for offline segments once the index is ready
  bool hold_due = false;   // the hint's Enter: the Hold not yet taken by the audio task
  bool hold_sent = false;  // ... taken, its LedgerDone not yet here
  uint32_t hold_host = 0;  // the Hold's Mac, ids and recordings, kept from the Enter on:
  MonoId hold_from;        // an unplug before LedgerDone must not forget them (review 3)
  uint8_t hold_n = 0;
  uint32_t hold_since = 0;
  uint32_t busy_since = 0;  // a Ledger command went out (the watchdog, review 7)
  uint32_t plug_seen = 0;   // the last pass: the hint's time stands still while hidden
  bool drop_due = false;   // a Clear with more to delete: asked again
  Opt<OfflineMark> mark;   // what NVS holds
  bool mark_due = false;   // an unplug's mark, written once no recording runs
  OfflineMark mark_next;
};

// queue.done / inbox.dismiss in flight (05 §6.3: at most one).
struct SmallReq {
  bool inflight = false;
  MsgId rid;
  uint32_t sent_ms = 0;
};

// docs/20 U16: the internal cpafs, mounted on demand once the SD card latched while
// running, so a capture is kept (the next boot's migration moves it to the card).
class SpareStore {
 public:
  virtual ~SpareStore() = default;
  virtual FileStore* mount() = 0;  // nullptr: no cpafs
};

class Runtime : public ShellPort {
 public:
  Runtime(Transport& tx, FileStore& fs, Platform& plat, Display& disp, Module* const* modules, uint8_t n);

  // v0.3 recording (docs/11 §4.7): before begin(). Without it the device cannot record:
  // gestures are refused and hello / pong carry no files (the v0.2 bytes).
  void attach_rec(RecPort& port, const RecBoot& boot);
  void attach_spare(SpareStore& spare) { spare_ = &spare; }  // before begin(); optional
  void begin(const BootInfo& boot, uint32_t now, const StorageBoot& storage = StorageBoot{});
  void loop(uint32_t now, KeySet hw_keys);
  MonoCounter& ids() { return ids_; }  // shared with the audio task

  Shell& shell() { return shell_; }
  const Shell& shell() const { return shell_; }
#if CPA_TEST_HOOKS
  // docs/19 §10: the longest full redraw and animation tick so far, by Platform::micros().
  struct RenderTimes {
    uint32_t max_render_us = 0;
    uint32_t max_tick_us = 0;
  };
  const RenderTimes& render_times() const { return times_; }
  void reset_render_times() { times_ = RenderTimes{}; }  // device test: each language timed apart
#endif
  const LinkCore& link() const { return link_; }
  const OutboxCore& outbox() const { return outbox_; }

  // ShellPort
  const AppState& state() const override { return buf_[cur_]; }
  const SysInfo& sys() const override { return sys_; }
  SaveResult save_note(const NoteBody& body) override;
  SaveResult save_queue(const QueueBody& body) override;
  bool send_agent(const AgentDraft& draft) override;
  bool send_request(const OnlineRequest& r) override;
  void set_context_slot(uint8_t slot) override;
  void set_ui(const UiFlags& ui) override;
  rec::RecRefusal rec_gesture(RecMode mode, uint8_t slot) override;
  rec::RecRefusal rec_refusal() const override { return rec::rec_refusal(rec_ctx()); }
  void rec_input(rec::RecInput in, uint8_t arg) override;
  SettingsSave store_lang(Lang l) override;  // runtime_settings.cpp
  bool kept_ask(KeptAsk a) override;          // runtime_kept.cpp

 private:
  // AppState: copy, let `edit` change one slice of the copy, flip; and redraw unless told
  // not to: an audio level alone, which the animation ticks show (v0.5 S3), or a change no
  // page shows before the once-a-second frame (kRenderEveryMs; #43: the link's last_rx_ms).
  template <class Edit>
  void update(Edit edit, bool redraw = true) {
    AppState& next = buf_[cur_ ^ 1];
    next = buf_[cur_];
    edit(next);
    cur_ ^= 1;
    if (redraw) shell_.mark_dirty();
  }

  // runtime.cpp: boot and the loop stages
  void begin_storage(const StorageBoot& sb);
  CPA_NOINLINE void migrate_boot(FileStore& from, MigrateObserver* obs);
  void pump_rx(uint32_t now);
  CPA_NOINLINE FrameEvent scan_once(size_t& lines);
  void pump_keys(uint32_t now, KeySet hw);
  void key_event(const KeyEvent& ev, uint32_t now);
  void pump_timers(uint32_t now);
  void sample_stats(uint32_t now);
  void sync_power(uint32_t now);
  void sync_backlight();
  void render(uint32_t now);
  void render_full(uint32_t now);
  void animate(uint32_t now);  // v0.5 S3: Backlight::On only
  void sync_link_slice();
  void set_moment(Moment m);  // v0.5: Mando reacts on home (docs/19 §7.2)
  uint32_t free_estimate() const;
  // SdStore's free_bytes() is 0 only once a Disk error latched it (sd_store.h): the card
  // failed during the boot scan or migration. A storage error, never "full" (09 §3.8).
  bool sd_lost_at_boot() const { return sys_.store == Store::Sd && boot_free_ == 0; }
  // docs/20 U16: the card latched at boot or since (SdStore's free_bytes() is 0 once
  // latched, and costs nothing: it never scans the FAT).
  bool card_lost() const { return sd_lost_at_boot() || (sys_.store == Store::Sd && fs_.free_bytes() == 0); }
  // runtime_link.cpp: inbound lines and replies
  void handle_line(const char* line, size_t n, uint32_t now);
  void link_effect(const LinkEffect& e, const proto::Decoded& d, uint32_t now);
  void link_changed(const LinkEffect& e, const proto::Decoded* d, uint32_t now);
  void deliver(const proto::Decoded& d, uint32_t now);
  void deliver_snapshot(const proto::Body& b, uint32_t now);
  void deliver_reply(const proto::Decoded& d, uint32_t now);
  void send_reply(const LinkEffect& e, uint32_t now);
  void fill_reply_body(const LinkEffect& e, uint32_t now);
  CPA_NOINLINE bool transmit(TxClass cls);
  // runtime_outbox.cpp: items, acks, agent results
  SaveResult save_item(ItemKind kind, const NoteBody* note, const QueueBody* queue);
  CPA_NOINLINE SaveResult save_spare(const MonoId& id);  // the card latched: cpafs (U16)
  CPA_NOINLINE bool encode_persist(const MonoId& id);
  CPA_NOINLINE void track_added(const MonoId& id);
  CPA_NOINLINE void track_ack(const MonoId& id, proto::AckStatus status, ErrCode code);
  CPA_NOINLINE void track_link_lost();
  void pump_outbox(uint32_t now);
  CPA_NOINLINE Effects<OutboxEffect> tick_outbox(uint32_t now);
  void send_item(const MonoId& id);
  void apply_outbox(const OutboxStep& s);
  void agent_events(const AgentStep& s, uint32_t now);
  void agent_event(const AgentEvent& e, uint32_t now);
  void flush_agent_save();  // a failed / unknown request's text, saved from loop() itself
  void small_reply(const proto::Decoded& d);
  CPA_NOINLINE void save_projects_cache();
  void load_projects_cache();
  void store_failed();  // one more storage error (system page)
  // runtime_rec.cpp: recording and file.* (docs/11 §4.7, §5)
  void begin_rec(uint32_t now);
  bool rec_avail() const;
  rec::RecCtx rec_ctx() const;
  Opt<proto::FilesInfo> files_info() const;
  void pump_rec(uint32_t now);
  void tick_rec();  // rec_tick, after pump_keys
  void rec_event(const rec::RecEvt& e);
  void rec_apply(const rec::RecStep& st);
  void rec_effect(const rec::RecEffect& e);
  void post(const rec::RecCmd& c);
  void retry_stop();
  rec::RecCmd tag_cmd(rec::CmdKind kind, uint8_t slot) const;
  void mirror_rec();
  void rec_link_lost();
  void show_end(const rec::RecEffect& e);  // an Ended or Refuse: the card, or the screen lit
  void rec_seen();  // RecLast: seen once the screen is fully lit, gone once it is dark again
  // The backlight's ● rule (power.h): recording, or (unlocked) a memo's end screen held
  // under G0 (docs/20 U15), never dark either: the G0 press was minutes ago. Locked, the
  // lock screen shows none of it.
  bool rec_keeps_lit() const { return rec::rec_live(rec_core_) || (rec_core_.held && !shell_.locked()); }
  // runtime_files.cpp: file.*, debug.rec, debug.mic and debug.codec
  CPA_NOINLINE bool rec_request(const proto::Decoded& d, uint32_t now);
  CPA_NOINLINE void file_request(const proto::Decoded& d);
  CPA_NOINLINE void file_get(const MsgId& re, const proto::FileGet& get);
  CPA_NOINLINE void file_del(const MsgId& re, const proto::FileDel& del);
  CPA_NOINLINE void file_list(const MsgId& re, const proto::FileList& list);
  CPA_NOINLINE static bool items_fit(void* self, uint8_t k);
  void queue_file(const PendingFile& p);
  void post_next_file();
  void flush_busy();
  void file_reply(const rec::RecEvt& e);
  CPA_NOINLINE bool send_data(const rec::RecEvt& e);
  CPA_NOINLINE void send_items(const rec::RecEvt& e, const MsgId& re, bool more);
  CPA_NOINLINE void send_data0(const MsgId& re, const proto::FileGet& g, uint32_t size);
  void send_error(const MsgId& re, ErrCode code, const Opt<proto::FieldPath>& field, const Opt<proto::ErrText>& msg);
  void send_ack(const MsgId& re);
  void begin_reply(const MsgId& re);
  void committed(const rec::RecEvt& e);
  // runtime_kept.cpp (docs/28 S1a)
  void begin_kept();
  rec::Viewer viewer() const;
  uint32_t wall_now_s() const;  // a trusted wall clock (D16), 0 none
  void kept_hello(const proto::Hello& h);
  void kept_host(const proto::Hello& h);  // the Mac on the link, before its hello is answered
  void kept_link_lost();
  void kept_time();
  bool post_ledger(rec::LedgerAsk ask, uint32_t host = 0, const MonoId& lo = MonoId{});
  void pump_kept(uint32_t now);
  void pump_ledger(uint32_t now);
  void plug_check(uint32_t now);
  void plug_apply(const PlugStep& st);
  void post_hold(uint32_t now);
  void hold_gave_up(const Txt& why);
  void ledger_lost();
  bool plug_key(const KeyEvent& ev, uint32_t now);  // the hint's key: true = taken
  void kept_event(const rec::RecEvt& e);
  void ledger_done(const rec::RecEvt& e);
  void hold_done(const rec::RecEvt& e);
  void say_done(const rec::RecEvt& e);
  static const Txt& ledger_refusal(rec::LedgerNo no);
  KeptView kept_view() const;
#if CPA_TEST_HOOKS
  // runtime_debug.cpp
  void fill_debug_state(uint32_t now);
#endif
  // runtime_settings.cpp (docs/21 §6.8)
  void begin_settings();
  void sync_settings();  // the write a switch left for the end of the recording
  SettingsSave write_settings();

  Transport& tx_;
  FileStore& fs_;
  Platform& plat_;
  Display& disp_;
  Shell shell_;

  AppState buf_[2];
  uint8_t cur_ = 0;
  LinkCore link_;
  PlugFilter plug_;  // debounced tx_.plugged() for the link (plug_filter.h)
  OutboxCore outbox_;
  LineFramer framer_;
  proto::Codec codec_;
  proto::Message out_;  // the message being encoded (kept off the loop stack)
  MonoCounter ids_;     // the boot's MonoId sequence, shared with the audio task
  SysInfo sys_;
  ChordState chords_;
  GhostState ghost_;
  KbWatch kbw_;
  PowerState power_;
  SmallReq small_;
  // Set when an agent request failed or its result is unknown; the text is saved by
  // flush_agent_save() at the top of the loop, not deep inside the line handling.
  bool agent_save_ = false;
  AgentEventKind agent_save_kind_ = AgentEventKind::Failed;
  ErrCode agent_save_code_ = ErrCode::None;
  char rx_[kRxChunk] = {};
  size_t rx_pos_ = 0;
  size_t rx_len_ = 0;
  uint32_t now_ = 0;
  uint32_t last_render_ = 0;
  uint32_t last_stats_ = 0;
  uint32_t boot_free_ = 0;     // free bytes at boot (SD: 0xFFFFFFFF, 0 once latched)
  uint32_t boot_items_ = 0;    // outbox + bad items at boot
  bool rendered_ = false;
  bool backlight_ = true;
  Backlight level_ = Backlight::On;
  RecPort* rec_ = nullptr;
  SpareStore* spare_ = nullptr;
  RecBoot rec_boot_;
  rec::RecCoreState rec_core_;
  RecSide rec_side_;
  KeptSide kept_;
  uint32_t key_ms_ = 0;
  bool key_seen_ = false;
  Settings settings_;           // what the device shows (docs/21 §6.8)
  Settings stored_;             // what NVS holds, as far as this boot knows
  bool settings_due_ = false;   // a switch during a recording: written once it ends
#if CPA_TEST_HOOKS
  InjectQueue inject_;
  RenderTimes times_;
#endif
};

}  // namespace cpa
