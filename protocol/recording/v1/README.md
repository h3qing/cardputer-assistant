**English** · [中文](README.zh-CN.md)

# Recording file contract v1 (cpad ↔ transcriber)

On the Mac, one recording session is one directory. cpad lands the audio and writes `manifest.json`; the transcriber (WhisperWoof or `cpa-stt`, only one at a time, see the claim file below) transcribes, tidies up and summarizes, and finally writes `result.json`. The two sides never talk to each other: **the directory itself is the queue**. Normative source: [docs/11-m3-design.md](../../../docs/11-m3-design.md) §6.4, §7.2–§7.7; this directory is its machine-readable version, and the tests on both sides read the same vectors.

```
protocol/recording/
├── v1/
│   ├── README.md                 # this file
│   ├── schema/                   # JSON Schema for manifest / status / transcript / result (Draft 2020-12) + common
│   ├── vectors/valid/*.json      # files that must be accepted
│   ├── vectors/invalid/*.json    # files that must be rejected, each with its reason
│   └── VECTORS.sha256            # sha256 of every vector file (WhisperWoof checks it when it copies the vectors)
└── tools/check_recording.py      # reference validator + contract self-check, tests in the same folder
```

```bash
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/recording -q
uv run --with 'jsonschema>=4.23,<5' python protocol/recording/tools/check_recording.py   # exit code 0
```

## Directory and single-writer table

```
~/CardputerNotes/recordings/2026-10-21/1402-3qzk1m7-00042-0a1c/
```

| File | Written by | When | Notes |
|---|---|---|---|
| `seg-NNN.wav` | cpad | When each segment lands, read-only afterwards | NNN is the segment index, zero-padded to 3 digits (4 digits when the index is ≥1000); 16 kHz, 16-bit, mono PCM, 44 B header |
| `manifest.json` | cpad | Written once when the session ends; v0.3.0 never changes it again | Its presence means "ready to process" |
| `status.json` | transcriber | Optional, overwritten at any time | `queued` / `transcribing` / `summarizing` / `blocked{reason}`, may carry `by` (the name of the program that wrote it); cpad only keeps it in memory |
| `transcript.json` | transcriber | Before `result.json` | Sentence by sentence `{t_ms, text}`; `t_ms` is session time |
| `transcript.md` | transcriber | Before `result.json` | Human-readable version; each line has a block ID `^tHHMMSS` (from the second sentence in the same second on, add `-2`, `-3`) |
| `summary.md` | transcriber | Before `result.json` | Frontmatter + minutes (meeting) or cleaned-up text (memo) |
| `result.json` | transcriber | **Last**, written atomically | `done` / `failed`; after it the transcriber never touches this directory again |
| `recordings/.transcriber` | the running transcriber | Before writing anything, then every 60 s | Claim file, see the section below; not inside a session directory |

