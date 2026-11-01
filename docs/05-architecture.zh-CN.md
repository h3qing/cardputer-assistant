[English](05-architecture.md) · **中文**

# 05 · 架构设计（v0.1）

> 状态：**定稿 v0.1** · 日期：2026-10-19 · 角色：Lead（合并 PRD、复用调研、架构三份草案，并处理评审意见）
> 依据：[02-landscape-report.md](02-landscape-report.zh-CN.md)、[03-hardware-bringup.md](03-hardware-bringup.zh-CN.md)、[01-mac-setup.md](01-mac-setup.zh-CN.md)、`firmware/hwtest/`；本机 Claude Code 2.1.272 的 `--help`、`agents --json`、二进制枚举；core 2.0.17 源码。
> 标记：✅ 本机或真机实测 · 📚 文档或源码 · ⏳ 待验证（都给了退路）
> 读者：固件和 daemon 的实现者、模块贡献者。**做什么**以 [04-prd.md](04-prd.zh-CN.md) 为准，本文只规定**怎么搭**。协议（§6）是两端唯一的契约。

---

## 0. 一页摘要

1. **三块代码，一份契约。** C++ 固件 `cpa-fw`、Python daemon `cpad`（命令行叫 `cpa`）、`protocol/`（JSON Schema、`limits.json`、测试向量）。固件和 daemon 只通过 NDJSON 协议 v0 耦合，契约测试保证两端不漂移。
2. **纯核心 + 命令式外壳。** 两端的业务逻辑都写成纯函数：`step(const State&, const Input&) -> {State, Effects}`。I/O 只在外壳里做。这样既满足"不可变"，所有逻辑也都能在 Mac 上测试。
3. **执行只走在线请求。** outbox 是可靠投递通道，但里面**只装笔记和排队项**。开 session 和回复用 `agent.*` 在线请求发出：设备必须处于 Ready 状态，经过确认框，不持久化，也不自动重试。
4. **v0.1 只接 Claude Code 的 CLI。** 设备在线时每 3 秒轮询 `agents --json`，生成看板和收件箱；开 session 用 `--bg --name … --permission-mode … -- <text>`；回复空闲后台 session 用 `stop` + `--bg --resume`，前提是 G3 通过。不装 hooks，不开任何 TCP 端口。
5. **链路只用 USB-Serial/JTAG（HWCDC）。**
   - 设备：接收缓冲 8KB，每 20ms kick 一次 TX，单行最长 2048B。
   - daemon：安全打开串口；握手重试 3 次，每次 2s；主机发起的数据，未回复部分不超过 4KB（credit 流控）；每 5s 心跳一次。
6. **设备存储只用 LittleFS。** 一个条目一个文件：先写 `.tmp`，关闭后再 `rename`，收到 ack 后删除。条目 ID 用 MonoId，不依赖时间。
7. **固件单线程主循环。**
   - `AppState` 不超过 4KB，用静态双缓冲，按切片做 reducer。
   - loop 栈 16KB。
   - 显示用全屏 8bpp sprite（32KB），中文字体 efontCN_12。
8. **模块在编译期静态注册。** v0.1 只有 `board`、`inbox`、`system` 三个模块。共享的采集页（Compose）和确认框放在 shell 里。模块代码不 include Arduino 或 M5 的头文件，所以能在 native 环境下测试。
9. **v0.1 的安全边界是"物理 USB 连接 + 当前登录的 Mac 用户"。** 设备上没有任何密钥；daemon 校验固件前缀并绑定序列号；子进程直接传 argv，不经过 shell；显式指定权限模式，默认 `plan`；日志不记录正文。

---

## 1. 约束与原则

### 1.1 决定架构的硬约束

| 约束 | 来源 | 架构上怎么应对 |
|---|---|---|
| 没有 PSRAM；空闲堆约 281KB，最大连续块约 254KB | ✅ 03 §1 | 设备只负责输入、显示和 outbox，其余放在 Mac。缓冲区一律定长 |
| 没有 RTC | 📚 02 §1 | 用 MonoId 做 ID；墙钟时间可以缺失（`wall_ms:null`） |
| HWCDC 的第一条回复会被扣住 | ✅ 03 §3 | 固件每 20ms kick 一次；**设备从不主动发起握手**，由主机发送并重试 |
| HWCDC 接收队列满了会**悄悄丢字节**；`setRxBufferSize` 必须在 `begin` 之前调用 | ✅ core 2.0.17 `HWCDC.cpp` | 接收缓冲设为 8192；单行 ≤2048；主机未回复的数据 ≤4096 |
| 打开串口可能复位芯片 | ✅ 03 §2 | 只用安全打开的顺序；要重启设备只能通过固件内部（测试版用 `debug.reboot`） |
| 默认 loop 栈 8192B | ✅ `CONFIG_ARDUINO_LOOP_STACK_SIZE` | `SET_LOOP_TASK_STACK_SIZE(16*1024)` |
| 键盘没有二极管，部分三键组合会出幽灵键 | 📚 02 §6，⏳ G1 | 输入层做"矩形检测"；keymap 只用两键组合，并做静态检查 |
| FatFS 的 rename 不是原子的，LittleFS 的是 | 📚，⏳ G10 | v0.1 只用 LittleFS；SD 推到 v0.2，并且每条记录带校验和 |
| 工具链 A 默认 `-std=gnu++11`、GCC 8.4 | ✅ `platformio-build-esp32s3.py` | 改成 `-std=gnu++17`（G2 时顺带验证）；如果选 B，GCC 14 没有这个问题 |
| 麦克风的问题范围比以为的窄：出在扬声器到麦克风的切换 | 📚 IDF #18621 | 录音不在 v0.1 的关键路径上；工具链由 G2 决定 |
| USB-JTAG 在深度睡眠时会断开 | 📚 02 §8 | v0.1 不睡眠，用电池时只关背光 |

### 1.2 工程规则在架构里怎么落地

| 规则 | 落地方式 |
|---|---|
| 不可变 | 固件：所有逻辑都是纯函数，切片按值返回；`AppState` 放在两个静态缓冲里，外壳把新值组装进另一个缓冲后切换索引，旧状态从不被原地修改。<br>daemon：`@dataclass(frozen=True, slots=True)`、`tuple`、`dataclasses.replace`；持久化只追加 |
| 例外边界（需要你确认，见 04 §16 第 5 条） | 串口收发环、显示 sprite、LittleFS 文件句柄天生是可变的，只允许出现在 `hal/` 和 `usb_transport/` 里。daemon 的串口对象、文件句柄、子进程只出现在外壳里 |
| 文件 200 到 400 行（最多 800），函数 <50 行，嵌套 ≤4 | 按功能拆目录。CI 跑：<br>• `tools/repo/check_sizes.py`：超过 400 行警告，超过 800 行失败<br>• `lizard -L 50`<br>• ruff `C901`、`PLR0915`<br>• WP4 起加 clang-tidy `readability-function-size`（`LineThreshold 50`、`NestingThreshold 4`） |
| 按功能或领域组织 | 固件：`lib/<领域>`、`modules/<功能>`；daemon：`cpad/<领域>`。不建 `models/`、`utils/` 这种按类型分的目录 |
| 不碎片化堆 | 领域代码里不用 `String` 或 `std::string`，统一用 `FixedStr<N>`；ArduinoJson 只用一个复用的 `JsonDocument` |
| TDD，80% 覆盖率 | 见 §9。每个工作包的验收测试都要先写（RED） |
| 安全 | 见 §8 |
| 简单优先 | 一个线程、一条链路、一台设备、一个 AI 适配器、三个模块；运行时只依赖 `pyserial` |

### 1.3 v0.1 明确不做（接口位置已预留）
设备配对、hooks、HTTP/TCP 端口、MCP、mods、SD 卡、录音、审批、WiFi/BLE、Noise、运行时加载模块、manifest 校验、Leader 键、睡眠、OTA、MSC/HID、Codex 适配器、SQLite、pydantic、Starlette。

---

## 2. 组件图

```
┌────────────── Cardputer ADV 固件 cpa-fw（C++17，工具链按 ADR-0001）───────────────┐
│ modules/   board（主页+项目页） │ inbox │ system           ← 只依赖 module_api        │
│              ▲ onKey / render(Canvas&)       │ ctx.compose / request / open / toast │
│ shell/     导航栈 │ 按键分发（浮层→全局→模块） │ Compose │ Confirm │ Toast │ StatusBar  │
│ core/      AppState(≤4KB, 双缓冲) + 切片 reducer │ MonoId │ FixedStr │ Clock │ Result   │
│ input/     KeySet → GhostFilter → ChordDetector → KeyEvent（纯）                      │
│ outbox/    OutboxCore（纯）── FileStore 接口 ── LfsStore（hal）                        │
│ link/      LineFramer（纯）→ protocol/Codec（ArduinoJson）→ LinkSession（纯）            │
│ usb_transport/  HWCDC：RX 8KB、TX 4KB、20ms kick、发送队列 ≤4 行   ⚠ 只在设备上编译    │
│ hal/       M5Canvas 8bpp + efontCN_12、键盘、LittleFS、NVS、电池/背光  ⚠ 只在设备上编译 │
└───────────────────────────────────┬────────────────────────────────────────────────┘
                                    │ USB-Serial/JTAG CDC（303A:1001，绑定序列号）
                                    │ NDJSON 协议 v0（protocol/）
┌───────────────────────────────────┴──────── Mac daemon cpad（Python 3.12 + pyserial）┐
│ device/    PortFinder → SafeOpen → Handshake → DeviceLink（读线程、credit 写队列、心跳） │
│ protocol/  codec + 手写校验器 + limits（读 protocol/limits.json）                       │
│ hub/       HubState + reduce() → Effects；snapshot 构建（有上限、rev、≤2048B）           │
│ projects/  projects.json 校验，cwd → 项目       inbox/  "需要你"推导（防抖、忽略）        │
│ agents/    AgentAdapter 接口 ← claude_code/（agents --json、spawn、reply）              │
│ events/    events.jsonl（只追加 + fsync）+ fold      queue/  从事件折叠出队列            │
│ notes/     每日 markdown（块 ID，幂等）        control/  unix socket（pause、status）    │
│ cli/       cpa daemon|status|project|queue|pause|service|device                       │
└──────┬──────────────────────────────┬───────────────────────────────┬────────────────┘
       │ 子进程（argv，不经过 shell）   │ 文件                           │ AF_UNIX 0600
┌──────┴─────────────────────────┐ ┌──┴─────────────────────────┐ ┌───┴──────────────┐
│ claude agents --json           │ │ ~/CardputerNotes/*.md       │ │ cpa CLI          │
│ claude --bg --name … -- <text> │ │ $CPA_HOME/events.jsonl …    │ │ 固件上传前脚本    │
│ claude stop / --bg --resume    │ └────────────────────────────┘ └──────────────────┘
└────────────────────────────────┘
```

**数据流 1：G0 速记**
1. `G0Tap` 触发，shell 打开 Compose。
2. 用户按 Enter，shell 构造一条 `outbox.item`，`kind` 为 `note`。
3. `OutboxCore` 发出 `WriteFile(tmp)` 和 `Rename` 两个副作用，`LfsStore` 执行。
4. 写入成功后，屏幕显示"已保存"。
5. 链路处于 Ready 时，设备发送这一行。
6. daemon 校验并去重，追加 `item.received` 事件并 fsync，回复 `outbox.ack`，再渲染笔记。
7. 设备收到 ack，删除文件。

