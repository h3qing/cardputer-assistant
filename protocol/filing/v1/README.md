**English** · [中文](README.zh-CN.md)

# Filing contract v1 (`cpa filing … --json`, `cpa project … --json`)

The external interface of the "to file" inbox (docs/23). Other programs (the Cardputer page in WhisperWoof, scripts, a terminal) run `cpa` with `execFile` and read the one JSON document it prints. **`cpa` is the only writer**: state lives only in `$CPA_HOME/events.jsonl`, one record per action. This contract covers only the shapes `cpa` prints. Normative source: [docs/23-mac-inbox.md](../../../docs/23-mac-inbox.md) §2.2, §3.1–§3.4. This directory is its machine-readable version.

```
protocol/filing/
├── v1/
│   ├── README.md                 # this file
│   ├── schema/                   # JSON Schema (Draft 2020-12) for ls / show / result / error / projects / host / row + common
│   ├── vectors/valid/*.json      # documents that must be accepted
│   ├── vectors/invalid/*.json    # documents that must be rejected, each with its reason
│   └── VECTORS.sha256            # sha256 of every vector file (WhisperWoof checks it when copying the vectors)
└── tools/check_filing.py         # reference validator + contract self-check; tests live in the same directory
```

```bash
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/filing -q
uv run --with 'jsonschema>=4.23,<5' python protocol/filing/tools/check_filing.py   # exit code 0
```

The daemon's `tests/contract/test_filing_contract.py` really runs every subcommand, and each output must pass `check_filing.py`. Every line of `data/filing.jsonl` from `cpa export` is checked as a `row`. The results of the MCP tools `list_unfiled` and `get_filing_item` are checked as `ls` and `show`.

**MCP** (2026-10-22, docs/23 P1-E): the read-only `cpa mcp` tool `list_unfiled` returns the `ls` document and `get_filing_item` returns `show`, each with an added `trust` (docs/15 §3.7). Past the output limit they add `truncated:true` (`ls` gives fewer rows and `next_cursor` continues after the last row; `show` truncates `sentences` and adds `next_offset`). Differences from the CLI: `filing` cannot be `hidden`, and hidden items are never returned; `fw` is always null; `prof` and `clips` are null; each field in a row is taken from an allow-list. There are no MCP write tools.

## Commands and documents

