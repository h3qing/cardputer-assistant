# cpad / cpa

Mac daemon (`cpad`) and command line (`cpa`) for Cardputer Assistant. Design: [docs/05-architecture.md §7](../docs/05-architecture.md); wire protocol: [protocol/](../protocol/) (05 §6 is the source of truth). Python 3.12, runtime dependency `pyserial==3.5` only.

```sh
uv sync                                   # dev environment (.venv)
uv run cpa project add [--key K] [--slot N] [--folder ~/code/cpa] -- Name     # a named group 1-9; the folder is optional
uv run cpa project rename|move|folder|rm|ls ...   # all with --json (protocol/filing/v1 projects); docs/25 D24
uv run cpa daemon                         # foreground; or: uv run cpa service install
uv run cpa status                         # never prints note or prompt text; store=sd|int|none launcher=yes|no ('-' = v0.1 firmware)
uv run cpa pause 120                      # release the serial port (flashing); also --seconds N
uv run cpa queue ls|add <key> <text>|done <qid>|reopen <qid>
uv run cpa rec ls [--json] [--hidden]     # recording sessions, newest first: id, ★, mode, start, duration, project, state (never text)
uv run cpa rec show <sid> [--json]        # one session: folder, files present, project, ★ / hidden, result state / error code
uv run cpa rec unhide <sid>               # list a session hidden on the device again
uv run cpa device forget                  # unbind the USB serial number
uv run cpa host name [-- Desk | --clear] [--json]  # name this Mac: shown on the device, stamped on new recordings
uv run cpa lang [zh|en|auto]              # the language of cpa, cpad and cpa-stt; auto: CPA_LANG, then the locale
uv run cpa filing [ls] [--json] [--orphans] [--filing new|filed|handed|done|hidden|all] [--project KEY] [--from D]
                  [--to D] [--text W | --text -] ...  # recordings to file (docs/23), newest first; D also today|yesterday
uv run cpa filing show|done|hide|reopen <sid> [--json]   # show reads; each write is one record and prints its undo
uv run cpa filing file <sid> KEY|--none [--other] [--json]
uv run cpa filing hand <sid> [KEY] [--spawn] [--json]    # queue a memo's own text; started only on the device board
uv run cpa filing triage                  # the same list one key at a time in this terminal (1-9 file, x done, u undo)
uv run cpa project ls [--json]            # --json: protocol/filing/v1 projects (key, name, slot, ok; no path)
```

**Language**: English first, Chinese supported (`cpad/i18n.py`). The language is decided each time something is
shown or written: `CPA_LANG`, then config.json `lang` (`cpa lang zh|en`), then `LC_ALL`, `LC_MESSAGES`, `LANG`; a
value starting with `zh` is Chinese, anything else English. `cpa service install` copies the login shell's
`CPA_LANG` / `LC_ALL` / `LC_MESSAGES` / `LANG` into the LaunchAgent (launchd gives cpad none). Daily-note lines,
default titles, builtin destination labels and the header handed to other apps follow the language when they are
written; a line already written is never rewritten, and every reader parses the box, `#tag` and block id, not the
words.

`$CPA_HOME` defaults to `~/Library/Application Support/CardputerAssistant` (0700; files 0600):
`config.json`, `projects.json`, `events.jsonl` (append-only, fsync per append, the only authority),
`cpad.sock` (AF_UNIX 0600; `pause` / `status`), `cpad.lock`, `logs/cpad.log` (JSON lines, redacted unless
`CPA_LOG_CONTENT=1`), `host_id`, `id_key` (the HMAC key behind the short device ids of `cpa filing`; never
exported or logged). Notes go to `~/CardputerNotes/YYYY-MM-DD.md`. `limits.json` ships inside the
package (`src/cpad/protocol/limits.json` links to `protocol/limits.json`; the build copies it into the
wheel), so `uv tool install ./daemon` needs no checkout. `CPA_PROTOCOL_DIR` overrides where it is read from.

## Recordings (v0.3, docs/11 §6)

