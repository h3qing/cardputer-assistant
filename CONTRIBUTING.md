**English** · [中文](CONTRIBUTING.zh-CN.md)

# Contributing (draft)

Contributions are welcome. Interfaces will still change during v0.x, so open an issue first to say what you plan to do. CI checks the rules below, and reviews look at them too.

## Read these first

- What we build: [docs/04-prd.md](docs/04-prd.md); how it is built: [docs/05-architecture.md](docs/05-architecture.md).
- The protocol's single source of truth is 05 §6; the machine-readable version is in [`protocol/`](protocol/). The process for changing the protocol is in [protocol/README.md](protocol/README.md).
- Important decisions are written as ADRs in [docs/adr/](docs/adr/).
- Docs are written in English at their normal path. A Chinese version, when there is one, sits next to it as `<name>.zh-CN.md`, and line 1 of each file links to the other. When you change one, change the other in the same PR.

## TDD: RED → GREEN → REFACTOR

1. **RED**: write the test first, run it, and confirm it fails because "the feature does not exist yet".
2. **GREEN**: write just enough implementation to make the test pass.
3. **REFACTOR**: tidy the code while the tests stay green.

- When fixing a bug, first write a test that reproduces it.
- When a test fails, change the implementation, not the test to suit the implementation (unless the test itself is wrong; say so in the PR).
- Coverage target 80% (firmware pure logic and the daemon counted separately). The HAL is not counted in coverage; it is covered by HIL on the real device.
- All three test layers are required: unit, integration (the daemon against a pty fake device and a fake `claude`), and end to end (HIL on the real device plus a manual checklist).

## Code size and structure

| Rule | Limit | Check |
|---|---|---|
| File length | 200–400 lines is right; over 400 warns, over 800 fails | `python3 tools/repo/check_sizes.py --warn 400 --fail 800 <path>` |
| Function length | < 50 lines | `uvx lizard -L 50 -w <path>`; from WP4 the firmware also uses clang-tidy `readability-function-size` |
| Nesting | ≤ 4 levels | clang-tidy `NestingThreshold 4`; Python uses ruff `C901` |

- Organise folders by feature or domain (firmware `lib/<domain>`, `modules/<feature>`; daemon `cpad/<domain>`); no folders by type such as `models/` or `utils/`.
- Simplicity first: no unused features or abstractions; do not write 200 lines for what 50 lines can solve.

## Immutable data

- Business logic is always written as pure functions: `old state + input → new state + side effects`, returning new values and never modifying in place.
- In Python use `@dataclass(frozen=True, slots=True)`, `tuple` and `dataclasses.replace`; persistence is append-only.
- The only exception: inherently mutable hardware resources such as the serial ring buffers, display sprites and file handles, which may appear only in the firmware's `hal/` and `usb_transport/` and in the daemon's I/O shell ([04 §16](docs/04-prd.md) item 5).

## Adding an app (firmware module, v0.4.0)

An app is a module; the interface is in [05 §5.4](docs/05-architecture.md), the design in [13 §10](docs/13-assistant-v2-plan.md). Follow `modules/home/` (Home) or `modules/inbox/` (Inbox). All paths are relative to `firmware/app/`.