**数据流 2：开 session**
1. Opt+S 打开 Compose，用户输入后进入 Confirm。
2. 用户在 Confirm 里按 Enter：
   - 链路 Ready 时，发送 `agent.spawn`（在线请求，最多 1 个在途）；
   - 否则在 outbox 里写一条 `queue(intent=spawn)`。
3. daemon 收到 `agent.spawn` 后依次处理：校验 → 构造 argv → 执行 `claude --bg …` → 解析短 id → 记录 `agent.spawned` 事件 → 回复 `agent.result`。

---

## 3. 仓库结构（标注了负责的工作包）

```
/                                   # Apache-2.0；WP1 负责 git init
├── LICENSE  NOTICE  README.md  CONTRIBUTING.md  SECURITY.md  .gitignore      [WP1；README 快速上手部分由 WP5 补]
├── .github/workflows/ci.yml                                                  [WP1]
├── protocol/                                                                 [WP1]
│   ├── README.md                  # 指向本文 §6；版本规则
│   ├── limits.json                # 唯一的限制表，两端都读（固件用 limits.h 镜像，并有测试比对）
│   ├── schema/v0/<type>.schema.json
│   ├── vectors/v0/messages/<type>.json   # 合法和非法用例（含恶意正文；字节级用例用 line_b64）
│   ├── vectors/v0/flows/<name>.json      # 握手、TX 被扣住、outbox 重放、重启检测、credit、离线执行降级
│   └── tools/check_vectors.py  tools/test_check_vectors.py
├── firmware/
│   ├── hwtest/                    # 现有的探测固件，保持不改（G1、G2 用它）
│   └── app/
│       ├── platformio.ini                                                    [WP2]
│       ├── src/main.cpp  src/module_list.cpp                                 [WP2 先放桩，WP4 接手]
│       ├── lib/core/ lib/protocol/ lib/link/ lib/outbox/ lib/input/          [WP2，纯逻辑，native 可测]
│       ├── lib/module_api/ lib/shell/                                        [WP4，纯逻辑，native 可测]
│       ├── lib/hal/ lib/usb_transport/                                       [WP4，⚠ 只在设备上编译]
│       ├── modules/board/ modules/inbox/ modules/system/                     [WP4]
│       ├── scripts/check_budget.py  scripts/pre_upload.py                    [WP4]
│       └── test/core/<suite>/  test/ui/<suite>/  test/device/<suite>/        [WP2: core；WP4: ui、device]
├── daemon/                                                                   [WP3]
│   ├── pyproject.toml  uv.lock    # 包名 cpad；入口 cpa；运行时依赖只有 pyserial==3.5
│   ├── src/cpad/{app.py, cli/, config/, protocol/, device/, hub/, projects/, agents/claude_code/,
│   │             inbox/, queue/, events/, notes/, control/, service/, logging/}
│   └── tests/{unit/, integration/, contract/, fixtures/{bin/fake-claude, agents/*.json}}
├── tools/
│   ├── repo/check_sizes.py  repo/test_check_sizes.py                         [WP1]
│   └── hil/run_e2e.py  hil/scenarios/  hil/tests/                            [WP5]
└── docs/  01..03（已有）  04-prd.md  05-architecture.md  07-hil-checklist.md [WP5]  adr/0001-toolchain.md  adr/0002-protocol-v0.md [WP1]
```

规则：
- `lib/` 下面除 `hal/` 和 `usb_transport/` 以外，`modules/` 也算在内，都**不许 include `Arduino.h`、`M5*`、`esp_*`**。CI 用 grep 检查；native env 用 `lib_ignore = hal, usb_transport` 把这两个目录排除掉。
- 新增一个模块 = 在 `modules/<id>/` 新建目录 + 在 `src/module_list.cpp` 里加一行。

`platformio.ini`（工具链 A 是基线；ADR-0001 选了 B 时，只改 `platform` 这一行）：

```ini
[common]
lib_deps_board =
    https://github.com/m5stack/M5Cardputer.git#1.2.0
    m5stack/M5Unified@0.2.26
    m5stack/M5GFX@0.2.33
lib_deps_core = bblanchon/ArduinoJson@7.4.3

[env:cardputer-adv]
platform = espressif32@6.10.0      ; B = https://github.com/pioarduino/platform-espressif32/releases/download/54.03.21-2/platform-espressif32.zip
board = esp32-s3-devkitc-1
framework = arduino
board_build.partitions = default_8MB.csv   ; 2×3.2MB app + 1.5MB 数据分区（LittleFS）
board_build.filesystem = littlefs
build_unflags = -std=gnu++11
build_flags = -std=gnu++17 -Wall -Wextra -DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1 -DCORE_DEBUG_LEVEL=0
lib_extra_dirs = modules
lib_deps = ${common.lib_deps_board} ${common.lib_deps_core}
extra_scripts = pre:scripts/pre_upload.py

[env:cardputer-adv-test]           ; HIL：带 debug.* 钩子
extends = env:cardputer-adv
build_flags = ${env:cardputer-adv.build_flags} -DCPA_TEST_HOOKS=1

[env:native]
platform = native
test_framework = unity
lib_extra_dirs = modules
lib_ignore = hal, usb_transport
lib_deps = ${common.lib_deps_core}
build_flags = -std=c++17 -Wall -Wextra -Werror --coverage -DCPA_PROTOCOL_DIR=\"${PROJECT_DIR}/../../protocol\"
```

---

## 4. 复用决策（带版本）

| # | 能力 | 决策 | 精确选择 | 理由与退路 |
|---|---|---|---|---|
| 1 | 固件工具链 | REUSE | **A（基线）**：`espressif32@6.10.0`，即 Arduino 2.0.17 / IDF 4.4.7 / GCC 8.4，hwtest 已经跑通。**B（候选）**：pioarduino `54.03.21-2`，即 Arduino 3.2.1 / IDF 5.4。**只有 G2 当天四项检查全部通过才用 B** | IDF 4.4 已 EOL；出厂固件和 CardMind 都证明 IDF 5.4 下麦克风能用。B 的 HWCDC 和 A 是同一套源码，**kick 仍然要保留**。永远不写不带版本的 `platform`：官方 v8.0.0 今天已经跳到 3.3.12（IDF 5.5）。C（Arduino 3.3.9 以上，HWCDC 已修）是 v0.3 的目标 |
| 2 | 构建和固件测试 | REUSE | PlatformIO 6.2.0（用 uv tool 安装）、Unity（PIO 自带）、gcovr | — |
| 3 | 板级库 | REUSE | M5Cardputer git tag `1.2.0`、M5Unified `0.2.26`、M5GFX `0.2.33` | 逐文件标注 SPDX MIT。键盘一旦出问题，就把 `TCA8418KeyboardReader` ADAPT 到本仓库（保留文件头）：每轮把 FIFO 读空，再加看门狗 |
| 4 | 显示 | REUSE + BUILD | M5GFX `M5Canvas` 全屏 8bpp（32,400B），加 5 个自写小部件；字体 `fonts::efontCN_12`（213,444B） | 不用 LVGL `9.6.0`：典型用法约需 100KB RAM，对纯文本列表不划算。Mooncake `2.3.3` 只借生命周期的命名 |
| 5 | JSON | REUSE | ArduinoJson `7.4.3` | v0.3 无线链路需要压缩时，用它自带的 MessagePack |
| 6 | 设备存储 | REUSE + BUILD | core 自带的 LittleFS（esp_littlefs）、Preferences（NVS）；outbox 自己写 | SD 卡（core `SD`/FatFS）放到 v0.2；SdFat `2.3.1` 不用 |
| 7 | daemon 运行时 | REUSE | Python 3.12（uv）、`pyserial==3.5`，其余只用标准库：asyncio、json、fcntl、socket、subprocess、plistlib、hashlib、os | Starlette、uvicorn、pydantic、SQLite、MCP 都推到 v0.2 再决定。不用 `pyserial-asyncio`（2021 年后停更），改为读线程 + `call_soon_threadsafe` |
| 8 | daemon 开发依赖 | REUSE | pytest 9.x、pytest-asyncio 1.x、pytest-cov、hypothesis、ruff、jsonschema 4.x（只在契约测试里用）、lizard | 精确版本锁在 `uv.lock` |
| 9 | Claude Code | REUSE CLI | 已在 2.1.272 上验证：`agents --json [--all]`、`--bg`、`-n/--name`、`--permission-mode`、`stop`、`--bg --resume` | hooks、mods（需要 2.1.287 以上）、channels、Agent SDK 都不进 v0.1 |
| 10 | LaunchAgent | ADAPT | 照 cc-buddy-bridge（MIT）的 `_service_launchd.py` 思路，用 `plistlib` 生成 plist | 保留出处 |
| 11 | 设计借鉴 | ADAPT | coding-toys（MIT）：cwd 映射到 git 根目录并缓存、丢掉不以 `{` 开头的行、HWCDC RX 缓冲的教训。m0rg0t（MIT）：outbox 一条一个文件。CardMind（MIT）：用固件预算 JSON 卡体积。cli-buddy（Apache-2.0）：v0.2 mod 适配器的起点 | 复制代码时保留原版权头，并写进 NOTICE |
| 12 | 以后的预选 | 以后再定 | v0.2：`mcp==2.3.*`、whisper.cpp `1.9.5`。v0.3：NimBLE-Arduino `2.5.1`，Noise 参考 ESPHome 的 noise-c 方案 | — |

**License 策略**：
- MIT、BSD、Zlib、Apache-2.0 的代码可以复制，NOTICE 里保留版权声明。
- 没有 License 的仓库和 GPL-3.0（Tactility 核心）只借设计，不复制代码。
- arduino-esp32 是 LGPL-2.1。我们源码完全公开、用户可以自己重新编译，NOTICE 里写清楚这一点。
- efont 字库的许可证随 M5GFX 一起写进 NOTICE。
- Agent SDK 的 TypeScript 版不是开源许可，v0.x 不依赖它。

**对已有文档的更正**（在 WP1 里修改）：
1. 03 §3 里"Arduino 3.x 重写过 HWCDC"不准确。修复从 3.3.9 才有（PR #12606），3.2.1 仍然需要 kick。
2. 02 §8 的麦克风问题范围更窄：出在扬声器到 M5Unified 麦克风的切换。
3. 02 §4 改成："设备不在时，审批不做决定，由终端照常询问"。

---

## 5. 固件设计

### 5.1 运行模型

v0.1 只用 Arduino 的 `loopTask`（core 1）一个线程。

```cpp
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void loop() {
  const uint32_t now = clock.nowMs();
  hal.update();                                     // M5Cardputer.update()
  usb.kick(now);                                    // 每 20ms 执行 (void)(bool)Serial
  for (uint8_t i = 0; i < 4 && usb.readLine(line); ++i)
    shell.apply(link_step(state().link, LinkInput::line(line, now)));
  shell.apply(chord_step(chords, KeySample{hal.keys(), now}));
  shell.apply(link_step(state().link, LinkInput::tick(now)));
  shell.apply(outbox_step(outboxState, OutboxInput::tick(now)));
  shell.renderIfDirty(canvas, now);                 // 最多 20fps
  power.tick(now);
}
```

时间预算：一圈不超过 20ms；任何一步都不能阻塞超过 50ms；LittleFS 单次写入不超过一个条目（不超过 2KB）。

### 5.2 纯核心、外壳和状态缓冲

```cpp
struct Effects { Effect items[4]; uint8_t count; };     // 定长，不在堆上分配
template <class S> struct Step { S state; Effects effects; };

Step<LinkCore>   link_step  (const LinkCore&,   const LinkInput&);
Step<OutboxCore> outbox_step(const OutboxCore&, const OutboxInput&);
Step<ChordState> chord_step (const ChordState&, const KeySample&);
ProjectsView reduce_projects(const ProjectsView&, const ProjectsSnapshot&);   // 切片按值返回，≤1.6KB
SessionsView reduce_sessions(const SessionsView&, const SessionsSnapshot&, uint32_t now);
```

