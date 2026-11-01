[English](25-route-to-any-app.md) · **中文**

# 25 · 说着话送到任何 app：去向 = 通用种类 + 每个 app 一份预设

> 状态：**设计稿，待你拍板**（§8）· 日期：2026-10-22（同日按四份评审修订：用户、隐私安全、契约架构、可行性；评审之间的分歧和取舍见 §9）· 角色：架构（去向和路由）· 基于 origin/main `d330465` · 对应 #61，关联 #34、#53、#52、#54、#24。
> **方向调整（2026-10-26，D23、D24、§7.6）**：要求是：语音不宜直接交给 coding agent，先进自己的 app，再手工放到不同的 agent 里。所以：**R2 里设备说完就直接送到 agent（桌前自动填草稿、说话时按 `X`）暂缓**；语音先进 Mac app 的"待归档"，由你在 Mac 上挑去向、按 ⌘↩ 送出（R1 已经能做）。项目改成 Mac 上建的**有名字的分组**，设备上按 1–9 归组，代码目录可连可不连。
> 要求（2026-10-22）：把说的话送到任何聊天或 agent app，例如 Grok；说话时按键选项目，和现在的设计一样；机制不是给某一个 app 做的集成，而是让所有 app 都容易接上，以语音输入为主。
> 依据：docs/13 §9.5–§9.6；docs/14（只在本地分支 `feat/destinations`）；docs/15 §3.4–§3.12、§4、§5；docs/18 §3.5–§3.6、§7、§12；docs/20 D3、U11；docs/22 §3.1、§5.4、§5.9、§8；docs/23 §2.1、§3.1–§3.6、§5.2；docs/24 §1.2、§2.1、§2.4、§5.2（`wt-mac`，未合并）；三份调研：A 每个 app 的入口，B 通用机制和本机实验，C 和已有设计对账。
> 标记：**[源]** 读过的代码、app 包或文档 · **[测]** 本机实测 · **[研]** 外部文档或论坛 · **[算]** 推算 · **[推]** 推断 · **[设]** 设计假设，可以改 · **[待]** 要验证 · **👤** 需要你。
> 范围：只写设计。没改别的 app 的设置和文件，没往任何 app 或服务发过东西；Grok Bot 等 app 的行为是从 app 包、`app.asar` 代码和公开文档读出来的，**没有实际跑过**（§8.2 第 2 条请你手测）。
> 路径缩写：`cpad/` = `daemon/src/cpad/`；`dst/` = `feat/destinations`（`b5ad361`）的 `daemon/src/cpad/dest/`；`d14` = 同一分支的 `docs/14-destinations.md`；`d24` = `wt-mac` 的 `docs/24-mac-app.md`；`GB` = `/Applications/Grok Bot.app/Contents/Resources/app.asar`（`main-core.cjs`、`main-app.cjs`）。

## 0. 一页摘要

1. **你要的**：说话时按 1–9 选项目、按一个字母选"送到哪"，字就进 Grok Bot；同一套能接任何 app，不给某个 app 写专用代码。
2. **Grok Bot 走不了本机 MCP**（你猜的那条路）：它的 MCP 服务跑在 Grok Bot 账号的云端电脑上，碰不到这台 Mac 的 `cpa` 和笔记 [源 GB]。能用的是它自带的**预填链接** `grokbot://app/v1/prompt?text=…&id=<Bot>`：把字放进那个 Bot 的输入框并聚焦，**从不替你发送** [源 GB]。
3. **任何 app 都能接**（§2）：去向 = **通用种类** + **每个 app 一份预设**（纯数据）。最通用的一种是 `app.<名>`：字放进剪贴板（只在本机、不进历史），把那个 app 调到前面，你按 ⌘V。**这台 Mac 上任何 app 都能这样接，在列表里点一下就加上，不写 JSON**。有预填链接的 app（Grok、Claude、Cursor、ChatGPT）用 `url.<名>`，少按一下 ⌘V。去掉 docs/14 的 `plugin`；信箱 `box` 这次不做（§2.5）；本机命令 `cmd` 放到 R4。代码里出现预设里的 bundle id 或链接 scheme，测试就失败。
4. **你只做一次**（§3.1）：Mac app 集成页 → 去向 → Grok Bot → 字母默认 `X` → 加上 → **送一条测试**，再回答两个问题："Grok 里出现'测试'了吗？""自动发出去了吗？"。答"出现了、没自动发"以后，这个去向才算"只填草稿"。不改 Grok Bot 的任何设置，不要 API key。
5. **在桌前、设备插着**（§3.2）：按住 G0 说话，中途按 `3`（项目）和 `X`（Grok），松开；约 3 s 后 Grok 跳到前面，输入框里是一行 `[Cardputer 语音·可能有错字或别人的话·#acmeshop] 你的话`；**你在 Grok 里按回车才真正发出去**。一共 4 下。说超过约 1 分钟（约 220 个汉字）就走剪贴板，多按一下 ⌘V。
6. **揣兜一天、回来再插上**（§3.3，最常见）：说的时候照样 3 下；回到 Mac，"待归档"顶上的"**等你送**"一组里每条按 ⌘↩、再在 Grok 里回车，每条 2 下。设备上的"等你"里按 Enter 也行。
7. **只按了项目号也能到 Grok**（§3.4，R1 就有）：给项目 3 设"常用去向 = Grok"，项目 3 的便条在"等你送"里已经选好 Grok，⌘↩ 一下送。不用等固件。
8. **不会丢、不会做两次**（§3.6）：预填最多一次，同步、重启、重连都不重放；Grok 的同一个 Bot 30 分钟内已经填过一条时，下一条改走剪贴板并提示"Grok 里还有上一条"，免得被 Grok 悄悄吞掉。今日笔记那一行永远先写。
9. **永不自动执行**：预填只填草稿；送去会执行的地方（`cmd`、没验证过的链接）每条都要在设备上翻完全文按 Enter；会议只进今日笔记、待办和本机文件；话里说的"发到哪"不算数，只认你按的键；家和公司的去向分开（docs/18）。
10. **Mac 端**（§5）：复活 `feat/destinations`（cherry-pick，5 处一行的冲突），再测试先行地改；链接由 cpad 经 LaunchServices 打开，只交给加的时候认准的那个 app（bundle id + 签名 Team ID），正文不进任何进程的参数；不要辅助功能权限，不用 AppleScript。（2026-10-22 实现记录：R1 里从 Mac 送的链接由送的那个 `cpa` 进程用同一个 `LSOpenFromURLSpec` 打开，cpad 打开要等 R2 接进 hub，见 §7.4、D7。）
11. **分期**（§7）：**先手测**（你，2 分钟）→ 合并 docs/23 的 `feat/filing-p1` → **R1 只动 Mac**（加固件的编解码表登记，不发固件）：`cpa filing send`、等你送、常用去向，约 1,100 行代码 → **R2 设备**：说话时按 `X`、桌前自动填草稿。R3 拉取（可选）、R4 `cmd`、R5 粘贴执行器。
13. **2026-10-26 方向调整**（D23、D24、§7.6）：设备直接送到 agent（R2 的 `X`、桌前自动填草稿）**暂缓**；语音先进 Mac app，你再手工送到不同的 agent。项目是 Mac 上建的有名字的分组（`cpa project add -- 名字`，代码目录可选），设备上按 1–9 归组。
12. **22 条决定**都有默认（§8.1），分成"你会感觉到的"11 条和"工程内部"11 条；要你回答的 6 件在 §8.2。

## 1. 你要的，和今天差在哪

### 1.1 要求拆开

- 说的话直接进 Grok 这类聊天或 agent app（§3）。
- 本机 MCP 是一种可能的路；对 Grok Bot 不通（§2.5）；Claude Code、Cursor、Codex 今天就能经 docs/15、docs/23 的只读工具来读（§5.8）。
- 说话时按键：数字选项目，字母选去向（§4.1）。
- 沿用现在的设计 docs/14 的按键和路由（§5.1）。
- 不限于某一个 app：机制通用，app 只是配置（§2）。
- 语音为主，打字是辅（§3、§4）。
- 没有 app 专用代码；接一个 app = 在列表里点一下（§2.1、§2.3）。

### 1.2 目标：按你的标准数按键（数到"在 Grok 里发出去"）

| 做什么 | 怎么按 | 次数 |
|---|---|---|
| 在桌前、设备插着：说一句送到 Grok，归 3 号项目 | 按住 G0 说，中途按 `3`、`X`，松开；约 3 s 后 Grok 跳出来；回车 | 1 按住 + 2 键 + Grok 回车 = **4**（R2）|
| 揣兜一天回来，插上 USB | 说的时候同上 3 下；回到 Mac："等你送"里每条 ⌘↩，Grok 里回车 | 设备 3 下；Mac 上**每条 2 下** |
| 只按项目号，项目 3 的常用去向是 Grok | 按住 G0 说，中途按 `3`；Mac 上"等你送"里这条已选好 Grok：⌘↩，Grok 里回车 | 设备 2 下；Mac 上每条 2 下（**R1 就有**）|
| 说超过约 1 分钟（约 220 个汉字）| 同上；Grok 跳出来时输入框是空的，提示"全文在剪贴板"：⌘V，回车 | **+1**（⌘V）|
| 同一个 Bot 30 分钟内已经填过一条 | 这条走剪贴板，提示"Grok 里还有上一条：这条在剪贴板，⌘V 接在后面" | **+1**（⌘V）|
| 设备锁着录的 | 不自动填；到 Mac 前在"等你送"里 ⌘↩ | 同"揣兜回来" |
| 说完改主意 | 自动填好的：在 Grok 里删掉；还在"等你送"的：Mac 上 ⌥D，或设备"等你"里 Opt+D | 1 |
| 打字送到 Grok | Opt+X，打字，Enter；Grok 里回车 | 连着 Mac 时 Enter 就填草稿（D8）|
| Mac 上把一条旧便条送到 Grok | 待归档里选中，⌥X；Grok 里回车 | 2 |
| 交给 Claude Code | 照今天：录音时按 `Q` 排队，或 `S` 到看板上开（plan 模式）| 不变 |

每一下都看得见结果：设备上的"存到"胶囊立刻变，被拒的键有一行安静的提示（§4.2）；Mac 上 5 秒提示"已填进 Grok · 到 Grok 里按回车发出"；没有声音。

### 1.3 今天的样子

- **docs/14 的去向设计和代码只在本地分支** `feat/destinations`（`b5ad361`，比 origin/main 落后 56 个提交 [测 `git rev-list`]）。cherry-pick 到 main 只有 5 处一行的冲突，两边都保留后 daemon 2725 个、协议 526 个测试全过 [测，可行性评审在临时克隆里跑过，已删]。
- **docs/23 的 `cpa filing` 在 `feat/filing-p1`**：比 main 多 7 个提交、少 25 个，和 main 有 9 处冲突（`cli/commands.py`、`cli/main.py`、`hub/snapshots.py`、`queue/fold.py`、一个测试、docs/15、ADR 0004、两个协议 README）[测 `git merge-tree`]。R1 要先合并它。
- **转写 `cpa-stt` 已经在 main 上**（#62，`d330465`），**默认关**，你跑一次 `cpa-stt use` 才开 [源 docs/22 §4.13]。不开的话便条没有 `memo.text`，任何去向都会降级成今日笔记（前置检查 `no_text`）。
- **main 上设备从不发 `todo` 以外的去向**：录便条时只绑了 0–9 [源 firmware/app/lib/input/keymap.cpp:99-104]；`RecMeta` 只有 `slot`，没有 `dest` [源 firmware/app/lib/rec/rec_meta.h:37]；录音页"存到"胶囊只显示项目 [源 firmware/app/lib/shell/rec_pages.cpp:257-258]。但线上的 `outbox.item` 和便条的 `file.items` 已经带 `dest` 字段 [源 protocol/README.md 第 20 条]。
- **docs/14 的 `plugin` 是 WhisperWoof 的 MCP 插件** [源 d14:53]，正是 #34 以后不要的 app 专用集成 [源 docs/13-assistant-v2-plan.md:503-513 的修订]。
- **代码里已经有 app 名字**：`cpad/mcp/cli.py:18` 写死 4 个 MCP 客户端（claude-code、codex、cursor、claude-desktop）和它们的配置路径；`cpad/agents/claude_code/**` 是 ADR 0002 的 session 执行器 [源]。
- **Grok Bot 0.75.0**：Cursor 的 Electron 底子，bundle `com.anysphere.sand`，链接 scheme `grokbot://`、`sand://`；MCP 配置表单有 stdio 和 http 两种，存在账号上，不在本机文件里 [源 GB]。
- **这台 Mac 上还有**：ChatGPT.app（其实是 Codex，`com.openai.codex`）、Claude.app、Cursor、Notion、Obsidian、`claude` 命令行、xAI 的 `grok` 命令行（`~/.grok/bin/grok`，不是 Grok Bot）[测]。

## 2. 模型：种类 + 预设

### 2.1 原则