With a v0.3 device (its `hello` / `pong` carry `files`) and the card's index ready, cpad pulls committed
segments with `file.*` whenever the device is not recording: memos first, then by id, up to 4 × 1380 B
requests in flight, CRC-32 per chunk and SHA-256 per segment. A segment is staged in
`$CPA_HOME/rec-staging/<dev>/<id>.part`, landed atomically as
`<notes_dir>/recordings/<YYYY-MM-DD>/<HHMM>-<session>/seg-NNN.wav` (directories 0700, files 0600),
recorded as `rec.seg.stored` in `events.jsonl`, and only then deleted from the card (`file.del`). If the
device starts recording mid-pull it answers `E_BUSY`; cpad waits for a `pong` with `live:false` and
continues from the same offset. `cpa pause` or unplugging keeps the `.part`; the next connection resumes
from its size minus 64 KiB. A v0.2 device (no `files`) is never sent `file.*`. When a session is complete
(or, after power loss, finalised with `gaps`), cpad writes `manifest.json` (contract v1:
[protocol/recording/v1](../protocol/recording/v1/)).

Transcription, cleanup and summaries are done by **WhisperWoof** (the release with the Cardputer inbox,
expected 2.9.0 or later; enable it in WhisperWoof's settings). WhisperWoof writes `status.json`,
`transcript.json` / `.md`, `summary.md` and finally `result.json` into the session folder. cpad watches
`result.json` (every 3 s) and appends one daily-note line: a meeting links to its `summary.md` with the
todo count; a "todo" (待办) memo becomes a `- [ ]` line; a memo note shows its cleaned text; without a summary
the line links to the transcript; a failure, or no result after `transcribe_wait_h` hours, gives a
"not transcribed" (未转写) line linking the audio. AI todos are never added to any queue in v0.3.

- **Plaintext**: audio, transcripts and summaries in `~/CardputerNotes` are not encrypted. Keep the folder
  out of iCloud Drive and Obsidian Sync; `cpa status` warns when `notes_dir` is under iCloud Drive.
- **Tell people first**: a meeting starts only after the device's "please tell the people present"
  confirm; this records that you confirmed, not that others agreed.
- **Manual rerun**: delete the session's `result.json`; WhisperWoof processes it again and **overwrites
  its own files, including a `summary.md` you edited**. cpad keeps watching failed sessions, so a
  successful rerun still writes the normal daily-note line.
- **Retention**: WAVs are removed 30 days after archiving, once the session is done and both
  `transcript.json` and `summary.md` exist (`rec.audio.swept`); text is kept forever. Audio of sessions
  without a summary is kept. After the sweep a memo line's audio link no longer resolves.
- `config.json`: `transcribe_wait_h` (default 24), `rec_pull` (default true; false leaves recordings on
  the card). `cpa status` shows `recordings: on_device pulling waiting processing blocked failed`.

### Recordings app and todo memos (v0.4.1, docs/13 §8, §9.3)

- **History on the device** (Opt+V): the device asks with `rec.list` (filter all / ★ / memo / meeting, 6 per
  page) and cpad answers `rec.items`, newest first by start time (never by session id: an NVS erase
  starts a new MonoId epoch). The paging cursor is opaque to the device, so a session that arrives
  between two pages never shifts or repeats a row. Sessions still on the card are listed too. A row's
  title is WhisperWoof's title, else the start of the memo text, else "Meeting 42m" (会议 42m); `todo_n` counts the open
  `- [ ]` boxes of a meeting's `summary.md`.
- **★, project, hide** (`rec.act`): cpad only appends `rec.edited` / `rec.hidden` to `events.jsonl`.
  `manifest.json` and WhisperWoof's files are never written, moved or deleted, and the daily-note line
  keeps its original `#project`. **Hiding is not deleting**: audio, transcript, summary, the daily-note
  line and the title in `events.jsonl` all stay on the Mac; `cpa rec ls --hidden` lists hidden sessions
  and `cpa rec unhide <sid>` brings one back.
- **Todo memos**: a memo recorded with G0 held and T pressed carries `dest:"todo"` on segment 0; its
  daily-note line is a `- [ ]` box whatever WhisperWoof's intent says. Any other destination writes the
  usual line. The destination is kept in `rec.archived`, never in `manifest.json`.
- **Environment stamp** (docs/23 §4.2): when segment 0 lands, cpad stamps `env{host, link}` from memory into
  `rec.seg.stored`, then `manifest.json` (an optional field of contract v1) and `rec.archived`. `host` is the
  name given with `cpa host name` (config.json `host_label`, unset by default, never the macOS computer
  name); `link` is `here` only when segment 0 started inside the current link (same boot, after its hello,
  before its latest pong), else `unknown`. A stamp that fails is null and never delays the landing.

### Filing (docs/23, P1)

- `cpa filing` reads `events.jsonl` and the archive only; it asks a running cpad for one number (segments still
  on the device, 0.5 s, null without an answer). `--json` prints the contract `protocol/filing/v1`; an argv this
  `cpa` does not know prints its error document too (`E_BAD_ARG`, field `argv`, exit 2). An older `cpa` without
  that prints nothing on stdout and exits 2: update `cpa`.
- Writes are one `events.jsonl` record each, decided again under the events lock; `file` is refused while the
  recording's queue item is open (`E_ALREADY`): withdraw it first with `reopen`.
- `hand` queues a memo's own text (never argv) for the device board, always started in plan mode. It is refused
  while a cpad older than api 2 runs or one does not answer within 2 s. With no cpad running it goes ahead, so an
  older cpad started afterwards would start that item in the project's own mode: restart cpad after updating.

## cpa-stt: transcription on this Mac (docs/22 §4)

`cpa-stt` is a separate program in this package (`src/cpa_stt/`, entry point next to `cpa`). It is one more
client of [recording contract v1](../protocol/recording/v1/README.md) in the transcriber role, like
WhisperWoof: cpad keeps no transcription code, and cpa-stt writes only into the recording folders. It reads
two of cpad's files, read-only: `$CPA_HOME/config.json` (where the notes are) and `projects.json` (project
names as the prompt). It is **off by default** and nothing runs in the background until you start it.