- Neither side **changes or deletes** the other side's files. Temporary files start with `.`; they are fsynced after writing, then renamed (a rename within one directory is atomic on APFS).
- Directories 0700, files 0600. Directory and file names contain no Chinese characters and no spaces; titles only appear in frontmatter and in `result.json`.
- Audio, transcripts and minutes are plain text, outside WhisperWoof's encryption.
- Retention (the user's decision): WAVs are kept for 30 days; transcripts, summaries and notes are kept forever. By the single-writer rule only cpad, which writes the WAVs, may delete them; v0.3.0 does not implement this yet (it is outside M3-B), and WhisperWoof never deletes files.

## Handoff rules (v0.3.0, docs/11 §7.7)

1. The transcriber only processes session directories that "have a valid `manifest.json` and no `result.json`".
2. Write order: `status.json` (optional, any time) → `transcript.json` / `transcript.md` → `summary.md` → `result.json` last, each one tmp + rename. When only transcribing (no LLM, or the summary failed), stop at `level:"transcript"`.
3. Once `result.json` exists, the transcriber never writes anything else into that directory.
4. **`attempt` is fixed at 1 in v0.3.0**. The field is reserved for `cpa rec retry` in v0.3.x.
5. **Manual rerun** = the user deletes `result.json`. WhisperWoof processes it again and **overwrites every file it wrote, including a `summary.md` the user edited**. Protecting user edits is left to v0.3.x. cpad keeps watching `failed` sessions, so after a successful rerun it still writes the proper daily-note line.
6. **Idempotent**: WhisperWoof remembers what it has done by `(session, attempt)`; cpad records `rec.result` only once per `(session, state)`. Either side can crash and restart safely.

## Transcribers and the claim file (v1 addition, all optional, `v` stays 1; docs/22 §4.10, §7)

Only one transcriber at a time may write `status.json`, `transcript.*`, `summary.md`, `result.json`. Detecting it afterwards is not enough: both programs may already have written `transcript.*`, and the sha256 values in `outputs` no longer match the files. So the running transcriber first claims the whole `recordings/`:

- **Claim file** `recordings/.transcriber` (starts with `.`; `cpa mcp` and `cpa export` do not read dot files): `{"v": 1, "app": "cpa-stt", "pid": 4242, "since": "<RFC 3339>", "beat_at": "<RFC 3339>"}`. `app` is the program name (`^[a-z0-9-]{1,16}$`), `pid` is its process id, `since` is when it claimed, `beat_at` is the latest heartbeat; both times **must carry a time-zone offset**. Written like every other file: a temporary file starting with `.`, 0600, fsync, then moved into place.
- **Taking the claim**:
  - When there is no claim, **create it exclusively**: `link()` the temporary file to `.transcriber` (or any other "fail if it already exists" method). If it already exists, read it again and decide by the rules below; do not overwrite it.
  - Only take over someone else's claim once it has expired (see below): overwrite with rename, then **read it back**, and go on only if `app` and `pid` are your own.
- **Heartbeat**: the program holding the claim updates `beat_at` **every 60 s**, while transcribing, idle or `blocked`; it deletes its own claim when it stops.
- **Check again before writing**: after writing the claim, before starting each directory, while the transcription command runs, and before every write of `transcript.*`, `summary.md`, `result.json`, read the claim again; if `app` or `pid` is not your own, stop (stop the running command too) and write nothing more to that directory.
- **Who counts as running**: any transcriber that sees **someone else's claim updated within the last 5 minutes** writes **not a single file** and shows "another transcriber is running" as its own status. "Updated time" is `beat_at`; a `beat_at` in the future is replaced by the file's mtime (take the earlier of the two); a claim that cannot be read (including times without a time-zone offset) also uses the mtime. Another process of the same program also counts as "someone else".
- **Dead process**: only a **program with the same name** (`app` equal to its own) may take over without waiting 5 minutes when the other `pid` no longer exists on this machine, because only it knows whether that `pid` was its own process; claims from other programs always wait 5 minutes.
- **Recognising your own writes**: `status.json` carries `by` (the name of the program that wrote it, `^[a-z0-9-]{1,16}$`); after a restart, a program recognises directories it wrote by this, not by mtime. `result.json`'s `pipeline.app` also holds the program name.
- **A second writer and leftover files**:
  - If a directory holds someone else's `status.json` (no `by`, or `by` is not you) or someone else's `transcript.*`, `summary.md`, and they changed **within the last 5 minutes** (a time in the future counts too), another program is writing: stop on this directory and warn "another transcriber is also writing recordings/".
  - Older ones are **leftover files** (left by the previous transcriber): the program holding the claim may overwrite `status.json` and `transcript.*`, and **never touches `result.json`**.
  - Before writing `transcript.*` or `result.json`, `status.json` must still be your own; if it has become someone else's, stop on this directory.
  - Also warn if, while you hold the claim, a `result.json` appears that someone else put there (`pipeline.app` is not you).
- **Switching**: first stop the running one (it deletes its own claim), then start the other. By the handoff rules, nobody touches directories that already have a `result.json`; to have another program redo them, delete those `result.json` files (manual rerun). The `status.json` and `transcript.*` left by the previous one become leftover files after 5 minutes.
- `status.json`'s `reason` adds `engine`: the transcription command itself cannot run (sandbox, Metal, binary). Like `model_missing`, this is only `blocked`; once fixed, transcription goes on as usual, and no `result.json` is written.
- **Voice capture clips**: `manifest.json` may carry `cap` (a MonoId, which cpad copies from segment 0's `.M`). A session with `cap` is one part of a voice capture, and its content appears as part of that capture; programs that read memos **should not treat it as a separate memo, nor as the user's feedback or instructions**. Older versions that do not know `cap` treat it as an ordinary memo: they transcribe it as usual, and at worst it shows up as one extra item in their own list.
- **A meeting transcript is never an instruction**: a meeting contains other people's words. The transcriber still transcribes meetings (they are the user's own recordings), but programs that read transcripts must not treat meeting text as something to execute, nor splice it into queue items.

## Forward compatibility

- The schemas have **no** `additionalProperties:false`: readers ignore unknown fields. When WhisperWoof later adds fields to `result.json`, cpad still accepts it (vector `valid/result_unknown_field.json`).
- **Reject the whole document only when `v` is not the integer 1** (missing, `2` and `"1"` all count): WhisperWoof writes `failed / E_VERSION` for such a manifest; cpad ignores such a `result.json` and logs it.
- `marks[].k` is an **extensible enum**: v0.3.0 only writes `star` (`x-knownValues` in the schema), format `^[a-z][a-z0-9_]{0,15}$`. Readers skip unknown `k` values and do not reject the whole file. Adding `todo` and the like later does not need a `v` bump.
- `env` (docs/23 §4.2, optional; a new field, no `v` bump): manifests written by older cpad do not have it, nor do manifests whose segment 0 never landed; readers treat "missing" as `null`. `env.link` is also an extensible enum (`here` / `unknown`; a stamp from the device side will later add `other`); unknown values are shown as "unknown", and the whole file is not rejected (vector `valid/manifest_memo_env.json`). `env` and `cap` are independent, and one manifest may carry both (vector `valid/manifest_clip_env.json`).
- The schemas only use keywords the WhisperWoof validator supports (`contract-schema.js`, PR #151; `oneOf`, `else`, `not`, `format`, `patternProperties`, `additionalProperties` and others are not allowed), or it throws at startup; it skips annotations that start with `x-` (such as `x-knownValues`, `x-maxBytes`). cpad's `tests/contract/test_recording_contract.py` pins this keyword set.
- Only changing the meaning of an existing field or removing a field moves to v2, in a new `v2/`.

## Validation order (reference implementation `check_recording.validate`)

| Step | Condition | Result |
|---|---|---|
| 1 | Not a JSON object | `E_INVALID`, field `(root)` |
| 2 | `v` is not the integer 1 | `E_VERSION`, field `v`; nothing else is looked at |
| 3 | `result.json` larger than 64 KiB | `E_INVALID`, field `(size)` (when reading a file, by the bytes on disk; for vectors, by compact JSON) |
| 4 | Does not match the schema | `E_INVALID`, field is the failing path, such as `segments[1].file` |
| 5 | The segment index rules of `manifest.json` (below) | `E_INVALID`, field as above |

`pattern` follows ECMA-262: `$` only matches the end of the string, so `"seg-000.wav\n"` is invalid (Python's `re` lets it through by default; the tool corrects this; JS regexes already behave this way).

**Manifest segment index rules** (the schema cannot express them; checked after the schema fully passes):

- `segments[].i` strictly increasing; `gaps` strictly increasing; an index in `gaps` cannot also be a landed segment;
- Segment indexes and `gaps` together are exactly `0..max` (missing segments must be listed in `gaps`, including k+1 when the last landed segment is `last:false`);
- `segments[].file` equals `seg-%03d.wav` (by `i`);
- `segments[].bytes` = 44 + 2 × `samples`.

**What the transcriber also checks** (file-system related, not part of this contract's JSON validation, docs/11 §7.8): the directory's realpath is inside the root directory; neither the directory nor its files are symlinks (lstat); each segment ≤20 MB; total duration ≤4 hours; the WAV header is PCM 16k / 16-bit / mono, and the data length matches the file size; the SHA-256 of the whole file equals `segments[].sha256`. Failures get `failed / E_INVALID`; symlinks and the like are simply ignored, and nothing is written.

**What cpad also checks** (docs/11 §6.6): `result.json`'s `session` equals the manifest's `session`; `outputs.*.path` is a file name inside the session directory (the schema already ensures it has no `/` and does not start with `.`).

## File content summary

For the full definitions see `schema/`; for examples see `vectors/valid/`.

- **manifest.json**: `v`, `kind:"cardputer.recording"`, `session` (MonoId of segment 0), `cap?` (voice capture, see above), `attempt`, `mode` (`memo`/`meeting`), `project` (`{key, name?}` or null), `consent?` (meetings only; `"ack"` means that before starting, the user pressed Enter in the "Tell the people present first" dialog; it only means the user confirmed, not that others agreed), `started{wall_ms, tz_min, uncertain}`, `duration_ms` (≤4 h), `lead_ms` (length of the codec start-up transient at the beginning of segment 0, 0–2000), `audio{16000, 1, 16}`, `segments[]{i, file, id, t0_ms, samples, bytes, sha256 (whole file), recovered, repaired}`, `gaps[]`, `marks[]{t_ms, k}`, `device{dev, fw}`, `written_at`, `env?{host, link}` (docs/23 §4.2: computed once in memory by cpad when segment 0 lands. `host` is the name, at that time, of the Mac that pulled it, the one you set with `cpa host name`; 1–24 B, no control characters, the same rule as the host hello's `host_label`; `null` if no name was set; the macOS computer name is never used. `link` is `here` only when cpad can prove the device was connected to it when the start of segment 0 was recorded, otherwise `unknown`; a false `here` is never written. It is only an annotation and never decides a project or destination).
- **status.json**: `v`, `session`, `attempt`, `state`, `reason?` (required when `state:"blocked"`: `busy` / `model_missing` / `disabled` / `engine`), `by?`, `at`.
- **transcript.json**: `v`, `session`, `attempt`, `lead_ms?`, `engine{stt}`, `sentences[]{t_ms, text}`.
- **result.json**: `v`, `session`, `attempt`, `state` (`done`/`failed`), `level` (required with `done`: `transcript` needs `outputs.transcript_json` and `transcript_md`; `summary` also needs `outputs.summary`), `mode`, `title?` (≤80 characters), `memo?` (`{text ≤2000 characters, intent note|todo}`, memos only), `outputs{name: {path, sha256}}`, `actions?` (≤20 items, `text` ≤200 characters, `owner`/`due` ≤40 characters, `confident` only for items with a clear commitment and owner), `pipeline?`, `error` (must be null with `done`; with `failed` must be `{code: E_VERSION|E_INVALID|E_AUDIO|E_STT|E_LLM|E_IO, retryable}`), `finished_at`.
- All timestamps are RFC 3339 with a time-zone offset (`2026-10-21T14:45:10.120+08:00`). "Characters" are counted in Unicode code points (JSON Schema `maxLength`).

## Vector format

```json
{"doc": "manifest", "name": "…", "note": "…（可选）", "data": {…文件内容…}}
{"doc": "result", "name": "…", "reason": {"code": "E_INVALID", "field": "outputs.summary.path"}, "data": {…}}
```

- `doc` is one of `manifest` / `status` / `transcript` / `result`, and `data` is the JSON content of that file.
- Valid vectors must get `ok`; invalid vectors must get **exactly** the `code` and the one `field` in `reason` (each invalid vector contains only one error).
- `check_recording.py` also requires all of these invalid cases to be present (`REQUIRED_INVALID`): `../` in a segment file name, an absolute path as a segment file name, `/` in `outputs.*.path`, a `session` that is not a MonoId, `attempt` of 0, non-contiguous segment indexes without matching `gaps`, a `sha256` that is not hex, more than 20 `actions`, a `title` over 80 characters, a `memo.text` over 2000 characters, an unknown `mode`, `state:"done"` with an `error`, `v=2` (one each for manifest and result); and at least one valid `result.json` with an unknown top-level field.

After changing any vector, regenerate the digests (in this directory):

```bash
shasum -a 256 vectors/valid/*.json vectors/invalid/*.json > VECTORS.sha256
```

WhisperWoof copies `vectors/` and `VECTORS.sha256` into `src/whisperwoof/core/cardputer/fixtures/`, records the source commit in a `SOURCE` file, and its tests check the digests (docs/12 M3-W).