- **去向 = 种类 + 名字**，例如 `url.grok`、`app.notion`。种类定规则（要不要确认、离线怎么办、谁执行），名字只是你的配置；id 自带种类，离线记下的 id 事后不会被改成别的种类 [源 d14:59-64]。
- **任何 app 都有一条路**：`app.<名>` = 剪贴板 + 把 app 调出来 + 你按 ⌘V。它对每个 app 都成立，不要任何权限。有预填链接的 app 用 `url.<名>`，是少按一下的升级，不是前提。
- **预设 = 一份 JSON**：种类、模板、长度上限、建议字母、是否离开 Mac、怎么认出"装了这个 app"、链接会不会自动发送（`effect`）。随 cpad 的 wheel 发（`cpad/dest/presets/v1/`，用 `importlib.resources` 读，#57 不让用 `__file__`）；你自己的放 `$CPA_HOME/presets/*.json`（0600，同一份严格 schema，不能顶替随 cpad 发的 id）。加新 app 不用等 cpad 发版：`app.<名>` 在列表里点一下，有链接的写一份预设（§2.3）。
- **代码里没有 app**（测试，名单从 `presets/v1/*.json` 读，不手写）：
  1. 整个 `cpad/` 和 `mac/`：不许出现任何预设的 bundle id 或 `<scheme>://`（不分大小写）。
  2. `cpad/dest/**` 和 `mac/**/*.swift`：再加预设 id 和 `label`（区分大小写、整词）。不在 `cpad/mcp/**`、`cpad/filing/**` 里查 `label`：那里的 `cursor` 是分页游标。
  3. 豁免：`cpad/agents/claude_code/**`（ADR 0002 的 session 执行器）。`cpad/mcp/cli.py` 的 `CLIENTS` 过得了 1 和 2，但它仍是 app 专用代码，测试注释里记成带日期的已知例外，R3 把它搬进数据（§5.8）。d24 §2.4 第 1 张卡在 Swift 里写死的 4 个标签同样改成从 `cpa mcp setup --list --json` 来。
- **加种类的门槛**：这台 Mac 上能做；正文不进任何进程的参数；不注入按键；"送到了"的意思说得清。只有新**机制**才加种类，新 app 永远只加预设或在列表里点一下。

### 2.2 种类，第 2 版

`fl` 是设备和 Mac 都看的一串标志：`l` 现场（只在你在场时做，同步、重启、重试都不补做）、`c` 离开 Mac、`p` 要项目、`k` 每条在设备上翻完全文按 Enter。

| 种类 | 类 | 做什么 | 谁执行 | `fl` | 和 docs/14 比 |
|---|---|---|---|---|---|
| `daily`、`todo` | 归档 | 今日笔记一行 | NotesWriter | — | 不变 |
| `file.<名>` | 归档 | 往文件追加一行；R4 加 `mode:"drop"`：每条写成一个文件 | FileAdapter | 默认 `c`，`--local` 才没有 | 不变（R4 加 drop）|
| `queue` | 记录 | `queue.added`，`intent=todo` | 路由 | `p` | 不变 |
| `session` | 执行 | 只排 `intent=spawn`，设备看板上 Enter 才开（ADR 0002）| 路由 | `p` | 不变 |
| `clip` | 现场 | 复制到剪贴板 | ClipWriter | `l` | 写法换成 ClipWriter（§5.3）|
| **`app.<名>`** | 现场（只填草稿）| 复制到剪贴板 + 把那个 app 调出来；**你按 ⌘V，从不替你按** | AppAdapter | `l`，`c` 由你加的时候选 | **新** |
| **`url.<名>`**（`effect:"draft"`）| 现场（只填草稿）| 按模板打开预填链接 | UrlAdapter | `lc`（看预设）| **新** |
| **`url.<名>`**（`effect:"act"`）| 执行 | 同上，但目标 app 可能会自己做事 | UrlAdapter | `lck` | **新** |
| `paste`、`paste.<名>` | 现场 / 执行 | ⌘V 到前台 app 或指定 app，从不按回车 | 外部执行器（R5）| 裸 `paste`：`lk`；`paste.<名>`：`l` | 加名字 |
| `cmd.<名>`（R4）| 执行 | 跑一个参数写死的本机程序，正文只走 stdin | CmdAdapter | `lck` | **新** |
| ~~`plugin.<名>`~~ | — | 删掉；WhisperWoof 的插件变成 `app` / `url` 预设或 R4 的 `cmd` | — | — | **删** |

- 策略改成看 `fl`，不再写死 `ARCHIVE_KINDS`、`live`、`session` [源 dst/policy.py:17、42、73-82]；`KindSpec.custom` 拆成 `builtin` + `custom`，因为 `paste` 两样都是 [源 dst/model.py:49]；`PluginOpts` 换成 `UrlOpts`、`AppOpts`（R4 再加 `CmdOpts`）[源 dst/model.py:74-76]。
- 名字 ≤12 个字符，对齐 main 的线上模式 `^[a-z][a-z0-9-]{0,11}(\.[a-z0-9-]{1,12}){0,2}$` [源 protocol/limits.json:72]；docs/14 的 16 个字符会被 main 的固件拒收 [源 dst/model.py:32-33]。
- **设备上的容量**：`dests.snapshot` 最多 12 行，去掉 5 个内置的（`daily`、`todo`、`queue`、`session`、`clip`），设备上最多 7 个自定义去向；可绑的字母 9 个，`C` 是剪贴板，自定义最多 8 个。更多的去向只在 Mac 上用（⌥字母、"送到…"）。

### 2.3 一份预设长什么样

```json
{"v": 1, "preset": "grok-bot", "label": "Grok", "kind": "url", "effect": "draft",
 "detect": {"bundle": "com.anysphere.sand"},
 "template": "grokbot://app/v1/prompt?text={text}&id={p}",
 "focus": "grokbot://app/v1/agent?id={p}",
 "max_text_utf16": 1000, "max_url": 2048, "overflow": "clip",
 "map": {"pattern": "^[A-Za-z0-9_-]{1,128}$",
         "from_link": "^grokbot://app/v1/(?:agent|prompt)\\?(?:[^#]*&)?id=([A-Za-z0-9_-]{1,128})"},
 "leaves_mac": true, "key_hint": "x"}
```

- **模板变量**：`{text}` 必须恰好一次；`{project}` 是项目 key；`{p}` 取你在 `map` 里给这个项目填的值（Grok 用来选 Bot；没填时取 `map["*"]`）。值为空的查询参数整个去掉，所以没填 Bot 时链接里就没有 `&id=`。`map` 收 `pattern` 的值，也收一整条链接，用 `from_link` 取出 id，所以你在 Grok 里复制 Bot 的链接直接贴就行。
- **模板只许这些形状**（`urllib` 解析，不是字符串拼）：`{text}`、`{p}`、`{project}` 只能各自是一个完整的查询值；scheme、host、path 都是字面量。字面量查询参数的名字不许像 `key|token|auth|secret|sig`，字面量值最长 16 个字符。
- **固定的头**：cpad 给每次交给别的 app 的字（`url`、`app`、`paste.<名>`、溢出到剪贴板的）前面加一个头，和正文**在同一行**，中间一个空格：
  - 语音：`[Cardputer 语音·可能有错字或别人的话·#acmeshop] 正文`
  - 打字：`[Cardputer 打字·#acmeshop] 正文`
  - 没项目时去掉 `·#…`。第一个字永远是 `[`，所以 agent 输入框里 `!`（shell）、`/`（命令）、`@`、`#` 开头的特殊含义碰不到；接收的 agent 也知道这是语音识别的草稿。
  - 不换行，因为 Grok 拒收任何带控制字符的查询值（`\n` 也算），整条链接被丢掉而 LaunchServices 照样回成功 [源 GB `ob()`、`Fg()`]。
- **`effect`**：`"draft"` = 可以只填草稿、从不自己做事；`"act"` = 可能做事。**缺了就当 `act`**。只有随 cpad 发、评审过模板的预设才能写 `draft`；你写的 `url` 预设一律 `act`，测试也升不上去。写了 `draft` 的预设，每台 Mac 上加的去向也先当 `act`，直到"送一条测试"后你答"出现了、没自动发"（§3.1），记一条 `dest.changed{verified:"draft"}`；答"发了"就一直是 `act`。`act` 的去向归执行类（`fl` 有 `k`）：每条设备上翻完全文按 Enter，Mac 上点不算（D14），打字 Enter 不直接送（D8）。
- **`overflow: "clip"`**：清洗、加头以后正文超过 `max_text_utf16`（按 UTF-16 码元数，`len(s.encode("utf-16-le"))//2`，和 Grok 的 JS `.length` 一样：一个 emoji 算 2）或编码后整条链接超过 `max_url` 时，走 `app` 的路：全文进剪贴板，用 `focus` 链接把那个 Bot 调到前面（没有 Bot id 时只把 app 调到前面），提示"太长：全文在剪贴板，到 Grok 按 ⌘V"。不截断，不丢字。**只在剪贴板去向开着时**才这样走（你关了剪贴板，它就失败，记 `E_TOO_LONG`，这条留在等你送里写"太长"）。
- **`destinations.json` 仍然严格**（未知字段拒收、0600 [源 d14:122-142]）。`cpa dest add` 把预设里的 `template`、`focus`、上限、`overflow`、`effect`、`map` **抄进**这个去向，记下 `preset:"grok-bot"` 作来源；改了或删了预设文件，已经加的去向不变（和 d14"id 自带种类"一个道理），`cpa dest ls` 提示"有新版预设"。
- **能接到哪里**（三道关，表是数据，放在 `presets/v1/deny.json`）：
  1. scheme 必须在预设 `detect.bundle` 那个 app 的 `CFBundleURLTypes` 里登记过。
  2. 不许的 scheme：`http`、`https`、`file`、`data`、`javascript`、`applescript`、`shortcuts`、`x-apple*`、`smb`、`vnc`；不许的路由：`*/mcp/install*`、`run-shortcut`、`advanced-uri`、`x-callback-url`；不许的处理 app：`com.apple.shortcuts`、`com.apple.Terminal`、`com.googlecode.iterm2`、`com.apple.ScriptEditor2`。
  3. 你写的预设一律 `act`（见上）。
- **`map` 的值**：照 Grok 的规则最长 128 [源 GB `Tu=/[A-Za-z0-9_-]{1,128}/`]；以已知密钥前缀开头的拒收（`xai-`、`crsr_`、`sk-`、`sk_`、`ghp_`、`github_pat_`、`AKIA`、`xox`，表是数据）。没有任何字段是给密钥用的。
- **Grok 的测试向量**：`presets/v1/grok-bot.vectors.json` 照抄 Grok 自己的检查：链接里每个原始字符在 0x21–0x7E；没有 `#`、`\`；整条 ≤2048；解码后没有控制字符，匹配 `\s*\S[\s\S]*`；空参数去掉 [源 GB `NB()`、`Rle`]。
- **预设的格式是公开契约** `protocol/dest-preset/v1/`（schema、对的和错的向量、README），`cpa dest add --preset-file PATH` 先按它校验。

### 2.4 这台 Mac 上每个 app 用哪种

记号：✓ 能用 · ✗ 没有。"会不会自动发送"没手测前一律按 `act` 加（§2.3）。

| App（版本）| `url` 预填（不发送）| 已有的只读 MCP 能读（docs/15、docs/23）| `app` 剪贴板 + 调出 | 默认 |
|---|---|---|---|---|
| Grok Bot 0.75.0 | ✓ `grokbot://app/v1/prompt?text=&id=`；正文 ≤1000 个 UTF-16 码元、整条 ≤2048，约 220 个汉字 [源 GB] | ✗ MCP 服务跑在它的云端电脑上 [源 GB] | ✓（溢出时就用它）| `url.grok`，测试后算只填草稿 |
| ChatGPT.app 26.1007（Codex）| ✓ `codex://threads/new?prompt=`（`q` 也映射到 `prompt`）；会不会自动发送 [待] [源] | ✓ `~/.codex/config.toml` | ✓ | `url.codex`，测试后才算只填草稿 |
| Claude 3.1623.4 | ✓ `claude://claude.ai/new?q=`、`claude://code/new?q=&folder=`，约 16 KiB；会不会自动发送 [待] [源] | ✓ `claude_desktop_config.json` | ✓ | `url.claude`，测试后才算只填草稿 |
| `claude` 命令行 2.1.306 | — | ✓ | — | 已有的 `queue`、`session` |
| Cursor 3.24.18 | ✓ `cursor://anysphere.cursor-deeplink/prompt?text=&workspace=`，约 1,100 个汉字；拒收控制字符和像密钥的词 [源] | ✓ `~/.cursor/mcp.json` | ✓ | `url.cursor`，测试后才算只填草稿 |
| Notion 7.34.0 | ✗ 只能开页面 | ✗ 只有云端 Custom Agents [研] | ✓ | `app.notion` |
| Obsidian 1.8.9 | `obsidian://daily?append=` 能用，但不需要 [研] | ✗ | ✓ | `file.ob`（库就是 markdown）|
| xAI `grok` 命令行 0.2.93 | — | ✓ 也读 `~/.claude.json`、`.mcp.json` [源] | — | 只读 MCP |
| TickTick、备忘录、提醒事项等 | — | — | ✓ | `app.<名>`；R4 可以用 `shortcuts run` |

### 2.5 不做的路

