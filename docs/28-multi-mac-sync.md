**English** · [中文](28-multi-mac-sync.zh-CN.md)

# 28 · One device, several Macs: the device is the courier, the Macs are the filing cabinets

> Status: **design draft, waiting for your call** (§10) · Date: 2026-10-26 · Revision r2 (after three reviews; how each point was handled is in §11) · Role: architecture (who owns the data between the device and several Macs) · Based on origin/main `ca8e003` · Related: docs/11 (recording transfer), docs/18 (v0.8 profiles, not implemented), docs/23 (Mac inbox, archive state), docs/25 §8 and the unpushed `feat/projects-app` (projects become "named groups"), docs/26 (SEAL encryption at rest, not implemented), docs/27 (new Mac, not implemented).
> Requirement (2026-10-26): when the device is plugged into a Mac, sync the recordings to it, but a recording meant for another Mac must not be lost just because this Mac connected first; recordings and data such as groups should also be kept on the device, so switching computers does not mean connecting and grouping again; find the best balance.
> Marks: **[src]** code or docs read · **[meas]** read-only measurement on this Mac · **[inf]** inference · **[asm]** design assumption, can change · **[tbv]** to be verified by a test · **👤** needs you.
> Scope: design only. No device connected, no serial port opened, no code changed; for `~/CardputerNotes` only the files were counted.
> Path shorthands: `fw/` = `firmware/app/lib/`; `cpad/` = `daemon/src/cpad/`; `$CPA_HOME` = `~/Library/Application Support/CardputerAssistant`.

## 0. One-page summary

