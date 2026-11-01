**English** · [中文](README.zh-CN.md)

# Protocol v0 contract

The NDJSON protocol between the firmware `cpa-fw` and the daemon `cpad`. **[docs/05-architecture.md §6](../docs/05-architecture.md) is the single source of truth for the spec**; this directory is its machine-readable version, and the contract tests on both ends read the same files here:

- Firmware: `firmware/app/test/core/test_codec_vectors`, `test_link_flows`, `test_limits_match_json`, and `firmware/app/test/ui/test_rec_flows` (`flows/rec/`);
- daemon: `daemon/tests/contract/`.

```
protocol/
├── limits.json                     # values and formats from 05 §6.1–6.4, §6.7 (read or mirrored by both ends)
├── schema/v0/common.schema.json    # shared definitions: envelope, IDs, restricted strings, enums
├── schema/v0/<type>.schema.json    # one per message type (all types in 05 §6.5, including debug.*)
├── schema/meta/                    # format of the vector files themselves
├── vectors/v0/messages/<type>.json # per message type: valid cases + invalid cases
├── vectors/v0/flows/<name>.json    # multi-message timelines: handshake, TX parked, replay de-dup…
├── vectors/v0/flows/rec/<name>.json # flows where the host asks for files (file.*): only a device with the recording engine can answer
├── vectors/v0/flows/app/<name>.json # flows where an app on the device sends rec.* (v0.4.1): the link core cannot send these requests itself
├── tools/                          # check_vectors.py (contract check) and its tests
│   ├── fixtures/v02_cases.sha256   # digest of every v0.2 case: add only, never change (test_additive.py)
│   ├── fixtures/v031_cases.sha256  # digest of every case and flow (including flows/rec/) in v0.1–v0.3.1, same rule
│   └── fixtures/v040_cases.sha256  # digest of every case and flow (including flows/rec/) up to v0.4.0, also append only
├── recording/                      # recording file contract (cpad ↔ WhisperWoof), see recording/v1/README.md
├── filing/                         # filing contract: output of `cpa filing … --json` (docs/23), see filing/v1/README.md
└── setup/                          # first-run setup contract: rows of `cpa setup check --json` (docs/27 §2.6), see setup/v1/README.md
```

## Checks

```bash
uv run --with 'jsonschema>=4.23,<5' python protocol/tools/check_vectors.py          # exit code 0, prints case counts per type
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/tools -q          # the tool's own tests
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/recording -q      # recording file contract v1
uv run --with 'jsonschema>=4.23,<5' python protocol/recording/tools/check_recording.py
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/filing -q         # filing contract v1
uv run --with 'jsonschema>=4.23,<5' python protocol/filing/tools/check_filing.py
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/setup -q          # first-run setup contract v1
uv run --with 'jsonschema>=4.23,<5' python protocol/setup/tools/check_setup.py
```

`check_vectors.py` checks:

1. Every schema is valid Draft 2020-12, and limits with the same name in `limits.json` and the schemas agree (`schemas.LIMIT_POINTERS`);
2. Every message type has a vector file with at least 1 valid and 1 invalid case; conversely, every vector file has a schema;
3. Valid cases decode as ok; invalid cases decode to the declared error code (`E_BAD_FIELD` must also match the single `field`);
4. `outbox.item` accepts no execution kind (`spawn`, `reply`, `feedback`, `agent.spawn`, `agent.reply`), and each has a matching invalid case;
5. `agent.spawn` covers all the malicious bodies in 05 §9.2;
6. `file.*` has all the invalid cases required by docs/12 M3-P (`check_vectors.REQUIRED_FILE_CASES`), and the `crc` of every valid `file.data` case equals the CRC-32 of the bytes decoded from `b64`; the v0.4.1 messages have all the invalid cases required for acceptance in docs/13 §12.2 (`rec_rules.REQUIRED_REC_CASES`);
7. All 20 required flows are present (host-sent `file.*` in `flows/rec/`, device-sent `rec.*` in `flows/app/`, the rest in `flows/`), and they meet the invariants in the "Flows" section below.