**状态缓冲**：`static AppState g_buf[2]; uint8_t g_cur;`

外壳每次更新 `AppState` 分三步：
1. 把 `g_buf[g_cur]` 复制到 `g_buf[g_cur ^ 1]`；
2. 只替换发生变化的那个切片；
3. 切换 `g_cur`。

旧状态从来不被原地修改，最大的栈上临时对象就是一个切片。

**栈和内存的保障**：
- `static_assert(sizeof(AppState) <= 4096)`，并有一个 native 测试检查同一个条件；
- 系统页显示 `uxTaskGetStackHighWaterMark`。

### 5.3 AppState

```cpp
struct AppState {                 // ≤ 4096B
  LinkInfo     link;              // Detached/Attached/Ready；agent: ok|missing|error；caps.spawn/reply；last_rx_ms
  TimeInfo     time;              // known、offset_ms、tz_min
  ProjectsView projects;          // rev、rx_ms、9×{slot, key[13], name[25], run, need, queued, ok}、other{run, n}
  SessionsView sessions;          // rev、rx_ms、total、10×{sid[9], slot, name[33], kind, st, need, age_s, short[17]}
  QueueView    queue;             // rev、12×{qid[13], slot, intent, text[81], sid[9]}
  OutboxInfo   outbox;            // pending、inflight、bad、full
  AgentReqInfo agent;             // idle | inflight{rid, kind, sent_ms}
  DeviceInfo   device;            // boot、bat_mv、heap/min/max_block、stack_hwm、ghost_n、kb_reinit_n
  UiContext    ctx;               // 当前项目 slot（决定 Compose 的默认目标）
};
enum class EventKind : uint8_t { Key, LinkChanged, StateChanged, OutboxChanged, AgentResult, Tick };
```

项目列表（`slot`、`key`、`name`）变化时，写到 LittleFS 的 `/cfg/projects.json`，供离线时选目标、显示名字。session 和队列信息**不落盘**。

### 5.4 模块接口（最小）

```cpp
namespace cpa {
class Module {
 public:
  virtual ~Module() = default;
  virtual const char* id() const = 0;                          // ^[a-z][a-z0-9_]{1,15}$
  virtual bool begin(ModuleContext& ctx) = 0;                  // 返回 false 表示禁用，原因显示在 system 页
  virtual void onEnter(uint8_t arg) {}                         // 成为前台模块；arg 例如 slot
  virtual bool onKey(const KeyEvent& key) = 0;                 // 返回 true 表示已处理
  virtual void render(Canvas& c, const Rect& area) const = 0;  // 只读：state() + 自己的 UI 状态
};

class ModuleContext {                                          // 测试里用 FakeContext
 public:
  virtual const AppState& state() const = 0;
  virtual void compose(const ComposeSpec& spec) = 0;           // 共享的编辑器；提交后由 shell 决定走 outbox 还是在线请求
  virtual void confirm(const ConfirmSpec& spec) = 0;           // 打开后 400ms 内的按键忽略
  virtual bool request(const OnlineRequest& r) = 0;            // queue.done / inbox.dismiss / agent.*；不是 Ready 或已有请求在途时返回 false
  virtual void open(const char* moduleId, uint8_t arg) = 0;
  virtual void toast(ToastLevel lv, const char* utf8) = 0;
  virtual uint32_t nowMs() const = 0;
};
}  // namespace cpa
```

- 模块自己的界面状态（光标、标签页）也是值类型，用纯函数更新：`BoardUi next(const BoardUi&, const KeyEvent&, const AppState&)`，写在 `<module>_logic.cpp` 里。模块类本身只是一层薄外壳。
- `Canvas` 是一个抽象接口，只有这几个方法：`fill`、`text(x, y, utf8, Style)`、`textWidth`、`lineHeight`。
  - 设备上由 `hal/m5_canvas` 用 M5GFX 实现。
  - 测试里由 `FakeCanvas` 实现，它把画面记成文字行，方便断言。v0.1 不做 golden 文件。

> **v0.4.0 补充（只新增，都有默认实现，v0.3 的模块不用改；设计见 [13 §10.1](13-assistant-v2-plan.zh-CN.md)）**：
>
> ```cpp
> struct HelpRow {                                             // Tab 菜单"本页"的一行
>   KeyEvent ev;                                               // 在这一行按 Enter 时重放的键；kind 为 None 的行只显示
>   const char* keys = "";                                     // 行首写的键名（"空格"、"1-9"）
>   const char* label = "";                                    // 一句说明
> };
> class Module {
>   virtual const char* hint() const { return ""; }            // 提示栏（v0.1 起已有）；v0.4.0 起最右边写"Tab菜单"
>   virtual uint8_t contextSlot() const { return 0; }          // 正在看的项目，作 Compose 的默认目标（v0.1 起已有）
>   virtual uint8_t help(const HelpRow** rows) const;          // 本页的行；默认 0 行、*rows 为 nullptr
> };
> class ModuleContext {
>   virtual const SysInfo& sys() const = 0;                    // 系统页要的事实（v0.1 起已有）
>   virtual bool press(const KeyEvent& ev) { return false; }   // 替我按一个键，排到 §5.6 第 9 步
> };
> ```
>
> - **`help()`**：`*rows` 指向模块自己持有的存储（一般是静态数组），到模块下一次 `onEnter()` 或 `onKey()` 之前有效。一组键（如 `1-9`）的行把 `ev.kind` 设成 `None`，只显示、不执行。不用列 Tab；Opt+L 由外壳补上。
> - **`press()`**：把 `ev` 排到当前这个键处理完之后，像用户按下一样从 §5.6 第 3 步走一遍。只走一层：重放出来的键不能再排一个键。返回 false 表示没排上：默认实现、已经排了一个、或者正在重放。
> - **主页的格子和菜单的行都只通过重放按键来执行**，不另设"执行动作"的接口，所以菜单里能做的事，直接按键也一定能做，效果相同。
> - 菜单本身不是模块，是外壳的浮层：`MenuState` 放在 Shell 里，内容和画面来自纯函数库 `lib/help`。

### 5.5 注册

```cpp
// src/module_list.cpp —— 新增模块只改这里
static cpa::BoardModule  g_board;
static cpa::InboxModule  g_inbox;
static cpa::SystemModule g_system;
cpa::Module* const kModules[] = { &g_board, &g_inbox, &g_system };
```

为什么不用静态构造函数自注册：PlatformIO 默认会把库归档成静态库，链接器会把没有被引用的自注册对象丢掉，而且静态初始化的顺序也不确定。启动时只做一项检查：id 不能重复。manifest、能力位、消息归属这类校验都推到 v0.2 做 SDK 时再说。

v0.4.0 起列表里还有主页启动器 `home`（`modules/home/`）。注册了 `home`，它就是导航栈的根页面；没注册时根页面是 `board`，也就是 v0.3 的行为（现有测试的 Rig 不注册 `home`，所以 v0.1–v0.3 的导航测试照旧）。

### 5.6 Shell、导航与按键分发

**屏幕布局**：
- 顶部 12px 是状态栏；
- 中间 240×111 是内容区；
- 底部 12px 是按键提示栏。

**导航栈**最多 4 层。按返回键时，先问前台模块要不要处理；模块不处理才弹栈；栈空了就回到根页面：v0.1–v0.3 是 `board` 主页，v0.4.0 起注册了 `home` 就是主页启动器（§5.5）。Opt+H 清空栈，回到根页面。v0.4.0 起，模块自己调 `open()` 打开页面时总是新加一层，同一个模块也一样（项目看板的总览打开一个项目页），所以返回先回到总览、再回到根页面；全局键（Opt+P、Opt+1…9 等）打开时，栈顶是同一个模块就只换它的参数，和 v0.3 一样（`nav.h`）。

**按键分发顺序**（v0.4.0，[13 §5.4](13-assistant-v2-plan.zh-CN.md)）：

| 步 | 在哪 | 做什么 |
|---|---|---|
| 1 | Runtime，排在 `power_key` 之前 | **锁屏关卡**（`lib/shell/lock.h`）：锁住时只放行 Opt+L、3 秒窗口内的 Enter 和 G0（规则见 04 §8.8），其他键全部吞掉，也不点亮屏幕。放行的 G0 直接走录音这条路（卡片亮着时的便条、它的松开、开会时的 ★），不管锁屏下面是不是采集页，草稿不动 |
| 2 | Runtime | `power_key`：熄屏时第一个键只亮屏、被吞掉，G0 例外（§5.11）；会锁屏的 Opt+L 也例外，直接锁上，屏幕保持黑的 |
| 3 | Shell | **Tab 菜单**：打开时吃掉所有键。G0 例外：菜单关掉，G0 照常起作用（开会时加 ★）。按住 Backspace 关掉菜单时，这次按住的连删不再往下传 |
| 4 | Shell | **Tab 和 Opt+L**：查当前所在层的绑定。页面上是 `Layer::System`，浮层上是浮层自己的层，所以哪里有效全由 `kKeymap` 决定（见下表） |
| 5 | Shell | **录音浮层**（便条 HUD、会议确认框、会议页、结束确认框，v0.3）：录音状态机不是 Idle 时吃掉所有键 |
| 6 | Shell | **浮层**（Compose、Confirm）：浮层能处理的键它全部吃掉。在 Compose 里按 Opt+1…9 改的是目标，Opt+H 和 Opt+I 被忽略 |
| 7 | Shell | **全局组合键**：G0、Opt+H/P/I/1…9/N/Q/S/M/R |
| 8 | Shell | **前台模块**；模块不处理的返回键（`` ` ``、Backspace、Fn+`）弹栈 |
| 9 | Shell | **按键重放**：模块的 `press()` 或菜单里按 Enter 排了一个键时，等这个键处理完，再从第 3 步把它走一遍。只走一层，重放出来的键不能再排键 |

v0.3 是第 5–8 步，v0.1 和 v0.2 没有第 5 步；第 1–4 步和第 9 步是 v0.4.0 加的。

Tab 和 Opt+L 在各层是否有效（`kKeymap` 里就是这样绑的，13 §5.3）：

| 层 | Tab 菜单 | Opt+L 锁屏 |
|---|---|---|
| 主页和应用（`Layer::System`） | ✓ | ✓ |
| 采集页（`Layer::Compose`） | ✓，只有"本页"和"控制" | ✓，草稿留在内存里 |
| 确认框，含会议确认框和结束确认框（`Layer::Confirm`） | ✓，只有"本页"和"控制" | —，先处理完确认框 |
| 会议页（`Layer::RecPage`） | ✓，只有"本页"和"控制" | ✓，录音不停 |
| 便条 HUD（`Layer::RecMemo`） | — | — |
| 锁屏（`Layer::Lock`） | — | 解锁的第一步 |

采集页的"放弃已输入的内容？"按确认框处理（`Layer::Confirm`）：菜单的"本页"是这个提示自己的三行（Enter 放弃草稿，`` ` `` 和 Fn+` 返回编辑），Opt+L 无效。

菜单打开时，`debug.state` 的 `overlay` 仍报菜单下面的那一层；测试固件另外报可选字段 `ui{menu, locked}`（协议 v0 只新增，见 [protocol/README.md](../protocol/README.zh-CN.md)）。

**Compose（采集页）**
- 参数是 `ComposeSpec{kind: note|queue|spawn|feedback, project, sid}`。
- 只能输入 ASCII，单行，长度 1 到 500B。`spawn` 和 `feedback` 类型的文本不能以 `-` 开头。
- 草稿只存在内存里。