Turning it on (`uv tool install ./daemon` puts `cpa` and `cpa-stt` on your PATH; from a checkout, prefix
`uv run`):

```sh
cpa-stt setup                 # finds ggml-large-v3-turbo.bin / ggml-silero-*.bin, asks, then links them
cpa-stt use                   # fills the whisper.cpp example, runs the self-test in the sandbox, turns it on
cpa-stt run --once            # transcribe what is waiting now, then exit
cpa-stt watch                 # or keep polling every 2 s in a terminal (Ctrl-C stops, a second Ctrl-C at once)
cpa-stt service install       # or a LaunchAgent that runs `watch` at login (not installed by default)
```

Only one transcriber may write `recordings/`: if WhisperWoof's Cardputer inbox is on, turn it off first.

```sh
cpa-stt setup PATH...         # link these model files instead (never copied, never downloaded); --yes: no question
cpa-stt use my-config.json    # another engine or other options: your own config file
cpa-stt status [--json]       # on/off, running or not, LaunchAgent, claim, command, sandbox, self-test, timings
cpa-stt test my.wav           # one 16 kHz mono WAV through the same sandbox, text only in your terminal
cpa-stt retry                 # unblock (also a running watch) and redo cpa-stt's own failed recordings
cpa-stt off                   # off: removes the LaunchAgent, a running watch stops before its next recording
```

- **Config** `$CPA_HOME/stt/config.json` (0600): `provider` off | command, the command as an `argv` array
  (`argv[0]` absolute, `{wav}` exactly once, `{offset_ms}` at most once, never a shell), `meetings` (default
  true: meetings are your own recordings too; their text is never an instruction), `timeout_s`,
  `drop_lines`, `sandbox_mach`, `recordings_dir`. The whisper.cpp example
  (`src/cpa_stt/examples/whisper-cpp.json`) is data: large-v3-turbo, `-l auto`, greedy (`-bs 1 -bo 1`),
  Silero VAD when `ggml-silero-*.bin` is in `$CPA_HOME/models`, and your project names as `--prompt`.
  Another engine is another config file. `cpa-stt use` without a file keeps your other settings.
