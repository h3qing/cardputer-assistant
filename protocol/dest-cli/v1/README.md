**English** · [中文](README.zh-CN.md)

# Send-from-Mac contract v1 (`cpa dest … --json`, `cpa filing send --json`)

The external interface for "waiting for you to send" and for sending from the Mac to any destination, such as a Grok bot (docs/25 §5.4, §5.5). The Mac app (docs/24) and your scripts run `cpa` with `execFile` and read the one JSON document it prints. **`cpa` is the only writer**: state lives in `$CPA_HOME/events.jsonl` and `destinations.json`; this contract only covers the shapes that `cpa` prints. Normative source: [docs/25-route-to-any-app.md](../../../docs/25-route-to-any-app.md) §3.1, §3.4, §5.4, §5.5, §7.4.

```
protocol/dest-cli/
├── v1/
│   ├── README.md                 # this file
│   ├── schema/                   # pending / sent / dropped / usual / tested / error + common (Draft 2020-12)
│   ├── vectors/valid/*.json      # documents that must be accepted
│   ├── vectors/invalid/*.json    # documents that must be rejected, each with its reason
│   └── VECTORS.sha256            # sha256 of every vector file
└── tools/check_dest_cli.py       # reference validator + contract self-check, tests in the same folder
```

```bash
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/dest-cli -q
uv run --with 'jsonschema>=4.23,<5' python protocol/dest-cli/tools/check_dest_cli.py   # exit code 0
```

The daemon's `tests/contract/test_dest_cli_contract.py` runs every command for real (with fake LaunchServices and a fake clipboard), and every output must pass `check_dest_cli.py`. The schemas only use keywords the WhisperWoof validator understands (the same allow list as `protocol/filing/v1`), so one validator reads both contracts.

## Commands and documents

| Command | Prints on success | What it writes |
|---|---|---|
| `cpa dest pending [--expired] --json` | `pending` | — |
| `cpa dest send REF [DEST] [--again] [--by cli\|app] --json` | `sent` | `dest.decided{origin:"mac", how:"pick", attempt}`, then `dest.handed` / `dest.delivered` / `dest.failed` |
| `cpa filing send ID DEST [--again] [--by cli\|app] --json` | `sent` | same as above |
| `cpa dest drop REF [--by cli\|app] --json` | `dropped` | `dest.dropped` |
| `cpa dest usual [PROJECT [DEST\|-]] --json` | `usual` | when it changes something: `usual` in `destinations.json`, plus `dest.changed{op:"usual", project, id}` |
| `cpa dest test ID --send --json` | `tested` | when it was handed over: `dest.handed{item:"test-NNNN"}` |
| Any of the above, rejected | `error` | — |

- **`sent`**: `outcome` is `handed` (given to another app: `via:"url"` is a prefilled link, `via:"clip"` is the clipboard plus bringing the app forward; whether it was received is unknown), `delivered` (a local file or clipboard destination), or `failed` (`code`, exit code 1, the item stays in `pending`; `E_INTERRUPTED`: an error or an interruption while sending, for example the app's 30-second timeout killed `cpa`; in that case `dest.failed` is still written before exit). An item sent from the Mac that has been `running` for more than 60 seconds (the process is gone and did not even get to write `dest.failed`) is listed by `pending` as `failed` with `E_INTERRUPTED`; you can send it again or drop it. `why` says why a link fell back to the clipboard: `too_long` (D9), `draft_busy` (the same bot was filled within 30 minutes, D10). It never carries the body text. The Mac app's notices follow it: "Filled into Grok · press Return in Grok to send" (`handed` + `url`, 5 seconds), "On the clipboard · press ⌘V in Grok within 2 minutes" (`handed` + `clip`, stays until you close it; it has "Copy again", which is `--again`).
- **`pending`**: each row of `rows[]` has `ref, why, dest, label, kind, fl, st, code, project, src, at, text`. `why`: `held` (`st` is `hold` or `failed`; `failed` carries `code`), `usual` (a suggestion preselected from the project's usual destination, D21: never sent on its own), `expired` (only with `--expired`). `dest` is the preselected destination; `⌘↩` is `cpa dest send REF` without DEST. `text` has passed `display_safe()` and is at most 280 characters. `fl` is the destination's flag string (docs/25 §2.2); if it cannot be read, treat it as `lck`. When the destination has been deleted, `label` is its id and `fl` is null.
- **`tested`**: a one-time test (docs/25 §3.1, D12). `handed:false` + `E_DRAFT_BUSY`: a real memo filled this bot's draft within the last 30 minutes, and the test would silently drop it, so nothing was handed over; send or clear the draft there first. `--json` never asks anything; the caller asks "Did 'test <marker>' appear?" and "Was it sent automatically?" itself, then runs `cpa dest verify ID --draft` (it appeared and was not sent) or `--act`. If it did not appear, ask the user to paste the bot's link, run `cpa dest map ID '*=link'`, and test again.
- **`error`**: same shape as the `error` of `protocol/filing/v1`; `error` is an extensible enum (show unknown values as they are). `field` only names the argument and never echoes its value. Exit code 2: `E_BAD_ARG`, `E_UNKNOWN_DEST`, `E_UNKNOWN_PROJECT`; all others 1.

| `error` | `field` | When |
|---|---|---|
| `E_BAD_ARG` | `id` | Not a recording id; or a meeting sent somewhere other than the daily note, the to-do list or a local file |
| `E_BAD_ARG` | `ref`, `dest`, `project`, `argv` | Not an id in "waiting for you to send"; not a valid destination id, or it is `daily`, `todo`, `queue`, `session` (to hand to an AI, use `cpa filing hand`); not a project key; an argument argparse does not accept |
| `E_UNKNOWN_ID` | `id`, `ref` | No archived recording with that id; no such item in "waiting for you to send" (including one another `cpa` has just sent or dropped) |
| `E_UNKNOWN_PROJECT` | `project` | Not a project registered on this Mac |
| `E_UNKNOWN_DEST` | `dest` | No such destination |
| `E_DEST_OFF` | `dest` | Turned off, not yet allowed to leave the Mac, or this version has no executor for it (`paste`) |
| `E_OTHER_PROFILE` | `dest` | This destination belongs to another profile (docs/18) |
| `E_NO_TEXT` | `text` | No memo text yet (is transcription on? `cpa-stt use`) |
| `E_TOO_LONG` | `text` | For a destination that needs confirmation on the device: the audio is longer than 40 s or the text is longer than 500 B |
| `E_NEEDS_DEVICE` | `dest` | This destination may send on its own (`fl` contains `k`): only a confirmation on the device counts; if it only fills a draft, test it once with `cpa dest test ID --send` first |
| `E_BUSY` | `ref` | This item is being sent right now |
| `E_ALREADY` | `dest` | This item was already handed to (or written into) this destination; add `--again` to send it again. One extra press never fills the same item twice |
| `E_UNAVAILABLE` | `dests` or none | `destinations.json` cannot be used; or the config or log cannot be read |

Nothing is written when a command is rejected. With `--json`, an argument argparse does not accept also prints `error` (`E_BAD_ARG`, `field:"argv"`, exit code 2). **Exit code 2 with an empty stdout** means this `cpa` is older than this contract.
