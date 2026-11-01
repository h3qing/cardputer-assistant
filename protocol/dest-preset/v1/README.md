**English** · [中文](README.zh-CN.md)

# Destination presets v1 (docs/25 §2.3)

One preset = how to reach one app, as pure data. cpad ships some with the wheel (`daemon/src/cpad/dest/presets/v1/`). Put your own in `$CPA_HOME/presets/*.json` (mode 0600), or use `cpa dest add --preset-file PATH`. No bundle id or link scheme from any preset appears in the code (`daemon/tests/unit/test_dest_no_app_in_code.py`).

- `preset.schema.json`: the format. Unknown fields are always rejected, so there is no place to put a secret.
- `vectors.json`: what cpad must accept (`valid`) and what it must reject (`invalid`). `schema: true` means the schema alone rejects it; the rest are extra cpad rules, listed below.

Beyond the schema, cpad also checks:

1. **Template shape** (parsed with `urllib`, not string concatenation): `<scheme>://<host>/<path>?name=value&…`. Scheme, host and path are literals. `{text}` appears exactly once, and as a whole query value. `{p}` and `{project}` may likewise only be whole query values. Literal values are at most 16 characters. Parameter names must not look like `key|token|auth|secret|sig`. No `#` and no `\`. `focus` uses the same scheme, without `{text}`.
2. **Where it may lead** (`presets/v1/deny.json`, data): denied schemes (`http`, `https`, `file`, `data`, `javascript`, `applescript`, `shortcuts`, `x-apple*` and others), routes (`/mcp/install`, `run-shortcut`, `advanced-uri`, `x-callback-url`) and handler apps (Shortcuts, Terminal, iTerm, Script Editor). On add, the scheme must also be registered in that app's `CFBundleURLTypes`, and cpad records its bundle id and signing Team ID. On send, if they no longer match: `E_HANDLER_CHANGED`, and nothing is opened.
3. **`effect`**: missing = unknown. Until it is tested on this Mac and you answer "it appeared, it did not send by itself" (`cpa dest test ID --send`, `cpa dest verify ID --draft`), it is treated as `act` (each item: read the full text on the device, then press Enter). Only presets shipped with cpad can be tested down to draft-only. Presets that say `act`, and your own presets, are always `act`.
4. **`map` values** (`--map project=value-or-link`): must match `map.pattern`. When you paste a whole link, `map.from_link` extracts the value. Values that start with a known secret prefix are rejected.
5. `label` is at most 24 UTF-8 bytes (the schema can only count characters).

Text handed to an app always carries a header on the same line: `[Cardputer 语音·可能有错字或别人的话·#project] text` ("Cardputer voice · may contain typos or other people's words"; typed text gets `[Cardputer 打字·#project]`, "Cardputer typed"). Control characters (including newlines) become spaces. Length is counted in UTF-16 code units.