提交时 shell 按下面的规则决定怎么发：

| kind | 去向 |
|---|---|
| `note`、`queue` | 写进 outbox |
| `spawn`、`feedback` | 先打开 Confirm |

Confirm 按下面的规则决定执行还是排队：

| 条件 | Confirm 显示 | 按 Enter 后 |
|---|---|---|
| `spawn`：Ready 并且 `agent=ok` | 开始 | 发送 `agent.spawn` |
| `feedback`：`kind=bg`、`st=idle`、Ready、`caps.reply` 四项都满足 | 直接发送 | 发送 `agent.reply` |
| 其他情况 | 存为排队，并写明原因 | 写一条 outbox 条目，`kind=queue`，`intent` 为 `spawn` 或 `feedback` |

**在线请求的结果**：`agent.result` 由 shell 处理，对应的提示由它弹出。

| 结果 | shell 的动作 |
|---|---|
| `ok` | 提示"已开始 · <short>" |
| 失败 | 文本自动存成排队项，再提示错误 |
| 链路断开或 30s 超时还没有结果 | 提示"结果未知"，文本存成排队项并标 `uncertain` |

在线请求一律不重试。

**Toast**：每条显示 2.5s，最多排队 3 条。比较前后两份 sessions 快照，如果有 session 的 `need` 新变成 true，就弹出"<key>/<name> 等你"。

### 5.7 输入层

```
hal.keys() → KeySet（56 个矩阵位 + G0 位，值类型）
          → diff(prev, now)          // 比较完整的按键集合；M5 的 isChange() 只比较按下的键数（✅ 03 §6）
          → GhostFilter              // 这一帧出现完整矩形 → 整帧丢弃，ghost_n++
          → ChordDetector（纯）      // → KeyEvent
```

`KeyEvent` 有这几种：`Char{c}`、`Chord{mods, key}`、`G0Tap`、`G0Long`、`G0LongUp`（v0.3：按住 G0 之后的松开）、`Nav{Up, Down, Left, Right, Enter, Back, Bksp, Menu}`。`Nav::Menu` 是 Tab（v0.4.0，带不带 Shift 都一样）；v0.3.1 及以前按 Tab 不产生任何事件。

规则：
1. **按下时触发。** 动作以按下那一刻完整的按键集合为准，松开不触发任何动作（G0 除外）。
2. **修饰键与字符。** 修饰键有 Fn、Shift、Ctrl、Opt、Alt。只要按住 Ctrl、Opt 或 Alt 中的任何一个，就不产生 `Char`。
3. **幽灵键检测。** 矩阵坐标沿用 hwtest 的换算：`row = x/2`，`col = y + 4*(x%2)`。如果按下的集合里同时有 `(r1,c1)`、`(r1,c2)`、`(r2,c1)`、`(r2,c2)` 四个角，就分不清哪个是幽灵键，整帧丢弃并计数。
4. **keymap 静态检查。** `test_keymap_static` 对 keymap 里的每一个组合跑一遍幽灵检测，任何可能产生幽灵键的组合都通不过测试。
5. **G0 的短按和长按。** 按下后不到 500ms 就松开，算 `G0Tap`；按住满 500ms 时立刻发出 `G0Long`，不等松开。两者之间没有空档。
6. **自动重复。** 只有 Backspace 支持：按住 500ms 后开始，之后每 60ms 重复一次。
7. **键盘看门狗。** 有键处于按下状态，但集合连续 10s 没有变化，就调用 `Keyboard.begin()` 重新初始化，并让 `kb_reinit_n` 加 1。G1 复现锁死后再细化条件。如果 G1 失败，就 ADAPT TCA8418 驱动：每轮把 FIFO 读空，并把 INT（G11）的状态加入判断。
8. **Leader 键** v0.1 不做，v0.4.0 也不做。**Tab 菜单不是 Leader 键**：菜单打开后按字母，是在屏幕上看得见的选项里做选择（"去哪"和"控制"页上，这一页有那一行时；"本页"上，不在浮层上、并且全局层或系统层绑了 Opt+那个字母时），不是隐藏的按键前缀；菜单里每一行执行的都是重放一个普通按键（§5.4 的 v0.4.0 补充）。

### 5.8 USB 传输与链路

**UsbTransport**（`usb_transport/`，只在设备上编译）：
- 初始化顺序：先 `Serial.setRxBufferSize(8192)`，再 `setTxBufferSize(4096)`、`setTxTimeoutMs(0)`，最后 `begin()`。这几个 API 在 2.0.17 里都有（✅）。
- 每 20ms 执行一次 `(void)static_cast<bool>(Serial)`，也就是 kick。
- 写数据前先检查 `availableForWrite() >= len`，空间不够就放进定长发送队列（最多 4 行）。
- 发送队列满了，只丢可以重发的 `outbox.item`；回复类的消息从不丢弃。

**LineFramer**（纯函数）：
- 按 `\n` 切行，忽略 `\r`。
- 一行超过 2048B 时，一直丢到下一个 `\n`，然后报 `E_TOO_LARGE`。
- 不以 `{` 开头的行当作噪声丢掉，并计数。

**Codec**：
- 用一个复用的 `JsonDocument` 解析。
- 按 `limits.h` 校验 `v`、`t`、`id` 以及每个字段。`limits.h` 是 `protocol/limits.json` 的镜像，两者由测试比对。
- 不合法的 UTF-8 一律拒绝（`E_BAD_FIELD`）。

**LinkSession 状态机**（纯函数）：

| 状态 | 什么时候进入 | 允许做什么 |
|---|---|---|
| `Detached` | `isPlugged()` **持续 500ms** 为假（`lib/link/plug_filter`，HIL 实测：HWCDC 的 SOF 检测偶尔会有约 1ms 的误报，不去抖会让链路从 Ready 掉线；只接充电器时也是假） | 什么都不发 |
| `Attached` | 插着主机，但还没收到合法的主机 `hello`，或者超过 15s 没收到数据 | 只回复 `hello` |
| `Ready` | 收到 `v=0` 的主机 `hello`，并且已经回复 | 全部 |

几条规则：
- 设备从不主动发起握手。
- 每收到一条主机主动发来的消息（不带 `re`），设备都恰好回复一条：`hello`、`pong`、`ack` 或 `error`。
- 处于 Ready 时又收到主机的 `hello`，说明 daemon 重启了。这时重新握手，outbox 里在途的条目退回待发；在途的 agent 请求按"结果未知"处理。

### 5.9 Outbox（只用 LittleFS）

**文件布局**：
```
/outbox/<MonoId>.json        待发（内容就是要发送的那一行 outbox.item 原文）
/outbox/<MonoId>.tmp         正在写入
/outbox/bad/<MonoId>.json    被 daemon 判为 rejected，或者启动时解析失败（最多保留 20 条，超出时删最旧的）
/cfg/projects.json           slot→key/name 缓存
```

**写入**：`open(tmp, "w")` → `write` → `close()`（LittleFS 在 close 时提交）→ `rename(tmp, json)` → 成功后才显示"已保存"。LittleFS 的 rename 是原子的（📚 littlefs 的设计；断电行为由 ⏳ G10 实测）。

**启动扫描**：
- 只有 `.tmp`、没有同名 `.json`：说明从来没提交成功，用户也没见过"已保存"，直接删除。
- `.tmp` 旁边已有同名 `.json`：**不删 `.tmp`**，只计数，并在系统页报告"不变式被打破"。按 LittleFS 的设计这不应该发生；SD 卡上（v0.2）FatFS 的 rename 不是原子的，这条规则正是为那种情况准备的。
- `.json` 文件解析失败，或者解析出的 `id` 和文件名对不上：移到 `bad/`。

**MonoId**：Crockford base32 编码，格式是 `EEEEEEE-BBBBB-SSSS`，共 18 个字符。

| 段 | 位数 | 含义 |
|---|---|---|
| `E` | 32 | epoch：第一次初始化 NVS 时生成，用 `esp_random()` 混入 MAC 地址。Flash 整片擦除后 epoch 会变，所以不会和已经确认过的旧 ID 撞车 |
| `B` | 25 | NVS 里的开机计数。每次开机**先把计数加 1 并写回 NVS，然后才发出任何 ID** |
| `S` | 20 | 本次开机内的序号 |

同一个 epoch 内，ID 按字典序排就是按时间排。daemon 用 `(dev, id)` 去重。boot id 就是 `EEEEEEE-BBBBB`。

**发送与确认**（`OutboxCore` 是纯函数）：
- 只在 Ready 时发送，按 ID 顺序，同时最多 4 条在途。
- 3s 没收到 `outbox.ack` 就原样重发，之后的间隔依次是 6s、12s，再往后都是 30s。链路断开时，在途的条目退回待发，不算一次重试。
- 按 `outbox.ack` 的 `status` 处理：
  - `stored` 或 `duplicate`：删除文件；
  - `rejected`：移到 `bad/`，并弹出提示。
- 收到 ID 不认识的 ack，直接忽略。

**容量**：最多 200 条待发，并且剩余空间至少 64KB。达到任何一条，Compose 就拒绝提交。

**投递语义**：设备至少投递一次，daemon 按 ID 幂等去重，所以效果上是恰好一次。ack 的含义是"daemon 已经把这条 fsync 进 `events.jsonl`"，**不代表**笔记已经写成。

### 5.10 时间
- `nowMs()` 就是 `millis()`。
- 墙钟 = `up_ms + offset`，`offset` 由 `time.set{epoch_ms, tz_min}` 设置。保留 `tz_min` 是为了让状态栏显示本地时间。
- 软复位后墙钟还在，断电后就丢失；墙钟未知时显示 `--:--`。

### 5.11 电源（v0.1 极简）
- `isPlugged()` 为真时，屏幕常亮。
- 用电池时，60 秒没有按键就关背光。之后：
  - 按任意键只点亮屏幕，这次按键不触发任何动作；
  - G0 例外：点亮屏幕的同时直接打开速记。
- 不做任何睡眠。

> **v0.4.0 补充**（`lib/shell/power.h`，[13 §7.1–§7.2](13-assistant-v2-plan.zh-CN.md)）：
> - **锁屏优先于"插着电就常亮"**：锁住时屏幕熄灭，插着 Mac 也一样，只有锁屏卡片亮着的 3 秒例外。锁屏关卡排在 `power_key` 之前（§5.6 第 1 步），被它吞掉的键不点亮屏幕。
> - **熄屏时按 Opt+L 直接锁上**，不是只亮屏；屏幕保持黑的，所以不会"醒来就执行"什么。
> - **录音时 ● 永远看得见**：录音状态是 Cue、Recording、EndConfirm 或 Finishing 时，背光最暗只到"调暗"，并且画出 ● 和计时，不管是否锁屏、菜单是否打开、是否在弹提示。

### 5.12 显示与中文
- 用全屏 `M5Canvas`，8bpp 色深（32,400B），状态变脏才 `pushSprite`。
- 字体用 `efontCN_12`：ASCII 每行 40 个字符，中文每行 20 个字。
- daemon 按 UTF-8 字符边界截断文本，设备只校验字节上限。
- 遇到缺字时，HAL 先尝试用 M5GFX 的字形查询来检测，检测到就画 `□`；检测不了，就记下 M5GFX 默认怎么处理。HIL 会用本机真实的中文 session 名测一遍。

### 5.13 预算（CI 和系统页都会检查）

