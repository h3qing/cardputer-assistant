**English** · [中文](18-context-profiles.zh-CN.md)

# 18 · Context profiles (#19): strict separation of home and work

> Status: **design draft (revised after adversarial review), awaiting your decision** (§15) · Date: 2026-10-21 · Role: architecture (context profiles) · Target version: v0.8.0, after docs/14 (destinations) and docs/15 (local data).
> Based on: #19 (requirements and sketch); docs/13 §3, §5.2, §6.1–§6.2, §7.1, §9.6, §10.3–§10.4, §13 item 7, §14, §15, §18 item 8; docs/14 §2.2, §3.1, §7.2, §7.6–§7.8, §10.4; docs/15 §2.4, §3.6, §3.11, §7.3; docs/17 §1.4, §2, §3.6, §5.1, §9, §10.1 (drafted in the same batch); docs/05 §6.9, §8; docs/04 §5.3, §8.1, S11; docs/02 §3, §7; SECURITY.md; ADR 0001; `protocol/README.md` items 14, 20; `protocol/limits.json`; on the v0.4.1 branch (`wt-v041-daemon`), `daemon/src/cpad/config/host.py`, `protocol/validate.py`, `protocol/schema/v0/hello.schema.json`, `common.schema.json`, `protocol/tools/test_worst_lines.py` and the firmware's `lib/protocol/decode_host.cpp`; in the firmware (`release/v0.4.0`), `lib/outbox/outbox_core.h`, `outbox_io.h`, `lib/shell/{runtime_outbox,confirm,shell}.cpp`, `lib/storage/storage_plan.h`, `lib/rec/{rec_index,rec_meta,file_req}.h`, `lib/core/app_state.h`, `mono_id.h`, `lib/shell/status_bar.cpp`, `lib/hal/hal_sha256.h`, `test/core/{test_menu_model,test_codec_vectors,test_link_flows}`, `modules/system/`; the daemon's `device/port_finder.py`, `hub/reduce.py`, `config/settings.py`; `firmware/sim/README.md`, `sim/lib/simio/sha256.h`; `docs/gates/launcher.md`.
> Tags: **[src]** doc or code that was read · **[calc]** computed with a script · **[inf]** inferred · **[asm]** design assumption, can change · **[todo]** to verify with a gate, a test or the real device · **👤** needs you.
> Scope: design only. No device connected, no firmware run, no code changed. Screen mockups are drawn at 240×135, about 40 ASCII characters or 20 Chinese characters per line; `▸` marks the selected item. How review comments were handled is in the appendix at the end.

## 0. One-page summary

