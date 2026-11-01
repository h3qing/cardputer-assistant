**English** · [中文](17-v06-select-dont-type.zh-CN.md)

# 17 · v0.6 "Select, don't type": everything waiting for you can be handled by selecting; to-dos confirmed and ticked on the device

> Status: **design draft (revised after two rounds of adversarial review), waiting for your decision on §17** · Date: 2026-10-21 · Role: architecture (v0.6) · Target versions: v0.6.0 and v0.6.1 (docs/13 §15).
> Sources: #16; #33/#35 (hooks, on main); daemon `agents/claude_code/{hooks,hook_state,hook_settings,adapter,argv,blockers}.py`, `inbox/need.py`, `hub/{snapshots,reduce}.py`, `projects/registry.py`, `control/{client,server}.py`, `cli/hooks.py`, `config/settings.py`; firmware `modules/{home,inbox}/*`, `lib/shell/{confirm,shell_menu}.cpp`, `lib/help/menu_model.cpp`, `lib/module_api/module.h`, `lib/core/{domain,app_state}.h`, `lib/input/keymap.cpp` (v0.4.0, `release/v0.4.0`), `src/module_list.cpp`; `protocol/limits.json`, `schema/v0/*`, `tools/{flows,check_vectors}.py`; from the v0.4.1 worktree `notes/{render,writer}.py`, `recordings/{route,service}.py`, `app.py`, `rec.items`, `hello.caps`, `tools/rec_rules.py`, `tools/test_additive.py`; from the v0.5 worktree `dest/router.py`; docs/05 §6.10, §7.5, §8; docs/11 §1.2, §6.7, §7.5–§7.9, §18; docs/13 §3, §4.1, §5.2, §5.6, §6.1–§6.2, §10, §14, §15; docs/14 §1.2, §2.3, §3.1, §3.2, §5, §7, §10.3; docs/15 §2.3, §3.4, §3.6, §5; docs/16 §4, §7, §11 (App SDK, in progress); gates/cli-gates.md G3, §5a, §7; SECURITY.md; docs/02 §3, §4, §7 (similar products); Claude Code 2.1.306's `--help` and binary strings (from the research summary, not rechecked for this draft).
> Marks: **[src]** code or docs read · **[meas]** existing measurement · **[calc]** computed with a script · **[inf]** inferred · **[assume]** design assumption, can change · **[open]** to verify with a gate or real device · **👤** needs you.
> Scope: only v0.6's choice-based replies, meeting to-do confirmation, the to-do app and on-device approval. No device connected, no CLI run, no code changed. Context profiles are in docs/18. Screen sketches are drawn for 240×135, about 40 ASCII characters or 20 Chinese characters per line. How review comments were handled is in the appendix at the end.

## 0. One-page summary