| 项 | 预算 |
|---|---|
| 静态 RAM（.data + .bss） | ≤96KB（hwtest 是 89KB，其中 64KB 是麦克风缓冲，v0.1 不需要） |
| 空闲堆 / 最大连续块 | ≥150KB / ≥64KB（含 32KB 的 sprite） |
| loop 栈 | 16KB；栈剩余量的最低值要 ≥4KB |
| `AppState` | ≤4KB（`static_assert`） |
| 单条消息解析的峰值 | ≤8KB 堆（2KB 行缓冲加 `JsonDocument`） |
| 应用固件 | ≤2.5MB（每个 app 分区 3.2MB） |

`scripts/check_budget.py` 解析 `firmware.map` 和 `firmware.bin` 的大小，超出预算就让构建失败。

---

## 6. 协议规范 v0（唯一真相源）

### 6.1 分帧
- 一行一个 JSON 对象，UTF-8 编码，以 `\n` 结尾。发送方不发 `\r`，接收方忽略 `\r`。
- **单行 ≤2048 字节**（含 `\n`），两个方向都是这个上限。
- 不以 `{` 开头的行是噪声，比如启动日志。直接丢弃，不回错误，只计数。
- daemon 序列化时用 `json.dumps(obj, ensure_ascii=False, separators=(",", ":"))`。中文按 UTF-8 原样输出，不转成 `\uXXXX`。序列化后如果超过 2048B，就从列表尾部减条目。

### 6.2 信封

```json
{"v":0,"t":"<type>","id":"<msg-id>","re":"<id>"}
```

| 字段 | 必填 | 规则 |
|---|---|---|
| `v` | 是 | 主版本号，v0 期间固定为 `0`。不一致就回 `error{code:E_VERSION}` |
| `t` | 是 | 消息类型，格式 `^[a-z][a-z0-9_.]{1,31}$` |
| `id` | 是 | 发送方生成，在一次连接内唯一，≤24 个 ASCII 字符 `[A-Za-z0-9-]`。设备的普通消息用 `d<n>`，主机用 `h<n>`，`outbox.item` 用 MonoId |
| `re` | 回复时必填 | 被回复那条消息的 `id` |
| 其他 | | 负载字段，直接放在顶层 |

处理规则：
- 不认识的字段一律忽略。
- 不认识的 `t`：如果这条消息不是回复，就回 `E_UNKNOWN_TYPE`。
- 一行解析不出来（`E_PARSE`），因为拿不到 `id`，不回复，只计数。
- 可选字段可以省略，省略就表示"没有"。daemon 收到 `null` 也接受。

### 6.3 交互规则与流控（credit）
1. **设备从不主动握手。** 主机发 `hello`，2s 没回应就重发，最多 3 次。
2. **主机主动发的消息要求恰好一条回复。** 这类消息是不带 `re` 的：`hello`、`ping`、`time.set`、`*.snapshot`，以及测试版里的 `debug.*`。设备必须对每一条回复一次，可以是 `hello`、`pong`、`ack` 或 `error`。
3. **主机的 credit 窗口。** 主机主动发出、还没收到回复的字节总数不能超过 **4096B**（设备接收缓冲 8192B 的一半）。5s 还没收到回复，就把这部分 credit 收回来；快照如果因此丢了，靠 `pong` 里的 `revs` 对账自动补发。
4. **设备主动发的请求，各有在途上限。**
   - `outbox.item`：最多 4 条在途，回复是 `outbox.ack`。
   - `agent.spawn` / `agent.reply`：最多 1 条在途，回复是 `agent.result`。
   - `queue.done` / `inbox.dismiss`，以及 v0.4.1 的 `rec.list` / `rec.act`：共用最多 1 条在途，回复是 `ack`、`rec.items`（只回 `rec.list`）或 `error`。

   主机对这些请求的回复每条都不超过 256B（`host_reply_max`），**唯一例外是 `rec.items`**，它只受单行 2048B（`line_max`）限制（`protocol/README.md` 第 20 条，`rec_rules.LARGE_HOST_REPLIES`）。因为它占的是那 1 个在途名额，最坏情况下设备接收缓冲里的数据是 4096 + 5×256 + 2048 = 7424B，小于 8192（v0.4.1 之前是 4096 + 6×256）。
5. **设备每圈最多处理 4 行入站数据**；每秒最多回复 5 条 `error`。
6. **状态推送靠 rev 对账。** 三种快照各自带一个 `rev`。设备在 `pong` 里报告自己手上的 `revs`，比主机落后的那份，主机重新推送一次。

### 6.4 握手、心跳与重启检测

```
daemon 安全打开端口（序列号绑定之后，只打开那一个）
H→D hello                     （2s 没回应就重发，最多 3 次）
D→H hello(re)                 fw 必须以 cpa-fw/ 开头，否则 6s 内释放端口，30s 后再试
H→D time.set        D→H ack
H→D projects.snapshot / sessions.snapshot / queue.snapshot   D→H ack × 3
D→H outbox.item × ≤4 …        H→D outbox.ack …
每 5s：H→D ping     D→H pong(up_ms, pending, revs)
```

- **判断设备重启**：`boot` 变了，或者 `boot` 没变但 `up_ms` 变小了。这时 daemon 重新下发 `time.set` 和三份快照。
- **判断掉线**：任何一方 15s 没收到有效数据。
- **第一次握手成功后**，daemon 把这台设备的 USB 序列号写进 `config.json` 的 `device.serial`。

### 6.5 消息目录（v0.1）

| `t` | 方向 | 模式 | 关键字段 |
|---|---|---|---|
| `hello` | H→D | 主机发起 | `role:"host"`、`app`、`agent`（`ok`/`missing`/`error`）、`caps{spawn, reply}` |
| `hello` | D→H | 回复 | `role:"device"`、`fw`、`dev`、`boot`、`up_ms`、`rst`、`time`（bool）、`pending`、`bat_mv`、`revs{projects, sessions, queue}` |
| `ping` / `pong` | H→D / D→H | 主机发起 / 回复 | `pong`：`up_ms`、`pending`、`revs` |
| `ack` | 双向 | 回复 | 只有 `re` |
| `error` | 双向 | 回复 | `re`、`code`、`field?`、`msg?`（简短英文） |
| `time.set` | H→D | 主机发起 | `epoch_ms`、`tz_min` |
| `projects.snapshot` | H→D | 主机发起（状态推送） | `rev`、`projects[≤9]{slot, key, name, run, need, queued, ok}`、`other{run, n}` |
| `sessions.snapshot` | H→D | 主机发起（状态推送） | `rev`、`total`、`sessions[≤10]{sid, slot, name, kind, st, need, age_s, short?}` |
| `queue.snapshot` | H→D | 主机发起（状态推送） | `rev`、`items[≤12]{qid, slot, text, intent, sid?, uncertain?}`，每个项目最多 4 条；`uncertain`（v0.4.1，docs/20 U25）只在为 true 时出现，见 protocol/README 第 22 条 |
| `outbox.item` | D→H | 可靠投递 | `id`（MonoId）、`kind`（`note`/`queue`）、`at{up_ms, wall_ms}`、`body` |
| └ `note` | | | `body{text, project?}` |
| └ `queue` | | | `body{text, project, intent(todo/spawn/feedback), sid?, uncertain?}` |
| `outbox.ack` | H→D | 回复 | `re`、`status`（`stored`/`duplicate`/`rejected`）、`code?` |
| `agent.spawn` | D→H | 在线请求 | `project`、`text`、`qid?`（带 `qid` 时以 daemon 保存的全文为准） |
| `agent.reply` | D→H | 在线请求（只在 `caps.reply` 时） | `sid`、`text` |
| `agent.result` | H→D | 回复 | `re`、`ok`、`code?`、`name?`、`short?` |
| `queue.done` | D→H | 在线请求 | `qid` |
| `inbox.dismiss` | D→H | 在线请求 | `sid` |
| `debug.key` / `debug.state` / `debug.reboot` | H→D | **只在测试版固件里有** | 注入按键 / 返回 `AppState` 摘要和时间戳 / 软重启 |

**字段取值约定**：

| 字段 | 取值 |
|---|---|
| `kind` | `tty` 对应 interactive，`bg` 对应 background。`daemon` 和 `daemon-worker` 不下发 |
| `st` | `busy`、`shell`、`idle`、`waiting`、`unknown` |
| `need` | 由 daemon 计算，规则见 §7.6 |
| `sid` | session UUID 的前 8 个十六进制字符 |
| `short` | 只有后台 session 有，取自 `agents --json` |
| `age_s` | 发送时这个 session 进入当前状态已经多少秒；设备在显示时自己往上加，所以单纯时长变化不会让 `rev` 加 1 |

**outbox 里只有 `note` 和 `queue` 两种 kind，这是一条硬性规定。** 执行类请求（`agent.*`）不能出现在 outbox 里，契约测试会断言：任何带执行语义的 kind 都是非法的。

### 6.6 示例（来自 `protocol/vectors`）

```jsonl
{"v":0,"t":"hello","id":"h1","role":"host","app":"cpad/0.1.0","agent":"ok","caps":{"spawn":true,"reply":false}}
{"v":0,"t":"hello","id":"d1","re":"h1","role":"device","fw":"cpa-fw/0.1.0","dev":"020000000001","boot":"3QZK1M7-0000C","up_ms":53211,"rst":"poweron","time":false,"pending":3,"bat_mv":4150,"revs":{"projects":0,"sessions":0,"queue":0}}
{"v":0,"t":"time.set","id":"h2","epoch_ms":1792440000000,"tz_min":-420}
{"v":0,"t":"ack","id":"d2","re":"h2"}
{"v":0,"t":"projects.snapshot","id":"h3","rev":42,"projects":[{"slot":1,"key":"cpa","name":"Cardputer 助手","run":1,"need":1,"queued":2,"ok":true},{"slot":2,"key":"blog","name":"博客","run":0,"need":0,"queued":0,"ok":true}],"other":{"run":0,"n":1}}
{"v":0,"t":"ack","id":"d3","re":"h3"}
{"v":0,"t":"sessions.snapshot","id":"h4","rev":17,"total":2,"sessions":[{"sid":"8d3e7a19","slot":1,"name":"架构设计","kind":"tty","st":"waiting","need":true,"age_s":540},{"sid":"5c2e8a14","slot":1,"name":"cpa/chord-tests","kind":"bg","st":"busy","need":false,"age_s":75,"short":"a1b2c3"}]}
{"v":0,"t":"queue.snapshot","id":"h5","rev":9,"items":[{"qid":"q17","slot":1,"text":"add LittleFS fault-injection test","intent":"todo"}]}
{"v":0,"t":"outbox.item","id":"3QZK1M7-0000C-0003","kind":"note","at":{"up_ms":41000,"wall_ms":null},"body":{"text":"idea: dedupe by sid","project":"cpa"}}
{"v":0,"t":"outbox.ack","id":"h6","re":"3QZK1M7-0000C-0003","status":"stored"}
{"v":0,"t":"outbox.item","id":"3QZK1M7-0000C-0004","kind":"queue","at":{"up_ms":98000,"wall_ms":1792440045000},"body":{"text":"write native tests for chord detector","project":"cpa","intent":"spawn"}}
{"v":0,"t":"agent.spawn","id":"d7","project":"cpa","text":"Write native tests for the chord detector"}
{"v":0,"t":"agent.result","id":"h7","re":"d7","ok":true,"name":"cpa/write-native-tests","short":"a1b2c3"}
{"v":0,"t":"agent.reply","id":"d8","sid":"5c2e8a14","text":"looks good, add a test plan"}
{"v":0,"t":"agent.result","id":"h8","re":"d8","ok":false,"code":"E_NOT_REPLYABLE"}
{"v":0,"t":"queue.done","id":"d9","qid":"q17"}
{"v":0,"t":"ack","id":"h9","re":"d9"}
{"v":0,"t":"inbox.dismiss","id":"d10","sid":"8d3e7a19"}
{"v":0,"t":"ack","id":"h10","re":"d10"}
{"v":0,"t":"ping","id":"h11"}
{"v":0,"t":"pong","id":"d11","re":"h11","up_ms":120400,"pending":0,"revs":{"projects":42,"sessions":17,"queue":9}}
{"v":0,"t":"error","id":"d12","re":"h12","code":"E_BAD_FIELD","field":"sessions[3].name"}
```

