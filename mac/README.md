**English** · [中文](README.zh-CN.md)

# Cardputer Assistant for Mac

The Cardputer's own Mac app: one window (Inbox · Recordings · Projects · Device · Integrations · Settings) plus a menu bar icon. The UI is in Chinese and English (follows the system; you can change it in Settings, docs/24 §2.7). The design is in
[docs/24](../docs/24-mac-app.md).

## Public interfaces only

It is just an ordinary client of the public interfaces, with no private channel:

- `cpa … --json` (`status`, `rec ls`, `project`, `filing`, `dest`, `mcp`, `export`), run directly with an argument array, not through a shell, with a timeout;
- the open files in the notes folder (default `~/CardputerNotes`), read-only;
- the local transcriber `cpa-stt` (`status --json`, `use`, `off`, `setup`, `service install`, `retry`).

It does not connect to cpad's socket, does not read `~/Library/Application Support/CardputerAssistant`, does not go online, and no app's name appears
in its code, comments or tests; a source-scan test (`RulesTests`) pins these rules. The only thing that changes anything is the `cpa` / `cpa-stt` command you clicked.
So anything it can do, any app can do with the same commands. The Integrations page says how to connect: first "Destinations" (send what you say to any app, via `cpa dest … --json`),
then, for developers, MCP, command line + JSON, files, the transcriber, export, and "Copy the connection guide" (a piece of Markdown you paste into any app's coding assistant).

## Build, run, test

Needs macOS 14+ and the command line tools (`xcode-select --install`; tried on Swift 6.2), not Xcode. Run everything in `mac/`:

| What | Command |
|---|---|
| Build | `swift build` |
| Run directly (for development) | `swift run CardputerAssistant` |
| Test (a built-in assertion program; non-zero exit code on failure) | `swift run CardputerCoreTests` |
| Draw every screen as PNG (fake data; English in `out/shots/`, Chinese in `out/shots/zh/`, `00-overview.png` is the overview; real off-screen windows, `-full` whole pages, menu bar) | `swift run CardputerShots` (or `--out dir`, `--lang en\|zh`) |
| Draw the same screens read-only with this Mac's real cpa (the images contain real text; do not commit them) | `swift run CardputerShots --live --out dir` |
| Build the `.app` (release, Mando icon, ad-hoc signed and verified) | `scripts/build-app.sh` → `build/Cardputer Assistant.app` |
| Install to `~/Applications` (only when you run it) | `scripts/install-app.sh` |

- The `.app` version comes from the repo's latest `v*` tag (`v0.4.0` → 0.4.0); with no tag it is 0.0.0.
- Icon: `scripts/export_mando.py` draws the firmware's Mando sprite at 1024 px, `sips` scales it to each size, and `iconutil` makes `AppIcon.icns`.
- No App Sandbox (the sandbox would pass on to the `cpa` child process); built and installed only on this machine, not notarized, not distributed.
- CI: the `mac` job in `.github/workflows/ci.yml` runs build, tests, screen drawing (PNGs uploaded as the `mac-shots` artifact) and
  `build-app.sh` on a macOS runner; it runs only when `mac/` or files it reads (`tools/theme/`, the firmware sprites, `ci.yml`) change, and does not install.

## Layout

- `Sources/CardputerCore`: pure logic (contracts, commands, filters, keys, undo, refresh scheduling, capability detection), depends only on Foundation, tests first.
  UI text is written as `tr("中文 {0}", value)`: the Chinese text is the key; the English table is in `Localization/EnglishTable*.swift`, and `L10nTests` checks every `tr()` has an English entry, matching placeholders, and no Chinese that bypasses `tr`.
- `Sources/CardputerUI`: SwiftUI screens, the Mando theme, models; `Sources/CardputerAssistant`: the App and the menu bar.
- `Sources/CardputerCoreTests`: the test program; `Sources/CardputerShots`: draws screens from the fake data in `Fixtures/` (with `--live` it runs the real `cpa`, but only lets read commands through).
- `Fixtures/`: copies of the filing/v1 and dest-cli/v1 vectors (with `SOURCE`), fake `cpa dest` output in `dest/`, fake `cpa` output and a fake notes folder; no real data.
- `scripts/export_mando.py`: generates `MandoSprites.swift` and the app icon from the firmware sprites (source and license in NOTICE).