In addition, `tools/test_additive.py` uses `tools/fixtures/v02_cases.sha256` to guard every v0.2 message case and every flow: you can only append at the end; no existing case may change by a single byte (the digest is over the case's canonical JSON: sorted keys, compact, ASCII-escaped). This digest was generated before the M3 changes and is never regenerated. `tools/fixtures/v031_cases.sha256` guards, the same way, every message case and every flow (including `flows/rec/`) as released in v0.3.1; it was generated from the `v0.3.1` tag before the v0.4.0 changes and is also never regenerated. `tools/fixtures/v040_cases.sha256` guards, the same way, all cases and flows up to v0.4.0 (including `flows/rec/`); it was generated before the v0.4.1 changes. When v0.4.0 was merged, the cases added for v0.3.2's `debug.codec` and V40-P's `debug.state.ui` were appended to it; beyond that it does not change.

## Versioning rules (05 §6.9)

- `v` in the envelope is the major version, fixed at `0` during v0. Any other value gets `E_VERSION`.
- When **only adding** fields or message types, `v` does not change: the older end ignores fields it does not understand; a type it does not understand gets `E_UNKNOWN_TYPE` (ignored if it is a reply). So every schema allows unknown fields and none says `additionalProperties: false`.
- When **changing meaning or removing a field**, `v` goes up by 1, new `schema/v1/` and `vectors/v1/` are created, and the old version directories stay until neither end supports them.
- Order for any protocol change: first change 05 §6 → then the schemas and vectors here (RED: the contract tests on both ends go red) → finally the implementations on both ends.
- Capabilities are negotiated via `agent` and `caps` in the host's `hello`. The type names reserved in §6.10 (`voice.*`, `approval.*`, `log`, `pair.*`) get no schema until they are implemented.

## Schema conventions

- Each schema declares its direction with `x-dir`: `h2d` (host→device), `d2h` (device→host).
- Envelope: messages that are not replies (sent by the host on its own, or device requests) use `common#/$defs/request` and **must not carry `re`**; replies use `common#/$defs/reply` and **must carry `re`**.
- Optional fields may be omitted, or be `null` (05 §6.2).
- Custom keywords (ignored by standard validators, enforced by `check_vectors.py`):
  - `x-maxBytes`: limits string length in **UTF-8 bytes** (`maxLength` counts characters);
  - `x-uniqueBy`: a field in an array of objects must not repeat (e.g. `slot`, `sid`, `qid`);
  - `x-maxPerKey`: how many times the same value of a field may appear in an array of objects (queue: ≤4 per project);
  - `x-b64Len`: the base64 string named by `data` in an object must decode to exactly as many bytes as the integer named by `len` (`b64` and `n` of `file.data`); a mismatch is `E_BAD_FIELD b64`.
- `pattern` follows ECMA-262: `$` only matches the end of the string (Python `re`'s `$` also matches before a trailing newline; the tools correct for this).

## Receiver decision order (both ends' codecs follow this order)

Reference implementation: [`tools/wire.py`](tools/wire.py).

| Step | Condition | Result | Reply? |
|---|---|---|---|
| 1 | After dropping every `\r`, a line (including `\n`) is longer than `line_max`=2048 bytes | `E_TOO_LARGE` | No (no `id` available), count only |
| 2 | Does not start with `{` | noise | No, count only |
| 3 | Not a JSON object (including `NaN`, truncation, trailing garbage) | `E_PARSE` | No, count only |
| 4 | `v` missing or not an integer / `v`≠0 | `E_BAD_FIELD v` / `E_VERSION` | see below |
| 5 | `t`, `id`, `re` badly formed | `E_BAD_FIELD t\|id\|re` | see below |
| 6 | Unknown `t` | with `re`: ignore; otherwise `E_UNKNOWN_TYPE` | see below |
| 7 | Invalid UTF-8 in any string | `E_BAD_FIELD <path>` | see below |
| 8 | Does not match the type's schema | `E_BAD_FIELD <path>` | see below |

**Who gets a reply**: only messages with "a valid `id` and no `re`" get a reply, and exactly one; messages with `re` (replies) are never replied to, and errors are only counted. `field` is a path of the form `a.b[0].c`, for example `sessions[3].name`.

**Reply type for each request**:

| Request | Normal reply | On error |
|---|---|---|
| `hello` (host) | `hello` (device, `role:"device"`) | `error` (e.g. `E_VERSION`) |
| `ping` | `pong` | `error` (e.g. `E_NOT_READY` before handshake) |
| `time.set`, `*.snapshot`, `debug.key`, `debug.reboot` | `ack` | `error` |
| `debug.state` (request) | `debug.state` (device summary, with `re`) | `error` |
| `outbox.item` | `outbox.ack` (`stored` / `duplicate`) | `outbox.ack` (`rejected`, must carry `code`) |
| `agent.spawn`, `agent.reply` | `agent.result` (`ok:true`) | `agent.result` (`ok:false`, must carry `code`) |
| `queue.done`, `inbox.dismiss` | `ack` | `error` |
| `file.list` (v0.3) | `file.items` | `error` (`E_BUSY` msg `live`/`full`, `E_NOT_READY`) |
| `file.get` (v0.3) | `file.data` | `error` (`E_BUSY`, `E_UNKNOWN_ITEM`, `E_STORAGE`, `E_NOT_READY`) |
| `file.del` (v0.3) | `ack` (also `ack` if the file does not exist) | `error` (`E_BUSY`, `E_BAD_FIELD size`/`sha256`) |
| `debug.rec` (v0.3, test build) | `ack` | `error` |
| `debug.mic` (v0.3.1, test build) | `ack` | `error` |
| `debug.codec` (v0.3.2, test build) | `ack` | `error` (`E_BUSY live`) |
| `debug.lang` (v0.4.3, test build) | `ack` | `error` |
| `rec.list` (v0.4.1, sent by device) | `rec.items` | `error` (`E_BAD_FIELD`, `E_INTERNAL`; an old cpad replies `E_UNKNOWN_TYPE`) |
| `rec.act` (v0.4.1, sent by device) | `ack` | `error` (`E_UNKNOWN_ITEM`, `E_BAD_FIELD`, `E_INTERNAL`; an old cpad replies `E_UNKNOWN_TYPE`) |
| `dests.snapshot` (v0.5) | `ack` | `error` (old firmware that does not know it replies `E_UNKNOWN_TYPE`; the host only pushes it to firmware that has `dests` in `revs`) |

While the device is Attached (not yet handshaken) it only replies to `hello`; every other host message gets `error{code:E_NOT_READY}`.

## Vector format

### Messages: `vectors/v0/messages/<type>.json`

```json
{
  "type": "agent.spawn",
  "valid":   [{"name": "...", "dir": "d2h", "msg": {...}, "line_b64": "...（可选）"}],
  "invalid": [{"name": "...", "dir": "d2h", "msg": {...}, "code": "E_BAD_FIELD", "field": "text"},
              {"name": "...", "dir": "h2d", "line_b64": "...", "code": "E_TOO_LARGE"}]
}
```

- `dir`: the direction of this message on the wire; the receiver is the end that makes the decision.
- Valid cases must have `msg` (the decoded result, used for a deep semantic comparison). When `line_b64` is present, it is the raw bytes on the wire (exactly one line, ending in `\n`), and must decode to `msg`. (`（可选）` in the example above means "optional".)
- Invalid cases give either `msg` or `line_b64`. With only `msg`, the bytes on the wire are its canonical serialisation: `json.dumps(msg, ensure_ascii=False, separators=(",", ":")) + "\n"`, UTF-8 encoded (05 §6.1).
- An invalid case contains **one** error only, so `field` is unique.
- `code` is only ever one of the receiver decisions: `E_PARSE`, `E_TOO_LARGE`, `E_VERSION`, `E_UNKNOWN_TYPE`, `E_BAD_FIELD`.

### Flows: `vectors/v0/flows/<name>.json`

A timeline. `steps` are in non-decreasing order of `t` (milliseconds). Three kinds of step:

| Step | Fields | Meaning |
|---|---|---|
| A line on the wire | `t`, `from` (`host`/`device`), `msg` or `line_b64`, optional `deliver_t`, `lost`, `outcome`, `note` | `t` is when it is sent. `deliver_t` is when it reaches the other side (only for device TX parking); `lost: true` means the other side never receives it; `outcome` is the receiver's decision, default `ok`, or `noise`, `ignored` or an error code |
| Event | `t`, `event`, optional `args`, `note` | `detach` (cable pulled: both ends' link state is cleared), `daemon_restart` (the daemon process restarts; host IDs are renumbered), `device_reboot` (the device restarts; device IDs are renumbered, back to Attached), `user_spawn` (the user pressed Enter on the confirm box; `args` gives `project`, `text`) |
| Expectation | `t`, `expect`, optional `note` | State that should hold at this moment |

`initial` describes the starting state with the same keys. Keys usable in expectations:

| Key | Meaning | Recomputed by the tool? |
|---|---|---|
| `host.credit_used` | Bytes (including `\n`) the host sent on its own that have neither had a reply nor passed the 5 s reclaim period | Yes |
| `device.outbox_inflight` | Entries the device has sent with no `outbox.ack` yet (resending the same ID does not count twice) | Yes |
| `device.agent_inflight` | The device's in-flight `agent.*` requests (after a 30 s timeout, the result is treated as unknown) | Yes |
| `device.link` | `Detached` / `Attached` / `Ready` | No; for both ends' tests to assert |
| `device.outbox_pending` | Total entries in the device outbox not yet acked (including in-flight) | No |
| `host.link` | `offline` / `handshaking` / `ready` | No |
| `host.items_stored` | Distinct entries the daemon has stored by `(dev, id)` | No |
| `host.queue_added` | Number of `queue.added` events | No |
| `host.spawn_calls` | Number of times the daemon actually called agent spawn | No |
| `host.reboot_detected` | Whether the latest device `hello`/`pong` made the host decide the device had restarted | No |

Invariants `check_vectors.py` checks for every flow (implementation in [`tools/flows.py`](tools/flows.py)):

- Every line decodes to its declared `outcome`, and its direction matches the schema's `x-dir`;
- A delivered host-initiated message gets exactly one device reply (if `detach`, `device_reboot` or `daemon_restart` happens afterwards, those not yet replied to may go unanswered);
- A reply may only answer a message the other side has delivered;
- Host credit ≤ `host_inflight_max`, and the part with no reply after `credit_reclaim_ms` is reclaimed; device in-flight limits are outbox ≤4, agent ≤1, queue/inbox ≤1 (v0.4.1's `rec.list` and `rec.act` also use this one slot, docs/13 §10.2); once the host's `hello` reaches the device (re-handshake), all the device's in-flight requests are cleared and outbox entries go back to waiting to send;
- Expectations marked "recomputed by the tool" in the table above match the recomputed result;
- The v0.3 host-side `file.*` invariants (docs/11 §5.4, implementation in [`tools/file_flows.py`](tools/file_flows.py)):
  - The host only sends `file.list`/`file.get`/`file.del` when the latest `hello`/`pong` it received carries `files`, with `files.live` false and `files.rev` not 0 (old devices without `files` never receive `file.*`), and no more than `file_inflight_max`=4 `file.*` are without a reply;
  - After any `file.*` gets `E_BUSY`, the host sends no more `file.*` until a later `hello`/`pong` shows `files.live:false`;
  - Each `file.*` gets exactly one reply, of the matching type: `file.list`→`file.items`, `file.get`→`file.data` (same `fid` and `off`, `n`≤`len`), `file.del`/`debug.rec`/`debug.mic`/`debug.codec`→`ack`, or `error`;
  - `file.del` is only sent after the host has received every byte `[0, size)` of the file (received only after `deliver_t`; `lost` does not count; bytes received before a disconnect or daemon restart still count, because they are in `.part`).
- The v0.4.1 device-side `rec.*` invariants (docs/13 §12.2, implementation in [`tools/rec_flows.py`](tools/rec_flows.py)):
  - The device only sends `rec.list`/`rec.act` when the latest host `hello` it received carries `caps.recs:true` (it must be received again after `detach` or `device_reboot`; the device cannot see `daemon_restart`, so that has no effect);
  - Reply types match: `rec.list`→`rec.items` or `error`, `rec.act`→`ack` or `error`; `rec.items` has no more entries than the requested `max`;
  - The `cursor` of `rec.list` must be the `next` of some `rec.items` the device received earlier, passed back unchanged (the device does not parse it or make one up).

Required flows: `handshake`, `tx_parking`, `outbox_replay_dup_ack`, `reboot_detect`, `credit_window`, `offline_spawn_becomes_queue`, `agent_result_unknown`; added in v0.3: `old_daemon_ignores_files`, `old_device_no_files`, and in `flows/rec/`: `file_pull_resume`, `file_del_idempotent`, `files_rev_relist`, `file_list_paging`, `file_busy_while_live`; added in v0.4.1: `caps_absent`, `old_daemon_ignores_dest`, `new_daemon_old_firmware`, and in `flows/app/`: `rec_list_paging`, `rec_act_hide`, `rec_list_old_daemon`. There is also `envelope_rules`, covering the handling rules of 05 §6.2.

**`flows/app/`** (v0.4.1): flows where the device sends `rec.list`/`rec.act` must go here (`check_vectors.py` checks this). Only an app on the device (the recording app) sends these requests; the link core in `core/test_link_flows` cannot. The firmware replays them in the V41-R/V41-F app tests; the daemon's `tests/contract/test_flows.py` replays all three directories.

**`flows/rec/`**: flows where the host sends `file.list`/`file.get`/`file.del` must go here (`check_vectors.py` checks this). The firmware's `core/test_link_flows` replays the flows under `flows/` with only the v0.2 link core, which cannot answer `file.*`; `flows/rec/` is replayed by `ui/test_rec_flows` against the full Runtime (with the recording engine running on the host). The daemon's `tests/contract/test_flows.py` replays both directories. Every MonoId in the flows uses an epoch the device can really produce (32 bits, first character ≤`3`, 05 §5.9); so do the recording file contract's vectors (checked by `tools/test_file_vectors.py`).

## What this contract adds to and tightens in 05 §6

Places 05 §6 leaves open that this contract pins down. Changing any of them follows the versioning rules above.

1. **Field formats**: `dev` is 12 lowercase hex digits (the MAC without separators, for example `020000000001`); `fw` matches `^cpa-fw/[!-~]{1,24}$`, and any other prefix is `E_BAD_FIELD fw`; `app` is 1–32 visible ASCII; `rst` matches `^[a-z0-9_]{1,16}$`; `short` is 1–16 visible ASCII (no spaces); `boot` is the first two parts of a MonoId.
2. **Numeric ranges**: `up_ms`, `rev`, `age_s` are uint32; `pending`, `bat_mv`, `run`, `need` (project), `queued`, `total`, `other.*` are 0–65535; `wall_ms`, like `epoch_ms`, is ≥1735689600000.
3. **Body text**: the body of note, queue, spawn and reply is 1–500 **bytes** of UTF-8 with no control characters (U+0000–U+001F, U+007F), so it is always a single line. The protocol layer allows non-ASCII (the device input layer only produces 0x20–0x7E). A body containing `\u0000` is invalid; **no substitution is done**. The daemon replaces control characters with spaces only for input outside the protocol (such as `cpa queue add`).
4. **Project names, session names and queue summaries** also may not contain control characters; `agent.result.name` uses the session name's 32-byte limit (the daemon truncates at a character boundary).
5. **Conditionally required**: a `queue` entry with `intent=feedback` must carry `sid`; `outbox.ack` with `status=rejected` must carry `code`; `agent.result` with `ok=false` must carry `code`.
6. **Uniqueness**: `slot` (projects), `sid` (sessions) and `qid` (queue) must not repeat within a snapshot.
7. **The slot in sessions.snapshot is 1–9**: sessions of unregistered projects are not sent one by one; they only count toward `projects.snapshot.other`.
8. **error**: `field` is 1–48 visible ASCII, `msg` is 1–80 printable ASCII. So no host reply is ever over `host_reply_max`=256 bytes (the credit calculation in 05 §6.3 depends on this).
9. **debug.\*** (only in test firmware; release firmware replies `E_UNKNOWN_TYPE`):
   - `debug.key{keys[1..3], hold_ms?}`: press these keys together, hold for `hold_ms` (default 50, at most 5000), then release; key names from `common#/$defs/keyName` (hwtest's 4×14 layout plus `g0`). Replies `ack`.
   - `debug.state`: the host request has no fields; the device replies with a message of the same name carrying `up_ms`, `link`, `mod` (foreground module), `overlay` (`none`/`compose`/`confirm`), `overlay_ms?`, `key_ms?`, `outbox{pending, inflight, bad}`, `revs`, `need`, `projects[≤9]{slot, run, need, queued}`. Every `*_ms` is a device `up_ms` timestamp; HIL uses them to compute latency.
   - `debug.reboot`: the device replies `ack` first, then calls `esp_restart()`.
10. **`\r`**: the receiver drops every `\r` in a line; the 2048-byte limit applies to the length after dropping.
11. **`limits.json`**, besides the table in §6.7, also holds the constants from §6.1–6.4 that both ends need (retry interval, heartbeat, timeouts, credit reclaim, lines handled per loop, error reply rate limit).
12. **Two optional fields in the device `hello` (v0.2, docs/09 §6)**: additive only, `v` is still 0.
    - `store`: `"sd"` / `"int"` / `"none"`, the main storage chosen at this boot (SD card, internal `cpafs`, no usable storage); any other value is `E_BAD_FIELD store`.
    - `launcher`: boolean, whether the firmware runs under the bmorcelli Launcher; a non-boolean is `E_BAD_FIELD launcher`.
    - Both fields may be omitted or `null`, which mean the same: unknown (v0.1 device). When the firmware does not set them, the encoding is byte-for-byte the same as v0.1; the daemon shows `-`.
13. **The host only ever sends two kinds of line** (docs/09 §6): a JSON object starting with `{`, or an empty line (`\n`, the nudge, used to release parked device replies; see ADR-0001 §5.2). The host never sends a line starting with a letter or any other character. Reason: the Launcher's serial console is open during the splash screen and parses every line it receives as a command; a line starting with `{` only gets `ERR unknown command…`, but a line starting with a letter might happen to be a real command (for example `flash overwrite`, see docs/gates/launcher.md). The daemon's `tests/unit/test_host_lines_never_start_with_word.py` guards this.
14. **Recording transfer (v0.3, docs/11 §5)**: additive only, `v` is still 0, and no schema says `additionalProperties:false`.
    - Optional field `files{n 0–256, rev uint32, live bool}` in the device `hello` and `pong`: `rev` 0 means the boot recovery scan has not finished; `live` means the recording state machine is not Idle. No `files` (or `null`) means recording transfer is not supported (v0.2, nosd), and the host sends no `file.*`. Firmware without recording encodes byte-for-byte the same as v0.2.
    - New messages: `file.list{after?, max? 1–4}` → `file.items{items[≤4], more}`; `file.get{fid, off uint32, len 1–1380}` → `file.data{fid, off, n 0–1380, size, crc, b64}` (`b64` required when `n`>0, absent when `n`=0); `file.del{fid, size, sha256?}` → `ack`; the test build's `debug.rec{seg_ms 10000–600000}` → `ack`. For each entry in `items` see `common#/$defs/fileItem`; `size` includes the 44 B header, max 44 + 600000×32.
    - The `debug.state` reply gains optional `rec{live, mode, seg, files_n, files_rev}` (`mode` and `seg` are null when idle). `rec` may also carry recording health counters (asserted directly by HIL's G-A2 / G-A4 / G-A6; they cover the current recording, hold the previous recording's values when idle, and are reset when the next recording starts; omitted or null means this build does not measure them): `ring_high` (the highest number of full blocks waiting at once in the microphone ring buffer to be written to the card, 0–65535), `mic_missing` (how many times the capture callback found no free block to re-attach; each may lose a block; G-A2 requires 0), `rot_ms_max` (time of the slowest segment switch, ms), `heap_free` / `heap_block` (the lowest free heap and largest free block during recording, bytes); the last four are uint32.
    - `limits.json` gains all the keys of §5.5; those that map to a schema are registered in `schemas.LIMIT_POINTERS`, and the rest (`file_inflight_max`, `rec_min_ms`, `memo_max_ms`, `meeting_max_ms`, `seg_ms_default`) are constants used by the implementations on both ends.
    - Worst-case line lengths (`tools/test_worst_lines.py`, `id`/`re` 24 characters, every number at the maximum the schema allows, booleans `false`): `file.data` (n=1380) 2023 B, `file.items` (4 entries, each with 16 marks and every optional field) 1988 B, `pong` with `files` 238 B, all ≤2048.
    - `enc`, `files.bin` and `revs.recs` are names reserved for later and not created in v0.3 (`git grep -nE '"(enc|bin)"' -- protocol/schema/v0 protocol/limits.json` should find nothing). v0.4.1's `caps.recs` is a capability bit in the host `hello` and is unrelated to `revs.recs`; `revs` still has no `recs`.
15. **The recording file contract** (cpad writes `manifest.json`, WhisperWoof writes the other files) is not a wire protocol; it lives separately in [`recording/v1/`](recording/v1/README.md), with its own schema, vectors, `VECTORS.sha256` and `check_recording.py`.
16. **Microphone gain (v0.3.1, test firmware only)**: additive only, `v` is still 0; release firmware replies `E_UNKNOWN_TYPE`, and the CI and release workflows check that release firmware contains none of the names `debug.(key|state|reboot|rec|mic)`.
    - `debug.mic{pga 0–10, vol 0–255, mag 1–64}` → `ack`. All three fields are required. `pga` is the PGA step of ES8311 REG14, 3 dB per step (0–10 maps to 0 to +30 dB). `vol` is the ADC volume of REG17, 0.5 dB per step; 191 (0xBF) is 0 dB. `mag` is M5Unified's `magnification`; with `over_sampling` fixed at 1, 2 means no amplification.
    - The setting takes effect from the next recording start; the segment being recorded keeps its gain. After a device restart it goes back to the firmware's `kMicGain`. For HIL calibration (`mic_cal`) only.
    - Reply rules are the same as `debug.rec`; before handshake it replies `E_NOT_READY`.
17. **Codec cold-start hook (v0.3.2, test firmware only, #31)**: additive only, `v` is still 0; release firmware replies `E_UNKNOWN_TYPE`, and the CI and release workflow checks now include `debug.codec` (`debug.(key|state|reboot|rec|mic|codec)`).
    - `debug.codec{warm}` → `ack`. `warm` is a required boolean. With the microphone off, the device soft-resets the ES8311 (REG00 = 0x1F), so the codec is back in its just-powered-up state: at the next recording start its output is forced to 0 for about 1 second. When `warm` is true, the boot-time codec warm-up runs again after the soft reset, which simulates a real power-up.
    - While recording (including while still "preparing") it replies `E_BUSY` with `msg` `live` and does nothing. Before handshake it replies `E_NOT_READY`. Only for HIL's `rec_cold_start` and device tests.
18. **Why spawn failed (v0.4, #25)**: additive only, `v` is still 0. `agent.result` gains optional field `why` (`^[a-z][a-z_]{0,15}$`), present only when `ok=false`, `code=E_SPAWN`, and the Mac knows the reason:
    - `untrusted`: the Claude Code CLI does not trust this project folder yet (the stderr of `claude --bg` starts with `Workspace not trusted.`). The desktop app's trust records are separate from the CLI's, so a folder used in the desktop app may still not be trusted by the CLI. The fix is needed once: in a terminal, go to the project folder, run `claude` once and accept the trust prompt. cpad never accepts it for the user.
    - `login`: `claude auth status` in the daemon's environment shows `loggedIn:false` (or stderr says not logged in). In that case cpad does not start the session and replies with this reason directly. Fix: `claude auth login` in a terminal.
    - The device treats an unknown value as if the field were absent. Old firmware ignores `why` and shows `E_SPAWN` as before. The firmware does not round-trip this field yet, so `vectors/` has no case with `why` for now (the firmware's vector test requires decode-then-re-encode to be equal field by field); on the daemon side, `tests/contract/test_spawn_why.py` checks it against the reference schema and confirms that a reply with `why` is still ≤`host_reply_max`.
19. **`debug.state.ui` (v0.4.0, test firmware only, docs/13 §12.1)**: additive only, `v` is still 0. The device reply gains optional `ui{menu, locked}`, both boolean and both required: `menu` means the Tab menu is open, `locked` means the device is locked (Opt+L). Omitted or `null` means a build before v0.4.0; when the firmware does not set it, the encoding is byte-for-byte the same as v0.3.1. While the menu is open, `overlay` still reports the layer below it; **no new values** are added to `overlay`, because the v0.3.1 cpad only knows `none`/`compose`/`confirm`, and a new value would make it reply `E_BAD_FIELD`. The v0.3.1 cpad ignores `ui` (verified by the daemon's `tests/contract/test_old_cpad_compat.py` with the v0.3.1 `specs.py` frozen byte for byte).
20. **The recording app and capture destinations (v0.4.1, docs/13 §8, §9.3, §11.1, §12.2)**: additive only, `v` is still 0, no schema says `additionalProperties:false`, and no new error codes. The recording file contract (`recording/v1/`, `manifest.json`) does not change by a single character.
    - New optional fields in the host `hello`: `caps.recs`, `caps.todo` (boolean); `host_id` (`^[a-z0-9-]{8,32}$`, generated randomly the first time cpad runs; the device does not base any decision on it before v0.8); `host_label` (1–24 bytes, no control characters, only shown on the "Control" page). All may be omitted or `null`; omitted, `null` and `false` mean the same: not supported. Old firmware ignores these fields.
    - `dest`: destination id, `common#/$defs/destId`, `^[a-z][a-z0-9-]{0,11}(\.[a-z0-9-]{1,12}){0,2}$`, at most 24 bytes. It appears in the `note` body of `outbox.item` (`body.dest`) and in each entry of `file.items` (only segment 0 of a memo carries it). Omitted, `null` and `"daily"` all mean today's notes; v0.4.1 firmware only ever sends `"todo"`. When cpad receives a `dest` it does not know, it always stores it as an ordinary entry in today's notes (docs/13 §9.2) and never downgrades it to paste or execute; an old cpad ignores this field, with the same effect.
    - `rec.list{filter, max, cursor?}` (device request): `filter` is `all`/`starred`/`memo`/`meeting`, `max` is 1–`rec_list_max`=6, both required; `cursor` is the `next` of the previous page's `rec.items`, passed back unchanged; without it, it is the first page.
    - `rec.items{items, next?}` (host reply): `items` holds at most `max` `common#/$defs/recRow`, with no `rec` repeated; `next` is present when there is another page. `recRow` requires `rec` (MonoId), `mode` (`memo`/`meeting`), `st` (a value of cpad's `sessions.state_of`: `on_device`, `pulling`, `archived`, `processing`, `blocked`, `done`, `transcript`, `failed`, `stale`); optional `wall_ms` (wall-clock start time; without it cpad uses the archive time), `dur_ms` (uint32), `project`, `starred`, `marks` (number of ★ added while recording), `title` (1–`rec_title_max`=48 bytes, no control characters), `todo_n` (number of unchecked to-dos in `summary.md`). Order and paging are up to cpad: newest wall-clock time first, **not by MonoId alone** (the epoch changes after an NVS erase, docs/13 §8.1); hidden sessions are not listed.
    - `cursor` / `next`: an opaque string, `^[A-Za-z0-9_-]{1,32}$` (`rec_cursor_pattern`, `rec_cursor_max`=32). The device does not parse it; when cpad cannot decode it, it replies `E_BAD_FIELD cursor`.
    - `rec.act{rec, op, project?}` (device request): `op` is `star`/`unstar`/`tag`/`hide`; there is no delete (docs/13 §8.4). `tag` must carry `project`: a project key, or `null` for no project; for other `op`s, `project` may only be omitted or `null`. Replies `ack`; if the session is not found, `E_UNKNOWN_ITEM`. cpad only appends events (`rec.edited`, `rec.hidden`); it never writes, moves or deletes any file and never changes `manifest.json`.
    - In flight: `rec.list`, `rec.act`, `queue.done` and `inbox.dismiss` share the device's "at most 1 in flight" slot. The device only sends them when the latest host `hello` carried `caps.recs:true`; an old cpad replies `E_UNKNOWN_TYPE` (`flows/app/rec_list_old_daemon`).
    - **The only exception to `host_reply_max` is `rec.items`** (`rec_rules.LARGE_HOST_REPLIES`): it is only limited by `line_max`. Because it uses that one in-flight slot, the worst case for the device receive buffer goes from 4096 + 6×256 to 4096 + 5×256 + 2048 = 7424 bytes, still under 8192. Item 4 of 05 §6.3 says the same.
    - Worst-case line lengths (`tools/test_worst_lines.py`): `rec.items` with 6 entries, every field at its maximum, a 48-byte Chinese title and a 32-character `next`: 1580 B; with a title of 48 `"` (each escaped to 2 bytes; no character costs more): 1868 B. `file.items` with all 4 entries carrying `dest:"todo"`: 2044 B; with a `dest` longer than 5 bytes, 4 entries do not fit (2124 B at 24 bytes), so the device lists fewer entries with `more:true`, as `file.items` already allows (firmware `file_fit_page`); 3 entries with a 24-byte `dest` are 1621 B.
    - `limits.json` gains `rec_list_max`, `rec_title_max`, `rec_cursor_max`, `rec_cursor_pattern`, `host_label_max`, `host_id_pattern`, `dest_pattern`, `dest_max`, all registered in `schemas.LIMIT_POINTERS`.
21. **`debug.state.diag` and recording health counters (gh #21, test firmware only)**: additive only, `v` is still 0, no schema says `additionalProperties:false`, and no new error codes.
    - The device reply gains optional `diag{…}` with 16 fields, all required: `heap_free`, `heap_min`, `heap_block`, `stack_left`, `audio_stack_left`, `loop_n`, `loop_max_us`, `loop_win_max_us`, `gap_max_us`, `gap_win_max_us`, `loop_over_20`, `loop_over_50`, `loop_over_100`, `loop_over_500` are uint32; `ghost_n`, `kb_reinit_n` are counts. Omitted or `null` means a build without this object; when the firmware does not set it, the encoding is byte-for-byte the same as before.
    - Meaning: `heap_free`, `heap_block`, `stack_left` are numbers the system page samples once a second (free heap, largest free block, bytes of the loop task stack never used); `heap_min` is the lowest free heap since boot; `audio_stack_left` is the bytes of the audio task stack never used, 0 when there is no audio task (no SD card). `loop_*` are timed with `micros()` by `loop()` in `src/main.cpp`, and one pass includes the keyboard scan: `loop_n` is the pass count (wraps at 2^32), `loop_max_us` is the longest pass since boot, `gap_max_us` is the longest interval between the starts of two adjacent passes since boot (one pass plus time taken by other tasks between passes), `*_win_max_us` are the same maximums over only the last 15–30 seconds (two 15-second buckets), and `loop_over_20/50/100/500` are the numbers of passes over that many milliseconds. `ghost_n` and `kb_reinit_n` are the "ghost" and "reset" counts on the system page. Platforms that do not time the loop (the host simulator, device test programs) report every `loop_*` as 0.
    - From this release, the test firmware fills in the recording health counters of item 14: the audio task resets them at every recording start, updates them after each block (128 ms) is written, and keeps the previous recording's values when idle. `ring_high` and `mic_missing` come from `MicRing` (`mic_missing` counts how many times the capture callback found no free block, or failed to re-attach a block); `rot_ms_max` is the time from the start of writing the block where the segment switches until it is written (including that block's own write); `heap_free` / `heap_block` are the lowest samples taken at recording start and after each block written. When nothing has been recorded since this boot, all five fields are omitted: for test firmware, item 14's "omitted = not measured" also covers "not measured yet". Release firmware does not time the loop or sample these counters.
    - Compatibility: an old cpad does not look at `diag` (the daemon's `tests/contract/test_old_cpad_compat.py` checks each `debug.state` reply vector with the v0.3.1 `specs.py`). Worst-case line length (`tools/test_debug_state_diag.py`): 9 projects, `rec` with every health counter, `ui` and `diag`, every number at the maximum the schema allows: 1464 B ≤ 2048.
22. **New session with an unknown result (v0.4.1, docs/20 U25)**: additive only, `v` is still 0, no new error codes, `limits.json` unchanged.
    - Each item of `queue.snapshot` gains optional field `uncertain` (boolean or `null`). True means the device stored this item when the result of `agent.spawn` was unknown (timeout or disconnect, `body.uncertain` of `outbox.item`), so the session may already have been opened on the Mac. cpad only writes this field when it is true, so ordinary items are byte-for-byte the same as v0.4.0; omitted, `null` and `false` mean the same.
    - New firmware carries it into `QueueRow.uncertain`, and when a session is opened from this item the confirm box says "May have started: see Sessions"; old firmware ignores the field and shows the item as before. New cases are only appended at the end of `queue.snapshot.json` (valid: `uncertain:true` and `null`; invalid: a string → `E_BAD_FIELD items[0].uncertain`); `tools/test_queue_uncertain.py` guards that it is an optional boolean.
    - Not wire protocol, but changed at the same time: when cpad sends a session `name`, it strips its own project's `key/` prefix (the device adds it when displaying, docs/20 U14, 05 §7.6); `events.jsonl` gains `queue.reopened`, carrying the original item's `qid, project, text, intent, sid, origin, uncertain` (`cpa queue reopen`, docs/20 U12, 05 §7.3, §7.7).
23. **Filing contract (docs/23 P1-B, #54)**: like the recording contract, not a wire protocol; it lives separately in [`filing/v1/`](filing/v1/README.md): the JSON printed by `cpa filing … --json` and `cpa project ls --json` (list rows, details, results of write commands, errors, projects), with its own schema, vectors, `VECTORS.sha256` and `check_filing.py`. Protocol v0 and the recording contract do not change by a single character. (2026-10-22, P1-C) Items queued by `cpa filing hand` are ordinary `queue.added` (`origin:"inbox"`); the device board shows them as usual, the summary starts with "语音·" ("Voice·"), and they always open in plan mode; protocol v0 does not change either. (2026-10-22, P1-E) The read-only `cpa mcp` tools `list_unfiled` and `get_filing_item` return the same `ls` and `show` documents (plus `trust`; never including hidden ones; `fw` is null).
24. **Capture destinations (v0.5, [docs/14 §7](../docs/14-destinations.md))**: additive only, `v` is still 0, no new error codes.
    - `dests.snapshot{rev, dests[≤12]}` (host→device, replies `ack`): the destinations the device can offer, ordered by `slot`. Each row is `{id, slot 1–12, label, kind, key?, fl?}`: `id` matches `^[a-z]{3,7}(\.[a-z0-9][a-z0-9-]{0,11})?$` (a built-in destination is just the kind name; a custom one is `<kind>.<name>`, with a name of at most 12 characters, so `id` always fits `dest_pattern` in `limits.json`, docs/25 §2.2); `label` is 1–24 bytes of UTF-8 with no control characters, `"` or `\`, so it is never escaped on the wire; `kind` matches `^[a-z]{3,7}$` (a pattern, not an enum); `key` is a single lowercase letter or null; `fl` matches `^[a-z]{0,8}$` (docs/25 §2.2, in the order `l c p k`): `l` live (a note taken offline must be confirmed with Enter in "needs you"), `c` leaves this Mac, `p` needs a project, `k` each entry must be read in full on the device and confirmed with Enter. cpad sends it for every row, `""` when there are no flags. `slot` must not repeat; `key` may repeat, and the device takes the first row. (2026-10-22, docs/25 R1-B: `fl` replaces the earlier three booleans `live`, `cloud`, `proj`, and the name limit went from 16 to 12.)
    - The device only displays and parses keys; decisions are made only in cpad: it only binds letters in its own alphabet (docs/14 §3.1); an unknown `kind` is still displayed, and behaviour depends only on `fl`; unknown letters in `fl` are ignored; a missing `fl` (or null) is treated as `lck`, preferring to confirm more (docs/25 §4.6); extra fields are ignored. The snapshot is cached in `/cfg/dests.json`, and after boot the cached `rev` is reported in `revs.dests` (0 = none).
    - `rev` is a content hash (CRC-32 of each row's canonical JSON; if it is 0, 1 is used), not a counter: after a cpad restart a counter would start over and collide with an old rev in the device cache. The host only pushes when `revs.dests` differs from its own rev; firmware without `dests` in `revs` never receives it.
    - Worst-case line length (`tools/test_dest_worst_lines.py`, 12 rows, every field at its limit, `id` 20 characters, `fl` 8 letters, `key` as `null`—`null` is 1 byte longer than `"z"`): 1507 B (1843 B with the old three booleans and 16-character names). `label` still forbids `"` and `\`, so no row ever needs escaping.
    - **Merge order**: the firmware's `test_codec_vectors` requires the number of types in the vectors and schemas to equal `kMsgTypeCount`, so the firmware codec learns `dests.snapshot` in the same batch as this schema (2026-10-22, docs/25 R1-A: the 31st message, after `rec.act` and before `debug.*`; release firmware has 25). This firmware release only decodes it and does not accept it: after handshake it still replies `E_UNKNOWN_TYPE`, and does not report `dests` in `revs`, so cpad does not push to it; the cache, display and keys come with F5 (docs/25 §4.6). Fields not defined in the vectors start with `x_` (the firmware's vector test only ignores those).
25. **UI language (v0.4.3, test firmware only, [docs/21 §7](../docs/21-settings.md))**: additive only, `v` is still 0, no new error codes, `limits.json` unchanged.
    - `debug.lang{lang}` (host→device, replies `ack`): `lang` is required, `"zh"` or `"en"`. The device takes the same switching path as pressing Enter on the language row of the settings page: an immediate full-screen redraw, a switch hint, and an NVS write (during recording it is remembered and written after the recording ends). Like `debug.key`, it needs a handshake first; before handshake it replies `E_NOT_READY`. The HIL fixture reads `debug.state.ui.lang` at the start, uses this to set Chinese when needed, and restores the original language at the end.
    - `debug.state.ui` gains optional `lang` (`"zh"` / `"en"`): the language shown now. Omitted means a build before v0.4.3; `menu` and `locked` in `ui` are unchanged. Worst-case line length (`tools/test_debug_lang.py`) 1476 B ≤ 2048.
    - Release firmware replies `E_UNKNOWN_TYPE`; the CI and release workflows check that release firmware contains no `debug.(key|state|reboot|rec|mic|codec|lang)`. The firmware codec, schemas and vectors learn it in the same batch (the 32nd message in the test build, after `debug.codec`; release firmware still has 25).