### 6.7 限制表（`protocol/limits.json` 是机器可读版本）

| 项 | 上限或格式 |
|---|---|
| 单行 | ≤2048B（含 `\n`） |
| `id` / `re` | ≤24 个 ASCII 字符，只能是 `[A-Za-z0-9-]` |
| 项目 `key` | `^[a-z0-9][a-z0-9-]{0,11}$` |
| `slot` | 1 到 9，不能重复 |
| 项目 `name` / session `name` / 队列摘要 `text` | ≤24 / 32 / 80B UTF-8，daemon 按字符边界截断 |
| 用户正文（note、queue、spawn、reply） | 1 到 500B，单行。设备只能输入 0x20 到 0x7E 的字符；daemon 把控制字符替换成空格。spawn 和 reply 的正文去掉首尾空白后，不能以 `-` 开头 |
| `sid` / `short` / `qid` | `^[0-9a-f]{8}$` / ≤16 个 ASCII 字符 / `^q[0-9]{1,10}$` |
| `epoch_ms` / `tz_min` | ≥1735689600000 / −840 到 840 |
| 快照条数 | 项目 ≤9；session ≤10；队列 ≤12，其中每个项目 ≤4 |
| 主机未回复的数据 | ≤4096B |
| 设备在途请求 | outbox ≤4；agent ≤1；queue/inbox/rec（v0.4.1）共用 ≤1 |

### 6.8 错误码

| code | 含义 |
|---|---|
| `E_PARSE` | 不是合法的 JSON（不回复，只计数） |
| `E_TOO_LARGE` | 一行超过 2048B |
| `E_VERSION` | `v` 不兼容 |
| `E_UNKNOWN_TYPE` | 不认识这个 `t` |
| `E_BAD_FIELD` | 字段缺失、类型不对、超长、不是合法 UTF-8，或者以 `-` 开头；`field` 给出 JSON 路径 |
| `E_NOT_READY` | 还没握手就发了业务消息 |
| `E_STORAGE` | 设备存储失败 |
| `E_UNKNOWN_PROJECT` / `E_UNKNOWN_SESSION` / `E_UNKNOWN_ITEM` | 项目、session 或队列项不存在，或者无法唯一确定 |
| `E_NO_AGENT` | Mac 上找不到 Claude Code |
| `E_SPAWN` | 开 session 失败或超时 |
| `E_NOT_REPLYABLE` | 目标不是空闲的后台 session，或者回复功能没打开 |
| `E_REPLY_FORKED` | resume 复制出了新 session，回复功能在本次运行期间关闭 |
| `E_BUSY` | 已经有一个 agent 请求在途 |
| `E_INTERNAL` | 其他错误 |

`msg` 只写一句简短的英文原因。**不回显正文、路径、命令行或命令输出。**

> **v0.2 补充（只新增，`v` 仍为 0）**：设备的 `hello` 可以带两个可选字段：`store`（`sd` / `int` / `none`，表示 outbox 存在哪里）和 `launcher`（boolean，表示是不是经由 Launcher 启动）。不设置时编码结果和 v0.1 逐字节相同；v0.1 的 daemon 会忽略它们。另外，主机发出的每一行必须以 `{` 开头或者是空行，因为 Launcher 的串口控制台共用这个端口，以单词开头的行可能被它当成命令执行。详见 docs/09 §6 和 protocol/README。

### 6.9 版本演进
- 只**新增**字段或消息类型时，`v` 不变，旧的一端会忽略看不懂的内容。改变语义或删除字段时，`v` 加 1。
- 能力通过主机 `hello` 里的 `caps` 和 `agent` 来协商。
- 任何新消息类型都要先写 schema 和测试向量（RED），再实现两端。

### 6.10 预留类型名（v0.2 以后才实现，名字已经占住）
- `voice.begin`、`voice.chunk`、`voice.end`：音频分块传输，用 base64，带 sha256，可以续传。
- `approval.request`、`approval.decision`：审批。
- `log`：只在测试版里用。
- `pair.*`：v0.3 的无线配对。

---

## 7. Mac daemon 设计

### 7.1 进程模型与目录

**进程模型**
- 单进程，一个 asyncio 事件循环。`HubState` 只有一个写入者：`reduce()` 返回 `(HubState, Effects)`，再由 `EffectRunner` 去执行。不需要锁。
- 串口读取放在独立的读线程里，读到一行就通过 `call_soon_threadsafe` 交给事件循环。
- 子进程用 `asyncio.create_subprocess_exec` 启动，不经过 shell，带超时。
- 单实例：用 `fcntl.flock` 锁住 `$CPA_HOME/cpad.lock`；打开串口时加 `exclusive=True`。这个独占锁只是建议性的，挡不住别的程序，所以刷机前一定要先执行 `cpa pause`。

**目录** `$CPA_HOME`，默认 `~/Library/Application Support/CardputerAssistant`，目录权限 0700，文件权限 0600，启动时检查：

```
config.json      claude_bin（绝对路径）、notes_dir、device.serial、poll_s=3、inbox_idle_s=60、spawn_env（登录 shell 环境白名单）、experimental_bg_reply=false
projects.json    {"version":1,"projects":[{"key","name","slot","root","spawn_permission_mode"}]}
events.jsonl     只追加，每次写入都 fsync，是唯一的权威数据
cpad.sock        AF_UNIX 控制通道，0600
cpad.lock
logs/cpad.log    按 1MB 轮转，保留 5 份，已脱敏
```

### 7.2 设备管理（`device/`）
1. **PortFinder**：每秒查找一次 `303A:1001` 的端口。如果配置里已经绑定了序列号，只认那一个；没有绑定时，遇到不认识的端口要等 30s 退避期过了才去试。
2. **SafeOpen**：先以 `dsrdtr=True` 打开，再设 `rts=False`，最后设 `dtr=False`。单元测试用一个假的 Serial 记录控制线的变化序列，断言 (RTS, DTR) 从来没有出现过 (1, 0)。
3. **Handshake**：发 `hello`，等 2s，最多试 3 次；校验 `fw` 的前缀和 `v`；不符合就在 6s 内释放端口。
4. **DeviceLink**：
   - 写队列按 credit 窗口限制在 4096B 以内；
   - 回复按 `re` 匹配；
   - 每 5s 发一次 `ping`，15s 收不到数据就判定掉线；
   - 发现 `boot` 变了，就重新同步。
5. **暂停**：`cpa pause [--seconds 120]` 通过 unix socket 通知 daemon 关闭端口、暂停重连；到时间后自动恢复。

### 7.3 状态、事件与幂等

```python
@dataclass(frozen=True, slots=True)
class HubState:
    link: LinkState                      # 连接状态、boot、revs_sent、设备上报的 revs
    projects: tuple[Project, ...]
    sessions: tuple[Session, ...]        # 最近一次轮询的结果，带 since
    dismissed: frozenset[tuple[str, str, int]]   # (sid, st, since)：状态一变，这条忽略记录就自动失效
    queue: tuple[QueueItem, ...]         # 由 events 折叠得到
    revs: Revs

def reduce(state: HubState, ev: HubEvent) -> tuple[HubState, tuple[Effect, ...]]: ...
```

**事件**：每条事件写成 `events.jsonl` 里的一行 JSON，至少包含 `ev`、`at`（daemon 的 ISO 时间）、`dev?`，写入后不再修改。

| 事件 | 含义 |
|---|---|
| `item.received` | 收到设备的 outbox 条目。字段：`id, kind, body, at_dev, received_at, orphan?` |
| `queue.added` | 新增排队项。字段：`qid, project, text, intent, sid?, origin(device/cli), from_item?` |
| `queue.done` | 排队项被标记为完成 |
| `queue.spawned` | 排队项被用来开了 session |
| `queue.reopened` | 关掉的排队项（`queue.done` 或 `queue.spawned`）重新打开，原样回到队列，qid 不变。字段和 `queue.added` 一样：`qid, project, text, intent, sid?, origin, uncertain`，由 `cpa queue reopen` 扫一遍日志找到原项填上；折叠因此不用记住关掉的项，耗时随日志线性增长（v0.4.1，docs/20 U12） |
| `agent.spawned` / `agent.spawn_failed` | 开 session 成功 / 失败。只记项目、名字、short、错误码，**不记正文**。`agent.spawn_failed` 在 Mac 知道原因时另带 `why`：`untrusted`（CLI 还没信任这个目录）或 `login`（CLI 没登录）（v0.4，#25） |
| `agent.replied` / `agent.reply_failed` | 回复成功 / 失败 |
| `device.seen` | 设备连接。字段：`dev, serial, fw` |

**处理 `outbox.item` 的顺序**：
1. 校验，不合法就回 `rejected`。
2. 查重。重复就回 `duplicate`，不再追加事件。
3. 追加 `item.received`；如果是 `kind=queue`，再追加 `queue.added`。
4. fsync。
5. 回 `stored`。
6. 产生副作用：渲染笔记，更新队列快照。

**去重索引**（`(dev, id)` 的集合）放在 `events/` 的外壳里，启动时从日志重建，纯核心只接收"是否重复"这个结果。

**项目已被删除的情况**：条目里的项目已经不存在时，仍然保存，标记为 `orphan`。笔记记成"未归属"，并保留原来的 key。**不丢用户的内容。**

### 7.4 Agent 适配器接口（`agents/base.py`）

```python
@dataclass(frozen=True, slots=True)
class AgentCaps: present: bool; version: str; spawn: bool; reply: bool

class AgentAdapter(Protocol):
    name: str
    async def probe(self) -> AgentCaps: ...                                   # 启动时一次，之后每 60s 一次
    async def list_sessions(self) -> tuple[SessionInfo, ...]: ...
    async def spawn(self, req: SpawnRequest) -> SpawnResult: ...              # root, name, text, permission_mode
    async def reply(self, req: ReplyRequest) -> ReplyResult: ...              # session, text, permission_mode
```

以后加 Codex 只需要实现这四个方法。v0.1 不做适配器注册表，也不做按项目选择适配器。

### 7.5 Claude Code 适配器（`agents/claude_code/`）