1. **Code**: put it in `modules/<name>/`. `<name>_logic.{h,cpp}` holds the value-type UI state and pure functions; `<name>_module.{h,cpp}` is only a thin shell. No heap, no blocking; no Arduino, M5 or esp headers included (CI's include rules check this); the module object is at most 1 KB.
2. **Register**: add a line to the hand-written list in `src/module_list.cpp` (no self-registration; see 05 §5.5 for why). The id matches `^[a-z][a-z0-9_]{1,15}$` and must be unique. Also add the module folder name to `lib_deps` of `cardputer-adv-test` in `platformio.ini` and to `APP_DIRS` in `scripts/stack_budget.py`, so that device tests and the stack budget include it.
3. **Shortcut**: single keys and two-key chords only, and each letter stands for one thing; letters in use and reserved are in [13 §5.2](docs/13-assistant-v2-plan.md), and Z is not used. Four places change:
   - `lib/input/keymap.h`: add the new `Action` at the end, leaving the values of existing actions unchanged;
   - `lib/input/keymap.cpp`: bind an `Opt+letter` in `Layer::Global` of `kKeymap` (`test_keymap_static` checks for ghost keys);
   - `lib/shell/shell.cpp`: open your module in `global_key()`;
   - `lib/help/action_help.cpp`: write a one-line description of the new action, shown on the menu's "Go" page (`test_action_help` checks every action has one).
4. **Home tile**: add a tile `{Opt+letter, "letter", "name"}` to `kHomeTiles` in `modules/home/home_logic.h`, and add it to the menu rows `kHelp` in `modules/home/home_module.cpp`. A tile does nothing itself; it only calls `ctx.press(Opt+letter)`, so its effect is always the same as pressing the shortcut. The v0.4.0 nine-tile grid is full, so adding a tile means changing `kHomeTileCount` and the layout, and updating `test_mod_home`.
5. **The menu's "Here" page**: implement `help()`, returning a static array. Each row is `HelpRow{ev, keys, label}`:
   - `ev` is the key pressed for you when Enter is pressed on this row in the menu; `keys` is the key name written at the start of the row; `label` is one line of description, at most 120 px (ASCII 6 px, CJK characters 12 px);
   - for a group of keys (such as `1-9`) leave `ev` empty (`kind` is `None`): shown only, not executed;
   - do not list Tab and Opt+L; the shell handles them;
   - if your module calls `ctx.press()` when it handles a key, write the target key of that `press()` directly in the row (for example Home's rows replay Opt+P, not P), because a replayed key cannot queue another key.
6. **Hint bar**: `hint()` lists at most the 3 most used keys, with "Tab菜单" ("Tab menu") always at the right end, and the whole line fits in 240 px.
7. **Tests** (write them first and watch them fail):
   - `test/ui/test_mod_<name>/`: use `FakeContext`, whose `pressed` records every key `press()` received;
   - `test/ui/test_menu_dispatch/`: add your module to the Rig, press Enter on every row of the "Here" page, and the effect must be exactly the same as pressing that key directly;
   - `test_keymap_static` and `test_action_help` all pass; if you changed keys, check the docs against the first line of the PR checklist below.

## Security check (before every commit)

- No hard-coded keys, tokens or personal data; configuration goes through environment variables or `$CPA_HOME`.
- Inputs are validated at the boundary (protocol fields against the schemas and limits in `protocol/`).
- Subprocesses use argument lists, never a shell; user text goes after `--`.
- Error messages carry only error codes, never body text, paths or command output; logs record only the length of body text and the first 8 characters of its hash.
- Report vulnerabilities privately as described in [SECURITY.md](SECURITY.md).

## Commit messages

Use conventional commits: `<type>: <description>`, where type is one of `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`, `ci`. For example:

```
feat: add outbox crash-injection tests
fix: drop lines longer than 2048 bytes before parsing
```

One commit does one thing; the PR description states the motivation, the approach and how it was verified.

## PR checklist

Tick each item in the PR description; write "N/A" where it does not apply:

- [ ] **Keys**: when `kKeymap` or `action_help` changed, the [Keys](README.md#keys) table in the README and the table in [docs/04 §8](docs/04-prd.md) were updated to match, keys and descriptions alike; single keys and two-key chords only (`test_keymap_static`).
- [ ] **Tests**: tests written first and seen failing; `pio test -e native` and the daemon's `pytest` all pass, coverage not below 80%.
- [ ] **Size**: `check_sizes.py --warn 400 --fail 800` and `lizard -L 50` pass.
- [ ] **Protocol**: only additions, no changes, `v` is still 0; schema, vectors and both codecs change together ([protocol/README.md](protocol/README.md)).
- [ ] **Security**: went through the "Security check" above.
- [ ] **Docs**: all links open; user-visible changes are in [CHANGELOG.md](CHANGELOG.md).

## Real device

- Do not press G0 for everyday flashing (it locks into download mode, see [03 §2](docs/03-hardware-bringup.md)). Before flashing, run `cpa pause` so the daemon releases the serial port.
- Opening the serial port must use the "safe open" order of control lines; (RTS, DTR) = (1, 0) must never occur.
