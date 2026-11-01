[English](02-landscape-report.md) · **中文**

# Cardputer ADV 口袋工作助手：现有项目调研报告

> 调研日期：2026-10-19。
> 方法：8 个方向并行搜索。分别是硬件、Cardputer 固件与框架、AI 编程助手的硬件伴侣、桌面伴侣类框架、离线录音、电脑与设备之间的通信与安全、中文和日文社区、GitHub 普查。搜到的结果合并去重后共 **201 个项目**。之后先做一轮"还缺什么"的检查，补搜了 2 个缺口，再对其中最关键的 **10 个项目做对抗式核实**：直接查仓库源码、提交记录、License 和 issue，专门找反例。
> 我又在本机亲自抽查了 20 个仓库是否存在，以及 Claude Code CLI 的 `agents --json`、`--bg` 和 `--name`。
> 全部 201 个项目见 [research/appendix-all-projects.md](research/appendix-all-projects.md)。

---

## TL;DR

1. **你的想法在 2026 年非常热门，但还没有人把它完整做出来。**
   - GitHub 上和 cardputer 相关的仓库有 1,596 个，其中 1,165 个是 2026 年新建的。
   - 2026 年 4 月以后，冒出了大约 60 个"Cardputer + AI 编程助手"的项目。起因有两个：Anthropic 开源了 Claude Desktop 的 **Hardware Buddy** 蓝牙协议（[anthropics/claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy)）；另外 Anthropic 在 **Code with Claude** 活动上直接发了 Cardputer-ADV，还配了官方插件 [cwc-makers](https://github.com/anthropics/claude-plugins-official/tree/main/plugins/cwc-makers)。
   - 但是没有任何一个项目同时做到这些：**按项目分组的看板**、**反馈队列**、**从设备上开新 session**、**组合键记笔记**、**离线录音、联网后再同步**、**加密的多种连接方式**、**模块化插件框架**。现有项目几乎都是一个人写的，0 到 200 颗星，代码写成一整块不分模块，不少还没有 License。
2. **建议不要整个 fork 某一个项目**，而是新建一个 MIT 或 Apache-2.0 的开源仓库，分四层：
   - 模块化的 C++ 固件：PlatformIO + Arduino + M5Unified/M5Cardputer
   - Mac 端的中枢守护进程（hub daemon）
   - AI 助手适配器：Claude Code hooks、`claude agents --json`、`claude --bg`，可选 mod
   - 一套带版本号的协议，USB、BLE、WiFi 三种连接通用

   然后从大约 8 个项目里借设计（见第 4 节），并把官方 Hardware Buddy 协议做成一个兼容模块。
3. **能存多少音频**：
   - 机身 Flash 只能存**几分钟到几十分钟**
   - SD 卡能存**几百到几千小时**
   - 真正的瓶颈是**电池**：关屏录音估计能撑 27 到 31 小时

---

## 1. 硬件事实：决定架构的几条

| 项 | 事实 | 对设计的影响 |
|---|---|---|
| 芯片 | ESP32-S3FN8：双核 240MHz，**8MB Flash，512KB SRAM，没有 PSRAM**（Espressif 型号表和多个固件配置都确认了） | 可用堆内存大约只有 200 到 300KB。复杂逻辑、渲染和集成要尽量放在 Mac 上 |
| 键盘 | TCA8418 I2C 键盘控制器（0x34，中断脚 G11），7×8 矩阵，**按键没有二极管** | **所有两键组合都不会误触发**。部分三键组合会多报一个根本没按的"幽灵键"（见第 6 节） |
| 修饰键 | Fn、Shift(Aa)、Ctrl、Opt、Alt，加上侧面的 G0 按钮。M5Cardputer 1.2.0 新增了 Fn 层（Fn+数字 = F1 到 F12，Fn+`;,./` = 方向键） | G0 很适合做"一键速记、按住录音"的专用键 |
| 唤醒 | G11（键盘中断）和 G0 都是 RTC GPIO，可以把芯片从深度睡眠或浅睡眠中唤醒 | 可以做到"装在口袋里睡眠，按键就醒"。M5Unified 没有现成实现，要自己写 |
| 音频 | ES8311 编解码器（8 到 96kHz），模拟 MEMS 麦克风，NS4150B 功放 | 功放硬件上**常开**，软件关不掉（只有插耳机时才会断开），会拉高睡眠电流 |
| 时钟 | **没有 RTC 芯片，也没有 32kHz 晶振** | 睡眠时每天会漂几秒，关掉电源开关时间就丢了。每次连上电脑都要同步一次时间 |
| microSD | 走 SPI 总线（和 EXT 扩展口共用）。ESP-IDF 默认不支持 exFAT | 大卡要格式化成 FAT32，或者换用 SdFat 库。单个文件最大 4GB，音频要分段存 |
| USB | 原生全速 12Mbps，只能当从设备。USB-Serial/JTAG 和 TinyUSB 共用一个 PHY | CDC 串口免驱动、刷固件能自动复位。想当 U 盘或 HID 键盘就得切到 TinyUSB 模式，二者只能选一个 |
| 电池 | 官方文档在 **2026-07-13 把 1750mAh 改成了 1500mAh**，取决于你那台的生产日期。充电芯片 TP4057，大约 300mA | 官方实测电流：亮屏工作 120mA，开 WiFi 132mA，开 BLE 155mA |
| 蓝牙 | 只支持 BLE 5，**没有经典蓝牙** | 不能当蓝牙耳机或麦克风用，音频只能走 WiFi、USB 或者自定义的 BLE 服务 |

### 能存多少音频（16kHz 单声道语音）

| 存储位置 | PCM WAV（32KB/s） | IMA-ADPCM（约 8KB/s） | Opus 16kbps（2KB/s） |
|---|---|---|---|
| 机身 Flash，Arduino 默认 8MB 分区（1.5MB 文件区） | **约 0.8 分钟** | 约 3 分钟 | 约 13 分钟 |
| 机身 Flash，去掉 OTA 只留一个 app 分区（约 4.9MB） | 约 2.7 分钟 | 约 10.6 分钟 | 约 43 分钟 |
| 32GB SD 卡 | **约 273 小时** | 约 1,079 小时 | 约 4,375 小时 |
| 128GB SD 卡 | 约 1,094 小时 | 约 4,315 小时 | 约 17,500 小时 |

- **内存只够缓存几秒音频**：1 秒 PCM 就是 32KB。所以录音必须边录边写 SD 卡，用 0.5 到 1 秒的环形缓冲来扛住写卡时的卡顿。
- **续航**：按 90% 可用电量估算，亮屏一直用大约 11 到 13 小时；关屏、关无线、只录音估计 27 到 31 小时（估算值，需要实测）。
- **深度睡眠电流没有任何人实测过**。按原理图估计会在毫安级，大约 3 到 4mA，主要是常开的功放。长期不用时建议直接拨电源开关。

---

## 2. 为什么 2026 年冒出这么多同类项目

- **2026-04-09**：Anthropic 发布 [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy)（2,658 星）。这是 Claude Desktop 开发者模式下的 BLE 协议：用 Nordic UART 传按行分隔的 JSON，内容包括 session 计数、待审批的请求，设备可以回复"允许一次 / 拒绝"。
- **Code with Claude 活动**：现场直接发 Cardputer-ADV，配套 [moremas/build-with-claude](https://github.com/moremas/build-with-claude) 和官方插件 [cwc-makers](https://github.com/anthropics/claude-plugins-official/tree/main/plugins/cwc-makers)（里面有 m5-onboard 等 skill，Apache-2.0）。
- **Claude Code 自己也开放了更多接口**：hooks、`claude agents --json`、`claude --bg --name`、mods（v2.1.287+）、channels（研究预览）。
- 结果就是大量**单人原型**，基本都只做了"状态显示 + 批准/拒绝"。

---

## 3. 重点候选项目（已对抗式核实）

| 项目 | 做了什么 | License | 支持 ADV | 活跃度 | 适合当基础吗 | 怎么用它 |
|---|---|---|---|---|---|---|
| [sohsatoh/cardputer-cli-buddy](https://github.com/sohsatoh/cardputer-cli-buddy) | **最接近你想法的"电脑端"**：用 BLE 或 WiFi 管理最多 9 个 Claude Code CLI session。Claude Code mod 负责注册 session、转发问题和审批、**以用户身份注入 prompt**。另有 Python 守护进程 buddyd，通信用 AES-CTR+HMAC 加密，**密钥只通过 USB 下发**，Mac 本机做语音转写 | Apache-2.0 ✅ | ✅（UIFlow2 MicroPython，锁定 v2.4.2） | 2026-10 新项目，作者只有一个人，0 星 | 中 | **照搬电脑端设计**：mod + daemon + 加密。设备端换成你的模块化固件。注意：不按项目分组，不能开新 session，不用 SD 卡，没有离线录音 |
| [dakshaymehta/cardputer-claude-os](https://github.com/dakshaymehta/cardputer-claude-os) | 星最多（208）。往 `/apps` 文件夹丢文件就能装应用；MCP 工具：notify、ask、confirm、show、progress；有审批审计日志、语音、Managed Agents 消息推送 | Apache-2.0 ✅ | ✅ | **已停更**（2026-07-01 后没有提交，7 个外部 PR 都没人回） | 弱 | 借鉴 **MCP 工具接口设计**和审计日志。应用一次只能跑一个（切换要重启），依赖云服务，MicroPython 堆内存紧张 |
| [anthropics/claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) | 官方 Hardware Buddy 协议和参考固件（M5StickC Plus） | MIT（bufo GIF 素材除外） | ❌（只有社区移植版） | 官方明确说**不维护**、不接受移植 | 弱 | **做成一个兼容模块**，这样不装 daemon 也能配合 Claude Desktop 使用。缺点：只有汇总数据，没有按项目区分，不能注入 prompt。终端里启动的 CLI session 的审批请求**收不到**（claude-code #89435） |
| [TactilityProject/Tactility](https://github.com/TactilityProject/Tactility) | 最完整的"ESP32 操作系统"：从 SD 卡加载 ELF 应用、有服务和内核模块、应用商店、Notes 应用，官方提供 ADV 固件包 | 核心 GPL-3.0；SDK、模块、ADV 设备目录是 Apache-2.0 | ⚠️ 有，但**ADV 没有音频**（设备树里缺 ES8311 节点），**Ctrl/Opt/Alt 不出字符** | 非常活跃（905 星）。不过维护者不接受小改动，主分支变化很快 | 中 | **值得持续关注，或者以后做成 Tactility 应用**。但现在录音和组合键这两个核心需求都满足不了，没有 PSRAM 时 ELF 应用能不能稳定加载也没验证过 |
| [m0rg0t/cardputer-adv-ai-console](https://github.com/m0rg0t/cardputer-adv-ai-console) | **离线录音到 SD + 持久化待发队列，等 WiFi 回来再上传**；带 TLS 证书固定的 Mac 网关；有 AI 编程 session 列表和审批 | MIT ✅ | ✅（C++，只支持 ADV） | 一个人维护，0 星 | 中 | 借鉴 **outbox 队列、SHA-256 去重、ES8311 录音服务、边录边写 WAV**。只支持 Codex，SD 上明文存密钥，代码不分模块 |
| [fat23cat/cardputer-hub](https://github.com/fat23cat/cardputer-hub) | **架构最理想**：分层的 Services、Mini Apps、ActionBus、Capability 注册表，配 Swift 写的 macOS Companion | ❌ **没有选 License**（等于保留所有权利） | ✅ | 一个人，1 星 | 弱（**法律上不能用**） | 只能参考架构，或者去问作者愿不愿意选 Apache-2.0 |
| [houxiaomu/m5stack-coding-toys](https://github.com/houxiaomu/m5stack-coding-toys) | 用 TypeScript 写的守护进程，从 statusLine 和 hooks 汇总多个 Claude Code session 的状态，再加上 git 信息；USB 上传 NDJSON；协议从 TypeScript 自动生成 C++ 代码 | MIT ✅ | ⚠️ 只是个空壳（键盘那部分没实现，大概率编译不过） | 2026-07 以后停更 | 中 | 借鉴**"多 session 状态汇总"的守护进程**和协议代码生成。它是**只读**的，不能审批也不能回复 |
| [charlottebepbop/cardputer-voice-remote](https://github.com/charlottebepbop/cardputer-voice-remote) | C++ 固件：录音，HTTPS 上传（证书固定，令牌通过 USB 下发），whisper.cpp 转写，再用 Channels 发进指定的 session | MIT | ✅ | **今天刚建的账号，一次性代码上传**，CI 一次都没真正跑过 | 弱 | 借鉴**局域网安全模型**和"Channels 投递"路径。刷机前务必审查代码 |
| [urazalievf/cardputer](https://github.com/urazalievf/cardputer)（CardputerOS） | 内核和应用分离、`Ctrl+数字`全局快捷键、Obsidian 格式的 Markdown 笔记、Mac 守护进程 | MIT | ❌ 只支持原版 v1.1 | 17 小时写完，之后就没动过 | 弱 | 借鉴 **App 接口和编译期注册**的模式 |
| [srg0/s1-adv](https://github.com/srg0/s1-adv) | 功能很多，但所有代码编译成一个单元（2.5 万行），后端绑定 OpenClaw | MIT 加部分 LGPL | ✅ | 活跃 | 弱 | 可以借鉴分块上传和带认证的 OTA |

**结论**：没有一个项目能直接拿来当底座。主要原因是 License、单人维护、代码不分模块、ADV 支持不完整这几样。

---

## 4. 按你的功能逐项对照：谁做过，借什么

| 你的功能 | 最好的参考 | 借什么 |
|---|---|---|
| **按项目显示进度，标出哪个需要我反馈** | `claude agents --json`（本机 CLI 已有，确认过）+ hooks；[m5stack-coding-toys](https://github.com/houxiaomu/m5stack-coding-toys) 的状态汇总；[abdallah/session-hub](https://github.com/abdallah/session-hub)（MIT，有 HTTP API：sessions、inbox、messages、start、permissions） | 按 git 根目录把 session 归到项目下，结合 hooks 推送实时变化 |
| **快速反馈，注入到正在跑的 session** | cardputer-cli-buddy 的 mod，用 `$.prompt.submit`；session-hub 的长轮询 mod；Channels（[tern/cardputer-claude](https://github.com/tern/cardputer-claude)、voice-remote） | 做成可选适配器。mods 是 2026-10 才出的新功能，Channels 还是研究预览 |
| **给项目开新 session** | `claude --bg --name <项目>/<主题> "<prompt>"`，在项目目录下执行（本机 CLI 确认过）；session-hub 的 start 接口；[herdr](https://github.com/herdrdev/herdr)（Apache-2.0，4.5 万星，socket API 支持 agent.list、prompt、worktree.create） | 在 daemon 里维护一份 `projects.json`，记录项目名、路径和别名（参考 [walkie](https://github.com/LewisSwan24/walkie) 的做法） |
| **审批** | [loml13/cardputer-claude-mcp](https://github.com/loml13/cardputer-claude-mcp)（MIT）：按风险分级的 PreToolUse 审批关卡，设备回复用 HMAC 签名；cli-buddy 的 PermissionRequest hook，终端和设备谁先答算谁的 | 不同风险用不同的键；危险操作要长按；设备不在时一律拒绝；每次决定都写审计日志 |
| **组合键记笔记** | [pjournal-esp32](https://github.com/autumnc/pjournal-esp32)：用 Ctrl 组合键当命令，Ctrl+Space 切中英文，Ctrl+I 速记；[aikb](https://github.com/xyb/aikb)（MIT）：同一个键单击、双击、三击、长按代表不同命令（tap-dance）；CardputerOS：Ctrl+数字 | 见第 6 节的按键方案 |
| **离线录音，联网后回传** | [maximmmanaev/cardputer-voice-notes](https://github.com/maximmmanaev/cardputer-voice-notes)（**没有 License**，只能借设计）：按 G0 录音，每 5 分钟一段写到 SD，断点续传，SHA-256 校验，Mac 确认收到之后设备才删；m0rg0t 的 outbox；[xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)（MIT，3.1 万星，**官方支持 ADV**，在没有 PSRAM 的 ADV 上能实时编码 Opus） | 第一阶段先存 PCM WAV 到 SD：不费 CPU，识别率最好。第二阶段再把 Opus 做成可插拔的模块 |
| **Mac 上转写** | mlx-whisper large-v3-turbo；Qwen3-ASR（通过 [mlx-audio](https://github.com/Blaizzy/mlx-audio)，2026-10 的测评中中英文都最强）；whisper.cpp；Apple SpeechAnalyzer | 你经常中英混说，所以**转写引擎要能替换**，拿你自己的录音来挑 |
| **中文显示和输入** | [hermes-remote-cardputer](https://github.com/Max-liu-1989/hermes-remote-cardputer)（MIT）：在 Mac 上渲染 1-bit 的 240×135 画面再发给设备，每帧约 1KB；efontCN 12px 字库约 112 到 210KB；[zh_pinyin_decoder](https://github.com/FRIEDparrot/zh_pinyin_decoder)（MIT，词库可以放 SD 卡） | v1 先不在设备上做输入法。拼音交给 Mac 转换，语音也在 Mac 上转成中文 |
| **模块化框架（设备端）** | ESPHome 的 external_components（编译期模块，带 manifest）；Flipper 的 FAP 外部应用和 API 版本控制；Stack-chan 的 MODs（manifest 里声明需要哪些能力）；[Mooncake](https://github.com/Forairaaaaa/mooncake)（M5 官方出厂固件用的应用生命周期框架，MIT） | v1 用**编译期模块注册**。运行时从 SD 加载应用（ELF）以后再说 |
| **模块化框架（电脑端）** | Stream Deck SDK / [OpenDeck](https://github.com/nekename/OpenDeck) 的 OpenAction；Bitfocus Companion；DeskThing | **社区贡献主要会发生在电脑端插件上**：插件是独立进程，可以用任何语言写，通过 manifest 声明，走本地 WebSocket |
| **通信协议** | Flipper 的 protobuf 信封格式（command_id、has_next、oneof）；Meshtastic 的串口分帧；Hardware Buddy 的 NDJSON；[stack-chan-dock](https://github.com/meganetaaan/stack-chan-dock)（MIT，已经定义好 approval.request 和 task.status 事件） | 消息类型按模块加前缀，比如 `cardputer.<module>.<action>`，连接建立时先握手交换版本和能力 |
| **安全** | ESPHome 的 Noise_NNpsk0；KDE Connect 那种"用户在两边确认后才配对"；BLE 用 LESC 绑定加数字配对码；libsodium 把 SD 上的文件逐个用 Mac 的公钥封装 | **首次配对走 USB**（必须物理接触），之后 WiFi 和 BLE 都走加密信道 |

---

## 5. 推荐架构

```
┌──────────────────────── Cardputer ADV（C++ 固件）────────────────────────┐
│ 模块：notes │ voice │ projects │ approvals │ queue │ buddy-compat │ …       │
│        ↑ 编译期注册：manifest + setup/loop/on_message/render                 │
│ 核心：事件总线 │ 键盘(TCA8418 加固驱动) │ UI │ SD outbox(ULID+ack) │ 电源/睡眠 │
│ 传输：USB CDC（主） │ BLE NUS │ WiFi(TCP/WS)   ← 一套带版本的协议 + Noise 加密   │
└──────────────────────────────────────────────────────────────────────────┘
                     │ USB 插上即同步 / WiFi / BLE
┌──────────────────────── Mac hub daemon（LaunchAgent）────────────────────┐
│ 独占串口 / BLE / WiFi 监听；配对与密钥；outbox 收件(幂等)；转写队列           │
│ projects.json（项目 = git 根目录）；每个项目一条反馈队列                     │
│ 插件（独立进程，JSON over 本地 WS）：                                       │
│   claude-code: hooks(HTTP) + `claude agents --json` + `claude --bg` + 可选 mod │
│   codex / herdr / session-hub 适配器 │ notes→Obsidian │ 转写引擎(可换)        │
│ 对外：localhost MCP server（notify/ask/confirm/capture_note/list_queue）     │
└──────────────────────────────────────────────────────────────────────────┘
```

**几个关键选择和理由：**

- **固件用 C++，不用 MicroPython（UIFlow2）。**
  - UIFlow2 在 ADV 上只剩大约 60 到 128KB 空闲内存。
  - UIFlow2 v2.4.3 以后的版本（基于 IDF 5.5）在 ADV 上**录音是静音的**。
  - 我们要同时跑录音、加密、BLE、WiFi、多个模块，MicroPython 撑不住。
  - **备注**：UIFlow2 可以当作"第一周快速摸清硬件"的工具，但不作为框架。
- **Arduino（PlatformIO）还是 ESP-IDF？**
  - 推荐先用 PlatformIO + Arduino + M5Unified/M5Cardputer，上手最快，官方也有配置。
  - 以后需要精细控制电源和 USB 组合设备时，可以过渡到"ESP-IDF + Arduino 当组件"，cardputer-hub 就是这么做的。
  - 这件事需要你来定，见第 9 节。
- **USB CDC 当主链路**：免驱动，按 VID:PID 自动发现，刷固件能自动复位。U 盘模式（MSC）做成一个手动进入的"传输模式"，因为进入后设备和电脑不能同时访问 SD 卡。
- **把复杂的东西放到 Mac 上**：没有 PSRAM，设备只负责采集输入、显示文本、存 outbox 和录音，集成和渲染都由 daemon 来做。
- **Tactility 和自研固件之间的取舍**：
  - Tactility 是唯一真正的"操作系统 + 插件"方案，但现在 ADV 上没有音频，修饰键也不出字符，核心代码是 GPL，维护者不接受小改动。
  - 所以 **v1 自研**，模块接口尽量设计得能移植。等 Tactility 补上 ADV 音频之后，可以评估把模块做成 Tactility 应用。
- **兼容官方 Hardware Buddy**：做成一个模块。这样不装 daemon 也能和 Claude Desktop 配合，社区现有的 CLI 桥接工具也能直接用，比如 [cc-buddy-bridge](https://github.com/SnowWarri0r/cc-buddy-bridge)。

---

## 6. 按键组合设计（草案，供讨论）

**硬件约束**：键盘没有二极管，所以**两键组合永远安全**。三键组合里，下面这些搭配会多报一个没按的"幽灵键"：
- Fn+Shift 加上 a 到 Enter 这一行的键
- Ctrl+Opt 加上 z 到 Space 这一行的键
- Ctrl+Alt 加上数字行、q/w/e、a/s、z 等

Fn+Alt、Shift+Alt、Opt+Alt 加第三个键，各自只有 2 个键会出问题。
另外有两个项目报告过 TCA8418 在快速打字后**锁死**，需要自己写更可靠的驱动：每轮把 FIFO 读空，再加看门狗，卡住就重新初始化。

**建议**：用 **Opt 当"本应用专用层"**（Opt 在 HID 里没有含义，不和系统快捷键冲突），再配合 **G0 物理键**和"先按一下前导键、再按字母"的方式：

| 操作 | 按键 | 说明 |
|---|---|---|
| 速记一条笔记 | **G0 短按** | 任何界面、睡眠中都能直接唤醒，进入输入框 |
| 按住录音（push-to-talk） | **G0 长按** | 保留 300 到 500ms 的预录缓冲，开头的字不会被截掉 |
| 切到项目 1 到 9 | **Opt + 1…9** | |
| 给当前项目加一条反馈或排队任务 | **Opt + Q** | 存进 outbox，联网后发给 daemon |
| 给当前项目开新 session | **Opt + S** | 由 daemon 执行 `claude --bg` |
| 打开"等我反馈"收件箱 | **Opt + I** | |
| 批准（常规） / 拒绝 | **Enter / Esc** | 审批请求出现后 400ms 内的按键一律忽略，防止误触 |
| 批准（高风险操作） | **Fn + Y 长按** | 和常规批准用不同的键 |
| 前导键模式 | 单击 Opt，再按一个字母 | 单手就能操作，完全不会误触发 |

这张表要等设备连上以后，用原始键码日志实测验证，尤其是 `Ctrl+Shift+S`、`Fn+Shift+A`、`Ctrl+Alt+W` 这几个组合。

---

## 7. Claude Code 接入点（按稳定性排序）

| 层 | 能做什么 | 稳定性 |
|---|---|---|
| **Hooks**（settings 配置，HTTP 回调到 127.0.0.1） | 收到状态（Notification、Stop、SessionStart）；**审批**（PermissionRequest）；AskUserQuestion 的答案（PreToolUse 修改输入） | ✅ 长期存在，有完整文档 |
| **Agent SDK** | daemon 自己创建并完全控制的 session | ✅ 官方维护，有版本号 |
| **`claude agents --json [--all]`** | 每个 session 的状态（cwd、working/blocked/done、在等什么） | 🟡 Agent view 属于研究预览，但文档写明它的 JSON 是外部读取状态的支持方式 |
| **`claude --bg --name`** | 在后台给某个项目开新 session | 🟡 本机 v2.1.272 已有 |
| **Mods（`$.prompt.submit`）** | 往正在跑的交互式 session 注入 prompt | 🟠 v2.1.287（2026-10-01）才加入，行为还在变。**本机是 2.1.272，要升级才能用** |
| **Channels** | 把设备消息推进 session，并转发审批请求 | 🟠 研究预览；自定义 channel 启动时必须加 `--dangerously-load-development-channels` |
| **Hardware Buddy BLE** | 只有 Claude Desktop 开发者模式支持，只给汇总数据 | 🟠 官方说明"不是正式支持的功能" |

**原则**：daemon 启动时先检测能力（Claude Code 版本号、`agents --json` 能不能跑通）。注入 prompt 做成可选功能。不要拿 PTY 或 tmux 抓屏模拟键盘当主路径。

---

## 8. 已知的坑

1. **录音静音问题还没修好**（截至 2026-10-19）：基于 ESP-IDF 5.5 的工具链（UIFlow2 ≥2.4.3、M5Stack Arduino 板包 3.3.x）在 ADV 上**录到的全是 0**，ESP-IDF #18621 仍未关闭。
   - 解决办法：**锁定 M5Stack 板包 3.2.x（IDF 5.4）**，或者绕过 M5.Mic，自己用 `i2s_std` 写录音通道。
   - 开机时加一个麦克风自检。
   - 这会影响 [01-mac-setup.md](01-mac-setup.zh-CN.md) 里"装最新版"的建议，那份文档已经补了提醒。
2. **没有 RTC**：每次连上电脑都同步时间，笔记和录音的 ID 要用单调递增的序号，不能只靠时间戳。
3. **睡眠电流偏高**：功放常开，估计有毫安级。睡眠前要关掉 ES8311、屏幕背光（GPIO38）和 ST7789，然后实测。**连着 USB 时不要睡**，因为 USB-Serial/JTAG 在深度睡眠时会断开。
4. **SD 卡**：exFAT 默认不支持，大卡要格式化成 FAT32，音频按 5 分钟分段，文件先写成 `.part`，录完再改名。
5. **U 盘模式和 SD 卡互斥**：进入 MSC 前设备要先卸载文件系统。Launcher 在 macOS 上挂载 ADV 的 bug 刚在 10-15 修掉，还没发布正式版。
6. **License 卫生**：不少好项目没有 License（cardputer-hub、clawdputer、voice-notes、tern/cardputer-claude、walkie），**只能借设计，不能复制代码**。claude-desktop-buddy 里的 bufo GIF 素材不在 MIT 范围内。
7. **安全**：
   - **默认不要开 Flash 加密和 Secure Boot**：基本是单向操作，开了以后 M5Burner、Launcher 这些社区刷机工具就都用不了。
   - 改用"SD 文件逐个加密、用 Mac 的公钥封装"。
   - 审批接口**绝对不能暴露到公网**。tern/cardputer-claude 的 Tailscale Funnel 做法是反面教材。
8. **官方截图有误**：M5Burner 恢复出厂的教程截图里，对话框标题写的是 "Burn Cardputer User Demo"。实际要选 **Cardputer-Adv** User Demo，别被误导。

---

## 9. 下一步：第一阶段建议

**需要你来做的决定：**
1. **License**：MIT，还是 Apache-2.0？Apache 带专利授权条款，cli-buddy 和 cardputer-claude-os 用的都是它，借它们的代码更省事。
2. **固件技术栈**：先用 PlatformIO + Arduino，还是一开始就上 ESP-IDF？
3. **AI 助手范围**：v1 只做 Claude Code，还是 Codex 也要一起支持？
4. **中文**：v1 只做显示，输入交给 Mac 和语音？还是一开始就要在设备上打拼音？
5. **连接方式**：v1 先只做 USB，再加 WiFi 和 BLE？还是三种一起上？

**第 0 步（硬件）**：
- 先让 USB 能识别（见 [01-mac-setup.md](01-mac-setup.zh-CN.md)）
- 读出芯片和 Flash 信息，确认真的没有 PSRAM
- **备份出厂固件**
- 用键码日志实测组合键会不会误触发
- 测一下麦克风

**第 1 阶段（1 到 2 周的验证）**：
- **固件 hello**：键盘输入 → 通过 USB 发 NDJSON 给电脑 → 写 SD → 录 10 秒 WAV。工具链锁定在 IDF 5.4 那一套。
- **Mac daemon hello**：用 `claude agents --json` 按 git 根目录给 session 分组，在 Cardputer 上显示"项目 A：2 个运行中、1 个等你"。
- 跑通一条完整的闭环：**G0 速记 → 存进 SD outbox → 插上 USB 自动同步 → 落到 Mac 上的笔记目录**。

**之后**：
1. 离线录音加转写
2. 反馈队列和开新 session（hooks + `--bg`）
3. 模块 SDK 和 manifest
4. Hardware Buddy 兼容模块
5. WiFi 和 BLE，以及加密配对

**回馈社区的机会**：
- 给 xiaozhi 或 M5Cardputer 提交更可靠的键盘驱动
- 帮 m5stack-coding-toys 补上 ADV 的验证
- 请 cardputer-hub、clawdputer、voice-notes 的作者选一个 License
- 给 session-hub 贡献一个"设备网关"客户端

---

## 附：主要资料来源

- 官方：[Cardputer-Adv 产品页](https://docs.m5stack.com/zh_CN/core/Cardputer-Adv)，[ADV 原理图](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Sch_M5CardputerAdv_v1.0_2025_06_20_17_19_58.pdf)，[M5Cardputer](https://github.com/m5stack/M5Cardputer)，[M5Unified](https://github.com/m5stack/M5Unified)，[出厂固件 CardputerADV 分支](https://github.com/m5stack/M5Cardputer-UserDemo/tree/CardputerADV)
- Anthropic：[claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy)，[cwc-makers 插件](https://github.com/anthropics/claude-plugins-official/tree/main/plugins/cwc-makers)，[build-with-claude](https://github.com/moremas/build-with-claude)
- Espressif：[esp-desktop-buddy](https://github.com/espressif/esp-desktop-buddy)，[esp_tinyusb](https://github.com/espressif/esp-usb/tree/master/device/esp_tinyusb)，[esp-adf-libs（Opus）](https://github.com/espressif/esp-adf-libs)
- 架构参考：[Flipper Zero 固件](https://github.com/flipperdevices/flipperzero-firmware)、[ESPHome](https://github.com/esphome/esphome)、[Stack-chan](https://github.com/stack-chan/stack-chan)、[OpenDeck](https://github.com/nekename/OpenDeck)、[Pebble mobileapp](https://github.com/coredevices/mobileapp)、[Omi](https://github.com/BasedHardware/omi)
- 全部 201 个项目：[research/appendix-all-projects.md](research/appendix-all-projects.md)
