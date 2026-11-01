**English** · [中文](12-m3-work-packages.zh-CN.md)

# 12 · v0.3.0 (M3 "Granola in your pocket") work packages

> Generated on 2026-10-20 by the M3 design workflow, finalized after review. The design is in [11-m3-design.md](11-m3-design.md); research is in docs/gates/whisperwoof.md, granola-pipeline.md, audio-capture.md.

## Decided by the user (2026-10-20)

- **WhisperWoof**: agents develop and test in a local clone, then open 4 PRs on h3qing/WhisperWoof, **reviewed and merged by the user**.
- **Gestures**: hold G0 to record a voice memo, release to stop; `Opt+R` → Enter starts a meeting recording; in a meeting, press G0 to add a ★ mark.
- **Consent prompt**: every meeting recording first shows "Tell the people present first" (请先告知在场的人), and starts only after Enter; the start and end tones cannot be turned off; the screen shows ● REC while recording. **Changed by the user on 2026-10-21**: recording is silent, for personal use; from v0.3.1 recording makes no sound at all; the meeting confirmation stays; the screen is the only cue.
- **Retention**: audio (WAV) is kept for 30 days; transcripts, summaries and notes are kept forever.
- **Archive location**: `~/CardputerNotes`. Transcription, tidying and summaries are all handed to WhisperWoof, and the Mac app also follows the WhisperWoof route (#13).

## Lead defaults (unless stated otherwise)

- Drop the pre-roll buffer: the mic and the speaker share I2S pins, so it becomes "speak after the beep". (From 2026-10-21 there is no beep; watch for `● REC` on the screen; still no pre-roll.)
- AI-extracted to-dos in v0.3 only appear as checkboxes in summary.md and do not enter project queues; queueing moves to v0.4 (it needs on-device review first).
- Summaries use the local model selected in WhisperWoof by default; cloud models are opted in per project in v0.5.
- Transfer: v0.3.0 only does base64; a binary mode waits until G-A1 measures a clear gain.
- The protocol uses generic `file.*` messages; `voice.*` is kept for v0.4 voice replies.
- Limits: memos at most 5 minutes, meetings at most 3 hours, at most 256 unsynced segments.
- WhisperWoof's Cardputer inbox is off by default, and the user turns it on in settings.
- The on-device recordings list (Opt+V) and `cpa rec retry` move to v0.3.x.

## M3-P · Protocol and file contract: file.* messages, files{n,rev,live} on hello/pong, debug.rec (seg_ms), limits, vectors and flows; recording file contract v1 (manifest/status/transcript/result)

- **Repo**: cardputer-assistant
- **Owned paths**: `protocol/limits.json`, `protocol/README.md`, `protocol/schema/v0/`, `protocol/vectors/v0/`, `protocol/tools/`, `protocol/recording/`
- **Depends on**: none

### Scope

Implement docs/11-m3-design.md §5 and §7.2–§7.7. This is the RED contract that M3-A, M3-B and M3-W build against, so it goes first.

(0) Guard first. Before changing anything:
- generate protocol/tools/fixtures/v02_cases.sha256, with one sha256 per existing message case and flow case;
- add test_additive.py, which asserts that every v0.2 case is still present and byte-for-byte unchanged.

(1) Wire protocol. All changes are additive; v stays 0. No additionalProperties:false anywhere.
- common.schema.json: add sha256 (lowercase hex, 64 chars), recMode (memo|meeting) and fileItem.
- hello.schema.json and pong.schema.json: add the optional files object {n 0–256, rev uint32 (0 = index not ready), live bool}. Do NOT add enc, files.bin or revs.recs.
- debug.state.schema.json: add the optional rec object {live, mode, seg, files_n, files_rev}.
- New schemas:
  - file.list (h2d request): after? MonoId, max? 1–4.
  - file.items (d2h reply): items[≤4] and more. Each item has:
    - id, kind const 'rec', rec, seg 0–9999, last, mode;
    - size ≤ 44+600000*32, sha256?;
    - dur_ms ≤600000, at {up_ms, wall_ms|null} | null;
    - project?, slot? 1–9, stars? (≤16 entries, each <600000), lead_ms? ≤2000, consent? const 'ack', rcv?.
  - file.get (h2d): fid, off uint32, len 1–1380.
  - file.data (d2h reply): fid, off, n, size, crc uint32; b64 is required when n>0 and must decode to exactly n bytes (checked in check_vectors).
  - file.del (h2d): fid, size, sha256?.
  - debug.rec (h2d, test hooks only): seg_ms 10000–600000.
- limits.json: add every key of §5.5 (file_list_max, file_get_b64_max, file_inflight_max, file_pending_max, file_marks_max, file_seg_max, sha256_pattern, lead_ms_max, rec_min_ms, memo_max_ms, meeting_max_ms, seg_ms_default/min/max). Keep schemas.LIMIT_POINTERS in sync.

(2) Vectors.
- At least 1 valid and 1 invalid case per new type.
- The invalid cases must include: len 1381; fid not a MonoId; items with 5 entries; 17 stars; a decoded b64 length different from n; an upper-case sha256; seg_ms 9999; consent 'yes'.
- New flows:
  - file_pull_resume;
  - file_del_idempotent;
  - files_rev_relist;
  - file_list_paging;
  - file_busy_while_live: file.get gets E_BUSY msg 'live'; the host sends no file.* until a pong with files.live:false, then resumes at the same offset;
  - old_daemon_ignores_files: the device sends files and the host never sends file.*;
  - old_device_no_files.
- Extend flows.py with three host invariants:
  - a file.del is only sent after the host has received every byte of that file;
  - every h2d file.* gets exactly one reply;
  - after an E_BUSY to any file.*, the host sends no file.* until a hello or pong shows files.live:false.

(3) Worst-case size tests (test_worst_lines.py). Build each line with 24-character id and re and every number at its schema maximum:
- file.data with n=1380 is 2024 B;
- file.items with 4 items, each with 16 stars and every optional field, is 1970 B;
- pong with files is 234 B;
- all three must be ≤2048 B.

(4) Recording file contract v1, in the new directory protocol/recording/v1/:
- README.md: the single-writer table, the handoff rules of §7.7 (attempt fixed to 1 in v0.3.0; manual rerun by deleting result.json) and the forward-compatibility rule (unknown fields ignored; only v≠1 rejects).
- manifest, status, transcript and result schemas (Draft 2020-12). The manifest includes consent?, segments[].t0_ms, gaps and marks[].k (only 'star' in v0.3.0, but the enum is documented as extensible).
- vectors/valid/*.json and vectors/invalid/*.json, each invalid case with its reason.
  - Invalid cases must include: '../' or '/' in a segment file name; outputs.path containing '/'; session not a MonoId; attempt 0; segment indices not contiguous without matching gaps; sha256 not hex; actions >20; title >80 characters; memo.text >2000 characters; unknown mode; state done with a non-null error; v=2.
  - Valid cases must include a result.json carrying an extra unknown top-level field.
- VECTORS.sha256.
- tools/check_recording.py and its tests.

Out of scope: firmware, daemon and WhisperWoof implementations; recs.snapshot (deferred to v0.3.x); any binary/enc field; docs/05 (the Lead edits it).

### Acceptance (tests first)

- (RED first) uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/tools -q   # includes test_additive (every v0.2 case unchanged), test_worst_lines (2024/1970/234 B, all <=2048) and the three new host invariants in flows.py, exercised by file_busy_while_live
- uv run --with 'jsonschema>=4.23,<5' python protocol/tools/check_vectors.py   # exit 0; lists file.list/file.items/file.get/file.data/file.del/debug.rec, each with >=1 valid and >=1 invalid case; b64 length==n checked
- uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/recording -q && uv run --with 'jsonschema>=4.23,<5' python protocol/recording/tools/check_recording.py   # every valid vector accepted (including the one with an unknown field), every invalid one rejected with its declared reason; VECTORS.sha256 matches
- git grep -nE '"(enc|bin|recs)"' -- protocol/schema/v0 protocol/limits.json ; test $? -eq 1   # no reserved bin/enc/recs fields
- python3 tools/repo/check_sizes.py --warn 400 --fail 800 protocol && uvx lizard -l python -L 50 -w protocol/tools protocol/recording/tools

## M3-A · Firmware: memo and meeting capture with consent confirm, audio task, SD segments with .M commit, power-loss recovery, file.* serving with recording/transfer mutual exclusion, G0LongUp, dim-not-off while recording, I2C lock

- **Repo**: cardputer-assistant
- **Owned paths**: `firmware/app/lib/rec/`, `firmware/app/lib/core/`, `firmware/app/lib/input/`, `firmware/app/lib/protocol/`, `firmware/app/lib/shell/`, `firmware/app/lib/hal/`, `firmware/app/modules/system/`, `firmware/app/src/`, `firmware/app/platformio.ini`, `firmware/app/scripts/stack_budget.py`, `firmware/app/test/core/support/fake_rec_file_port.h`, `firmware/app/test/core/test_rec_core/`, `firmware/app/test/core/test_rec_meta/`, `firmware/app/test/core/test_wav_header/`, `firmware/app/test/core/test_rec_recovery/`, `firmware/app/test/core/test_rec_index/`, `firmware/app/test/core/test_file_req/`, `firmware/app/test/core/test_chord_detector/`, `firmware/app/test/core/test_keymap_static/`, `firmware/app/test/core/test_mono_id/`, `firmware/app/test/core/test_codec_unit/`, `firmware/app/test/core/test_limits_match_json/`, `firmware/app/test/ui/test_rec_overlay/`, `firmware/app/test/ui/test_runtime_rec/`, `firmware/app/test/ui/test_shell_dispatch_order/`, `firmware/app/test/ui/test_runtime_keys_and_hooks/`, `firmware/app/test/ui/test_status_bar_states/`, `firmware/app/test/ui/test_mod_system/`, `firmware/app/test/ui/support/`, `firmware/app/test/device/test_rec_sd/`, `firmware/app/test/device/test_rec_rotation/`, `firmware/app/test/device/test_rec_budget/`, `firmware/app/test/device/test_runtime_budget/`
- **Depends on**: M3-P

### Scope

Implement docs/11-m3-design.md §3–§5 on the device, in three stages: A1 pure code (native TDD) → A2 hal → A3 shell, UI and wiring. A1 can run in parallel with A2/A3. Read G-A0 (heap baseline) before fixing the allocation mode, and G-A5 / G-A10 before fixing the cue/gain constants. Until G-A10 reports, the gain constant is a placeholder and this package is not done.

(A1) Pure code. lib/rec must not include Arduino, M5 or esp_* headers.
- rec_paths: the 8.3 mapping /cpa/rec/<E7>/<B5><S first 3>.W<S last>, with .M under the same name.
- wav_header: build and parse.
- rec_meta: .M encoder/decoder (snprintf, ≤512 B, the fields of §4.4 including consent and next). Decoding checks that the id equals the filename id.
- RecFilePort interface: open_trunc, write, fsync, pwrite_at0, close, size, read_at, list, unlink; FatErr semantics.
- RecWriter: open a segment, write blocks while updating SHA-256 through an injectable hash, fsync every 1 s, rotate (take the next id first, then backfill the header, fsync and close, then commit .M), finish.
- plan_rec_recovery: the §4.6 table as pure actions, including the id≠filename row, the <44 B row and at derived from the previous segment.
- RecIndex: up to 256 entries of {id, size} plus rev; rev is 0 until the scan completes.
- RecCore state machine (Idle → Confirm → Cue → Recording → Finishing):
  - memo: G0Long starts it; G0LongUp stops it; a stop in Cue or under 1 s discards with the cancel tone (2026-10-21, user decision: no tone from v0.3.1; it just discards); 5-minute cap;
  - meeting: Opt+R opens the consent confirm; Enter starts with consent ack; ` cancels; on the meeting page, G0Tap/G0Long/Space = star and digits set the tag; Opt+R or ` opens the end confirm; 3-hour cap;
  - refusals of §4.3;
  - at most 16 stars per segment;
  - mutual exclusion: when not Idle, every file.* is answered E_BUSY msg 'live'; on Start, every queued file.* is answered E_BUSY before Start is posted;
  - an allocation-failure refusal path for the record-time allocation mode.
- file_req: validates file.list/get/del:
  - bounds; in-flight >4 → E_BUSY msg 'full'; E_UNKNOWN_ITEM;
  - del: size or sha mismatch → E_BAD_FIELD; missing file → ack;
  - index not ready → E_NOT_READY;
  - plans the items page to fit within 2048 B, otherwise more=true.
- lib/core:
  - the mono_id sequence becomes an atomic counter shared by loop and the audio task;
  - AppState gains a rec slice (live, mode, elapsed, stars, project, pending, level).
  Static RAM grows by ≤4 KB.
- lib/input:
  - KeyEventKind::G0LongUp is emitted on release after G0Long; when G0Long itself is emitted on the release sample (the late path, chord_detector.cpp:75-77), G0LongUp is emitted in the same step;
  - keymap: remove VoiceHint; add RecMemo (G0Long, Global) and RecMeeting (Opt+R, Global), plus the rec overlay bindings. Overlays are exclusive (shell.cpp:89-98), so no None bindings are needed in Compose/Confirm;
  - every binding passes test_keymap_static.
- lib/protocol:
  - decode file.list/get/del and (with test hooks) debug.rec;
  - encode file.items, file.data (b64), and hello/pong with files only when recording is available, so encoding without files is byte-identical to v0.2;
  - protocol_limits.h mirrors limits.json.

(A2) hal.
- Audio task (core 0, priority 1, 6 KB stack):
  - owns the Mic/Speaker sequence of §4.2, the 6×4 KB ring filled by the mic release callback, RecWriter, the boot recovery scan and transfer reads (at most one read per request, never while RecCore is not Idle);
  - holds at most one open file at any time;
  - talks to loop through the queues of §4.7.
- Buffers (about 38 KB): allocated once at boot only when the primary store is Sd (default). A compile-time switch selects allocate-on-record-start instead, chosen from G-A0.
- VfsRecFilePort.
- sd_card: max_files becomes 3; an atomic sd_health flag is shared by VfsFatPort and VfsRecFilePort (EIO sets it, and both then report Disk). The pure SdStore is unchanged.
- Bounded FSINFO free-space read; when it is not available, free space is unknown.
- Tones of §4.2 with compile-time volume. (2026-10-21, user decision: removed in v0.3.1; recording is silent, for personal use.)
- Display backlight: three levels (on, dim, off).
- I2C mutex, on by default, around M5Cardputer.update()/Keyboard.keyList() (lib/hal/keyboard.cpp) and every Speaker/Mic begin/end (2026-10-21: from v0.3.1 only Mic begin/end; the speaker is never begun).
- Defaults until the gates decide: lead_ms 200, magnification as in M5Unified 0.2.26, TX ring 4 KB.

(A3) shell and UI.
- runtime_rec:
  - bridges RecCore ↔ the audio task;
  - answers file.* in loop;
  - generates a file.data line only when the whole line fits in the TX ring, never through the 4-line LineQueue;
  - handles debug.rec seg_ms under CPA_TEST_HOOKS.
- Rec overlays (new Overlay kinds, dispatched exclusively like Compose/Confirm): the memo HUD; the meeting start-confirm with consent text; the modal meeting page; the end-confirm. Both confirms have the 400 ms guard.
- power:
  - G0Long and G0LongUp are not swallowed when dark;
  - while recording on battery, the 60 s idle dims the backlight instead of turning it off;
  - keys are not swallowed while dimmed;
  - ● REC stays drawn.
- Status bar: ●mm:ss while recording; 录N (recordings N) when segments are waiting to sync.
- System page: one line, 录音 待同步 N / 剩余空间 (recordings waiting to sync N / free space).
- nosd build: G0Long and Opt+R are refused with the fail tone and the reason; no files field. (2026-10-21, user decision: no tone from v0.3.1; the on-screen reason only.)
- main.cpp wiring.
- stack_budget.py: add the new loop chains (file.* parse, file.data encode).

v0.2 test edits must be exactly those listed in docs/11 §11.2:
- test_chord_detector line 92;
- test_shell_dispatch_order test_g0_long_and_unbound_opt_do_not_type;
- test_runtime_keys_and_hooks (dark-swallow cases);
- test_status_bar_states;
- test_mod_system.
Each edit is called out in the hand-off note.

Out of scope:
- outbox, OutboxCore, store_commit, SdStore, outbox_migrate, plan_storage and their tests (guarded by git diff);
- the LED;
- the recordings list page (modules/recs) and recs.snapshot;
- the todo mark;
- the binary sub-mode, pulling while recording, pre-roll, the discard key;
- daemon, protocol/ and hwtest.

### Acceptance (tests first)

- (RED first) cd firmware/app && pio test -e native -f 'core/test_rec_core' -f 'core/test_rec_meta' -f 'core/test_wav_header' -f 'core/test_rec_recovery' -f 'core/test_rec_index' -f 'core/test_file_req' -f 'core/test_chord_detector' -f 'core/test_keymap_static' -f 'core/test_mono_id' -f 'core/test_codec_vectors' -f 'core/test_codec_unit' -f 'core/test_limits_match_json'   # recovery: power cut at every rotate/finish/delete/recover step -> 0 committed segments lost, sessions relinked via next, a .W with a valid .M is never deleted without file.del, .M whose id != filename is recovered; audio-task open files <=1; file.* while not Idle -> E_BUSY 'live' and queued requests flushed before Start; G0LongUp emitted in the same step on the late path; Stop in Cue -> discard; consent only after Enter
- (RED first) cd firmware/app && pio test -e native -f 'ui/test_rec_overlay' -f 'ui/test_runtime_rec' -f 'ui/test_shell_dispatch_order' -f 'ui/test_runtime_keys_and_hooks' -f 'ui/test_status_bar_states' -f 'ui/test_mod_system'   # key tables of §4.1; G0Long/Opt+R ignored while Compose/Confirm open; 400 ms guards; dim-not-off while recording and G0Long not swallowed when dark; nosd refusal; hello/pong without files byte-identical to v0.2; file.data never queued in LineQueue
- cd firmware/app && pio test -e native && git diff --quiet HEAD -- test/core/test_outbox_boot_scan test/core/test_outbox_core test/core/test_outbox_fat test/core/test_outbox_migrate test/core/test_sd_store test/core/test_store_commit test/core/test_storage_plan test/core/test_sd_paths test/core/test_link_flows test/core/test_link_session test/core/test_line_framer test/core/test_codec_vectors   # every v0.2 storage/outbox/link/codec suite passes unchanged (G9); other v0.2 test diffs limited to the §11.2 list
- cd firmware/app && uvx gcovr -r . --filter 'lib/|modules/' --exclude 'lib/(hal|usb_transport)/' --fail-under-line 80
- cd firmware/app && pio run -e cardputer-adv && python3 scripts/check_budget.py .pio/build/cardputer-adv && pio run -e cardputer-adv-test && pio run -e cardputer-adv-test-nosd   # static RAM <= v0.2 + 4096 B; stack gate passes with the new chains; app image warning threshold 1.75 MiB respected
- python3 tools/repo/check_sizes.py --warn 400 --fail 800 firmware/app/lib firmware/app/modules firmware/app/src firmware/app/test && uvx lizard -l cpp -L 50 -w firmware/app/lib/rec firmware/app/lib/shell firmware/app/lib/hal
- (real device with SD, run by the device-owning agent after cpa pause 900) cd firmware/app && pio test -e cardputer-adv-test -f 'device/test_rec_sd' -f 'device/test_rec_rotation' -f 'device/test_rec_budget' -f 'device/test_runtime_budget' -f 'device/test_sd_store'   # commit/recover under /cpatest; 30 s segments x3 sample-exact; idle heap >=150 KB / largest >=64 KB with the chosen allocation mode; record+cue >=96 KB / >=32 KB; transfer idle >=128 KB; audio task stack margin >=1.5 KB

## M3-B · Daemon: RecPuller (file.* pull with E_BUSY pause, staging, verify, land), session manifest with t0_ms/consent/gaps, wall-time resolution, ResultWatcher, daily-note lines (no queue routing), cpa rec ls/show and status

- **Repo**: cardputer-assistant
- **Owned paths**: `daemon/src/cpad/recordings/`, `daemon/src/cpad/device/link.py`, `daemon/src/cpad/protocol/`, `daemon/src/cpad/notes/`, `daemon/src/cpad/app.py`, `daemon/src/cpad/cli/`, `daemon/src/cpad/control/`, `daemon/src/cpad/config/`, `daemon/tests/`, `daemon/README.md`
- **Depends on**: M3-P

### Scope

Implement docs/11-m3-design.md §6 and the cpad side of the §7.2–§7.7 contract, in two stages: B1 transfer → B2 results and notes. One agent can do both in order. If the stages are split:
- B1 owns link.py, protocol/ and recordings/{pull,land,manifest,wall,records}.py;
- B2 owns notes/ and recordings/{results,route}.py;
- app.py, control/ and cli/ stay with whoever finishes last.
hub/ is NOT touched: there is no recs snapshot in v0.3.0.

(B1) Transfer.
- protocol/messages.py and specs.py: dataclasses and specs for file.list/items/get/data/del and debug.rec. DeviceHello and Pong gain the optional files {n, rev, live}.
- link.py: request_reply(msg) -> Future. It resolves on the matching re, or with an error/timeout after 5 s. It is still metered by the credit window and does not change existing request or nudge behaviour.
- recordings/pull.py: a pure plan plus an IO shell triggered from on_ready and on_pong.
  - Do not pull while files.live is true or files.rev is 0.
  - List pages via after/max.
  - Order: memo sessions first, then id ascending.
  - Already-stored segments: send file.del directly.
  - Resume from the .part size minus 64 KB.
  - Up to 4 file.get in flight, len 1380.
  - Check CRC32 and offsets; retry the same offset; 3 strikes gives up the file for this connection.
  - On E_BUSY to any file.*: stop issuing, keep the .part, and resume only after a pong or hello shows live:false.
  - Relist only between files, and only when the observed rev differs from the expected rev (last seen plus own successful deletes); never restart or duplicate the file in progress.
  - Log effective KB/s per landed file (numbers only).
- Verify:
  - size == 44 + bytes;
  - the header equals the rebuilt one (for rcv segments, replace it and set repaired);
  - sha256 of data[44:] matches, unless rcv.
- recordings/land.py:
  - stage at $CPA_HOME/rec-staging/<dev>/<fid>.part with mode 0600;
  - fsync, rename to <notes_dir>/recordings/<YYYY-MM-DD>/<HHMM>-<sid lower>/seg-NNN.wav, fsync the directory; on a different volume, copy to .seg-NNN.wav.tmp, then rename;
  - permissions 0700/0600;
  - append rec.seg.stored (fsync) → file.del → rec.seg.deleted.
- recordings/wall.py: resolve_wall(at, boot, link_seen, now), following §6.5 (at may be null).
- recordings/manifest.py:
  - session completion: contiguous 0..k with last;
  - gap finalisation when the device is not live and a full listing shows none of the session's segments; a trailing last:false adds k+1 to gaps;
  - t0_ms per segment: from cumulative samples, or from the up_ms difference after a gap;
  - marks converted to session t_ms;
  - consent;
  - whole-file sha256;
  - manifest v1 (attempt 1) written with tmp+rename;
  - append rec.archived.
- recordings/records.py: rec.seg.stored, rec.seg.deleted, rec.archived, rec.result, rec.stale.

(B2) Results and notes.
- recordings/results.py, the ResultWatcher run from the housekeeping tick:
  - stat status.json and result.json for archived sessions without a done result; failed sessions stay watched, so a manual rerun is picked up;
  - validate result.json against protocol/recording v1: ≤64 KB; unknown fields ignored; reject only on v≠1 or invalid known fields; bare filenames; session matches;
  - append rec.result once per (session, state);
  - keep status.json state in memory only;
  - append rec.stale after transcribe_wait_h with no result.
- recordings/route.py: pure rec_entry(event) producing the §6.7 daily-note lines:
  - meeting done with the 待办 (to-do) n count;
  - memo note;
  - memo todo as '- [ ]';
  - transcript-only;
  - -u block id for failed or stale;
  - ~ prefix when the time is uncertain;
  - control characters become spaces; [ ] escaped inside link text; title ≤80, memo ≤280.
  It never produces queue.added.
- notes/writer.py: backfill also replays rec.* events through rec_entry.
- app.py:
  - wire the puller and watcher;
  - device_status/cpa status gains a recordings block {on_device, pulling, waiting, processing, blocked, failed, last_result_at};
  - iCloud warning when notes_dir resolves under ~/Library/Mobile Documents.
- control and cli:
  - cpa rec ls [--json];
  - cpa rec show <sid> (paths and states only, never text).
- config: transcribe_wait_h (default 24), rec_pull (default true).
- README: usage, plaintext archive, manual rerun by deleting result.json (it overwrites summary.md), minimum WhisperWoof version.

Out of scope:
- WhisperWoof code, firmware, protocol/ files (M3-P owns them);
- recs.snapshot and any hub change;
- queue routing of AI todos;
- cpa rec retry and attempt bumps;
- binary decoding, pulling while live;
- retention or deletion of archived WAVs;
- any change to item.received, queue or outbox semantics.

### Acceptance (tests first)

- (RED first) cd daemon && uv run pytest -q tests/unit/test_rec_pull.py tests/unit/test_rec_land.py tests/unit/test_rec_manifest.py tests/unit/test_rec_wall.py tests/unit/test_link_request_reply.py   # resume truncates 64 KB; CRC retry same offset; 3-strike give-up; E_BUSY pauses until live:false then resumes by offset; rev relist only on unexpected rev and never mid-file; rev=0 and live -> no file.*; memo sessions first; crash at every landing step never leaves a device delete without rec.seg.stored; t0_ms with and without gaps; trailing last:false finalises with gaps [k+1]; consent carried into manifest
- (RED first) cd daemon && uv run pytest -q tests/unit/test_rec_results.py tests/unit/test_rec_route.py tests/unit/test_rec_notes.py tests/unit/test_rec_cli.py   # malicious result.json vectors rejected, unknown fields ignored, v=2 ignored; failed->done after manual rerun writes the proper line; daily lines incl. ~ prefix, checkbox memo, transcript-only and -u forms are idempotent by block id and backfill; bracket escaping; no queue.added ever; cpa rec show never prints text
- cd daemon && uv run pytest -q tests/contract   # v0 vectors (incl. new file.* and file_busy_while_live) and protocol/recording/v1 vectors both pass
- cd daemon && uv run pytest -q tests/integration/test_recordings.py   # pty FakeDevice serves a 1-segment memo and a 3-segment meeting -> WAVs verified and landed, manifest.json written, test double writes result.json -> daily-note lines; FakeDevice starts recording mid-pull (E_BUSY + live:true) then stops -> transfer completes by offset; cpa pause mid-transfer then resume; v0.2 device (no files) is never sent file.*
- cd daemon && uv run pytest --cov=cpad --cov-fail-under=80 && uv run ruff check . && uv run ruff format --check .
- cd daemon && git diff --quiet HEAD -- tests/integration/test_items.py tests/unit/test_queue_events_notes.py tests/unit/test_host_lines_never_start_with_word.py tests/unit/test_snapshots.py tests/unit/test_reduce.py   # v0.2 behaviour tests pass unchanged (G9)
  - G9 exception, added in the M3-B review round on 2026-10-21 (the lead confirms it at merge): test_host_lines_never_start_with_word.py asserts that its samples cover every h2d schema, so M3-P's four new h2d types (file.list, file.get, file.del, debug.rec) fail it unless the file changes. The change only adds lines: one import, three samples at the end of `_host_messages()` and one `DEBUG_REQUESTS +=` line. No v0.2 line is edited or removed, and the new types also go through the one-line/'{' property test. For this file the check is `test "$(git diff --numstat HEAD -- tests/unit/test_host_lines_never_start_with_word.py | cut -f2)" = 0` against the v0.2 baseline (0 removed lines); the other four files still pass `git diff --quiet`.
- python3 tools/repo/check_sizes.py --warn 400 --fail 800 daemon/src daemon/tests && uvx lizard -l python -L 50 -w daemon/src

## M3-W · WhisperWoof (4 PRs): transcriber zeroMs/silenceRms/backpressure, preemptible reasoning queue, Cardputer inbox with own sherpa sidecar and transcript write-back, memo/meeting summaries and result.json

- **Repo**: WhisperWoof
- **Owned paths**: `src/helpers/meetingLocalTranscriber.js`, `src/whisperwoof/core/meeting/local-transcriber.test.ts`, `src/services/localReasoningBridge.js`, `src/helpers/modelManagerBridge.js`, `src/helpers/llamaServer.js`, `src/helpers/sidecarReaper.js`, `src/whisperwoof/bridge/reasoning-queue-pure.js`, `src/whisperwoof/bridge/background-gate-pure.js`, `src/whisperwoof/bridge/cardputer-inbox.js`, `src/whisperwoof/bridge/cardputer-inbox-pure.js`, `src/whisperwoof/bridge/cardputer-summary-pure.js`, `src/whisperwoof/bridge/cardputer-prompts.js`, `src/whisperwoof/bridge/cardputer-writeback.js`, `src/whisperwoof/bridge/app-init.js`, `src/whisperwoof/core/cardputer/`, `src/whisperwoof/core/reasoning/`, `src/whisperwoof/ui/settings/CardputerSection.tsx`, `src/whisperwoof/ui/settings/WhisperWoofSettings.tsx`, `docs/design/cardputer-inbox.md`
- **Depends on**: M3-P

### Scope

Implement docs/11-m3-design.md §7.1–§7.9 and §13 in the user's own repo h3qing/WhisperWoof (MIT), based on main 767ae8d or later.

Authorisation comes first. The user must approve:
- opening the GitHub issue (step 0: the contract and the PR plan, in Chinese and English);
- each PR.
The agent works in a local clone on one branch per PR and pushes nothing without approval.

Follow WhisperWoof's own rules:
- pure logic goes in bridge/*-pure.js and is tested from src/whisperwoof/core/**/*.test.ts, because vitest only collects src/whisperwoof/**/*.test.ts(x) and enforces 80% coverage;
- npm run typecheck is a hard gate;
- UI follows DESIGN.md;
- no transcript or summary text in debugLogger.

PRs, in order. W1 and W3 can run in parallel; W2 must merge before W4.

(W1) MeetingLocalTranscriber.
- Optional constructor options:
  - zeroMs: the time assigned to sample 0 of the fed stream. When given, _track does no Date.now() and no first-chunk subtraction;
  - silenceRms: the default stays 0.003.
- push() returns true when it queued a clip.
- Behaviour without the options is unchanged.
- Tests in core/meeting/local-transcriber.test.ts:
  - feeding 3 h of synthetic PCM in 1 s chunks through a slow fake transcribe, awaiting idle() whenever push returns true, keeps queued clips ≤1 and buffered bytes ≤(clipTargetS+lookaheadS+1)*32000;
  - each line's timestamp equals zeroMs plus its sample offset;
  - silenceRms is honoured;
  - the defaults are unchanged.

(W2) Preemptible reasoning.
- reasoning-queue-pure.js: interactive before background; enqueuing an interactive request aborts the in-flight background call and requeues its chunk at the head of the background queue.
- background-gate-pure.js: background work is allowed only when the last interactive request was ≥10 s ago.
- Thread an AbortSignal through localReasoningBridge.processText → modelManagerBridge.runInference → llamaServer.inference; on abort, req.destroy().
- Background calls cap max_tokens (map ≤512, reduce ≤1024).
- A logContent:false option suppresses the resultPreview logs at localReasoningBridge.js:67 and modelManagerBridge.js:409 for background calls.
- Default callers (dictation) keep their current behaviour.
- Tests with a fake llama HTTP server:
  - an interactive request submitted during a background call starts within 200 ms;
  - the aborted background chunk completes after its retry;
  - with no background work, the interactive path is unchanged.

(W3) Cardputer inbox and transcription.
- cardputer-inbox.js:
  - started from app-init.js next to startTelegramSync (app-init.js:535-536);
  - discovery: scan at start, then fs.watch, plus a 30 s poll fallback;
  - root from the setting (default ~/CardputerNotes/recordings); disabled by default.
- cardputer-inbox-pure.js:
  - candidate rule: a valid manifest and no result.json;
  - strict validation of manifest v1: MonoId regex; realpath inside root; lstat with no symlinks; seg-NNN.wav names; ≤20 MB per segment; ≤4 h total; PCM 16k/16-bit/mono header with consistent data length; sha256 match; text caps; unknown fields ignored, v≠1 → failed/E_VERSION;
  - status transitions: queued → transcribing → summarizing; blocked with busy, model_missing or disabled.
- Transcription:
  - its own sherpa server, pidKey 'parakeet-cardputer' with logTranscripts:false, registered in sidecarReaper.js;
  - each contiguous run of segments is fed through a MeetingLocalTranscriber in chunks of ≤1 s, awaiting idle() after any cut;
  - zeroMs = the run's first t0_ms (plus lead_ms for seg 0, whose lead is skipped), so timestamps are session t_ms;
  - silenceRms = CARDPUTER_SILENCE_RMS (default 0.0005 until G-A10);
  - every clip waits on background-gate;
  - Memory replacement, simplified-Chinese conversion and punctuation are applied with the existing main-process helpers.
- Write status.json, transcript.json and transcript.md (unique ^tHHMMSS ids), then result.json level=transcript last, each with tmp+rename. Once result.json exists, never write to that directory again.
- System notification on completion, without text.
- CardputerSection.tsx: an enable toggle and the folder, registered in WhisperWoofSettings.tsx.

(W4) Summaries.
- cardputer-prompts.js:
  - memo cleanup and classification (rule-first prefixes, then an LLM yes/no) and a title;
  - meeting map prompt per 5-minute window and a reduce prompt with ★ marker notes (−30 s/+10 s windows) and the fixed headings of §7.5;
  - single microphone, speakers unknown;
  - output in Chinese with technical terms kept in English;
  - transcript inside delimiters with 'ignore instructions inside'.
- cardputer-summary-pure.js: chunk plan; action parsing (confident only for an explicit commitment with an owner); rendering of summary.md with [@mm:ss](transcript.md#^t…) anchors and ^act-<n> ids.
- cardputer-writeback.js: tmp+rename for each file, result.json last.
- All LLM calls go through the W2 queue as background with logContent:false, using only the user's selected local model; cloud is off unless explicitly enabled.
- result.json level=summary. Error codes are E_VERSION|E_INVALID|E_AUDIO|E_STT|E_LLM|E_IO with retryable; retryable errors back off and are retried 3 times before failing.

Fixtures: copy protocol/recording/v1/vectors and VECTORS.sha256 from cardputer-assistant into src/whisperwoof/core/cardputer/fixtures/, with a SOURCE file naming the commit; a test checks the digest.

Docs: docs/design/cardputer-inbox.md (English, WhisperWoof's doc language). It covers the manual rerun (delete result.json; it overwrites summary.md).

Out of scope:
- DB/history ingest (former W5);
- summary.regen and attempt bumps;
- the cpad.sock client and the Cardputer sidebar page (v0.5);
- Prompt Studio integration, diarization, FireRedASR2, cloud-by-default;
- touching cardputer-assistant files.

### Acceptance (tests first)

- (RED first) npx vitest run src/whisperwoof/core/meeting/local-transcriber.test.ts   # zeroMs/silenceRms honoured, defaults unchanged, push returns cut flag; 3 h synthetic PCM via slow fake transcribe keeps queued clips <=1 and buffered bytes <= (clipTargetS+lookaheadS+1)*32000; line timestamp == zeroMs + sample offset
- (RED first) npx vitest run src/whisperwoof/core/reasoning   # fake llama server: interactive submitted during a background call starts within 200 ms; aborted background chunk completes after retry; background waits for 10 s dictation idle; logContent:false suppresses previews
- (RED first) npx vitest run src/whisperwoof/core/cardputer   # contract vectors: every valid manifest/result accepted (incl. unknown fields), every invalid one rejected (path traversal, symlink, oversize, bad header, sha mismatch, v=2); inbox flow on a temp dir with a 1-segment memo and a 3-segment meeting (with a gap) + fake transcribe/LLM -> transcript.json, transcript.md, summary.md, then result.json written last; status transitions; session t_ms correct across the gap; result.json present -> directory never touched again; deleting result.json reprocesses; fixtures digest matches SOURCE
- npm run test:coverage   # 80% thresholds still met
- npm run typecheck && npm run lint
- (manual G-W2, dev build with real llama-server) summarise a 60-minute recording while dictating 20 times -> polish p95 within +300 ms of baseline, 0 POLISH_TIMEOUT fallbacks, llama-server accepts the next request immediately after an abort
- (manual, the user, dev build with real models) enable the Cardputer inbox, drop a device-recorded session folder under ~/CardputerNotes/recordings -> SenseVoice transcript and Qwen summary appear; feeds G-A10 and G-G5

## M3-H · Gates and HIL: v0.2 heap baseline (G-A0), hwtest gate firmware (throughput, cue timing and I2C, mic level), fake WhisperWoof, recording HIL scenarios incl. pull-interrupted-by-recording, v0.2 regression, manual checklist and gate report

- **Repo**: cardputer-assistant
- **Owned paths**: `firmware/hwtest/`, `tools/hil/scenarios/recording.py`, `tools/hil/harness/fakewoof.py`, `tools/hil/tests/test_fakewoof.py`, `tools/hil/tests/test_recording_helpers.py`, `tools/hil/scenarios/__init__.py`, `tools/hil/harness/select.py`, `docs/07-hil-checklist.md`, `docs/gates/m3-device-gates.md`, `docs/gates/captures/`
- **Depends on**: M3-P, M3-A, M3-B

### Scope

Phase 0 has no dependency and should start immediately on the device-owning agent. Phase 1 starts after the Lead commits the in-flight v0.2 HIL changes in tools/hil, and runs once M3-A and M3-B land.

(Phase 0) Baseline and hwtest gate firmware.
- G-A0: flash the v0.2 cardputer-adv-test build with an SD card inserted, run device/test_runtime_budget, and record the CPA_BUDGET line (heap_free, max_block, stack_left). State whether boot-time allocation of about 38 KB keeps ≥150 KB / ≥64 KB, which decides M3-A's allocation mode.
- Extend firmware/hwtest/src and host/ with three commands:
  - !xfer <b64|bin> <len> <depth> <txring 4|8> <ram|sd>, reading from RAM or from the existing /sd/cpa_hwtest/rec.wav. The host script measures effective KB/s, CPU % and ping RTT under load once per second. → G-A1.
  - !cue <ms> <vol>: times Speaker.begin, playback, Speaker.end and Mic.begin; records the RMS of 0–500 ms after Mic.begin in 10 ms bins; and runs a keyboard-hammer mode that reads the TCA8418 continuously during codec reconfiguration, with and without an I2C mutex, counting I2C errors and bad keys. → G-A5. (2026-10-21: a silent pattern skips the speaker, for lead_ms on v0.3.1's start; host command cue-silent.)
  - !mic <mag> <pga> <s>: records a WAV at the given software magnification and ES8311 PGA setting, so the user can read a mixed zh/en passage at 30 cm and at 1 m. The host computes peak and RMS (int16 and float) against WhisperWoof's 0.001 and 0.003 gates. The WAV is pulled for a manual SenseVoice run in WhisperWoof Upload Audio. → G-A10.
- Results (JSON captures under docs/gates/captures/ plus tables) go to docs/gates/m3-device-gates.md, ending with the decided constants: allocation mode, TX ring, bin yes/no for v0.3.x, lead_ms, volume, I2C lock confirmed, magnification and PGA, CARDPUTER_SILENCE_RMS.

(Phase 1) HIL.
- tools/hil/harness/fakewoof.py: a deterministic implementation of the WhisperWoof side of contract v1.
  - It watches a temporary notes_dir and writes status.json, transcript.json/.md, summary.md and result.json (memo intent todo when the fake text starts with '待办' (to-do)).
  - It ignores unknown manifest fields and passes the protocol/recording/v1 vectors.
- tools/hil/scenarios/recording.py. Recordings start and stop through debug.key with real keys; debug.rec only sets seg_ms.
  - rec_memo: hold G0 for 4 s → landed seg-000.wav (sha OK) → manifest → fakewoof → one daily-note line → card cleaned (files.n back to 0) → no queue.added in events.
  - rec_meeting:
    - debug.rec seg_ms=30000; Opt+R, Enter; 70 s with 2 ★ (Space); Opt+R, Enter;
    - expect 3 segments, contiguous samples, manifest consent ack, marks in session time, summary line;
    - record app-level effective KB/s against the ≥100 KB/s target and the 1 h ≤20 min floor.
  - rec_resume: cpa pause during the second segment, then resume → completes by offset, with no duplicate events.
  - rec_reboot: debug.reboot while recording → the rcv segment lands with a repaired header, and the session finalises (including the trailing-gap case).
  - rec_live_hold: start a pull of a multi-segment meeting, then hold G0 to start a memo mid-pull. Assert:
    - every file.* sent after that gets E_BUSY msg 'live';
    - no file.data arrives until a pong with live:false;
    - the pull then completes by offset with every sha correct.
  - rec_nosd: on cardputer-adv-test-nosd, holding G0 is refused, there is no files field, and v0.2 nosd behaviour holds.
  - rec_soak (G-A2): 10 min recording with heartbeat and snapshots; reply p95 ≤100 ms, 0 over 600 ms, 0 E_NOT_READY, 0 dropped frames from debug.state.
  - rec_rotation (G-A4): 30 one-minute segments, sample-exact.
- Register all scenarios in scenarios/__init__.py and select.CANONICAL.

(Phase 2) Manual checklist. Add G-A5 (ear; not applicable from v0.3.1, which has no cues), G-A7 (dimmed backlight), G-A8, G-A9, G-A10, G-A12, G-G5, G-W2 and G-E2E to docs/07-hil-checklist.md, with exact steps and pass criteria from docs/11 §14.

Out of scope: app firmware, daemon or WhisperWoof code changes. Defects found are reported back to M3-A, M3-B or M3-W.

### Acceptance (tests first)

- (RED first) uv run --project daemon pytest tools/hil/tests -q   # fakewoof passes every protocol/recording/v1 valid vector round-trip and refuses invalid manifests; scenario helpers (sha/sample accounting, marks to session time, E_BUSY window assertion) unit-tested; existing HIL unit tests still pass
- python3 tools/repo/check_sizes.py --warn 400 --fail 800 tools/hil firmware/hwtest/src firmware/hwtest/host && uvx lizard -l python -L 50 -w tools/hil && uvx lizard -l cpp -L 50 -w firmware/hwtest/src
- cd firmware/hwtest && pio run   # gate commands build
- (device-owning agent, cpa pause 900, v0.2 cardputer-adv-test with SD) cd firmware/app && pio test -e cardputer-adv-test -f 'device/test_runtime_budget'   # G-A0 CPA_BUDGET line captured into docs/gates/m3-device-gates.md
- (device-owning agent, cpa pause 1800, hwtest flashed) uv run --no-project --with pyserial==3.5 python firmware/hwtest/host/hwtest_host.py xfer-sweep --out docs/gates/captures/g-a1.json && ... cue-sweep && ... mic-capture   # G-A1/G-A5/G-A10 tables and decided constants recorded
- (device with SD, cardputer-adv-test v0.3 flashed, cpa pause 3600) uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_memo,rec_meeting,rec_resume,rec_reboot,rec_live_hold,rec_rotation,rec_soak --report $TMPDIR/hil-rec.json   # exit 0; rec_meeting reports app-level KB/s
- (same firmware) uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,sd_store,sd_bulk,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk,back_keys --report $TMPDIR/hil-v02-regress.json   # G-R: v0.2 behaviour intact
- (cardputer-adv-test-nosd flashed) uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_nosd,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk --report $TMPDIR/hil-nosd.json   # nosd unchanged; recording refused

## Real-device gates

- G-A0 v0.2 heap baseline: flash the v0.2 cardputer-adv-test build with the SD card mounted and capture the CPA_BUDGET line of device/test_runtime_budget (heap_free, max_block). Decides whether M3-A allocates about 38 KB at boot (default) or the ring at record start: after allocation the device must still have ≥150 KB free heap and a ≥64 KB largest block. Device-owning agent, about 10 minutes. Start now.
- G-A1 HWCDC sustained throughput (hwtest !xfer: b64 vs bin, TX ring 4 vs 8 KB, pipeline depth, RAM vs SD source, ping RTT under load): produces one table. It decides the app TX ring size and whether v0.3.x adds a binary sub-mode (audio-capture.md §7.3). It does not validate app-level throughput; HIL rec_meeting does that. Agent, about 30 minutes. Start now.
- G-A5 Cue timing, lead_ms and I2C concurrency (hwtest !cue): measures how long Speaker.begin, the tone, Speaker.end and Mic.begin take, and the RMS for 0–500 ms after Mic.begin; hammers the keyboard during codec reconfiguration, with and without the mutex. Pass: loop step ≤50 ms; 0 bad keys and 0 codec-config failures in 20 runs with the lock; lead_ms and volume set by about 2 minutes of human listening; memo start ≤1.0 s from pressing G0, and meeting start ≤0.8 s from Enter. Blocks the M3-A constants. 2026-10-21, user decision: v0.3.1 records silently, so the volume and the listening step are not applicable; the start path is the G0 hold plus Mic.begin, and the I2C result stands. lead_ms was measured only with a speaker cycle before Mic.begin; the silent start is not re-measured (three silent !sdrec starts gave 130-150 ms), so re-measure with hwtest cue-silent and keep 150 only if p95 fits (m3-device-gates §4.3).
- G-A10 Mic level and mixed zh/en accuracy (hwtest !mic, 30 cm and 1 m, recorded on the device and the Mac mic at the same time): speech RMS compared with WhisperWoof's silence gates (0.001 whole-file, 0.003 per piece). Pass: SenseVoice MER on the device recording no more than 5 points worse than on the Mac mic, measured with WhisperWoof Upload Audio. Sets mic magnification/PGA and CARDPUTER_SILENCE_RMS. Human. Blocks the M3-A gain and the M3-W threshold; neither package is done before this.
- G-A2 Link responsiveness while recording (HIL rec_soak: 10-minute recording with heartbeat and snapshot pushes). Pass: 0 dropped frames, reply p95 ≤100 ms, 0 replies over 600 ms, 0 E_NOT_READY. Agent. Blocks release.
- G-A4 Seamless segment rotation (HIL rec_rotation: 1-minute segments × 30). Pass: summed samples equal wall time × 16000 minus the first-buffer latency, within 1 block. Also records the worst rotation time and the ring high-water mark. Agent. Blocks release.
- G-A6 Memory and stack (idle with SD / recording + cue / transfer; 2026-10-21: from v0.3.1 recording has no cue). Pass: idle free heap ≥150 KB and largest block ≥64 KB; recording ≥96 KB and ≥32 KB; transfer ≥128 KB; audio task stack margin ≥1.5 KB; loop margin ≥4 KB. Agent (device/test_rec_budget). Blocks release.
- G-A7 Battery: full charge, recording with the backlight dimmed (it is not turned off while recording), record until auto-stop, log bat_mv every minute. Pass ≥8 h. Sets the 3.45 V stop and 3.55 V warn thresholds. Human, overnight. Blocks release.
- G-A8 Power-loss recovery: 10 cuts during recording (power switch off and unplugged), 3 of them during segment rotation with 1-minute segments. Pass: every boot recovers a playable WAV with rcv:true; the session relinks via next, or is finalised with gaps (including the trailing-gap case); it syncs; lost clusters are noted with Disk Utility First Aid. Human. Blocks release.
- G-A9 Card pulled and card full during recording. Pass: pulling the card causes at most one stall (≤2 s) and a fail tone (from v0.3.1 no tone, only the on-screen reason), and the segment syncs after reinsert and reboot; with a nearly full card, the segment finishes cleanly with 'SD 卡已满' (SD card full). Human. Blocks release.
- G-A12 Feel: one day of use with 20 memo and 20 meeting start/stop cycles (meeting via Opt+R → Enter consent confirm) with ★ marks. The user confirms the tones are audible but not annoying and the consent confirm is not a burden, counts pocket mis-triggers, and checks that ● REC on the dimmed screen is readable. Human. Blocks release. 2026-10-21: the tone check is not applicable from v0.3.1 (recording is silent); the rest stands.
- G-G5 Summary quality (Mac side): Qwen3.5 2B map-reduce on a bench-like recording and on device recordings. Human scoring: action-item recall; fabricated todos (must be 0); every ★ present in 重点 (key points). Also check that the −30 s/+10 s marker window works. Human. Blocks release.
- G-W2 Dictation under background summarisation (Mac, WhisperWoof dev build with real llama-server): summarise a 60-minute recording while dictating 20 times. Pass: polish p95 within +300 ms of baseline; 0 silent POLISH_TIMEOUT fallbacks; llama-server serves the next request immediately after an aborted background call (otherwise switch background calls to stream:true or add a second slot). Human and agent. Blocks the W2 merge.
- G-E2E End-to-end with the real WhisperWoof: an offline 20-minute meeting started with Opt+R → Enter, with 3 ★ marks and project cpa, plus one '待办' (to-do) memo; then dock. Pass, within the §10 budgets: summary.md has 3 items in 重点 (key points) and ≥1 action, each linking back to transcript.md; the daily note has the meeting line and a '- [ ]' memo line; the device 录N (recordings N) returns to 0; cpa rec ls shows done; no queue items were created. Human. Blocks release.
- G-R v0.2 regression on the v0.3 test firmware: every v0.2 HIL scenario on cardputer-adv-test with SD and on cardputer-adv-test-nosd. Agent. Blocks release.
- Deferred, not run for v0.3.0: G-A3 (recording and syncing at the same time; would allow pulling while live), G-A11 (binary framing robustness; only if G-A1 leads to bin), G-G2 (full-duplex I2S; would bring back pre-roll and in-recording cues), G-G7 (marker-window tuning, folded into G-G5).

## Open questions (handled per the decisions above)

- Gestures: hold G0 for a voice memo (releasing stops it). Opt+R opens a one-key confirm, '开始会议录音？请先告知在场的人' (Start meeting recording? Tell the people present first), and Enter starts the meeting. Opt+R or ` then Enter ends it. In a meeting, G0 or Space adds a ★ mark; there is no separate todo mark in v0.3.0. Is this OK?
- Can PRD 04 §7.7's 300–500 ms pre-roll be dropped in favour of 'speak after the beep'? It would only come back if full-duplex audio (G-G2) ever works.
- Consent: meeting tones (about 360 ms, three notes) cannot be turned off in v0.3, and every meeting starts with the 'tell the people present' confirm, which is recorded as consent:"ack". The screen stays dimly lit with ● REC while recording. There is no LED and no silent meeting mode. OK? **2026-10-21, user decision:** recording is silent, for personal use, so from v0.3.1 recording makes no sound at all; the meeting consent confirm stays; the screen is the only feedback.
- AI-extracted todos: in v0.3 they stay only in each meeting's summary.md (as checkboxes), and '待办' (to-do) voice memos become '- [ ]' lines in the daily note. Nothing goes into any project/agent queue until v0.4 adds on-device review. Is that acceptable, or do you want tagged-project queueing sooner?
- Plaintext archive: is it acceptable that WAVs, transcripts and summaries sit unencrypted in ~/CardputerNotes, outside WhisperWoof's vault (which appears to be on)? Should audio be kept forever (about 115 MB per hour) or for a retention period? Please confirm the folder is not synced to iCloud or Obsidian Sync.
- Summaries: use only the local model you selected in WhisperWoof (Qwen3.5 2B today) by default? Or may long meetings use a cloud model, opted in per project?
- WhisperWoof process: may agents prepare the changes in a local clone and, with your approval each time, open the issue and the 4 PRs (W1 transcriber, W2 preemptible LLM queue, W3 inbox and transcription, W4 summaries) on h3qing/WhisperWoof? Who reviews and merges? Is it OK to bump WhisperWoof's VERSION (for example to 2.9.0) to release? Note that in v0.3 recordings will not appear in WhisperWoof's history or notes database; that moves to v0.5.
- Transfer speed: ship base64 only in v0.3.0? The target is at least 100 KB/s, which means about 20 minutes per recorded hour in the worst case and a few seconds per short memo. A binary mode would come in v0.3.x only if G-A1 shows a clear gain.
- Protocol naming: use generic file.* messages for host-pull transfer (replacing the outbox kind:"rec" reserved in docs/09) and keep voice.* reserved for v0.4 voice replies?
- Limits: memo cap 5 minutes, meeting cap 3 hours, and at most 256 unsynced segments (about 21 hours) before new recordings are refused. Are these OK?
- Should the WhisperWoof Cardputer inbox stay off until you enable it in WhisperWoof settings (recommended), or turn itself on when ~/CardputerNotes/recordings exists?
- In v0.3.0 the device shows only 'recording' and 'N segments waiting to sync'; transcription status is visible on the Mac only (cpa rec ls, daily note). Is it OK to defer the on-device recordings list (Opt+V) to v0.3.x?
- Re-running a failed or unsatisfying summary in v0.3.0 means deleting result.json in the session folder, which overwrites summary.md, including your edits. A proper `cpa rec retry` that protects edits comes in v0.3.x. Acceptable?
- Beyond the personal-todo checkbox lines, should anything from the thinking-partner and personal-todo ideas be in v0.3? Otherwise they stay in v0.5.