- **Models** live in `$CPA_HOME/models` as links. `setup` looks in `~/.cache/openwhispr/whisper-models` and
  then with Spotlight, shows the largest match and links it after a yes. Any model it links must be yours
  (or root's), not writable by others (nor its folder), not under `/Volumes`, and a ggml file. If the app
  that owns the file moves or deletes it, recordings wait (`model_missing`) until you link it again.
- **Sandbox**: every run is `sandbox-exec` with a generated profile: no network, writes only to the run's
  empty 0700 work directory and the Metal shader cache, two Mach services. It does not block reads. No
  `sandbox-exec`, no run. Environment `PATH=/usr/bin:/bin`, `HOME`, `LANG` only; stdin closed; stderr
  dropped; stdout up to 256 KiB plus 160 B per second of audio, and a run that prints more fails rather
  than leaving a cut transcript.
- **What it writes** in a recording folder, each atomically (dot temp file 0600, fsync, rename, folder
  fsync): `status.json` (`by: "cpa-stt"`) → `transcript.json` → `transcript.md` → `result.json` (linked in
  last, `level: "transcript"`, memos get `memo.text`). A folder with `result.json` is never touched.
  Nothing heard (silence gate, VAD, known phantom lines, the prompt echoed) = no sentences, no memo text,
  and `transcript.md` says "(nothing heard)" (（没听到）).
- **One transcriber at a time**: it claims `recordings/.transcriber` (created with `link()`, so only one of
  two starters wins) and renews it every 60 s, also when idle; another program's claim younger than 5
  minutes means it writes nothing. The claim is checked during every command run and before every
  transcript write; when it is gone, the run is killed and nothing more is written. Another program's
  `status.json` or transcript changed in the last 5 minutes means that folder is left alone and reported;
  older ones are leftovers it may overwrite (never `result.json`). `cpa status` shows who holds the claim.
- **Failures**: a broken environment (model or binary missing, no sandbox, self-test fails) only writes
  `status.json` blocked (`model_missing` / `engine`) and waits; the self-test runs again at most once a
  minute, and `cpa-stt retry` clears it at once. A file the command fails on, or a folder it cannot read
  or write, is retried after 30 s, 2 min and 8 min (`status.json` queued), then gets `failed / E_STT` or
  `failed / E_IO` (retryable); the other folders go on. Logs (`$CPA_HOME/stt/log/`, rotated at 1 MiB) hold
  ids, lengths, timings and exit codes only.

## Layout (`src/cpad/`)

| package | role | pure? |
|---|---|---|
| `protocol/` | limits from `../protocol/limits.json`, hand-written validators, codec, frozen-dataclass messages | yes |
| `device/` | framing, credit window, handshake verdicts, port choice (pure); SafeOpen and `DeviceLink` (shell) | mixed |
| `hub/` | `HubState`, `reduce()`, snapshots; `EffectRunner` executes effects in order (shell) | mixed |
| `events/` `queue/` `notes/` | event records and queue fold (pure); `EventLog`, `NotesWriter` (shell) | mixed |
| `recordings/` | pull plan, verify, manifest, wall time, contract reader, note lines, sessions, history pages (pure); `RecPuller`, landing, `ResultWatcher`, retention, `Recordings` (shells) | mixed |
| `projects/` `inbox/` | projects.json rules, cwd → project, the `need` rule | yes (git call cached) |
| `agents/claude_code/` | `agents --json` parsing, argv, spawn / reply flows | adapter is a shell |
| `control/` `service/` `logging/` `cli/` `config/` | socket, LaunchAgent, redaction, `cpa`, `$CPA_HOME` | shells |
| `app.py` | the daemon: supervisor, poller, housekeeping, link handlers | shell |

## Claude Code facts this code relies on (docs/gates/cli-gates.md, CLI 2.1.272)

- spawn: `claude --bg --name <key>/<slug> --permission-mode <mode> -- <text>` (mode from projects.json, default
  `plan`, `bypassPermissions` refused). Exit code 0 is not success: the short id comes from stdout line 1
  (`^backgrounded · ([0-9a-f]{8})\b`) or a listing diff by name, then `agents --json --all` must not show
  the session `failed` / `crashed`.
- reply (only with `experimental_bg_reply: true`): `claude stop <short>`, then `claude --bg --resume <sessionId>
  -- <text>` with **no other flag** (any flag forks a copy); "started a copy" on stderr, another id on stdout or
  a new same-name session in `--all` means `E_REPLY_FORKED` and reply stays off for the rest of the run.
- `agents --json` prints `kind: "interactive" | "background"` (registry form `bg` accepted); `daemon` /
  `daemon-worker` and unknown kinds are skipped; unknown statuses become `unknown`.

## Tests

```sh
uv run pytest tests/unit tests/contract -q      # pure logic + contract (reads ../protocol/vectors/v0)
uv run pytest tests/integration -q              # pty FakeDevice + tests/fixtures/bin/fake-claude, ~30 s
uv run pytest --cov=cpad --cov=cpa_stt --cov-fail-under=80
CPA_TEST_WHISPER=1 uv run pytest tests/integration/test_stt_sandbox.py   # real whisper-cli in the sandbox
```

Integration tests never open a real serial port or start a real Claude session: the daemon gets an injected
port list that only contains the FakeDevice pty, and `claude_bin` points at `fake-claude`. `PtySerial`
runs pyserial's real open/read/write on the pty and records (RTS, DTR) instead of the ioctls a pty rejects.