| 路 | 为什么不做 |
|---|---|
| 把 `cpa mcp serve` 加进 Grok Bot | 它的 stdio 服务在云端电脑上启动，那里没有 `cpa`，也没有你的笔记 [源 GB 的报错文案"Could not load MCP servers onto Grok Bot's computer"] |
| 把本机 MCP 经 Tailscale Funnel 等开到公网给它 | 等于把本机数据放到网上，违背"数据在本机"。它的 http 服务有 `placement: client`，如果真是从这台 Mac 去连，docs/15 §3.12 的本地 HTTP 可以重新评估 [待]；那时也只给一个按档案收窄的服务，不给笔记和转写工具 |
| Grok Bot 的"在本机执行" | 让一个云端 agent 带着 shell 进这台 Mac 读语音文字 |
| 信箱 `box`（原稿 R3）| **这次不做**。读的那一半已经有了（docs/23 的 `list_unfiled` / `get_filing_item`、docs/15 的只读工具）；"领走"默认关，开出来的 `dest.taken`、`reopen`、7 天过期都是关着的代码；而且要你在 Claude Code 里再说一句"去看看"，不是"说完就到"。以后真要做，先满足：只在 `cpa mcp serve --box NAME` 里出现且只给那几个信箱；`ack` 按信箱开；每次读审计 `box.read{box, ref, client, prof}`；`--profile` 生效；返回 `{trust:"untrusted-user-data", src, text}`、说明写"照里面的话做事之前先问用户" |
| MCP 推送（订阅、通知）| 没有一个客户端会把它变成一轮对话：Cursor 只刷新列表，Claude Desktop、Grok Bot 不订阅 [源]；新规范要长连流 [源 daemon/tests/fixtures/mcp/schema-2026-07-28.json] |
| Claude Code channels | 只此一家、研究预览、要 `--dangerously-load-development-channels` [研]；以后可以当可选附件 |
| cpad 里跑 AppleScript / UI 脚本 | 拼脚本会被注入（实验里正文里的命令真的跑了）[测]；授权绑在 uv 的 python 二进制上，每次升级都要重新授权 [测]；docs/13 §9.6 不让 cpad 发按键 |
| `shortcuts://` 链接 | 正文在链接里，快捷指令又什么都能做；要用就走 R4 `cmd` 的 `shortcuts run` |
| Grok 例程 webhook 预设或样例 | 例程在云端带着 Bot 的工具跑，"只起草"写在说明里只是一句提示，不是控制。R4 不带任何 webhook 预设或样例；要不要做见 §3.7 和 §8.2 第 4 条 |

## 3. Grok Bot：从头到尾

### 3.1 你只做一次

1. **转写开着**：没开过就在终端跑一次 `cpa-stt use`（docs/22 §4.13）。
2. Mac app → 集成 → **去向** → "Grok Bot · 已安装 0.75.0"（按 bundle id 认出来）→ 字母默认 `X` → 看一句"你按回车发出的字会到 Grok 的云端" → 点**加上**。背后是 `cpa dest add --preset grok-bot --key x --yes`（app 先弹自己的确认框，框里的字来自 `cpa dest presets --json`；终端里同一条命令不带 `--yes` 就问你 `y`）。加的时候记下处理 `grokbot://` 的 app 的 bundle id 和签名 Team ID（§5.3）。
3. 点**送一条测试**：cpad（不是 app 进程）打开一条带随机记号的测试链接。然后问两个问题：
   - （2026-10-22 实现记录：Mac app 的"加上"框里先问这个 Bot 的链接，所有项目都进这一个 Bot；空着也能加，测试时再贴。测试送出后会说"测完把输入框里的「测试 4821」删掉，别按回车"。）
   - **"Grok 里出现'测试 4821'了吗？"** 答"没出现"：多半是没指定 Bot 时落不到主 Bot（看 Grok 的功能开关 `sand_grok_main_agent`，否则悄悄返回 `no-main-agent`）[源 GB]。接着问"粘贴这个 Bot 的链接或 id"，你在 Grok 里复制 Bot 的链接贴进来，cpad 用预设的 `from_link` 取出 id，存成 `map["*"]`，再测一次。
   - **"你没按回车，它自己发出去了吗？"** 答"没有"：这个去向记成只填草稿（`dest.changed{verified:"draft"}`）。答"发了"：保持 `act`，每条都要在设备上翻完全文按 Enter（R2；R1 里这台 Mac 不送它）。
4. （可选）给项目设**常用去向**：项目 3 → Grok（§3.4）。
5. 设备下次连上就收到新的去向表（`dests.snapshot`），不用改设备上的任何设置。

**不用做的**：不往 Grok Bot 里加 MCP，不要 API key，不开 Grok Bot 的"在本机执行"，不改它的任何设置。

### 3.2 在桌前、设备插着（R2）

| 时刻 | 设备 | cpad | Grok Bot |
|---|---|---|---|
| 按住 G0 | 开录；"存到"胶囊显示 `#–` | — | — |
| 中途按 `3`、`X` | 胶囊变成 `#3 bark → Grok 云` | — | — |
| 松开 | 便条落盘，带 `slot:3`、`dest:"url.grok"`、`lock:0` | 连着就立刻拉取（10 s 的话约 0.6 s）| — |
| +约 3 s | 已回到主页（现状的结束提示）| 转写出字（10 s 的话约 1.7 s [源 docs/22:726]）；**先写今日笔记那一行**，再 `decide()`：判成"现场"（§5.2），打开预填链接，记 `dest.handed{via:"url", leaves_mac:true}` | 跳到前面，输入框里是 `[Cardputer 语音·可能有错字或别人的话·#acmeshop] 你的话`，光标在里面 |
| 你在 Mac 前 | — | — | 你看一眼、改一改，**按回车才发出去** |

自动填草稿要**同时**满足（任何一条不满足，这条就进"等你送"，§3.3）：
- 这个去向手测过、记成只填草稿（`effect:"draft"` 且 `verified`）；
- 便条结束时设备连着这台 Mac、没锁屏，同一次开机，从松开到出字不超过 60 s（设备的 `up_ms`）[设]；
- Mac 没锁屏（`CGSessionCopyCurrentDictionary` 的 `CGSSessionScreenIsLocked`）[待]，而且 Mac 键盘 3 s 内没人按（`CGEventSourceSecondsSinceLastEventType`，不要权限 [待]）：免得你正在别处打字时 Grok 抢走焦点，你的回车把它发出去；
- 不是会议；同一个 Bot 30 分钟内没填过别的（§3.6）。

这是 D5 的默认。代价：Grok 会在松开约 3 s 后自己跳到前面，共享屏幕或演示时也一样（§8.2 第 3 条）。cpad 是后台 LaunchAgent，macOS 26 会不会从那里把 Grok 的窗口带到前面 [待]，R1-C 的验收里用真的 LaunchAgent 测。

### 3.3 揣兜一天、回来再插上（最常见，docs/20 U11）

- **说的时候**：照样按住 G0、`3`、`X`，松开。设备锁着也能按字母 [源 d14:475]。便条带着 `dest` 落盘，什么都不弹。
- **插上以后**：cpad 拉取、转写、写今日笔记那一行；这些便条不是"现场"，判成暂存（`hold`），**同步时从不打开 Grok** [源 d14:216]。
- **在 Mac 上**："待归档"页顶上多一组"**等你送 · 4**"，每行写"→ Grok · #3 acmeshop · 08:10"和前几句，Grok 已经选好。⌘↩ 送这一条（Grok 跳出来，你回车），选中自动跳到下一条。每条 2 下。
- **在设备上也行**："等你"里选中按 Enter（每条设备 1 下 + Grok 回车 1 下）。
- **一次送好几条进同一个 Bot**：第二条起走剪贴板并提示"Grok 里还有上一条"（§3.6）。所以先在 Grok 里把上一条发出去，再 ⌘↩ 下一条；"等你送"那一组顶上写着这句。
- 终端里同样：`cpa filing triage` 先过"等你送"的几条，↩ 送到选好的去向，`g` 再按字母换一个去向。

### 3.4 只按了项目号：项目的常用去向（R1）

- 集成页"去向"卡里给项目设"常用去向"：项目 3 → Grok（存进 `destinations.json` 的 `usual:{"acmeshop":"url.grok"}`）。
- 项目 3 的便条（有字、不是会议、还没判过去向、24 h 内的）出现在"等你送"里，Grok 已经选好，写"建议"。⌘↩ 送，不管就算了，24 h 后不再提示，不写任何记录。
- **只认明确的键**这条不变：常用去向只是预选，真正选的是你在 Mac 上按的 ⌘↩（`how:"pick"`）；它永远不自动送（前置检查 `not_explicit`）。
- 这样 R1 不用等固件：今天设备上按 `3` 说话，回到 Mac 上 ⌘↩，Grok 就开好了草稿。设备上的 `X`（R2）只在这一条要去项目常用去向以外的地方时才需要。

### 3.5 Grok Bot 看到什么、做什么

- **看到的**只是输入框里的一段草稿，第一段是那个头。它不知道 Cardputer，也读不到你别的笔记。
- **做的**：在你按回车之前什么都不做。发出去以后，字到了 Grok Bot 账号的云端电脑上（"Bots and chats stay on the account's cloud computer"）[源 GB]——**这是你选的那一下让它离开 Mac 的**。
- 有字和项目一起到：项目在头里（`#acmeshop`）；指定了每个项目的 Bot，就由 Bot 区分项目。
- Grok Bot 能不能反过来读这台 Mac 上的东西：不能，除非你开它的"在本机执行"（不推荐，§2.5）。

### 3.6 怎么保证不丢、不做两次

| 机制 | 语义 |
|---|---|
| `url` | **最多一次**。一条采集只判定一次（docs/14 的折叠）；打开链接后立刻记 `dest.handed{via:"url"}`，不叫"送到了"：LaunchServices 收下链接，不说明 Grok 收下了草稿。重启、重连、重试都不再打开 [源 d14:216 不变式 2]。要再送，只能你在 Mac 上 ⌘↩ 或在设备"等你"里再按 Enter，记 `by`。 |
| 同一个 Bot 的草稿 | cpad 记住每个（去向, Bot）上一次预填的时间。**30 分钟内**再往同一个 Bot 填：这条改走剪贴板（`dest.handed{via:"clip"}`），用 `focus` 把那个 Bot 调到前面，提示"Grok 里还有上一条：这条在剪贴板，⌘V 接在后面"。剪贴板去向关着时，这条留在等你送，写"Grok 里还有上一条"。 |
| `app`、`clip`、`paste` | 最多一次；不自动重试；崩在中间记 `E_INTERRUPTED`，等你确认。 |
| `cmd`（R4）| 同上。 |
| `file` | 至少一次 + 块 id 去重，等于一次。 |

**Grok 会悄悄不收的情况**（都返回成功，cpad 看不到）[源 GB]：输入框里已经有没发的字（保留旧的，`kept-draft`）；同一条链接 2 s 内来两次（`CU=2e3`）；Bot id 解析不了；没给 Bot id 又没开主 Bot（`no-main-agent`）；链接里有控制字符（`query-value`）。前两条由上面的"30 分钟"兜住，后三条由加的时候的测试和 §2.3 的清洗兜住。Bot 不在你的列表里或没权限时它会弹一个提示。只有隐藏的团队 Bot 会弹确认卡；你自己的 Bot 不会 [源 GB]。

今日笔记那一行永远先写，所以哪条路失败了，话都还在。

### 3.7 不在 Mac 前也要它干活：例程 webhook（不做，只问你）

- Grok Bot 的每个 Bot 能挂"例程"：按时间、收邮件、或收到 webhook 就跑一次。webhook 的路径 `/automations/webhook/<例程 id>` 在 app 包里 [源 GB `main-app.cjs`]，要 `Authorization: Bearer crsr_…` [研：Hookdeck 指南和 Cursor 论坛]。
- 它在云端带着 Bot 的工具跑，正文一定离开 Mac，"只起草"写在说明里也拦不住。默认不做，R4 不带预设或样例。
- 你在 §8.2 第 4 条答"要"的话，条件是：只送打字的，或者在设备上翻完全文的语音；≤500 B；接的 Bot 没有任何有副作用的工具（不发邮件、没有 MCP 写、不开"在本机执行"）；确认卡上写"云端会执行"；key 在钥匙串里，由你自己的脚本读，cpad 不存。

### 3.8 老实说的限制

- **一个 Bot 一次只有一份草稿**：§3.6 的"30 分钟"是猜，cpad 看不到你在 Grok 里有没有按回车。猜错的代价只是多按一下 ⌘V。
- **约 220 个汉字**：更长的走剪贴板（§2.3 `overflow`），多按一下 ⌘V。说超过约 1 分钟就是这样，不算少见。
- **草稿会不会在你按发送前就同步到云端** [待]：所以 Grok 预设一律算"离开 Mac"。
- **Grok 只在你按发送时干活**；要它自己动，只能用云端的例程（§3.7）。
- **Mac 要醒着、你要登录着**：cpad 是登录会话里的 LaunchAgent；合盖时到的便条留在等你送。

## 4. 设备

### 4.1 按键

- **录便条时**（按住 G0，或 `fix/memo-hold` 的免按住模式 `RecLatch`，两层都绑）：数字 = 项目（今天就这样）；**绑了去向的字母 = 去向，后按的算**；`N` 退回今日笔记。G0 在键盘矩阵外，G0 + 一个键是两键组合，不会串键 [源 docs/13-assistant-v2-plan.md:127；docs/04-prd.md:374]。
- **没绑的字母**：一行安静的提示"Z 没绑去向"，不改任何东西。
- **要项目的去向**（`Q`、`S`）没按数字：胶囊变成提醒"要项目·按 1–9"。
- **开会时字母不生效**：会议里有别人的话 [源 d14:263；docs/23-mac-inbox.md §3.4]。
- **打字**：Opt+X 直接打开对准 Grok 的编辑器；编辑器里 Opt+字母改去向；Opt+G 列出全部去向 [源 d14:91-100]。在 Grok 的编辑器里按住 G0 加的语音片段照 docs/22 拼进同一条（只填草稿类收片段，`k` 类不收，§7.2 修订 docs/22）。
- **字母预算**：固定 `N T Q S`；可绑定只有 `b c f j k o u x y` 9 个；`G` 留给选择器，所以 Grok 用 `X`（xAI）[源 dst/model.py:29-31]。`X`、Opt+X、Opt+G 在每个固件分支上都没被占 [测]。
- **Mac 上用 ⌥ 加字母**：Mac 的待归档页已经用了 `j k x`，`cpa filing triage` 用了 `u x y n`（docs/23 §5.2；`feat/filing-p1` `cli/triage.py:236-237`），9 个可绑字母里 5 个撞了。所以 Mac app 里是 ⌥X，终端里是 `g` 再按 `x`（终端里的 ⌥ 随键盘布局打出别的字）。