1. **What you worry about is real today** (§1): any Mac that connects pulls every recording off the device, and once the check passes the device deletes its own copy at once [src fw/rec/file_req.h:7-8; docs/11:341]. If the wrong Mac pulls them, they are gone from the device and the other Mac never gets them.
2. **The balance in one sentence**: **the device is the courier; each Mac is a filing cabinet.** A recording stays on the device until it reaches "the Mac it should go to"; after delivery the device keeps a **kept copy** for a few days; if it went to the wrong Mac, you can "deliver again" from the device, and **this time it does not go to the Mac that just took it**. The groups (1–9) are mirrored on the device, so switching Macs does not mean grouping again; transcripts, archive state and folder links still live only on each Mac.
3. **Prevent first, then recover**: when you plug into a Mac and there are recordings **made offline**, the screen first shows a hint for 5 seconds: `离线 3 条 → 这台 · Tab 不给` ("3 offline go to this Mac · Tab: no"). If that is right, press nothing (0 presses); if not, press Tab then Enter (2 presses), and these recordings are kept from this Mac (§3.2, D11). This is the most common mislabel: what you record offline on the road follows the Mac you last connected to.
4. **Delivering again is not swallowed by the same Mac** (§3.4): on each delivery the device writes a hash of the receiving Mac's `host_id` into the ledger (4 B; `hello.host_id` is already on the wire today [src fw/protocol/messages.h:82; decode_host.cpp:16]). Recordings delivered again are **invisible** to that Mac and listed only to the other Macs.
5. **Kept copies** (§3.3): **7 days** by default; the segment limit is derived from the boot scan table (about 1260 segments in the default build, far more than 7 days' worth); when free space drops below 1 GB, the oldest whole recordings are deleted first. Kept copies are **invisible to every Mac**. They are kept and deleted **by whole recording**, never splitting a meeting.
6. **The ledger `SENT.L`** (§3.4): one small append-only file per epoch directory, 28 B per record, with a magic number and CRC; before it is opened for appending, any torn tail is cut off, so a power cut loses at most the last record; every write is fsynced before the ack. Only the audio task writes it (single writer). Losing a record always means either "delivered one extra time" or "the reassignment didn't take, and the screen shows it"; a recording is never lost.
7. **Kept copies are plain text** (S1 comes before SEAL, §5): `↑0` no longer means "no plain text on the card". The lock screen and Settings → Storage must show the number of kept segments; docs/18's "clear to zero before going to an untrusted Mac" becomes "clear to zero, then clear kept copies". Whether kept copies are on is 👤 item 2.
8. **Groups** (§4): the Mac is still where you edit them. The device stores a mirror (key, name, number + identity hash `tid`). **One set or two (one each for home and work) is 👤 item 1**: one set = you group only once, but the work Mac can see the home group names; two sets = you group once at home and once at work, which only saves work when the Mac you replace is a broken one. The default is one set, because switching computers and grouping again most likely means home ↔ work.
9. **Merging deletes nothing** (§4.3): each Mac stores the whole table from the last sync (not just a hash); when pulling back from the device it **only adds, never deletes**, and a group missing from the device merely becomes "unnumbered" on this Mac; when both sides changed, take the union by key; on a name conflict this Mac wins and the other version is saved aside.
10. **New Mac**: after binding (3–4 presses on the device, docs/27 §3.2; this is a security gate and **cannot be skipped**), the Mac app shows one row: "5 groups on the device [Use these]". This overturns half of docs/27 D12, limited to names and numbers (§4.4).
11. **Protocol**: all additive, `v` stays 0. Recordings: each `file.items` entry gains an optional `foreign` (§3.6); filtering by `host_id` is a change in behaviour, not in format. Groups: `projects.snapshot.tid`, `revs.ptid`, a new request `projects.get` and its reply `projects.items` (§7).
12. **After SEAL** (§6): by default **sealed only to the current profile** (docs/26 unchanged); after SEAL, kept copies and reassignment can be used only when the recipients allow it, otherwise they are greyed out. "Seal offline recordings to both sides" is downgraded to optional (👤 item 3), stating plainly that it weakens docs/26's goal.
13. **Phases** (§8): S0 touches only the Mac (an export bundle as a stopgap); **S1a** ledger + kept copies + host hash + the 5-second plug-in hint + one item "把最近交给 ·3F2A 的再交一次" (deliver again what recently went to ·3F2A) (does not wait for v0.8; about 2 PRs + one power-cut gate); S1b a list of kept copies you can page through; S3a (if one set is chosen) brings the device mirror and new-Mac adoption before v0.8; S2 reassignment follows v0.8; S3b two-set mirror; S4 follows SEAL.
14. **All 18 decisions** have defaults (§10.1); the 6 things you need to answer are in §10.2.

## 1. How things move today

### 1.1 Recordings

| Step | Today | Source |
|---|---|---|
| Where it is stored | SD card only: `/cpa/rec/<E7>/<B5><S3>.W<S1>` is the WAV, `.M<S1>` is a JSON of ≤512 B, and the `.M` is the commit marker | [src fw/rec/rec_paths.h:1-6; docs/11:205-216] |
| What `.M` holds | id, rec (= the id of segment 0), seg, last, next, mode, bytes, sha256, at, project or slot (only the number when the key is not known), stars, lead_ms, consent, rcv, fw; **no** profile, host or "delivered" field | [src fw/rec/rec_meta.h:26-44] |
| In-memory index | `RecIndex` holds at most 256 entries of 16 B each, plus a `rev`; rebuilt by the boot scan | [src fw/rec/rec_index.h:15; rec_scan.cpp:63-84] |
| What 256 means | "Segments waiting to sync"; when full, recording is refused (about 21 h) | [src fw/rec/rec_core.h:37-38] |
| Boot scan table | 16 B per name. The default build borrows the 24 KB audio ring buffer as the table, which holds 1536; with `CPA_REC_ALLOC_AT_START=1` (the ring buffer is allocated only when each recording starts, **not** "record at boot"; default 0) it allocates 4800 B temporarily, which holds only 300, and frees it after the scan | [src fw/hal/audio_task.h:21-24,37-38; audio_task.cpp:15,87] |
| Table full | Names beyond it are dropped in directory order; the comment says "wait for the next boot" | [src fw/rec/rec_scan.cpp:48] |
| Delivery | The Mac pulls: `file.list` → `file.get` (chunks of ≤1380 B, with CRC) → `file.del{size,sha256}`; always `E_BUSY "live"` while recording; at most 4 in flight | [src docs/11:303-347; protocol/limits.json:51-56] |
| Deletion | `file.del` checks size and sha256, then deletes `.W` first and `.M` second; it acks even if the file is already gone, so a resend is harmless | [src fw/rec/file_req.h:7-8] |
| Mac side | `.part` (0600) → verify → `seg-NNN.wav` → `rec.seg.stored` → `file.del` → `rec.seg.deleted`; **a segment that already has `rec.seg.stored` is not pulled, it just gets `file.del`** | [src cpad/recordings/puller.py:133; pull.py:190-193] |
| Mac list cursor | Lives for one session only; after a reconnect it lists from the start | [src cpad/recordings/puller.py:115-126] |
| Mac assembles a recording | Complete only when segment 0 through `last` are all there; the environment stamp and destination come from segment 0; the group comes from the last segment's project, otherwise **this Mac's** number table turns the slot into a key | [src cpad/recordings/manifest.py:4-6,38-45,76-82,115-125] |
| How long the Mac keeps it | Once there is a transcript and summary, the audio is cleared after 30 days; the text stays forever | [src cpad/recordings/retention.py:1-10,27] |
| Split by profile | **No**. Neither the firmware nor `recordings/` has a profile or host gate | [src cpad/recordings/stamp.py:1-10] |
| Does the device know the Mac? | `hello` carries `host_id`; the firmware decodes it but "doesn't use it before v0.8" | [src fw/protocol/messages.h:82; cpad app.py:248] |

**Conclusion**: after delivery the device keeps nothing, and dedup happens only in each Mac's own `events.jsonl`. Today, "plugging into the wrong Mac first" means losing the recordings.

### 1.2 Typed items (outbox)

One file per item, sent in id order only once the link is Ready, deleted on `stored` / `duplicate`, moved to `bad/` on `rejected` (only 20 kept) [src docs/05:488-525; outbox_core.h:7,19-20]. Typed things are small, and losing one costs far less than losing a recording; this doc **adds no kept copies for the outbox** (D12) and only has it follow docs/18's profile tag.

### 1.3 Groups (projects 1–9)

- Mac: `$CPA_HOME/projects.json` {key, name, slot, root, mode}. `feat/projects-app` (unpushed, `wt-proj`) turns projects into "named groups"; `root` may be empty, and so may `slot` (not in 1–9) [src wt-proj cpad/projects/edit.py:1-7].
- Device: caches only **one** `/cfg/projects.json`, re-encoded by `ProjectsData` with its count and rev reset; at boot `rev=0` forces the Mac to push again; **the last Mac to connect overwrites it** [src fw/shell/runtime_outbox.cpp:222-251].
- `projects.snapshot` carries only rows that have a number (`slot` required, `x-uniqueBy: slot`), and its only direction is `h2d` [src protocol/schema/v0/projects.snapshot.schema.json:6-17].
- From device to Mac there is only `revs{projects,sessions,queue}` in `hello` / `pong`; no message can send the table back to the Mac [src common.schema.json:81-89].

### 1.4 What the docs have already decided (none of it implemented)

| Doc | Decision relevant here |
|---|---|
| docs/18 §3.5 | **Core invariant**: items and segments tagged with profile P go only to Macs bound to P; for other segments `file.get` returns `E_UNKNOWN_ITEM`, `file.del` acks without deleting, and counts and rev are filtered by profile [src docs/18:138-165] |
| docs/18 §3.4, §15 item 4 | Offline recordings belong to the current profile; plugging into a bound Mac switches the current profile; **v0.8 does not change a profile afterwards** [src docs/18:15,21,707] |
| docs/18 §3.6 | One project cache per profile: `/cfg/p1/projects.json`, `/cfg/p2/…` [src docs/18:172] |
| docs/18 threat notes | A Mac that can flash firmware can reset the device into download mode, flash firmware and read the SD card, with no key press; before going to an untrusted Mac, clear the waiting items to zero; `↑N` tells you how much plain text is on the device [src docs/18:25,303-304,434,464] |
| docs/26 §4.3, D7, D8 | On write, seal to the Macs confirmed for this profile; with no key, plain text; **never seal to a Mac of another profile**; `prof` in the header is protected by the AAD (the header's SHA-256) [src docs/26:155-157,182-183,506-507] |
| docs/27 §4.1 | Notes, recordings, transcripts, `events.jsonl`, archive, stars and hidden state live only on the Mac that received them [src docs/27:300-315] |
| docs/27 §4.3, D12 | **"The device as a courier (a new Mac reads 1–9 from the device)" is not done** [src docs/27:337] |
| docs/23 | Archive state belongs to each Mac; unknown project keys form their own group, "other Macs' projects" [src docs/23:99] |

## 2. The requirements, taken apart

| Requirement | Meaning | This doc |
|---|---|---|
| A recording meant for another Mac must not be lost because this Mac connected first | A Mac that should not have a recording takes it, and the Mac that should have it never gets it | §3.2 hint on plug-in (prevent); §3.3–3.4 kept copies + deliver again (recover) |
| Recordings and app data should also be kept on the device | The device has a copy too | §3.3 kept copies (time-limited); no "device as permanent archive" (D1) |
| Data such as groups is stored on the device too | The groups have a copy on the device | §4 device mirror |
| Plugging into one Mac must not void the data on the device | Plugging into a Mac should not make things on the device disappear | §3.3 |
| Switching computers should not mean connecting and grouping again | Less work when switching Macs (most likely home ↔ work) | Groups: §4, depends on one set or two (👤 1); connecting: binding still takes 3–4 presses on the device (docs/27 §3.2), a security gate that is not skipped |

## 3. Recordings: the device is the courier

### 3.1 Where things live (overview)

| Thing | Device | Each Mac | Notes |
|---|---|---|---|
| Recordings not yet delivered (audio + `.M`) | **Yes**, until delivered to the right Mac | — | Already so today |
| Recordings after delivery | **Kept copy**: 7 days by default, by whole recording (§3.3) | Yes, long term | New |
| Who it was delivered to | A 4 B host hash per segment in the ledger | Knows for itself | New, used only for "don't give it to that Mac when delivering again" |
| The recording's owning profile | `prof` in `.M` (docs/18, set at recording time, never changed) + reassignments in the ledger | One copy in the manifest | Reassignment happens only on the device |
| Transcripts, summaries, archive state, stars, hidden | No | Yes | docs/23, docs/27 §4.1, unchanged |
| Groups 1–9 (key, name, number) | **Yes**, a mirror (one set, or one per profile, 👤 1) | Yes (edited here) | §4 |
| Folders linked to groups, permission mode, hooks | No | Yes | Anything that runs something never crosses over (docs/27 D12) |
| Destinations (Opt+letter) | docs/18's cache, only for offline use | Yes | Destination ids belong to each Mac [src docs/14 §2.2], not carried across |
| Bindings, key fingerprints, "the id at the last Mac connection" | NVS | Identity file | docs/18, docs/26; the last one is new, 12 B (§3.2) |

**No "the device is the single source of truth"** (D1): the device has no PSRAM, FAT is not safe against power cuts (a power cut during a rename can leave two names [src docs/gates/sd-card.md:133-136]), and it is the thing most easily lost. The Macs are the filing cabinets; the device only promises "nothing is lost before delivery, or in the few days after it".

### 3.2 Who it goes to

1. **After v0.8**: as in docs/18 §3.5. A segment is listed only to Macs with `prof ∈ ok(H)`, **minus** the Mac excluded by "keep from this Mac" in the ledger (§3.4).
2. **Before v0.8** (S1): there are no profiles, so any Mac you clicked "Connect" for (docs/27 O3) can take it, except the excluded one.
3. **The 5-second plug-in hint** (D11, S1a): on every disconnect the device stores the **last MonoId it issued** in NVS (12 B). The next time it is plugged in, once the handshake completes, if there are segments "newer than that and not yet delivered" (= recorded offline), a hint that does not block anything appears at the bottom of the screen for 5 seconds:
   - before v0.8: `离线 3 条 → 这台 · Tab 不给` ("3 offline go to this Mac · Tab: no")
   - after v0.8: `离线 3 条 → 家 · Tab 改` ("3 offline → Home · Tab: change")
   - During those 5 seconds **only these segments are held back** (not in `file.list`), and everything else syncs as usual; after 5 seconds they are released and `files.rev` goes up by one.
   - Tab → Enter (2 presses): before v0.8, these recordings are marked "keep from this Mac" for this Mac (the same host exclusion as AGAIN); after v0.8, they move to another profile (same as PROF).
   - Press nothing: 0 presses, delivered as usual.
4. **Reassign** (S2, D3): on the device, move recordings **not yet delivered** to another profile.
   - Only whole recordings (by `rec`, all segments together, **one ledger record**), never single segments;
   - The recording in progress cannot be reassigned;
   - While connected to a Mac, if any segment of the recording is on the current `file.list` page or in transfer, the option is greyed out and says "拔线后再改" (unplug, then change), so that half of it is not already on that Mac; segments held during the 5-second hint are not affected;
   - Ones already delivered (now kept copies) cannot be reassigned directly; use "再交给 公司" (Send again to Work) (§3.5), which both reassigns and excludes the original Mac;
   - After SEAL, the target profile's Mac must be among this segment's recipients `rk`; otherwise the option is greyed out and says "公司 的 Mac 解不开这条" (Work's Mac can't decrypt this) (§6).

### 3.3 Kept copies (what the device keeps for a few days after delivery)

**Rules** (D4, D5):

| Item | Default | Notes |
|---|---|---|
| When a segment becomes a kept copy | `file.del`, with size and sha256 both matching | This used to delete the files; now it appends `DELIV` (with the host hash), fsyncs, removes the segment from the index, then acks (the write order in §3.4) |
| Unit | **Whole recording** (`rec`) | Segments become kept copies one by one (they are delivered segment by segment), but **deleting and delivering again go by whole recording**; while a recording still has segments waiting to sync, its delivered segments are not deleted |
| How long | 7 days, counted from when the recording's last segment was delivered | Wall time comes from `time.set`; segments with `wall_s=0` (time unknown at delivery) are **never deleted by time**, only by space and segment count; clock jumps below |
| Maximum segments | `kKeptMax = 扫描表容量 − 256 − 16` (扫描表容量 = scan table capacity): 1264 in the default build; 752 in the allocate-on-demand build once `kScanScratch` goes from 4800 B to 16 KB | See "Where the segment limit comes from" below |
| Space | When free space is below 1 GB, delete from the oldest whole recording | Used only when free space is known (read from FSINFO, `free_known`); otherwise only by time and segment count [src fw/shell/rec_port.h:31] |
| Who can see them | **No Mac**; `file.list`, `files{n,rev}` and `hello.pending` do not count them | Nothing leaks, the Macs' dedup is not disturbed, and they do not use up any of the 256 segments |
| Very long recordings | A recording with more than `kKeptMax / 2` segments (about 105 h by default) gets no kept copy; on delivery the screen says "太长未留底" (too long, not kept) | Almost never happens; when it does, use S0's Mac export bundle |
| How deletion works | First append `GONE` (by `rec`) and fsync, then delete each segment's `.W` and `.M` | Power cut in between: the boot scan sees files still present for a `GONE` id and finishes deleting them |
| Who deletes | The audio task: after the boot scan, after each delivery, after receiving `time.set`, and **when each segment finishes recording** (while recording, it deletes only one recording, between segments) | Each segment is at most 10 min ≈ 19 MB, so kept copies have made room long before recording's 256 MB warning line [src rec_core.h:58-59] |
| You can change it | Settings → Storage → Kept copies: off / 7 days / 30 days | Switching to "off" deletes all kept copies at once, so like "Clear kept" it needs a confirm box (cursor on "Cancel") |

**Where the segment limit comes from**: the boot scan puts each name into a 16 B table slot; when there are more names than the table holds, the extra ones are dropped in directory order, **so segments waiting to sync can silently drop out of the index**, and the low count then lets even more recordings in [src rec_scan.cpp:48]. So the number of kept segments must always leave room for "256 waiting + kept + the one segment being recorded + orphan `.W` files left by a crash + ones half deleted": 16 are reserved for that. One more safeguard (D15): the scan counts any overflow; on overflow the index is marked "incomplete", **new recordings are refused** (saying "Too many files: sync first"), and once recording stops it rescans using the ring buffer. 1264 segments in the default build is about 210 h of recording, so what actually applies is the 7 days and the space. [tbv] In the allocate-on-demand build the largest free block at boot is about 209 KB [src audio_task.h:21-24]; the 16 KB temporary table needs measuring.

**Clock jumps** (D16): `time.set` from any Mac is accepted as is [src fw/shell/runtime_link.cpp:109]. A Mac whose clock is a year fast would make every kept copy "expire" at once. Rule: if the new wall time jumps further ahead than "the last wall time + time since boot + 1 day", **it is not used to delete kept copies this time**; wait until a later time (from another connection) gives a close value, then count.

**Kept copies on or off** (👤 item 2): in S1, before SEAL, kept copies are plain text (§5). On by default for 7 days, but the lock screen and Settings → Storage always show the number of kept segments and the age of the oldest in days: `待传 3 · 留底 12（最旧 5 天）· 空闲 478 GB` (to sync 3 · kept 12 (oldest 5 days) · free 478 GB).

**Why "invisible" rather than "kept for another Mac of the same profile to fetch automatically"** (a change to point 2 of the orchestration plan, D5):
- Usage A (the default) has only one Mac per profile, so fetching automatically does nothing for it;
- Visible kept copies would be taken by any Mac you clicked "Connect" for, which conflicts with "clear to zero before going to another Mac";
- docs/27 D8 already does not do "a meeting follows the first Mac" by default.
If usage C really needs it, it comes in S5 (D13).

**Storage and wear**: keeping a file longer costs zero writes; each segment adds one 28 B ledger append. The card is 512 GB, FAT32, 32 KB clusters [src docs/gates/sd-card.md:14,25]; audio is 115.2 MB/h [src docs/11:207]. [inf] This much writing is not what limits the card's life; the real limits are the scan table, RAM and FAT under power cuts.

### 3.4 The ledger `SENT.L`

`.M` is not rewritten (512 B budget; writing in place can tear it; `.M`'s `prof` is also needed for SEAL's AAD); instead each epoch directory gets one append-only ledger (D6):

```
/cpa/rec/<E7>/SENT.L or SENT.K     28 B per record, little-endian:
  magic(1)=0xA5  op(1)  prof(1)  flags(1)  boot(4)  seq(4)  seg(2)  rsv(2)  wall_s(4)  host(4)  crc32(4)
op: 0 GEN  file header: boot+seq hold the generation
    1 DELIV a segment delivered, now a kept copy; id = this segment; host = hash of the receiving Mac's host_id
    2 PROF  reassign; id = this recording's rec; prof = new profile; flags.foreign
    3 AGAIN deliver again; id = rec; host = the Mac to keep it from; prof = new profile (0 = unchanged); flags.foreign
    4 GONE  deleted; id = rec
flags.foreign: this recording changed profile or changed Mac; on delivery file.items carries foreign (§3.6)
```

- **Recorded by `rec`**: one PROF, AGAIN or GONE record covers every segment of a whole recording (waiting and kept alike), so after a power cut the whole recording has either taken effect or not, never half to Home and half to Work. DELIV is recorded per segment, because delivery is per segment anyway. A segment's state = its own last DELIV and the last PROF / AGAIN / GONE of its `rec`, whichever comes later in the ledger.
- **Host hash**: `host` = the CRC32 of `host_id` (carried in `hello`, already there before v0.8); `0` is reserved for "unknown". In memory each `RecIndex` entry gains 1 B, pointing into a small table of up to 4 "keep from" hosts (16 B), +272 B in total [asm]; this is also the place docs/18 F2's per-item filtering needs. For the excluded Mac, `file.list`, `files.n` and `hello.pending` do not count these segments.
- **Write order** (D17):
  - DELIV: append → fsync → remove from the index → ack. If the append or fsync fails, reply with an error (no ack), and the Mac retries next time. When the card is full (ENOSPC) and the ledger cannot be written, fall back to today's behaviour: delete directly, since the Mac has already stored it.
  - PROF, AGAIN: the screen waits until the audio task replies "written and fsynced" before showing "已改" (changed) / "Queued again"; on failure it says "Not changed".
  - Before AGAIN puts a recording back to waiting, it re-reads each segment's `.M` and the `.W` size; if a segment is missing (deleted earlier) or waiting + its segments > 256, it refuses, saying "Incomplete: cannot deliver again" / "Too many waiting: sync first".
  - GONE: see "How deletion works" in §3.3.
- **Torn tail**: when the ledger is opened for appending, it is first truncated to the last complete record whose magic number and CRC are right, then written. When reading hits a bad record, it searches forward by magic number for the next position that passes the CRC. So one power cut loses at most the last record, and later appends are never all misaligned.
- **What losing one record does** (§9's predictions are all written from this):
  - Losing DELIV: the segment is waiting again. When the same Mac lists it, the Mac already has `rec.seg.stored`, so it just sends `file.del`, and DELIV is recorded again; **before v0.8, if the next Mac is a different one, that Mac pulls this segment too**: both have a copy, nothing is lost, but one extra Mac got it (an allowed outcome; after v0.8 the profile gate stops it). fsync comes before the ack, so this happens only with "a power cut before fsync".
  - Losing AGAIN: still a kept copy, and the screen shows it did not take (because the screen waits for fsync).
  - Losing PROF: the owning profile is unchanged, and the screen shows it did not take.
  - Losing GONE: at boot the files are seen still there, and the next tidy deletes them.
- **Single writer** (D18): only the audio task writes the ledger (it already owns the file system). AGAIN, PROF, "Clear kept" and the 5-second hint's "keep from this Mac" in the UI are all sent to the audio task as new `RecCmd`s, and the screen waits for its reply event. Today's commands are only `{Start, Stop, Mark, Tag, List, Get, Del, SegMs, MicGain, Codec}` [src fw/rec/rec_msgs.h:30]; `Ledger` (with an op) and S1b's `KeptPage` need adding.
- **Compaction**: when the ledger is > 16 KB, the live records are written under the other name (`SENT.L` ↔ `SENT.K` in turn), with a `GEN` record first and the generation +1, fsynced, and then the old file is deleted. If both exist at boot: apply them by generation, old first, then new; if the new one's GEN is bad, use only the old one. The file names are not segment names: `rec_parse_name` accepts only 11 characters, with `.` at position 9 and W or M at position 10, so the ledger is never taken for a segment [src fw/rec/rec_paths.cpp:41-42].
- **Boot scan**: each epoch reads the ledger first (1 open), then scans the names. Today every name needs its `.M` read (once in `scan_entry` and once in `predecessor`) [src rec_scan.cpp:101,117]; "don't read `.M` for kept segments" **needs a code change**: the scan puts the waiting names at the front of the table according to the ledger state, and kept and deleted ones skip `.M`. This is counted in S1a.
- **Offline mark**: on disconnect, store the last MonoId issued (12 B) in NVS, for item 3 of §3.2.
- **epoch**: `kMaxEpochs = 8`; more than 8 epoch directories are not scanned. That is already so today; but after wiping the device more will be left on the card than today (the kept copies), so wiping the device must either clear them too or point you to `cpa sd scrub`.

### 3.5 What you press on the device

All of this is in **the Recordings app → Tab menu** (no new keys), and these options do not appear while recording. Presses are counted from Home:

| You want to | How | Presses | Phase |
|---|---|---|---|
| On plug-in, you see the offline recordings are going to the wrong Mac | Tab → Enter during the 5-second hint | **2** | S1a |
| You just delivered to the wrong Mac: deliver the whole batch again | Open the Recordings app → Tab → "把最近交给 ·3F2A 的 3 次再交一次" (deliver the 3 that recently went to ·3F2A again) → Enter | about 4 | S1a |
| Look at the kept copies and pick one to deliver again | Open the Recordings app → Tab → "最近交出" (recently delivered) → ↓ to that row → Enter → "再交给 公司" (Send again to Work) (before v0.8, "Send again") → Enter | 6–7 | S1b |
| Reassign one that is not delivered yet | Waiting list → ↓ to that row → Enter → "改归 公司" (move to Work) → Enter | about 5 | S2 |
| Reassign everything recorded offline | Open the Recordings app → Tab → "离线录的 3 条改归 公司" (move the 3 recorded offline to Work) → Enter | about 4 | S2 |
| Delete the kept copies now | Settings → Storage → "Clear kept" → confirm box (cursor on "Cancel") | about 5 | S1a |

- "把最近交给 ·3F2A 的…" (deliver again what recently went to ·3F2A…): appears only when this device delivered something during its most recent connection and that Mac is not connected now; `·3F2A` is the first 4 digits of the host hash (replaced by the Mac's name after v0.8). The whole batch is one AGAIN record, with no list needed on the device.
- The "最近交出" (recently delivered) list (S1b): today the device has no recording list of its own; the Recordings app's list is given page by page by the Mac (`rec.list` → `rec.items`) [src docs/13:342,652]. It needs a new paging command for the audio task, `KeptPage`, which writes its result into a buffer the caller provides (it cannot go into `RecEvt`: the event queue has 16 slots [src fw/hal/audio_task.cpp:12], so a bigger event costs 16 times over). Each row: time, length, group, `→家` (→Home) or `→·3F2A`; after v0.8 only the current profile's are listed, and the rest show as `+N 条别的档案` (+N in other profiles) (same as `↑0+` in docs/18:303).
- When you have delivered to the wrong Mac, you are most likely still plugged into it: "deliver again" then excludes this Mac at once (host = the current connection), so it cannot swallow them straight back.

### 3.6 The Mac side

- **S1a needs only one optional change**: the delivery flow, `file.del` and dedup stay as they are. Segments delivered again are **not listed at all** to the excluded Mac; to other Macs they are ordinary new recordings.
- **Each `file.items` entry gains an optional `foreign:true`** (additive only; an old cpad ignores it [src protocol/README.md:52]): this recording changed Mac or changed profile. When a Mac sees `foreign`, it **trusts only the project key in `.M`, not the slot** (the slot is a number from another computer or another profile, and translating it would put the recording in the wrong group [src manifest.py:80]); with no key it counts as "no group". A key this Mac does not know goes into "other Macs' projects" [src docs/23:99].
- The second Mac receives **a new manifest with the same `rec` id**, and its archive state starts from scratch. If the two Macs have synced their groups (§4), the group key lands in the same group.
- The copy on the first Mac does not disappear by itself (the Macs do not talk to each other). It can be hidden or deleted in the Mac app's inbox as usual.
- **Mac rules after v0.8** (aligned with docs/26, §6): a Mac decides whether to accept by **its own binding** plus `rk`; `prof` in `.M` and in the SEAL header is the profile at recording time, used only to check the AAD, and **a recording is not refused just because it differs from this Mac's profile**.
- **S0 stopgap (Mac only)**: "Send to another Mac…" on a recording card in the Mac app makes a `recording/v1` directory bundle (manifest + wav); AirDrop it over, and the other Mac runs `cpa rec import`. This works today; the downside is that both Macs must be at hand. It is also the fallback for a very long meeting that got no kept copy.

## 4. Groups: a mirror on the device

### 4.1 One set or two (👤 item 1, decide this first)

In usage A there is one Mac at home and one at work.

| | One set (default) | Two sets (docs/18 §3.6's original default) |
|---|---|---|
| How many times you set up groups | **Once**, home and work share 1–9 | **Once each** at home and at work |
| Does switching Macs save work | Yes, for home ↔ work and for replacing a broken Mac | Only for replacing a broken Mac |
| Privacy | The work Mac sees the home groups' **names** (not the recordings or folders) | Neither sees the other's |
| When it can be built | **S3a, without waiting for v0.8** | S3b, waits for v0.8 |

In one sentence: **two sets = set up groups once at home and once at work; one set = set up once, but the work Mac sees the home group names.** One set is the default, because the requirement's case of not regrouping after switching computers is most likely between home and work; if the group names themselves are sensitive (client names, say), choose two sets.

### 4.2 Format

- One set: `/cfg/projects.json`; two sets: `/cfg/p1/projects.json`, `/cfg/p2/projects.json`. Written with today's `commit_file`.
- The content is still that one `projects.snapshot` line, with the counts zeroed, plus `tid`. `tid` is **not kept automatically**: today's cache is re-encoded from `ProjectsData` [src runtime_outbox.cpp:225-236], so `ProjectsData` and the codec both need one more field.
- `tid` = a 32-bit hash the Mac computes, covering only (slot, key, name), sorted by slot; **0 is reserved** (a result of 0 becomes 1), because `ptid=0` means "the device has no mirror". The firmware only stores it and never computes it.
- Only the key (`^[a-z0-9][a-z0-9-]{0,11}$`), the name (≤24 bytes of UTF-8) and the number 1–9 are stored [src protocol/limits.json:12-15]. **No `root`, `mode` or destinations.**
- The mirror is written in place on FAT, and a torn write counts as no cache [src runtime_outbox.cpp:224-226]; `ptid` comes only from a file that decoded, and is reported as 0 when it does not decode. So "getting the groups back from the mirror when the device is lost / a Mac breaks" is best effort.

### 4.3 Who decides: a three-way comparison

The Mac is still where groups are edited (`cpa project add|rename|slot|rm`, the Mac app). For each device (and, with two sets, for each profile), each Mac stores **the whole table from the last time both sides agreed** (≤9 rows of key/name/slot) and its `tid`, in `$CPA_HOME/sync/<设备码>.json` (named by device code; one writer per Mac). After the handshake the Mac reads `revs.ptid`:

| Device `ptid` | This Mac's `tid` | `synced` | Meaning | Action |
|---|---|---|---|---|
| = this Mac's `tid` | = device `ptid` | any | In agreement | Push only the counts (today's behaviour); `synced` is updated to the current table |
| = `synced.tid` | ≠ `synced.tid` | present | This Mac changed it | Push |
| ≠ `synced.tid` | = `synced.tid` | present | The other Mac changed it | **Pull**: `projects.get`, merged into this Mac per 4.4; one line in the Mac app: "Another Mac changed the groups: +Reading, renamed 2→3 [Undo]" |
| ≠ `synced.tid` | ≠ `synced.tid` | present | Both changed it | Pull, merge per 4.4 (union), push the result back; on a name conflict this Mac wins, and the overwritten name is saved to `$CPA_HOME/projects.conflict-<时间>.json` (with a timestamp); one line in the Mac app: "1 group name differs between the two [Have a look]" |
| ≠ this Mac's `tid`, non-zero | any | **absent** | First time this Mac sees the device (a new Mac, `settings/v1` imported, S3 just shipped) | **Ask once**: "Use the device's / Use this Mac's"; if this Mac has none, go straight to 4.5 |
| 0 | any | any | The device has no mirror (old firmware, just wiped, file corrupt) | Push |

No timestamps (the clocks on the device and the two Macs are not necessarily right).

### 4.4 How a pull merges (**a pull never deletes**)

Matched by key:
- On both sides → use the device's name and number; **this Mac's `root` and `mode` are left alone**.
- Only on the device → create a group **with no linked folder** (`root=null`); it can only archive, not open a session, and says "Link a folder" next to it.
- Only on this Mac → **kept**. If it had a number in `synced` and is gone from the device, this Mac only makes it "unnumbered" (the other Mac may simply have moved it out of 1–9, and the snapshot never carries unnumbered groups anyway). **Deleting happens only on this Mac**, never as part of a pull.
- Number conflict (two keys want the same number) → the device's one keeps the number, this Mac's one becomes "unnumbered".
- The same holds for a push: the device mirror is only the 1–9 page, so a push never makes another Mac delete anything.

The cost: a group deleted at home is still on the work Mac (just unnumbered) and has to be deleted again at work. What it buys: a group with a linked folder and archived recordings is never silently deleted by another Mac.

### 4.5 A new Mac's first binding

Conditions: the device has just bound this Mac (docs/18 §3.2 or docs/27 O3), this Mac has no groups, and the device's `ptid ≠ 0`.
The "Groups" row in the Mac app's first-run setup (docs/27 §2.3): `设备上有 5 个分组 [用这些] [不用]` (The device has 5 groups [Use these] [Don't]). Click "Use these" → `projects.get` → all created per 4.4 (none with a linked folder) → `synced` saved.

**This overturns half of docs/27 D12** (D9). docs/27 gave two reasons for not doing it:
1. **It needs new messages**: add `projects.get` / `projects.items` (§7); old firmware answers `E_UNKNOWN_TYPE`, and the Mac falls back to the `settings/v1` export file.
2. **Keep other profiles from reading it**: with one set, any bound Mac can read it (that is the cost in 👤 1); with two sets, only the profiles in ok(H) are returned, and an empty ok(H) gets an empty table; `revs.ptid` follows docs/18 §3.5 item 6. An unbound Mac always gets an empty table.

The `settings/v1` export stays, for destinations, `rec_pull` and other settings that are not on the device; the projects it imports are still forced to `plan` mode (docs/27 D12).

### 4.6 Aligning with projects-app

`feat/projects-app` is not pushed yet. Before it merges it needs four things: the `tid` algorithm in `cpad/projects/registry.py` (a pure function, with test vectors, 0 reserved); `add|rename|slot|rm` update `tid` after writing `projects.json`; `$CPA_HOME/sync/<设备码>.json` stores the whole table from the last sync; the Mac app's two notice lines ("changed by a pull" / "conflict") and undo.

## 5. Lost, replaced, wiped

| What happened | Recordings | Groups | Recovery |
|---|---|---|---|
| Recorded offline, plugged into the wrong Mac | Can be stopped in the 5-second hint | Not affected | Tab → Enter |
| Already delivered to the wrong Mac (same profile, or before v0.8) | Still in the kept copies, for 7 days | Not affected | "Send again" (再交一次; excludes the wrong Mac automatically), then plug into the right Mac |
| Wrong profile marked (after v0.8) | Not delivered: reassign; delivered: "Send again to Work" (再交给 公司) from the kept copies | Not affected | §3.5 |
| A very long meeting (> about 105 h) delivered to the wrong Mac | No kept copy | — | S0 export bundle on the wrong Mac |
| A Mac broken / lost | The last 7 days are in the kept copies, the rest in that Mac's Time Machine | The device has the mirror (best effort) | After binding the new Mac, "Use these"; "Send again" for the kept copies (after SEAL the old key has to come over with Migration Assistant, docs/26 §5.5; otherwise "can't open") |
| Switching Macs (Migration Assistant) | The Mac's whole filing cabinet moves over | This Mac has them | docs/27 D13 unchanged; `synced` moves over too |
| Device lost | What was waiting to sync is gone; everything delivered is on the Macs | Each Mac has its own | When the new device first connects, the Mac pushes the table (`ptid=0`) |
| **Device lost, with kept copies** | Whoever has the device can read **the last 7 days** of recordings (plaintext before SEAL); today they are deleted on delivery, so this is a new risk | The group names in the mirror (with one set, both home's and work's) | "Clear kept" (清掉留底) before going anywhere you don't trust; ciphertext after SEAL |
| Device wiped (NVS or card) | Still there if the card is (the marks are in `.M` and the ledger); with the bindings gone nobody gets them (docs/18 fails closed) | The mirror is on the card | Bind again; docs/27 D14 |
| An untrusted Mac flashes firmware and reads the card | Both waiting and kept recordings can be read (plaintext before SEAL); **no need to remove the card**, docs/18:25,434 | Group names in plaintext | Before going: sync to zero, **clear kept copies**; "clear" is only an unlink and free clusters can still be read, so `cpa sd scrub` [src docs/26 §4.4] |

**SECURITY.md needs changes** (together with S1a): `↑N` no longer stands for all the plaintext on the device; the lock screen adds the kept count next to the waiting count (`↑0 留12`, "↑0 kept 12" in English); docs/18:464's advice becomes "sync to zero, clear kept copies, `cpa sd scrub` when needed"; the group names in the mirror (with one set, both sides') are plaintext.

## 6. How this fits with SEAL (docs/26)

- **docs/26 is not changed by default** (D14): each file is sealed only to the confirmed Macs of **the current profile**; with no key it is plaintext; it is never sealed to another profile.
- So after SEAL, "reassign" and "Send again to Work" (再交给 公司) work only when `rk` contains a Mac of the target profile (the device can read the plaintext `rk` in `.M`); otherwise they are greyed out. When the home and work Mac lists differ, things recorded offline cannot be reassigned afterwards. Then the 5-second hint of §3.2 stops them before delivery (it stops "who gets it", which needs no re-encryption).
- **Optional: seal offline recordings to both sides** (👤 item 3): a recording started while no Mac is connected is sealed to all Macs of both profiles. The cost must be said plainly: under docs/18 §3.4 every offline recording has a current profile, so this covers most of your memos; a work Mac, **without removing the card**, can reset the device into download mode, flash firmware and read the card (docs/18:25,434), and so open these home memos. This weakens docs/26's goal; choosing it means changing both docs/26 D7 and D8 ("never seal to another profile's Macs", docs/26:507).
- **AAD and `prof`**: the `prof` in the SEAL header (plaintext, never written back) is the profile at recording time, and AAD = the header's SHA-256 [src docs/26:182-183]. Reassigning writes only the ledger and never touches the header, so the AAD check always uses the original header and still passes. Mac rule: **when the header's `prof` ≠ the delivery profile, it counts as valid only if my `kid` is in `rk`**. This loosens the tamper protection on `prof` a little (even if it is changed, the file can only go to a Mac that can open it), and needs a line in docs/26.
- Kept files are ciphertext; "Clear kept" is still only an unlink, and what stays in free clusters is ciphertext.

## 7. Protocol changes (additions only, `v` stays 0, no new error codes)

| Change | Direction | When | Size |
|---|---|---|---|
| Storage for kept copies, deliver again and reassign | — | S1, S2 | Format unchanged: kept copies are not counted in `file.*` or `files{n,rev}` |
| `file.list` / `files.n` / `hello.pending` leave those segments out for the excluded host | device→Mac | S1a | Behaviour change, format unchanged; uses `hello.host_id` |
| `file.items` gets an optional `foreign` (bool) per item | device→Mac | S1a | At most +15 B/item; recompute items per page against `line_max` 2048 and add a worst-case line vector |
| `projects.snapshot` gets an optional `tid` (uint32) | Mac→device | S3a | +16 B |
| `revs` gets an optional `ptid` (uint32) | device→Mac (`hello`, `pong`) | S3a | +16 B |
| New request `projects.get{}`, new reply `projects.items{tid, projects:[{slot,key,name}]}` | Mac→device / device→Mac | S3a | One question, one answer, like `rec.list`/`rec.items` and `file.list`/`file.items`. `projects.snapshot` uses the request envelope (`re` must be absent [src common.schema.json:7-15; README:60]), so it cannot serve as a reply. 9 rows of at worst about 120 B each (the name `\u`-escaped is 48 B) → about 1.1 KB ≤ 2048; add a worst-case line vector |

Old firmware answers `E_UNKNOWN_TYPE` [src protocol/README.md:52]; old cpad ignores `tid`, `ptid` and `foreign`. `.M` gets no new fields (the 768 B of `kMetaMax` are kept for docs/26).

Alternatives: put the table into `hello` (1 KB on every handshake); treat the table as a virtual file (mixed into the recording counts). Both are messier than one question, one answer.

## 8. Phases

| Phase | What | Where | Depends on | Size | Risk |
|---|---|---|---|---|---|
| **S0** | Mac app "Send to another Mac…" export bundle + `cpa rec import`; one README line: "check the recording app before plugging into another Mac" | Mac | None | 1 small PR | Low |
| **S1a** | Ledger module (torn-tail truncation, GEN, compaction); boot scan reads the ledger, waiting segments first, refuse to record on overflow; `file.del` moves into the kept copies (write order); cleanup (7 days, clock jumps, space, end of segment); host-hash filtering + `foreign`; offline mark + 5-second hint; one "Send again what went to ·X recently" row; one line in System → Storage, kept count on the lock screen, kept-copies switch; SECURITY.md | Firmware + protocol (`foreign`) + Mac (understands `foreign`) | None (does not wait for v0.8) | About 2 PRs + one hardware power-cut gate | Medium: FAT power cuts, boot scan; pass §9 first |
| **S1b** | Pageable "Recently delivered" list (audio task `KeptPage` command + list screen) | Firmware | S1a | 1 PR | Low |
| **S3a** (if one set) | One mirror + `tid` + `ptid` + `projects.get/items`; Mac three-way comparison, add-only merge, ask once; "Use these" in first-run setup | Firmware + Mac + protocol | projects-app merged | 3 changes (firmware, Mac, protocol vectors) | Medium |
| **S2** | Reassign (one recording, "the N recorded offline"); PROF; the 5-second hint becomes per profile; `prof_[]` computed from `.M` plus the ledger | Firmware | docs/18 minimal profiles (P8-P, F0–F2, D1) | Goes with docs/18 | Medium |
| **S3b** (if two sets) | One mirror per profile, `ptid` per ok(H) | Firmware + Mac | S2 | 1 PR | Low |
| **S4** | Kept copies are ciphertext; "can't open" greyed out; optional sealing to both sides (👤 3) | Firmware + Mac | docs/26 SEAL | Goes with SEAL | — |
| **S5** | Another Mac of the same profile fetches kept copies automatically | Firmware + Mac | Only if you choose usage C | — | High; not done by default |

**The case in the requirement**: S1a solves "lost" (kept copies + deliver again, which the same Mac cannot swallow back) and most of "delivered to the wrong Mac" (the 5-second hint). But **S1 does not solve privacy**: before v0.8 the wrong Mac still pulls and transcribes the home memos; only S2 (v0.8 profiles) stops that.

## 9. Tests (written first in each phase)

- **Ledger**: for each op, truncate the last record by 1–27 B, then append a few more, **and require all the later ones to read back**; `SENT.L` and `SENT.K` both present (by generation); the new file's GEN corrupt; one CRC error in the middle (recovered by magic); an empty ledger. Oracle: no segment is ever **neither waiting nor kept but still has files**; losing one record can only lead to "delivered once more" or "a no-effect you can see on the screen".
- **Power cut** (HIL, run serially): cut power before the DELIV append / after fsync but before the ack; cut power halfway through deleting after GONE; cut power halfway through writing AGAIN. After boot every segment has exactly one state; after dedup the Mac stores nothing twice. **Allowed outcome**: with a power cut before fsync and another Mac next, both Macs have a copy (before v0.8).
- **Deliver again**: deliver to A → deliver again while on A → **after A reconnects, `file.list` does not list them and `files.n`, `hello.pending` do not count them**; B pulls as normal; both manifests have the same `rec`; slot-only groups in B's manifest become "no group" (`foreign`).
- **5-second hint**: record 3 offline, plug in; within 5 seconds `file.list` does not list these 3 but lists the others; after Tab → Enter they are never listed to this Mac; with no press they are listed after 5 seconds and `files.rev` changes.
- **Whole recordings**: a 6-segment meeting with a power cut halfway through PROF / AGAIN has the same owner for all 6 segments after boot; cleanup deletes only whole recordings; a recording partly delivered and partly waiting is not deleted; "deliver again" is greyed out for a recording with missing segments; with 250 waiting, "deliver again" for a 10-segment recording is refused.
- **Scan limit**: with names = table capacity + 1, the index is marked "incomplete", recording refuses to start, and the rescan after stopping is complete; measure the heap for the 16 KB table in the on-demand allocation build.
- **Clock**: kept copies with `wall_s=0` are not deleted by time; a `time.set` that jumps a year forward deletes no kept copies, and they are deleted only after a second connection gives a similar time.
- **Space**: with `free_known=false`, only time and segment count apply; with free < 1 GB, the oldest recording is deleted at the end of a segment; kept copies never push recording to the 256 MB warning line.
- **Invisible**: for a kept segment, `file.get` answers `E_UNKNOWN_ITEM`, `file.del` acks without touching it, and counts and rev do not change.
- **Reassign while in transfer** (S2): connected to a Mac with a segment on the current `file.list` page, PROF is greyed out.
- **Groups**: one case for each of the six rows in §4.3; `tid` test vectors agree between Python and firmware, and 0 is replaced; **A and B both change → A connects → B connects → A connects: the group A added is still there**; after one Mac moves a group out of 1–9, the other pulls it as just "unnumbered", with its linked folder and archived recordings intact; ask once when there is no `synced`; `projects.get` from an unbound Mac returns an empty table; 9 rows of 8-character Chinese names are ≤ 2048 after escaping.
- **SEAL** (S4): when `rk` does not include the target profile, "Reassign to / Send again to" are grey; a segment whose header `prof` ≠ the delivery profile and whose `kid` is in `rk` is accepted by the Mac, and AAD passes with the original header; one whose `kid` is not in `rk` is not delivered.

## 10. Decisions, and what needs your call

### 10.1 Decisions

| # | Topic | Default | Alternatives | Why | See |
|---|---|---|---|---|---|
| D1 | Split between device and Mac | The device is the courier (until delivered + a few days of kept copies), the Mac is the filing cabinet | Device as the permanent archive | The device has no PSRAM, FAT is not safe against power cuts, and it is the easiest thing to lose; the Mac has Time Machine | §3.1 |
| D2 | Who gets it | docs/18 §3.5, minus the hosts excluded in the ledger; before v0.8, any Mac you clicked "Connect" for, minus the excluded | A new rule | Already reviewed; the exclusion is a new layer on top | §3.2 |
| D3 | Reassign afterwards | Allowed: not delivered yet, whole recording, not recording, not in transfer while connected to a Mac; after SEAL limited by `rk`. **Revises docs/18 §15 item 4** | Keep not doing it | The case in the requirement is exactly a wrong mark at recording time | §3.2 |
| D4 | Keep after delivery? | Kept copies: 7 days, whole recordings, `kKeptMax` = scan table − 256 − 16, free < 1 GB deletes from the oldest, also cleaned at the end of a segment; "Off" needs confirmation | Delete on delivery (today) | Not losing comes first; the limit comes from the scan table, so it never crowds out waiting segments | §3.3 |
| D5 | Who sees kept copies | Nobody; only "deliver again" turns them back into waiting | Other Macs of the same profile fetch them automatically | No use under A; a Mac that should not have them would take them | §3.3 |
| D6 | How state is recorded | One append-only ledger per epoch, 28 B/record, magic + CRC, PROF/AGAIN/GONE per `rec`, DELIV per segment; torn tail truncated first; GEN generation + compaction alternating between two names | Rewrite `.M` | In-place writes on FAT tear, a power cut during a rename leaves two names; `.M` is needed for AAD | §3.4 |
| D7 | Keys on the device | All in the recording app's Tab menu: plug-in hint 2 presses, deliver a whole batch again about 4, pick one 6–7, reassign about 5; clearing kept copies needs confirmation | New hotkeys; do it on the Mac | No new keys; the most common case takes the fewest presses | §3.5 |
| D8 | Where groups live | Device mirror (key, name, number + `tid`), no folders or permissions; the Mac is where they are edited | Device as the single source of truth; not stored | Groups can be recovered both when switching Macs and when the device is lost; anything that runs things never crosses | §4.2 |
| D9 | A new Mac takes the device's groups | First binding, this Mac empty, the device has some → "Use these". **Revises docs/27 D12, names and numbers only** | Only `settings/v1` | The requirement: no regrouping after switching computers | §4.5 |
| D10 | Merge rule | Three-way comparison, the Mac stores the whole table from the last sync; a pull only adds, never deletes (missing ones only lose their number); both changed → union, on a name conflict this Mac wins and the other name is saved; no `synced` → ask once; `tid` 0 reserved | This Mac's whole table wins; timestamps | Overwriting the whole table would delete groups the other Mac just added and their folder links | §4.3, §4.4 |
| D11 | Ask on plug-in? | No, but with offline recordings a 5-second hint that blocks nothing and holds back only those; Tab → Enter keeps them from this Mac / reassigns them | No hint at all; ask every time | Right: 0 presses, wrong: 2, stopped before delivery | §3.2 |
| D12 | Kept copies for typed items? | No, they only follow docs/18's marks | Same as recordings | Small, cheap to type again | §1.2 |
| D13 | Automatic fetch for usage C | Not done, S5 held in reserve | Do it now | Usage A is the default | §8 |
| D14 | Offline recordings under SEAL | **Seal only to the current profile** (docs/26 unchanged); reassigning limited by `rk`; the Mac treats "header `prof` ≠ delivery profile, but `kid` in `rk`" as valid | Seal offline recordings to both sides (👤 3, weakens docs/26, needs D7 and D8 changed) | A work Mac must not be able to open home memos just by flashing firmware | §6 |
| D15 | Scan table overflow | Count the overflow → mark the index incomplete → refuse to start recording → rescan after stopping | Leave it for the next boot (today) | Today waiting segments silently drop out of the index and more recording is let through | §3.3 |
| D16 | Clock jumps | `wall_s=0` never deleted by time; a forward jump beyond "last time + time since boot + 1 day" is not used for deleting | Trust every `time.set` | One Mac with a wrong clock must not wipe all kept copies at once | §3.3 |
| D17 | Write order | DELIV: append → fsync → out of the index → ack; the screen waits for fsync; GONE is written before deleting; AGAIN checks the files first | Ack first | A loss can only go towards "delivered once more" | §3.4 |
| D18 | Who writes the ledger | Only the audio task; the UI sends commands and waits for replies | Whoever uses it writes it | Two tasks appending to one file on FatFs garble it | §3.4 |

### 10.2 Your call

1. **One set of groups for home and work, or two**: default **one set**: set up groups once, but the work Mac sees the home group names (not the recordings or folders). Two sets = set up once each at home and at work, and neither sees the other's. If any group names are sensitive (client names and the like), choose two sets.
2. **Kept copies on or off**: default on, 7 days. The cost: until encryption is built, the device holds the last 7 days of recordings in plaintext; if the device is lost, or an untrusted Mac flashes firmware and reads the card, all of them can be read. You can also choose 30 days, or "Off" (delete on delivery, as today).
3. **After encryption, should memos recorded offline also be sealed to the other side**: default **no** (sealed only to the profile of the time). If yes, reassigning afterwards is freer, but a work Mac can open the home memos you recorded offline just by flashing firmware.
4. **The 5-second hint when plugging into a Mac**: default on (it only appears when there are offline recordings, and blocks nothing). You can also choose "no hint" or "ask every time".
5. **When you delete a group at home, should the work Mac delete it too**: default **no**; it only loses its number, so that a group with a linked folder and archived recordings is never silently deleted; the cost is deleting it on each side.
6. **Do you have a case where "both Macs count as home"** (docs/27 usage C, for example a desktop plus a laptop): if not, S5 is not built.

Reply template: `1 一套 / 2 开7天 / 3 不要 / 4 要提示 / 5 不跟着删 / 6 没有` (that is: 1 one set / 2 on, 7 days / 3 no / 4 hint on / 5 don't follow / 6 none; with no reply, this line is what gets done).

## 11. Review log (r1 → r2)

Every issue from the three reviews was adopted; disagreements were settled like this:

- **Kept segment count**: one reviewer said 40 segments last only 2–4 days and "7 days" was dishonest; another pointed out that the 40 came from reading `CPA_REC_ALLOC_AT_START` as "record at boot", and that the default build's table holds 1536 names. The latter's facts were adopted: the limit is now computed from the scan table (1264 by default), and the on-demand allocation build raises its scratch table to 16 KB; plus the safeguard of refusing to record on overflow (D15). So "7 days" is true in the default build, and System → Storage shows how many days old the oldest one is.
- **Deliver again swallowed back by the same Mac**: all three raised it; unified as a 4 B host hash in every ledger record and 1 B per entry in memory pointing to a 4-entry table (D2, §3.4); a record grows from 20 B to 28 B (also adding the magic and a segment number relative to `rec`).
- **Fewer presses**: adopted the "5-second hint on plug-in" (D11), going from "don't ask" to "don't ask, but hint"; the whole-batch "deliver again" goes into S1a and the pageable list is split off into S1b, because the device has no recording list of its own today.
- **Slot-only groups landing in the wrong group**: the reviews gave two options (translate the slot to a key on the device / add a "drop the slot" mark). The device cannot change `.M` and the Mac cannot see the ledger, so the mark was chosen, and it reaches the Mac through the optional `foreign` in `file.items` (§3.6). This turns "S1 with zero protocol changes" into "S1 with one optional field".
- **D14 sealing to both sides**: the review pointed out the conflict with docs/26 D7 and D8, and that "out of reach over USB" was wrong (flashing firmware is enough to read the card). Changed to sealing only to the current profile by default, with sealing to both sides demoted to an option (👤 3), and the wording corrected.
- **Plaintext risk of kept copies**: adopted. The lock screen shows the kept count (not borrowing docs/18's `↑0+`, because that `+` already means "another profile has items waiting"), SECURITY.md and docs/18's advice change together, and the kept-copies switch becomes 👤 2.
- **Merging**: the reviews separately proposed "push the union on a conflict", "a pull never deletes" and "never delete one with a linked folder". The three became one simpler rule: a pull only adds, never deletes, and missing ones only lose their number (§4.4); deleting happens only on this Mac (👤 5).
- **One set or two**: adopted "ask this first, don't default to two sets"; the default became one set, and the part of one set that can come early became S3a.
- **The reply to `projects.get`**: adopted the new `projects.items`; `tid` has to go into `ProjectsData` and the codec.
- Everything else (GONE order, single writer, GEN generation, space rules, `free_known`, clock jumps, the line length with `foreign`, cited line numbers, closing the two [tbv] on `rec_parse_name` and AAD, `kMaxEpochs`, §0 saying binding still needs key presses, "S1 does not solve privacy") was changed as suggested.

## 12. S1a implementation record (2026-10-27, done without the device at hand)

S1a was built per §3 and §8. Below are the places that differ from the main text or that the main text left unclear; each one has tests.

| Item | Main text | Implementation | Why |
|---|---|---|---|
| DELIV's `rsv(2)` | Reserved | Stores `back` = this segment id's seq − the seq of this recording's rec (same boot; ≥ 0xFFF means "unknown", and the segment counts as a recording of its own) | The ledger alone can delete and count whole recordings, without reading `.M` again |
| AGAIN | One per `rec` | One per segment, one fsync per batch; `flags` gains `HOLD` (0x04) = exclude only, do not put kept copies back into waiting (the 5-second hint's "keep from this Mac" uses it) | A batch often spans several recordings; the screen says "Queued again" (已排回) only after the fsync, so after a power cut the screen shows it did not take effect |
| Torn tail | Truncate to the last good record, then append | No truncation: skip by magic when reading, and **tidy when dirty** into the other name (GEN + live records, fsync, then delete the old file) | No `ftruncate` on FatFs; it takes the same path as compaction, so the tests cover only one path |
| Both names present | Use both, old first, then new | Use only the old one (the smaller GEN generation; if the first record is not a good GEN, only `SENT.L` counts as the "original"), delete the new one, tidy again next time | The new one is the tidied result of the old one and may be half-written; the old one is always complete |
| What tidying writes (reviews 1, 2) | Live records | GEN with `BATCH` (0x08): `host` = the Mac of the latest batch (0 = none), `wall_s` = the record number of the batch's first record; then, in order: one DELIV for each segment that is GONE but still has files → one GONE per recording → kept DELIVs not in the batch → AGAIN (none with `host` 0) → the batch's DELIVs | Tidying drops the record of a delivery to another Mac that "separates two batches", so older kept copies from the same Mac would be merged into the latest batch (deliver again would send too much); GONE acts only on kept segments, so when the files could not be deleted and no DELIV comes before it, it does nothing and the recording goes back to waiting |
| Compaction threshold | > 16 KB | > max(16 KB, live records + 4 KB); always tidy after `清掉留底` (Clear kept), so no Mac hashes stay in the ledger | 1264 segments of live records are already 35 KB |
| Memory for replay | Not stated | When idle, borrow the 24 KB ring buffer as the table, 16 B per segment (the on-demand allocation build allocates 16 KB temporarily); while recording, ledger commands answer "busy" | The device has no PSRAM |
| `kKeptMax` | Scan table − 256 − 16 | The same: 1264 by default, 752 in the on-demand allocation build after `kScanScratch` goes from 4800 B to 16 KB; tidying deletes by segment count down to `kept_low` = limit − one full waiting table (256, or half of that when the table is small): 1264 → 1008; **at the limit, new deliveries are deleted straight away** (today's behaviour) | The boot scan table always fits; leaving room for a full waiting table means new deliveries are still kept when the clock is untrusted and free space unknown (review 5). At the limit it deletes the new one rather than the oldest, because at delivery time the audio task has no buffer for replaying the ledger |
| `RecIndex` | +1 B per entry, +272 B in total | +4 B per entry (`excl` pointing to the 4-host table, `first` = segment 0), +1 KB of heap in total | `first` lets the 5-second hint count recordings, not segments |
| How many Macs a segment excludes | Up to 4 | Each segment excludes only the most recent one (4 slots shared by all segments; a 5th Mac evicts the oldest slot) | After AGAIN, if it went to B and is then taken back from B, A getting it again is an allowed outcome |
| When cleanup runs | Boot, after each delivery, `time.set`, after each segment | After the boot scan, after unplugging (when this connection delivered something), after a trusted `time.set`, **between two file requests while plugged in with free < 1 GB** (review 4); **never while recording** | Delivery no longer frees space (kept copies), and a meeting started right after a pull, still plugged in, needs the space first; with free < 1 GB it deletes from the oldest, and a meeting is at most 345 MB, so recording never reaches the 8 MB refusal line |
| Incomplete recordings | Delivered segments are not deleted while any segment is waiting | A recording whose last segment (`last`) is not kept is not deleted by time, only by segment count, space or "Clear kept" | Waiting segments are in the loop's index, which the audio task cannot see |
| D15 overflow | Rescan with the ring buffer after stopping | Refuse to start recording, showing "Too many files: sync, then restart" (卡上段太多：先同步，再重启); rescan after the restart | In the default build the table is the ring buffer, so overflow happens only in pathological cases |
| Where "Send again" lives | Recording app's Tab menu | Settings → Storage (Opt+M `,` Enter, confirm box with the cursor on "Cancel", `/` Enter: 5 presses), with the value "from ·3F2A: 2" (·3F2A 的 2 次; the Mac that will not get them); the recording app (Opt+V) is not on the device yet | The same page has "Clear kept" and the facts about kept copies; something that cannot be undone needs confirmation (review 4); the earlier wording "2 → ·3F2A" (2 次 → ·3F2A) read the wrong way round (review 3) |
| Undoing "Send again" (review 6) | None | Settings → Storage → "To this Mac" (给这台 Mac; Opt+M `,` `.` Enter: 4 presses): for the waiting segments excluded from the connected Mac, one AGAIN HOLD each with `host` = 0, and that Mac gets them again; "Send again" leaves at least 12 free waiting slots (`kAgainSpare`), so it never fills the table and blocks recording | With only one Mac, "Send again" would leave recordings stuck in waiting |
| A 5th Mac (review 7) | Up to 4 excluded per segment | In the boot scan a 5th Mac evicts the oldest slot, and names pointing to that slot become "anyone may have it" (as in `RecIndex`) | Otherwise the wrong Mac would be excluded |
| Clock jumping back (review 8) | A forward jump > 1 day is not used | A jump of > 1 day forward or back is not used for now (no deleting by time; deliveries record `wall_s` as 0) until a later, similar time confirms it | A Mac a year slow would make delivered kept copies look a year old, and the next correct time would delete them all |
| 5-second hint | Tab → Enter | The same; during the hint, Tab belongs to the hint (no menu), and when it asks, 10 seconds with no answer counts as "give as usual"; offline mark = the next MonoId at unplug (NVS `offb`/`offs`, not written while recording); **while locked or with the Tab menu open the hint is not visible: the 5 seconds do not run, keys do not go to it, and those segments stay held, for up to 2 minutes**; after Enter, this Mac and those segments are recorded, so it stays excluded even after unplugging; the hint says "N offline" (counting recordings), and so does the receipt; when the request cannot be handed to the audio task it shows "Not changed: this Mac gets them" (没改成，已照常给这台 Mac); after 30 seconds without an answer the screen is released (and the audio task no longer drops ledger replies) | Nothing gets stuck; plugging in while locked in a pocket is the most common case (reviews 1–3, 5–7) |
| `file.items.foreign`, kept-copies switch (off/7/30 days), S1b list | S1a | Not done | This round did only the ledger, kept copies, deliver again, the hint and clearing |

Verified on the host (without the real device): `test_rec_ledger_model` (the reviews' reproductions, plus a fuzz test against a model: deliver again is exactly the latest batch, kept and cleared segments never return to waiting, random SD card failures during deletes), `test_runtime_kept_hint` (locked, Tab menu, unplug after Enter, receipt, 30 seconds without an answer, give to this Mac, plugged in without enough space), `test_ledger_rec`, `test_ledger_table`, `test_ledger_policy`, `test_rec_ledger` (a power cut at every call: delivery, deliver again, clear; a DELIV torn at every byte), `test_rec_ledger_cuts` (the ledger truncated at every byte, a CRC error in every record, a power cut at every call of the boot tidy, a fuzz test of random commands + random power cuts, run with `CPA_LEDGER_FUZZ_RUNS=400`), `test_runtime_kept` (two Macs end to end), the storage page in `test_mod_system`, `test_lock_card`; the simulator's four `scripts/kept/` scripts with screenshots in Chinese and English; cpad's dedup (`test_a_segment_listed_again_is_deleted_unpulled_and_archived_once`).

**Still needs the real device** (§9's HIL power-cut gate): cut power before and after the DELIV fsync, halfway through a clear, and during the boot tidy; the largest free block at boot for the 16 KB scratch table in the on-demand allocation build; the headroom of the audio task's 6 KB stack under ledger commands (`device/test_rec_budget`); how an `O_APPEND` open behaves on FatFs.

**After the real device (evening of 2026-10-27, docs/07 §11.1–§11.3)**:

- **hello's `files.n` was counted for the previous Mac** (§11.3 finding 3, fixed): the hello reply was filled in before `kept_hello()`, so `files.n` used the previous Mac's exclusion table (0 after unplugging = visible to all). On a real unplug the Mac lists anyway, and what it lists is right; but when B had just stopped and A handshook right after (the link never dropped), A's hello said 0, the following pong said 1 with `files.rev` unchanged, and A waited 60 s without listing. Now the device recognises this Mac before answering hello (`kept_host()`), and a change of Mac moves `files.rev` one step: hello itself counts for this Mac, and the Mac is sure to list again. During the 5-second hint, hello still counts the segments the hint is holding (the hint opens only after hello), `file.list` is empty, and `files.rev` moves when the hint lets go, so pulling is not affected. Tests: `test_new_mac_on_a_live_link_sees_its_own_count` and `test_hello_counts_for_the_mac_that_sent_it` in `test_runtime_kept_hint`.
- **Free heap 4 KB lower than v0.5.0, accepted** (§11.1 finding 2): `test_runtime_budget` free 169,540 B (v0.5.0 173,552, −4,012), minimum 162,260 (−5,344), largest block 147,444 (−8,192), audio task stack headroom 3,532 B (−196). That is more than the +1 KB estimated for `RecIndex` above; the rest is the engine's ledger buffer and the loop side's kept-copy state. 169.5 KB free is still far above the 96 KB recording needs and the 128 KB transfer needs, and every budget assertion passes. After ledger commands the audio task stack still has at least 2,040 B left (`CPA_REC_LEDGER` in `test_rec_budget`: deliver again 200 segments in 3.4 s, clear in 4 commands in 9.1 s).
- **"Send again" after "To this Mac" makes the batch wider** (§11.3 finding 4, current behaviour, not changed): a batch = consecutive segments delivered to the same Mac. After "To this Mac", A recognises these segments and deletes them without pulling, and the device records them again as kept copies delivered to A, so they join the batch; then A pulls 2 more recordings and you send again: 4 are queued again (the 2 given back earlier + the 2 new ones), B takes them all, and A still archives each `rec` only once. The screen's "Send again" row shows exactly this N before you press: "from ·XXXX: N" (再交一次 ·XXXX 的 N 次).

## Appendix: evidence index

- Firmware: `fw/rec/rec_paths.h:1-12`, `rec_paths.cpp:41-42`, `rec_meta.h:26-44`, `rec_index.h:15`, `rec_scan.cpp:48,63-117`, `rec_core.h:37-38,58-59`, `rec_msgs.h:30`, `file_req.h:1-10`, `fw/hal/audio_task.h:21-24,37-38`, `audio_task.cpp:12,15,86-89`, `fw/protocol/messages.h:66,82`, `decode_host.cpp:16-21`, `fw/shell/runtime_link.cpp:109`, `runtime_outbox.cpp:222-251`, `rec_port.h:31`.
- Protocol: `protocol/limits.json:5,12-15,30,51-56`, `schema/v0/projects.snapshot.schema.json:6-17`, `file.items.schema.json`, `common.schema.json:7-15,81-89,127-142`, `protocol/README.md:52,60`.
- Mac: `cpad/recordings/puller.py:115-133`, `pull.py:190-193`, `manifest.py:4-6,38-45,76-82,115-125`, `service.py:142,172`, `retention.py:1-10,27`, `stamp.py:1-10`, `app.py:248`; `cpad/projects/edit.py:1-7` and `registry.py` in `wt-proj`.
- Docs: docs/11:192-347; docs/13:342,652; docs/18:15,21,25,138-172,303-304,434,464,705-707; docs/23:99; docs/26:155-157,182-183,506-507, §4.4, §5.5; docs/27:300-340 and D8, D12–D14; docs/gates/sd-card.md:14,25,133-136.
- Measured on this Mac: `~/CardputerNotes/recordings`, only file names and sizes were looked at.
