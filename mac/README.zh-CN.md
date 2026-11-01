[English](README.md) · **中文**

# Cardputer Assistant for Mac

Cardputer 自己的 Mac app：一个窗口（待归档 · 录音 · 项目 · 设备 · 集成 · 设置）加一个菜单栏图标；界面有中文和 English（跟随系统，设置里可以改，docs/24 §2.7）。设计在
[docs/24](../docs/24-mac-app.zh-CN.md)。

## 只用公开接口

它只是公开接口的一个普通客户端，没有私有通道：

- `cpa … --json`（`status`、`rec ls`、`project`、`filing`、`dest`、`mcp`、`export`），用参数数组直接跑，不经过 shell，有超时；
- 笔记目录（默认 `~/CardputerNotes`）里的开放文件，只读；
- 本地转写程序 `cpa-stt`（`status --json`、`use`、`off`、`setup`、`service install`、`retry`）。

它不连 cpad 的 socket，不读 `~/Library/Application Support/CardputerAssistant`，不联网，代码、注释和测试里不出现
任何 app 的名字；源码扫描测试（`RulesTests`）钉着这几条。唯一会改东西的，是你点了的那条 `cpa` / `cpa-stt` 命令。
所以它能做的，任何 app 照同样的命令都能做——集成页写着怎么接：先是"去向"（说的话送到任何 app，经 `cpa dest … --json`），
再是给开发者的 MCP、命令行 + JSON、文件、转写程序、导出，和"复制接入说明"（一段 Markdown，贴给任何 app 的编程助手）。

## 造、跑、测

要 macOS 14+ 和命令行工具（`xcode-select --install`；在 Swift 6.2 上试过），不要 Xcode。都在 `mac/` 里跑：

| 做什么 | 命令 |
|---|---|
| 编译 | `swift build` |
| 直接跑（开发用） | `swift run CardputerAssistant` |
| 测试（自带的断言程序，失败时退出码非 0） | `swift run CardputerCoreTests` |
| 把每一屏画成 PNG（假数据，英文在 `out/shots/`、中文在 `out/shots/zh/`，`00-overview.png` 是总览；屏幕外的真窗口、`-full` 整页、菜单栏） | `swift run CardputerShots`（或 `--out 目录`、`--lang en\|zh`） |
| 用这台 Mac 的真 cpa 只读地画同样这些屏（图里是真文字，别提交） | `swift run CardputerShots --live --out 目录` |
| 造 `.app`（release、Mando 图标、ad-hoc 签名并校验） | `scripts/build-app.sh` → `build/Cardputer Assistant.app` |
| 装到 `~/Applications`（只在你跑它时） | `scripts/install-app.sh` |

- `.app` 的版本号取仓库最新的 `v*` tag（`v0.4.0` → 0.4.0）；没有 tag 时是 0.0.0。
- 图标：`scripts/export_mando.py` 把固件的 Mando sprite 画成 1024 px，`sips` 缩成各尺寸，`iconutil` 生成 `AppIcon.icns`。
- 不进 App Sandbox（沙箱会传给 `cpa` 子进程）；只在本机造、本机装，不公证、不分发。
- CI：`.github/workflows/ci.yml` 的 `mac` 任务在 macOS runner 上跑编译、测试、画图（PNG 作为 `mac-shots` artifact 上传）和
  `build-app.sh`；只在 `mac/` 或它读的文件（`tools/theme/`、固件的 sprite、`ci.yml`）有改动时跑，不装。

## 目录

- `Sources/CardputerCore`：纯逻辑（契约、命令、筛选、按键、撤销、刷新调度、能力检测），只依赖 Foundation，先写测试。
  界面上的字都写成 `tr("中文 {0}", 值)`：中文就是键；英文表在 `Localization/EnglishTable*.swift`，`L10nTests` 查每个 `tr()` 都有英文、占位符一致、没有不经 `tr` 的中文。
- `Sources/CardputerUI`：SwiftUI 各屏、Mando 主题、模型；`Sources/CardputerAssistant`：App 和菜单栏。
- `Sources/CardputerCoreTests`：测试程序；`Sources/CardputerShots`：用 `Fixtures/` 的假数据画图（`--live` 时跑真的 `cpa`，但只放行读的命令）。
- `Fixtures/`：filing/v1 和 dest-cli/v1 向量的副本（带 `SOURCE`）、`dest/` 假的 `cpa dest` 输出、假的 `cpa` 输出和一个假的笔记目录，没有真数据。
- `scripts/export_mando.py`：从固件的 sprite 生成 `MandoSprites.swift` 和 app 图标（来源和许可证见 NOTICE）。