### 4.2 屏幕（240×135，录便条页）

```
y14   [免按住 / 宽限 的胶囊，现状]
y95   存到 [#3 bark] → [Grok 云]           ← 去向胶囊：云 = 离开 Mac
y110  X Grok  C剪贴板  O sb  …              ← 只列绑了的自定义字母，最多 5 个，当前的反色
y123  1cpa 2blog 3bark 4th 0不归属          ← #53：1–9 是谁，代替今天的固定提示
```

- **y95 放得下**："存到"从 x60 起，项目胶囊从 x89 起；去向胶囊最多 4 个 ASCII 或 2 个汉字再加"云"（≤48 px），项目胶囊截到约 91 px，到 x234 为止 [算]。
- **y110 放得下**：一行约 228 px，5 个就约 210 px；多了写"…"，Opt+G 看全部 [算]。
- 今天 y123 是固定的"G0松开结束 1-9选项目 0不归属" [源 firmware/app/lib/shell/rec_overlay.cpp:109]；#53 把它换成项目名，空槽不显示，每槽宽 ⌊(38−n)/n⌋ 个 ASCII 字符 [算]。y110 今天是空的 [源 firmware/app/lib/module_api/canvas.h:110-116]。docs/14 的"按住时：T待办 C剪贴板…"那一行和 #53 合进同一个布局；#53 要等 `feat/ux-decisions` 的录音页合并。

### 4.3 确认卡：只给执行类

- 便条结束后设备回到主页、弹一个提示 [源 firmware/app/lib/shell/runtime_rec.cpp:125-130]；字约 3 s 后才有（松手宽限 0.6 s、拉取约 0.6 s、转写约 1.7 s）。这时你可能已经打开了别的页面。
- **只有 `fl` 含 `k` 的去向弹卡**（`cmd`、`effect:"act"` 的 `url`、裸 `paste`）。只填草稿类不弹：桌前自动填（§3.2），不满足条件的进等你送。只有 `c`（离开 Mac）的归档类（`file`）也不弹：开启时已经确认过一次（d14 §2.1）。
- **卡只在主页、没开别的东西时出现**；别的时候只把"等你"的数字加一。卡出现后 500 ms 内的 Enter 不算，免得一下给编辑器或列表的 Enter 变成"送出"。
- **卡上是全文分页**：一页页翻完，Enter 只在最后一页有效；cpad 收到 `dest.send go` 时核对这次连接里设备取过这一条的全文（`dest.get` → `dest.text{ref, text, sha8}`，sha8 对得上），对不上就拒绝（照 docs/22 §5.4 第 2 条）。语音超过 40 s 或正文超过 500 B 的，不送这类去向，降级成今日笔记，原因 `too_long`。
- Enter 送；`` ` `` 放进"等你"；15 s 没人理就自己关，**这一条留在"等你"**（和 docs/20 D3 开始框同样的时长，但不取消）。
- **锁着时**：字母照样能按 [源 d14:475]，卡片和"等你"要解锁后才看得到。锁着录的便条带 `lock:1`，永远不自动填（§3.2）。口袋里乱按（docs/20:670 的模糊测试）永远送不出 Mac。

### 4.4 离线和过期

- 设备上便条带着 `dest` id 落盘（`RecMeta.dest` → `.M`）；连上后 cpad 判定为 `queued`：现场类一律暂存，出现在 Mac 的等你送和设备的"等你"，**不会在同步时自动打开任何 app** [源 d14:216]。
- 24 h 没处理记 `dest.expired{item, dest}`（d14 已有这两个字段 [源 d14:386]）。
  - **没项目的**：回到待归档当"新"的（docs/23 §2.1），行里写"建议：Grok"（从 `dest.expired.dest` 读），⌥X 一下就能再送。
  - **有项目的**：本来就算已归档，不回待归档；`cpa dest pending --expired` 和 `cpa audit` 里看得到，已归档的行上有一个"没送出：Grok（过期）"的小标签。
- 换了绑定也不会送错：记的是 id，不是字母 [源 d14:59-64]；id 已经删掉的降级成今日笔记，原因 `unknown`。

### 4.5 家和公司（docs/18）

- **设备**：去向缓存每个档案一份，`/cfg/p<n>/dests.json`，`dests.snapshot` 照 docs/18 §3.5 第 5 条采用或只缓存 [源 docs/18 §3.6]。确认卡和 `dest.send`、`dest.get` 只在当前档案 ∈ ok(H) 时出现和发出（docs/18 §3.5 第 4 条已经列了 `dest.send`）。"等你"里的每行带 `src`，换档案就清掉。
- **Mac**：每个去向带 `profiles:[…]`。`profile.json` 的 `served` 多于一项时必填；`cpa dest add` 默认取唯一的那个，多于一个就问你。前置检查 `other_profile`：条目的 `prof` 不在去向的 `profiles` 里就降级成今日笔记；`cpa filing send`、Mac app 用同一条检查。所有 `dest.*` 审计行带 `prof`。
- **测试**：docs/18 §12 的金丝雀测试加一条：家里的金丝雀字符串永远不出现在公司去向的链接、剪贴板写入或 `cmd` 的 stdin 里。

### 4.6 固件要加什么（F5）

- `/cfg/p<n>/dests.json` 缓存；`RecMemo` 和 `RecLatch` 两层都绑可绑定字母和 `N`；`RecMeta.dest`、`RecMeta.lock` 写进 `.M`。
- 胶囊、字母行（≤5 个）、#53 图例；"去哪"页；"等你"里的待送行；确认卡（只在主页、500 ms 防误按、全文分页）；`dest.send`、`dest.get`。
- **不认识的种类照样显示**（d14 §10.4 原来是隐藏 [源 d14:473]），行为只看 `fl`，不认识的字母忽略。`fl` 缺了或读不懂就按 `lck` 处理，显示"云"（宁可多确认）。这样以后加种类不用发固件。
- 三个布尔改成一串 `fl`，12 行最坏情况的行长要重算（今天 1843 B [源 d14:350]，`fl` 会让它变短）。

## 5. Mac 端

### 5.1 路由：复活 `feat/destinations`，不重写

- `git merge-tree origin/main feat/destinations` 只有 5 处冲突，全是一行的登记（`cli/main.py`、`protocol/specs.py`、两个测试、`protocol/README.md`）；29 个新文件干净 [测 `git merge-tree`，只写临时对象]。
- 做法：从 origin/main 开新分支，cherry-pick `b5ad361`，解冲突，先绿；再一个个小提交（先写失败的测试）：删 `plugin` → 按 `fl` 判断 → 名字 ≤12 → 每个自定义去向都有 `leaves_mac`、`profiles` → `origin:"mac"` → 新的前置检查 → `url`、`app` → 预设。
- 不重写：96 格真值表是测试的标准答案，评审修过的 11 处也在里面。
- 固件只动编解码表：`dests.snapshot` 是第 31 个消息，`test_codec_vectors` 断言向量数等于 `kMsgTypeCount`（今天 30）[源 firmware/app/test/core/test_codec_vectors/test_main.cpp:55；firmware/app/lib/protocol/messages.h:52]。R1 是"Mac + 固件的编解码表"，**不发固件**。

### 5.2 `decide()`

**输入多一种来源**：`origin` 原来是 `live`、`queued`、`confirmed`，加 **`mac`**：有人在这台 Mac 上确认了这一条（`cpa filing send`、`cpa dest send`，`by` 是 `cli` 或 `app`）。只有这两条命令会设它，同步、重放、重启从不设。（不叫 `here`：docs/23 的 `env.link:"here"` 已经是"录音时连着这台 Mac"。）

**语音的 `live`**（D5，原来语音一律 `queued`）：只有满足 §3.2 那几条时，路由才把一段便条判成 `live`；`decide()` 本身仍是纯函数，不读时钟、不读 Mac 的锁屏状态。

**前置检查**（按顺序，第一条不满足的就是原因，降级成今日笔记，那一行已经写了）：

| # | 条件 | `reason` | |
|---|---|---|---|
| 0 | 会议只许去 `daily`、`todo`、`leaves_mac:false` 的 `file` | `meeting` | 新 |
| 1 | 目录里没有这个 id | `unknown` | |
| 2 | 这个版本没有执行器（R5 前的 `paste`、`paste.<名>`，R4 前的 `cmd`）| `unavailable` | 改名单 |
| 3 | 关着 | `disabled` | |
| 4 | 条目的 `prof` 不在去向的 `profiles` 里 | `other_profile` | 新 |
| 5 | 会离开 Mac，但没有 `optin` | `no_optin` | |
| 6 | `how` 不是 `key` / `pick`，而 `fl` 有 `l`、`k` 或 `c` | `not_explicit` | 改成看 `fl` |
| 7 | 没有文字：只认 `memo.text` 或打的字，**从不拿拼起来的转写** | `no_text` | |
| 8 | 要项目却没有项目 | `no_project` | |
| 9 | `fl` 有 `k`，而语音超过 40 s 或正文超过 500 B | `too_long` | 新 |

路由层还有 d14 的 `bad_text`（排队正文清洗后不能用）。

**真值表**（前置检查都通过之后）。记号：**D** 投递；**H✓** 暂存，等你在设备或 Mac 上确认；**H⏚** 暂存，等设备连上；**Q** 排队 `intent=todo`；**S** 排队 `intent=spawn`；**v** 加 `voice:true`（docs/22 §5.4 第 8 条：不再用 `uncertain`）；**E** 回 `E_NEEDS_DEVICE`，这条仍是 H✓；**D\*** 要设备取过全文（sha8）。

| 种类 | 打·现·在 | 打·现·离 | 打·排·在 | 打·排·离 | 语·现·在 | 语·现·离 | 语·排·在 | 语·排·离 | 确·在 | 确·离 | Mac·在 | Mac·离 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 归档：`daily` `todo` `file` | D | D | D | D | D | D | D | D | D | D | D | D |
| `queue` | Q | Q | Q | Q | Qv | Qv | Qv | Qv | Q / Qv | Q / Qv | Q / Qv | Q / Qv |
| `session` | S | S | S | S | Sv | Sv | Sv | Sv | S / Sv | S / Sv | S / Sv | S / Sv |
| `clip` | **D** | H⏚ | H✓ | H⏚ | H✓ | H⏚ | H✓ | H⏚ | **D** | H⏚ | **D** | **D** |
| 只填草稿：`url`(draft)、`app.<名>`、`paste.<名>` | **D** | H⏚ | H✓ | H⏚ | **D** | H⏚ | H✓ | H⏚ | **D** | H⏚ | **D** | **D** |
| 执行：`cmd`、`url`(act)、裸 `paste` | H✓ | H⏚ | H✓ | H⏚ | H✓ | H⏚ | H✓ | H⏚ | **D\*** | H⏚ | E | E |

- 列：打 = 打字，语 = 语音；现 = `live`，排 = `queued`；确 = 设备上确认过；在 / 离 = 设备链路 Ready 与否。"打·现·离""语·现·离"不会出现（`live` 要求链路在），照样有定义。测试全覆盖：10 类行（`daily`、`todo`、`file`、`queue`、`session`、`clip`、`url` draft、`app`、`url` act、`cmd`）× 12 列，再乘会议与否、档案对不对（前置检查 0、4）。
- `Mac·离` 也能送：这就是 R1 不插设备也能从 Mac 送进 Grok 的原因。`session` 从 Mac 来也只排队（和 docs/23 §3.4 一样），开 session 仍在设备看板上按 Enter（ADR 0002）。
- `clip` 的"语·现·在"仍是 H✓：桌前自动做的只有只填草稿类（D5），自动往剪贴板写语音没有好处。
- **不变式 2 改成**：现场类只在这几种情况下是 D：设备上确认且在线；打字的 `live`（`clip`、只填草稿类）；语音的 `live`，只对验证过的只填草稿类；`origin:"mac"`。同步、重启、重试永远不打开、不复制、不粘贴。执行类只认设备上翻完全文的确认。
- 投递时的两种新失败：加了 `url` 去向以后 app 被删了（LaunchServices 回 `-10814` [测]）记 `dest.failed{code:"E_DEST_MISSING"}`；处理这个 scheme 的 app 换了或签名的 Team ID 变了记 `dest.failed{code:"E_HANDLER_CHANGED"}`。两种都什么都不打开、不重试。

### 5.3 适配器

| 适配器 | 怎么做 | 依据 |
|---|---|---|
| `UrlAdapter` | 1）加头（§2.3），把所有 C0、DEL、C1 控制字符（含 `\n`、`\t`）换成空格，去掉 Cf 类和双向控制符，空了就失败；2）按 UTF-16 码元数查 `max_text_utf16`，编码（`quote(…, safe="")`，`+` 变 `%2B`，Cursor 会把 `+` 当空格 [测]）后查 `max_url`，超了走 `overflow`；3）查草稿占用（§3.6）；4）用 `LSCopyApplicationURLsForBundleIdentifier` 找到加的时候记下的那个 app，用 Security 框架（ctypes，`SecStaticCodeCreateWithPath` → `kSecCodeInfoTeamIdentifier`）核对签名 Team ID，再用 ctypes 调 `LSOpenFromURLSpec`、**指定 `appURL`** 打开，所以后装的 app 抢不走这个 scheme；**不跑 `open`**：参数里的正文同一用户的进程都看得到，链接太长还会"argument list too long" [测]；不要 PyObjC，不要任何权限；5）记 `dest.handed{via:"url"}` | 调研 B §1(2)；安全评审 |
| `ClipWriter` | ctypes 调 `NSPasteboard`：`prepareForNewContentsWithOptions:` 带"只在本机"（`NSPasteboardContentsCurrentHostOnly`，不经通用剪贴板同步到 iPhone）[研][待]，加 `org.nspasteboard.ConcealedType` 和 `TransientType` 标记（剪贴板历史工具不记）；2 分钟后剪贴板的 `changeCount` 没变就清掉。ctypes 走不通时退回 `pbcopy`（正文走 stdin），这时要你按一下确认才写，提示"剪贴板历史可能会记" | 用户、安全评审 |
| `AppAdapter`（`app.<名>`、溢出、草稿占用）| `ClipWriter` 写剪贴板，加头；再用预设的 `focus` 链接（Grok：`grokbot://app/v1/agent?id={p}`）或只按 bundle 把 app 调到前面（`LSOpenFromURLSpec`，只带 app 的位置）；从不按 ⌘V；记 `dest.handed{via:"clip"}` | 用户评审 |
| `ClipAdapter` | 不变，换用 `ClipWriter` | d14 adapters |
| `FileAdapter` | 不变（`O_APPEND`、`O_NOFOLLOW`，一行一次 write [测 8 进程 × 2000 行无撕裂]）；R4 的 `drop` 模式写临时文件、fsync、`link()` 成 `<ref>.md`，同一条再写是空操作 | 调研 B §1(5) |
| `CmdAdapter`（R4，单独的 ADR）| 见下 | 调研 B §1(4)；安全评审 |

