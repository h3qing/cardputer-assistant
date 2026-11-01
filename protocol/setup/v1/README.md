**English** · [中文](README.zh-CN.md)

# First-run setup contract v1 (`cpa setup check --json`)

The machine-readable version of docs/27 §2.3–§2.6. `cpa setup check --json` prints one JSON object per line (JSON Lines). Each line is one setting on this Mac. The Mac app's first-run setup page (docs/27 O6) and the terminal both read it. `cpa` only reads; it changes nothing and returns within 5 s (no Spotlight, no login shell, no network, no serial port).

```
protocol/setup/
├── v1/
│   ├── README.md                 # this file
│   ├── schema/row.schema.json    # JSON Schema for one row (Draft 2020-12)
│   ├── vectors/valid/*.json      # rows that must be accepted
│   ├── vectors/invalid/*.json    # rows that must be rejected, each with its reason
│   └── VECTORS.sha256
└── tools/check_setup.py          # reference validator + contract self-check; tests in the same directory
```

```bash
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/setup -q
uv run --with 'jsonschema>=4.23,<5' python protocol/setup/tools/check_setup.py   # exit code 0
```

The daemon's `tests/contract/test_setup_contract.py` runs every row, under every combination of facts, through `check_setup.py`, and checks that `mac/Fixtures/setup-v1` is an identical copy.

## One row

| Field | Meaning |
|---|---|
| `v` | 1 |
| `id` | `service notes name import device profile disk stt hooks claude mcp dest` (`x-knownValues`, only ever added to); today's `cpa` prints `service notes name device disk stt hooks claude` (`claude`: whether the background service can find Claude Code, `claude_bin` in `config.json`; when it is not found, the row says to run `cpa service install` again after installing it) |
| `state` | `ok` done · `act` needs your click · `wait` waiting for the device · `warn` works but should be changed · `off` optional, not set up · `skip` you said not needed · `na` not applicable · `err` broken |
| `required` | Only required rows (`service notes device`) count toward "N steps left" |
| `blocked_by` | Until these rows are done, this row's buttons are grey (the device row waits for the background service and the notes folder) |
| `title`, `detail` | For people to read; passed through `display_safe` |
| `fix[]` | Buttons: `{op, label?, cand?}`. `op` must be from the verb table: `service.install`, `hooks.install`, `host.name`, `stt.setup`; `notes.dir`, `device.bind`, `device.forget`, `settings.import`, `setup.skip` are reserved for docs/27 O2–O6. The app builds argv from its own verb table and never copies any string from the row; an unknown `op` gets no button |

**Never appears**: `host_id`, the USB serial number (`serial`), `dev`. The board only uses the device code (the first 4 characters of `sha256("cpa-dev:" + serial number lowercased with colons removed)`, uppercase, docs/27 D6). `check_setup.py` rejects any of these three keys at any depth.

## `--quiet`

Prints nothing. Exits 0 when every required row is `ok`, otherwise 1. The install script uses it to decide whether to end with "done" or list what is still missing.