**What you want** (#16, docs/13 §15): make decisions on the device by selecting only, never typing. Done means: **reply to a session, confirm a to-do, tick a to-do, all three without typing.**

**Where we fall short today** (§1.2):

1. "Waiting for you" only knows that a session is waiting, not why. The hooks already compute the reason (waiting for permission, waiting for plan approval, asking a question, idle), but it never reaches the device.
2. The device can do nothing for a session that **is waiting**. Replies only work for idle background sessions; sessions waiting for permission or asking a question are turned into queued feedback, and queued feedback is never delivered automatically. Sessions opened from the device default to plan mode [src registry.py `DEFAULT_MODE`], so the most common "waiting for you" is very likely "approve the plan" [inf].
3. Replies can only be typed. The device can only type ASCII, and you dictate in Chinese.
4. To-dos the AI pulls out of a meeting are only counted; to-dos can only be ticked in Obsidian.

**Ship in two steps** (§17 item 1):

- **v0.6.0** (firmware + cpad):
  1. **Options by reason in "Waiting for you"** (§3): each row of `sessions.snapshot` gets a `why`. Pressing Enter in "Waiting for you" makes the device ask the Mac for this row's **choice sheet** (`choice.get`); pressing a number picks one, and after confirmation `agent.pick` is sent. **The device never sends body text**, only "which option" and the one-time token `tok` the Mac gave it.
  2. **Preset replies** (#16, §4): an idle background session shows "1 Continue / 2 Looks good, keep going" (option 2 only appears when you can see what it last said). Each project can be configured on the Mac (`$CPA_HOME/replies.json`, `cpa reply`). Delivery still uses the existing stop → `--resume <sessionId> -- <text>` (with no other flags). If a copy gets forked, cpad stops the copy first.
  3. **Meeting to-dos confirmed one by one** (§6): look at them one at a time. T = my to-do; Q = for AI; Opt+G = send to another destination; Opt+D = skip. All three go through the docs/14 destination router and are visible in `cpa audit`. `summary.md` is not changed by a single character.
  4. **To-do app, Opt+W** (§7), with three tabs: Mine (checkboxes with a `^cpa-` block ID in the daily notes; Enter ticks or unticks), To confirm (meeting to-dos), For AI (each project's queue). Ticking changes only 1 byte of that line, after checking the line has not been changed by someone else.
- **v0.6.1** (Mac side only, gate G-D1 first):
  5. **On-device approval** (§5, **off by default**, turned on with `cpa decide on`): through Claude Code's `PermissionRequest` hook, handle permission waits, plan approval and AskUserQuestion on the device. **If the device is absent, does not answer, times out, or anything goes wrong, it falls back to the normal dialog on the Mac; it never auto-allows.** High-risk objects (commands, paths, URLs) are **shown verbatim**; if they cannot be shown in full they do not go to the device. A high-risk "Allow" needs **Y**, and the menu cannot press it for you. If G-D1 fails, v0.6.1 does not ship, and v0.6.0 is unaffected.

**Protocol** (settled once in v0.6.0; v0.6.1 does not change the protocol again):
- 6 new messages: `choice.get`, `choice.sheet`, `agent.pick`, `todo.list`, `todo.items`, `todo.act`. The read-only pair is not under `agent.*`, so the rule "`agent.*` is only sent from the confirm box" stays.
- 6 optional fields on existing messages: `why` on `sessions.snapshot` rows, `review` on `queue.snapshot`, `choose`, `decide`, `todo_app` in the host's `hello.caps`, and `feats` in the device's `hello`.
- `v` stays 0, and **no new error codes**.
- The two large replies, `choice.sheet` (worst case 1290 B) and `todo.items` (worst case 1699 B) [calc], share the "at most one in flight" lane with `rec.items`, so the device receive buffer's worst case is unchanged at 7424 / 8192 B.
- The 6 new schemas must be merged in the same batch as the firmware codec registration (§9.7).

**Security** (§13):
- Execution still goes only through "online request + device confirm box (400 ms debounce) + no retry". A choice sheet opens with nothing preselected, and Enter is ignored for 400 ms after it opens.
- New data flows in v0.6.0: the Stop hook forwards one more **tail excerpt** (≤120 B, cleaned the same way as the existing head excerpt); cpad changes one byte in the daily note.
- New data flows in v0.6.1: a short summary of the tool arguments is sent to the device for approval. "Tool arguments never leave the hook process" in SECURITY.md must be rewritten: it only holds until you run `cpa decide on`. On-device approval **has no PIN lock**, so an unlocked device can approve commands; this goes into SECURITY.md.

**7 things need your call** (§17), all with defaults.

## 1. Starting point and gaps

### 1.1 What exists today

| Part | Current state | Evidence |
|---|---|---|
| hooks (#33, on main) | `cpa hook` only forwards allow-listed fields, never prints, and exits 0 within 0.5 s; `hook_state.merge()` computes the phase, the reason `why` (permission / plan / question / input / login / error) and `msg`. `msg` is a ≤120 B excerpt from the **start** of `last_assistant_message`; `excerpt()` removes `` ` ``, `*`, `# ` and format characters | [src hooks.py:1-8, 32-33, 48-63; cli/hooks.py:1-5; hook_state.py:1-13] |
| Reason sent to the device | **No**. `merge_pairs()` only returns `(info, project)` and drops `why` and `msg`; `sessions.snapshot` rows only have `st` and `need` | [src hook_state.py:175-178; sessions.snapshot.schema.json] |
| Grace period for dialog waits | The hook state for a dialog wait only has `NEEDS_GRACE_S = 2 s`; after that a disagreeing poll result wins | [src hook_state.py:8-9, 25-26, 148-154] |
| Order of "Waiting for you" | `_order` only has three groups, "needs you / running / other", sorted by `since` within a group; the snapshot holds at most 10 rows | [src hub/snapshots.py:57-59, 62-80] |
| Replies | Conditions: link Ready, `caps.reply`, background, idle. `experimental_bg_reply` defaults to false. Method: stop first, then `--bg --resume <sessionId> -- <text>`. One fork turns replies off for the rest of this run (`E_REPLY_FORKED`), but **the copy is not stopped** | [src adapter.py:127-153; argv.py:51; confirm.cpp:27-35; settings.py:60] |
| `since` | Only resets when a poll sees the status change; a turn that finishes between two polls (3 s) is invisible | [src inbox/need.py:3, 30-33] |
| Sessions waiting for permission or asking a question | The device classifies them as `NotIdle` and stores queued feedback instead, which is never resent automatically | [src confirm.cpp:32; 05 §7.5] |
| G3 | stop → resume without flags keeps the ID ✅. Whether the model actually replied was **not verified**, because the CLI was not logged in at the time. 2.1.306's `--help` says `--resume` on a session that is still running opens a separate copy | [meas cli-gates.md §4; research] |
| "Waiting for you" app | Rows = sessions with `need=true`. Enter opens typed feedback, then confirm; Opt+D dismisses (online only); the detail line is `claude attach <short>` | [src inbox_logic.cpp:38-70; inbox_module.cpp:37-57] |
| Menu | "This page" on the overlay lists that layer's bindings; Enter closes the menu and **replays** that key (docs/13 principle 4) | [src menu_model.cpp:70, 134; shell_menu.cpp:138-146] |
| Meeting to-dos | `result.json.actions[]`: at most 20, `text` at most 200 characters, with `owner`, `due`, `confident`. cpad only records the count `actions_n`, and the note says "N to-dos". v0.4.1's `rec.items.todo_n` = the number of unticked items in `summary.md` | [src docs/11 §7.6, §6.7; v0.4.1 recordings/service.py:235-241] |
| To-dos | v0.4.1 writes `- [ ] HH:MM #key text ^cpa-<id>`. `NotesWriter` only appends, idempotent by block ID; **it replays the whole event history on every start**, so a line you deleted in Obsidian comes back at the next start | [src v0.4.1 notes/render.py:48; writer.py:55-71; app.py:118-122] |
| In-flight lanes | `queue.done`, `inbox.dismiss`, `rec.*` share 1, timeout 10 s; `agent.*` has its own 1, timeout 30 s. The protocol tools put every `agent.*` into the latter | [src limits.json; docs/13 §10.2; tools/flows.py:75-79] |
| Control socket | A request line is at most 1024 B; `client.request` uses one timeout for both connect and read | [src control/server.py:27; control/client.py:14-29] |

### 1.2 Gaps

| # | Gap | How v0.6 fills it |
|---|---|---|
| 1 | The device does not know why a session is waiting | Add `why` to snapshot rows; give 1–3 lines of explanation in the choice sheet (§3.1, §3.2) |
| 2 | A waiting session cannot be acted on from the device | On-device approval (§5, v0.6.1), off by default |
| 3 | Replies need typing | Preset replies (§4) |
| 4 | Nobody confirms meeting to-dos; to-dos cannot be ticked | Confirm one by one (§6); the to-do app (§7) |

### 1.3 In scope and out of scope

| In scope | Out of scope (where it goes) |
|---|---|
| **v0.6.0**: reasons on snapshot rows; choice sheets (`choice.get` / `choice.sheet` / `agent.pick`); preset replies and `replies.json`; confirming meeting to-dos one by one (through the destination router); the three tabs of the to-do app; ticking and unticking. **v0.6.1**: on-device approval (the hook's `--decide` mode, summaries, the waiting book, risk-based keys) | App skeleton and check tools `new_app.py`, `check_apps.py`: belong to docs/16 (App SDK); this version only uses them. Replies to interactive and desktop sessions: still handled on the Mac; a mods adapter goes into the backlog (§4.4). Voice replies: v0.7, through recording contract v1 (§17 item 6). Ticking offline. Resending queued feedback once a session goes idle: backlog. PIN lock: backlog (§13). Context profiles: docs/18. A general host-driven UI `ui.*`: v0.7; this version's choice sheet is its first sketch |

### 1.4 Division of work with other docs

- **docs/14**: D5-B's "Pending" sits at the very top of "Waiting for you". v0.6's order is: Pending → sessions that need your decision (permission, plan, question) → the "N meeting to-dos to confirm" row → other sessions that need a look (login, error, idle). Meeting to-dos' T, Q and Opt+G all go to docs/14's router (`router.on_capture`), so "sending meeting-summary to-dos out one by one", which docs/14 §1.2 left for v0.6, is done here too (§6).
- **docs/15**: MCP only reads files. Ticked to-dos are written back to the file, so MCP sees them immediately. The "handled" state of meeting to-dos must be reflected in `list_todos` (§12). Ticking a to-do is the first time cpad **edits** a daily note **in place**, breaking "cpad only appends"; recorded in the differences table.
- **docs/16**: the to-do app follows the App SDK conventions (`expect_app_contract`, `check_apps.py`); its home tile goes into the empty slot in row 4 of the first-party 3×3 grid, not into the row docs/16 reserves for apps.
- **docs/18**: v0.6 adds a few pieces of **Mac data that are shown even offline**: `why` on snapshot rows, `queue.snapshot.review`, and the "For AI" tab (from `queue.snapshot`). v0.8 must split them by profile like the existing sessions and queue (docs/18 §3.6). Choice sheets and to-do pages are one-off: they are cleared when the link leaves Ready, on lock, or when a different Mac connects (`host_id` changes) (§3.6), and never go into any cache. In v0.6.1 cpad only makes the hook wait when the device reports "the current profile belongs to this Mac" (docs/18 §7.1 D9).

## 2. Principles

1. **The device never sends body text for execution**: preset replies and approvals only send `tok` plus a number. Preset text, denial messages and answer labels all live on the Mac. Typed replies (the existing `agent.reply`) stay as they are.
2. **Execution only through online request + device confirm box + no retry** (inherited from SECURITY.md). `agent.*` can only be sent from `confirm()` (inherited from module.h); the read-only `choice.get` is not under `agent.*`.
3. **Options are bound to a token**: what the device saw is what the Mac executes. If things have changed on the Mac, the token is void and the reply is `E_NOT_REPLYABLE`; it never "swaps in the closest option". Things changed means: the session started running, finished another turn, the dialog was already answered in the terminal, or the presets changed.
4. **Approval is off by default; on any error, fall back to the Mac dialog, never auto-allow.**
5. **Keys by risk, guarding against slips**: a high-risk "Allow" in the confirm box needs Y, and neither Enter nor the menu can stand in for it; a choice sheet opens with nothing preselected, and Enter is ignored for 400 ms after it opens.
6. **Files are the truth**: to-dos follow the daily notes; `summary.md` belongs to WhisperWoof, and cpad does not write it [src recording contract v1 single-writer table].
7. **One letter, one job** (extending docs/13 principle 2): T to-do, Q for AI, Opt+G pick a destination, Opt+D handle or skip, digits = projects. The choice sheet is the only exception (§3.3).
8. **What you see is what runs**: objects you are asked to approve on the device (commands, paths, URLs) are shown verbatim, with no characters removed, no whitespace collapsed and nothing redacted; if that is not possible, the item stays on the Mac (§5.2).

## 3. "Waiting for you": every row can be handled by selecting

### 3.1 The reason, from hook to device

- `hooks.py`: besides the existing head excerpt `msg`, the Stop event forwards a **tail excerpt** `tail`: the last ≤2000 characters of `last_assistant_message`, cleaned by the same `excerpt()`, taking the last ≤120 B. Questions usually come at the end. The allow-list `KEYS` gains `tail`, and the daemon's `parse()` still validates strictly.
- `merge_pairs()` changes to return `(info, project, why, msg, tail, at)`, where `at` is the time of the hook state; `inbox/need.Tracked` gains these fields.
- `sessions.snapshot` rows get an optional field `why`, **present only when `need=true`**:
  - values `permission`, `plan`, `question`, `input`, `login`, `error`;
  - uses the pattern `^[a-z]{1,10}$`, not an enum. The device treats an unknown value as if the field were absent;
  - without hooks, fall back on `waiting_for`: permission → `permission`, question → `question`, dialog → not given.
- **From v0.6.1, the waiting book is the truth**: while cpad is holding a hook for a session, that row is always `need=true` with `why` taken from the waiting book, whatever the poll says. Reason: while the hook is still running the dialog may not have appeared yet, and `agents --json` very likely reports busy [open G-D1 d]; with the 2 s grace period the row would vanish within seconds while the hook still waits for 100 s.
- **Order**: within the "needs you" group, `_order` sorts by reason first: permission, plan, question → login, error → idle; within one kind, the longest-waiting first. When the snapshot only holds 10 rows, what most needs you is never pushed out.
- `msg` and `tail` are not in the snapshot (10 rows × 120 B do not fit); they are only given in the choice sheet (§3.2).
- How the device shows them: Permission, Plan, Question, Idle, Login, Error.

### 3.2 The choice sheet: options by reason

| `why` | Condition | `kind` | Options (generated by cpad) | After picking |
|---|---|---|---|---|
| permission | v0.6.1, on-device approval on, hook waiting (§5) | `permission` | 1 Allow this once; 2 Allow all for this session (hidden by default, §5.6); 3 Deny: stop and wait for me; 4 Deny: try another way | Answer the dialog through the hook |
| plan | Same, tool is ExitPlanMode | `plan` | 1 Approve, file edits still ask; 2 Don't approve: keep refining the plan | Same |
| question | Same, AskUserQuestion with exactly 1 question, single choice | `question` | 1–4 = the options Claude gave | Same, with the answer |
| input / error, or no reason and status idle (no hooks installed) | Background, idle, `caps.reply` | `reply` | This project's presets, at most 5 | stop → resume (§4) |
| None of the above | — | `none` + reason code | No options from cpad | — |

- **Reason codes for `none`** (the `why` field); the device supplies the display text itself:
  - `interactive`: interactive session, handle it on the Mac;
  - `no_decide`: on-device approval is not on for this Mac (in v0.6.0 every permission, plan and question is this);
  - `mac_only`: approval is on, but this item can only be handled on the Mac (command cannot be shown in full, suspected secret, MCP tool, several questions or multi-select, raised by a subagent);
  - `released`: waited too long, handed back to the Mac dialog;
  - `elicit`: an MCP server is asking you for input [src hook_state.py:83-84];
  - `quota`: quota used up; continue on the Mac once it recovers [src hook_state.py:85-86];
  - `no_reply`: replies are not enabled;
  - `login`: claude on the Mac is not logged in;
  - `busy`: the session is running;
  - `gone`: the session no longer exists.
- **A row the device adds itself** (unnumbered, so digits cannot pick it): the last row of a `reply` sheet is "Type a reply…", and the last row of a `none` sheet is "Type a note (queued)…". Both do what pressing Enter on this row does today: type, confirm, and the confirm box decides whether it is sent as a reply or stored as queued feedback [src confirm.cpp:27-35]. So a `none` sheet loses nothing compared with today. The hint bar on every sheet reads "`` ` `` back (leave it to the Mac)".
- **Explanation lines**: at most 3 `lines`, each ≤64 B.
  - Permission: the object to approve, verbatim, on 1–2 lines, plus one line of explanation (§5.2);
  - Plan: the first 3 lines of the plan, plus `total` (how many lines the plan has);
  - Question: the question;
  - Reply: what the session last said, from `tail` (the end, ≤120 B, on two lines), labelled "It last said". Without `tail` the sheet says "(can't see what it last said)", and presets with `when:"seen"` are not offered (§4.1).
- **Countdown**: approval sheets carry `ttl_s`, how many more seconds the hook will wait. At 0 the dialog stays on the Mac.

### 3.3 Keys

| Key | Action |
|---|---|
| 1–6 | Pick option N directly, opens the confirm box |
| `;` `.` | Move. **Nothing is selected when the sheet opens**; the first press lands on option 1 |
| Enter | Opens the confirm box for the selected option; on the "Type a reply…" / "Type a note (queued)…" row it opens the typing page. Does nothing when nothing is selected; **Enter is always ignored for 400 ms after the sheet opens** |
| Opt+D | Dismiss this session (as in the list, online only). In v0.6.1, if cpad is holding its hook, it also lets go at once and the dialog stays on the Mac |
| `` ` `` / Backspace | Back to the list, doing nothing. The hook keeps waiting, and after the timeout the dialog stays on the Mac |

- **Why digits**: #16 asks for numbered presets (e.g. 1 continue / 2 looks good / 3 pause). There are no "projects" in a choice sheet, so here a digit means "which option". This is the exception to principle 7, valid only inside choice sheets (§17 item 2).
- **Why nothing is preselected**: in v0.4, pressing Enter on a "Waiting for you" row opens a harmless typing page, and you are used to pressing it repeatedly. If the sheet opened on option 1 (the most permissive "Allow this once" or "Continue"), Enter, Enter, wait 400 ms, Enter would approve. Now you either press a digit (an explicit choice), or press `;` `.` first.
- **Menu**: "This page" says "1-6 pick this option (press 1-6)"; Enter on it does not execute, consistent with docs/13 §6.1 "rows for a group of keys are for information only".

### 3.4 Confirm box (keys by risk)

| Option | What the confirm box says | Accept key |
|---|---|---|
| Allow (high risk: commands, network, files outside the project, sensitive files inside the project); approve a plan | Shows the object again, plus "this once only"; for a plan, "Only saw the first 3 lines (N in total)" | **Y** (Enter does nothing) |
| Allow (low risk: editing ordinary files inside the project); allow all for this session; answer a question; preset reply | The option and the object | Enter |
| Deny; don't approve the plan | The option | Enter |

- All have the 400 ms debounce [src confirm.cpp:56]; `` ` `` cancels.
- New key layer `Layer::ConfirmHigh`: `y` → Confirm, the back key and `` ` `` → Cancel. Y is an ordinary letter and produces no ghost keys; `test_keymap_static` checks this [inf].
- **The menu cannot press Y for you**: when the Tab menu is opened on ConfirmHigh, the "Y Allow" row under "This page" is **for information only** (`ev.kind = None`), with "press Y directly" at the end of the row. Otherwise Tab → select this row → Enter would replay `y` and bypass "only Y counts". This is an exception to docs/13 principle 4 (a menu row presses the key for you), recorded in the differences table; `test_menu_model` and `test_menu_dispatch` get assertions.
- `ConfirmKind::Pick` is **not queued** offline; it shows "Offline, not sent" (`Route::Unavailable`). Options and tokens only mean something at that moment.

### 3.5 Offline and errors

| Situation | What the device does |
|---|---|
| Offline | "Waiting for you" shows the last snapshot, marked "○ offline". Enter opens a small page: "Offline. Connect to the Mac to choose", with a row "Type a note (queued)…" below it, also not preselected. Choice sheets, approval and presets are unavailable |
| Host has no `caps.choose` (old cpad) | Enter takes today's path |
| The in-flight lane is busy | Shows "Mac is busy, try again shortly" (docs/13 §10.2) |
| `agent.pick` returns `E_NOT_REPLYABLE` | Shows "Things changed, not sent", closes the choice sheet, and the list refreshes with the snapshot |
| No reply to `agent.pick` within 30 s | Shows "Mac didn't answer; not sure it was delivered". **No retry** |
| Locked | "Waiting for you" is not visible. The lock card only says "Waiting N". Unlock first to choose |

### 3.6 When things are cleared

The choice sheet (`ChoiceSheet`) and the to-do page (`TodoPage`) live only in memory and are only valid while open. If any of the following happens, the device clears them at once and goes back to the list:

- The link leaves Ready (cable pulled, timeout, cpad restart);
- Lock;
- The host `hello` received has a different `host_id` from last time (a different Mac);
- The choice sheet has been open for more than 120 s (the token has expired too).

Choice sheets contain commands and paths, so they must not be shown again after locking or after switching to another Mac.

## 4. Preset replies (#16)

### 4.1 Configured on the Mac: `$CPA_HOME/replies.json`

```json
{"version": 1,
 "default": [{"label": "继续"}, {"label": "看起来不错，接着做", "when": "seen"}],
 "projects": {"cpa": [{"label": "跑全部测试", "text": "跑全部测试，修好失败的再停。"}]}}
```

(The example labels are Chinese: "Continue", "Looks good, keep going", and for project `cpa` "Run all tests" with text "Run all tests, fix the failures, then stop.")

- **Rules** (pure function `parse_replies()`, strict validation):
  - `version` must be 1; no extra fields at the top level or in any item, so a token cannot fit in structurally;
  - `default` has at most 5 items; `projects.<key>` has at most 5 items and **replaces the default for that project as a whole**;
  - `label` 1–36 B, no control characters, `"` or `\`;
  - `text` optional, defaults to `label`, must pass `check_text()` (1–500 B, no control characters, does not start with `-` after trimming) [src argv.py:22-27];
  - `when` optional, with only one value, `seen`: offer this item only when the choice sheet has a `tail` (you can see what it last said). Something like "Looks good" should not appear when you have not seen the content;
  - file mode 0600.
- **Why not in `projects.json`**: `dump_registry()` only writes known fields [src registry.py `dump_registry`], so an added field would be wiped by the next `cpa project add`.
- **Command line** (typed on the Mac, not on the device): `cpa reply ls [--project k]`, `cpa reply add [--project k] <label> [--text …] [--seen]`, `cpa reply rm`, `cpa reply reset`, `cpa reply status` (whether it is usable, and why).
- **Built-in defaults** (when the file does not exist): Continue / Looks good, keep going (`when:"seen"`). `text` always equals `label`; Claude understands Chinese. The draft's "Pause, wait for me" was removed: sending that to an idle session is just one stop + resume (with fork risk and a model turn), with the same effect as dismissing with Opt+D.

### 4.2 The device sends only a number

1. The device sends `choice.get{sid}`, and cpad replies `choice.sheet{tok, kind:"reply", lines, opts}`.
2. `tok` binds: the sid, the session's `(status, since)` at that moment, the hook state's `at`, the first 8 hex digits of the sha256 of `msg` and `tail` together, the content hash of `replies.json`, and the order of presets on this sheet.
3. The device sends `agent.pick{sid, tok, n}`. cpad checks one by one:
   - the token exists, is unused, and is no older than 120 s;
   - the session is still in the list, background, idle, and `since` has not changed;
   - the hook state's `at` and the excerpt hash have not changed. A turn finished between two polls is invisible to `since`, but there will be a new Stop hook, which changes `at` and `tail`;
   - the hash of `replies.json` has not changed.
4. Only when everything matches is it expanded into text and handed to `adapter.reply()`.
5. Record `agent.replied{sid, preset:n}`. No body text is recorded; the existing record just gains one optional field.

### 4.3 Delivering safely to the session (fork risk)

- **Only to background, idle sessions**. Interactive, running or waiting sessions are never resumed: on 2.1.306, `--resume` on a session that is still running opens a separate copy [research].
- At pick time cpad **lists again**; status must be idle and `since` unchanged, otherwise it returns `E_NOT_REPLYABLE`.
- argv as before: `stop <short>`, then `--bg --resume <full sessionId> -- <text>`, **with no other flags** [meas G3 4b].
- Fork detection as before; any one of three signals counts: "started a copy" in stderr; the id on stdout is not the original short id; a new sessionId with the same name appears in `--all`.
- **After a fork, stop the copy first**: the copy's id comes from stdout or the difference in `--all`; cpad immediately runs `claude stop <copy>` and records `agent.reply_forked{sid, copy_stopped}`; only then does it return `E_REPLY_FORKED`, `caps.reply` becomes false for the rest of this run, and the device's reply sheet becomes `none/no_reply`. Otherwise the copy would run "Continue" in the same worktree alongside the original session. If it cannot be stopped (copy id not found, or stop fails), record `copy_stopped:false`, and the device shows "A copy appeared; check on the Mac".
- **G3′ (new gate, before v0.6.0 ships, 👤 about 5 minutes)**: on a logged-in 2.1.306, run the G3 script from cli-gates.md §7. Three things must be seen: the model really replied; the ID did not change; no "started a copy" in stderr. Once it passes, write this CLI version into cpad's constant `VERIFIED_CLI["reply"]`.
- **On by default or not** (§17 item 3): the default of `experimental_bg_reply` becomes `"auto"`: on only when this Mac's `claude --version` has **the same major and minor version as the one G3′ verified, and is not lower**; otherwise off, with the reason in `cpa reply status` ("CLI 2.2.0 not verified; rerun G3′"). You can also fix it to `true` or `false` in `config.json`. If G3′ fails, the constant stays empty, and `"auto"` means off.
- Queued feedback is still not delivered automatically. "Confirm a resend on the device once the session is idle" goes into the backlog.

### 4.4 Interactive sessions

Still "handle it on the Mac". cardputer-cli-buddy uses mods' `$.prompt.submit` to inject text into the terminal as the user [src 02 §3 line 73, §4 line 93]. That is a new feature from 2026-10, and it would type arbitrary text into the terminal you are using. Not in v0.6; it goes into the backlog.

## 5. On-device approval: permission, plan, question (v0.6.1, off by default)

v0.6.1 changes only the Mac side: the protocol, choice sheets and ConfirmHigh are all in v0.6.0 already (§9, §10), and the device side is tested with a fake host. G-D1 (§5.7) is the first item of v0.6.1; if items a and b do not hold, v0.6.1 does not ship.

### 5.1 Flow

```
Claude Code: a background session wants to use Bash and raises a permission dialog
 └ PermissionRequest hook: cpa hook --decide (stdin is the tool arguments, reads at most 1 MiB)
    1. Forward the allow-listed event as before (≤0.5 s, the old path from #33)
    2. brief(): compute the summary and risk facts. The full arguments are only seen in the hook process
    3. Connect to cpad.sock (connect ≤0.2 s), send {cmd:"decide"}
    4. Phase 1: must receive {held:true} or {pick:null} within 0.5 s; otherwise print nothing and exit
    5. Phase 2 (only after held): wait at most another 110 s for {pick:…}
cpad:
    Conditions not met → reply {pick:null} at once → hook prints nothing → the dialog appears on the Mac as usual
    Conditions met → record in the waiting book, reply {held:true}; this row is need=true, why from the waiting book
Device:
    Waiting "!Permission" → Enter → choice.get → choice sheet → 1 → press Y in the confirm box → agent.pick
cpad:
    Token matches → send the hook {pick:{act:"allow"}} → agent.result ok → record agent.decided
hook:
    Print fixed-shape JSON (§5.5), exit 0 → Claude Code runs the tool
```

**Why two phases**: the hook runs for every permission request of every session (the matcher is `*`). If cpad's event loop got stuck, a single-phase hook would wait the full 110 s before letting go; if the terminal dialog only appears after the hook ends [open G-D1 f], every permission request in your terminal would be held up for almost 2 minutes. With two phases, a stuck cpad delays things by at most 0.7 s.

**When cpad replies null at once** (any of):
- `decide` is off, or the decide handler did not pass verification (§5.8);
- the CLI version on this Mac is outside the verified range, or this hook input's shape differs from what G-D1 captured (§5.8, `reason:"shape"`);
- the device link is not Ready, or the device's `hello.feats` has no `choose` (old firmware, §9.6);
- the device reports that the current profile does not belong to this Mac (docs/18 §7.1 D9, `reason:"other_profile"`);
- the session is not a background session (v0.6.1 only handles background sessions, §5.7 f);
- the session does not belong to a registered project, or the hook's `cwd` does not match this session's `cwd` in the list;
- the summary is marked `unsupported` (§5.2);
- the request comes from a subagent (the hook input has `agent_id`) [assume, G-D1 m];
- **Duplicate**: the same `(sid, tool_use_id)` is already waiting (for example, the handler is installed in both the user and project settings); the later one gets null (`reason:"dup"`) and the first keeps waiting. If the hook input has no `tool_use_id` [open G-D1 n], judge by "same sid, same tool, same summary hash, within 2 s";
- a **different** new request arrives for the same session: the old one gets null first, and only the newest is kept (a session has only one dialog at a time).

**While waiting**:
- Device disconnects: null for all waiting items within 1 s;
- cpad's 100 s limit is reached: reply null and record `agent.decide_fallback{sid, reason:"timeout"}`;
- You press Opt+D on this row on the device: `inbox.dismiss` reaches cpad, which immediately replies null to this sid's waiting item (`reason:"dismissed"`), so there is no need to wait out the 100 s;
- Claude Code ends the hook (for example you answered in the terminal, or the handler timed out): the connection drops, the waiting book removes the item, later picks return `E_NOT_REPLYABLE`, and the row on the device becomes `none/released`.

**Timeouts**: connect 0.2 s, phase 1 0.5 s; for phase 2, cpad 100 s < hook 110 s < the handler `timeout` of 120 s in settings. So cpad always lets go first.

### 5.2 The summary `brief()` (runs only in the hook process)

The **object** (`obj`) is the very thing you are asked to approve, sent to the device **verbatim** using the new `brief_obj()`, which never removes characters:
- ASCII printable characters only (0x20–0x7E), single line, ≤128 B (shown on the device on two lines, each ≤64 B);
- not a single redaction rule (below) may match;
- if any of these fails, the item is marked `unsupported` and handled on the Mac.

The ASCII limit is deliberate: the device font fully covers ASCII, while non-ASCII characters may lack glyphs or be look-alikes, so what you see would not necessarily be what runs.

| Tool | Object `obj` | Note line `note` | Risk |
|---|---|---|---|
| Bash | The full command, verbatim | "Claude says: " + `description` (written by the model, through `excerpt()`, ≤64 B, only when present) | High |
| Edit / MultiEdit / Write / NotebookEdit, file inside the project and not on the sensitive list (§5.3) | Relative path (paths inside the project may use printable BMP characters, also with nothing removed; if that is not possible, `unsupported`) | "Edit N lines" or "New file, N lines" | **Low** |
| Same, but the file is a sensitive file inside the project, or outside the project | Absolute path starting with `~/`, verbatim | Same | High |
| Read / Grep / Glob / LS (only reads outside the project ask) | Path or search term, verbatim | — | High |
| WebFetch | The full URL, verbatim (not just the host: the query string may carry data being sent out) | — | High |
| WebSearch | Search term, verbatim | — | High |
| `mcp__<server>__<tool>` | — | — | `unsupported`: argument shapes vary and cannot be shown in full |
| ExitPlanMode | — | The first 3 non-empty lines of the plan (`excerpt()`, each ≤64 B), `total` = total lines of the plan | High (approving a plan) |
| AskUserQuestion | — | The question (≤128 B, two lines); option labels (each ≤36 B, at most 4); only 1 question with single choice is supported, otherwise `unsupported` | Low |
| Anything else | — | — | `unsupported` |

- Plans, questions and descriptions are text written by the model and are not executed, so they can be cleaned with `excerpt()`; **the object never goes through `excerpt()`**. The review found that `excerpt()` would show `rm -rf *` as `rm -rf`, and `` echo `id` `` as `echo id` [src hooks.py:32-33]; that is exactly how the draft let "what you see" and "what runs" diverge.
- **Redaction** (applied to note lines; objects are only checked, and a match means `unsupported`):
  - `NAME=value` where `NAME` contains token, secret, passw(or)d, api_key, auth, cookie, session (case-insensitive) → `NAME=***`;
  - `--token x`, `--password=x`, `--api-key x` and the like → value replaced with `***`;
  - the value after `Authorization:` or `Bearer` → `***`;
  - `user:pass@` in URLs → `***@`;
  - ≥24 consecutive `[A-Za-z0-9+/=_-]` characters → `***`. So a command like `echo <a long base64 string>|base64 -d|sh` always stays on the Mac.
- Summaries, questions and labels are only handed to cpad over the 0600 `cpad.sock`. cpad keeps them only in memory; `events.jsonl` and logs record only the length and the first 8 hex digits of the sha256 (§13).

### 5.3 Risk tiers

- **Low**: answering AskUserQuestion; or editing a file, when all five of these hold:
  1. the tool is one of Edit, MultiEdit, Write, NotebookEdit;
  2. the realpath of the target file is inside the session's `cwd` (computed by the hook), and that `cwd` is inside a **registered project root** (cpad looks it up with `SessionInfo.cwd` from the list). The hook's `cwd` alone is not enough: `cwd` can be any directory;
  3. the project root itself is safe: not `$HOME`, not `/`, and not a parent of `$CPA_HOME`, `~/.claude` or `notes_dir`. A project root can be registered as any absolute path [src registry.py:67-68]; registered as `$HOME`, "inside the project" would include `~/.claude` and `~/.zshrc`;
  4. the relative path is not on the **sensitive list** [assume]: `.claude/**`, `.mcp.json`, `.git/**`, `.envrc`, `.husky/**`, `.vscode/**`, `.github/workflows/**`, `package.json`. Editing these is the same as changing permissions or planting code that will run automatically later;
  5. the hash of the realpath of the `cwd` the hook reports equals the hash of the realpath of this session's `cwd` in the list [src agents/base.py `SessionInfo.cwd`].
- **High**: everything else, including Bash, network access, reads and writes outside the project, files on the sensitive list, and approving a plan.
- **Who computes it**: cpad computes `tier` itself from the five rules above and does not use a tier reported by the hook. But in rule 2, "inside the project" and the realpath are computed locally by the hook and reported to cpad. The hook is our own program, running as the same user as cpad; malicious processes under the same user are out of scope anyway [src SECURITY.md]. What cpad can do is cross-check: rules 3 and 5 use its own registry and list, and the sensitive list is checked again against the object itself (when editing a file inside the project, the object is the relative path inside the project).

### 5.4 cpad's waiting book (pure function `choices/book.py`)

- `Pending{sid, tuid, tok, kind, tool, tier, obj, note, lines, labels, total, since, deadline}`, stored by sid, at most 16 at a time [assume].
- Functions: `hold`, `answer(tok, n)`, `expire(now)`, `link_down()`, `closed(sid)`, `dismissed(sid)`, `is_dup(sid, tuid, key, now)`. They return a new book and the effects to carry out (what to send back to which hook connection), without changing the original object.
- Token: `secrets.token_urlsafe(9)`, 12 characters, void after one use.

### 5.5 The hook only prints a few fixed shapes [open, finalised by G-D1]

```jsonc
// Allow once
{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"allow"}}}
// Deny (message is a constant in the hook; "stop and wait for me" adds "interrupt":true once G-D1 l confirms it)
{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"deny","message":"…"}}}
// Answer AskUserQuestion (updatedInput = the tool_input the hook read itself, plus answers)
{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"allow",
  "updatedInput":{"questions":[…unchanged…],"answers":{"<question text>":"<selected label>"}}}}}
// Allow all for this session: hidden by default; shape in §5.6
// Everything else: print nothing → the dialog appears on the Mac as usual
```

- **Where strings come from**: either constants in the hook (denial messages), or the `tool_input` the hook read itself (question text, selected label). cpad sends the hook only three things: `act` (allow / always / deny / answer), `i` (option index) and `m` (denial message number). So even if another process impersonated cpad, the most it could do is make the hook print one of these shapes. Malicious processes under the same user are out of scope anyway [src SECURITY.md].
- **Denial messages** (Chinese constants):
  - `stop`: "用户在 Cardputer 上拒绝了这一步。先停下，等用户的下一条消息。" ("The user denied this step on the Cardputer. Stop and wait for the user's next message.") Once G-D1 l confirms that `interrupt:true` makes the session stop, add it too, rather than relying only on the model to comply;
  - `alt`: "用户在 Cardputer 上拒绝了这一步。换一个不需要这一步的办法。" ("The user denied this step on the Cardputer. Find a way that doesn't need this step.")
  - `plan`: "用户还没批准这个计划。继续完善计划，先不要开始实现。" ("The user hasn't approved this plan yet. Keep refining the plan and don't start implementing.")
- Handlers without `--decide` still "never print".

### 5.6 "Allow all for this session" and approving plans

- **"Allow all for this session" is hidden by default**. It is added for low-risk file edits only if G-D1 i confirms both of these:
  1. how absolute-path rules are written (my inference is the double-slash form `Edit(//<dir>/**)` [inf]). The scope is the **verified session `cwd`** (§5.3 rules 2 and 5: inside a registered project root and matching the list), the same directory used to judge low risk; not the file's directory, and not an unverified path the hook reports itself;
  2. the same `updatedPermissions` can add several session-level `ask` rules covering the sensitive list (§5.3 rule 4), and `ask` takes precedence over this `allow`. Otherwise, after "allow all", edits to `.git/hooks/*` would not ask again either.

  If either does not hold, this option is not built, and every low-risk file edit comes back to the device for one Enter. The rule is written only with `destination:"session"` and **never into any settings file**; high risk never gets an "always allow". The option label is "本 session 都允许改项目文件" ("Allow project file edits for this session", 36 B, exactly the `label` limit).
- **Approving a plan**: only "Approve, file edits still ask" is offered, not "Approve and auto-accept edits". The confirm box says "Only saw the first 3 lines (N in total)" and needs Y. Which mode the session enters after approval must be measured in G-D1 h [open]:
  - if it measures as "still asks": every later file edit comes back to the device;
  - if it measures as "auto-accept edits": the confirm box is reworded to "Approving lets it edit files directly", still with Y.

### 5.7 Gate G-D1 (the first item of v0.6.1, 👤 about 20 minutes in your logged-in terminal)

Following cli-gates.md: each section in a temporary empty directory, `plan` or `manual` mode, harmless prompts, `stop` plus `rm` at the end. cpad is replaced by a small test service that only returns fixed answers, with no device. The captured hook inputs are saved as `daemon/tests/fixtures/hooks/permission-request-2.1.306-*.json`, and later unit tests use them.

| # | To confirm | If it does not hold |
|---|---|---|
| a | Hook prints nothing → the dialog appears as usual | **v0.6.1 does not ship**, v0.6.0 unaffected |
| b | The allow shape → the tool runs | Same |
| c | deny + message → the model sees the message | Deny without an explanation |
| d | What `agents --json` reports as status for a background session while the hook waits | The waiting book is already the truth (§3.1); this only decides whether `hook_state` needs more changes |
| e | After the handler times out: the dialog is still there, and the session waits until someone runs `claude attach` | Write the measured behaviour into §5.1 |
| f | Interactive terminal: while the hook runs, is the dialog shown at the same time (whoever answers first wins), or only after the hook ends | Shown at the same time: `decide_interactive` (off by default) can be added later; only after the hook ends: interactive sessions are never handled |
| g | AskUserQuestion: allow plus `updatedInput.answers` → the model receives the answer | Question sheets become `none/mac_only` |
| h | Which mode after ExitPlanMode allow | See §5.6 |
| i | The shape of `updatedPermissions`, how absolute-path rules are written, and whether session-level `ask` rules override `allow` | "Allow all for this session" is not built |
| j | Can the handler `timeout` be set to 120 s; does the hook input contain `cwd` | Shorten the timeouts to what is measured; without `cwd`, treat everything as high risk |
| k | When a background session is open in `claude attach`, does the terminal dialog also wait for the hook | Yes: the waiting limit for background sessions drops from 100 s to 20 s, stated in `cpa decide status` |
| l | Does the session stop when deny carries `interrupt:true` | No `interrupt`; rely only on the denial message |
| m | What a subagent's permission request looks like (`agent_id` set), and where its dialog appears | Keep "subagents always get null" |
| n | Does the hook input contain `tool_use_id` | Deduplicate by "sid + tool + summary hash + 2 s" instead |

### 5.8 Switches, handlers and CLI versions

- **Handler verification**: at cpad startup and whenever a settings file changes, read `~/.claude/settings.json` and `.claude/settings*.json` in registered projects, and confirm the decide handler exists, in exactly one place. `caps.decide` is true only when `decide:true` **and** the handler verification passed.
- **Survives reinstall**: today `cpa hooks install` first deletes every handler whose `args` start with `hook`, then installs the plain one [src hook_settings.py:78-83, 119-127], so running it again would quietly replace the decide handler. Change it so that `install` reads `decide` from the config and, if it is on, installs the decide version.
- **Both settings files have it installed**: `cpa decide status` and `cpa hooks install` both warn; the waiting book deduplicates as in §5.1, so one dialog is taken only once.
- **CLI version**: `VERIFIED_CLI["decide"]` records the version G-D1 verified. This Mac's version counts as usable only with the same major and minor version and not lower; `cpa decide status` says "newer than verified; consider rerunning G-D1" when the version is newer. In addition, every request runs a **shape self-check**: if the hook input lacks a required field G-D1 captured, or has an unknown `decision`-related field, reply null (`reason:"shape"`). Claude Code updates often; accepting only one exact version would turn approval off at every update.

## 6. Confirming meeting to-dos one by one

### 6.1 Where the data comes from, and where it goes

- **Source**: `actions[]` in `result.json`, treated as untrusted input, with `results.py`'s validation (≤64 KB, only known fields read) [src docs/11 §6.6]. Only meetings with `state:"done"`, `level:"summary"` from the last 14 days [assume].
- **`ref`**: `<rec>.a<n>.<sha6>`, where `sha6` is the first 6 hex digits of the sha256 of this to-do's original text. If `result.json` is regenerated and the text at the same index changes, the old decision no longer matches, and the item counts as "to confirm" again; a decision never lands on a different item.
- **Displayed text**: the original is first cleaned by `router.queue_text()` (control characters replaced with spaces, leading and trailing whitespace trimmed), then cut to 96 B without splitting a character, with "…" appended if cut.
- **Four actions**. T, Q and Opt+G all go to docs/14's router `router.on_capture()` with `Capture{ref, source:"voice", dest, how, project, text}` and `origin:"confirmed"` (you just pressed it on the device). `source` is recorded as `voice` because the text comes from transcription; so anything queued is always `uncertain=true`:

| Key | Action | Destination | Written to | Events |
|---|---|---|---|---|
| T | My to-do | `todo`, `how:"key"` | Appends a line to **today's** daily note, in exactly v0.4.1's to-do line format `- [ ] HH:MM #proj text ^cpa-<rec>-a<n>`, where text = the cleaned full text plus "（[会议：标题](recordings/…/summary.md)）" ("(meeting: title)" link) | `todo.mine{ref, rec, n, text, project, title, dir, at}` (a content event, carrying the body like `item.received`, for `NotesWriter` to write the line); the router records `dest.decided`; once written, `todo.written{ref}` |
| Q | For AI | `queue`, `how:"key"` | — | The router records `dest.decided` and `queue.added{origin:"dest", from_item:ref, uncertain:true}`; the body is **the part shown on the device** (§6.3) |
| Opt+G | Send to another destination | The one you pick from the list, `how:"pick"` | Decided per docs/14 §5 (clipboard, file, etc.) | The router's `dest.*` events; queue-type destinations also use only the shown part |
| Opt+D | Skip | — | No file written | `todo.skipped{ref}` |

- **`summary.md` is not touched**: it belongs to WhisperWoof [src recording contract v1 single-writer table; docs/13 §8.4]. So the same to-do is still `- [ ]` in `summary.md`. Two counts must subtract the handled ones (M8):
  - `rec.items.todo_n` = the number of unticked items in `summary.md` **minus** the items already handled for this meeting, at least 0 [src v0.4.1 recordings/service.py:235-241];
  - docs/15's `list_todos` adds `decided` to meeting items, and `status=open` no longer returns handled ones (§12).
- **Why through the router**: docs/14 §1.2 left "sending meeting-summary to-dos out one by one" for v0.6; with the same router, `cpa audit` shows where every item went, and there is only one set of cleaning rules (`queue_text()`: text starting with `-` is downgraded, nothing else removed). When the router decides to downgrade (e.g. `bad_text`), the device shows "Not sent: <reason>", and the item stays "to confirm".
- **Idempotency**:
  - the first decision for each `ref` wins, **except skip**: skip writes nothing, so it can later become T, Q or Opt+G;
  - sending the same decision again returns `ack`, so retries are safe;
  - a different decision (other than skip) returns `E_BAD_FIELD` (`field:"op"`), and the device refreshes.
- **Restarts do not bring back lines you deleted**: `NotesWriter` replays the whole history on every start and only checks whether the block ID is in the file [src writer.py:55-71; app.py:118-122]. So once the `todo.mine` line has been written, record `todo.written{ref}`; on replay, skip `todo.mine` events that already have `todo.written`, and only fill in those where "the event was recorded but the line was not written". A line you deleted in Obsidian does not come back at the next start. (Ordinary note lines have the same problem; that is existing behaviour, goes into the backlog, and is not changed in this version.)
- **Changing your mind**: for my to-dos, tick it in the to-do app or delete the line in Obsidian; for AI items, mark done with Opt+D under "For AI"; for skipped items, open the meeting in the recordings app and decide again.

### 6.2 Flow and keys

- **Entry points**:
  - the "N meeting to-dos to confirm" row in "Waiting for you" (N comes from `queue.snapshot.review`);
  - the "To confirm" tab of the to-do app;
  - Enter on the recordings app's detail page, for this meeting only, listing skipped ones too (in v0.4.1 Enter on the detail page does nothing [src docs/13 §8.2]).
- **One at a time**: after a decision it moves to the next automatically; **for 400 ms after moving, T, Q, Opt+D and Opt+G are ignored**, so a double press does not decide two items. `` ` `` is "later" and records nothing.
- **Choosing a project after Q**: the default is the project the meeting was recorded under, and Enter uses it; 1–9 picks another directly (digits = projects, principle 7); if the meeting has no project, you must choose one.
- **Opt+G**: `lib/ui/pick_list` lists the available destinations from `dests.snapshot` (the same list as docs/14 §10.1); `;` `.` to select, Enter to confirm. Nothing preselected. For destinations that need a project, choose the project next.
- **No confirm box**: none of these actions executes anything. Anything queued still needs your confirmation on the device before a session is opened. docs/11 §1.2 says "AI to-dos must be confirmed on the device before entering the queue"; this page is that confirmation.

### 6.3 Text from meetings is untrusted

Meeting to-dos come from transcription and an LLM, and may contain what other people said. So:

- **What gets queued is what you saw**: Q (and Opt+G with a queue-type destination) queues only the part shown on the device (≤96 B). When the original is longer, the page shows one more line at the bottom: "Q queues only what's shown; full text is in the summary". T writes the full text into the note: a note is only a record and is never executed.
- **The source is visible when opening a session**: the summary cpad generates for `queue.snapshot` starts with "会议·" ("Meeting·") for queue items from meetings, so it is visible in the confirm box for opening a session.
- **Always plan mode**: a session opened from a meeting queue item always uses `plan`, whatever `spawn_permission_mode` the project is set to [src registry.py `DEFAULT_MODE`].
- The destination is decided only by the key you press, never by the text (docs/14 §5.4 item 3): if someone in the meeting says "send it to Slack", nothing happens.

## 7. The to-do app (Opt+W)

### 7.1 Three tabs

| Tab | Content | Source | Offline |
|---|---|---|---|
| Mine | Unticked checkboxes. Items just ticked stay in the list and disappear once you leave the page | Lines with a `^cpa-` block ID in the daily notes of the last 30 days | Not viewable |
| To confirm | Meeting to-dos, handled one at a time | §6 | Not viewable |
| For AI | Each project's queue items | `queue.snapshot`, already on the device, at most 12 items and at most 4 per project [src limits.json] | Shows the last sync |

`,` `/` switch tabs, `;` `.` move; reaching the last row automatically requests the next page (as in the recordings app).

### 7.2 Mine: listing and ticking

- **Listing**:
  - cpad scans the `YYYY-MM-DD.md` files of the last `todo_days` (default 30) days under `notes_dir`, takes lines matching `^- \[( |x|X)\] … \^cpa-[a-z0-9-]+\s*$`, newest first;
  - the displayed text drops the time, `#project`, and links like `[音频](…)` (keeping only the link text), ≤96 B.
- **Only lines with a `^cpa-` block ID are listed** [assume]: these all come from the Cardputer, including typed, voice and meeting-confirmed ones. Checkboxes you write yourself in Obsidian have no stable ID and are not listed in v0.6 (§17 item 5).
- **`ref`**: date + block ID + the first 6 hex digits of this line's sha256, generated by cpad and sent back unchanged by the device.
- **Tick (`op:"done"`) and untick (`op:"undo"`)**: cpad does these in order on the same thread as `NotesWriter`, so they never interleave with cpad's own appends:
  1. the directory's realpath must be `notes_dir`; open the file with `O_RDWR | O_NOFOLLOW` and `fstat` to confirm it is a regular file;
  2. read the file and find the **only** line ending with this block ID; if none, return `E_UNKNOWN_ITEM`;
  3. this line's hash must equal the one in `ref`. If not, the line was just changed elsewhere (e.g. Obsidian), so return `E_BUSY`;
  4. compute the offset of the byte after `[`, and use `os.pwrite` to **write only this 1 byte** (space ↔ `x`), leaving every other byte untouched; `fsync`;
  5. read the line back to check: if it is the expected new content, success; if not (in the tiny window where Obsidian happened to rewrite the whole file), record `todo.tick_lost{ref}` and return `E_BUSY`;
  6. record `todo.ticked{ref, done}`, without body text.
- **Message on the device**: `E_BUSY` can also have other causes (the file is being written), so it always says "Not changed, refreshed" and lists again.
- **Why not rewrite the whole file**: temp file + `os.replace` has a window between reading and replacing that would overwrite whatever Obsidian saved in between. Changing 1 byte and checking afterwards shrinks that window to a single `pwrite`.
- **Why edit the line itself, rather than only recording a "done" event**: with only an event, Obsidian would still show it unticked, and the file and the device would disagree; docs/15 also treats the file as the truth [src docs/15 §3.4].
- **When Obsidian has the file open** [open]: it should notice the external change and reload [inf]. Try it once by hand in H6: with today's note open in Obsidian, tick an item on the device.
- **No ticking offline**: what changes is a file on the Mac, and the outbox only holds notes and queue items (inherited from SECURITY.md).

### 7.3 To confirm, For AI

- **To confirm**: this is the page from §6.
- **For AI**:
  - grouped by project;
  - Enter = open a session, the same path as the queue tab on the project page: confirm box → `agent.spawn` with `qid`;
  - Opt+D = done (`queue.done`);
  - beyond 12 items, the last row says "N more: cpa queue ls on the Mac".

### 7.4 How to get there

- **Opt+W**. W was reserved for WhisperWoof, which is no longer needed after #34. In both docs/14 §3.1 and docs/16 §4.2 it is a reserved first-party letter, not one of the 9 bindable ones, so it does not clash with destinations you bound in v0.5.
- **Home**: v0.4.1's first-party grid has 4 rows; row 4 only has "L Lock" and "Tab Menu", with the middle empty [src docs/13 §5.6]. "W To-dos" goes into that slot, so the first-party grid stays at 4 rows, and the row docs/16 reserves for third-party apps is unaffected.
- Opt+T is still "add a to-do" (v0.4.1). Pressing Opt+T inside the to-do app also adds one, with `how:"key"` (you explicitly pressed Opt+T), and returns to the list afterwards. `how:"app"` (the app's default destination) from docs/14 §3.2 does not apply to this app: it has no "save without pressing a key" path.
- **Why Opt+T does not open the app**: docs/14 §3.2 settled "Opt+letter = add one item to that destination", with N T Q S as the fixed four. Changing T would add an exception to the menu's "Save to" section (§17 item 4). The cost is that "to-dos" uses two letters: T adds one, W opens the app. This is not fully consistent with docs/13 principle 2 (one letter, one job), and is recorded in the differences table.
- docs/16 says the device's module list has only 8 slots [src docs/16 §10]. After v0.6 there are 6: Home, Projects, Waiting, System, Recordings, To-dos.

## 8. Screen sketches

(The device UI is in Chinese; the sketches below show the English meaning of each screen.)

"Waiting for you" (v0.6, with reasons; meeting to-dos come after the sessions that need your decision):
```
● online ↑0 !4             13:42 83%
Waiting (4)
▸!Perm  cpa/fix-login    2m bg
 !Plan  cpa/arch-design  9m bg
 3 meeting to-dos to confirm ›
  Idle  notes/weekly     1h bg
Enter pick Opt+D dismiss ;.move Tab
```

Approval: run a command (v0.6.1, high risk, nothing selected when it opens):
```
cpa/fix-login · Command · High   96s
git push origin fix/login
Claude says: run tests and push
 1 Allow this once
 2 Deny: stop and wait for me
 3 Deny: try another way
1-3 pick ;.move `back       Tab menu
```

Confirm box (high risk, needs Y):
```
Allow this command? (high risk)
git push origin fix/login
cpa · fix-login · this once only
Later commands will ask again.
Y Allow                   ` Cancel
```

Pressing Tab on it, the row under "This page" reads "Y Allow (press Y directly)", and Enter does not execute it.

Approval: edit a file inside the project (low risk; "Allow all for this session" hidden by default):
```
cpa/fix-login · Wants to edit a file
src/auth/login.py · Edit 12 lines
 1 Allow this once
 2 Deny: stop and wait for me
 3 Deny: try another way
1-3 pick ;.move `back       Tab menu
```

Approve a plan:
```
cpa/arch-design · Plan to approve  High
1. Split out brief.py, add redaction
2. Add --decide mode to the hook
3. Add 12 unit tests   (41 lines)
 1 Approve, file edits still ask
 2 Don't approve: keep refining
1-2 pick ;.move `back       Tab menu
```

Question:
```
web/landing · Asking you
Which main colour for the home page?
 1 Blue (current brand colour)
 2 Green
 3 Orange
Other answers need typing: claude attach 4c1e
1-3 pick ;.move `back       Tab menu
```

Preset replies (idle background session, v0.6.0):
```
notes/weekly · Idle 1 hour
It last said: …draft is in draft.md.
Shall I add last week's numbers?
 1 Continue
 2 Looks good, keep going
   Type a reply…
1-2 pick ;.move Opt+D dismiss  Tab
```

No options (`none`; every permission request in v0.6.0 looks like this):
```
cpa/fix-login · Waiting for permission
Device approval is off on this Mac.
Handle it on the Mac: claude attach 8f3a
 Type a note (queued)…
`back ;.move              Tab menu
```

Offline:
```
cpa/fix-login · Permission  ○ offline
Offline. Connect to the Mac to choose.
 Type a note (queued)…
`back ;.move              Tab menu
```

Meeting to-dos, one at a time:
```
Meeting to-do 2/3 · Client sync 10/21
Send Alice the quote
Owner me · Due Fri · Confidence high
T My to-do (today's note)
Q For AI → #cpa (1-9 other project)
Opt+G send elsewhere  Opt+D skip
T Q Opt+G Opt+D `later      Tab menu
```

To-dos · Mine:
```
● online ↑0 !2             13:42 83%
To-dos [Mine] Confirm3 ForAI
▸[ ] Send Alice the quote by Fri #cpa 10/21
 [ ] Buy a USB-C hub            10/20
 [x] Call the plumber           10/19
 [ ] Fix README key table  #cpa 10/18
Enter tick/untick Opt+T add  Tab menu
```

To-dos · For AI:
```
To-dos  Mine Confirm3 [ForAI]
#cpa ▸Fix the login tests      queued
      Meeting·Refactor brief.py
#web  Meeting·Home page in blue
Enter session… Opt+D done   Tab menu
```

To-dos, offline:
```
To-dos [Mine] Confirm ForAI  ○ offline
Connect to the Mac to see and tick.
"For AI" shows the last synced queue.
`back                     Tab menu
```

Home (v0.6, W in the empty slot of row 4):
```
▸P Projects 3  I Waiting !4  V Rec 2
 N Note        T To-do       R Meeting
 Q Queue AI    S Session     M System
 L Lock        W To-dos 3    Tab Menu
!Perm cpa/fix-login 2 min
```

- Glyphs: `[ ]`, `[x]`, `›` instead of characters like ☐ ☑ ⚠ that may not be in efontCN_12 [open]. Every new page is drawn once in the device tests and the simulator (`cpa-sim shots`), asserting `missing_glyphs == 0` (the approach of docs/13 §19).
- The lock card is unchanged and only shows counts.

## 9. Protocol additions (additions only, `v` stays 0)

Settled once in v0.6.0, including fields only v0.6.1 uses (`caps.decide`, `tier`, `ttl_s`, `total`, and `kind` values such as `permission`). v0.6.1 only changes the Mac-side implementation.

### 9.1 Overview

| Change | Direction | Content | Compatibility |
|---|---|---|---|
| `sessions.snapshot` rows gain `why?` | H→D | §3.1, present only when `need` | Old firmware ignores it |
| `queue.snapshot` gains `review?` | H→D | Number of meeting to-dos waiting for confirmation (count) | Old firmware ignores it |
| Host `hello.caps` gains `choose?`, `decide?`, `todo_app?` | H→D | Boolean or null. `choose`: answers `choice.get`, `agent.pick`; `decide`: on-device approval is on, the handler verified, the CLI version usable; `todo_app`: answers `todo.*`. Not called `todos`, to avoid confusion with v0.4.1's `caps.todo` ("can write to-do lines") | Old firmware ignores them |
| Device `hello` gains `feats?` | D→H | Array of words, each `^[a-z][a-z0-9_]{0,11}$`, at most 8; v0.6 firmware sends `["choose"]` | Old daemons ignore extra keys [src docs/14 §7.1, `validate.py obj()`] |
| `choice.get` (new) | D→H request | `sid` | Read-only, on the shared in-flight lane (10 s); the device checks `caps.choose` first |
| `choice.sheet` (new) | H→D reply | `sid`, `tok`, `kind`, `why?`, `tier?`, `ttl_s?`, `total?`, `obj_lines?`, `lines[≤3]`, `opts[≤6]` | **The second large reply**, see §9.5 |
| `agent.pick` (new) | D→H request | `sid`, `tok`, `n` | Execution type, on the agent lane (30 s); only sent from `confirm()`; answered with the existing `agent.result` |
| `todo.list` (new) | D→H request | `view` (mine / review), `max` (1–4), `cursor?` | Shared lane; checks `caps.todo_app` first |
| `todo.items` (new) | H→D reply | `items[≤4]`, `n?`, `next?` | **The third large reply**, same lane |
| `todo.act` (new) | D→H request | `ref`, `op`, `project?`, `dest?` | Shared lane; answered with `ack` or `error` |

**Naming**: the read-only pair is called `choice.*`, not `agent.*`. The protocol tools put every `agent.*` on the agent lane [src tools/flows.py:75-79], and the firmware requires `agent.*` to be sent only from the confirm box [src module.h:49-51]; a read-only request under `agent.*` would break both. The `approval.request` / `approval.decision` reserved in docs/05 §6.10 are not used: approval and preset replies share one shape, "fetch a sheet of options, pick by number", and those two names stay reserved, unused by v0.6.

### 9.2 Examples

```jsonl
{"v":0,"t":"choice.get","id":"d31","sid":"8f3a21c0"}
{"v":0,"t":"choice.sheet","id":"h32","re":"d31","sid":"8f3a21c0","tok":"k7Qm2xT9aB4c","kind":"permission","tier":"high","ttl_s":96,"obj_lines":1,"lines":["git push origin fix/login","Claude 说：跑测试并推送"],"opts":[{"n":1,"act":"allow","label":"允许这一次"},{"n":2,"act":"deny","label":"拒绝：先停下等我"},{"n":3,"act":"deny","label":"拒绝：换个办法"}]}
{"v":0,"t":"agent.pick","id":"d33","sid":"8f3a21c0","tok":"k7Qm2xT9aB4c","n":1}
{"v":0,"t":"agent.result","id":"h34","re":"d33","ok":true}
{"v":0,"t":"todo.list","id":"d41","view":"mine","max":4}
{"v":0,"t":"todo.items","id":"h42","re":"d41","n":7,"next":"m2","items":[{"ref":"20261021-3qzk1m7-0000c-0005.3f9a1c","text":"周五前给 Alice 发报价","st":"open","project":"cpa","day":1021}]}
{"v":0,"t":"todo.act","id":"d43","ref":"20261021-3qzk1m7-0000c-0005.3f9a1c","op":"done"}
{"v":0,"t":"ack","id":"h44","re":"d43"}
{"v":0,"t":"todo.list","id":"d45","view":"review","max":4}
{"v":0,"t":"todo.items","id":"h46","re":"d45","n":3,"items":[{"ref":"k3m9q2t-00042-0a1c.a1.7c20e1","text":"给 Alice 发报价单","st":"new","project":"cpa","day":1021,"meta":"客户同步 · 我 · 周五"}]}
{"v":0,"t":"todo.act","id":"d47","ref":"k3m9q2t-00042-0a1c.a1.7c20e1","op":"queue","project":"cpa"}
{"v":0,"t":"todo.act","id":"d49","ref":"k3m9q2t-00042-0a1c.a2.19bd04","op":"send","dest":"file.sb"}
```

(The Chinese strings in the examples are the same texts as the sketches in §8: "Claude says: run tests and push", the three permission options, "Send Alice the quote by Friday", and the meta "Client sync · me · Friday".)

On a permission sheet, the first `obj_lines` lines (1 or 2) of `lines` are the object itself (§5.2); the device draws them verbatim and draws them again in the confirm box; note lines come after.

### 9.3 Field rules

| Field | Rule |
|---|---|
| `tok` | Opaque, `^[A-Za-z0-9_-]{1,16}$`, sent back unchanged by the device, void after one use |
| `kind` | `^[a-z]{1,12}$`, known values `reply`, `permission`, `plan`, `question`, `none`. The device treats unknown values as `none` |
| `why` (`choice.sheet`) | Only present with `kind:"none"`; values in §3.2, `^[a-z_]{1,12}$` |
| `tier` | `low` / `high`, only on `permission`, `plan`, `question` sheets |
| `ttl_s` | 0–255, how many more seconds the hook will wait |
| `total` | 0–65535, only on `plan` sheets: how many lines the plan has |
| `obj_lines` | 0–2, only on `permission` sheets: how many of the first `lines` are the object to approve |
| `lines` | ≤3 items, each 1–64 B UTF-8, no control characters (may contain `"`, common in commands) |
| `opts` | ≤6 items, `x-uniqueBy: n`; `n` 1–6; `act` is `^[a-z]{1,8}$`, known values `reply`, `allow`, `always`, `deny`, `answer`; `label` 1–36 B, no control characters. The device picks the confirm box's accept key from `act` and `tier`, and does not show items with an unknown `act` |
| `view` | `^[a-z]{1,8}$`, known values `mine`, `review` |
| `ref` | Opaque, `^[A-Za-z0-9_.-]{1,48}$`, generated by cpad, sent back unchanged by the device |
| `todo.items[]` | `ref`, `text` (1–96 B), `st` required; optional `project`, `day` (MMDD, 101–1231), `meta` (≤32 B, "meeting title · owner · due" assembled by cpad). `st` is `^[a-z]{1,8}$`: open / done on the mine page, new / mine / queue / sent / skip on the review page |
| `op` | `^[a-z]{1,8}$`, known values: `done`, `undo` on the mine page; `mine`, `queue`, `send`, `skip` on the review page. A pattern, not an enum |
| `dest` | Only together with `op:"send"`, docs/14's `DEST_ID` pattern, ≤24 characters |
| `cursor` / `next` | Same as `rec.*`: `^[A-Za-z0-9_-]{1,32}$`, opaque [src docs/13 §12.2] |

### 9.4 Error codes (none new)

| Situation | Reply |
|---|---|
| sid not in the list | `choice.get` → `error E_UNKNOWN_SESSION`; `agent.pick` → `agent.result E_UNKNOWN_SESSION` |
| Token unknown, used or expired; session state changed; another turn finished; hook already let go; presets changed | `agent.result E_NOT_REPLYABLE` |
| `n` out of range; `agent.pick` itself invalid | `agent.result E_BAD_FIELD`. Today `hub/reduce.failure_reply()` only answers `agent.result` for `agent.spawn` and `agent.reply` [src reduce.py:207-226]; `agent.pick` must be added |
| A preset reply forked | `agent.result E_REPLY_FORKED` (existing) |
| A pick with the same token is being processed | `agent.result E_BUSY` |
| `ref` not found; the meeting to-do's original text changed (`sha6` mismatch) | `error E_UNKNOWN_ITEM` |
| The line was just changed elsewhere, or the file is being written | `error E_BUSY` |
| `op` does not apply to this `ref`, or a different decision was already made | `error E_BAD_FIELD`, `field:"op"` |
| `dest` unknown or unavailable | `error E_BAD_FIELD`, `field:"dest"` |
| `project` not registered | `error E_UNKNOWN_PROJECT` |
| Writing the file failed | `error E_INTERNAL` (`msg` without paths) |

### 9.5 Line lengths [calc]

Computed with `json.dumps(ensure_ascii=False, separators=(",",":"))` plus a newline; `id` and `re` at 24 characters, numbers at their maximum, all optional fields filled.

| Line | How it is built | Bytes |
|---|---|---|
| `choice.sheet` worst case | 3 note lines of 64 `"` each (doubled by escaping), 6 option labels of 36 `"` each, `tok` 16, `kind`/`why` 12, with `total`, `ttl_s`, `obj_lines` | **1290** |
| `choice.sheet`, Chinese | Notes 3 × 21 Chinese characters, labels 6 × 12 Chinese characters | 879 |
| `todo.items` 4 items worst case | `text` 96 `"`, `meta` 32 `"`, `ref` 48 characters, `next` 32 characters | **1699** |
| `todo.items` 4 items, Chinese | — | 1179 |
| `todo.items` **5 items** worst case | — | 2086 (over the limit, hence at most 4 items per page) |
| `choice.get` / `agent.pick` / `todo.list` / `todo.act` (with a 24-character `dest`) | — | 74 / 105 / 126 / 187 |
| `agent.result` with `E_NOT_REPLYABLE` and a 16-character `why` | — | About 165, ≤ `host_reply_max` 256 |
| `sessions.snapshot` 10 rows worst case, **without** `why` | Names all 32 `"` | 1998 |
| Same, **with** `why` | — | 2188; `fit_list` cuts it to 9 rows, `total` still the real count [src snapshots.py:43-46] |
| Same, with 32 B ASCII names (the common `<key>/<slug>` case) | — | 1868 |
| Extra `feats` in the device `hello` | 8 words of 12 characters | 130 (added to the device `hello` worst-case line; recomputed in P6) |
| **Control socket** `decide` request (v0.6.1, `json.dumps` with default arguments, non-ASCII escaped as `\uXXXX`) | permission: `obj` 128 `\`, `note` 64 B; plan: 3 × 64 B plus `total`; question: 2 × 64 B plus 4 × 36 B labels; `sid` 36, `tuid` 64, `tool` 64 | 706 / 716 / **888**, ≤ `REQUEST_MAX` 1024 [src control/server.py:27] |

- **Receive buffer**: `choice.sheet` and `todo.items` are both replies to requests on the shared lane. That lane has only one request in flight at a time, so the device receive buffer's worst case is still 4096 + 5×256 + 2048 = 7424 B, under 8192 B [src rec_rules.py `LARGE_HOST_REPLIES`]. `rec_rules.LARGE_HOST_REPLIES` gains these two types; `APP_REQUESTS` gains `choice.get`, `todo.list`, `todo.act`. `agent.pick` is not in `APP_REQUESTS`: it is on the agent lane, and its reply is the small `agent.result`.
- The three shapes of the `decide` request validate their fields separately (permission only allows `obj`, `note`; plan only `lines`, `total`; question only `lines`, `labels`), so the three worst cases never add up. `test_decide_size.py` guards the 888 B.

### 9.6 Mixing old and new

| Combination | Result |
|---|---|
| New cpad + old firmware | Old firmware ignores `why`, `review` and the extra `caps`, and sends no new requests. Old firmware never picks, so cpad only makes the hook wait when the device's `hello.feats` contains `choose`; otherwise it always replies null at once (`reason:"old_fw"`) |
| Old cpad + new firmware | No `caps.choose`, `caps.todo_app`: Enter takes today's typing path; the to-do app shows "cpad on the Mac needs an upgrade", and the "For AI" tab works as usual |
| v0.6.0 cpad + v0.6 firmware | `caps.decide` absent or false; permission sheets are `none/no_decide` |
| v0.6.1 cpad with `decide` off | `caps.decide:false`; same as above |

### 9.7 Merge constraints

- The firmware's `test_codec_vectors` asserts that the number of schemas equals `kMsgTypeCount + 1`; the daemon's `test_every_message_type_has_a_validator` requires schemas and `codec.KNOWN_TYPES` to match one to one [src docs/14 §7.8].
- So the 6 new schemas (P6) **must land on main in the same batch as the firmware codec registration (F6-0)**.
- The protocol tools change in the same batch: `check_vectors.EXEC_KINDS` gains `agent.pick`, and `outbox.item` gains an invalid case with `kind:"agent.pick"` (`protocol/README.md` item 4 requires one for every execution kind) [src check_vectors.py:61, README.md:37]; the lane classification in `flows.py` needs no change (`choice.*` and `todo.*` fall into misc, `agent.pick` into agent).
- Before merging, snapshot the vectors of v0.4.1 and earlier into `fixtures/v041_cases.sha256`, so `test_additive.py` guards that the old cases are unchanged [src test_additive.py].

## 10. Firmware

v0.6.0 finishes the device side in one go, including the high-risk confirm box approval needs; v0.6.1 only touches firmware if HIL finds problems.

### 10.1 Modules and components

| Part | What it does |
|---|---|
| `modules/inbox` (changed) | Shows the reason on rows; Enter: with `caps.choose`, send `choice.get`, otherwise take today's typing path; offline, open the small "Connect to the Mac to choose" page; the choice sheet is the second layer of "Waiting for you"; the "N meeting to-dos" row opens the "To confirm" tab of the to-do app |
| `lib/ui/pick_list` (new, pure function) | A selection list with key names: draws rows, handles digits and `;` `.`, works out how many rows fit in 240 px; **supports "nothing selected"** and the 400 ms Enter guard after opening. If v0.5 has already extracted a shared list component, add these two to it |
| `modules/todo` (new) | The to-do app's three tabs, the one-at-a-time confirmation page (including the 400 ms guard against double presses after skipping), project selection, destination selection. Written to docs/16's App SDK, passing `expect_app_contract` and `check_apps.py` |
| `modules/home` (changed) | "W To-dos N" in the empty slot of row 4; "the most urgent item" carries its reason |
| `lib/shell/confirm` (changed) | `ConfirmKind::Pick`: picks `Layer::Confirm` or `Layer::ConfirmHigh` by `act` and `tier`; not queued offline |
| `lib/help/menu_model` (changed) | On the ConfirmHigh layer the accept row is for information only (`ev.kind = None`), ending with "press Y directly" |
| Runtime (changed) | A shared decode area for large replies (`RecPage`, `ChoiceSheet`, `TodoPage` share one block of static memory); the result of `agent.pick` goes to the app that started it; the clearing triggers in §3.6 (link leaves Ready, lock, `host_id` changes) |

### 10.2 Interface additions (additions only, all with defaults)

```cpp
// module.h
enum class RequestKind : uint8_t { QueueDone, InboxDismiss, RecList, RecAct, ChoiceGet, TodoList, TodoAct };
// agent.* is still not here: agent.pick is only sent from confirm() (the rule in module.h:49-51 is unchanged)
enum class ConfirmKind : uint8_t { Spawn, Feedback, Pick };
struct ConfirmSpec { /* existing fields */ Tok tok; uint8_t pick_n; PickAct act; Tier tier; };  // object and summary go into the existing text
struct ModuleResult { /* existing fields */ const ChoiceSheet* sheet; const TodoPage* todos; };
// domain.h
enum class SessionWhy : uint8_t { None, Permission, Plan, Question, Input, Login, Error };
enum class SheetKind : uint8_t { None, Reply, Permission, Plan, Question };
enum class PickAct : uint8_t { Unknown, Reply, Allow, Always, Deny, Answer };
enum class Tier : uint8_t { Low, High };
struct SessionRow { /* existing fields */ SessionWhy why; };
struct ChoiceSheet { Sid sid; Tok tok; SheetKind kind; NoneWhy why; Tier tier; uint8_t ttl_s; uint16_t total;
                     uint8_t obj_lines; uint8_t n_lines; SheetLine lines[3]; uint8_t n_opts; SheetOpt opts[6]; };
struct TodoRow { TodoRef ref; TodoText text; TodoSt st; Opt<ProjectKey> project; uint16_t day; Opt<TodoMeta> meta; };
struct TodoPage { uint8_t n; TodoRow rows[4]; uint16_t total; Opt<RecCursor> next; };
// app_state.h
struct LinkInfo { /* existing fields */ bool cap_choose; bool cap_decide; bool cap_todo_app; };
struct QueueData { /* existing fields */ uint16_t review; };
```

### 10.3 Keymap additions

| Layer | Key | Action |
|---|---|---|
| Global | Opt+W | `Action::Todos`, opens the to-do app |
| ConfirmHigh (new) | `y` | Confirm (information only in the menu, never replayed) |
| ConfirmHigh | Back key, `` ` `` | Cancel |
| ConfirmHigh | Tab | Menu (same as the Confirm layer) |

Digits on choice sheets, and T / Q / 1–9 on the one-at-a-time confirmation page, are characters the app handles itself; they are not in the global table, and the app's `help()` passes them to the menu (docs/13 §6.2). Opt+G and Opt+D on the confirmation page are handled by the app, like docs/14's "Pending" in "Waiting for you". All new bindings must pass `test_keymap_static`'s ghost-key check.

### 10.4 Budget [calc, estimated from struct fields, measured in H6]

| Item | Added | Limit |
|---|---|---|
| AppState | `SessionRow.why` 10 B + capability bits 1 B + `review` 2 B ≈ 13 B | 4096 B (3488 B in v0.3.1, v0.4.0 added 4 B; headroom after v0.4.1 is whatever V41-H measures [open]) |
| "Waiting for you" app object | `ChoiceSheet` about 465 B | ≤1 KB per app |
| To-do app object | 4 rows × about 195 B + cursor and UI state ≈ 830 B | ≤1 KB per app |
| Large-reply decode area (static) | The largest of the three, about 830 B | Static RAM growth measured together with free heap ≥150 KB and largest block ≥64 KB |
| Heap | 0: no heap, no blocking (docs/13 §10.3) | — |

## 11. cpad

### 11.1 New files

| File | Version | Content |
|---|---|---|
| `choices/presets.py` | v0.6.0 | Parsing `replies.json`, defaults, `when:"seen"`, read and write |
| `choices/sheet.py` | v0.6.0 | Pure function `build_sheet(tracked, pending, presets, caps, now) -> (ChoiceSheet, Binding)`; reason codes for `none` |
| `choices/pick.py` | v0.6.0 | Pure function `check_pick(binding, tracked, presets_hash, now) -> None | code` (including the hook `at` and the excerpt hash) |
| `todos/mine.py` | v0.6.0 | Scanning, generating and parsing `ref`; `tick(path, ref, done)`, writing only 1 byte and checking, per §7.2 |
| `todos/review.py` | v0.6.0 | Pure functions: fold `rec.result`, `todo.*`, `dest.decided`, `queue.added(from_item=<rec>.a…)` into the to-confirm list and the handled count per meeting; `load_actions(dir)` reads and validates `result.json`; `display_text()` |
| `todos/render.py` | v0.6.0 | Pure function `todo_entry(event)`: the daily-note line for `todo.mine` (v0.4.1's to-do line format) |
| `todos/service.py` | v0.6.0 | `Todos.on_device_request(msg) -> reply` (following v0.4.1's `Recordings`); T, Q, Opt+G call `dest.router.on_capture()` |
| `cli/reply.py`, `cli/todo.py` | v0.6.0 | §11.5 |
| `agents/claude_code/brief.py` | v0.6.1 | Pure functions `brief(tool, tool_input, cwd, root) -> Brief`, `brief_obj(text)` (removes nothing; `unsupported` if not possible), `redact(text)`, the sensitive list (§5.2, §5.3) |
| `agents/claude_code/decide_io.py` | v0.6.1 | Pure functions: `request(doc, brief)` builds the hook → cpad request; `parse_request(doc)` is the strict daemon-side validation (field allow-list per kind, lengths, note lines equal to `excerpt()`, the object meeting `brief_obj()`'s rules, shape self-check); `output(answer, tool_input)` builds the hook's output (§5.5) |
| `agents/claude_code/decide_client.py` | v0.6.1 | The hook-side two-phase client: connect 0.2 s, first line 0.5 s, second line 110 s (§5.1) |
| `choices/book.py` | v0.6.1 | Waiting book, tokens, deduplication (§5.4) |
| `cli/decide.py` | v0.6.1 | §11.5 |

### 11.2 Existing files changed

| File | Version | Change |
|---|---|---|
| `agents/claude_code/hooks.py` | v0.6.0 | Stop also forwards `tail`; `KEYS` gains `tail` |
| `agents/claude_code/hook_state.py` | v0.6.0 | `merge_pairs()` carries `why`, `msg`, `tail`, `at` |
| `inbox/need.py` | v0.6.0 | `Tracked` gains `why`, `msg`, `tail`, `hook_at` |
| `agents/claude_code/adapter.py` | v0.6.0 | After a fork, `stop` the copy first, then return `E_REPLY_FORKED` (§4.3) |
| `hub/{state,inputs,effects,reduce,snapshots}.py` | v0.6.0 | `choice.get`, `agent.pick`; `failure_reply()` gains `agent.pick`; `why` and `_order` on snapshot rows; `queue.snapshot.review`; summaries of queue items from meetings start with "会议·" (Meeting·), sessions opened from them always `plan` |
| `recordings/service.py` | v0.6.0 | `todo_n` in `_counted()` subtracts handled items |
| `notes/writer.py` | v0.6.0 | `_entry()` understands `todo.mine`; once written, the hub records `todo.written`; `backfill` skips `todo.mine` events that already have `todo.written` |
| `config/settings.py` | v0.6.0 / v0.6.1 | `experimental_bg_reply` defaults to `"auto"`, `todo_days` (default 30); v0.6.1 adds `decide` (default false) |
| `protocol/{specs,messages}.py` | v0.6.0 | The 6 new types and new fields (part of P6) |
| `app.py` | v0.6.0 / v0.6.1 | Hands `todo.*` to `Todos`; v0.6.1 wires up decide |
| `agents/claude_code/hook_settings.py` | v0.6.1 | The decide handler: `PermissionRequest`, `args:["hook","--decide"]`, `timeout:120`; `install()` reads `decide` from the config; `verify()` finds every place it is installed |
| `cli/hooks.py` | v0.6.1 | `--decide`: after forwarding the event, go through steps 3–5 of §5.1; print only the shapes in §5.5; on any exception print nothing and exit 0 |
| `control/server.py` | v0.6.1 | The `decide` command replies the first line first, then holds the connection, and the hub replies the second line |

### 11.3 The control socket's `decide` (v0.6.1)

```jsonc
// hook → cpad (only over the 0600 cpad.sock)
{"cmd":"decide","sid":"<full session UUID>","tuid":"toolu_…","kind":"permission","tool":"Bash",
 "obj":"git push origin fix/login","note":"Claude 说：跑测试并推送","inside":false,
 "cwd_h":"<first 16 hex characters of sha256(realpath(cwd))>","sub":false}
// cpad → hook, first line (within 0.5 s)
{"ok":true,"held":true}                      // or {"ok":true,"pick":null}: fall back to the Mac dialog
// cpad → hook, second line (only after held)
{"ok":true,"pick":{"act":"allow"}}           // or {"act":"deny","m":"stop"} / {"act":"answer","i":1} / {"act":"always"} / null
```

- `parse_request` follows `hooks.parse()`'s rules: a field allow-list per kind, and one extra field means rejection; note lines must equal `excerpt(line)`; the object must meet `brief_obj()`'s rules; `labels` ≤4 × 36 B. For file edits, `inside` says whether the object is a relative path inside the project or an absolute path starting with `~/`.
- The risk tier `tier` is computed by cpad per §5.3: the registry, the `cwd` in the list and the sensitive list are all checked by cpad itself; `inside` and the realpath are facts reported by the hook (the end of §5.3 explains why that is acceptable).
- The request carries no absolute paths: `cwd` is sent only as a hash, and cpad computes the same hash from `SessionInfo.cwd` in the list to compare. This keeps a request line at worst 888 B (§9.5).

### 11.4 Events (`events.jsonl`)

| Event | Fields | Body |
|---|---|---|
| `agent.decided` | `sid, tool, tier, kind, act, n, obj_len, obj_sha8` | No |
| `agent.decide_fallback` | `sid, tool, reason` (off / handler / version / shape / offline / old_fw / other_profile / not_bg / unregistered / cwd / unsupported / subagent / dup / timeout / replaced / dismissed / closed) | No |
| `agent.replied` | Existing fields plus `preset?` | No |
| `agent.reply_forked` | `sid, copy_stopped` | No |
| `todo.mine` | `ref, rec, n, text, project, title, dir, at` | **Yes** (like `item.received`, it is the source of the note) |
| `todo.written` | `ref` | No |
| `todo.skipped` | `ref` | No |
| `todo.ticked` / `todo.tick_lost` | `ref, done` / `ref` | No |
| `dest.decided`, `queue.added` | Written by docs/14's router as usual, with `from_item` being the meeting to-do's `ref` | Same as now |

### 11.5 Command line

| Command | Purpose |
|---|---|
| `cpa reply ls / add / rm / reset / status` | §4.1, §4.3 |
| `cpa todo ls [--done] [--days N]`, `cpa todo review` | Read-only, for troubleshooting |
| `cpa decide on [--yes]` (v0.6.1) | Writes the decide handler (shows the change only; writes with `--yes`; backup rules follow `hook_settings`), and sets `decide:true` |
| `cpa decide off` | Sets `decide:false` and switches the handler back to the plain one |
| `cpa decide status` | Whether it is on, where the handler is installed, the CLI version and the verified version, the reason for the latest fallback |
| `cpa decide log [--since]` | Lists `agent.decided` and `agent.decide_fallback`, without body text |

## 12. Relation to the local MCP (docs/15)

- **Ticked to-dos** are written straight back to the daily note, and `list_todos` sees them next time. Both sides treat the same file as the truth.
- **Meeting to-dos**: when `list_todos` returns items from `summary.md`, it adds an optional field `decided` (mine / queue / sent / skip / null), computed with `todos.review`'s fold function; `status=open` does not return handled items, to avoid duplicating the line in the daily note and to avoid forever showing "3 more". This is a small change in phase B of docs/15, done by the docs/15 line of work.
- **`get_status`** may add `decisions_waiting` (count only). Optional.
- **What MCP can never do**: answer approvals, pick options, open sessions, reply to sessions, modify existing content, including ticking to-dos (docs/15 §3.6 "writes that will never exist"). `cpa mcp` does not forward `cpad.sock`; it only sends one hard-coded read-only request [src docs/15 §5]. The draft's "add an MCP write tool for ticking to-dos later" contradicted docs/15 §2.3 and §3.6 and has been removed.
- Approval summaries never touch disk, so MCP cannot read them.
- **The exception to "cpad only appends"**: docs/15 §2.1, §2.3 and docs/14 §2.3 all say cpad only appends to daily notes. From v0.6 there is one exception: when a to-do with a `^cpa-` block ID is ticked or unticked on the device, cpad changes 1 byte of that line. Recorded in the differences table; the two places in docs/15 need one sentence changed accordingly.

## 13. Security

**The trust boundary is unchanged**: physical USB plus the currently logged-in macOS user [src SECURITY.md].

| Change | Version | Risk | Mitigation |
|---|---|---|---|
| The device can approve tool calls | v0.6.1 | Someone holding an **unlocked** device that is plugged into your Mac can approve a command a background session is waiting on. Before, the most they could do was open a session in plan mode | Off by default, on only with `cpa decide on`; high risk needs Y, which the menu cannot stand in for; nothing preselected when a sheet opens, 400 ms debounce; the confirm box shows the object again; tokens are single-use; background sessions only; every decision recorded as `agent.decided`; nothing visible or selectable while locked, and choice sheets cleared on lock. **The lock screen is not a PIN lock** (docs/05 §8 paired approval with an optional PIN lock; v0.6.1 has no PIN, §17 item 1); the README and SECURITY.md say so |
| What you see differs from what runs | v0.6.1 | Summary cleaning alters the command, so what you approved is not what runs | Objects shown verbatim, ASCII only, single line, ≤128 B, no redaction match; otherwise kept on the Mac (§5.2); hypothesis tests guard "what is shown equals what runs, or it is refused" |
| "Inside the project" treated as low risk | v0.6.1 | Project root registered as `$HOME`, or edits to `.git/hooks`, `.claude/settings.json` | The five rules in §5.3; the sensitive list is high risk |
| A stuck cpad holds up the terminal | v0.6.1 | Every permission request has to wait for the hook | Two-phase handshake; if cpad does not send the first line, the delay is at most 0.7 s (§5.1) |
| Summaries of tool arguments leave the hook process | v0.6.1 | Commands or paths may contain secrets | Only in decide mode; objects matching a redaction rule never go to the device, and note lines are redacted first; only over the 0600 `cpad.sock`; cpad keeps them only in memory, logs and events only have lengths and hashes; the device keeps them only in memory and never writes them to SD; cleared on lock, disconnect and switching Macs |
| The hook starts printing to stdout | v0.6.1 | The output feeds into Claude's decisions | Only the fixed shapes in §5.5; strings only from constants or the `tool_input` the hook read itself; cpad only provides `act`, `i`, `m` |
| Auto-allow | — | — | Does not exist: without that one pick on the device, cpad never answers allow; any exception means "print nothing" |
| "Allow all for this session" | v0.6.1 | Scope too wide, covering sensitive files | Hidden by default; offered only if G-D1 confirms `ask` rules can cover the sensitive list; limited to low risk, the verified session directory (inside a registered project root), this one session, never written to a settings file; never offered for high risk |
| Stop forwards a tail excerpt | v0.6.0 | ≤120 B more of model output leaves the hook | Cleaned like the existing head excerpt, also only over `cpad.sock`; not recorded in `events.jsonl`, kept only in memory |
| Preset text | v0.6.0 | Turned into argument injection | `replies.json` strictly validated, 0600, `check_text()`; argv as before, with the body after `--` |
| Preset reply forks | v0.6.0 | A copy runs along in the same worktree | Stop the copy first after a fork; on by default only for verified CLI versions (§4.3) |
| Editing daily notes | v0.6.0 | Corrupting your notes, or writing somewhere else | Only 1 byte of a line carrying our block ID; `O_NOFOLLOW`, directory restricted to `notes_dir`; line-hash check, and read-back check after writing; on the same thread as cpad's appends; never deletes lines; always records `todo.ticked`, and can be undone |
| `summary.md` | v0.6.0 | Competing with WhisperWoof to write it | Not written (single-writer table) |
| Someone in a meeting "speaks in" an instruction | v0.6.0 | To-do text is transcribed | Treated only as data: only the part you saw gets queued; sessions show "Meeting·" and are always plan mode; your confirmation on the device is still required [src docs/11 §7.9] (§6.3) |
| A malicious process under the same user impersonates the hook or cpad | — | Forged approval requests, or forged answers | Out of scope (SECURITY.md). Impersonating cpad can at most make the hook print one of those shapes; a forged request only adds a sheet on the device, and nothing happens unless you pick |

**What changes in SECURITY.md**:
- v0.6.0 (DOC6): add "Stop also forwards a ≤120 B tail excerpt" to the hooks row; add "when a to-do is ticked on the device, cpad changes 1 byte of that line" to "Daily notes".
- v0.6.1 (DOC61): the hooks row states "once `cpa decide` is on, short summaries of permission requests are sent to the device"; add an "On-device approval" row; under "Known risks" add "the lock screen is not a PIN lock; with on-device approval on, an unlocked device can approve commands"; mark "v0.2: approval keys by risk" in docs/05 §8 as "done in v0.6.1 (off by default, no PIN lock)".

## 14. Tests (always write the test first: RED → GREEN → REFACTOR)

### 14.1 daemon

| Test file (new) | Version | Assertions written first |
|---|---|---|
| `tests/unit/test_hook_tail.py` | v0.6.0 | Stop forwards `tail`, the last ≤120 B; `parse()` rejects over-long or unclean `tail` |
| `test_choices_sheet.py` | v0.6.0 | Row-by-row check against the table in §3.2; every reason code for `none` (including `elicit`, `quota`, `released`); presets replaced as a group; no `when:"seen"` presets without `tail`; `lines` truncated without splitting a character |
| `test_choices_pick.py` | v0.6.0 | Token, state, `since`, hook `at`, excerpt hash and presets hash each mismatching returns the matching error code; **a turn finished between two polls** (`since` unchanged, `at` changed) is refused |
| `test_snapshot_order.py` | v0.6.0 | Sessions needing your decision (permission, plan, question) come before idle ones; with 11 sessions, the one cut is not one needing your decision |
| `test_presets_config.py` | v0.6.0 | Round trip; `version: 2` rejected; an extra field (specifically `token`) rejected; `label` containing `"` or too long rejected; `text` starting with `-` rejected; `when` only accepts `seen`; file mode 0600 |
| `test_reply_fork.py` | v0.6.0 | Each of the three fork signals triggers: `stop` on the copy first, then `E_REPLY_FORKED`; `copy_stopped:false` when the copy cannot be found |
| `test_reply_policy.py` | v0.6.0 | `"auto"`: on only for the same major and minor version and not lower than verified; a higher minor version is off; off when the constant is empty |
| `test_failure_reply_pick.py` | v0.6.0 | An invalid `agent.pick` gets `agent.result E_BAD_FIELD`, not `error` |
| `test_todos_mine.py` | v0.6.0 | Scanning rules (including `[X]`); `ref` round trip; before and after tick and untick the file differs by exactly 1 byte; line-hash mismatch returns `E_BUSY`; simulate Obsidian rewriting the whole file between read and write, the read-back check catches it, returns `E_BUSY` and records `todo.tick_lost`; symlinks refused; refused when the block ID appears twice |
| `test_todos_review.py` | v0.6.0 | Folding; `ref` carries the original-text hash, so after `result.json` is regenerated old decisions do not land on new items; first decision wins, skip can be changed; the same decision sent again gets `ack`; T, Q, Opt+G all go through `router.on_capture()` and write `dest.decided`; Q's body equals the shown part; when `queue_text()` downgrades, an error is returned and the item stays to confirm |
| `test_todos_render.py` | v0.6.0 | The `todo.mine` line equals v0.4.1's to-do line format; `todo.written` once written; **delete the line and restart, it does not come back**; when the event was recorded but the line was not written, startup fills it in once |
| `test_rec_todo_n.py` | v0.6.0 | `todo_n` subtracts handled items, at least 0 |
| `test_meeting_spawn.py` | v0.6.0 | Summaries of queue items from meetings start with "会议·"; sessions opened from them are always `plan` |
| `test_brief.py` | v0.6.1 | Object, note and risk tier for each tool; **hypothesis**: for any command, either the object on the device equals the command that runs byte for byte, or it is `unsupported`; dedicated cases: `*`, `` ` ``, `# `, `\n`, zero-width space, full-width characters, a 129 B command, commands matching redaction; canaries: 20 ways of writing secrets placed in `tool_input` never appear in the summary |
| `test_tier.py` | v0.6.1 | Project root being `$HOME`, `/`, or a parent of `~/.claude` is always high risk; every item on the sensitive list is high risk; `unsupported` when the hook's `cwd` hash does not match the list |
| `test_decide_io.py` | v0.6.1 | `parse_request` rejects extra fields, over-long and unclean lines per kind; `output()` produces JSON for each `act` equal to the shapes G-D1 captured; `answer`'s `updatedInput` keeps the original `questions`; unknown `act` prints nothing; shape self-check |
| `test_decide_client.py` | v0.6.1 | A fake server accepts the connection but never answers: the hook exits within 0.7 s with no output; null on the first line: no output; disconnect after held: no output |
| `test_decide_size.py` | v0.6.1 | The worst-case request of all three kinds ≤1024 B (888 B) |
| `test_hook_decide_cli.py` | v0.6.1 | `main(["hook","--decide"])`: with no cpad, no output, exit 0, done within 0.5 s; on allow, stdout has only that one line; stdin over 1 MiB is drained, with no output |
| `test_choices_book.py` | v0.6.1 | `hold`, `answer`, `expire`, `link_down`, `closed`, `dismissed`, replacement for the same sid; a second request with the same `(sid, tuid)` gets `dup`; tokens single-use; functions do not change the original object |
| `test_hook_settings_decide.py` | v0.6.1 | With `decide:true`, `hooks install` installs the decide handler; approval is still on after rerunning `install --yes`; warns when both user and project settings have it, and one dialog is taken only once |
| `tests/integration/test_decide_e2e.py` | v0.6.1 | fake-claude plus a real cpad, plus `cpa hook --decide` reading G-D1's fixture, plus a fake device: after a pick the hook prints allow; after the device disconnects, empty output within 1 s; empty at once after Opt+D (`inbox.dismiss`); empty after the timeout, with `decide_fallback` recorded; empty at once when the device's `hello.feats` has no `choose` (`old_fw`); **a waiting session is still in the snapshot among 11 sessions, with `need=true`** |
| Security canary | v0.6.1 | Put a special string in a summary, run the whole flow, and it is found nowhere in `events.jsonl`, logs or stderr |

Coverage keeps `--cov-fail-under=80`; the target for `choices/`, `todos/`, `brief.py`, `decide_io.py` is 90% or more (everything except IO is pure functions).

### 14.2 Protocol and firmware

| Test | Assertion |
|---|---|
| `protocol/tools/test_v06_worst_lines.py` | Every line in §9.5: schema-valid, byte count matching the table, ≤2048 B; `todo.items` with 5 items over the limit; the device `hello` worst-case line with `feats` |
| `test_additive.py` plus `v041_cases.sha256` | Old vectors unchanged |
| `check_vectors.py` | `agent.pick` is in `EXEC_KINDS`, and `outbox.item` has an invalid case with `kind:"agent.pick"` |
| Firmware `test_codec_vectors` | All 6 new types decode and round-trip equal |
| Firmware `test_choice_sheet` (new) | Nothing selected when opened; Enter ignored within 400 ms of opening; Enter ignored when nothing is selected; digits pick directly; `;` `.` Enter; items with unknown `act` are not shown; high-risk allow goes to ConfirmHigh, Enter does nothing, Y works; picks are not queued offline |
| Firmware `test_menu_model` / `test_menu_dispatch` | On ConfirmHigh, the accept row under "This page" has `ev.kind == None`; Enter on it produces no Confirm |
| Firmware `test_inbox_why` (new) | How reasons are shown; without `caps.choose` Enter takes the old path; offline Enter opens "Connect to the Mac to choose"; the last row of a `none` sheet is "Type a note (queued)…" |
| Firmware `test_sheet_clear` (new) | Choice sheets and to-do pages are cleared when the link leaves Ready, on lock, when `host_id` changes, and after 120 s |
| Firmware `test_todo_logic` (new) | Three tabs; one-at-a-time confirmation with T / Q / 1–9 / Opt+G / Opt+D; keys ignored for 400 ms after a skip; on `E_BUSY`, shows "Not changed, refreshed" and refreshes; paging |
| Firmware `test_mod_todo` (new, docs/16) | `expect_app_contract<TodoModule>()` |
| Firmware `test_keymap_static` | Opt+W and the ConfirmHigh layer produce no ghost keys and do not clash with other layers |
| Device tests and `cpa-sim shots` | Every page in §8 drawn once, `missing_glyphs == 0`, every line fits in 240 px |
| `check_apps.py` (docs/16) | The to-do app passes |

### 14.3 HIL and manual

| Scenario | Version | Where | What to look for |
|---|---|---|---|
| `tools/hil/scenarios/choose.py` (new) | v0.6.0 | `cpa-sim serve` and the real device | A fake host sends various `choice.sheet`s (including permission, plan, question) → press a digit → confirm → `agent.pick`; high risk needs Y and the menu cannot press it; pressing Enter repeatedly on opening sends nothing |
| `todo.py` (new) | v0.6.0 | Same, with a temporary `notes_dir` | List, tick, untick; change the file between the two steps and get `E_BUSY` |
| `review.py` (new) | v0.6.0 | Same, with a constructed `result.json` | One at a time: T, Q (with a project change), Opt+G, skip; `summary.md` byte-identical before and after; visible in `cpa audit` |
| Old cpad / old firmware | v0.6.0 | Same | Every row of §9.6 |
| `decide.py` (new) | v0.6.1 | `cpa-sim serve` + real cpad + fake-claude + hook fixtures | End to end: after picking, the hook prints allow; fallback after pulling the cable; fallback after Opt+D |
| 👤 G3′ | v0.6.0 | Your terminal | §4.3 |
| 👤 G-D1 | v0.6.1 | Your terminal | §5.7 |
| 👤 One real-device round (about 15 minutes) | v0.6.0 | Real device plus real Claude Code | Use a preset reply once; confirm 3 meeting to-dos (T, Q, skip); tick a to-do while today's note is open in Obsidian. **No typing at all** |
| 👤 One real-device round (about 15 minutes) | v0.6.1 | Same | A background session in a temporary directory: approve a plan once; allow a file edit once; allow a harmless command with Y; answer a question once. **No typing at all** |

## 15. Work packages

**v0.6.0**:

| ID | Content | owns_paths | Depends on | Acceptance |
|---|---|---|---|---|
| **G6** | Gate G3′; record the verified CLI version; capture Stop hook fixtures | `docs/gates/v06-gates.md`, `tools/gates/g3p.sh`, `daemon/tests/fixtures/hooks/stop-2.1.306-*.json` | None | G3′ has a measured conclusion; the value of `VERIFIED_CLI["reply"]` is in the gate record |
| **P6** | 6 schemas, new fields, vectors, README, line lengths, protocol tools | `protocol/schema/v0/{choice.get,choice.sheet,agent.pick,todo.list,todo.items,todo.act}.schema.json` (new), `protocol/schema/v0/{common,sessions.snapshot,queue.snapshot,hello}.schema.json`, `protocol/vectors/v0/messages/{the 6 above}.json` (new), `protocol/vectors/v0/messages/outbox.item.json` (invalid case added), `protocol/vectors/v0/flows/app/*`, `protocol/tools/{rec_rules,check_vectors,test_v06_worst_lines}.py`, `protocol/tools/fixtures/v041_cases.sha256`, `protocol/README.md`, `daemon/src/cpad/protocol/{specs,messages}.py` | None | `pytest protocol/tools`; `check_vectors.py`; all daemon tests green; **merged in the same batch as F6-0** |
| **F6-0** | Firmware shared interfaces and codec registration; keymap additions; information-only menu rows | `firmware/app/lib/protocol/*`, `firmware/app/lib/module_api/module.h`, `firmware/app/lib/core/{domain,app_state}.h`, `firmware/app/lib/input/{keymap.h,keymap.cpp}`, `firmware/app/lib/help/{action_help,menu_model}.cpp`, `firmware/app/test/core/{test_codec_vectors,test_keymap_static,test_menu_model}` | P6 | `pio test -e native` all green; `static_assert(sizeof(AppState) <= 4096)`; headers frozen from here (docs/13 §17) |
| **D6-C** | Reasons, choice sheets, presets, stopping fork copies, snapshot order | `daemon/src/cpad/choices/{presets,sheet,pick}.py` (new), `cli/reply.py` (new), `agents/claude_code/{hooks,hook_state,adapter}.py`, `inbox/need.py`, `hub/{state,inputs,effects,reduce,snapshots}.py`, `config/settings.py` (the reply lines), `app.py` (the choice lines), the matching `tests/unit/*` | P6, G6 | §14.1's v0.6.0 tests for choice sheets, presets, fork and order |
| **D6-T** | To-dos: mine, confirmation (via the router), rendering, service, CLI, counts | `daemon/src/cpad/todos/*` (new), `cli/todo.py` (new), `notes/writer.py`, `recordings/service.py` (one place in `_counted`), `app.py` (the `todo.*` lines), `hub/snapshots.py` (only `review` added, merged after D6-C), the matching `tests/unit/*` | P6; docs/14's `dest/router.py` merged | §14.1's todos tests; `summary.md` byte-identical before and after |
| **F6-I** | Reasons in "Waiting for you", choice sheets, the Pick confirm box, the small offline page, clearing triggers | `firmware/app/modules/inbox/*`, `firmware/app/lib/ui/pick_list.*` (new), `firmware/app/lib/shell/{confirm,runtime,shell}.*` | F6-0 | `test_choice_sheet`, `test_inbox_why`, `test_sheet_clear`; v0.1–v0.5 regression |
| **F6-T** | The to-do app, the home tile | `firmware/app/modules/todo/*` (new), `firmware/app/modules/home/*`, `firmware/app/src/module_list.cpp` | F6-0; docs/16's App SDK merged | `test_todo_logic`, `test_mod_todo`; `check_apps.py`; home tests; `missing_glyphs == 0` |
| **H6** | Integration, HIL, manual acceptance | `tools/hil/scenarios/{choose,todo,review}.py` (new), `docs/07-hil-checklist.md`, `docs/releases/v0.6.0.md` | All of the above | Every v0.6.0 row of §14.3 passes; your real-device round with no typing at all |
| **DOC6** | Docs | `SECURITY.md`, `README.md`, `docs/04-prd.md` §8 (keymap), `docs/14-destinations.md` §2.3 and `docs/15-local-data-access.md` §2.1, §2.3 (the "only appends" exception), `CHANGELOG.md` | None (release notes finalised after H6) | Every v0.6.0 change listed in §13 is in |

**v0.6.1** (after v0.6.0 ships):

| ID | Content | owns_paths | Depends on | Acceptance |
|---|---|---|---|---|
| **G61** | Gate G-D1; capture fixtures | `docs/gates/v061-gates.md`, `tools/gates/g_d1.sh`, `daemon/tests/fixtures/hooks/permission-request-2.1.306-*.json` | v0.6.0 | Every item of §5.7 has a measured conclusion; items that do not hold change the design per the "If it does not hold" column; stop here if a or b does not hold |
| **D61-H** | The decide hook: summary, object, risk, two-phase client, output, settings, `cpa decide` | `daemon/src/cpad/agents/claude_code/{brief,decide_io,decide_client}.py` (new), `agents/claude_code/hook_settings.py`, `cli/hooks.py`, `cli/decide.py` (new), `config/settings.py` (the decide lines), the matching `tests/unit/*` | G61 | §14.1's v0.6.1 hook-side tests plus canaries; with `decide` off, behaviour byte-identical to v0.6.0 (existing hook tests unchanged) |
| **D61-C** | Waiting book, hub wiring, waiting book as the truth, letting go on Opt+D, deduplication | `daemon/src/cpad/choices/book.py` (new), `choices/sheet.py` (the three approval sheets), `hub/{state,inputs,effects,reduce,snapshots}.py`, `control/server.py`, `app.py` (the decide lines) | D61-H | `test_choices_book.py`, `test_decide_e2e.py` |
| **H61** | Integration, HIL, manual acceptance | `tools/hil/scenarios/decide.py` (new), `docs/07-hil-checklist.md`, `docs/releases/v0.6.1.md` | All of the above | Every v0.6.1 row of §14.3 passes |
| **DOC61** | Docs | `SECURITY.md`, `README.md`, `docs/05-architecture.md` §8 (annotation), `CHANGELOG.md` | None | Every v0.6.1 change listed in §13 is in |

```
v0.6.0:
G6 ──► D6-C
P6 ══ F6-0 (same batch) ──► F6-I, F6-T
P6 ──► D6-C ──► D6-T (snapshots.py follows)
docs/16 App SDK ──► F6-T; docs/14 router ──► D6-T
everything ──► H6 (ships v0.6.0); DOC6 in parallel
v0.6.1:
G61 ──► D61-H ──► D61-C ──► H61 (ships v0.6.1); DOC61 in parallel
```

**Merge agreement**: P6 and F6-0 in the same batch (§9.7). D6-C and D6-T both change `hub/snapshots.py` and `app.py`; D6-C merges first and D6-T follows. The draft's T6-A (`check_apps.py`, `new_app.py`) was removed: docs/16 is already building the same thing under `tools/apps/`, and this version only depends on it.

## 16. Risks

| Risk | Mitigation |
|---|---|
| The hook contract differs from what we assume (output shapes, timeouts, ordering with the dialog) | Approval goes into v0.6.1, with G-D1 as its first item; if a or b does not hold it does not ship, and v0.6.0 is unaffected; output shapes pinned by fixtures |
| Behaviour changes after a CLI upgrade | Only verified major and minor versions, not lower than verified; a shape self-check on every request; `cpa decide status` warns; the fallback is always "the dialog on the Mac" |
| You don't see it, the hook keeps hanging, the session is held up | Only holds when the device is online, the session is in the background, and the current profile belongs to this Mac; at most 100 s; a countdown on the device; Opt+D lets go at once; a stuck cpad delays at most 0.7 s |
| Summaries leak secrets | Objects matching redaction never go to the device; note lines redacted, plus canary tests; never on disk; not shown when locked; off by default |
| Approving something on the device that should not be approved | Objects shown verbatim; high risk needs Y, which the menu cannot stand in for; nothing preselected on opening; tokens bound to what was seen; sensitive files are high risk; plan approval never offers "auto-accept edits" |
| Preset reply forks | The existing three signals; stop the copy first; G3′; on by default only for verified CLI versions |
| A preset reply reaches a session that "just said another turn" | The token binds the hook's `at` and the excerpt hash (§4.2) |
| Obsidian and the device change the same file at once | Write only 1 byte, check the line hash before and read back after; refresh after `E_BUSY`; verified by hand in H6 [open] |
| One copy in `summary.md` and another in the daily note | `todo_n` and `list_todos` both subtract handled items (§6.1, §12); `summary.md` untouched |
| To-dos you deleted in Obsidian come back | `todo.written` makes replay skip what was already written (§6.1) |
| 6 new schemas turn firmware CI red | P6 and F6-0 in the same batch (§9.7) |
| With `why` added, the worst-case snapshot only fits 9 rows | `fit_list` already cuts, and `total` is the real count; common names fit 10 rows (§9.5); what needs your decision sorts first and is never cut |
| App objects close to 1 KB | 4 items per page; every `TodoRow` field has a limit; measured in H6 [open] |
| New glyphs missing from the font | Use ASCII instead; `missing_glyphs == 0` |
| Digits mean "which option" on choice sheets but "project" elsewhere | Only on choice sheets; stated in the hint bar and the menu; on the confirmation page digits are still projects |
| v0.5's shared components or docs/16's App SDK not merged yet | `lib/ui/pick_list` lives in this version for now and is swapped once v0.5 has one; F6-T waits for docs/16 |

## 17. What needs your call

If you don't reply, the defaults are used.

1. **On-device approval goes into v0.6.1**: v0.6.0 ships reasons, preset replies, meeting to-dos and the to-do app first; approval ships separately as v0.6.1 once G-D1 passes, changing only the Mac side; off by default, turned on only when you run `cpa decide on` on the Mac; background sessions only; **no PIN lock**, and the fact that the lock screen is not a security lock goes into SECURITY.md. **Default: agree**. Alternatives: ship it together in v0.6.0; or push it to v0.7 together with an optional PIN lock (the original pairing in docs/05 §8).
2. **Choice sheets use digits 1–6 to pick directly** (#16 asks for numbered presets). This is the only exception to "digits = projects", valid only on choice sheets; nothing is selected when a sheet opens, and Enter needs a selection first. **Default: agree**. Alternative: only `;` `.` plus Enter.
3. **Preset replies available by default**: once G3′ passes, `experimental_bg_reply` defaults to `"auto"`, on only when the CLI's major and minor version match the verified one and are not lower. **Default: agree**. Alternative: always off by default, and you turn it on yourself in `config.json`.
4. **The to-do app uses Opt+W**, with "W To-dos" in the empty slot of home row 4; Opt+T stays "add a to-do". **Default: agree**. Alternative: Opt+T opens the to-do app, and typing a to-do takes two presses.
5. **How to-dos are written and ticked**:
   - meeting to-dos confirmed as "mine" go into the daily note of **the day you confirm them**;
   - ticking changes that line's `[ ]` to `[x]` directly, only 1 byte, with a conflict check;
   - only lines with a `^cpa-` block ID are listed.

   **Default: agree**. Alternatives: write into the note of the meeting day; also list checkboxes you write by hand in Obsidian (located by line number plus hash, which drifts more easily when the file changes a lot).
6. **Voice replies in v0.7**: hold G0 to speak, then check the transcript on the device and confirm; through recording contract v1 per #34, no WhisperWoof-specific path. **Default: agree**. Alternative: pull it into v0.6, at the cost of needing v0.5's "Pending" and transcript return first.
7. **Meeting to-dos can go to any destination (Opt+G)**: docs/14 §1.2 left this for v0.6, and this version routes it through the destination router together with T and Q. **Default: agree, in v0.6.0**. Alternative: v0.6.0 only does T and Q, and Opt+G comes later (recorded in the differences table).

## Appendix: differences settled against existing docs

| Before | Here | Why |
|---|---|---|
| docs/13 §14, §15: no approval in v0.6; "approval tiers" in the backlog | On-device approval in v0.6.1, off by default, after v0.6.0 and after G-D1 | The most common "waiting for you" in #16 is waiting for approval; splitting off v0.6.1 keeps v0.6.0 from being held up by the hook contract (§17 item 1) |
| docs/05 §8: approval keys by risk, paired with an optional PIN lock; in v0.2 | v0.6.1, off by default, no PIN lock | Needs hooks (#33) and G-D1 first; the PIN lock goes into the backlog, and SECURITY.md states the lock screen is not a security lock |
| docs/05 §6.10: reserves `approval.request` / `approval.decision` | Uses `choice.get` / `choice.sheet` / `agent.pick`; `approval.*` stays reserved, unused | Approval and preset replies are the same "fetch a sheet, pick by number"; a read-only request cannot be under `agent.*` (§9.1) |
| docs/13 principle 4: a menu row presses that key for you | "Y Allow" on ConfirmHigh is information only, never replayed | Otherwise Tab + Enter bypasses "high risk only accepts Y" |
| docs/13 principle 2: one letter stands for one thing | "To-dos" takes two letters: T adds one, W opens the app | docs/14 §3.2 made Opt+T "add a to-do", so opening the app needs another letter |
| docs/13 §4.1: the to-do app is "T on home" | Opt+W, home tile W, in the empty slot of row 4 | Home tiles only replay keys (docs/13 §6.2), and Opt+T adds a to-do |
| docs/13 §10.4, §15: v0.6 builds `check_apps` and `new_app` | Belongs to docs/16 (App SDK, `tools/apps/`); this version only depends on it | docs/16 is already building the same thing |
| docs/13 §10.4: host-driven `ui.list` / `ui.pick` only in v0.7 | v0.6 first builds a dedicated choice sheet | Approval needs token binding, the agent lane and keys by risk; the general `ui.*` is extracted from this in v0.7 |
| docs/11 §18: `reply.choices` in cpad.sock v1 | Over device protocol v0 | The person choosing is on the device; cpad.sock v1 is still v0.7 |
| docs/14 §1.2: sending meeting-summary to-dos out one by one in v0.6 | Done in v0.6.0: T, Q, Opt+G all through the destination router | One set of cleaning rules, one audit (§6.1) |
| docs/14 §2.3, docs/15 §2.1, §2.3: cpad only appends to daily notes | Ticking a to-do changes 1 byte of that line | To-dos follow the file, and Obsidian must show the tick (§7.2) |
| SECURITY.md: tool arguments never leave the hook process | After `cpa decide on`, redacted short summaries and verbatim objects leave it | Approving without seeing the command is more dangerous |
| docs/11 §1.2: AI to-dos must be confirmed on the device before entering the queue | The one-at-a-time confirmation page is that confirmation; no extra confirm box | Entering the queue is not execution |

## Appendix: evidence index

- **Issues**: #16 (numbered presets, configured per project on the Mac, free text by voice, drop the typing CER check); #33 (hooks); #34 (no app-specific integrations, WhisperWoof's W no longer needed); #19 (context profiles, see docs/18).
- **daemon (`wt-design`, i.e. main)**:
  - `agents/claude_code/hooks.py:1-8, 21-34, 48-63, 115-128`: allow-list, `MARKUP` / `HEADING` (removes `` ` ``, `*`, `# `), `excerpt()`, `parse()`;
  - `hook_state.py:1-13, 25-26, 83-86, 148-154, 175-178`: reasons, `NEEDS_GRACE_S`, elicitation and quota, `_lag_s`, `merge_pairs()` dropping `why`;
  - `hook_settings.py:23-28, 78-83, 119-127`: handler timeout 5 s, `PermissionRequest` matcher `*`, `is_ours()`, `install()` deleting everything before installing;
  - `adapter.py:127-153`: reply and fork detection (copy not stopped); `argv.py:22-27, 51-52`: `check_text`, `resume_argv`;
  - `inbox/need.py:1-7, 30-33, 37-47`: meaning of `since`, rules for waiting; `hub/snapshots.py:43-46, 57-59, 62-80`: `fit_list`, `_order`, session rows; `hub/reduce.py:141-160, 207-226`: `_spawn_text`, `failure_reply`;
  - `agents/base.py:18-33`: `SessionInfo.cwd`; `control/client.py:14-29`, `control/server.py:27, 88-90`: one timeout, `REQUEST_MAX = 1024`;
  - `projects/registry.py:67-68`: a project root only needs to be absolute; `DEFAULT_MODE = "plan"`; `dump_registry()` only writes known fields;
  - `cli/hooks.py:1-8`: never prints, always exits 0; `config/settings.py:58-60`: `inbox_idle_s`, `experimental_bg_reply`.
- **v0.4.1 worktree (`wt-v041-daemon`)**: `notes/render.py:30-50` (to-do checkbox line); `notes/writer.py:55-71` (append idempotent by block ID); `app.py:113-122` (replays the whole history at startup); `recordings/service.py:235-241` (`todo_n` from `summary.md`); `protocol/schema/v0/{rec.items,hello,common,queue.snapshot}.schema.json`; `protocol/README.md` item 4 (invalid cases for execution kinds), line 213 (`rec.items` is the only large reply, 7424 B); `tools/{rec_rules,flows,check_vectors,test_additive}.py` (`LARGE_HOST_REPLIES`, `APP_REQUESTS`, `_request_kind`, `EXEC_KINDS`).
- **v0.5 worktree (`wt-destinations`)**: `dest/router.py:100-104` (`queue_text`), `135-137` (`on_capture`); docs/14 §1.2, §2.3, §3.1, §3.2, §5, §10.1, §10.3.
- **docs/16 worktree (`wt-app-sdk`, in progress)**: §4.1 (app tiles in the row below the 3×3 grid), §4.2 (W is a reserved first-party letter), §7 (`check_apps.py`), §10 (module list has 8 slots), §11 (first-party apps).
- **Firmware (`release/v0.4.0`)**: `lib/input/keymap.cpp:43-122`; `lib/module_api/module.h:31-55, 89-121`; `lib/core/app_state.h:99-113`; `lib/core/domain.h:67-91`; `modules/inbox/*`; `modules/home/home_logic.h:23-33` (v0.4.0's 3×3 grid); `lib/shell/confirm.cpp:27-35, 55-61`; `lib/help/menu_model.cpp:70, 134`; `lib/shell/shell_menu.cpp:138-146`; `test/core/test_menu_model`, `test/ui/test_menu_dispatch`; `src/module_list.cpp`; `firmware/sim/README.md` (`cpa-sim shots`, `serve`).
- **Protocol**: `protocol/limits.json` (`line_max` 2048, `host_reply_max` 256, `device_rx_buffer` 8192, the two in-flight limits, `text_max` 500, typable characters 32–126); `schema/v0/common.schema.json` (16 error codes, `agentText`); the schemas of `agent.reply`, `agent.result`, `sessions.snapshot`, `inbox.dismiss`.
- **Docs**: docs/05 §6.10, §7.5, §8; docs/11 §1.2, §6.6–§6.8, §7.5–§7.9, §18; docs/13 §3, §4.1, §5.2, §5.6, §6.1–§6.2, §7.1, §10, §12.2, §14, §15, §19; docs/15 §2.1, §2.3, §3.4, §3.6, §5; gates/cli-gates.md §0, §1, §4, §5a, §7; SECURITY.md.
- **Similar products** (docs/02 §3, §4, §7): sohsatoh/cardputer-cli-buddy (PermissionRequest hook and the terminal dialog, whoever answers first wins; AskUserQuestion with 2–4 options; ExitPlanMode not handled on the device; keys only over USB); haochen2115/cli-buddy (uses PermissionRequest rather than PreToolUse, falls back to the dialog on error); loml13/cardputer-claude-mcp (keys by risk: Enter for normal, Y for dangerous; falls back to the terminal when the device is absent; replies HMAC-signed); Hardware Buddy (only "Allow once" and "Deny"); yuk1-kondo/cardputer-agent-deck (Fn+1..6 preset commands); session-hub (remote allow / deny, first answer wins, falls back on error).
- **Line lengths**: the numbers in §9.5 are computed with `json.dumps(..., ensure_ascii=False, separators=(",", ":"))` plus a newline (the control socket line uses `json.dumps` default arguments, matching `control/client.py`); the script is `v06calc/rev.py` in the scratchpad and is not in the repo; P6's `test_v06_worst_lines.py` and D61-H's `test_decide_size.py` recompute with the same construction.

## Appendix: how review comments were handled

The reviews raised 8 high, 15 medium and 13 low items, plus a "missing tests" section. Each was checked against the code (`excerpt()`, menu replay, `NEEDS_GRACE_S`, `_order`, `client.request`, `_stop_and_resume`, `since`, confirm-box debounce, `module.h`, `flows.py`, `EXEC_KINDS`, `failure_reply`, `hooks install`, `REQUEST_MAX`, `NotesWriter.backfill`, `todo_n`); all held and all were adopted; L10 was partly adopted.

| Review comment | Handling |
|---|---|
| **H1** The command summary on the device is not the command that runs (`excerpt()` removes `*`, `` ` ``, `# `, gives only the first line, and `***` can hide things) | **Adopted**. New principle 8, "what you see is what runs"; the object now uses `brief_obj()`, which removes nothing: ASCII printable only, single line, ≤128 B, no redaction match, otherwise `unsupported`; command first, `description` labelled "Claude says"; full URLs; MCP always stays on the Mac; hypothesis tests with dedicated cases (§5.2, §14.1 `test_brief.py`) |
| **H2** The Tab menu bypasses "only Y counts" | **Adopted**. The accept row on ConfirmHigh is information only (`ev.kind = None`), recorded as an exception to docs/13 principle 4; `test_menu_model` / `test_menu_dispatch` get assertions (§3.4, §10.1, differences table) |
| **H3** "Editing files inside the project" counted as low risk; "allow all for this session" might cover the whole home directory | **Adopted**. Five rules for low risk: inside `cwd` and `cwd` inside a registered project root, the project root not `$HOME` / `/` / a parent of sensitive directories, not on the sensitive list, the hook's `cwd` matching the list; "allow all for this session" hidden by default, offered only if G-D1 i confirms the absolute-path form and that `ask` rules can cover the sensitive list, scoped to the verified `cwd`, the same directory as the low-risk check (§5.3, §5.6, §5.7 i) |
| **H4** A held request drops out of "Waiting for you"; the newest request is cut first | **Adopted**. The waiting book is the truth: while held, the row is always `need=true` with `why` from the waiting book; `_order` sorts by reason, permission, plan and question before idle; both have tests (§3.1, §14.1) |
| **H5** A stuck cpad holds up every permission request, including terminal ones | **Adopted**. Two-phase handshake: 0.2 s to connect, `held` or null within 0.5 s, otherwise the hook exits silently; a test with a fake server that "accepts but never answers"; G-D1 gains row k (background session opened with `claude attach`) (§5.1, §5.7, §14.1 `test_decide_client.py`) |
| **H6** Nobody stops a forked copy | **Adopted**. After a fork, `stop` the copy before returning `E_REPLY_FORKED`, recording `copy_stopped`; on by default only for major and minor versions G3′ verified (§4.3, §17 item 3) |
| **H7** The token cannot see a turn finished between two polls; "it said" shows the start | **Adopted**. The token also binds the hook's `at` and the excerpt hash; Stop gains a tail excerpt `tail`, used for display; without `tail`, presets with `when:"seen"` (like "Looks good") are not offered (§3.1, §4.1, §4.2) |
| **H8** Three Enters approve | **Adopted**. Nothing selected on opening, Enter needs a selection first; Enter ignored within 400 ms of opening; tested (§3.3, §10.1, §14.2) |
| **M1** `agent.sheet` breaks "`agent.*` only from the confirm box" and the protocol tools; `agent.pick` not in `EXEC_KINDS`; `failure_reply` doesn't know it; docs/05 §6.10 not mentioned | **Adopted**. The read-only pair renamed `choice.get` / `choice.sheet` (on the shared lane); `agent.pick` added to `EXEC_KINDS`, with an outbox invalid case, and to `failure_reply`; docs/05 §6.10's `approval.*` in the differences table (§9.1, §9.4, §9.7) |
| **M2** Approval contradicts the docs/13 roadmap; the PIN lock is not addressed | **Adopted**. Default changed so that v0.6.0 ships presets and to-dos first, with approval as v0.6.1 after G-D1; the postponed PIN lock is written into §13, §17 item 1 and the differences table |
| **M3** T6-A duplicates docs/16; home row 4 takes docs/16's slot | **Adopted**. T6-A removed, F6-T depends on docs/16; W goes into the empty slot of row 4 of v0.4.1's first-party grid, with no new row (§7.4, §15) |
| **M4** "Add MCP to-do ticking later" contradicts docs/15; in-place note edits not in the differences table | **Adopted**. That sentence removed; the "only appends" exception written into §12 and the differences table, and DOC6 changes the matching sentences in docs/14 and docs/15 |
| **M5** Contradicts docs/14: sending out one by one postponed, T built separately, two sets of cleaning rules | **Adopted**. T, Q, Opt+G all go through `router.on_capture()` (`origin:"confirmed"`, `how:"key"/"pick"`), cleaning uses `queue_text()`, to-do lines use v0.4.1's format; Opt+G delivers docs/14 §1.2's "send out one by one" in v0.6.0, listed as §17 item 7 (§6.1, §6.2) |
| **M6** Deleted to-dos come back after restart | **Adopted**. `todo.written` recorded once written, and replay skips it; a "delete, restart, still gone" test. The same problem for ordinary note lines is existing behaviour, into the backlog (§6.1, §14.1) |
| **M7** Ticking rewrites the whole file; "4 bytes" is wrong; `[X]` missed | **Adopted**. Open with `O_NOFOLLOW`, check the line hash, `pwrite` 1 byte, `fsync`, read back and check, on the same thread as `NotesWriter`; changed to 1 byte; `[X]` matched (§7.2, §13) |
| **M8** Counts never go down | **Adopted**. `todo_n` subtracts handled items; docs/15's `status=open` does not return handled ones (§6.1, §12) |
| **M9** Decisions bound to the index, not the text | **Adopted**. `ref` carries the original text's `sha6`, and a mismatch is refused (§6.1, §9.4) |
| **M10** Untrusted meeting text becomes a prompt you only saw half of | **Adopted**. Q queues only the part shown on the device; session summaries start with "Meeting·"; always plan mode (§6.3). The review's suggestion of "page through the full text first" was not done: queuing only what you saw is simpler, with the same effect |
| **M11** Leaks between profiles; choice sheets not cleared after a disconnect | **Adopted**. §1.4 rewritten, listing v0.6's new offline data for docs/18 to split by profile; choice sheets and to-do pages cleared on disconnect, lock and Mac switch (§3.6). docs/18 has been synced |
| **M12** Reinstalling hooks quietly removes the decide handler; two settings files take it twice | **Adopted**. `install` reads `decide` from the config; `caps.decide` requires the handler verification to pass; deduplicate by `(sid, tool_use_id)` (§5.1, §5.8) |
| **M13** Pressing Opt+D on a held request still leaves the hook waiting 100 s | **Adopted**. `inbox.dismiss` makes cpad let go at once (`reason:"dismissed"`) (§3.3, §5.1) |
| **M14** `none` sheets lose today's typed note; offline Enter goes straight to typing | **Adopted**. The last row of a `none` sheet is "Type a note (queued)…", and of a `reply` sheet "Type a reply…" (both unnumbered); offline Enter opens the small "Connect to the Mac to choose" page, with typing as the second item (§3.2, §3.5, §8) |
| **M15** Accepting only one exact CLI version turns approval off at every update | **Adopted**. Accept the same major and minor version, not lower than verified; a shape self-check on every request; `cpa decide status` warns (§5.8) |
| **L1** Label too long | **Adopted**. Changed to "本 session 都允许改项目文件" ("Allow project file edits for this session", 36 B) (§5.6) |
| **L2** Wrong principle number cited | **Adopted**. Consistently "principle 7 (extending docs/13 principle 2)" (§2, §3.3) |
| **L3** `caps.todos` and `caps.todo` are easy to confuse | **Adopted**. Renamed `caps.todo_app` (§9.1) |
| **L4** Missing `none` reason codes | **Adopted**. Added `released`, `elicit`, `quota`; `multi` folded into `mac_only` (§3.2) |
| **L5** Says cpad does not trust the hook's tier, yet the tier depends on the hook-reported `inside` | **Adopted**. §5.3 states what cpad checks itself and what is a fact reported by the hook, cross-checked against the `cwd` in the list |
| **L6** Preset 3, "Pause", is useless | **Adopted**. Removed from the defaults (§4.1) |
| **L7** Use `interrupt` for "stop and wait for me" | **Adopted**. G-D1 gains row l; `interrupt:true` added once confirmed (§5.5, §5.7) |
| **L8** Subagent permission requests | **Adopted**. Always null (`reason:"subagent"`), with G-D1 row m to observe (§5.1, §5.7) |
| **L9** Control socket lines are 1024 B | **Adopted**. The three shapes validated separately, worst case 888 B, written into §9.5 and guarded by `test_decide_size.py` |
| **L10** A double press during one-at-a-time confirmation decides two items | **Partly adopted**. Keys ignored for 400 ms after each move to the next item; "skip" can be changed later (it writes nothing). No timed undo window: lines written by T and items queued by Q each have their own undo path (§6.1, §6.2) |
| **L11** W and T go against docs/13 principle 2; the `how` of Opt+T inside the app | **Adopted**. Written into the differences table; Opt+T inside the app carries `how:"key"` (§7.4) |
| **L12** Plan approval should say only the first 3 lines were seen | **Adopted**. New field `total`; the confirm box says "Only saw the first 3 lines (N in total)" (§3.4, §9.3) |
| **L13** The `E_BUSY` message is too specific | **Adopted**. Always "Not changed, refreshed" (§7.2) |
| **Missing tests** | **Adopted**. An invalid `agent.pick` gets `agent.result`; a held request still in the snapshot among 11 sessions; lock and disconnect clear the choice sheet; `hooks install` keeps the decide handler; two settings files take it only once; approval still on after reinstall. All in §14 |
| **Additivity check** (new fields and messages are all additions; the only protocol-level problem was M1) | Agreed. With M1 handled, compatibility with old daemons and old firmware is in §9.6 |