**`CmdAdapter` 的规矩（R4）**：只走 stdin 不够，接收的程序可能把 stdin 当代码跑。
- `cpa dest add` 拒绝这些 `argv[0]`：shell（`/bin/sh`、`bash`、`zsh`）、解释器（`osascript`、`python3`、`node`、`ruby`、`perl`）、拼参数的（`xargs`、`env`）、agent 命令行（`claude`、`codex`、`grok`、`cursor-agent`）。表是数据（`presets/v1/cmd-deny.json`），所以代码里仍然没有 app 名字。agent 走 `queue`、`session` 或 `url`。
- `argv[0]` 是你自己的普通文件，它和它所在的目录组和其他人都不能写；加的时候记下 sha256、大小、mtime，送的时候变了就失败（`E_CMD_CHANGED`），要重新加。参数里不许有 `{…}` 占位。
- `shell=False`、`close_fds=True`、`start_new_session=True`；环境变量只有固定的 `LANG`、`PATH` 和 `CPA_DEST`、`CPA_REF`、`CPA_PROJECT`、`CPA_SOURCE`；`cwd=/`；10 s 超时就 `killpg`；退出码 0 = 送到；输出丢弃，stderr 只记长度 [测：敌意正文原样穿过，超时后没有残留进程]。
- 快捷指令：预设存快捷指令的 identifier（`shortcuts list --show-identifiers`），不存名字（名字会改、会被别的快捷指令用上）；设备卡上显示名字。`shortcuts run` 收 `-i <路径>`，不收 stdin [测 `shortcuts run --help`]：先试 `-i /dev/stdin`，不行就写一个 0600 的临时文件放 `$TMPDIR`，跑完删掉。ADR 写明：快捷指令里对输入做"运行 Shell 脚本"或"运行 AppleScript"，就等于执行代码。
- 子进程继承 cpad 的隐私权限（TCC）。文档写明，并劝你别给 cpad 的 python 开"完全磁盘访问"。

### 5.4 从 Mac 送：等你送

- `cpa dest pending --json` 一次给齐 Mac 列表和 triage 要的行，每行 `why` 是：
  - `held`：暂存的（等确认、等设备）；
  - `usual`：项目有常用去向、还没判过去向的（§3.4）；
  - `expired`：过期的，只在 `--expired` 时给。
- **docs/23 §2.1 第 3 条改成**：有效去向不含暂存中的（`await_confirm` / `await_device`）。暂存的行不管有没有项目都进"等你送"一组；没项目的、过期或丢掉以后回到"新"。
- 送：`cpa dest send REF [DEST]` 或 `cpa filing send ID DEST`，都用 `origin:"mac"`、`how:"pick"`，经 `decide()`；执行类回 `E_NEEDS_DEVICE`。丢：`cpa dest drop REF`（`dest.dropped`，今日笔记那一行还在）。
- `cpa filing send` 对会议去今日笔记、待办、本机文件以外的地方回 `E_BAD_ARG`（`field:"id"`），和 `hand` 一样。
- 正文从不在参数里：`send` 只收 id，cpad 自己取正文。

### 5.5 命令行（公开契约，Mac app 和你的脚本都用它）

| 命令 | 做什么 |
|---|---|
| `cpa dest ls [--json]` | 列去向：字母、种类、云 / 本机、`effect`、验证没、档案、最近一次送出、有没有新版预设 |
| `cpa dest presets [--installed\|--apps] [--json]` | 列预设；`--installed` 只列装了的（`LSCopyApplicationURLsForBundleIdentifier`，`/Applications` 和 `~/Applications` 都找得到，版本读 `CFBundleShortVersionString`）；`--apps` 列这台 Mac 上每个 app 的名字和 `CFBundleIdentifier`，给"其他 app…"用；带每个预设的确认文案，app 不自己写字 |
| `cpa dest add` | `--preset P \| --app BUNDLE \| --preset-file PATH`，`--key K`，`[--map 项目=值或链接]`、`[--profile X]`、`[--local]`、`[--yes]`；离开 Mac 的不带 `--yes` 就问 `y` |
| `cpa dest test ID`、`cpa dest verify ID --draft\|--act` | 经 cpad（LaunchAgent）送一条带随机记号的测试；按你的回答记 `dest.changed{verified}` |
| `cpa dest rm ID` | 删 |
| `cpa dest pending [--expired] [--json]`、`cpa dest send REF [DEST]`、`cpa dest drop REF` | §5.4 |
| `cpa filing send ID DEST` | 把待归档里的一条送到任意去向（docs/23 P1-G），`origin:"mac"` |
| `cpa filing triage` | 先过等你送：↩ 送到选好的，`g` 再按字母换去向，`n` 跳过 |
| `cpa audit --leaves-mac` | 什么离开过这台 Mac |

### 5.6 审计

- 照 docs/14：`dest.decided / delivered / failed / dropped / expired / changed`，只记长度和哈希前缀，不记正文 [源 d14:378-400]。
- 加 `dest.handed{item, dest, kind, via:"url"|"clip", by, bytes, sha8, leaves_mac, prof}`：交给了别的 app，但不知道它收没收。`fold_pending` 把它和 `delivered` 一样当成结束。`cpa audit --leaves-mac` 两种都列。
- 所有 `dest.*` 带 `prof`；`dest.changed` 记 `verified`、`handler{bundle, team}` 的变化。

### 5.7 Mac app（d24）

- **集成页第 6 张卡"去向"**：上半是已加的去向（字母、种类、云 / 本机、验证没、最近一次送出、项目的常用去向）；下半是"这台 Mac 上能接的"（`cpa dest presets --installed --json`），每个一个**加上**和一个**送一条测试**按钮，测试后弹那两个问题；最后一行"**其他 app…**"：列出 `cpa dest presets --apps --json`，选一个 app、选一个字母、选"会不会送到云端"，就加成 `app.<名>`。背后全是 `cpa dest …`，app 里没有任何 app 专用分支。
- **待归档页**：
  - 顶上"等你送"一组（`cpa dest pending --json`）；
  - ⌘↩ 送到选好的去向，⌥+字母送到那个字母的去向，⌥D 不送了；Enter 仍是打开详情（docs/23 §5.2）；
  - 会议行没有"送到…"；
  - Mac 上确认**只对不含 `k` 的**算（D14）；
  - 5 秒提示"已填进 Grok · 到 Grok 里按回车发出"或"在剪贴板 · 到 Grok 按 ⌘V"。
- `cpa dest add` 由 app 跑时 stdin 是关的（d24 §5.2），所以 app 先弹自己的确认框，再带 `--yes`。
- 粘贴执行器（R5）：`cpa dest exec --kinds paste --json` 长连，app 收到 `{key, bundle, text}` 后把 app 调到前面、确认它在前台、⌘V、恢复剪贴板、**从不按回车**。要辅助功能权限，所以是设置里的一个开关、默认关，这要修订 d24 §1.2"写只有你点的 cpa 命令"。

### 5.8 本机 AI 来读（R3，可选，约 200 行）

- 不做信箱（§2.5）。Claude Code、Cursor、Codex、Claude Desktop 和 `grok` 命令行今天就能经 `cpa mcp serve` 读：docs/15 的只读工具和 docs/23 的 `list_unfiled`、`get_filing_item`。
- R3 只做三件小事：
  - `cpa filing ls` 和 `list_unfiled` 加 `--dest` / `dest` 筛选，"我送去 Grok 的那几条"能一下找到；
  - `cpa mcp setup generic` 打印一段通用的 stdio JSON，任何本机 MCP 客户端都能贴；
  - 把 `cpad/mcp/cli.py` 写死的 4 个客户端搬进数据（每个一份：`format` 是 `json-mcpServers`、`toml-mcp_servers` 或 `cli` 三种之一，加 `where`、`stdio_type`），三个通用的渲染函数；`cpa mcp setup --list --json` 给 Mac app 生成标签页。这样"代码里没有 app"的那个已知例外就删掉了。

## 6. 安全和隐私

| 规则 | 怎么做到 | 依据 |
|---|---|---|
| 正文不进任何进程的参数 | 链接经 `LSOpenFromURLSpec`；`cmd`、`pbcopy` 走 stdin；调 app 只带 bundle 或 app 位置 | [测]；d14:494 |
| 只填草稿，从不发送 | `url` 只开预填链接，要验证过才算只填草稿；`app` 和执行器从不按回车；Grok 的预填链接从不发送 | [源 GB]；docs/13 §9.5 |
| 链接不会被别的 app 截走 | 加的时候记下处理 app 的 bundle id 和 Team ID；送的时候指定这个 app 打开，对不上就 `E_HANDLER_CHANGED` | 安全评审 |
| 链接只能是草稿入口 | 模板只许查询值里放正文；scheme 必须是那个 app 登记过的；拒绝表（§2.3）；你写的一律当执行类 | 安全、契约、可行性评审 |
| 只在你选的时候离开 Mac | 每个自定义去向都有 `leaves_mac`，默认是；加的时候要打 `y` 或在 app 里确认；设备上标"云"；`cpa audit --leaves-mac` | d14:131；§5.6 |
| 语音不自动执行 | 执行类每条设备上翻完全文按 Enter（sha8 核对，40 s / 500 B 上限）；桌前自动填的只有验证过的只填草稿类；同步、重启从不打开 app | d14:216；docs/22 §5.4；ADR 0002 |
| 抢焦点不会替你发出 | 自动填草稿要 Mac 键盘 3 s 没人按；头的第一个字是 `[` | §3.2、§2.3 |
| 口袋误按送不出去 | 锁着能按字母，但锁着录的不自动填，确认卡和"等你"要解锁后才有 | d14:475；docs/20:670 |
| 话里的"发到哪"不算 | 去向只来自你按的键，不看正文 | d14 不变式 3 |
| 会议只进本机 | 前置检查 0：只去今日笔记、待办、本机文件；字母在会议里不生效；`cpa filing send` 拒；Mac 上没有"送到…"；交给别的 app 的正文只取 `memo.text`，从不拼转写 | docs/23 §3.4 |
| 家和公司不混 | 去向带 `profiles`，前置检查 `other_profile`；设备每个档案一份缓存；金丝雀测试 | docs/18 §3.5–§3.6、§12 |
| 剪贴板不扩散 | 溢出和草稿占用只在剪贴板去向开着时才用剪贴板（`app.<名>` 是你加的时候就选了剪贴板）；只在本机、不进历史的标记；2 分钟后清；做不到就要你按一下 | §5.3 |
| 不存第三方密钥 | `destinations.json` 没有给密钥的字段；字面量参数和 `map` 拒收像密钥的值；webhook 和 API 的 key 在钥匙串，由你的脚本读 | docs/13 §9.5；§2.3 |
| 不注入按键 | cpad 不跑 AppleScript、不要辅助功能；粘贴只在 Mac app 里、默认关 | docs/13 §9.6；[测] |
| `cmd` 只能你自己加，也拦不住所有事 | 只有 `cpa dest add` + `y` 能建；拒绝表；文件钉住；子进程继承 cpad 的隐私权限 | §5.3 |
| Mac 上的确认挡不住本机程序 | Mac 上点一下、管道进 `cpa dest add` 的 `y`，本机任何有 shell 的程序都能代做；但它本来就能做你能做的一切（直接打开链接）。所以 Mac 上的确认只用于只填草稿类，执行类只认设备 | docs/23 §3.4 |
| 不认识的种类按最严的来 | `fl` 缺了或读不懂当 `lck` | §4.6 |
| 转写是不可信数据 | 清掉控制字符和 Cf 类字符；链接编码；预览过 `display_safe()`；带"可能有错字或别人的话"的头 | docs/23 §3.3 |

## 7. 分期和工作包