| 能力 | 实现 | 状态 |
|---|---|---|
| probe | `claude --version` 加上 `which`。`claude_bin` 用绝对路径，因为 launchd 的 PATH 很短 | ✅ 2.1.272 |
| list | 只在设备处于 Ready 时，每 3s 执行一次 `claude agents --json`，超时 10s。解析时宽松处理：不认识的 `status` 记为 `unknown`，`kind` 为 `daemon` 或 `daemon-worker` 的跳过。interactive session 没有 `id`、`state`、`waitingFor` 字段 | ✅ 字段；⏳ G4 语义 |
| spawn | `argv = [claude_bin, "--bg", "--name", f"{key}/{slug}", "--permission-mode", mode, "--", text]`，`cwd=root`，`env=spawn_env`，超时 20s。从 stdout 里解析短 id；解析不到，就对比执行前后的 `agents --json`，按名字匹配 | ✅ 参数存在；⏳ G7、G8、G9 |
| 注 | CLI 的 `agents --json` 里，后台 session 的 kind 实际是 `background`（不是 `bg`）；daemon 把它映射成协议里的 `bg`。还要看 `state` 字段：`blocked` 当作等你，开 session 后出现 `failed`/`crashed` 当作失败。`--bg` 即使 exit 0 也不代表成功，要用 `^backgrounded · ([0-9a-f]{8})` 解析短 id，再用 `agents --json --all` 核对（G7/G8） | ✅ |
| reply | 只在 `experimental_bg_reply=true`（G3 通过后由你手动打开）时才声明支持。步骤：<br>1. 重新查询一次，确认目标是 `bg` 并且是 `idle`；<br>2. 执行 `claude stop <short>`；<br>3. 执行 `claude --bg --resume <sessionId> -- <text>`（G3 实测：带 `--permission-mode` 会开出副本，所以不带；原 session 的权限模式会保留）；<br>4. 用 `agents --json --all` 核对 sessionId 没有变。出现新的 id，或者输出里有复制提示，就返回 `E_REPLY_FORKED`，并在本次运行期间关闭回复 | ⏳ G3 |

**输入防护**：
- `text` 去掉首尾空白后不能以 `-` 开头，否则返回 `E_BAD_FIELD`。
- `sid` 必须能在最近一次轮询结果里唯一对应到一个 UUID，否则返回 `E_UNKNOWN_SESSION`。
- `mode` 必须属于 `{plan, manual, acceptEdits, dontAsk, auto}`。

**session 命名**：取正文开头最多 4 个 ASCII 单词，转成小写，只保留 `[a-z0-9-]`，长度不超过 24；取不出来时用 `task-<n>`。正文里有中文（任何全角或宽字符）时，改取开头 8 个字：一个 ASCII 单词算一个字，ASCII 转小写，字和字之间原来隔着空白或标点的写成一个 `-`，最多 24 字节，按字符边界截断（v0.4.1，docs/20 U14）。

**日志**：argv 里的正文替换成 `<text len=N sha8=…>`；stderr 只记录前 200 字节，而且也要脱敏。

**hooks 与轮询的合并**（v0.4，#33，`agents/claude_code/hook_state.py`，纯函数）：hook 说明"为什么"，轮询（`agents --json`）说明"现在是否还是这样"。
- **阶段**：每个 session 记最近一次 hook 得出的阶段。`working`：UserPromptSubmit、PreCompact、PreToolUse / PostToolUse。`needs`（原因 permission / plan / question / input / login）：PermissionRequest、PreToolUse(AskUserQuestion)、Notification 的 `permission_prompt` / `elicitation_*` / `quota_auto_resume_stale`、登录类的 StopFailure。`done`：Stop、SessionStart、`idle_prompt`、其他 StopFailure。`ended`：SessionEnd。
- **一致就用 hook**：轮询的状态和阶段对得上时，用 hook 的结论，并带上原因和摘录（`msg`）。登录类等待在轮询为 `idle` 时也算对得上。
- **`seen`**：hook 之后开始的某次轮询显示过对得上的状态，这个 hook 就算被确认。确认之后，下一次对不上的轮询直接胜出（用户在终端里答了，或者没装 hook）。
- **`LAG_S`（10s）**：还没确认的 hook，在 hook 之后 `LAG_S` 内仍然压过对不上的轮询，认为轮询还没跟上。对话框类等待（permission / plan / question）只有 `NEEDS_GRACE_S`（2s）：普通工具批准之后没有任何 hook，只有轮询知道等待已经结束，所以 hook 之后 2s 以上才开始的轮询，只要不是 `waiting` 就以轮询为准。
- **`unknown`**：轮询状态不认识时，既不算确认也不算否定。`working` / `done` 照用 hook；`needs` 最多保持 `LAG_S`，不会一直亮着。
- **SessionEnd**：这个 session 在轮询跟上之前就从看板上去掉（最多 `LAG_S`）。
- **子 agent**：带 `agent_id` 的事件（子 agent 发的）不改变 session 的状态。
- hook 一到就用上一次的列表重新合并并推给设备，下一次轮询再确认或推翻。最多记 64 个 session；不在列表里的 session 超过 `LAG_S` 就忘掉。`waiting_for` 跟着合并后的状态走。没装 hooks 时一切照旧由轮询决定。

### 7.6 项目、收件箱与快照
- **项目校验**：`root` 必须是存在的目录，存成 realpath；**不要求是 git 仓库**。`key` 和 `slot` 必须唯一。`spawn_permission_mode` 默认 `plan`，填 `bypassPermissions` 时拒绝加载。daemon 每 3s 检查一次 `projects.json` 的修改时间，变了就重新加载。
- **cwd 归属哪个项目**：
  1. 对 cwd 取 realpath，按最长前缀匹配项目的 `root`。
  2. 没匹配上，就执行 `git -C <cwd> rev-parse --git-common-dir` 再匹配一次。结果缓存 60s，命令超时 2s。
  3. 还是没匹配上，归到 `other`。
- **since**：daemon 第一次看到某个 `(sid, st)` 组合时，记下当前时间作为 since；状态一变就重新计时。daemon 重启后，since 会偏小，可以接受。
- **need 的判定**：下面三个条件同时满足时，`need` 为真。
  1. 这个 session 属于已登记的项目；
  2. `st == waiting`，或者后台 session 的 `state` 是 `blocked` 而且没在跑（`st` 不是 busy / shell，#5），或者 `st == idle` 并且 `age ≥ inbox_idle_s`（默认 60s）。这里的 `st` 是和 hooks 合并之后的状态（见 §7.5）；
  3. `(sid, st, since)` 不在忽略列表里。
- **快照**：
  - 由纯函数 `build_snapshots(state) -> (projects, sessions, queue)` 生成。
  - 内容有变化时（不算 `age_s`）才把 rev 加 1。
  - sessions 的排序：先放 `need` 的，按等待时间从长到短；再放 busy 的；再放其余的。最多 10 条，`total` 写实际总数。
  - 每份快照序列化后都不超过 2048B，超出时从尾部减条目。
  - session 的 `name` 如果以它自己项目的 `key/` 开头（设备上开的 session 都是这样），下发时去掉这个前缀，因为设备显示时自己会加 `key/`；32 字节都留给名字本身（v0.4.1，docs/20 U14）。去掉以后为空就照原样发。

### 7.7 队列
- 当前队列是对事件日志的纯函数折叠：`fold_queue(events) -> tuple[QueueItem, ...]`。
- `qid` 由 daemon 单调分配，格式 `q<n>`。
- 设备发来带 `qid` 的 `agent.spawn` 时：
  1. 以队列里保存的全文作为 prompt；
  2. 成功后追加 `queue.spawned`。
- 命令行：`cpa queue ls [key]`、`cpa queue add <key> <text>`（`origin=cli`）、`cpa queue done <qid>`、`cpa queue reopen <qid>`（v0.4.1，docs/20 U12：设备上 Opt+D 点错了行，在 Mac 上撤销；只认已经关掉的 qid）。
- 设备开 session 时结果未知（超时或断线）存成的排队项带 `uncertain`，`queue.snapshot` 原样下发，设备据此提示"可能已开过"（v0.4.1，docs/20 U25）。
- v0.1 不做：排队的反馈重新投递给原 session；生成队列 markdown。

### 7.8 笔记（`notes/`）
- 每条 `item.received(kind=note)` 追加一行到 `<notes_dir>/YYYY-MM-DD.md`，格式是 `- HH:MM #<key> <text> ^cpa-<monoid 小写>`：
  - 日期和时间优先用 `wall_ms`；没有时用 `received_at`，前面加 `~`。
  - 未归属的笔记不加 `#key`。
- **幂等**：写之前先找块 ID，已经存在就跳过。
- **补写**：启动时把事件日志里还没落地的笔记补写一遍。
- **默认目录** `~/CardputerNotes`。这个目录不受 TCC 保护，LaunchAgent 可以直接写。
- 正文是单行，因为设备只能输入单行，daemon 也会把控制字符替换成空格。所以块 ID 一定落在同一行。

### 7.9 控制通道与命令行
- **控制通道**是 `cpad.sock`（AF_UNIX，权限 0600）。每次请求一行 JSON（最多 1024 字节），比如 `{"cmd":"pause","seconds":120}` 或 `{"cmd":"status"}`。不监听任何 TCP 端口。
  - `{"cmd":"hook","ev":…,"sid":…}`（v0.4，#33）：`cpa hook` 转发的一条 Claude Code hook 事件。只认白名单字段 `ev, sid, ntype, tool, detail, sub, msg`：`ev` 必须是字符串而且是已知事件，`sid` 是完整的 session UUID，`ntype` / `detail` 符合 `[a-z0-9_]{1,32}`，`tool` 符合 `[A-Za-z0-9_.:-]{1,64}`，`sub` 是布尔值，`msg` 是已经清洗过的单行摘录（≤120 字节）。daemon 逐字段重新校验（`agents/claude_code/hooks.parse`），任何一项不合格（包括类型不对，比如 `ev` 是数组或对象）都回 `{"ok":false,"error":…}`。hook 的内容从不执行。
- **命令行**：
  - `cpa daemon`
  - `cpa status`
  - `cpa project add|ls|rm`
  - `cpa queue ls|add|done|reopen`
  - `cpa pause [--seconds N]`
  - `cpa service install|uninstall`
  - `cpa device forget`
  - `cpa hook [EVENT] [--home DIR]`：Claude Code 对每个 hook 事件调用它。从 stdin 读最多 1 MiB，只转发白名单字段，0.5s 发不出去就放弃；总是 exit 0，从不打印（exit 2 会挡住提示或工具，stdout 会被 Claude 当作上下文）
  - `cpa hooks print|install|uninstall [--settings FILE] [--cpa PATH] [--yes]`：默认只显示改动，加 `--yes` 才写 `~/.claude/settings.json`（或 `--settings` 指定的文件）。handler 用 exec 形式（`command` 是 cpa 的绝对路径，`args` 以 `hook` 开头，不经过 shell）；卸载只删这样的 handler。写之前把旧文件备份到 `$CPA_HOME/hook-backups/`（目录 0700、文件 0600），不放在设置文件旁边：项目里的 `settings.local.json` 只按确切文件名被 git 忽略，旁边的副本会被提交
- **LaunchAgent**：
  - plist 用 `plistlib` 生成，放在 `~/Library/LaunchAgents/`，设置 `RunAtLoad` 和 `KeepAlive`。
  - 安装时执行一次 `zsh -lic env`，只保留 `PATH`、`HOME`、`LANG`、`LC_*`、`SHELL`、`USER`，写进 `spawn_env`。这几项都不是敏感信息。
- **日志**：
  - 每行一个 JSON。
  - `RedactFilter` 把正文类字段（`text`、`body`、`prompt`）替换成长度加 sha256 的前 8 位。
  - 只有设置了 `CPA_LOG_CONTENT=1` 才记录原文，这时启动会打印警告。

---

## 8. 安全分阶段