| Command | Prints on success | What it writes (one record per action, or nothing) |
|---|---|---|
| `cpa filing [ls] --json [filters]` | `ls` | — |
| `cpa filing show ID --json` | `show` | — |
| `cpa filing file ID KEY\|--none [--other] --json` | `result` | `rec.edited{project, by:"cli"}` |
| `cpa filing done ID --json` | `result` | `rec.edited{done:true, by:"cli"}` |
| `cpa filing hide ID --json` | `result` | `rec.hidden{by:"cli"}` |
| `cpa filing reopen ID --json` | `result` | Removes the one reason for the current state: `rec.unhidden`, `queue.done{qid, by:"undo"}` (a queue item with no session yet) or `rec.edited{done:false}` |
| `cpa filing hand ID [KEY] [--spawn] [--other] --json` | `result` | `queue.added{origin:"inbox", from_item:ID, project, intent:"todo"\|"spawn", voice:true, bytes, sha8}` (see "Hand to AI" below) |
| `cpa project ls\|list --json` | `projects` | — |
| `cpa project add [--key K] [--slot N] [--folder DIR] --json -- NAME` | `projects` (after the add) | `projects.json` |
| `cpa project rename KEY --json -- NEW_NAME` | `projects` | `projects.json` |
| `cpa project move KEY N --json` (the project that was on N moves to KEY's old number) | `projects` | `projects.json` |
| `cpa project folder KEY DIR\|--none --json` | `projects` | `projects.json` |
| `cpa project rm KEY --json` (recordings and queue items keep this key) | `projects` | `projects.json` |
| `cpa host name --json [-- NAME \| --clear]` | `host` (the name after read, set or clear) | `host_label` in `config.json` (no event) |
| Any of the above fails | `error` | — |

(2026-10-22, docs/25 R1-D) `cpa filing send ID DEST --json` is not in this contract. It is the same command as `cpa dest send` and prints `sent` or `error` from [`protocol/dest-cli/v1`](../../dest-cli/v1/README.md) (`error` has the same shape as here, with a few more error codes).

- `row` is never printed on its own. It is `rows[]` in `ls`, `row` in `show`, and one line of `data/filing.jsonl` in `cpa export` (hidden items are included there).
- Without `--json` the command prints a few lines for people (not in the contract). For write commands the last line is the undo command, e.g. `undo: cpa filing file 3QZK1M7-00042-0A1F --none`.
- `cpa filing triage` (2026-10-22, docs/23 P1-F) has no `--json` and is not in the contract. It walks through items one by one in the terminal. Its keys write exactly the `file`, `done` and `hand` records from the table above. Undo is `reopen`, or `file` back to the previous project, also with `by:"cli"`.
- Exit codes: 0 success (including `changed:false`); 2 bad arguments (`E_BAD_ARG`, `E_UNKNOWN_PROJECT`); 1 the state of this recording does not allow it (all other error codes).
- With `--json`, even arguments argparse does not know (options or commands this `cpa` does not have yet, e.g. a future `--net` or `--prof`) print an `error`: `E_BAD_ARG`, `field:"argv"`, exit code 2 (2026-10-22). **Exit code 2 with empty stdout** means this `cpa` is older than this contract: update `cpa`.

### Filters (`ls`)

By default it lists only "new" items, without the other-computer projects group, newest first, 200 rows per page.

| Argument | Meaning |
|---|---|
| `--filing new\|filed\|handed\|done\|hidden\|all` | State; `all` is everything except hidden |
| `--orphans` | Also include the other-computer projects group (new rows where `asked` is not null) |
| `--from` / `--to YYYY-MM-DD` | Date of the recording directory, both ends inclusive |
| `--device ID` | The 12-digit short id in the row |
| `--link here\|unknown` | Whether the device was connected to this Mac while recording; no stamp counts as `unknown` |
| `--mode memo\|meeting` | |
| `--project KEY` (may repeat) \| `--none` | A key registered on this Mac; `--none` means no project |
| `--tx …` (may repeat) | Transcription state; values as in `row.tx` |
| `--text WORDS` \| `--text -` | Literal substring, case-insensitive, searched in the title, memo body and full transcript; `-` reads it from stdin (1–200 characters). WhisperWoof's search terms go this way, never into argv |
| `--limit N`, `--cursor C` | 1–1000 rows per page; `C` is the previous page's `next_cursor`, and the filters must match the previous page |
| `--no-text` | `title` and `text` are null |

Two conveniences for people (2026-10-22, command line only, not in the contract, not in MCP): `--project` without `--filing` lists with `all` ("new" rows never have a project, so it would always be empty otherwise); `--from` and `--to` also accept `today` and `yesterday` (this Mac's local date). Programs should still give `--filing` and dates explicitly.

`counts` ignores the filters: `new` is new rows on this Mac; `other_mac` is new rows in the other-computer projects group; `clips_waiting` is 0 until voice captures (#52) are merged; `on_device` is the number of segments still on the device and not yet pulled. It asks only a running cpad and is null when cpad is not running.

### Rules for write commands (docs/23 §3.3)

1. **argv holds only ids, project keys and enum values**, each validated first. An id is a MonoId (case-insensitive; it comes back in upper case). A key matches `^[a-z0-9][a-z0-9-]{0,11}$` and is registered on this Mac. The one exception is a key the recording itself has carried: pressed while recording, or set on it earlier and later removed on this Mac (only undo uses it). Body text never goes into argv: for `hand`, `cpa` takes the body from the recording itself.
2. **One record per action**: inside the file lock on `events.jsonl`, `cpa` **decides again** from the log as it is while locked (counting what another process just wrote), then appends. It answers only after fsync. It can write while cpad is not running. The CLI never writes the daily note; cpad writes the line for a filed item (docs/23 §2.4).
3. **Idempotent**: if the item is already in that state, nothing is written: `changed:false`, `undo:null`.
4. **Rejections** (an `error` document; the input is never echoed, and `field` only names the argument):
   - `E_BAD_ARG`: invalid id, key, enum value, date or cursor; `file` with neither KEY nor `--none` (or both); the other-computer projects group without `--other` (`file`, `hand`, `field:"other"`); `hand` on a meeting (`field:"id"`: a meeting holds other people's words, docs/23 D16).
   - `E_UNKNOWN_ID`: no filed recording with this id (one still being pulled counts as absent).
   - `E_UNKNOWN_PROJECT`: this Mac has no such key; or `hand` without KEY when the recording's effective project is not on this Mac.
   - `E_ALREADY`: the state does not allow it. `done` only applies to "new" items (filed, handed or hidden ones return `field:"filing"`). With an open queue item, `file`, `hide` and `hand` are refused (`field:"queue"`: the queue item keeps its project; `reopen` it first). `hand` on a hidden item (`field:"filing"`). `reopen` on a filed item (use `file` to change project, `file --none` to go back to no project; `field:"filing"`), or on a queue item whose session has already started or that is done (`field:"queue"`).
   - `E_UNAVAILABLE`: `config.json` cannot be read, or `projects.json` cannot be read (`cpa project ls`, `field:"projects"`); for `hand`, the running cpad is older than this `cpa` or does not answer (`field:"cpad"`: an old cpad would not open this item in plan mode; restart it with `cpa service install` first).
   - `E_NO_TEXT`: the recording for `hand` has no usable text yet (`field:"text"`): no memo body and no transcript, or the text is empty after cleaning and truncation, or starts with `-`.
5. **Undo**: `undo` is a neutral object `{op, id, args}`. The caller rebuilds the command from its own verb table and validates the id again; it never copies it verbatim. `file` → `{op:"file", args:{project: previous effective project or null}}` (`project` is always present; it is null when the previous value was not a key at all). `done`, `hide`, `hand` → `{op:"reopen"}`. `reopen` → `{op:"done"}` or `{op:"hide"}`. Withdrawing a queue item has no undo, and neither does a case where the undo would be refused by `hide` (an open queue item): `undo:null`. An undo is itself an ordinary event. Chained undos (newest first, one step each) each return to the state before that action.

### Hand to AI (`hand`, docs/23 §3.4)

- **Queue only**: writes one `queue.added` with `origin:"inbox"`, `from_item` set to the recording id, `project` set to KEY (the effective project if not given), and `intent` set to `todo`, or `spawn` with `--spawn`. A session is opened **only on the device board** by selecting the item and pressing Enter (ADR 0002), and **always in plan mode**, whatever mode the project is set to. Its summary on the board starts with "语音·" (voice·). It cannot be opened on the Mac.
- **`cpa` takes the body itself**: the memo body (`memo_text`), or if there is none, the `transcript.json` sentences joined with spaces. It goes through `queue_text()`: `display_safe()`, trim whitespace, cut to 500 B (never half a character, no suffix). If the result is empty or starts with `-`, it returns `E_NO_TEXT`. `voice:true`; `bytes` is the UTF-8 byte count of the body; `sha8` is the first 8 hex digits of its sha256. `queue_text` in `cpa filing show ID` is this body read back by qid.
- **Memos only**: a meeting returns `E_BAD_ARG` (`field:"id"`), and its transcript is never read.
- A recording has at most one open queue item at a time. After the queue item's session has opened, or it is done, the recording can be handed again.
- Undo is `reopen`: `queue.done{qid, by:"undo"}`, which an old cpad also closes. An item whose session has already opened cannot be withdrawn (`E_ALREADY`, `field:"queue"`). If it is withdrawn while the device is opening it (`queue.spawned` is written only once opening is done), it still counts as handed to AI after opening, with `open:false`, and cannot be handed again.
- `result.queue` is the `{qid, intent, open}` after the hand.

## Fields

Full definitions are in `schema/`; examples are in `vectors/valid/`.

- **row**: `id`, `kind`, `filing`, `why`, `asked`, `at{wall_ms, tz_min, uncertain}`, `dur_ms`, `device`, `env`, `prof`, `project`, `starred`, `tx`, `title`, `text`, `intent`, `clips`, `queue`. **Every field is always present.**
  - `filing`: `new`, `filed`, `handed`, `done`, `hidden` (a closed enum). It is not called `state`: in the MCP `list_recordings`, `state` is the transcription state.
  - `project`: the effective project, given only when it is a key on this Mac, otherwise null. `asked` appears only in new rows of the other-computer projects group; it is the key pressed while recording.
  - `device`: the first 6 bytes (12 hex digits) of `HMAC-SHA256(id_key, "dev|"+dev)`. **The device's own id (its factory MAC address) is never given.** Each Mac has its own `id_key`, so ids on two Macs cannot be linked. Before cpad first starts this version there is no `id_key` yet, and the value is null.
  - `env`: the Mac-side stamp (docs/23 §4.2) `{host, link}`. Both fields are always present (`host` is null when the Mac has no name; `link` is null when it is not an enum word). For old recordings the whole object is null. It never contains network information.
  - `title` ≤80 characters, `text` ≤280 characters (counted in code points, not UTF-16: U+20000 and an emoji each count as one). Both go through `display_safe()`: no C0, DEL, C1 (U+0080–009F), U+2028/2029, bidi controls (U+202A–202E, U+2066–2069) or lone surrogates (U+D800–DFFF, half an emoji cut off by the transcriber; when `pattern` matches with the `u` flag, a surrogate pair is one character). `text` is the memo body, or the first few transcript sentences when there is none.
  - `prof` (context profiles, v0.8) and `clips` (voice captures) are null until then; the schema does not constrain their type.
  - `queue`: `{qid, intent, open}` after a hand to AI; always present when `filing:"handed"`.
- **show**: `row`, `text` (the full memo body, ≤2000 characters), `sentences` (`[{t_s, text}]`, null when there is no transcript), `todos` (to-dos from the summary, ≤20), `audio` (`seg-NNN.wav` file names, no directory; empty after the 30-day cleanup), `fw`, `queue_text` (the queued body handed to AI, ≤500 characters). All text goes through `display_safe()`.
- **result**: `ok:true`, `id`, `filing` (the state after the action), `changed`, `undo`, and `queue` (this recording's queue item after the action, same as `row.queue`; after `hand` it is the newly queued item).
- **error**: `ok:false`, `error` (one of the fixed error codes above), optional `field` (`^[a-z][a-z0-9_]{0,15}$`).
- **projects**: `[{key, name, slot, ok, linked, folder}]`, sorted by `slot`, matching 1–9 on the device. `name` goes through `display_safe()`. **No local paths are given.** `ok:false` means the linked project folder is gone.
  (2026-10-26, docs/25 §8) A project is first of all a **named group**; a code folder is optional. `linked` says whether a folder is linked; `folder` is the last component of that folder's path (≤64 characters, no `/`), null when not linked. Older `cpa` versions do not give these two fields; readers treat them as `linked:true`, `folder:null`. Only projects with a linked folder get hooks, project detection by cwd, and session opening (opening a session on the device for an unlinked project returns `E_UNKNOWN_PROJECT`).
  On success the write commands (`add`, `rename`, `move`, `folder`, `rm`) print this document after the change. On failure they print `error` and write nothing: `E_BAD_ARG` (`field` is `name`, `key`, `slot`, `folder`, `mode` or `argv`; exit code 2), `E_UNKNOWN_PROJECT` (`field:"key"`; exit code 2), `E_ALREADY` (the key or number is already taken, or all 9 numbers are used; `field` is `key` or `slot`; exit code 1), `E_UNAVAILABLE` (`projects.json` cannot be read; `field:"projects"`; exit code 1). The name goes after `--` (it may be Chinese, 1–24 bytes, same rules as `cpa host name`). Without `--key`, the key is built from the ASCII letters and digits in the name, or `p<number>` if there are none. Without `--slot`, the first free number is used.
- **host** (2026-10-22): `{v:1, host}`, this Mac's name, null when it has no name (the default). A read prints the current name; set and clear print the new one. A name is 1–24 bytes, does not start with `-`, and has no characters that `display_safe()` would remove (what is stored is what is shown). An invalid name gives `error` (`E_BAD_ARG`, `field:"name"`, exit code 2) and nothing is written.

## Forward compatibility

- The schemas have **no** `additionalProperties:false`: readers ignore fields they do not know.
- **Extensible enums** (`row.kind`, `why`, `tx`, `env.link`, `intent`, `queue.intent`, `result.undo.op`) are marked with `x-knownValues` and use the format `^[a-z][a-z0-9_]{0,15}$`, following the precedent of `marks[].k` in the recording contract. A reader that meets an unknown value shows that item as "unknown" and does not reject the whole row. `kind:"capture"` and `link:"other"` will come later.
- **The whole document is rejected only when `v` is not the integer 1** (missing, `2`, `"1"`, `true` all count). A `row` on its own (one exported line) has no `v`.
- Only changing the meaning of an existing field or removing a field moves to v2, in a new `v2/`.
- The schemas use only keywords the WhisperWoof validator knows. `check_filing.KEYWORDS` is the same allow-list as its `contract-schema.js` (plus annotations starting with `x-`). That validator throws at startup on any other keyword, so `oneOf`, `else`, `not`, `format`, `default`, `$comment`, `uniqueItems`, `additionalProperties` and so on cannot be used. The self-check looks wherever that validator walks (`properties`, `$defs`, `anyOf`, `allOf`, `items`, `if`, `then`). Nullable objects and arrays are written as `"type": [..., "null"]` so that error paths can point at the fields inside them.

## Validation order (reference implementation `check_filing.validate`)

| Step | Condition | Result |
|---|---|---|
| 1 | Not a JSON object | `E_INVALID`, field `(root)` |
| 2 | `v` is not the integer 1 (not checked for `row`) | `E_VERSION`, field `v`; nothing else is checked |
| 3 | Does not match the schema | `E_INVALID`, field is the failing path, e.g. `rows[0].text` |

`pattern` follows ECMA-262 (`$` matches only the end of the string).

## Vector format

```json
{"doc": "ls", "name": "…", "data": {…}}
{"doc": "row", "name": "…", "reason": {"code": "E_INVALID", "field": "project"}, "data": {…}}
```

- `doc` is one of `ls` / `show` / `result` / `error` / `projects` / `host` / `row`.
- A valid vector must give `ok`; an invalid vector must give **exactly** the `code` and the one `field` in `reason`.
- `check_filing.py` also requires: at least one valid vector per document type; at least one valid vector with a row that has an unknown field and also an unknown `kind`, `why` or `tx`; and all invalid cases in `REQUIRED_INVALID`: `v=2` (`ls`, `result`), a row id that is not a MonoId, `text` over 280 characters, `title` with a bidi control, `text` with a terminal escape, `text` with a lone surrogate, `env.link` not an enum word, `env.host` over 24 characters or with a bidi control, `asked` not a key, `device` that is a MAC address, an unknown `filing`, `handed` without `queue`, `asked` on a row that is not "new", `project` not a key, a transcript sentence with control characters, `audio` given as a path, `undo` with `changed:false`, an unknown error code, an upper-case project key, a project `folder` given as a path, and a computer name with control characters.

After changing any vector, regenerate the digests (in this directory):

```bash
shasum -a 256 vectors/valid/*.json vectors/invalid/*.json > VECTORS.sha256
```

WhisperWoof copies `vectors/` and `VECTORS.sha256` into its own fixtures and records the source commit in a `SOURCE` file (docs/23 P2-c).