1. **What you want** (#19): the same Cardputer behaves differently at home and at work, and the two are **strictly separated**. What you record at home never reaches the work Mac, and the other way round, including recordings on the SD card that have not been transferred yet.
2. **Not possible today**: the device hands waiting notes and recordings to **any** Mac that completes a handshake. Whichever Mac is plugged in first takes them; once a recording is taken it is deleted from the device. There is only one project cache, and it follows the last Mac plugged in (§1.1).
3. **The model** (§3) has three parts:
   - **Profiles**: in v0.8, exactly two, **home** and **work**;
   - **Bindings**: which profile a Mac (identified by the `host_id` in the v0.4.1 hello) belongs to, **chosen only by you on the device**; what the Mac says about itself does not count;
   - **Current profile**: everything newly recorded is tagged with it. When you **plug USB into** a bound Mac, or a different Mac completes a handshake, it switches automatically to that Mac's profile; the same Mac handshaking again (cpad restart, Mac waking) does not undo your manual switch.

   Two rules govern every delivery:
   - **The device only hands items and recordings of profile P to a Mac bound to P.** An unbound Mac or a cpad that is too old (no `host_id`) gets nothing;
   - **Things derived from a Mac's data follow the profile the data came from, not the current profile** (for example a reply to a home session, or a queue item saved after starting a session failed).
4. **No typing** (§5): when a new Mac is plugged in, the device shows a choice sheet "This Mac belongs to: Later / Home / Work / Neither", with the cursor on "Later", and the choice needs a confirmation. Switching profiles by hand is in System (Opt+M) → Profiles, again a choice plus a confirmation. **No new keys**; the right side of the status bar gains one character ("家" for home or "公" for work).
5. **Things recorded offline** belong to the current profile, and the status bar always shows which profile that is. v0.8 does not change a record's profile afterwards, and does not switch automatically by time of day.
6. **WiFi**: **not done** in v0.8; only a gate name, G-WIFI, is reserved (§6).
7. **Mac side** (§7): cpad learns from the device which profile it serves, records `prof` in events and in `manifest.json`, and checks it again when it receives items; on a mismatch it writes nothing and handshakes again, **never replying `rejected`** (that would make the device move the item to `bad/` and eventually delete it). `cpa mcp serve --profile` and `cpa export --profile` filter by the tag on the data, which is phase D of docs/15.
8. **Threat model** (§8): v0.8 can guarantee that "an honest cpad cannot get the content of another profile, nor its count fields", which is L1. It **cannot guarantee** two things:
   - A Mac set on reading the device (for example one controlled by a company's IT) can use esptool to reset the device into download mode, read the whole flash, then flash a firmware that reads the SD card. Everything on the device that **has not been synced yet** is plain text. Real protection needs L3 (each profile's data encrypted at rest, P8-E, your decision);
   - A little metadata leaks: gaps in item IDs reveal how many items another profile recorded; while another profile is recording, `files.live` is true (§8.3).
9. **Migration** (§9): after the upgrade, until a cpad with a `host_id` is first plugged in, the device behaves as it does today. **On that first plug-in, delivery pauses** and a choice sheet waits for you; choosing "No profiles" returns to today's behavior. Items waiting from before the upgrade belong to no known profile, and the first binding asks you once about them, with the cursor on "Keep for now". Once profiles are on, you can turn them off on the device as soon as every profile-tagged waiting item on the device has been cleared.
10. **The protocol only adds things, `v` stays 0, no new message types or error codes** (§10):
    - The device's `hello` and `pong` add `prof{act, ok, ask, legacy}`;
    - `outbox.item` and `file.items[]` add `prof`;
    - `debug.state` adds `prof`;
    - Recording contract v1's `manifest.json` adds `prof`.
11. **Tests** (§12): run a virtual device in the simulator and let three cpads connect in turn (three `CPA_HOME`s: home, work, stranger). Every home record carries a canary string, and the test finally asserts that none of them can be found in the work side's events, notes or recording folders, and that neither side lost anything. At the protocol layer, add `flows/prof/` and a tool that checks "`prof ∉ ok(H)` never appears on the wire".
12. **The 9 decisions you need to make** are in §15. The most important is item 1: do you use **two Macs**, or **one laptop carried back and forth**?

## 1. Starting point and boundaries

### 1.1 What the device hands to whom today

| Thing | Where on the device | Handed to today | After handing over | Basis |
|---|---|---|---|---|
| Typed notes, to-dos, queue items | outbox: `/outbox/<id>.json` (under `/cpa/` on the card when there is an SD card) | Any Mac that completes a handshake, in ID order, at most 4 in flight | Deleted on `stored` or `duplicate`; moved to `bad/` on `rejected`, keeping only 20 | [src outbox_core.h:1-22; SECURITY.md] |
| Memos, meeting recordings | `.W` and `.M` under `/cpa/rec/<E7>/…` on the SD card | Any cpad, pulled with `file.list` / `file.get` (`rec_pull` on by default) | `file.del` after checking, deleted on the device | [src file_req.h; config/settings.py; docs/11 §3.3] |
| Project names and slots | `/cfg/projects.json`, only one copy, on main storage (the SD card when one is inserted) | Not uploaded; the snapshot pushed by the last Mac plugged in overwrites it | Used offline to turn `#3` into a project key | [src runtime_outbox.cpp:6-8; storage_plan.h:1-11] |
| Destination bindings (v0.5) | `/cfg/dests.json`, only one copy | Same | Same | [src docs/14 §7.2, §10.4] |
| Sessions, queue, "waiting for you" reasons, count of meeting to-dos awaiting confirmation (v0.6) | Memory only | From the Mac currently connected; after disconnecting, still shown as "the last snapshot" | — | [src app_state.h; docs/17 §1.4] |
| Queue items derived from Mac data | outbox | Offline replies to a session, and starting a session that failed or has an unknown result, are all saved as queue items | Same as row one | [src confirm.cpp:27-35; runtime_outbox.cpp:166-185] |

- Today's trust boundary is "physical USB plus the currently logged-in macOS user". After its first handshake, cpad binds to the device's USB serial number [src port_finder.py:1-4], but **the device side trusts anyone**.
- So if you carry 3 waiting items and a meeting recording from home to work, they go into the work Mac's `~/CardputerNotes` as soon as you plug into it.
- Pressing `#3` offline uses project 3 of the last Mac plugged in [src docs/13 §13 item 7].

### 1.2 In scope and out of scope

| In scope (v0.8) | Out of scope |
|---|---|
| The model of profiles, bindings and current profile; filtering the outbox and recordings by profile; derived items following their source profile; one project cache and one destination cache per profile; handling unknown Macs; the switching UI; cpad recording and checking `prof`; MCP and export filtering by profile; migration; tests | One Mac serving two profiles (P8-B1, waiting for the answer to §15 item 1); WiFi hints (P8-W); encryption at rest on the device (P8-E); wireless links and their pairing (the v0.3 wireless column of docs/05 §8); turning arbitrary apps on or off per profile |

### 1.3 Two ways of use

| Form | What it looks like | Can host identity tell them apart? | v0.8 |
|---|---|---|---|
| **A: two Macs** | One at home, one at work, each running a cpad | Yes, two Macs have two `host_id`s | **Done** |
| **B: one laptop** | The same Mac carried home and to work | No, there is only one `host_id`. Only a manual switch or WiFi can tell. And both profiles' data land on the same Mac under the same macOS user, so **they are separate for organization only, not for security** | The model leaves room (`prof.ok` is an array); the implementation is in P8-B1 |

docs/13 §18 item 8 assumed you "only plug into one Mac", and you never answered that point. #19 asks to switch by the connected computer or by WiFi, so both forms are possible. 👤 See §15 item 1.

## 2. Principles

1. **A profile is decided only by host identity and your choice, never by content**, the same rule as destinations [src docs/13 §3 principle 8]. Whatever the Mac says in its hello is only a hint; bindings are chosen only by you, on the device.
2. **Default deny**: an unknown Mac gets nothing. No waiting items, no recordings, no agent requests, and even "how many items are waiting" is reported as 0.
3. **Current profile and bindings are managed separately**:
   - The current profile decides the tag on new records and what the UI shows;
   - Bindings decide what can be handed over.

   For example, if you switch to "work" by hand, the home Mac can still take the "home" waiting items, but not the "work" ones.
4. **Derived things follow their source**: items derived from data pushed by a Mac (sessions, queue, projects) take the profile that data belongs to, not "the current profile when Enter was pressed".
5. **Narrowing takes effect at once; widening waits until the Mac knows**: forgetting a Mac or rebinding it gives less right away; binding, or handing over "undecided", gives more only after the device has told this Mac its new `prof`. So cpad never receives something it thinks it should not receive.
6. **Switch profiles only when there is no overlay, no recording and no request in flight**. A recording always counts as the profile at the moment it started; a meeting never changes profile halfway through.
7. **Choose only, never type**: every operation is "select with `;` `.`, press Enter, then pass a confirmation". No new keys, because docs/14 §3.1 has already handed out all 26 Opt+letters (the W kept for WhisperWoof went to the to-do app in docs/17).
8. **When something goes wrong, lean toward "deny"**: if the profile table cannot be read or NVS was erased, as long as the device still has profile-tagged items or the "profiles on" flag, treat it as "profiles on, but no bindings at all", never falling back to "give to anyone".
9. **The protocol only adds things**, `v` stays 0, no new message types and no new error codes [src docs/05 §6.9; docs/13 §12.2]. Recording contract v1 only adds optional fields.

## 3. Model

### 3.1 Profiles

| No. | key (on the wire, in events and in `--profile`) | Shown on the device | Status bar | Color |
|---|---|---|---|---|
| 1 | `home` | 家 (Home) | 家 | Existing `Color::Ok` [asm] |
| 2 | `work` | 公司 (Work) | 公 | Existing `Color::Accent` [asm] |
| 0 | (not written) | 未定 (Undecided) | — | — |

- The key format is `^[a-z][a-z0-9-]{0,11}$`. The data structures leave room for up to 4 profiles, but the v0.8 UI only offers two [asm]: Chinese cannot be typed on the device, and custom names would need configuration on the Mac side, which is not done until someone needs it.
- **0 means "undecided"**: things recorded before the upgrade, before the first binding, and after NVS was lost but before rebinding. They get special handling (§9): until you decide, they are handed to no Mac.

### 3.2 Bindings

- A binding has three parts:
  - The first 16 bytes of `sha256("cpa-host:" + host_id)`;
  - The profile number: 1, 2, or "neither";
  - The `host_label` that Mac reports about itself, for display only.
- **Only a `host_id` of at least 16 characters is accepted**. The protocol's `host_id_pattern` only requires 8 characters [src limits.json], which is 32 bits, too short to serve as a secret; cpad itself generates 16 hex characters [src config/host.py]. For one shorter than 16, the device says "This Mac's identity is too short to use profiles" and treats it as a cpad that is too old.
- **In v0.8 a Mac can be bound to only one profile** (form A). Choosing "neither" is also stored: the next time it is plugged in, it is not asked again and gets nothing; it is visible in System → Profiles and can be forgotten (§5.6).
- At most 8 are stored [asm], "neither" included. A similar product, cardputer-agent-deck, also allows at most 8 [src docs/02 §7]. When full, forget one in System → Profiles first.
- Bindings are stored in NVS (internal flash), not on the SD card, so changing the card does not change the bindings.

### 3.3 Current profile

| When | How the current profile changes |
|---|---|
| **USB is plugged into** a Mac bound to P (the device sees USB go from absent to present while running) and the handshake succeeds | Switches to P, with the notice "已切到 公司（插着「工作 MBP」）" (Switched to Work (plugged into "Work MBP")) |
| The Mac that handshakes is **not the same as the last one** (a different `host_id` hash), and it is bound to P | Same |
| The same Mac handshakes again (cpad restart, Mac waking, link timeout) without the cable being unplugged | **Unchanged**. Your manual switch stays in effect until another Mac is plugged in or the cable is plugged in again |
| USB unplugged | Unchanged (still the profile of "the last one plugged in") |
| Another profile chosen in System → Profiles | Switches, with a confirmation (§5.7) |
| A Mac that is unbound, was marked "neither", or runs a cpad that is too old is plugged in | Unchanged |
| Power-on | Restored to the one before power-off; "which Mac handshook last" is restored too (both in NVS) |
| NVS lost (§3.7) | Becomes "undecided"; everything recorded afterwards is undecided until you bind again |

- **When to hold off**: while there is an overlay, a recording, or an agent request or shared-channel request in flight, remember the switch and do it when all of these are over (principle 6). A manual switch at those times just says "等…结束再换" (wait until … ends to switch).
- **Why switch automatically on plug-in**: host identity is the first signal named in #19, and every binding was confirmed by you personally. Without the automatic switch, you would have to make one more choice every time you arrive somewhere.
- **Why a re-handshake does not switch**: cpad restarts, the Mac waking and link hiccups all cause a new handshake [src protocol's `daemon_restart` flow]. If every handshake recomputed the current profile from bindings, then after you switch to "work" by hand at home, one link hiccup would switch you back to "home", and the work content you record afterwards would go into the home Mac.
- **Why not switch by time of day**: it would change the profile without you knowing, and then silently put the wrong tag on what you record next. #19 only treats it as a fallback, and it is not done here (§15 item 8).

### 3.4 Tagging every record with a profile

| Record | Where the tag goes | What the tag is |
|---|---|---|
| Typed notes, to-dos, queue items (outbox) | The item file itself gets a `prof` field (this file is the line sent on the wire). `OutboxCore` also stores a 1-byte number, so sending can filter without reading the file | The current profile at the moment Enter saves it |
| Memos, meetings | Each segment's `.M` gets a `prof`; `RecIndex` also stores a 1-byte number | The current profile at the moment recording started (segment 0), copied to every later segment |
| **Derived queue items**: offline replies to a session (`decide_reply` taking `Route::Queue`), queue items saved when starting a session failed or had an unknown result (`flush_agent_save`), queue items saved from the project page | Same as row one | **The profile of the source data**: the `src` of the slice holding that session row, queue row or project row; `AgentReqInfo.src` of the in-flight request (§3.6) |
| Starting a session, replies (online requests) | Not stored; only sent to the Mac serving the source profile | At the moment of sending |

- On the wire it is always the key (`"prof":"work"`); in device memory it is the number (0–4).
- Old files have no `prof` and are treated as 0 (undecided).

### 3.5 Handover rules (the core invariant)

The device is connected to Mac H, and `ok(H)` is the set of profiles H is bound to (in v0.8 either empty or exactly one):

1. **outbox**: only send items with `prof ∈ ok(H)`. Undecided items are only sent after you assign "undecided" to H's profile (§9). Order is still by ID, still at most 4 in flight.
2. **Recordings**:
   - `file.list` only lists segments with `prof ∈ ok(H)`;
   - For other segments, `file.get` replies `E_UNKNOWN_ITEM`, and `file.del` replies `ack` without deleting. This is exactly the same reply as "this segment is not on the card", revealing nothing about its existence [src file_req.h];
   - `files.rev` only changes when a segment H can see is committed or deleted (one rev per profile, reporting H's profile's). cpad already "relists when the rev changes", so this is compatible.
3. **Counts**: `pending` and `files.n` in `hello` and `pong` only count what H may take.
4. **Online requests**: `agent.spawn`, `agent.reply`, `agent.pick`, `queue.done`, `inbox.dismiss`, `rec.*`, `dest.send` (D5-B), and the new v0.6 `choice.get`, `todo.list`, `todo.act` (docs/17 §9), are only sent when **the current profile ∈ ok(H)**. Otherwise everything is treated as offline; for example, Opt+S becomes a queue item. This is just one more gate on the device side; docs/17's protocol does not change.
5. **Snapshots pushed by H** (projects, sessions, queue, destinations, pending):
   - Current profile ∈ ok(H): taken into AppState, the slice's `src` recorded as the current profile, and stored in this profile's cache;
   - ok(H) not empty but the current profile not in it: **not taken into AppState**; only what can be stored (projects, destinations) goes into the cache of H's profile;
   - ok(H) empty: still reply `ack`, but neither take nor cache it.

   `time.set` is accepted as usual.
6. **Which `revs` to report** (the device's `revs.*` in `hello` / `pong`, which docs/14 §7.2 uses to decide whether to push again):
   - ok(H) not empty: report the copy belonging to H's profile. When that is the current profile, report AppState's; when not, report projects and destinations from H's profile cache, and echo the sessions, queue and pending (memory only) that H pushed in this connection;
   - ok(H) empty: echo the rev of the last copy H pushed in this connection (whether taken or not). So an old cpad does not push again every 5 s;
   - **Never report another profile's rev**: a rev is a content hash, and reporting it would hand H a fingerprint of another profile.
7. **When `ok(H)` is recomputed**:
   - On receiving the host's `hello`. A new handshake already clears in-flight requests and returns outbox items to waiting [src the invariants of "flows" in protocol/README.md]; after unplugging, the link goes back to Detached, and switching to another Mac requires a new handshake;
   - When you change bindings on the device (§3.7).

For a Mac with an empty `ok(H)` (unbound, marked "neither", no `host_id`, or the choice sheet is open), the link still handshakes and pings as usual, the status bar shows `◇ 未分配` (◇ Unassigned), `hello` / `pong` carry `prof{ok:[], ask}`, counts are reported as 0, and nothing else is given.

The test oracle in one sentence: **at any moment, no line the device writes on the wire to H contains the content of an item or segment with `prof ∉ ok(H)`, nor any count field computed from them (`pending`, `files.n`, `files.rev`, `revs.*`).** Gaps in item IDs and `files.live` are known metadata leaks and are not part of this oracle (§8.3).

### 3.6 What each profile has

| Thing | One per profile? | Stored where | Notes |
|---|---|---|---|
| Waiting notes and recordings | Yes | outbox and SD card (both profiles mixed, told apart by tags) | §3.4 |
| Project cache | Yes | `/cfg/p1/projects.json`, `/cfg/p2/projects.json` | Offline `#3` resolves with the current profile's projects, fixing "resolved with the last Mac's projects" |
| Destination cache (v0.5) | Yes | `/cfg/p1/dests.json` and so on | Offline Opt+letter follows the current profile's bindings. Destination ids are already per Mac [src docs/14 §2.2] |
| Sessions (with docs/17's `why`), queue (with `review` and the "for AI" label), pending (v0.5) | Yes | Memory only, taken per §3.5 item 5 | Each of these AppState slices carries a `src`. **Cleared on a profile switch**, then whatever can be loaded (projects, destinations) is loaded from the new profile's cache; sessions and queue wait for this profile's Mac to push them. docs/17's choice page and to-do page are cleared at the same time |
| Meeting recording switch | Yes | NVS | "Is meeting recording allowed", named in #19. Allowed for both by default; when off, pressing Opt+R says "公司 档案关了会议录音" (meeting recording is off for the Work profile). **Can be cut**: if you have no use for it, drop this row in P8-F3 |
| Status bar character and color | Yes | Hard-coded in the firmware | §3.1 |
| Preset replies (#16, v0.6) | Naturally | Configured on the Mac | Each Mac has its own set |
| Switches for other apps | No | — | #19's sketch of "which modules each profile turns on" is not done, apart from meeting recording [asm] |

**Why `src`**: review found a real leak path. Connected to the home Mac, switch by hand to "work", then reply to a home session: the device treats it as offline and saves the reply as queued feedback [src confirm.cpp:27-35; `queue_instead` in shell.cpp]; if the tag came from the current profile, the text would be tagged `work` and handed to the work Mac; then pressing Enter on it under "for AI" makes the work cpad start a **new** session with that text [src hub/reduce.py:141-174]. The same goes for the fallback queue item saved when an agent request fails after a switch while it was in flight [src runtime_outbox.cpp:166-185]. Now there are three safeguards:
1. Slices in AppState that come from a host always belong to the current profile (taken only when the current profile ∈ ok(H); cleared on a profile switch);
2. The tag of a derived item comes from the slice's `src`, and the failure fallback of an in-flight request comes from `AgentReqInfo.src`;
3. No profile switch while a request is in flight (§3.3).

### 3.7 When bindings take effect

| Change | Direction | Takes effect when |
|---|---|---|
| Forgetting a Mac; rebinding it to another profile; setting the currently connected Mac to "neither" | Narrowing | **At once**: recompute ok(H), return in-flight outbox items to waiting (as on a new handshake), stop answering `file.*` for disallowed segments; the next `pong` carries the new `prof` |
| Binding the currently connected Mac to a profile; handing "undecided" to H's profile | Widening | **After the device sends a `pong` carrying the new `prof`**. A `pong` can only be sent in reply to a `ping`, every 5 s [src limits.json `ping_interval_ms`], so at most 5 s late. The wire is an ordered serial stream, so cpad always sees the new `prof` before the newly allowed items |

Why split it this way: review pointed out that if uploading started right after a binding was confirmed, cpad would still hold `ok:[]`, and every item in the first few seconds of the sync would be refused; refused items go to `bad/`, which keeps only 20 and deletes the rest [src outbox_core.h:5, 21]. Now the device side tells cpad first, and on the cpad side a mismatch only causes a new handshake, never a refusal (§7.1 D2).

## 4. Host identity: `host_id`

### 4.1 Mac side

- **Already in v0.4.1** [src wt-v041-daemon config/host.py]:
  - On first run, 16 random hex characters are generated (`secrets.token_hex(8)`) and stored in `$CPA_HOME/host_id`, mode 0600;
  - If the file content after `.strip()` does not match `host_id_pattern`, it is regenerated;
  - Never written to logs;
  - `host_label` comes from `config.json`, at most 24 B.
- **v0.8 adds two things**:
  1. **Tie it to the hardware**, with the fingerprint in a **separate file** `$CPA_HOME/host_hw` (0600), holding the first 16 hex characters of `sha256(IOPlatformUUID)`. The format of the `host_id` file **does not change by a single character**: a v0.4.1 cpad reading a two-line file would judge it malformed and regenerate `host_id` [src config/host.py:22-40; validate.py:23-29 replaces `$` with `\Z`], and on a downgrade every binding would be void. Rules:
     - `host_hw` does not exist (upgraded from v0.4.1–v0.7): keep the existing `host_id`, write the fingerprint, **do not regenerate**;
     - `host_hw` exists and matches this machine: as usual;
     - `host_hw` exists but does not match: regenerate `host_id`, write the new fingerprint, and log "host_id regenerated (hardware changed)" without writing either value.

     **Why it is worth doing**: Migration Assistant and backup restores copy the home Mac's `$CPA_HOME` as-is to another Mac, which could then impersonate the home one; this is a real L1 risk. The cost is that after switching to a new Mac, the device asks you once more [asm].
  2. **`cpa host reset`**: switch to a new `host_id` by hand (for example before giving this Mac away), confirmed by typing `y`. The device's old binding will never match again; forget it in System → Profiles.
- `cpa status` does not show `host_id`; it only shows "本机：公司（设备上的绑定）" (This Mac: Work (binding on the device)).

### 4.2 Device side

- Only the hash is stored (§3.2), never the original. **No message on the wire ever echoes `host_id` or reveals anything about another Mac**; the device only tells H about H's own binding (`prof.ok`).
- One SHA-256 is computed per host `hello`. On the device with `hal_sha256` (mbedtls) [src lib/hal/hal_sha256.h], in the simulator with `sim/lib/simio/sha256.h`. The pure-function library `lib/core/profile` does not call them directly: the Runtime computes the 16-byte digest and passes it to `on_hello(digest, plugged)`, so native tests do not depend on the HAL.

### 4.3 Why no pairing key over USB

#19 mentions "pairing several hosts, one key per host"; docs/05 §8 puts "NVS key + HMAC challenge in the hello" together with wireless. v0.8 is USB only, so there is no pairing key, for these reasons:

- `host_id` is a 64-bit random number known only to that Mac (a 0600 file) and the device, and the device never reveals it. Malware on the same Mac is outside the protection scope anyway [src SECURITY.md].
- **USB is not entirely free of third parties either**: before cpad binds to a board's USB serial number, it sends `host_id` in plain text to any 303A:1001 serial port [src port_finder.py:3]. So a strange ESP32-S3 dev board plugged into your home Mac could get the home Mac's `host_id` on cpad's first handshake and later use it to impersonate it. That requires someone able to plug a device into your home Mac, which is inside the "physical USB" trust boundary; it is recorded as a residual risk in §8.3 T4.
- The other ways to impersonate the home Mac are to get the home Mac's `$CPA_HOME` (the hardware tie blocks a direct copy, §4.1), or to read out the device's flash. Whoever manages the latter already has what they want (all plain text on the device), and an HMAC would not block anything more.
- Adding a challenge would mean one more round trip, storing keys in NVS, an implementation on each end, and handling "the Mac changed but the key did not".
- **Once wireless is done**, the traffic can be overheard, and `host_id` can no longer serve as a secret. Proper pairing per docs/05 §8 comes then.

## 5. What you see on the device

### 5.1 First time: whether to use profiles

The first time v0.8 firmware meets a cpad with a `host_id` (and profiles have never been used before), it shows:

```
◇ 未分配 ↑0 !0               13:42 83%
要把"家"和"公司"分开吗？
分开以后，家里记的只交给家里的 Mac。
▸以后再说（这次什么都不交）
 这台是 家
 这台是 公司
 不分档案（和升级前一样）
;.选 Enter定（要确认）       Tab菜单
```

In English: "◇ Unassigned … Separate 'home' and 'work'? Once separated, what you record at home only goes to the home Mac. ▸ Later (hand over nothing this time) / This is Home / This is Work / No profiles (same as before the upgrade). ;. select, Enter choose (needs confirmation), Tab menu."

- **The cursor is on "Later"**, the safest row; "No profiles" is last. The first cpad with a `host_id` may well be the work Mac (a v0.4.1 cpad already sends `host_id`); if the default were "No profiles", two Enters pressed on another page would hand all pre-upgrade waiting items to work.
- **Key presses within 400 ms of opening are ignored**, as with confirmations [src docs/04 §8.1; confirm.cpp:56]. Every choice sheet does this.
- **While the choice sheet is open, nothing is handed over**, and the wire treats it as "unassigned": `hello` / `pong` carry `prof{ok:[], ask:true}`, and `pending` and `files.n` report 0. Otherwise pre-upgrade waiting items might already have gone to this Mac before you chose "Work". Closing the sheet with `` ` `` equals "Later".
- **When it appears**: not while locked, with an overlay, or recording; the link stays "unassigned", and the lock card gains a line "新 Mac 待分配" (new Mac to assign). This only happens once after the upgrade.
- Choosing "Later": nothing is handed over this time; it asks again next time it is plugged in, or press Enter on "这台 Mac（未分配）" (This Mac (unassigned)) in System → Profiles (§5.6).
- Choosing "No profiles": needs a confirmation. After confirming, everything is as before, and it never asks again. To use profiles later, open System → Profiles → "开始分档案" (Start using profiles) while connected to a Mac, and the same choice sheet appears (without the "No profiles" row).
- Choosing "Home" or "Work": goes to the confirmation of §5.3.

### 5.2 A new Mac (once profiles are on)

```
◇ 未分配 ↑0 !0               13:42 83%
新的 Mac：「Work Mac」
（名字是它自己报的）它属于：
▸以后再说（这次什么都不交）
 家
 公司
 都不是（以后也不交，只充电）
;.选 Enter定（要确认）       Tab菜单
```

In English: "New Mac: 'Work Mac' (the name it reports itself). It belongs to: ▸ Later (hand over nothing this time) / Home / Work / Neither (never hand over, charge only)."

- The cursor defaults to the safest row; the same 400 ms guard.
- Not shown while locked, with an overlay or recording; the link stays "unassigned". The lock card then gains a line "新 Mac 待分配" (new Mac to assign).
- With profiles on, if the plugged-in cpad is too old (no `host_id` in the hello, i.e. older than v0.4.1), or the `host_id` is shorter than 16 characters, it only says "这台 Mac 的 cpad 太旧，分不了档案，什么都不交。升级 cpad 后再插。` 知道了" (This Mac's cpad is too old for profiles; nothing is handed over. Upgrade cpad and plug in again. ` OK).

### 5.3 Confirming a binding

```
把「Work Mac」设为 公司？
以后插上它：自动切到 公司，
只把 公司 的待传和录音交给它。
家 的待传（↑3 录2）留在设备上。
Enter 确定                     ` 取消
```

In English: "Set 'Work Mac' as Work? When plugged in from now on: switch to Work automatically, and only hand Work's waiting items and recordings to it. Home's waiting items (↑3, 2 recordings) stay on the device. Enter confirm, ` cancel."

- Key presses within 400 ms of opening the confirmation are ignored, as with the existing confirmations [src docs/04 §8.1].
- **After confirming, the current profile switches to the chosen one at once** (§12.4 step 4 relies on this), with the notice "已切到 公司" (Switched to Work). If a request happens to be in flight, it waits for it to end per §3.3.
- On the first binding, profile mode is turned on at the same time (§9).
- This is "widening": this profile's things start being handed over only after the device sends a `pong` carrying the new `prof` (≤5 s) (§3.7).

### 5.4 Waiting items from before the upgrade

After a binding is confirmed, if the device still has "undecided" things, it asks next:

```
设备上有没分档案的待传：↑12 录3
（升级前、或第一次分档案前记的）
▸先留着（以后在 系统→档案 里决定）
 交给 公司（这台 Mac）
;.选 Enter定（要确认）
```

In English: "The device has waiting items with no profile: ↑12, 3 recordings (recorded before the upgrade or before profiles were first used). ▸ Keep for now (decide later in System → Profiles) / Give to Work (this Mac)."

- "Give to" needs one more confirmation. After it, the "undecided" things as a whole count as that profile (one byte in NVS, no file changed); this is also "widening", and upload starts after the next `pong`.
- They can only be handed over as a whole, not picked one by one: picking would mean showing each item's content on the device, and v0.8 does not change profiles of records (§15 item 4).

### 5.5 Status bar, lock screen, and the Controls page

- **Status bar**: one character to the left of the clock, in the profile's color, for example `… ※k3 公 13:42 83%`; not shown when profiles are off. When connected to an unassigned Mac, the link area on the left shows `◇ 未分配` (◇ Unassigned, Warn color), a new value of `link_glyph` [src status_bar.cpp].
- **↑N and 录N (recordings N) count the current profile**; otherwise, at work, the 3 home items would keep ↑ at 3, looking as if sync were broken. But **when another profile still has waiting items, a `+` follows the number** (`↑0+`). SECURITY.md says ↑N "lets you know how much plain text is still on the device", and the T6 mitigation also relies on you seeing at a glance whether home still has untransferred items; counting only the current profile would hide that. Per-profile counts are in System → Profiles.
- **Lock card**: the date line gains the profile character, for example "10/21 周二 · 电 83% · 公" (10/21 Tue · battery 83% · Work); counts show **the total over all profiles** (when locked, what matters most is "how much plain text is still on the device").
- **The Controls page** gains a read-only row "档案  公司（系统页里换）" (Profile Work (change in System)). It is not an executable row because the Opt+M it would replay already appears on the "Go to" page, and the menu tests require every global-layer binding to appear exactly once on the "Go to" or "Controls" page [src docs/13 §6.2; test/core/test_menu_model/test_main.cpp:79 `test_every_global_binding_appears_once_on_goto_or_control`].

### 5.6 System (Opt+M) → Profiles

Today the System page is 9 read-only rows [src modules/system/system_logic.h]. v0.8 adds a second tab, switched with `,` `/` (the list layer already supports it):

```
系统  设备 [档案]
▸当前    公司 ·「工作 MBP」   Enter换
 待传    家 ↑3 录2 · 公司 ↑0 录0
 这台Mac 未分配「Lin 的 MBP」  Enter
 Mac     公司「工作 MBP」       Enter
 Mac     家「家里 iMac」         Enter
 Mac     都不是「会议室 iMac」   Enter
 会议录音 家 允许 · 公司 允许   Enter
 未定    ↑0 录0
 关闭分档案（待传都清零才能用）
;.选 Enter改 ,/换页 `返回  Tab菜单
```

In English: "System  Device [Profiles] / ▸ Current: Work · 'Work MBP', Enter to switch / Waiting: Home ↑3 2 rec · Work ↑0 0 rec / This Mac: unassigned 'Lin's MBP' / Mac: Work 'Work MBP' / Mac: Home 'Home iMac' / Mac: Neither 'Meeting room iMac' / Meeting recording: Home allowed · Work allowed / Undecided ↑0 0 rec / Turn off profiles (only when all waiting items are cleared)."

| Row | After pressing Enter |
|---|---|
| Current | Opens the switch sheet (§5.7) |
| This Mac (unassigned) | Only shown while connected to an unassigned Mac (including one where you pressed "Later" or `` ` ``): shows the choice sheet of §5.2 |
| Mac (Home / Work / Neither) | "忘掉这台 Mac？以后插上会重新问。" (Forget this Mac? It will ask again when plugged in.), needs a confirmation. "Neither" Macs are listed too, so they can be forgotten as well |
| Meeting recording | Choose a profile first, then "Allow" or "Off", needs a confirmation |
| Undecided | As in §5.4: hand it as a whole to one profile, or keep it |
| Turn off profiles | Can only be chosen when **every profile-tagged waiting count is 0** (↑ and recordings of each profile, plus undecided); otherwise the row says "待传都清零才能用" (only when all waiting items are cleared) and Enter does nothing. Once chosen it needs a confirmation: "关掉以后，所有 Mac 都能拿到设备上的东西。" (Once off, every Mac can take what is on the device.) After confirming, the profile table in NVS and the `PROFON` flag on every storage are cleared, returning to "No profiles" (§9) |

This page only shows counts and Mac names, never the content of any item.

### 5.7 Switching by hand

```
切到哪个档案？现在：家
▸家
 公司
之后记的都标这个档案，
只交给这个档案的 Mac。
;.选 Enter定（要确认）     ` 取消
```

In English: "Switch to which profile? Now: Home. ▸ Home / Work. Everything recorded afterwards is tagged with this profile and only handed to this profile's Mac."

- After confirming, if the connected Mac does not serve the new profile, it says "已切到 公司。这台 Mac 是 家，公司 的东西不会交给它。" (Switched to Work. This Mac is Home; Work's things will not be handed to it.) From then on the project page, "waiting for you" and the recording history are shown as offline, using Work's cache (§3.6).
- No switching while recording or with a request in flight; it says "录音中不能换档案" (cannot switch profile while recording) / "等 Mac 回完再换" (wait for the Mac to reply, then switch).
- A manual switch stays in effect until you plug the cable in again or plug into another Mac (§3.3).

### 5.8 Notices

| Situation | Notice |
|---|---|
| A bound Mac is plugged in and the profile changes | 已切到 公司（插着「工作 MBP」） (Switched to Work (plugged into "Work MBP")) |
| Saving while connected to an unassigned Mac | 已存（这台 Mac 未分配，先不交） (Saved (this Mac is unassigned; not handed over yet)) |
| Saving when the current profile does not belong to this Mac | 已存（公司 的，等插上 公司 的 Mac） (Saved (Work's; waiting for Work's Mac)) |
| Opt+S when the current profile does not belong to this Mac | 不能在这台 Mac 上开 session，已存成排队项 (Cannot start a session on this Mac; saved as a queue item) |
| Opt+R when this profile has meeting recording off | 公司 档案关了会议录音（系统→档案） (Meeting recording is off for the Work profile (System → Profiles)) |
| Trying to switch profile while recording | 录音中不能换档案 (Cannot switch profile while recording) |
| Trying to switch profile with a request in flight | 等 Mac 回完再换 (Wait for the Mac to reply, then switch) |

### 5.9 Keys

No new keys; only existing ones are used: Opt+M, `,` `/` (switch tabs), `;` `.` (move up and down), Enter, `` ` ``.

Choice sheets and confirmations are overlays; opening the Tab menu on top of them shows only the "This page" and "Controls" pages [src docs/13 §6.1].

Choice sheets reuse v0.6's choice list `lib/ui/pick_list` and the existing confirmation (docs/17 §10.1; if v0.5 has already extracted a shared list component, it is the same one [src docs/13 §10.4]). But here **no numbers are shown and digits are not accepted**: docs/17 only lets digits mean "item N" on the agent's choice page, and states that this is the only exception (docs/17 §2 principle 7); the profile choice sheet does not open a second exception. Unlike docs/17's choice page, the cursor here **starts on the safest row** ("Later", "Keep for now"): that row does nothing, so preselecting it prevents mistakes.

## 6. WiFi (P8-W, not done in v0.8)

The device has no WiFi or BLE code today; ADR 0001 states that IDF 4.4 is already EOL and the toolchain must be re-evaluated before adding wireless [src adr/0001-toolchain.md:77]; the WiFi stack might also push free heap below the 150 KB floor [inf; src docs/13 §10.3]. So v0.8 does not do it. If it is done later, the direction is "passive scan only, suggest only, never switch automatically" (both SSID and BSSID can be forged), and it must first pass the gate **G-WIFI** (toolchain re-evaluation, heap, no transmission, no effect on the USB link); details will be written then. WiFi signals for one laptop (form B) are evaluated together with P8-B1.

## 7. Mac side: cpad, MCP, export

### 7.1 cpad rules (P8-D1)

| # | Rule |
|---|---|
| D1 | On every `hello` / `pong` from the device, record its `prof`. When a new profile appears in `ok`, append it to `$CPA_HOME/profile.json` (mode 0600, **append only**: records "which profiles this Mac has received, and when first", e.g. `{"version":1,"served":[{"prof":"work","since":"…"}],"devices":{"<dev>":{"ever_ok":true}}}`), and record an event `prof.seen{dev, ok, legacy}` |
| D2 | When the device reports `prof`: the `prof` of an `outbox.item` must be in `ok`; an item without `prof` is accepted only when `legacy` has a value, and is recorded as `legacy`. If neither holds (a device bug, or cpad has not yet seen the new `prof`): **write nothing and do not reply `rejected`**, record `prof.mismatch{kind:"item"}`, then **send the host `hello` again**. By the existing invariants, this clears in-flight items and returns them to waiting, and the device's `hello` reply carries the latest `prof` |
| D3 | Recordings: for segments in `file.items` whose `prof` is not in `ok`, do not `get` or `del`; record `prof.mismatch{kind:"file", n}` and likewise handshake again once |
| D4 | 3 consecutive D2 / D3 mismatches in one connection: stop accepting items and recordings from this device (no `ack`, no pulls), and `cpa status` and the log warn "device profile state is inconsistent". The things on the device stay as they are and never go to `bad/` |
| D5 | `item.received` records carry `prof`. The `manifest.json` cpad writes gains the optional field `prof`; recording contract v1 allows new fields [src "forward compatibility" in protocol/recording/v1/README.md] |
| D6 | When `ok` is empty (the device is asking you, or this Mac was set to "neither"), push no snapshots; `cpa status` shows "在设备上给这台 Mac 选档案" (choose a profile for this Mac on the device). The device echoes `revs` per §3.5 item 6, so an old cpad does not keep pushing either |
| D7 | `cpa status` adds a line `profile: work (公司)  device now: work`. When the device's current profile does not belong to this Mac, it shows `device now: other`, because the device does not say which one (§10.3) |
| D8 | When the same `dev` once reported a **non-empty** `ok` on this Mac and now reports no `prof` (firmware downgraded, or you fully erased the device and started over): warn "device no longer separates profiles" in `cpa status` and the log. Having reported only `ok:[]` (for example while the choice sheet was open, or after you chose "No profiles") does not count. cpad cannot stop this; it only warns |
| D9 | v0.6.1 device approvals: when the device reports `prof.act` as `null` (the current profile does not belong to this Mac, so the device will not send `choice.get`), `decide` returns null at once (`reason:"other_profile"`), so the hook does not wait 100 s for nothing (docs/17 §5.1) |
| D10 | (2026-10-22, docs/25 §4.5, R1-B) Every custom destination carries `profiles:[…]`: `cpa dest add --profile X` (repeatable); without it, "any profile". Once `profile.json` exists and `served` has more than one entry, it is required. Pre-check `other_profile`: if the destination has `profiles` and the item's `prof` is not in it (or unknown), fall back to the daily note; every `dest.*` audit line carries `prof`. The canary test of §12 adds one case: home strings never appear in the links or clipboard writes of work destinations |
| D10 | `host_hw` and `cpa host reset` (§4.1) |

Old data without a profile tag (notes, recordings and events from before v0.8) counts as this Mac's **default profile**: the first entry of `served` in `profile.json` [src docs/15 §3.11].

### 7.2 MCP and export (phase D of docs/15, P8-D2)

- `cpa mcp serve --profile work`: only returns data tagged `work`, plus old untagged data when the default profile is `work`. **The filter only looks at the tag on the data**, not at the device's current state: the device forgetting this Mac, asking you, or being reflashed never stops the AI client from connecting next time.
- `--profile X` when X never appears in `served` of `profile.json` (for example `--profile home` on the work Mac): refuse to start, exit code 2, stderr "profile home was never served on this Mac". It does not silently return an empty list, so you do not think nothing was recorded at home.
- `profile.json` does not exist (this Mac has never received anything with a profile): `--profile` always exits with code 2, stderr "no profile has been served on this Mac yet".
- **Daily notes are not filtered**: note lines have no tag. In form A, one Mac's notes folder already belongs to only one profile, so it is returned whole; the tool description states "notes are not filtered by profile".
- Once `mcp.json` has `"profile_required": true`, starting without `--profile` is refused [src docs/15 §3.11]. The server **never** switches profile based on a name the client reports.
- Events written by docs/15 phase B write tools (`add_note`, `add_todo`, `queue_add`) carry `prof`: the single entry of `served` when there is exactly one; omitted when there are none or more than one (counts as the default profile).
- The device counts returned by `get_status` already only include what this Mac may take (§3.5 item 3), so they reveal nothing about how much another profile holds.
- `cpa export --profile work` likewise, with `"profile":"work"` added to `CPA-EXPORT.json`; exit code 2 too when `profile.json` does not exist. `host_id` is still not exported [src docs/15 §2.4].
- **Before v0.8 none of these options exist**, consistent with docs/15.

### 7.3 One Mac, two profiles (form B, P8-B1, waiting for your confirmation)

Only the work to be done is listed here, without detail:
- The device allows one Mac to be bound to two profiles, provided cpad reports `caps.prof` in its hello;
- cpad's `config.json` adds `profiles: {home: {notes_dir}, work: {notes_dir}}`, writing into different notes and recording folders by the item's `prof`;
- `rec.list` adds `prof`, listing only the current profile's recording history;
- The project table and sessions are still shared by this Mac;
- Where the profile signal comes from (manual, the Mac's WiFi) is evaluated then.

**This form is not separate for security**: same macOS user, same disk, separate only for organization. The README must say so.

## 8. Threat model

### 8.1 What must hold

**A "work" Mac can never get the content of any "home" capture**, whether typed, spoken or a meeting recording, including what is still on the SD card and untransferred; and the other way round. **Nor any count field computed from another profile** (`pending`, `files.n`, `files.rev`, `revs.*`). The small metadata leaks are in §8.3 T14.

These guarantees **only hold for release firmware**. Test firmware has `debug.key`, with which any connected host (unassigned ones included) can press the choice sheet for you and bind itself; CI and the release workflow already check that release firmware has no `debug.*` [src protocol/README.md item 16].

### 8.2 Three levels

| Level | Protects against | How | v0.8 |
|---|---|---|---|
| **L1 routing** | An honest cpad taking the wrong thing: the wrong Mac plugged in, a new Mac, a `$CPA_HOME` copied from elsewhere | The device filters by profile (§3.5); derived items follow their source; unknown Macs get nothing by default; `host_id` serves as a secret, the device stores only its hash and only accepts ≥16 characters, the Mac side ties it to the hardware; cpad checks again | **Done** |
| **L2 pairing key** | Someone impersonating the host on the line | NVS key + HMAC challenge (docs/05 §8) | Not done. Reasons in §4.3; done together with wireless |
| **L3 encryption at rest** | A Mac set on reading the device (reading the whole flash with esptool, or flashing a firmware to read the SD card); a lost device; the SD card pulled out | Each profile's items and recordings are encrypted with that Mac's public key before writing to storage. The protocol reserved the name `enc` for this long ago [src protocol/README.md item 14]; the algorithm is left to the P8-E design | Not done. Listed separately as P8-E, your decision (§15 item 2) |

**Why L1 cannot stop the kind of adversary L3 protects against**: the device has neither flash encryption nor Secure Boot turned on, because both are one-way, and once on, the community flashing tools no longer work [src docs/04 S11; SECURITY.md]. Today's USB serial supports auto-reset into download mode, which is how `pio run -t upload` flashes [src docs/13 §9.6; adr/0001-toolchain.md:100]. So a determined Mac can read LittleFS and NVS without you pressing any key, and by flashing a firmware it can read the SD card [inf]. This has nothing to do with profiles; it is because the device itself is plain-text storage.

**The cost of L3** (which is why it is off by default):
- It must first pass the gate G-ENC: the CPU and heap cost of encrypting 32 KB per second while recording [todo]; whether random-number entropy is enough on the ESP32-S3 with the radio off [todo].
- The semantics of `file.*` change: ciphertext is transferred, and cpad decrypts.
- **v0.5's playback on the device does not work for encrypted recordings**, because the device has no private key.
- The device can no longer see the text it stored either.

### 8.3 Threat table

| # | Threat | Response | Residual risk |
|---|---|---|---|
| T1 | Home waiting items and recordings pulled by the work cpad | §3.5 filtering, counts filtered too; cpad checks again (§7.1 D2, D3) | — |
| T2 | Recording with the wrong profile (forgot to switch) | The status bar always shows the profile character; plugging into a bound Mac switches automatically; no switch with an overlay, while recording or with a request in flight | v0.8 cannot change a record's profile afterwards (§15 item 4) |
| T3 | A new Mac, a friend's Mac | Default deny; the choice sheet's cursor on "Later", 400 ms guard; choosing "Neither" refuses it permanently | — |
| T4 | A Mac impersonating "the home one" | `host_id` is a 64-bit random number, mode 0600, never logged; the device stores only its hash, never echoes it, and only accepts ≥16 characters; the Mac side ties it to the hardware, so copying `$CPA_HOME` is useless | Whoever gets the home Mac can impersonate it, but that person already has the home archive. Before cpad binds to a board's serial number it sends `host_id` to any 303A:1001 serial port, so plugging a strange dev board into the home Mac would get it (§4.3) |
| T5 | The work Mac changes its `host_label` to "Home iMac" to trick you into choosing wrongly | The choice sheet says "the name is what it reports itself"; and when binding, you are physically at that Mac | If you choose wrongly, things go to the wrong place, and **what was handed over cannot be taken back** (§9). The confirmation is the last guard |
| T6 | A Mac set on reading the device (controlled by IT, or with malware) | L1 cannot stop it. Mitigation: synced things do not stay on the device (recordings deleted once pulled, outbox deleted on ack); the status bar's `↑0+` and "Home ↑3 2 rec" in System → Profiles remind you to plug in at home before going to work and clear it | 👤 §15 item 2. Real protection needs P8-E |
| T7 | Lost device, SD card pulled out | Plain text, as today [src SECURITY.md] | Same |
| T8 | Through Launcher: browsing the SD card (it has its own SD file manager and WebUI); other firmware Launcher installs shares our NVS and could change bindings [src docs/gates/launcher.md:50, 54] | Outside cpa's control; documented in the README [inf] | Only happens through your own actions |
| T9 | NVS erased (`erase_region 0x9000`, or erased by Launcher) and falling back to "give to anyone" | Principle 8: the `PROFON` flag is written in NVS and on **every mounted storage** (internal cpafs, SD card); the boot scans of outbox **and** recordings can both turn profile mode on; nothing is handed over until both scans are done. After NVS is lost, the current profile is "undecided", new records are undecided too, and nothing is given until you bind again [src docs/gates/launcher.md:114] | Erasing NVS **plus** pulling or swapping the card (with no cpafs, e.g. under Launcher) equals a reset. Only happens through your own actions |
| T10 | Flashing back firmware older than v0.8 | Old firmware does not know `prof` and gives to anyone. cpad warns (§7.1 D8); the release notes say "a downgrade loses isolation" | Only happens through your own actions |
| T11 | A reply or failure fallback tagged with the wrong profile, its text reaching the other Mac and starting a new session there | Principle 4 and the three safeguards of §3.6: slices belong only to the current profile, derived items take `src`, no switching with a request in flight | — |
| T12 | An AI client on the work Mac reading home things through MCP | Home things are not on the work Mac at all; `--profile` and `profile_required` filter recordings, captures and the queue again | Daily notes are not filtered by profile (§7.2); no effect in form A |
| T13 | One Mac, two profiles (form B) | Separate only for organization | Documented in the README |
| T14 | Metadata leaks | Count fields are all computed per profile (§3.5) | The sequence of item IDs (MonoId) is shared by the outbox and recordings [src runtime_outbox.cpp:38], so the work Mac seeing gaps in IDs can infer how many items another profile recorded; while another profile is recording, `files.live` is true and `file.get` replies `E_BUSY live` [src file_req.h:3]. No separate per-profile ID sequence for this: that is scope creep |

### 8.4 What goes into SECURITY.md

- v0.8's trust boundary: **each Mac only gets the profile bound to it**; an unknown Mac gets nothing. Only for release firmware.
- The device is still plain-text storage. A Mac that can read the flash with esptool or flash firmware can read everything on the device that **has not been synced yet**, regardless of profile. Before going to a Mac you do not trust, sync "home" waiting items to zero at home first (the status bar's `↑0+` reminds you), or wait for P8-E.
- Gaps in item IDs and "recording in progress" reveal a little metadata to the other Mac.
- Downgraded firmware loses isolation; erasing NVS plus swapping the card equals a reset.

## 9. Migration: from today's "one profile"

| Phase | Device | cpad |
|---|---|---|
| Just upgraded to v0.8, no cpad with `host_id` seen yet | Profile mode off, same as v0.7: no `prof`, gives to anyone; the project cache still uses `/cfg/projects.json` | The device reports no `prof`, and cpad behaves as before |
| First cpad with `host_id` seen | Shows the choice sheet of §5.1. **Delivery pauses while it is open**; the wire treats the Mac as "unassigned" | Receives `prof{ok:[],ask:true}`, pushes no snapshots |
| "Later" chosen | Nothing handed over this time; asks again next plug-in | Same |
| "No profiles" chosen and confirmed | Records "declined", never asks again, everything as before | `prof` no longer appears; D8 not triggered |
| "Home" or "Work" chosen and confirmed | Profile mode on: the profile table written in NVS, `PROFON` written in NVS and on every mounted storage; the current profile switches to the chosen one; everything recorded afterwards carries `prof`. The old `/cfg/projects.json` is no longer used; this Mac will push a new one right away, stored under `/cfg/pN/` | Receives `prof` from the next `pong`, writes `profile.json` |
| Old waiting items on the device | Shows §5.4: keep "undecided", or hand it over as a whole | Once handed over, accepted as `legacy` |
| Another Mac plugged in | Shows the choice sheet of §5.2 | — |
| Wanting to turn it off | After every tagged waiting item is cleared, System → Profiles → "Turn off profiles" (§5.6) | `prof` no longer appears |

- **Why turning off requires clearing first**: turning off amounts to handing tagged things to any Mac. Once cleared, nothing tagged is left on the device, so turning off leaks nothing. No need to run a full esptool erase from a Mac.
- If you only use cpads older than v0.4.1 (no `host_id`), profile mode can never turn on, and everything stays as before.
- Old data on the Mac side counts as the default profile (end of §7.1).
- **What was handed over cannot be taken back**: rebinding after a wrong binding leaves what was handed over before still on that Mac. Things handed to the wrong Mac before v0.8 do not move back automatically either: if you used to plug into both Macs, the work Mac's `~/CardputerNotes` may already hold home things, which you need to clean up yourself with `cpa rec ls` and the notes.

## 10. Protocol additions (additions only, `v` stays 0)

### 10.1 Overview

| Change | Direction | Content | Compatibility |
|---|---|---|---|
| Device `hello` (reply) and `pong` add `prof?` | D→H | `{act?, ok, ask?, legacy?}`, rules in §10.3 | Old cpads ignore extra fields [src `obj()` in validate.py]. Not sent when profile mode is off and no choice sheet is open |
| `outbox.item` adds `prof?` | D→H | Top-level field, alongside `kind`; both `note` and `queue` may carry it | Old cpads ignore it. Not sent when profile mode is off |
| Each item of `file.items[]` adds `prof?` | D→H | Every segment carries it | Same |
| `debug.state` (reply) adds `prof?` | D→H | `{mode, act, hosts, legacy}`, for HIL only | Only in test firmware |
| Recording contract v1 `manifest.json` adds `prof?` | Written by cpad | Profile key | WhisperWoof ignores unknown fields |
| `limits.json` adds `prof_key_pattern`, `prof_ok_max` (4), `prof_host_id_min` (16) | — | Read by both ends | — |

- **No new message types**: binding and switching are local operations on the device, and cpad learns the result from `hello` / `pong`.
- **No new error codes**: other profiles' segments use `E_UNKNOWN_ITEM`; on a mismatch cpad replies no error and only handshakes again (§7.1 D2).
- **The host `hello` gains no fields**: the `host_id` and `host_label` added in v0.4.1 are enough [src hello.schema.json]. `caps.prof` is kept for P8-B1.
- **Semantic changes (fields unchanged)**: in profile mode, `files.rev` and `revs.*` are reported per §3.5 items 2 and 6.

### 10.2 Examples

```jsonl
{"v":0,"t":"hello","id":"h1","role":"host","app":"cpad/0.8.0","agent":"ok","caps":{"spawn":true,"reply":false,"recs":true,"todo":true},"host_id":"5f0c2a9e41d7b3c8","host_label":"工作 MBP"}
{"v":0,"t":"hello","id":"d1","re":"h1","role":"device","fw":"cpa-fw/0.8.0","dev":"020000000001","boot":"3QZK1M7-0000D","up_ms":5120,"rst":"poweron","time":true,"pending":1,"bat_mv":4020,"revs":{"projects":0,"sessions":0,"queue":0},"files":{"n":0,"rev":7,"live":false},"feats":["choose"],"prof":{"act":"work","ok":["work"],"legacy":null}}
{"v":0,"t":"outbox.item","id":"3QZK1M7-0000D-0003","kind":"note","prof":"work","at":{"up_ms":98000,"wall_ms":1792440045000},"body":{"text":"ask Lin about the Q4 deck","project":"deck","dest":"todo"}}
{"v":0,"t":"pong","id":"d9","re":"h8","up_ms":40120,"pending":0,"revs":{"projects":2918374651,"sessions":4,"queue":2},"files":{"n":0,"rev":1,"live":false},"prof":{"act":null,"ok":[],"ask":true}}
```

The situation in the last line: the device is connected to a Mac not yet assigned and is asking you which profile it belongs to. The device actually has 3 waiting items, but `pending` reports 0; `revs` echoes what this Mac pushed this time, and `files.rev` does not change with another profile's recordings.

### 10.3 Field rules

| Field | Rule |
|---|---|
| `prof.ok` | Required. An array of keys, 0–`prof_ok_max` (4) entries, no duplicates; a v0.8 device only sends 0 or 1 entry. **Lists only this Mac's own bindings**; widening waits for it to appear here first (§3.7) |
| `prof.act` | The current profile's key, **given only when it is in `ok`**, otherwise `null`. So the work Mac never learns "the device is in the home profile now"; a v0.6.1 cpad also uses it to decide whether the device can approve for it (§7.1 D9) |
| `prof.ask` | `true` means the device is showing a choice sheet for this Mac |
| `prof.legacy` | When the "undecided" things have been assigned to a profile and that profile is in `ok`, its key. cpad uses it to accept old items without `prof` |
| `prof` (on items and segments) | Key, format `^[a-z][a-z0-9-]{0,11}$`. Every segment the device sends carries it: for old `.M` files without it, it is filled in from `legacy` before encoding (`file.items` is encoded on the fly anyway). Old outbox files are sent as they are, without `prof` (the file content is the line on the wire [src runtime_outbox.cpp:95-96]), and cpad recognizes them through `legacy` |
| `debug.state.prof` | `mode`: `off` / `on` / `declined`; `act`: key or `null`; `hosts`: number of bindings; `legacy`: key or `null`. Test build only, for HIL assertions |

### 10.4 Line lengths

Computed with `json.dumps` in compact form with `ensure_ascii=False`, keys at the longest 12 characters, `prof.ok` with 4 entries [calc]:

| Line | Now | With the additions | Notes |
|---|---|---|---|
| Device `hello`, every field at its maximum (including v0.5's `revs.dests`, `revs.pending`) | 463 B | about 726 B (docs/17's `feats` 130 B + `prof` 133 B) | Lines the device sends are only bound by the 2048 B total limit |
| Worst `pong` | 238 B (v0.4.1), about 278 B after v0.5's two `revs` | about 410 B | Same |
| `outbox.item` note: 500 `"`, with a 24 B `dest`, `how`, `live` | 1223 B [src docs/14 §7.6] | 1242 B | |
| `outbox.item` queue: 500 `"` | — | 1238 B | |
| `file.items` with 4 entries, every optional field at its maximum, no `dest` | 1988 B [src test_worst_lines.py] | **2076 B, over the limit** | In profile mode every entry carries `prof`, so the 4-entry worst case is always over. The device lists one fewer entry via `file_fit_page` and sets `more:true`; v0.4.1's long `dest` already set this precedent [src protocol/README.md item 20] |
| Same, each entry with `dest:"todo"` | 2044 B | 2132 B, over the limit | Same |
| `file.items` with 3 entries, each with a 24 B `dest` and `prof` | — | 1687 B; about 1726 B with v0.5's `how` | Fits |

- These lines all go into `protocol/tools/test_worst_lines.py`. The existing `(worst_file_items, 1988)` entry becomes "1988 without `prof`; with `prof`, 4 entries over the limit, 3 entries within".
- These numbers were computed by adding fields to the v0.4.1 constructors; after v0.5 and v0.6 merge, the fields will change again, so **P8-P must recompute on the merged schemas**, and the numbers of that time are authoritative.

### 10.5 Mixing old and new

| Combination | Result |
|---|---|
| v0.8 firmware + a cpad without `host_id` (older than v0.4.1) | With profile mode off: as before. With it on: this Mac gets nothing, and the device asks you to upgrade cpad |
| v0.8 firmware + a v0.4.1–v0.7 cpad | Can be bound (as long as it has a `host_id` of ≥16 characters), and the device filters as usual; cpad does not record or check `prof`, and cannot use `--profile`. `revs` are echoed per §3.5 item 6, so it does not keep pushing |
| v0.8 cpad + firmware older than v0.7 | No `prof`, cpad works as before. If this device once reported a non-empty `ok` on this Mac, it warns (§7.1 D8) |
| A v0.4.1–v0.7 cpad reading a `$CPA_HOME` written by v0.8 | The `host_id` file format is unchanged, `host_hw` is ignored, and `host_id` is not regenerated |
| v0.8 on both ends | All behavior described in this doc |

### 10.6 Merge constraints

- No new schema files, so the firmware `test_codec_vectors` assertion "number of schemas = `kMsgTypeCount + 1`" is not affected [src docs/14 §7.8].
- But the firmware vector tests require "decode then re-encode, equal field by field" [src test/core/test_codec_vectors/test_main.cpp:1-5; the note under item 18 of protocol/README.md]. So the new valid cases with `prof` must be merged into main **in the same batch** as the firmware codec change that understands `prof` (P8-F0).
- `v040_cases.sha256` may only be appended to [src the "validation" section of protocol/README.md].
- **New flows go in `flows/prof/`**: the existing `core/test_link_flows` replays `flows/` only with the v0.2 link core, which cannot filter by profile [src test/core/test_link_flows]; and flows where the host sends `file.*` must live in `flows/rec/`. So `flows/prof/` is replayed by a Runtime-level firmware test (`test_profile_flows`) and the daemon's `tests/contract`; a new tool `protocol/tools/prof_flows.py` checks the invariant "an item or segment with `prof ∉ ok(H)` never appears on the wire", registered in the required flow list of `check_vectors.FLOW_FOLDERS`. The expectation keys add `device.outbox_pending_visible` (the waiting count H can see); the existing `device.outbox_pending` stays the total.
- Order as usual: first change 05 §6, then the schemas and vectors, finally the implementations on both ends.

## 11. Firmware implementation and budgets

| Where | Change | Budget |
|---|---|---|
| New pure-function library `lib/core/profile.*` | `ProfileTable`: profiles, bindings, current profile, the host of the last handshake, the assignment of undecided, the meeting recording switches. `bind`, `forget`, `lookup(digest)`, `allowed_mask(host)`, `on_hello(digest, plugged)`, `switch_to`, `narrow` / `widen_after_pong` all return new values without changing the original. The hash is computed by the Runtime and passed in (§4.2). The NVS blob encoding is versioned; corrupt data returns an empty table with profile mode on (principle 8) | Table about 380 B, in the Runtime's static area, not in AppState |
| `AppState` | A new slice `ProfileInfo{mode, act, host_mask, host_state, flags}` of about 8 B; each of the 5 slices from the host gains a 1-byte `src`; `AgentReqInfo` gains a 1-byte `src` | About +14 B. About 3.5 KB now, limit 4 KB, about 600 B headroom [src docs/13 §10.3] |
| Shell switching logic | On a profile switch, clear the session, queue and pending slices, load the new profile's project and destination caches, clear docs/17's choice page and to-do page; defer while a request is in flight | — |
| `OutboxCore` | A parallel array `uint8_t prof[200]`; `outbox_tick` gains a `mask` parameter and skips disallowed items; `narrow()` returns in-flight items that are no longer allowed to waiting | +200 B. `OutboxStep` grows from about 2.9 KB to about 3.1 KB, and it is on the 16 KB loop stack [src runtime.h]; run `scripts/stack_budget.py` to confirm [todo]. If it does not fit, compress to 2 bits per item (50 B) |
| `RecIndex` | A parallel array `uint8_t prof_[256]`; `rev_` becomes one per profile (5 × 4 B). Not put into `Entry`: with alignment that would grow each entry from 16 B to 20 B, 1 KB more in all [src mono_id.h: MonoId 12 B] | +276 B, part of the heap allocated once at boot |
| `rec_meta`, `file_req` | `.M` gains `prof` (`.M` limit 512 B, about 15 B more [todo: measure the headroom]); `file_plan_list` takes a `mask` parameter; for disallowed segments, `get` and `del` reply as "not on the card" | — |
| Boot scan | When the outbox or recording scan reads anything with `prof`, profile mode is set on; likewise if `PROFON` exists anywhere in NVS or on any mounted storage (principle 8). **Nothing is handed over until both scans are done** | — |
| Caches | `/cfg/pN/projects.json`, `/cfg/pN/dests.json`; read once on a profile switch | Only read on a switch |
| Shell UI | Choice sheets and confirmations (reusing v0.6's `lib/ui/pick_list`, digit choice off, cursor preset on the safe row, 400 ms guard); the "unassigned" link state; switches deferred until overlays close, recording ends and requests reply | Choice sheet state lives in the Shell, not AppState |
| System app | A second tab "Profiles" (with "This Mac (unassigned)", "Neither" entries, "Turn off profiles") | App object no more than 1 KB |
| Status bar, lock screen, menu Controls page | See §5.5 | Newly used glyphs (「」◇ 公 家) must satisfy `missing_glyphs == 0` [todo] |

- **No heap** (the 276 B of `RecIndex` is allocated once at boot).
- **No blocking**: NVS is only written on binding, forgetting, switching, a handshake with a different Mac, or changing a meeting recording switch, none of which happen while recording (no profile switch while recording).
- No WiFi code at all before P8-W.

## 12. Tests (always tests first: RED → GREEN → REFACTOR)

### 12.1 Firmware native (Unity)

| Test | Assertions written first |
|---|---|
| `test_profile_table` | Bind, forget, look up by hash; the same Mac cannot be bound to two profiles; a `host_id` shorter than 16 characters is refused; refused once 8 entries (including "neither") are stored; the mask of "neither" is always empty; the NVS blob round-trips equal; a corrupt or wrong-version blob gives an empty table with profile mode on; no function changes the original value |
| `test_profile_switch` | Plugging (`plugged`) into a bound Mac switches to its profile; **after a manual switch, the same Mac sending hello again leaves the current profile unchanged**; a handshake with a different Mac switches; after boot, the current profile and "the last host" are restored; deferred with an overlay, while recording, or with a request in flight, and switched afterwards; an unbound Mac does not trigger a switch; a manual switch needs confirmation, with the 400 ms guard |
| `test_profile_src` | Switching clears slices from the host; H's snapshot does not enter AppState when the current profile is not in ok(H); **derived feedback and queue items take the slice's `src`, not the current profile** (also with an artificially constructed stale slice); a switch is deferred while an agent request is in flight; queue items saved by the failure fallback and for "unknown result" take `AgentReqInfo.src` |
| `test_profile_bind_timing` | Forgetting or rebinding the connected Mac: narrows at once, in-flight items return to waiting; binding or handing over undecided: not a single item sent before a `pong` with the new `prof` |
| `test_profile_first_sheet` | The first choice sheet's cursor is on "Later"; key presses within 400 ms have no effect; not shown while recording, with an overlay or locked; while open, `hello` / `pong` carry `prof{ok:[],ask:true}`, counts are 0, nothing is handed over |
| `test_outbox_prof` | `outbox_tick` with `mask`: disallowed items are never sent; allowed items still go in ID order, at most 4 in flight; returned to waiting on disconnect; undecided items not sent before the `legacy` assignment |
| `test_file_req_prof` | `file_plan_list` only lists allowed segments; for other profiles' segments, `get` replies `E_UNKNOWN_ITEM` and `del` replies `ack` without deleting; `files.n` only counts allowed ones; **`files.rev` does not change when another profile's segment is committed or deleted**; old `.M` without `prof` count as undecided |
| `test_profile_revs` | With ok(H) not empty, report the rev of H's profile cache; when empty, echo the rev H pushed this time; never report another profile's rev |
| `test_profon` | With only recordings tagged (outbox empty), profile mode stays on after erasing NVS; `PROFON` only on SD, only on cpafs, or only in NVS each turns it on; nothing is sent before both boot scans are done; after NVS is lost the current profile is undecided, and new records are undecided too |
| `test_rec_meta_prof` | `.M` with `prof` round-trips equal; with the longest key `.M` still does not exceed 512 B; every segment of a meeting copies segment 0's `prof` |
| `test_profile_flows` (Runtime + two scripted fake hosts, replaying `flows/prof/`) | The home Mac (A), the work Mac (B), an unknown Mac (C) and a Mac without `host_id` (D) are plugged and unplugged in turn, with reboots in between. **Check every line the device sends**: none contains an item or segment with `prof ∉ ok(current host)`; the `pending` and `files.n` received by C and D are always 0 |
| Property test (fixed seed, 1000 iterations) | Random operation sequences from {record one item in the current profile, record a memo, reply to a session, start a session (success / failure / unknown), plug A/B/C/D, the same Mac sending hello again, unplug, manual switch, bind, forget, reboot, erase NVS, pull the card}. Invariants: the one above always holds; a derived item's tag always equals its source slice's `src`; after erasing NVS and before rebinding, nothing is sent |
| Menu and UI | The Controls page's profile row is read-only; each global-layer binding still appears exactly once on the "Go to" or "Controls" page; the status bar shows `↑0+` when another profile has waiting items; the lock card shows the total; "Turn off profiles" in System → Profiles is only selectable when waiting counts are all 0; "This Mac (unassigned)" and "Neither" entries are all listed; every new screen satisfies `missing_glyphs == 0` |

### 12.2 Protocol

- Add valid cases with `prof` for each of `hello`, `pong`, `outbox.item`, `file.items`, `debug.state`; invalid cases include: `prof` with capital letters, 13 characters long, `ok` with 5 entries, duplicates in `ok`.
- New flows (in `flows/prof/`, §10.6):
  - `prof_unassigned_host`: an unassigned Mac only gets `ack`, never items, and counts are 0;
  - `prof_two_hosts`: A takes home's, B takes work's, with no crossover;
  - `prof_legacy_hold`: undecided items are not sent before assignment;
  - `prof_widen_after_pong`: after binding, the first newly allowed item always comes after a `pong` with the new `prof`;
  - `prof_mismatch_rehello`: on a mismatch cpad handshakes again and does not reply `rejected`.
- `tools/prof_flows.py` checks each flow's invariant and joins the required list.
- `test_worst_lines.py` adds the lines of §10.4; asserts that `file.items` with 4 entries all carrying `prof` is over the limit, and with 3 entries is not.

### 12.3 daemon (pytest)

| Test | Assertions |
|---|---|
| `test_profile_state.py` | `prof` can be parsed; `profile.json` round-trips equal, mode 0600, **append only**; the default profile is the first entry of `served` |
| `test_profile_enforce.py` | `prof` not in `ok`: **no `rejected`**, no event written, host hello sent again, `prof.mismatch` recorded; no `prof` and `legacy` empty: same; `legacy` set: recorded as it; `file.items` mismatch: no `get`, no `del`, handshake again; 3 in a row: stop accepting and warn; no snapshots pushed when `ok` is empty |
| `test_profile_downgrade_warn.py` | After reporting a non-empty `ok`, no `prof`: warn; after only reporting `ok:[]` (choice sheet, "No profiles"), no `prof`: no warning |
| `test_host_id_hw.py` | With no `host_hw`, keep `host_id` and write the fingerprint; on a mismatch, regenerate, with neither old nor new value in the log; **the `host_id` file format is unchanged, and v0.4.1's `host_id()` reading a directory written by v0.8 does not regenerate**; `cpa host reset` changes nothing without typing `y` |
| `test_decide_other_profile.py` | When the device reports `prof.act:null`, `decide` returns null at once (`other_profile`) |
| `test_manifest_prof.py` | A `manifest.json` with `prof` still passes recording contract v1 validation |
| `test_mcp_profile.py`, `test_export_profile.py` | `--profile` filters by tag; a profile not in `served` gives exit code 2; no `profile.json` gives exit code 2; still starts after the device forgets this Mac; `profile_required` takes effect; old data counts as the default profile; notes are not filtered; phase B write tools carry `prof`; no `host_id` in the export |

### 12.4 Simulator: three fake Macs (G-PROF-1)

Use the existing `cpa-sim serve`: one process is one device, and the card and outbox stay in the process, kept across `debug.reboot` [src firmware/sim/README.md]. Write a new script `tools/sim/run_two_hosts.py` with these steps:

1. Start `cpa-sim serve`; create three temporary `CPA_HOME`s: `home`, `work`, `stranger` (`cpad_sim.py` already requires them to be in temporary directories).
2. Start cpad(home), press keys through stdin to choose "This is Home" on the choice sheet and confirm; wait until the next `pong` carries `prof`.
3. `usb off`; record 3 notes, 1 to-do and 1 queue item, each body containing `HOMECANARY-<n>`; then record a memo (the WAV given by `--mic`). Note the recording session ID the device reports.
4. Stop cpad(home), start cpad(work), `usb on`, choose "Work" and confirm; the current profile switches to Work at once. Record 2 notes with `WORKCANARY-<n>`, then record a meeting.
5. **Regression for the switch leak**: `usb off`, start cpad(home), `usb on` (switches to Home automatically). Send a reply to a home session (fake-claude), switch to Work by hand before `agent.result` comes back, and assert it is deferred ("等 Mac 回完再换", wait for the Mac to reply); once it returns, switch to Work again and assert the home session is gone from "waiting". Record a `WORKCANARY-x` under the Work profile. Restart cpad(home) once more (the same Mac sends hello again) and assert the current profile is still Work.
6. Stop cpad(home), start cpad(stranger), `usb on`, wait 30 s, choose "Later".
7. Plug in home again, then work again, with one `debug.reboot` in between.
8. **Assertions**:
   - In work's `events.jsonl`, notes folder and `recordings/`, there is no `HOMECANARY` at all, no recording session ID from step 3, and no `prof:"home"`; step 5's reply only appears on the home side; the home side likewise has no `WORKCANARY` (including step 5's `WORKCANARY-x`);
   - stranger's `events.jsonl` has no `item.received`, not one recording was pulled, and `cpa status` shows `pending 0`;
   - Each side received all of its own things: not only "nothing wrong received" but also "nothing lost"; the device's `bad/` is empty;
   - `debug.state.prof` matches expectations at every step.
9. Run the "undecided" flow once more on its own: record 2 items with profiles off, then start using profiles and choose "Keep for now", and assert neither Mac gets them; then hand undecided to Home in System → Profiles, and assert only home receives them.

Screenshots use `cpa-sim shots` with a new script `scripts/v08_profiles.cpas`, covering: the first choice sheet, the new-Mac choice sheet, the confirmation, undecided, System → Profiles, the status bar's "家"/"公", `↑0+` and `◇ 未分配`, the lock card. Each must satisfy `missing_glyphs == 0`.

**Existing HIL scenarios**: `run_hil_sim.py` uses the real cpad (which has its own `host_id`), and the simulator boots with empty NVS, so the first handshake shows the choice sheet of §5.1 and pauses delivery, and scenarios such as `capture`, `offline_replay` and `rec_*` would get stuck [src "HIL scenarios on the virtual device" in firmware/sim/README.md]. Fix: after the handshake step in `tools/hil`, add a step "if `debug.state.prof.ask` is true, use `debug.key` to choose 'No profiles' and confirm", and those scenarios run as today; CI reruns all old scenarios with v0.8 firmware. Real-device HIL does the same.

The simulator's `FakePlatform` must be able to store the profile table so it survives reboots (P8-S).

### 12.5 Real device (G-PROF-2)

- On one Mac, run two `CPA_HOME`s (i.e. two `host_id`s) in turn, with the same steps as §12.4, but with a real device and a real SD card.
- Add three more steps to verify principle 8 and its boundary:
  1. Erase only NVS, card still in: after boot profile mode is still on (both `PROFON` on SD and tagged items can turn it on), the current profile is undecided, and nothing is given;
  2. Erase only NVS, pull the card, but with `PROFON` on internal cpafs (non-Launcher layout): same;
  3. Erase NVS, pull the card, and no cpafs: this is the documented residual risk (§8.3 T9); assert the device returns to "no profiles" and shows the first choice sheet, rather than silently handing things over.
- 👤 If it is form A: please plug into each of the two real Macs once, about 10 minutes; also read through the wording of the choice sheets and confirmations.

## 13. Work packages

| ID | Content | Depends on |
|---|---|---|
| P8-DOC0 | First change 05 §6 (fields) and §8 (add a v0.8 column); `protocol/README.md` adds item 23 | This doc finalized |
| P8-P | Schemas, vectors, `flows/prof/`, `tools/prof_flows.py`, `limits.json`, `test_worst_lines` (recomputed on the merged schemas); daemon encoding and validation | P8-DOC0 |
| P8-F0 | Firmware codec understands `prof` (field encoding and decoding only) | P8-P; **must be merged in the same batch as P8-P** (§10.6) |
| P8-F1 | `lib/core/profile` (pure functions) and NVS storage | None |
| P8-F2 | Tagging and filtering of outbox and recordings; slice `src` and clearing on a switch; one cache per profile; `files.rev` per profile; how `revs` are reported; narrowing / widening timing; principle 8's boot decision and `PROFON` | P8-F0, P8-F1 |
| P8-F3 | UI: choice sheets, confirmations, System → Profiles (with unassigned, neither, turn off profiles), status bar (with `↑0+`), lock screen, Controls page, notices | P8-F1; v0.6's `lib/ui/pick_list` (docs/17 §10.1) |
| P8-D1 | cpad: D1–D10 of §7.1 | P8-P |
| P8-D2 | `--profile` for MCP and export (phase D of docs/15); phase B write tools carry `prof` | P8-D1; phases A and B of docs/15 merged |
| P8-S | Simulator: `run_two_hosts.py`, `v08_profiles.cpas`, `FakePlatform` storing the profile table; handling of the first choice sheet in `tools/hil`; CI reruns old scenarios with v0.8 firmware | P8-F2, P8-F3, P8-D1 |
| P8-H | Real-device gate G-PROF-2; 👤 the two-Mac test | All of the above |
| P8-DOC | SECURITY.md (§8.4), README (profiles, downgrade, turning off profiles, Launcher), release notes | No dependencies, finalized last |
| Later | P8-B1: one Mac, two profiles; P8-W: WiFi hints (must pass G-WIFI); P8-E: encryption at rest (must pass G-ENC) | Each waits for the decisions of §15 |

```
P8-DOC0 ──► P8-P ══ P8-F0（同批）──► P8-F2 ──► P8-S ──► P8-H（发 v0.8.0）
P8-F1 ───────────────────────────► P8-F2、P8-F3
P8-P ──► P8-D1 ──► P8-D2
P8-DOC 并行，最后定稿
```

(In the diagram, "同批" means the same batch, "发 v0.8.0" means release v0.8.0, and the last line says P8-DOC runs in parallel and is finalized last.)

## 14. Risks

| Risk | Response |
|---|---|
| You actually carry one laptop back and forth (form B), and v0.8 does not help | Ask first in §15 item 1; the model and protocol leave room for form B (`ok` is an array), so P8-B1 does not change the shape of the protocol |
| Forgetting to switch while offline and recording with the wrong profile | The status bar always shows the profile character; plugging into a bound Mac switches automatically; v0.8 cannot change it afterwards, listed as a decision for you |
| A reply or failure fallback tagged with the wrong profile | Slices carry `src`, a switch clears them, no switch with a request in flight; the property test and simulator step 5 guard this specifically (§3.6, §12) |
| On the first binding cpad does not yet know the new `prof`, and items are refused and then deleted | Widening waits for `pong`; on a mismatch cpad only handshakes again, never `rejected` (§3.7, §7.1 D2) |
| The work Mac does not allow installing cpad | Then "work" profile things stay on the device indefinitely (visible as `↑0+` and in System → Profiles). 👤 Ask together with §15 item 1 |
| Two profiles share the outbox's 200-item capacity | A profile that does not sync for a long time crowds out the other. System → Profiles shows each count; when full, new items are refused by the existing rules |
| Two profiles' recordings are mixed on the SD card | Told apart by the tag in `.M`, with filtering guarded by unit and property tests; pulling the card exposes plain text (T7) |
| `OutboxStep` grows and presses on the loop stack | Run `stack_budget.py` [todo]; if short, compress to 2 bits per item |
| Newly used glyphs not in the efontCN_12 font [todo] | Screenshot tests assert `missing_glyphs == 0`; if a glyph is missing, switch to ASCII quotes |
| Falling back to "give to anyone" after NVS is erased | Principle 8: `PROFON` written in NVS and on every storage, both boot scans can turn profile mode on; the real-device test erases NVS three times to verify; "erase NVS plus swap the card" is documented as residual risk |
| Existing HIL scenarios stuck at the first choice sheet | `tools/hil` adds a step that chooses "No profiles" automatically; CI reruns with v0.8 firmware (§12.4) |
| You think v0.8 achieves L3, but it is only L1 | Stated clearly in §0 item 8, §8 and SECURITY.md; P8-E listed separately |
| You do not notice that an automatic switch changed the profile | A notice every time; the status bar character and color change together; automatic switches only happen on plug-in or a different Mac |

## 15. Decisions for you

If you do not reply, the defaults are used.

1. **How you use it**: two Macs (one at home, one at work), or one laptop carried back and forth? Can cpad be installed on the work Mac? **Default: two Macs, and the work Mac can install it**; v0.8 is built for form A. If it is one laptop, P8-B1 comes first, but then the separation is for organization only.
2. **How much protection**: **Default: v0.8 only does L1**, that is routing by profile, nothing for unknown Macs, `host_id` as a secret; the plain-text risk on the device and the small metadata leaks go into SECURITY.md. If you think the work Mac might deliberately read the device, then until P8-E is done, do not plug the device into the work Mac, or sync "home" waiting items to zero at home before you go. Alternative: do P8-E (encryption at rest) in v0.8. The cost is that v0.5's playback on the device cannot play encrypted recordings, and one more gate, G-ENC, must be passed.
3. **Unknown Macs**: **Default: give nothing**, show the choice sheet with the cursor on "Later".
4. **Which profile things recorded offline belong to**: **Default: the current profile**; on plug-in or a different Mac, switch automatically to that Mac's profile with a notice; the same Mac handshaking again does not undo a manual switch; v0.8 does not change profiles afterwards. Alternative: ask once on plug-in before switching.
5. **Waiting items from before the upgrade**: **Default: ask once on the first binding**, cursor on "Keep for now"; they can later be handed over as a whole in System → Profiles.
6. **Number and names of profiles**: **Default: fixed at two, "Home" (家) and "Work" (公司)**, with "家" or "公" in the status bar.
7. **What each profile covers**: **Default**: project cache, destination cache, meeting recording switch, and the status bar character and color. No "which apps each profile turns on". The meeting recording switch is named in #19, but it can be cut if you have no use for it.
8. **WiFi and time of day**: **Default: neither in v0.8**. WiFi later as P8-W, after the toolchain evaluation and G-WIFI. No time-of-day rule.
9. **Version**: **Default: v0.8** (after docs/14 and docs/15). If you are **already** plugging into two Macs back and forth, move P8-P, P8-F0, P8-F1, P8-F2 and P8-D1 forward to the next version, with the simplest confirmation as the UI at first. docs/13 §18 item 8 already planned it this way.

## Appendix: differences settled here compared with the #19 sketch and docs/13–15

| Before | Here | Why |
|---|---|---|
| #19: the Mac says in its hello "I am work-mac (profile=work)" | The Mac only reports `host_id` and a name; which profile it belongs to is chosen only by you on the device | If the Mac's word counted, any Mac could claim to be "home" |
| #19: manual switching with a chord (e.g. Opt+P) | No chord; choose in System → Profiles, with a confirmation | Opt+P is already projects; all 26 letters are taken; see docs/13 §14 |
| #19: a time-of-day fallback | Not done | It would change the profile without you knowing and put the wrong tag on things |
| #19: WiFi SSID as the second signal | Not done in v0.8; only the gate G-WIFI is reserved | The device has no WiFi; IDF 4.4 is EOL; SSIDs can be forged |
| #19: multi-host pairing, one key per host | Over USB, `host_id` serves as a secret (the device stores only its hash and only accepts ≥16 characters; the Mac side ties it to the hardware); keys are added together with wireless | §4.3 |
| #19: modules, outgoing destinations and accent colors all per profile | Only projects, destinations, the meeting recording switch (can be cut), and the status bar character and color | Only what #19 names and is useful now |
| docs/13 §14: switch on the Controls page with a confirmation | The Controls page has only one read-only row; switching is in System (Opt+M) → Profiles | The menu can only replay keys, and Opt+M already appears once on the "Go to" page |
| docs/14: items carry no `prof` | v0.8 adds `prof` to items, separate from `dest` | Destination is "where it goes", profile is "who may take it": two different things |
| docs/14 §7.2: the device reports its cached `revs`, and the host pushes again if they differ | In profile mode, reported per §3.5 item 6: H's profile cache, or an echo of what H pushed | Never report another profile's fingerprint, and never make an old cpad keep pushing |
| docs/15 §3.11: old untagged data counts as this Mac's default profile | Used as is. The default profile is the first profile this Mac served in `profile.json`; the filter only looks at the tag on the data | Does not depend on the device's current state (§7.2) |
| docs/17 §1.4 (original draft): v0.8 needs no change to v0.6 | v0.6's new offline data (`why`, `review`, "for AI") is split per profile along with the slices; the choice page and to-do page are cleared on a profile switch; v0.6.1 approvals look at `prof.act` (D9) | Review M11; docs/17 has been rewritten to match |

## Appendix: evidence index

- **Issues**: #19 (requirements, sketch, the "Cheap now" section); #24; #34 (no app-specific integrations, see ADR 0004).
- **Docs**:
  - docs/13: §3 (principles 2, 8), §5.2, §6.1–§6.2 (the menu only replays keys; each global-layer binding appears exactly once on the "Go to" or "Controls" page), §7.1 (the lock screen is not a security lock), §9.6, §10.3 (budgets), §10.4 (v0.5 shared components), §13 item 7 (the current state with several Macs), §14, §15, §18 item 8;
  - docs/14: §2.2, §3.1 (letter groups), §7.2, §7.6–§7.8, §10.4;
  - docs/15: §2.4, §3.6, §3.11, §7.3;
  - docs/17: §1.4 (v0.6's new offline data), §2 principle 7 (digits are the exception only on the agent choice page), §3.6 (when the choice page is cleared), §5.1 (when approval returns null), §9 (v0.6's new requests), §10.1 (`lib/ui/pick_list`);
  - docs/05: §6.9, §8;
  - docs/04: §5.3 (line 149), §8.1, S11;
  - docs/02: §7 (similar products such as cardputer-agent-deck);
  - docs/gates/launcher.md: line 50 (shared NVS), line 54, line 114.
- **SECURITY.md**: v0.1's trust boundary; "if the device is lost, items not yet synced are plain text" (from line 34); plain text on the SD card; the purpose of ↑N in the status bar.
- **ADR 0001**: lines 17, 77, 100 (IDF 4.4 is EOL; re-evaluate before adding wireless; auto-reset on upload).
- **Protocol**:
  - `protocol/limits.json` (`ping_interval_ms` 5000, `host_id_pattern` `^[a-z0-9-]{8,32}$`);
  - `protocol/README.md`: item 14 (the reserved name `enc`), item 16 (no `debug.*` in release firmware), item 20 (`host_id`, `host_label`, and the long-`dest` precedent for `file.items`), the note under item 18 (firmware vector tests require field-by-field round trips);
  - v0.4.1's `hello.schema.json`, `common.schema.json` (`hostId`, `hostLabel`, `fileItem`, `errorCode`); `protocol/tools/test_worst_lines.py` (`worst_item`, `worst_file_items` 1988 B, `worst_pong` 238 B);
  - the "forward compatibility" section of `protocol/recording/v1/README.md`.
- **Firmware** (`release/v0.4.0`):
  - `lib/outbox/outbox_core.h:1-22` (ID order, at most 4 in flight, capacity 200, `rejected` goes to `bad/` keeping only 20), `outbox_io.h`;
  - `lib/shell/runtime_outbox.cpp` lines 6–8 (`/cfg/projects.json`), line 38 (MonoId shared with recordings), lines 95–96 (files sent as they are), lines 166–185 (`flush_agent_save`); `lib/shell/confirm.cpp:27-35` (offline replies take `Route::Queue`), `:56` (400 ms); `lib/shell/shell.cpp` (`queue_instead`); `lib/shell/runtime.h` (`OutboxStep` of about 2.9 KB, 16 KB loop stack);
  - `lib/storage/storage_plan.h:1-11` (with a card inserted main storage is the SD card; under Launcher there may be no cpafs);
  - `lib/rec/rec_index.h` (256 entries, `rev_`), `rec_meta.h` (`.M` at most 512 B), `file_req.h:3` (`E_BUSY live`);
  - `lib/core/app_state.h`, `mono_id.h` (MonoId 12 B); `lib/hal/hal_sha256.h` (mbedtls, device only);
  - `lib/shell/status_bar.cpp`; `test/core/test_menu_model/test_main.cpp:79`; `test/core/test_codec_vectors/test_main.cpp:1-5`; `test/core/test_link_flows`; `modules/system/system_logic.h`;
  - on the v0.4.1 branch, `lib/protocol/decode_host.cpp`, `messages.h` lines 63–80.
- **daemon**: on the v0.4.1 branch, `config/host.py:22-40` (matched against the pattern after `.strip()`, regenerated if not), `protocol/validate.py:23-29` (`$` replaced with `\Z`), `config/settings.py` (the `host_id` path, `rec_pull`), `app.py` lines 118, 248; `device/port_finder.py:1-4, 15-16` (only 303A:1001, tries anyone before binding a serial number); `hub/reduce.py:141-174` (starting a session by qid takes the saved text); `protocol/validate.py` (lets extra fields through).
- **Simulator**: `firmware/sim/README.md` (`serve`, `cpad_sim.py`, `run_hil_sim.py` with the real cpad; card and outbox kept across reboots); `sim/lib/simio/sha256.h`.
- **Line lengths**: the numbers in §10.4 were computed by adding `prof` and `feats` to the constructors of v0.4.1's `protocol/tools/test_worst_lines.py` (`json.dumps(..., ensure_ascii=False, separators=(",", ":"))` plus a newline); the scripts are `profcalc/calc.py` and `v06calc/rev.py` in the scratchpad.
- **Similar products** (from docs/02, some only seen as search summaries): yuk1-kondo/cardputer-agent-deck (at most 8 Macs, USB first, mutual HMAC on every connection); sohsatoh/cardputer-cli-buddy (keys only provisioned over USB).
- **General knowledge, not specifically verified**: Android work profiles (separate storage and keys, badges, can be paused); iOS Focus modes (triggered by time, place or app, or switched by hand). Both show that "switching must be visible and manual"; triggers such as SSIDs can be forged.

## Appendix: how review comments were handled

The review concluded that "the routing model itself is right", and raised 5 high, 9 medium (M9 is a supplementary test list), 8 low and 4 scope comments. Each was checked against the code (`decide_reply` queueing offline, `flush_agent_save`, `reduce.py` starting a session by qid, `bad/` keeping only 20, `ping_interval_ms`, `.strip()` and `\Z` in `host.py`, `storage_plan.h`, `rev_` in `rec_index`, `E_BUSY live` in `file_req`, the shared MonoId, the location of the menu test, `port_finder`'s 303A:1001); all hold, and all were adopted. One more fix the review did not raise: v0.6.1 approvals would make the hook wait for nothing when the device does not serve this Mac, so D9 was added.

| Review comment | Handling |
|---|---|
| **H1** Replies and failure fallbacks get the wrong profile, leaking text and starting a new session on the wrong Mac | **Adopted**. New principle 4, "derived things follow their source"; three safeguards: slices taken only when the current profile ∈ ok(H), cleared on a profile switch; derived items take the slice's `src`, and failure fallbacks take `AgentReqInfo.src`; no profile switch with a request in flight (§3.3, §3.4, §3.5 item 5, §3.6). Tests: `test_profile_src`, the property test, simulator step 5 |
| **H2** On the first binding or handing over "undecided", cpad refuses items, which go to `bad/` and are eventually deleted | **Adopted**. New principle 5 and §3.7: narrowing takes effect at once and returns in-flight items, widening waits for a `pong` with the new `prof`; on a mismatch cpad writes nothing and handshakes again, never replying `rejected`, stopping with a warning only after 3 in a row (§7.1 D2–D4); §3.5 item 7 rewrites when ok(H) is recomputed, no longer contradicting "takes effect right after confirmation" |
| **H3** A new handshake silently undoes a manual switch | **Adopted**. Automatic switching only on plug-in or a different Mac; "the host of the last handshake" is stored in NVS, so boot does not switch wrongly either; test "after a manual switch, the same Mac sending hello again leaves the current profile unchanged" (§3.3, `test_profile_switch`) |
| **H4** The first choice sheet defaults to the most dangerous row, with no guard against mistakes | **Adopted**. The first row becomes "Later (hand over nothing this time)", with "No profiles" last; every choice sheet has the 400 ms guard; also deferred while recording or with an overlay (§5.1, §5.2) |
| **H5** A second line in the `host_id` file is incompatible with v0.4.1–v0.7 cpads | **Adopted**. The fingerprint goes into a separate `$CPA_HOME/host_hw`, and the `host_id` file format is unchanged; if missing it is written without regenerating, and only regenerated when it exists and does not match; the reason (Migration Assistant) is in §4.1; tests for reading old and new formats and for downgrades (§4.1, §10.5, `test_host_id_hw.py`) |
| **M1** Count side channels | **Adopted**. `files.rev` becomes one per profile, reporting only what H can see (§3.5 item 2, §11); ID gaps and "recording in progress" are in §8.3 T14 as known leaks; §8.1 and the test oracle become "no content, no count fields"; no separate per-profile ID sequence |
| **M2** Which profile's cache `revs.*` report was not specified | **Adopted**. §3.5 item 6: with ok(H) not empty, report H's profile; when empty, echo what H pushed this time; never another profile's. Test `test_profile_revs` |
| **M3** `/cfg/PROFON` is actually on the SD card, so the test in §12.5 cannot be done | **Adopted**. `PROFON` is written in NVS and on every mounted storage; the recording boot scan can also turn profile mode on; nothing is handed over until both scans are done; after NVS loss the current profile is undecided; "erase NVS plus swap the card equals a reset" is documented as residual risk; §12.5 becomes three steps, the third verifying the documented boundary (principle 8, §3.3, §8.3 T9, §11, §12.5) |
| **M4** ↑N counting only the current profile weakens the SECURITY.md mitigation | **Adopted**. The status bar shows `↑0+` when another profile has waiting items; the lock card shows the total (§5.5) |
| **M5** The protocol flow plan cannot work | **Adopted**. A new `flows/prof/`, replayed by a Runtime-level firmware test and the daemon's `tests/contract`; `tools/prof_flows.py` checks the invariant and joins the required list; expectation keys add `device.outbox_pending_visible` (§10.6, §12.2) |
| **M6** Existing HIL and simulator scenarios would hang | **Adopted**. After the handshake, `tools/hil` uses `debug.key` to choose "No profiles" automatically; CI reruns all old scenarios with v0.8 firmware (§12.4) |
| **M7** The wire state is undefined when the first choice sheet appears with profile mode off | **Adopted**. While the choice sheet is open, treat it as "unassigned": carry `prof{ok:[],ask:true}`, counts 0; §0 item 9 becomes "the first plug-in pauses delivery"; D8 does not falsely report a downgrade for a device that only reported `ok:[]` (§5.1, §7.1 D8, §9) |
| **M8** MCP and export depend on the device's current state | **Adopted**. `profile.json` becomes an append-only `served`; filtering only looks at data tags; exit code 2 when the file does not exist; documented honestly that notes are not filtered, T12 toned down; §9 states "what was handed over after a wrong binding cannot be taken back"; docs/15 phase B write tools carry `prof` (§7.1 D1, §7.2, §8.3 T12, §9) |
| **M9** Test list | **Adopted**. H1–H5, M1–M3 and M7 each have RED tests; plus narrowing / widening timing, no repeated snapshot pushes, choice sheet guard and default row, profile mode staying on after erasing NVS with only recordings tagged (§12.1–§12.3) |
| **L1** An unassigned Mac where `` ` `` was pressed cannot be bound without replugging; "Neither" Macs are invisible and cannot be forgotten | **Adopted**. System → Profiles adds "This Mac (unassigned) · Enter to choose a profile" and lists "Neither" entries (§5.6) |
| **L2** Turning profile mode off needs esptool | **Adopted**. "Turn off profiles" on the device, available only when every tagged waiting count is 0, a choice plus a confirmation (§5.6, §9) |
| **L3** §5.3 did not say the current profile switches right after confirmation | **Adopted** (§5.3) |
| **L4** In test firmware any host can bind itself with `debug.key` | **Adopted**. §8.1 states the guarantees only hold for release firmware |
| **L5** "No third parties on USB" overstated; `host_id` only requires 8 characters | **Adopted**. §4.3 states that cpad sends `host_id` to any 303A:1001 serial port before binding a serial number, added to T4's residual risk; the device only accepts `host_id`s of ≥16 characters, and `limits.json` adds `prof_host_id_min` (§3.2, §10.1) |
| **L6** Wrong citations | **Adopted**. The menu rule now cites `test_menu_model/test_main.cpp:79`; vector round trips cite `test_codec_vectors/test_main.cpp:1-5` and the note under README item 18; the simulator SHA-256 cites `sim/lib/simio/sha256.h`; `lib/core/profile` gets the digest through an interface (§4.2) |
| **L7** §10.4's line lengths did not count `feats` and `how`; 4-entry `file.items` without `dest` is also over | **Adopted**. Recomputed: device `hello` about 726 B, `pong` about 410 B, 4 entries without `dest` plus `prof` 2076 B over the limit; the existing 1988 test must change; P8-P recomputes on the merged schemas (§10.4) |
| **L8** Other firmware installed by Launcher shares NVS and can change bindings | **Adopted**. Added to T8 (§8.3) |
| **Scope: §6 WiFi too detailed** | **Adopted**. Condensed to one paragraph, keeping only the name G-WIFI (§6) |
| **Scope: per-profile meeting recording switch** | **Adopted**. Kept, marked "can be cut" (§3.6, §15 item 7) |
| **Scope: `cpa host reset` and the hardware tie** | Kept; reasons in H5 and §4.1 |
| **Scope: `debug.state.prof`** | Kept; the tests need it |
| **Typing check** | Agreed. The only command to type on the Mac was turning profile mode off, which L2 changed to a choice on the device |