| 阶段 | 信任边界 | 措施 |
|---|---|---|
| **v0.1：只有 USB** | 物理接触，加上当前登录的 macOS 用户 | • 设备上没有任何密钥；不做配对（没有密钥的配对只是摆设）<br>• daemon 校验固件前缀，绑定 USB 序列号，遇到别的板子 6s 内放开<br>• 不开 TCP 端口，控制只走权限 0600 的 unix socket<br>• 执行类操作只能在线请求加确认，outbox 里不可能有执行类条目<br>• argv 不经过 shell，正文放在 `--` 之后，拒绝以 `-` 开头的正文，校验 `sid`<br>• 显式传 `--permission-mode`，默认 `plan`，拒绝 `bypassPermissions`，`auto` 必须由项目明确写明<br>• 两端都校验输入；错误信息只给错误码；日志脱敏<br>• `$CPA_HOME` 目录 0700、文件 0600 |
| v0.1 已知风险 | LittleFS 里还没同步的条目是明文 | 收到 ack 就删除；状态栏显示待同步数量；写进 SECURITY.md |
| v0.2：hooks、审批、SD 卡 | 同上 | • hooks 用命令转发脚本加权限 0600 的 header 文件；如果需要 HTTP，只绑 127.0.0.1，校验 Host 和 token，带 Origin 头的请求一律拒绝。不用原生 http hook 的 env 插值，因为凭据类变量不会被插值<br>• 审批按风险分键，400ms 防误触；设备不在时不做决定；写审计日志<br>• SD 卡上的记录带校验和，每个文件用 Mac 公钥单独加密封装<br>• MCP 写入的队列项要记录来源；没有经过 Mac 确认，不能用来开 session<br>• 可选 PIN 锁 |
| v0.3：无线 | WiFi / BLE | • 首次配对只能走 USB：在 NVS 里存密钥，`hello` 里做 HMAC 挑战<br>• 链路用 Noise 加密（参考 ESPHome 的方案）<br>• 没加密的链路上不允许开 session、回复或审批 |
| 始终不做 | — | 默认开启 Flash 加密或 Secure Boot；把任何接口暴露到公网 |

---

## 9. 测试策略

### 9.1 测试金字塔

| 层 | 命令 | 测什么 |
|---|---|---|
| 固件单元测试（纯核心） | `pio test -e native -f 'core/*'` | 用测试向量测 LineFramer 和 Codec；用 flows 测 LinkSession；MonoId；OutboxCore（**在每一步存储操作处注入崩溃**，结果 0 丢失、0 重复）；GhostFilter；ChordDetector（含 500ms 的 G0 边界）；keymap 静态检查；切片 reducer；`sizeof(AppState)`；`limits.h` 和 `limits.json` 是否一致 |
| 固件单元测试（界面） | `pio test -e native -f 'ui/*'` | shell 的按键分发顺序；Compose（字节上限、`-` 前缀、类型和目标切换）；Confirm 的 400ms 防误触、执行和排队的分支；agent 结果未知时存成排队项；三个模块的逻辑；用 FakeCanvas 断言屏幕上的关键文字 |
| 固件真机测试 | `pio test -e cardputer-adv-test -f 'device/*'` | LittleFS 写入、改名、重新打开；NVS 开机计数；保存 200 次，p95 ≤100ms；8KB RX 突发不丢字节 |
| daemon 单元测试 | `uv run pytest tests/unit` | codec 和校验器；事件折叠和队列折叠；need 判定（防抖、忽略）；项目校验和 cwd 解析；用脱敏后的真实输出解析 `agents --json`；argv 构造（恶意正文）；slug；笔记格式和幂等；脱敏；SafeOpen 的控制线序列；credit 窗口；快照上限和 2048B 截断 |
| daemon 集成测试 | `uv run pytest tests/integration` | 用 `FakeDevice` 做一对 pty：它会扣住第一条回复直到主机下一次写入，模拟 TX 被扣住；超过 8192B 还没读的字节会被丢掉。<br>用 `fake-claude` 放在 PATH 上：记录 argv、cwd、env，按 fixture 输出，可以翻转 session 状态。<br>还测：unix socket 的 pause 和 status；"离线意图永不执行"；fsync 先于 ack；重放去重；遇到别的板子时释放端口 |
| 契约测试 | `uv run pytest tests/contract` 加上固件的 `core/test_codec_vectors` | 两端读同一份 `protocol/vectors`，解码结果做语义深比较；`flows` 两端行为一致；`check_vectors.py` 用 jsonschema 校验所有用例 |
| 端到端（HIL） | `uv run --project daemon python tools/hil/run_e2e.py` | 真机加真 daemon 加 `fake-claude`，场景见 §9.3 |

**覆盖率**：
- 固件：`lib/` 和 `modules/`，不含 `hal/`、`usb_transport/`，行覆盖率 ≥80%，用 gcovr 统计。
- daemon：≥80%，用 pytest-cov 统计。
- HAL 不计入覆盖率，由 HIL 来覆盖。

### 9.2 先写的关键测试（RED）
- **outbox 崩溃注入**：`FakeStore` 可以在第 k 步操作后"断电"，k 遍历 open、write、close、rename、remove 的每一步。断言：显示过"已保存"的条目一条都不丢，也不会重复发送。
- **离线意图**：断言 outbox 里 `kind` 只能是 `note` 或 `queue`。在 Attached 或 Detached 状态下按 Confirm，产生的一定是 `queue` 条目。daemon 收到 `queue(intent=spawn)` 时，fake-claude 的调用次数为 0。
- **恶意正文**：`--dangerously-skip-permissions`、`-c`、`--permission-mode=bypassPermissions`、`$(rm -rf ~)`、`; ls`、`\u0000`、长度 501B。每一条要么被拒绝，要么作为一个原样的 argv 元素到达 fake-claude，并且排在 `--` 之后。
- **权限模式**：项目没写 mode 时，argv 里是 `--permission-mode plan`。`projects.json` 里写 `bypassPermissions` 时，加载失败。
- **RX 溢出**：在不经过 credit 的情况下发送 12KB，FakeDevice 会丢字节，链路要靠 rev 对账恢复。经过 credit 时，0 丢失。
- **栈和体积**：`static_assert(sizeof(AppState) <= 4096)`。

### 9.3 HIL 场景（测试版固件 `-DCPA_TEST_HOOKS=1`）
1. 握手 ≤3s；重新打开端口后 `up_ms` 连续，说明没被复位。
2. 速记闭环：注入 `G0`、文本、`Enter`，2s 内笔记出现在 `notes_dir` 里。
3. 离线补发：先 `cpa pause`，注入 3 条笔记，设备上待发数为 3；恢复后正好 3 条。
4. 丢弃 ack：打开 daemon 的测试开关，丢掉第一个 ack，最后笔记仍然只有一条。
5. 重启：发 `debug.reboot`，daemon 检测到新的 boot，重新下发快照。
6. 看板：fake-claude 返回项目 A 的 2 个 session，用 `debug.state` 确认设备上的计数。
7. 开 session：注入 Opt+S、文本、Enter，等 400ms 以上再按 Enter。fake-claude 收到的 argv 依次包含 `--bg`、`--name cpa/…`、`--permission-mode plan`、`--`、正文，cwd 是项目根目录。
8. 离线开 session：在 pause 状态下走同样的流程，结果是一条排队项，fake-claude 的调用次数为 0。
9. 延迟：fake-claude 翻转状态 100 次，设备更新的 p95 ≤5s；G0 打开 Compose 的 p95 ≤300ms。
10. 中文：用本机真实的 session 名渲染，并记录缺字情况。
11. 浸泡 1 小时：每秒一次 ping 和快照，加上每分钟一条笔记，0 丢失，TX 没有卡住。

**人工检查单**（`docs/07-hil-checklist.md`）：
- G1 的 CER 测试；
- 10 次断电（开关拨到 OFF 并拔线）；
- 拔掉再插上 USB；
- 用电池时关背光和唤醒；
- G0 长按的提示；
- 插一块别的 ESP32-S3 板子，确认 daemon 会放开；
- 用一个真的 `claude` 在临时项目里做一次冒烟测试；
- README 的 15 分钟实测（在全新的 macOS 用户下）。

### 9.4 CI（GitHub Actions；HIL 不进 CI）
1. `python3 tools/repo/check_sizes.py`、`uvx lizard -L 50 -w`、`ruff`
2. `uv run --with jsonschema python protocol/tools/check_vectors.py`
3. `pio test -e native` 加 gcovr，要求 ≥80%
4. `pio run -e cardputer-adv` 加 `check_budget.py`，并确认正式版二进制里不含 `debug.key`
5. `cd daemon && uv run pytest --cov --cov-fail-under=80`
6. （WP4 起）clang-tidy `readability-function-size`

---

## 10. 实现顺序与工作包

```
WP1 仓库骨架 + 协议契约 ──┬──► WP2 固件纯核心 ──► WP4 固件设备层 + 界面 ──┐
（含 Day-0 闸门）         └──► WP3 daemon ─────────────────────────────────┴──► WP5 E2E/HIL + 发布文档
```

- **并行规则**：WP2 和 WP3 可以并行，WP4 也可以和 WP3 并行。并行的工作包负责的目录互不重叠（见 §3 的标注）。
- **前后接手**：WP2 先放 `src/` 的桩代码，WP4 接手后改写。WP5 接手 README 的"快速上手"一节。
- **里程碑对应关系**：

| 里程碑 | 工作包 |
|---|---|
| M0 | WP1 加 Day-0 闸门 |
| M1 | WP2 和 WP3，加上 WP4 的输入、Compose、outbox |
| M2 | WP4 的看板和收件箱 |
| M3 | WP3 的开 session 和回复，加上 WP4 的确认框 |
| M4 | WP5 |

---

## 11. 风险与待验证

| # | 风险 | 退路 |
|---|---|---|
| R1 | 幽灵键和锁死（G1） | 只用两键组合；GhostFilter；看门狗；把 TCA8418 驱动 ADAPT 进来，每轮读空 FIFO |
| R2 | G2 两套工具链都不理想 | 用 A；语音推迟，等 v0.3 写自己的 `i2s_std` 驱动 |
| R3 | `agents --json` 或 `--bg` 的字段变了 | 宽松解析；每个 CLI 版本保存一份 fixture；启动时检测能力 |
| R4 | G3 失败 | 删掉 F7，反馈全部进队列 |
| R5 | LaunchAgent 下开出的 session 环境不对，或者项目还没被信任（G9） | 用 `spawn_env` 白名单；要求项目事先在终端里信任过；`cpa status` 自检 |
| R6 | `pio monitor` 或 esptool 抢走串口数据，甚至复位设备 | 上传前脚本自动调用 `cpa pause`；靠 boot 变化检测重新同步 |
| R7 | 内部 Flash 擦写时卡顿（几十到几百毫秒） | 用 p95 指标验收；条目写小；USB 突发传输期间不写 Flash |
| R8 | 工具链 A 改 `-std=gnu++17` 后，M5 的库编不过 | 在 G2 时就验证；不行就把领域代码限制在 C++14 |
| R9 | 收件箱噪声太大 | 60s 防抖；手动忽略；"其他"不进收件箱 |
| R10 | 一个人做的范围太大 | 已按评审砍掉一批功能；按工作包推进 |

---

## 12. 需要你决定的问题

完整列表和默认值见 04 §16。其中和架构直接相关的有这几项：
- 默认权限模式 `plan`：M3 之前必须确认。
- 不可变规则的例外边界。
- 笔记目录。
- 命名。
- session 开在项目主目录还是 worktree。

每一条决定都会写成 `docs/adr/NNNN-*.md`。

## 13. 评审处理（与架构相关的部分）

逐条记录见 04 §17。下面三条是架构层面专门做的取舍：
- 没有采用"三条快照合成一条"的方案，因为 9 个项目加 10 个 session 会超过 2048B。现在拆成 `projects.snapshot`、`sessions.snapshot`、`queue.snapshot` 三份，每份带自己的 `rev`，都要回复 `ack`。
- "主机主动发的消息一律要回复"是本稿新加的统一规则，目的是让 credit 窗口能算得清楚。
- `device.reboot` 删掉了，测试版固件改用 `debug.reboot`。`time.set` 保留了 `tz_min`。