一律先写失败的测试（RED → GREEN → REFACTOR）。R1 和 R3 只动 Mac，不用设备就能全测；R2 的固件和 daemon 可以在各自的 worktree 里并行，真机测试（HIL）排在最后单独做。

### 7.1 工作包

| 包 | 内容 | 代码 / 测试（行）[算] | 依赖 |
|---|---|---|---|
| **R0 文档** | 本文；§7.2 的修订 | — | — |
| **R1-0 手测（你，2 分钟）** | §8.2 第 2 条的 4 条链接。结果定 Grok 预设的 `effect` 和"没给 Bot 时能不能用" | — | — |
| **合并 `feat/filing-p1`** | docs/23 P1（7 个提交，和 main 有 9 处冲突）| 已写好 | — |
| **R1-A 复活 D5-A** | cherry-pick `b5ad361`，解 5 处冲突；固件编解码表登记第 31 个消息（不发固件）| 50 / 50（加上原有 5,580）| — |
| **R1-B 种类 v2** | 删 `plugin`；按 `fl` 判断；`url`、`app` 的规格；名字 ≤12；`leaves_mac`、`profiles`；`origin:"mac"`；前置检查 0、4、9；12 列真值表 | 300 / 550 | R1-A、R1-0 |
| **R1-C 链接、剪贴板、预设** | `UrlAdapter`（清洗、头、UTF-16 长度、模板解析、拒绝表、钉住处理 app）、`ClipWriter`、`AppAdapter`、草稿占用；`presets/v1`（Grok、Claude、Cursor、Codex、Obsidian）和 Grok 的测试向量；`protocol/dest-preset/v1`；你的预设目录；`cpa dest presets/add/test/verify`；"代码里没有 app"的测试。验收：用真的 cpad LaunchAgent 送一条测试 | 550 / 700 | R1-B |
| **R1-D 从 Mac 送** | `cpa filing send`、`cpa dest pending/send/drop`；等你送（暂存、常用去向、过期）；docs/23 §2.1 第 3 条修订；triage 的 ↩、`g`+字母 | 200 / 300 | R1-C；`feat/filing-p1` |
| **R1-E Mac app** | 第 6 张卡、"其他 app…"、等你送一组、⌘↩、⌥字母 | 400 / 300（Swift）| R1-D；d24 v1 |
| **R2-A 接线** | D5-I：换掉 `_downgraded` [源 cpad/hub/reduce.py:139-142]；`app.py` 里载入、重载、过期；D5-B 的 `pending.snapshot`、`dest.send`；语音 `live`（Mac 锁屏、键盘空闲）；`dest.get` / `dest.text`；档案的闸 | 650 / 850 | R1-B |
| **R2-B 固件 F5** | §4.6 全部 | 1,000 / 800 | `feat/ux-decisions`、`fix/memo-hold` 合并 |
| **R2-C 真机** | HIL H5 加"说话送到 Grok"：桌前自动填、揣兜回来、锁屏、超长、同一个 Bot 连着两条、别的页面开着时出字、换档案 | — | R2-A、R2-B |
| **R3 本机 AI 来读（可选）** | `--dest` 筛选；`cpa mcp setup generic`、`--list --json`；客户端搬进数据 | 200 / 250 | R1-B |
| **R4 命令** | `CmdAdapter` 和单独的 ADR；快捷指令预设（identifier）；`file` 的 `drop` 模式；不带 webhook | 400 / 550 | R1-B |
| **R5 粘贴执行器** | `protocol/dest/v1`、`cpa dest exec`、Mac app 的辅助功能粘贴 | 250 + 400 Swift / 400 | d24 v1；修订 d24 §1.2 |

**最快让你对 Grok 说话的路**：手测 → 合并 `feat/filing-p1` → R1-A → R1-B → R1-C → R1-D。只动 Mac（加编解码表），约 1,100 行代码、1,600 行测试 [算]。装上以后：设备上照今天这样按住 G0 说、按 `3`；Mac 上终端 `cpa filing triage` 里 ↩（项目 3 常用去向是 Grok），或 `g`、`x`；Grok 就开好了草稿。R1-E 有了以后在 Mac app 里 ⌘↩。R2 合并后，`X` 能在说话时按，桌前自动填。

```
手测 ─┐
filing-p1 ─┐
R1-A ─► R1-B ─► R1-C ─► R1-D ─► R1-E（还要 d24 v1）
           ├──► R2-A ─┐
           ├──► R3     ├─► R2-C 真机
           └──► R4    R2-B（等 ux-decisions、memo-hold）─┘
d24 v1 ─► R5
```

### 7.2 要跟着改的文档（随改代码的那个 PR 一起合）

| 文档 | 修订 | 随哪个包 |
|---|---|---|
| docs/14 全文 | 带日期的横幅：`plugin` 删掉（连同原因 `cloud_optin`、前置检查 2 的 `unavailable` 名单、§1.2 的 WhisperWoof v0.7 执行器 → R5、§4.2 的 `plugin` 字段 → `url` / `app` / `cmd` 的选项、§9 的 `add plugin.*` → `add --preset`）；种类 v2 和 `fl`（§7.2 的 `live/cloud/proj` → `fl`）；名字 ≤12；§8.2、§11.1 的"离开 Mac"清单加 `url`、`app`；§10.4 改成"不认识的种类照样显示，`fl` 缺了当 `lck`"；§5.2 加前置检查 0、4、9；§5.3 换成本文 12 列的表；§5.4 不变式 2 换成 §5.2 的新写法（`origin:"mac"`、语音 `live`）；`uncertain` → `voice`；新事件 `dest.handed`；§7.6 的 1843 B 重算 | R1-A / R1-B |
| docs/13 §9.5 | "交给 WhisperWoof"→"任何执行器"；链接由 cpad 打开；Mac 上点一下对只填草稿类算确认（D14）| R1-D |
| docs/23 | §2.1 第 3 条：暂存不算有效去向，进"等你送"；§3.1、§3.4：`cpa filing send` 用 `origin:"mac"`，会议只去本机；第 228 行和 §6.1 第 392 行"不含云端插件"→"任何你开启过、标了云的去向"；§5.2、§3.6：⌘↩、⌥字母、triage 的 `g`；§3.5：`list_unfiled` 加 `dest` 筛选（R3）| R1-D / R3 |
| docs/22 | §3.1 第 141 行：只填草稿类（`url`、`app`）也收片段，执行类照旧存成独立便条；§5.9"片段只会出现在……四种去向"和"音频和文字不离开 Mac 成立"、§8 第 4 条：改成"文字只经你为这一条选的、标了云的去向离开 Mac" | R2-A |
| docs/18 | §7.1 加一行：去向带 `profiles`，`served` 多于一项时必填；§12 金丝雀测试加去向的链接、剪贴板、stdin | R1-B |
| docs/15 | §4 各客户端怎么接：`cpa mcp setup generic` 和数据化的客户端 | R3 |
| d24 | §2.4 第 6 张卡和"其他 app…"；第 1 张卡的标签从 `cpa mcp setup --list --json` 来（R3）；§2.1 等你送、⌘↩、⌥字母；§1.2 执行器开关（R5 才改）| R1-E / R3 / R5 |

### 7.3 实现记录（2026-10-22，R1-B、R1-C）

和上文不完全一样、或者上文没写死的地方，以这里为准：

- **链接怎么打开**：cpad 进程里用 ctypes 调 `LSOpenFromURLSpec`（`daemon/src/cpad/dest/macos.py`），`appURL` 指定加的时候认准的那个 app；正文只在本进程内存里，不进任何进程的参数，没有子进程，不要 PyObjC、不要任何权限。打开前用 `LSCopyApplicationURLsForBundleIdentifier` 找 app，用 `SecStaticCode`（不校验资源）核对签名和 Team ID，再读 `Info.plist` 确认 scheme 还登记着；对不上就 `E_HANDLER_CHANGED`，app 没了就 `E_DEST_MISSING`，什么都不开。苹果自带的 app 没有 Team ID，记成 `null`。
- **剪贴板**：同样走 ctypes 调 `NSPasteboard`（`pasteboard.py`）：`prepareForNewContentsWithOptions:` 带"只在本机"，正文加 `ConcealedType`、`TransientType` 两个标记，2 分钟后 `changeCount` 没变才清。退回 `pbcopy`（正文走 stdin）只在有人刚按了的时候（`by` 不是 `auto`），否则 `E_CLIP_PLAIN`。**原生写入没有对真的剪贴板跑过**（测试一律不碰真的剪贴板），只验证了这些方法存在、读 `changeCount` 可用；要在 R1-C 的验收里手测一次。
- **`effect`**：随 cpad 发的 `url` 预设都**不写** `effect`（= 不知道），所以加上以后是 `lck`，每条要在设备上确认；`cpa dest test ID --send` 送一条带随机记号的"测试 NNNN"，你答"出现了、没自动发"后跑 `cpa dest verify ID --draft`，这个去向才变成只填草稿（`lc`）。`verify --draft` 要求这次加上以后送过测试（看 `events.jsonl` 里的 `dest.handed`，`item` 以 `test-` 开头）。预设写了 `act` 的、你自己的预设（`$CPA_HOME/presets/*.json` 或 `--preset-file`），永远是 `act`。§2.3 示例里 Grok 预设写的 `"effect": "draft"`，在你做完 §8.2 第 2 条的手测以前不写进随 cpad 发的文件。
- **测试从哪里送**：`cpa dest test ID --send` 现在由 `cpa` 进程本身打开链接（同一个适配器、同一套钉住检查），还没有经过 cpad LaunchAgent；"从 LaunchAgent 能不能把 Grok 的窗口带到前面"仍是 R1-C 验收的 [待]。
- **头**：`[Cardputer 语音·可能有错字或别人的话·#项目] 正文`，打字是 `[Cardputer 打字·#项目]`；控制字符换空格，Cf 类字符去掉；长度按 UTF-16 码元。`presets/v1/grok-bot.vectors.json` 里每条链接都过 Grok 自己的检查（可打印 ASCII、没有 `#` 和 `\`、≤2048、解码后没有控制字符、空参数去掉）；约 200 个汉字放得下，约 220 个就溢出到剪贴板。
- **随 cpad 发的预设**：`grok-bot`（`url.grok`，字母 `x`）、`cursor`（`u`）、`claude`（`k`）、`codex`（`b`，ChatGPT.app 里的 Codex）、`obsidian`（`file.ob`，`o`，要 `--root 库`）。Cursor、Claude、Codex 的长度上限是按 §2.4 估的 [待]。
- **契约**：`protocol/dest-preset/v1/`（schema、对和错的向量、README）；`dests.snapshot` 的行带 `fl`（README 第 24 条）。
- **还没做**（R1-D 起）：`cpa filing send`、`cpa dest pending/send/drop`、常用去向（`usual`）、等你送；接进 hub（R2-A）。今天 `origin:"mac"` 只在 `decide()` 和真值表里，还没有命令会设它。（2026-10-22）R1-D 做了前四样，见 §7.4；接进 hub 仍是 R2-A。

### 7.4 实现记录（2026-10-22，R1-D：从 Mac 送）

- **命令**（公开契约 `protocol/dest-cli/v1/`：`pending`、`sent`、`dropped`、`usual`、`tested`、`error` 六份文档，向量和 `check_dest_cli.py`，CI 里跑）：
  - `cpa dest pending [--expired] [--json]`：等你送。`why` 是 `held`（暂存或送失败的，`st` 是 `hold` / `failed`，失败的带 `code`）、`usual`（§3.4 的建议）、`expired`（只在 `--expired` 时，最近 7 天）。暂存的在前，各组里旧的在前。
  - `cpa dest send REF [DEST] [--by cli|app] [--json]`：没给 DEST 时送到预选的（暂存的原去向，或项目的常用去向）。
  - `cpa filing send ID DEST [--by cli|app] [--json]`：待归档里的任何一条便条。
  - `cpa dest drop REF [--by …] [--json]`：`dest.dropped`；对"建议"的行也能用，之后不再建议。
  - `cpa dest usual [PROJECT [DEST|-]] [--json]`：项目的常用去向，存进 `destinations.json` 的 `usual`；只能是 Mac 上有执行器的去向（`clip`、`file.*`、`url.*`、`app.*`），项目要是这台 Mac 登记过的。
  - `cpa dest test ID --send [--json]`：终端里送完接着问"出现了吗？""自动发出去了吗？"，按回答跑 `verify --draft` 或 `--act`；没出现、而预设能选 Bot 时，让你贴 Bot 的链接或 id，存成 `map["*"]` 再测一次。`--json` 什么都不问（Mac app 自己问），打印 `tested`。
  - `cpa dest map ID PROJECT=值或链接… | PROJECT=-`：给 `url.*` 的每个项目指定 Bot。
  - `cpa filing triage`：先过等你送（↩ 送、`g` 再按字母换去向、`d` 不送、`n` 跳过），再过待归档；待归档的便条上也能 `g` 加字母送出去。
- **送的规矩**：正文只取 `memo.text`（打字的取 `item.received` 的正文），从不在 argv；先跑 `decide(…, origin="mac")`，被拒什么都不写（`E_NEEDS_DEVICE`、`E_NO_TEXT` 等）；锁里写开始投递的 `dest.decided`（同一条正在送时 `E_BUSY`），再交出去，再记 `dest.handed` / `delivered` / `failed`。不重试、不重放；失败的留在等你送。
- **和上文不完全一样的地方**：
  - `cpa filing send` 不收 `daily`、`todo`、`queue`、`session`（`E_BAD_ARG` `dest`）：交给 AI 仍走 `cpa filing hand`（docs/23 §3.4 已改）。
  - 可能自己做事的去向（`fl` 含 `k`，例如还没测过的 `url.grok`）从 Mac 送时**什么都不写**，直接回 `E_NEEDS_DEVICE` 并提示先测一次；不在等你送里留一条"等设备"，因为 R2 以前设备确认不了它，留下只会卡住。
  - 一个 Bot 30 分钟内只填一条（D10）跨进程也成立：`dest.handed{via:"url"}` 多记 `to`（Bot 值的 sha256 前 8 位，不记值本身），新的 `cpa` 进程从日志里读出来。测试那一条答完以后不再算（见下面"评审后的改动"）。
  - 交给别的 app 的字，除了控制字符，U+2028 / U+2029 也换成空格（还是一行）。
  - 剪贴板 2 分钟后清：`cpa` 进程活不到 2 分钟，所以交给一个分离的 `python -m cpad.dest.clip_clear <changeCount>`，参数里只有那个数，不读剪贴板，只在你之后没复制过东西时清。
  - 暂存的从 Mac 送失败后，`fold_pending` 给它一个不带正文的骨架，等你送照样列出它；再送时正文重新从 `rec.result` 读。
- **评审后的改动（2026-10-22）**：
  - 送的时候出错（ctypes、框架加载失败）记 `dest.failed{E_INTERRUPTED}`；被 ^C 或 SIGTERM（Mac app 30 秒超时）打断时先写 `dest.failed{E_INTERRUPTED, retryable:false}` 再退出；连这个都没来得及写的、从 Mac 送的 `running` 超过 60 秒，`pending.settled()` 当它失败列出来，能再送、能不送。原来这几种情况会一直卡在 `running`、从等你送里消失、再送都是 `E_BUSY`，直到 cpad 重启。
  - 同一条已经交给（或写进）这个去向的，再送回 `E_ALREADY`，加 `--again` 才再送（Mac app：再按一次）；读的时候还在等你送、锁里发现已被另一个 `cpa` 送掉或不送了的，回 `E_UNKNOWN_ID`，不再打开第二次。
  - cpad 的 `resume()` 不重试、不降级从 Mac 送的（`origin:"mac"`，或没有正文的骨架）：启动时还在 `running` 的记 `E_INTERRUPTED`，失败的留在等你送等人再送。
  - 认 app 更严：签名要满足 `anchor apple generic`（苹果签发的证书；ad hoc 和自签的都不算），没有 Team ID 的只认苹果自己的 app（`anchor apple`）；同一个 bundle id 有好几份时用第一份通过全部检查的；`deny.json` 的 `handlers` 对 `--app` 和每种预设都管（不分大小写）。
  - 一次性测试：只有经链接交出去的测试才让 `verify --draft` 生效；这个 Bot 30 分钟内被真的便条填过时测试不交（`tested` 的 `E_DRAFT_BUSY`）；答完（`verify`）以后测试那一条不再占着草稿，第一条真的便条照样走链接。终端里的问题改成中文，和 Mac app 一样。
  - `cpa dest ls --json` 的 url 行多一个 `takes_map`（预设能选 Bot），`cpa dest presets --json` 也有；文字输出的 url 行写现在的样子：`draft`（测过）、`untested`、`acts`。
  - 一条带单独代理码点（JSON 允许）的便条换成 U+FFFD 再交出去，不再在拼链接或 `pbcopy` 时出错；`clip_clear` 用 `python -I`、从 `/` 起，不会导入当前目录里的 `cpad/`。
- **还没验证的**（照旧 [待]，R1-C 验收时手测）：真的剪贴板写入、从 LaunchAgent 打开链接。今天 `cpa dest send` 和 `cpa dest test --send` 都在 `cpa` 进程里打开链接，还没经过 cpad。

### 7.5 实现记录（2026-10-22，R1-E：Mac app）

- 做法见 docs/24 §2.4 的"R1-E 做了的样子"。只经公开的 `cpa dest … --json` 和 `cpa filing send --json`；Swift 代码里没有任何 app 的名字（`RulesTests` 钉着），名字、字母、说明都来自 cpa。契约副本 `mac/Fixtures/dest-cli-v1`（`aae8694`），CardputerCoreTests 跑它的全部 31 个向量。
- 和 §5.7 不完全一样：测试的两个问题在卡里就地问，不弹窗（截图看得见、不挡别的）；"其他 app…"和换字母没做；`cpa dest map` 的链接是 argv 里唯一一个你贴的值（只许可打印 ASCII、没有空格、≤512，不是便条正文）。
- 设备上的 `X`（R2）、经 cpad 打开链接、真的剪贴板写入仍是 [待]。
- 评审后的改动（2026-10-22）：一次性测试按去向问（app 只问"贴出来了吗"；你自己的预设答"没发"也不记草稿；只有预设能选 Bot 时没出现才要链接，字样不写死"Bot"）；cpa dest 能回的每个码都有中文和下一步（daemon 的 `test_dest_codes_have_words.py` 钉着）；不再说"在设备上确认"这种 R1 做不到的话，灰的、只差测试的链接点一下就到 集成 → 去向 开始测；剪贴板那句一直留着到"知道了"，带"再复制"（`--again`）；`E_ALREADY` 时再按一次就再送；按数字归到有常用去向的项目后，选中它在等你送里的那一行（数字、⌘↩ 两下）；没项目的便条在"送到…"里写"送过去不带 #项目"；"加上"框里问 Bot 的链接（`cpa dest add --map`）。契约副本更新到 `8eddf6a`（33 个向量）。没做：待归档条目上"已送 Grok · 14:31"的标记（要新的数据来源）。

### 7.6 实现记录（2026-10-26：方向调整；项目是分组）

- **要求**（2026-10-26）：语音不宜直接交给 coding agent；先进自己的 app，再手工放到不同的 agent 里；电脑上可以建不同的项目，说话时在设备上按数字，把便条归到不同的项目里。第一步先做这些。
- **暂缓**：R2-A 接线里"说完直接送"的部分（桌前自动填草稿、设备上的 `X`、`dest.send`）和 R2-B 固件 F5、R2-C 真机都不排期。R1 不变：语音先进"待归档"，你在 Mac app 里选去向、⌘↩ 送；项目的常用去向（D21）照样只是预选。设备看板上从排队项开 session 的老路照旧（只对连了代码目录的项目，一律 plan，docs/22 §5.4），但语音片段不会自己开 session（docs/22 §5.4 的 2026-10-26 补记）。
- **项目是分组**（D24）：`cpa project add [--key K] [--slot N] [--folder 目录] -- 名字`；名字可以是中文（最多 24 个字母或 8 个汉字），key 从名字里的英文字母和数字来（≤12），没有就是 `p<号>`；不给号就用第一个空号。`rename`、`move`（和那个号上的项目对调）、`folder 目录|--none`、`rm`（录音和排队项保留 key）。都带 `--json`，打印 filing/v1 的 `projects` 文档（加了 `linked`、`folder`：目录路径的最后一段，从不给整个路径）或 `error`。老写法 `cpa project add KEY 路径 --slot N` 还能用。
- **只有连了目录的项目**才有 hook、按 cwd 认项目、设备上开 session；没连的设备开 session 回 `E_UNKNOWN_PROJECT`（不改固件和协议）。设备上的 1–9 照旧从 projects 快照来，没连的项目 `ok:true`。
- **Mac app**：新的"项目"页（docs/24 §2.6）：1–9 和设备一样，新建（第一个空号或空行的"放在这里"）、改名（或双击名字）、点编号选新号或拖 ≡ 到另一行换号（都是对调）、连目录…/断开目录、删除（第二次点才删）。"待归档"的 1–9 按钮和说明用这些名字。界面有中文和 English（docs/24 §2.7）。
- **评审后补（2026-10-26）**：删掉的项目的 key 不会再分给新项目——历史里（录音、归档、排队项、常用去向）还带着的 key，新建时自动避开（家务 `p1` 删了再建读书，读书拿 `p1-2`，不会继承家务的录音）；手写 `--key` 撞上历史要加 `--reuse` 才行（明确说"把这些录音交给它"）。`rm` 同时忘掉这个项目的常用去向（记一条 `dest.changed op:usual id:null`）。老写法（第二个位置参数）再带 `--key` 或 `--folder` 一律拒绝（field `argv`）；名字有空格要加引号。
- **后续（固件，未排期）**：没连目录的项目在设备上开 session 回 `E_UNKNOWN_PROJECT`，固件现在显示"项目不存在"（`runtime_outbox.cpp` 的 `failure_text` 只看 code，不显示 `why`），可项目明明在。下次动固件时把这个文案改成"项目没连目录"（或"不存在/没连目录"），协议不变。
- 老的 cpad 读到 `root:null` 的 projects.json 会整份拒绝（日志里有 `projects.json rejected`）：装新版后 `cpa service install` 重启一次。

## 8. 决定和要你拍板的事

### 8.1 决定

不回复就按默认做。

**你会感觉到的**

| # | 决定 | 默认（推荐）| 备选 | 为什么 | 见 |
|---|---|---|---|---|---|
| D1 | 怎么接"所有 app" | 通用种类 + 预设（纯数据）；任何 app 至少能走 `app.<名>`（剪贴板 + 调出来，你按 ⌘V），在列表里点一下就加上；有预填链接的 app 少按一下；测试保证代码里没有 app 的 bundle id 和 scheme | 每个 app 一个适配器 | 这是要求；加一个 app 不用写代码、不用等发版 | §2.1 |
| D3 | Grok Bot 的主路 | `url` 预填链接，只填草稿 | 本机 MCP；例程 webhook；粘贴 | MCP 在它的云端跑；webhook 也在云端；粘贴要辅助功能权限 | §3 |
| D4 | Grok 的字母 | 设备上 `X`；Mac app 里 ⌥X；终端里 `g` 再 `x` | `b c f j k o u y` 里任一个 | `G` 留给选择器；Mac 上的单字母已经被 `j k u x y` 占了 | §4.1 |
| D5 | 说完怎么送 | 设备连着、两边都没锁屏、Mac 键盘 3 s 没人按、去向验证过只填草稿：**直接填草稿**；否则进等你送，Mac 上 ⌘↩ 或设备上 Enter | 每条都在设备上按 Enter | Grok 不按回车从不发送，设备上的 Enter 是同一件事的第二次确认；每条省一下 | §3.2 👤 |
| D6 | 项目怎么带过去 | 同一行的头 `[Cardputer 语音·可能有错字或别人的话·#项目] 正文`；给项目指定了 Bot 就由 Bot 区分 | 只送正文 | Grok 拒收换行；`[` 开头碰不到 agent 输入框的特殊符号；接收的 agent 知道这是语音草稿 | §2.3 |
| D8 | 打字 Opt+X 后 Enter | 连着 Mac 时直接填草稿（只对验证过的只填草稿类）| 也要确认 | Enter 本身就是确认，目标 app 有名字 | §5.2 |
| D9 | 超长（约 1 分钟以上）| 全文进剪贴板（只在本机、不进历史、2 分钟清）+ 调出那个 Bot + 你按 ⌘V；剪贴板去向关着就留在等你送 | 截断 | 不丢字；你关了剪贴板就不偷偷用 | §2.3 |
| D10 | 同一个 Bot 只有一份草稿 | 30 分钟内再往同一个 Bot 填，就改走剪贴板并提示"Grok 里还有上一条" | 一条一条送，靠你记着 | Grok 会悄悄吞掉第二条；猜错只多按一下 ⌘V，不猜会丢字 | §3.6 |
| D18 | 会议 | 只进今日笔记、待办、本机文件；字母在会议里不生效 | 会议也能送 | 有别人的话 | §5.2 |
| D20 | 先做什么 | 手测 → 合并 `feat/filing-p1` → R1 只动 Mac（等你送、常用去向）→ R2 设备 | 先做固件 | R1 不等固件和真机，最快能用 | §7 |
| D21 | 项目的常用去向 | 做（R1-D）：项目 3 的便条在等你送里预选 Grok，⌘↩ 一下送；永远不自动送 | 不做，只靠 R2 的 `X` | 不用等固件；真正选的仍是你按的那一下 | §3.4 👤 |
| D23 | 语音直接送到 agent（2026-10-26） | **暂缓**：语音先进 Mac app 的"待归档"，你在 Mac 上手工送到不同的 agent（R1 的 ⌘↩、⌥字母）；R2 的设备直送不排期 | 照原计划做 R2 | 要求：先进自己的 app，再手工放 | §7.6 👤 |
| D24 | 项目是什么（2026-10-26） | Mac 上建的有名字的分组，设备上按 1–9 归组；代码目录可选，连了才有 hook 和开 session | 项目必须是一个代码目录 | 要求：电脑上建不同的项目，说话时按数字分组 | §7.6 |

**工程内部**

| # | 决定 | 默认（推荐）| 备选 | 为什么 | 见 |
|---|---|---|---|---|---|
| D2 | 种类 | 加 `url`、`app`；删 `plugin`；`cmd` 放 R4；`paste` 可以带名字；不做 `box` | 再加一个 `http` 种类 | `http` 要 cpad 存密钥；`app` 已经让每个 app 都有路 | §2.2 |
| D7 | 谁打开链接、怎么记 | cpad 经 `LSOpenFromURLSpec`，指定加的时候认准的 app（bundle + Team ID）；记 `dest.handed` | Mac app 用 NSWorkspace；记 `delivered` | 不等 Mac app，不要权限，正文不进参数；别的 app 抢不走 scheme；cpad 看不到 app 收没收 | §5.3、§5.6 2026-10-22 实现记录：R1 从 Mac 送时由那个 `cpa` 进程用同样的方式打开（同一个 Team ID 检查、正文不进参数）；cpad 打开等 R2 接进 hub（§7.4）。 |
| D11 | 信箱 `box` | 不做；R3 只给已有的只读工具加 `dest` 筛选、`cpa mcp setup generic`、客户端搬进数据 | 做信箱、`ack`、7 天过期 | 读的那一半已经有了；领走默认关；要你再说一句，不是"说完就到"；省约 600 行代码和测试 | §2.5、§5.8 |
| D12 | 链接会不会自己做事 | 预设字段 `effect`，缺了当 `act`；只有随 cpad 发、评审过模板的预设能写 `draft`，而且每台 Mac 上要测试后你答"没自动发"才生效；你写的一律 `act` | 一律当只填草稿 | 会不会只填草稿看目标 app，不看机制 | §2.3 |
| D13 | `cmd` | 做（R4），单独写 ADR；拒绝表、文件钉住、identifier、每条设备上翻完全文 | 不做 | 快捷指令和任何命令行从这一个口进；stdin 也可能被当代码跑 | §5.3 |
| D14 | Mac 上点一下算不算确认 | 对不含 `k` 的算（`origin:"mac"`，不插设备也能送）；执行类只认设备 | 都要回设备上按 | 只填草稿不执行任何东西；本机程序能冒充点击 | §5.2 |
| D15 | 设备上不认识的种类 | 照样显示，行为看 `fl`；`fl` 缺了当 `lck` | 隐藏（d14:473）| 以后加种类不用发固件；读不懂时宁可多确认 | §4.6 |
| D16 | 名字长度 | ≤12，对齐 main | docs/14 的 16 | main 的固件会拒收 | §2.2 |
| D17 | `feat/destinations` | cherry-pick 后小步改 | 重写 | 保住 96 格真值表和评审修正；5 处冲突、测试全过 | §5.1 |
| D19 | 不做的路 | §2.5 全部不做 | 先做 Claude Code channels | 都违背本机、可撤销或不注入的原则 | §2.5 |
| D22 | 家和公司 | 每个去向带 `profiles`；前置检查 `other_profile`；设备每个档案一份缓存；`dest.send` 只在当前档案 ∈ ok(H) 时发 | 一份去向表给两个档案 | 一台 Mac 服务两个档案时，一个档案的话会进另一个档案的 Grok | §4.5 |

### 8.2 要你拍板

1. **Grok 里是一个 Bot 收所有话，还是每个项目一个 Bot？** 👤 默认一个，靠头里的 `#项目` 区分。每个项目一个的话，§3.1 第 4 步给项目填 Bot：在 Grok 里复制那个 Bot 的链接，贴进来就行。
2. **请手动试 4 条链接**（各复制一行到终端，回车；我们没往任何 app 发过东西）。👤 每条告诉我三件事：**字出现了吗、弹确认卡了吗、发出去了吗**。
   ```
   open 'grokbot://app/v1/prompt?text=%E6%B5%8B%E8%AF%95'
   open 'grokbot://app/v1/prompt?text=%E6%B5%8B%E8%AF%95%0A%E7%AC%AC%E4%BA%8C%E8%A1%8C'
   open 'grokbot://app/v1/prompt?text=%E5%86%8D%E4%B8%80%E6%9D%A1'
   open 'grokbot://app/v1/prompt?text=%E6%B5%8B%E8%AF%95&id=wrong-id-123'
   ```
   第 1 条是"测试"；第 2 条带换行，应该什么都不发生；第 3 条在第 1 条还没发出时跑，看旧草稿会不会被换掉；第 4 条是错的 Bot id。第 1 条没出现字的话，再从 Grok 里复制一个 Bot 的链接贴给我。
3. **D5：在桌前说完，要不要直接填进 Grok？** 👤 默认要：每条省一下 Enter，Grok 不按回车从不发送。代价：松开约 3 s 后 Grok 会自己跳到前面，共享屏幕或演示时也一样（你正在 Mac 上打字时不会跳，留在等你送）。不要的话，每条在设备上或 Mac 上多按一下。
4. **不在 Mac 前时，要不要 Grok 在云端自己动手（例程 webhook，§3.7）？** 👤 默认不要；要的话放进 R4，只送打字的或翻完全文的语音，接的 Bot 不能有会动手的工具。
5. **项目的常用去向（§3.4）要不要？** 👤 默认要：给项目 3 设"常用 Grok"，只按 `3` 的便条到 Mac 上一下 ⌘↩ 就进 Grok，R1 就能用、不用等固件。
6. **Grok 之后先接哪个？** 👤 默认：Cursor 和 Claude（预填链接，各自过一次测试才算只填草稿），Obsidian 走 `file`。Claude Code 照今天用 `Q` / `S`。别的 app 随时在"其他 app…"里点一下。

## 9. 评审之间的分歧和取舍

四份评审（用户、隐私安全、契约架构、可行性）的修正都已经写进上面各节。下面是它们意见不一致、或者要合起来的地方。

| 分歧 | 选了 | 为什么 |
|---|---|---|
| Mac 上确认的来源叫 `mac`（契约）还是 `here`（可行性）| `mac` | docs/23 的 `env.link:"here"` 已经是"录音时连着这台 Mac" |
| 从 Mac 送 `session`：只排队（契约，docs/23 §3.4）还是 `E_NEEDS_DEVICE`（可行性）| 只排队，开 session 仍在设备看板上 | 它只写排队项，不执行；和 docs/23 已定的 `send ID session` 一致 |
| 信箱 `box`：补上 `--box`、档案、按信箱 `ack`、可信标记（安全、契约）还是不做（用户）| 不做；安全评审的条件写进 §2.5，以后要做先满足 | 读的那一半已经有了；领走默认关；用户的目标是"说完就到"，不是"再去取"；省约 600 行代码和测试 |
| "代码里没有 app"：豁免 `mcp/cli.py`（用户、可行性）还是把客户端搬进数据（契约）| 都要：测试按 §2.1 的范围今天就能绿，`mcp/cli.py` 记成带日期的已知例外，R3 搬进数据后删掉 | 测试第一天不红；原则最后也守住 |
| D5：设备上按 Enter（原稿）还是桌前直接填（用户）| 桌前直接填，再加四道闸：只填草稿且验证过、两边都没锁屏、Mac 键盘 3 s 没人按、Bot 没有占着的草稿 | Grok 不按回车从不发送；用户评审说得对，设备 Enter 是重复确认。键盘那一道是这次加的：抢焦点时你在别处按的回车会把草稿发出去 |
| 同一个 Bot 的草稿占用多久：10 分钟（用户）还是 30 分钟（可行性）| 30 分钟 | 猜成"占着"只多按一下 ⌘V；猜成"空着"会悄悄丢字 |
| 链接会不会自己做事：`draft_only` 每台 Mac 测试（用户）还是 `effect` 字段、没手测的当 `act`（契约、安全）| 一个字段 `effect`，缺了当 `act`；随 cpad 发、评审过模板的预设才能写 `draft`，每台 Mac 上测试并答"没自动发"才生效；你写的一律 `act` | 两边要的东西不冲突：预设说"能不能"，这台 Mac 上的测试说"真的是" |
| 溢出到剪贴板：桌前也要按 Enter（安全）还是只多一下 ⌘V（用户）| 剪贴板去向开着、而且写得上"只在本机、不进历史"标记时只多 ⌘V；标记写不上（退回 `pbcopy`）就要你按一下 | 安全评审担心的是剪贴板历史和通用剪贴板，标记正好挡住这两样；挡不住时才多问 |
| 剪贴板：2 分钟清（用户）还是加标记（安全）| 都做，再加"只在本机"（`currentHostOnly`）| 三样各挡一种扩散 |
| `map` 的值：≤40、拒高熵（安全）还是 ≤128（可行性）| ≤128（照 Grok 的规则），拒已知密钥前缀，不查熵 | Bot id 本身就是随机串，查熵会拒掉真的 id；前缀表挡住把 key 贴错地方 |
| 项目怎么带：同一行 `#项目`（可行性）还是固定的头（安全）| 合起来：同一行的头里带 `#项目`，后面一个空格接正文 | 两个问题都解决：不换行（Grok 不收）、开头不是特殊符号、接收方知道是语音 |
| 过期：带着选择回来（用户）还是只有没项目的回来（契约）| 没项目的回到"新"并写"建议：Grok"；有项目的不回待归档，标"没送出" | `dest.expired` 本来就记着 `dest`；有项目的本来就算已归档，再列一遍会重复 |
| 等你送里用 Enter 送（用户）还是 ⌥X（可行性）| ⌘↩ 送到预选的，⌥字母送到指定的；Enter 仍是打开详情 | 同一个键在不同组里意思不同会按错；Mac 上的单字母已经被占 |
| `cmd` 和 webhook：缩小（用户）还是加固（安全）| 都要：`cmd` 留在 R4 并照安全评审加固；webhook 不带预设或样例，只留一个问题 | 用户评审说的是"现在别做太多"，安全评审说的是"做的时候怎么做" |
| `url` 能接哪些 scheme：那个 app 登记过的（安全）还是只限随 cpad 发的预设里的 app（可行性）| 都要：scheme 必须是那个 app 登记过的；只有随 cpad 发、评审过模板的预设能是 `draft`；你写的一律 `act` | 你仍能接任何 app，只是没读过的就每条确认 |
| 交出去以后记什么：`dest.handed`（可行性）还是 `dest.delivered{via}`（安全）| `dest.handed`，带 `via` | 说清楚"交出去了、不知道收没收"；`via` 留着区分链接和剪贴板 |
| 依赖：cpa-stt 未合并（可行性，写评审时）| 已在 main（`d330465`），默认关，只要你跑一次 `cpa-stt use` | 评审之后 #62 合并了 |
| 手测几条链接：1 条（用户）还是 4 条（可行性）| 4 条，每条三个问题 | 能一次看出换行、旧草稿、错 id 这三种悄悄失败 |

## 附：证据索引

- 本地分支 `feat/destinations`（`b5ad361`）：d14:44-64、80-100、122-142、165、176-218（216 不变式 2）、242-257、263、283-358（350 行长）、378-407（386 `dest.expired`）、450、473、475、494；`dst/model.py:16、29-33、44-63、74-76`；`dst/policy.py:17、40-42、53、60-67（`_live`）、70-82`。
- `feat/filing-p1`（`e76330f`）：`cli/triage.py:236-237`；和 main 的 9 处冲突 [测 `git merge-tree`]。
- main（`d330465`）：`cpad/mcp/cli.py:18-63`；`cpad/hub/reduce.py:125-142`；`protocol/limits.json:72`；`protocol/README.md` 第 20 条；`docs/13-assistant-v2-plan.md:499-518`；`docs/15-local-data-access.md` §3.4、§3.6–§3.8、§3.11、§3.12、§4；`docs/18-context-profiles.md` §3.5、§3.6、§7.1、§12；`docs/20-ux-review.md:631、670`；`docs/22-voice-captures.md:141、§4.13、§5.4、§5.9、§8、:726`；`docs/23-mac-inbox.md` §1.3、§2.1、§3.1、§3.4、§5.2；固件 `input/keymap.cpp:99-104`、`rec/rec_meta.h:37`、`shell/rec_pages.cpp:257-258`、`shell/rec_overlay.cpp:109`、`shell/runtime_rec.cpp:125-130`、`module_api/canvas.h:110-116`、`test_codec_vectors/test_main.cpp:55`；`fix/memo-hold` 的 `input/keymap.cpp:111-112`（`RecLatch`）。
- 别的分支：d24 §1.2、§2.1、§2.4、§5.2（`wt-mac`）。
- app 包 [源]：Grok Bot 0.75.0 的 `app.asar`：`main-core.cjs` 的链接路由表（`/v1/agent?id=`、`prompt` 的 `text`、`id`）、`ob()` / `Fg()`（查询值里的控制字符一律拒收）、`NB()`（链接 ≤2048、可打印、无 `#` 和 `\`）、`Tu`（Bot id ≤128）、`Rle`、`Ale=1e3`、`CU=2e3`；`kept-draft`、`no-main-agent`；`main-app.cjs` 的 `/automations/webhook/`；MCP 表单字段、"Grok Bot's computer"的文案、"在本机执行"设置。Cursor 3.24.18 `extensions/cursor-deeplink/dist/main.js`、`extensions/cursor-mcp/dist/main.js`；Claude 3.1623.4 `app.asar`；ChatGPT.app 26.1007 的 `codex://threads/new`（`q` → `prompt`）和自带 codex；Claude Code 2.1.306 二进制里的字符串。只读，用 `wt-route` 里的 `asar_grep.py`、`asar_cat.py`（不进仓库）。
- 本机 [测]（macOS 26，只读或在临时目录，已删）：`shortcuts run --help`（`-i <路径>`、收 identifier）；参数长度上限和 `ps` 能看到参数；`LSOpenCFURLRef` 和 `LSCopyDefaultApplicationURLForURL`；AppleScript 注入和 JXA 读 stdin；`codesign` 看 uv 的 python；`cmd` 的 stdin、超时、`killpg`；8 进程并发追加；`launchctl print` 看 cpad 的会话；`feat/destinations` cherry-pick 后的测试数（可行性评审，临时克隆已删）。
- 外部 [研]：Hookdeck "Using Hookdeck with Grok Bot"；Cursor 论坛两帖（webhook 地址和 key 不显示）；Claude Code channels 参考；Obsidian URI 和 CLI 帮助；Notion "connect Custom Agents to MCP"；AppKit `NSPasteboardContentsCurrentHostOnly`；nspasteboard.org 的 `ConcealedType` / `TransientType` 约定。
