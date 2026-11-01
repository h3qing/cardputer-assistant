[English](24-mac-app.md) · **中文**

# 24 · Cardputer Assistant for Mac：自己的 Mac app，也是"任何 app 都能接"的样板

> 状态：**v1 已实现**（`mac/`；§10 的默认值）· 日期：2026-10-22（同日按三份评审修订：用户看到的、集成原则、稳定性；见文末"修订记录"）· 角色：架构（Mac app）· 基于 origin/main `995821d`，分支 `feat/mac-app` · 关联 #34、#54、#61，ADR 0002、ADR 0004，docs/25。
> 需求（2026-10-22）：目的不是给某一个 app 做集成，而是让所有 app 都能很容易地接进来；而且到现在还没有一个能打开的 Mac app。更早的语音备忘 M1（见 docs/23 抬头）要的是：没归属的录音进一个队列，写着转写、哪台设备、什么时候录的；在那里可以归档或开一个 workflow；最好有本地小模型先预处理，你只要点批准。
> 依据：docs/15 §1–§3；docs/19 §2.1、§5；docs/22 §4（`wt-stt`）；docs/23 §2–§5、§8；`protocol/filing/v1/README.md`（`wt-filing`，P1-A/B/C 已提交、未合并）；daemon 的 `cli/main.py`、`cli/rec.py`、`mcp/cli.py`、`archive/cli.py`、`app.py`；`cpa_stt/cli.py`（`wt-stt`）；本机只读实测。
> 标记：**[源]** 读过的代码或文档 · **[测]** 本机实测 · **[设]** 设计假设，可以改 · **👤** 需要你。
> 范围：只写设计。代码在同一分支的 `mac/`，照本文做。

## 0. 一页摘要

1. **做什么**：一个原生 Mac app「Cardputer Assistant」：一个窗口（待归档、录音、设备、集成、设置）加一个菜单栏图标，装在 `~/Applications`，双击就开。
2. **它只是一个客户端**：只用公开接口——`cpa … --json`、笔记目录里的开放文件、`cpa mcp`、`cpa export`、`cpa-stt`。不连 cpad 的私有 socket，不读 `$CPA_HOME`，没有给 WhisperWoof 或任何 app 的专用代码。**它能做的，任何 app 照同样的命令都能做**；WhisperWoof、Codex、Claude、Cursor 和它平起平坐（§1）。
3. **集成页**把这句话变成按钮：先是"去向"（说的话送到任何 app，`cpa dest` 有了以后接一个 app 是列表里点一下，docs/25 §5.7），再是"给开发者"的五张卡（MCP、命令行 + JSON、文件、转写程序、导出），片段一键复制、能不能用一眼看清；"复制接入说明"把这些整理成一段 Markdown，贴给任何 app 的编程助手（§2.4）。
4. **待归档**照 docs/23 §5 的交互：文字、哪台设备、哪台 Mac、什么时候；一行筛选（状态、日期 ▾、便条/会议 ▾、项目 ▾、文字）；按 1–9 一下归档（名字写在按钮上），交给 AI（排进待办 / 开工）、看过了、藏起来、播放，⌘Z 撤销（编辑菜单写着撤销哪一步）。本地小模型的建议进公开接口以前（G3），先用"文字里正好提到一个项目"当建议，按 Enter 批准（§2.1）。
5. **缺了也能用**：没装 `cpa filing` 时退回 `cpa rec ls --json` 的"没项目的录音"（只读），写清楚装什么能解锁；没装 `cpa-stt` 时本地转写开关是灰的（§4）。
6. **省心**：窗口或菜单看得见时每 3 s 问一次 `cpa`；看不见时只每分钟问一次 `cpa status --json`（菜单栏的点要真），文件读都不在主线程（§5）。
7. **怎么造**：SwiftPM（`mac/`），只要命令行工具、不要 Xcode；测试是一个可执行程序；每一屏用 ImageRenderer 画成 PNG 来看；脚本组装 `.app`、ad-hoc 签名（§7、§8）。
8. **16 条决定**都有默认值（§10.1）；**要你拍板的 3 件**：合并 `feat/filing-p1`、要不要"登录时打开"、docs/23 的 WhisperWoof 页还做不做（§10.2）。

## 1. 原则：它只是一个客户端

### 1.1 谁用哪个接口

| 公开接口 | 契约 | 这个 app 拿它做什么 | 别人怎么用同一个 |
|---|---|---|---|
| `cpa status --json` | 字段只增 [源 cli/main.py:105-112] | 设备页、菜单栏的点 | 任何脚本、状态栏工具 |
| `cpa filing ls/show --json`；`file/done/hide/reopen/hand --json` | `protocol/filing/v1` | 待归档 | WhisperWoof、启动器插件、你的脚本 |
| `cpa project ls --json` | 同上的 `projects` | 1–9 是谁 | 同上 |
| `cpa host name --json [-- 名字]` | 同上的 `host`（`feat/filing-p1`） | 这台 Mac 叫什么；起名 | 同上 |
| `cpa rec ls --json [--hidden]`、`cpa rec unhide` | 行形状 [源 cli/rec.py] | 录音页；待归档的退路 | 同上 |
| 笔记目录 `recordings/<日期>/<时间>-<会话>/` | 录音契约 v1 | 播放 `seg-NNN.wav`、全文转写、最近取回、转写卡在哪（`status.json`、`result.json`）、`.transcriber` | Obsidian、Finder、grep、转写程序 |
| `cpa mcp serve`（stdio，默认只读） | docs/15 §3 | 自己不用；集成页给接法：标签来自 `cpa mcp setup --list --json`（G8），没有就读 `cpa mcp setup --help` 列的客户端，再加"其他 MCP 客户端"；`cpa mcp tools/audit --json` 显示状态 | 任何 MCP 客户端 |
| `cpa export` | docs/15 §2.4 | 集成页的"导出" | 备份、迁移 |
| `cpa-stt status --json`、`use`、`off`、`setup`、`service install`、`retry` | docs/22 §4.11 | 设置页的本地转写开关；"重新转写失败的" | 任何守录音契约"转写程序"角色的程序 |

### 1.2 硬规则（§8.1 的测试钉住）

- 不连 `cpad.sock`；不读写 `$CPA_HOME`（`~/Library/Application Support/CardputerAssistant`）里的任何文件。
- 笔记目录只读，只开 `manifest.json`、`status.json`、`transcript.json`、`transcript.md`、`result.json`、`summary.md`、`seg-NNN.wav`、`recordings/.transcriber`；目录由 app 按 id 自己找（契约里没有路径；先按开始时间算出 cpad 放它的 `<日期>/<HHMM>-<id>`，再找那天和前后两天，最后才扫全部），realpath 必须在根目录内，`O_NOFOLLOW`，限大小。
- 写只有一种：跑你点过的那一条 `cpa` / `cpa-stt` 命令。没有后台写，没有不确认的批量写。
- 外部命令只经 `Process`、参数数组，不经 shell；`cpa` 的路径明确解析，有超时（§5.2）。
- 不联网、不开端口、不碰串口、不启停 cpad。
- 没有 app 专用代码：`mac/` 的源码、注释和测试里不出现任何 app 的名字（`RulesTests` 不分大小写地查）；集成页的客户端标签和片段都来自 `cpa mcp setup`，标签名由 id 生成。

### 1.3 缺什么，补进公开接口

App 要的东西公开接口里没有时，**加到 `cpa … --json`**，所有 app 一起受益，不在 app 里绕。评审后共十处：

| # | 缺口 | 补法（在 cpa 里） | v1 先怎么办 |
|---|---|---|---|
| G1 | `cpa status --json` 没有笔记目录 | 加 `notes_dir`（绝对路径） | 依次：设置里选的 → `notes_dir` → `cpa-stt status --json` 的 `recordings` 的上一级 → `~/CardputerNotes` |
| G2 | 这台 Mac 的名字 | **已有读法**：`cpa host name --json`（`feat/filing-p1`，契约 `host` 文档）；`cpa status --json` 的 `host_label` 可有可无 | app 用 `cpa host name --json` 读、`cpa host name --json -- 名字` 起名（1–24 字节）；旧 cpa 上灰着 |
| G3 | 建议器（本地小模型按现有项目大致分类） | 行里加可选的 `suggest{project, why}`，契约只新增 | 文字里正好提到一个项目（名字或 key）时当建议，写明"文字里提到了"，Enter 批准 |
| G4 | 只列"别的电脑的项目"列不出：`--orphans` 只是加行，新行超过 200 条时会被挤掉 | `cpa filing ls --orphans-only` 或 `--asked other` | `--orphans` 后在 app 里留 `asked` 的行 |
| G5 | 没有"今天几条" | `filing ls` 的 `counts.today` | 顶上一句不写今天 |
| G6 | 归档以外的错误读不出原因：`cpa export`、`rec unhide`、`cpa-stt use/off/setup/service install` 出错只给退出码 | 给它们 `--json` 错误文档（`E_EXISTS`、`E_CLOUD`、`E_INSIDE`、`E_PARENT`、`model_missing`…）；退出码 2 只留给用法错 | 写明是哪个程序出错，常见的命令加一句最可能的原因（导出：目标要新、不能在 iCloud 同步的文件夹或笔记目录里；`cpa-stt use`：多半是缺模型） |
| G7 | 说不出这个 cpa 支持什么，每个接入方都得靠"退出码 2"去试 | `cpa status --json`（或 `cpa --version --json`）加 `contracts: {filing:1, recording:1, host:1}` 和命令表 | 探测：只有探测时把退出码 2 当"没有这个命令"，别处都当出错 |
| G8 | `cpa mcp setup` 把说明和配置混在一起，也没有通用客户端 | `cpa mcp setup CLIENT --json` 给 `{where, format, body}`；`generic` 客户端给 `{command, args, env}`；`cpa mcp setup --list --json` | 第一行当"贴到哪"、其余当正文，只复制正文；"其他 MCP 客户端"由 app 按通用 stdio 形状生成 |
| G9 | `cpa-stt` 第一次 `use` 以前说不出模型连没连 | `cpa-stt status --json` 加 `models` 或 `ready`；`cpa-stt use --json` 给 `model_missing` / `selftest` | 没用过（`selftest_ok` 为空）或 `missing` 非空时出"选模型文件…"；`use` 失败后也出 |
| G10（低） | `cpa rec ls --json`、`cpa status --json` 没有 `v` 和 schema | 照 filing/v1 加版本 | 宽松读，字段只增 |

## 2. 窗口

左侧栏 + 列表 + 详情，最小 960×600。侧栏由 app 自己画（实心的 Sheet 色），不用 `NavigationSplitView`：macOS 26 上它把页面的滚动区变成玻璃工具栏的滚动边，页面会被推到标题栏下面，离屏也画不出来；自己排的侧栏每次都一样，截图画的就是这个真窗口（§8.2）。打开 app 就是待归档，第一条"新"的已选中，焦点在列表上。

| 屏 | 数据 | 主要动作 | 截图状态（§8.2） |
|---|---|---|---|
| 待归档 | `cpa filing ls/show --json`、`cpa project ls --json`、WAV | 1–9 归档、交给 AI、看过了、藏起来、播放、撤销 | 有字、没字、别的电脑的项目、提示条、全归好、简版 |
| 录音 | `cpa rec ls --json`、开放文件 | 播放、在 Finder 中显示、重新显示 | 列表、详情 |
| 项目（2026-10-26） | `cpa project list --json`，改动是 `cpa project add/rename/move/folder/rm --json` | 新建、改名、换号（编号菜单或拖 ≡）、连目录、删除 | 有连目录和只是分组的、正在改名和等确认删除、cpa 太旧 |
| 设备 | `cpa status --json`、`cpa project ls --json`、manifest | 复制命令 | 连着、没连、cpad 没跑 |
| 集成 | `cpa mcp setup/tools/audit`、`cpa status --json`、`.transcriber` | 复制片段、复制接入说明、导出 | 全能用、命令行卡、缺 `cpa filing`、缺 `cpa-stt`、每个 MCP 标签 |
| 设置 | `cpa-stt status --json`、`cpa --version` | 本地转写开关、电脑名、选目录 | 开、关、没装 |
| 菜单栏弹出框 | 最近一次刷新的结果 | 打开窗口、跳到某一条 | 有待归档、全归好、cpad 没跑 |
| 没找到 cpa | — | 复制安装命令、重新查找 | 一张 |

### 2.1 待归档

- **顶上一句**："12 条待归档 · 其中别的电脑的项目 1 条 · 设备上还有 2 段"（`counts.new + counts.other_mac`，和侧栏、菜单栏、"待归档"按钮是同一个数）。"今天几条"等 G5。
- **筛选，一行，不用打字**：状态是一组分段按钮（待归档 · 已交给 AI · 看过了 · 全部）；"别的电脑 N"有才出现，"没转出文字"在最近一次列表里有转写失败的才出现；日期 ▾、便条/会议 ▾、项目 ▾ 是菜单（1–9 按钮在详情里）；搜索框（`/` 进入，词经 stdin `--text -` 传）。全都映射成 `cpa filing ls` 的参数，app 不自己再算一遍归档。
- **一行**：标题或第一句（两行内）；没字时写"13:25 的便条"（状态在旁边说为什么）；"便条 · 24s"；相对时间和绝对时间（不确定加 ~）；哪台 Mac 取回（`env.host` 等于这台 Mac 的名字时写"这台 Mac"，没名字写"没起名的 Mac"）；设备（两台以上才显示短 id 末 4 位）；转写状态（图标加字加颜色）。
- **详情**：全文；一个胶囊"公司 取回 · 录音时连着它 / 录音时没连"；逐句转写（`[mm:ss]`，点一句从那里播）；播放条；**1–9 一排按钮，写编号和名字**，当前项目高亮、建议的项目描边；"交给 AI · 排进待办"（q）、"交给 AI · 开工（到设备上按 Enter）"（s）、"看过了"、"藏起来…"（要确认）、"在 Finder 中显示"。交给 AI 以后显示按 qid 读回的排队正文（"排队号 q12"）。转写失败或跑不了时说原因（`status.json` 的 `reason`、`result.json` 的 `error.code`），装了 `cpa-stt` 时给"重新转写失败的"（`cpa-stt retry`）。
- **建议卡**：行里有 `suggest`（G3）以前，新的、没项目的便条文字里正好提到一个项目（名字，或 key 当整词）时，详情顶上"建议：3 论文（文字里提到了）· 按 Enter 批准"，按钮描边；批准 = `cpa filing file ID KEY`，不另开写的路。提到两个、一个字的名字、别的电脑的项目都不建议。
- **按键**（焦点在列表上才生效，和 docs/23 §5.2 同一套）：↑↓ / `j` `k` 移动；`1`–`9` 归档并跳到下一条"新"的；`0` 退回没项目；空格播放 / 停；Enter 批准建议；`q` 排进待办；`s` 开工；`x` 看过了；`/` 搜索；⌘Z 撤销，可以连按。列表下面两行提示写着这些键。
- **反馈**：5 秒提示"已归到 3 acmeshop · ⌘Z 撤销"；失败就地说清楚（"cpad 比 cpa 老：在终端跑 `cpa service install`"），不跳；写的命令 30 s 超时，超时写"不确定有没有做成，已刷新"并重读列表。撤销用结果里的 `undo` 对象，app 用自己的动词表重建命令、再校验 id，不照抄；每一步登记进窗口的撤销管理器，编辑菜单写"撤销“归到 3 论文”"，没有可撤销的就灰着，⇧⌘Z 和文字框里的撤销照旧。
- **简版**（没装 `cpa filing`）：只能听、在 Finder 中显示；归档、交给 AI、看过了的按钮都不出现，一行写"归档、交给 AI、看过了都要 cpa filing"。
- **挡误操作**：别的电脑的项目，数字后再按 Enter（docs/23 D15），命令加 `--other`；会议的"交给 AI"灰着，写"会议里有别人的话，不能交"（D16）；没字的行 `q`、`s` 灰着，写"还没转写，先归档或等转写"。
- **空**：Mando 和"都归好了"。

### 2.2 录音

`cpa rec ls --json`：每行标题是文字（`result.json` 的标题或便条全文，再不然转写的头几句；每条录音每个状态只读一次），没字写"13:25 的便条"；下面是便条 / 会议、时长、时间、项目、★、状态。顶上"23 条录音 · 设备上还有 2 段"（段数用 `cpa status --json`，和设备页一样）。开关"显示藏起来的"（`--hidden`），藏起来的行有"重新显示"（`cpa rec unhide`）。详情读开放文件：逐句转写、`summary.md`（链接显示成文字，不加载图片）、为什么没转出来、播放、在 Finder 中显示。

### 2.3 设备

- **一行大字**："设备连着 · 固件 v0.4.1" / "设备没连" / "cpad 没在跑"。`link` 译成中文：连着、握手中、没连、暂停 N 秒。
- **数字**：设备上还有 N 段（`recordings.on_device`；没连时写"上次看到设备上有"）、待发 N 条（`device.pending`）、转写失败 N 段（有就给"重新转写失败的"）、最近取回（最新一天里最新 manifest 的 `written_at`）、最近转写（`recordings.last_result_at`）、存储卡、cpad 版本。
- **1–9 表**：九个数字都列，没项目的写"空"；编号、名字、key、目录（连着的写目录名，`ok:false` 写"目录不在了"，没连写"没连目录"）。来自 `cpa project ls --json`，没有它时用 `cpa status --json` 的 key。改项目在"项目"页（§2.6）。
- **哪台 Mac、什么网络**：这台 Mac 的名字（`cpa host name --json`）；每条录音的 `env.host`、`env.link` 在待归档里。不记网络：Wi-Fi 名字算行踪，契约里故意不存 [源 docs/23 §4.1]。
- `warnings` 原样列出（过 display_safe）。**不显示 `device.dev`**：它是设备出厂的 MAC 地址 [源 app.py:59-69；docs/23 §3.2]。

### 2.4 集成

开头一句："Cardputer 的数据对所有 app 开放；这个 app 自己也只用下面这些接口。"然后两部分：

**去向：说的话送到任何 app**（docs/25 §5.7）。这是"所有 app 都能接"对你的意思：说话时按一个字母，字就进那个 app 的输入框，从不替你按发送。`cpa dest`（docs/25 R1）有了以后，这张卡列出这台 Mac 上能接的 app（`cpa dest presets --installed --json`），每个一个"加上"和"送一条测试"，最后一行"其他 app…"；背后全是 `cpa dest …`。在那之前卡片写"要先有 cpa dest（R1）"，灰着列出这三步。

R1-E（2026-10-22）做了的样子，以这里为准：
- **探测**：`cpa dest pending --json` 回答了（不是退出码 2）才算有 `cpa dest`；之前一切照旧。
- **集成 → 去向**：已加的去向（⌥字母、种类、"会离开这台 Mac / 只在本机"、状态的一句话、url 和 app 有"送一条测试"），项目的常用去向（每个项目一行，一个去向一个按钮，点一下就是 `cpa dest usual 项目 去向`，再点或"不设"是 `-`），这台 Mac 上能接的（"已安装 0.75.0 · 字母 X"、"加上"）。"加上"先在卡里展开确认（cpa 预设自己的说明、会不会离开这台 Mac、"加上以后先送一条测试"），确认后才跑 `cpa dest add --preset P --yes`。
- **一次性测试**就地问，不弹窗：`cpa dest test ID --send --json` → "<app> 里出现"测试 NNNN"了吗？"（出现了 / 没出现）→ "自动发出去了吗？"（没有，只填进去了 → `verify --draft`；发出去了 → `verify --act`）；链接去向没出现时让你贴 Bot 的链接或 id，`cpa dest map ID '*=链接'` 再测一次。
- **待归档**：列表上面是"等你送 N"（`cpa dest pending --json`），↑↓ 把它和待归档当成一个列表走；选中一条时右边是它的详情：⌘↩ 送到预选的（`cpa dest send REF --by app`）、⌥D 不送了、下面"或者送到…"。便条的详情在"接下来"下面多一张"送到…"：一个去向一个按钮，链接在前（项目的常用去向排第一），按钮上写 ⌥字母，⌥ 加字母和点按钮一样（`cpa filing send ID 去向 --by app`）。会议、没字的、只读列表没有或灰着；可能自己发出去的（`fl` 含 `k`，例如还没测的）灰着、点了说"先送一条测试"，从不在 Mac 上替设备确认。送完 5 秒的提示用 cpa 打印的 `sent`："已填进 <app> · 到 <app> 里按回车发出"、"在剪贴板 · 到 <app> 按 ⌘V（<app> 里还有上一条草稿）"。等你送的一条送完或不送了，焦点到下一条。
- **还没做**："其他 app…"（`--apps`）、在卡上换字母（今天用预设的字母，cpa 在被占时不绑）、`cpa dest add` 没有 `--json`（app 只看退出码和一行字）。

**给开发者：读 Cardputer 的数据**，五张卡，每张一句话、一个状态（能用 / 要先装 X）、可复制的片段；右上"复制接入说明"把下面这些整理成一段 Markdown（cpa 的绝对路径、每条命令和它今天能不能用、规矩、契约在哪、通用 MCP 设置、笔记目录布局），贴给任何 app 的编程助手，它就照这些接。

1. **AI 客户端（MCP）**：标签来自 cpa——`cpa mcp setup --list --json`（G8），这个 cpa 没有时读 `cpa mcp setup --help` 里列的客户端；标签名由 id 生成。每个标签显示"贴到：……"（`cpa mcp setup <client>` 第一行），下面的正文才是复制的内容；JSON 的写"已有 mcpServers 时只把 cardputer 这一项并进去"。最后一个标签"其他 MCP 客户端"：传输 stdio、命令 `<cpa 的绝对路径>`、参数 `mcp serve`、`CPA_HOME`（设了才有），和一段通用的 `mcpServers` JSON。下面列 `cpa mcp tools --json` 的工具（读 / 写）和 `cpa mcp audit --json --last 20` 的最近调用（哪个客户端、哪个工具、什么时候；没有正文；`E_DISABLED` 写"被拒：写入没开"）。App 从不改别的 app 的配置。
2. **有界面的 app（命令行 + JSON）**：先写 cpa 在哪（从 Finder 打开的 app 的 PATH 里没有 `~/.local/bin`，所以复制的命令都带绝对路径），再列七条命令，每条自己写能不能用：`status --json`、`rec ls --json`、`rec show SID --json` 今天就能用；`filing ls/show/file --json`、`project ls --json` 写"装好 cpa filing 后"。规矩：参数数组、不经 shell、有超时、正文走 stdin、`v` 不是 1 就整份拒绝、不认识的字段忽略。契约在仓库的 `protocol/filing/v1/README.md`。这个 app 就是例子。
3. **文件**：笔记目录在哪（"在 Finder 中显示"）、布局（`manifest.json`、`seg-NNN.wav`、`transcript.json`、`transcript.md`、`result.json`（`memo.text` 是便条全文）、`summary.md`、`recordings/.transcriber`），契约：仓库的 `protocol/recording/v1/README.md`。笔记软件、grep 不用任何代码就能读。
4. **转写程序**：谁认领着 `recordings/`——读认领文件 `recordings/.transcriber`（`cpa status --json` 的 `transcriber` 是同一份信息）：cpa-stt / 别的程序 / 没有 / 认领过期。任何本地转写程序守录音契约的"转写程序"角色就能接（契约同上，转写程序一节）；`cpa-stt` 能跑任何守 docs/22 §4.3 约定的引擎。
5. **导出**："导出到…"（文件夹或 zip，可以不带音频）= `cpa export DEST [--zip] [--no-audio]`，命令写在按钮下面；目标要是新的，不能在 iCloud 同步的文件夹或笔记目录里。

### 2.5 设置

- **本地转写**：一个开关。开 = `cpa-stt use`，自检过了再 `cpa-stt service install`；缺模型（`missing` 非空或 `blocked:"model_missing"`）、从没开过（还没有配置，G9）或上一次 `use` 失败时出"选模型文件…"（系统对话框）→ `cpa-stt setup 路径` → 再开。关 = `cpa-stt off`。下面显示 `cpa-stt status --json`：命令、沙箱、自检、`blocked` 的原因、最近几段的耗时。别的程序认领着 `recordings/` 时开关灰掉，写"WhisperWoof 正在转写，两个只能开一个"（docs/22 §4.10）。默认关，和 `cpa-stt` 一致。
- **这台 Mac 的名字**：家 / 公司 两个按钮 → `cpa host name --json -- 名字`，读回 `host` 文档；名字 1–24 字节（8 个汉字）。旧 cpa（没有 `cpa host name --json`）上灰着，写"随 cpa filing 一起，还没合进 main"。
- **cpad 服务**：在不在跑、版本；没跑或太旧时给出可复制的 `cpa service install`，app 不启停它（D8）。
- **cpa 的位置**、**笔记目录**：显示解析到的路径，可以"重新查找"或手动选；选的排第一（§5.2），所以能拿一个分支的 `cpa` 先试。

### 2.6 项目（2026-10-26，docs/25 D24）

项目是这台 Mac 上建的**有名字的分组**：录音时在设备上按 1–9，这段语音就归到那个项目，先留在这个 app 的"待归档"里，由你手工送到某个 agent 或 app（docs/25 D23）。代码目录可连可不连：只有连了的才有 hook、按 cwd 认项目、设备上开 session。

- **新建**：输入名字（中文也行，最多 24 个字母或 8 个汉字），"新建"或回车放到第一个空号；输入了名字时空行出现"放在这里"，放到那个号 → `cpa project add --json [--slot N] -- 名字`。没输入名字时空行写"拖一个项目到这里，或先在上面输入名字"（不放灰掉的按钮）。key 由 cpa 从名字来（中文名是 `p<号>`；删掉的项目用过的 key 不会再给）。9 个号满了"新建"灰着。
- **1–9**：和设备一样九行。每行：≡（拖到另一行就换号）、编号（点开是 1–9 的菜单，选一个就换过去，那个号上的项目换到这里，两下点完，`cpa project move`）、名字（双击就改名；悬停看 key。key 不单独占一列：中文名的 `p3` 换号以后还是 `p3`，看着像错）、目录（连着的写目录名，绿；不在了，黄；没连，灰）、改名（就地输入，`cpa project rename --json KEY -- 新名字`）、连目录…（系统对话框选目录 → `cpa project folder --json KEY 路径`）或断开目录（`--none`）、删除（第一次点变成"删掉'名字'？录音会留着 · 删除 · 取消"，第二次才 `cpa project rm`，项目的常用去向一起忘掉）。
- 每个命令的回答就是改完以后的 `projects` 文档，立刻画出来；"待归档"的 1–9 按钮、说明和设备页都用同一份名字。被拒时说为什么（号被占了、key 被占了、目录不在了、名字不行）。
- 旧 cpa（没有 `cpa project list`）上整页只读：每行的按钮、≡ 和编号菜单都不出现，顶上写怎么更新，命令和设置、待归档里的是同一条（`Setup.updateCpa`，现在是 `uv tool install --force ./daemon`；docs/27 的安装脚本合并以后只改这一处）。
- 截图：`ImageRenderer` 画不了拖放和输入框（会画成黄块），所以快照里（`isSnapshot`）不挂拖放、编号不做菜单、输入框画成同样的框加字；真窗口照常。
- 名字是你打的字：放在 argv 最后、`--` 后面，和 `cpa host name` 一样先校验（1–24 字节、不以 `-` 开头、没有控制和方向字符）；路径只收绝对路径。

### 2.7 语言（2026-10-26）

- 界面有**中文**和 **English**。默认跟随系统：系统偏好语言的第一个是中文（zh-Hans、zh-Hant…）就用中文，否则用英文；设置里"语言 · Language"可以选 跟随 Mac（按钮上写现在是哪种："跟随 Mac（现在：中文）"、"Follow the Mac (English now)"）/ 中文 / English，存在这个 app 自己的 defaults 里，窗口和菜单栏马上换。
- 代码里每句界面文字都写成 `tr("中文 {0}", 值)`：中文本身就是键和中文表；英文表一份（`CardputerCore/Localization/EnglishTable*.swift`，为了文件不超过 400 行分成两个文件）。`{0}`、`{1}` 是按顺序代进去的值。
- `L10nTests`：每个 `tr()` 都有英文、两边的占位符一样、英文里没有中文；源码里没有不经 `tr` 的中文字面量。录音的文字、项目名、这台 Mac 的名字是你的内容，不翻译。
- 英文要读得像英文（评审 2026-10-26）：带数的句子是一整句加占位符（"{0} still on the device"），不拼两段；名字长度说"24 letters (8 Chinese characters)"，不说 bytes；较早的日期写"Sep 16"、"Aug 12, 2025"，不写 9/16；"1 segment / 3 segments"。`EnglishTests` 钉住这些。
- 截图两种语言都画：英文在 `out/shots/`，中文在 `out/shots/zh/`（`--lang en|zh` 只画一种）。

## 3. 菜单栏

- **图标**：Mando 的头（用它自己的颜色，深浅菜单栏都看得清，不是模板图）加一个点：绿 = 设备连着；灰 = 没连；红 = cpad 没跑或 `cpa` 出错；灰圈 = 不确定（`cpa status` 超过 90 s 没答或出错）。有待归档时旁边一个数字。不只靠颜色：弹出框第一行写字。看不见时也每分钟问一次 `cpa status --json`，点不会一直绿着。
- **点开**：状态一行（"设备连着 · 设备上还有 2 段"，不写固件版本）；最近 5 条待归档（第一句 + 时间），点一条就打开窗口并选中它；"打开 Cardputer Assistant"；"退出"。简版时没有列表。
- 关掉窗口后 app 只留在菜单栏、不占 Dock；打开窗口时回到 Dock。

## 4. 能力检测和退路

启动、打开窗口、点"重新检查"时探一次，之后 60 s 一次；`cpa` 没找到时每次都重新找一遍。办法是"跑一下看退出码"：argparse 遇到不认识的子命令退出码是 2。**只有探测这样读退出码 2**：`cpa` 和 `cpa-stt` 遇到坏的值、配置或文件错误也退出 2，别处的退出码 2 一律当"出错了"，写明是哪个程序（G6、G7）。

| 能力 | 怎么测 | 没有时 | 怎么解锁 |
|---|---|---|---|
| `cpa` | §5.2 的查找 | 整个窗口一页"没找到 cpa"，安装命令可复制，"重新查找" | 在仓库里 `uv tool install ./daemon` |
| cpad 在跑 | `cpa status --json` 的 `running` | 红点；待归档照常（`cpa filing` 不要 cpad），设备页写"cpad 没在跑" | `cpa service install` |
| `cpa filing` | `cpa filing ls --json --limit 1` 退出码 0 且 `v:1` | **简版"没项目的录音"**：`cpa rec ls --json` 里没项目、没藏起来的行；文字读开放文件（`result.json` 的 `memo.text`，没有就 `transcript.json` 的前几句，过 display_safe、≤280 字）；只能听和看，动作按钮灰着写"装好 `cpa filing`（docs/23 P1）才能归档" | docs/23 P1 合并后重装 `cpa` |
| `cpa project ls --json` | 退出码 | 用 `cpa status --json` 的 key 当名字 | 同上 |
| 新的 cpad | `hand` 回 `E_UNAVAILABLE`（`field:"cpad"`） | 提示"先重启 cpad" | `cpa service install` |
| `cpa project list`（2026-10-26） | `cpa project list --json` 是 `projects` 文档 | 项目页只读，写怎么更新 | 本分支合并后重装 |
| `cpa host name` | `cpa host name --json` 是 `host` 文档 | 设置里灰着，行里的"这台 Mac"只认名字 | `feat/filing-p1` 合并后重装 |
| `cpa-stt` | 和 `cpa` 同样的查找，再 `cpa-stt status --json` | 开关灰着，写"装好 cpa-stt（docs/22 VC-1）才能本地转写" | `wt-stt` 合并后重装 |

- 简版不叫"待归档"，侧栏没有数字：不在 app 里重做一遍归档的判断（docs/23 §5.3）。
- 本机今天 [测]：`~/.local/bin/cpa` 是指向 uv 工具目录的链接；`cpa-stt` 没装；装着的 `cpa` 还没有 `filing`。所以合并前打开 app 看到的就是简版。

## 5. 数据刷新和执行 `cpa`

### 5.1 刷新

- **看得见**（窗口没被挡住，或弹出框开着）：每 3 s `cpa status --json` 加当前页的列表；选中一行时 `show`；项目表 60 s 一次；设置页开着时 `cpa-stt status --json` 10 s 一次。
- **看不见**：只每分钟一次 `cpa status --json`（一次约 0.08 s，菜单栏的点要真）。另外用 FSEvents（文件级事件）盯 `<笔记目录>/recordings`（2 s 防抖，只读；从不盯 `$CPA_HOME`；点开头的文件不算，所以转写程序每分钟的心跳不会叫醒它）：有变化时跑一次 `cpa filing ls --json --limit 1` 更新菜单栏数字。App 一启动就把这些都读一遍，窗口没开也一样。醒来、窗口回到前面时马上刷一次。
- 同一种调用同时只有一个；上一次没回来就跳过这一轮，不排队。失败时保留上一次的数据，写"N 秒前"，连续失败退到 15 s 一次。结果和上次一样就不换，页面不会每 3 s 重画。
- 筛选一变、一个动作一做，列表的"代"就加一；之前出发、之后才回来的读取直接丢掉，不会把旧筛选或刚归档的那条带回来。
- 读笔记目录（详情、转写、`.transcriber`、最近取回）都不在主线程；最近取回只看最新的一天，看得见时 60 s 一次。简版的文字按（id、状态、结果）记住，只重读变了的录音；简版也守 `limit`；菜单在简版时不读列表。
- 播放：每段的长度按文件大小算（16 kHz 单声道 16 位，去掉 44 字节头），只把正在放的那一段读进内存，放完再读下一段。
- 动作完成后马上刷新列表，不等下一轮。

### 5.2 执行 `cpa`

- **查找**：设置里选的 → `~/.local/bin/cpa` → 进程 `PATH` 里的 → `/opt/homebrew/bin`、`/usr/local/bin`。选的排第一，没过检查就往下找。取 realpath，要求属于你或 root、组和其他人不可写。从 Finder 打开的 app，`PATH` 只有系统目录，所以 `~/.local/bin` 排在 `PATH` 前面。`cpa-stt` 一样。启动失败（比如它的 Python 环境被删了）当"没找到"。
- **`Process`**：参数数组；环境变量白名单 `HOME`、`USER`、`LOGNAME`、`LANG`、`TMPDIR`、`CPA_HOME`（有才传），`PATH` = `cpa` 所在目录 + `/usr/bin:/bin:/usr/sbin:/sbin`（`cpa mcp setup` 靠它填出真实路径）；stdin 默认关，搜索词经 stdin；超时 5 s（归档的写 30 s，`export` 10 min，`cpa-stt use` 2 min），到时对它的整个进程组 SIGTERM、1 s 后 SIGKILL；超时是硬的：程序退出以后输出最多再等 1 s（它留下的子进程可能还占着管道），然后就用已经收到的；只有程序没退出才算超时。stdout 最多读 4 MiB（`cpa rec ls` 没有 limit 选项，32 MiB）；每份输出只解析一遍；stderr 不显示、不记日志。
- **参数先校验**：id 是 MonoId；key 符合 `^[a-z0-9][a-z0-9-]{0,11}$`；枚举在表里；名字放 `--` 后、`-` 开头拒绝。
- **输出**：`Codable` 解析；`v` 不是整数 1 就整份拒绝；不认识的字段忽略，不认识的枚举值显示"未知"（契约的 `x-knownValues`）；给人看的字再过一遍 display_safe（C0、DEL、C1、双向控制符、U+2028/9）。
- **日志**：只记子命令名、耗时、退出码、错误码，不记 argv，不记任何文字。

## 6. 外观

- **深色**，令牌照 docs/19 §2.1（来自 WhisperWoof 深色主题）：底 `Bg` #120e0b、卡 `Card` #221c18、选中 `Select` #463525、正文 `Fg` #f3ede6、第二级 `Ink2` #bfb2a6、次要 `Dim` #96897b、强调 `Accent` #e2a867（唯一的强调色：主按钮、焦点、当前项目）、`Ok` #72c291、`Warn` #dcc15a、`Error` #f4675b。`Theme.swift` 的色值由测试和 `tools/theme/tokens.py` 逐个比对。
- **字和间距**：系统字体（SF、PingFang），正文 13 pt、标题 17 pt；8 pt 网格，卡片内边距 16、圆角 10；侧栏和内容都是实心的（侧栏 Sheet 色，D15）。
- **Mando**：`mac/scripts/export_mando.py`（只用 Python 标准库）读已提交的 `firmware/app/lib/theme/gen/mando64_gen.h`（36×64，4 位）和 `tools/theme/mando_colours.json`，最近邻放大成 PNG，`iconutil` 生成 `AppIcon.icns`；空状态用同一套帧。不读 WhisperWoof；来源和许可证已在 NOTICE（docs/19 §5.3）。
- **文案**：短、中文、说结果（"已归到 3 acmeshop"）；状态都是图标加字加颜色。

## 7. 打包

```
mac/
├── Package.swift            # swift-tools-version 5.9，platforms macOS 14
├── Sources/
│   ├── CardputerCore/       # 纯逻辑：模型、解析、筛选→argv、撤销、能力检测、display_safe、刷新调度、Runner 协议
│   ├── CardputerUI/         # SwiftUI 各屏、Theme、文案表
│   ├── CardputerAssistant/  # 可执行：App、窗口、MenuBarExtra、真正的 ProcessRunner
│   ├── CardputerCoreTests/  # 可执行：自带断言的测试程序
│   └── CardputerShots/      # 可执行：用假数据把每一屏画成 PNG
├── Fixtures/                # filing/v1 向量的副本（带 SOURCE）、假的 status / rec ls / cpa-stt 输出，没有真文字
└── scripts/
    ├── build-app.sh         # swift build -c release → build/Cardputer Assistant.app，ad-hoc 签名并校验
    ├── install-app.sh       # 换进 ~/Applications（先放临时名再改名），不要 sudo
    └── export_mando.py      # sprite → PNG → AppIcon.icns
```

- **`.app`**：`Contents/Info.plist`（`CFBundleIdentifier` `com.cardputer-assistant.mac`、`LSMinimumSystemVersion` 14.0、`CFBundleIconFile`、版本号）、`Contents/MacOS/CardputerAssistant`、`Contents/Resources/AppIcon.icns`；`codesign --force --sign -`，再 `codesign --verify --strict`。
- **不进 App Sandbox**：沙箱会传给子进程，`cpa` 就写不了 `$CPA_HOME`，`cpa-stt` 也装不了 LaunchAgent。不联网由源码扫描测试钉住（§8.1）。
- 本机造、本机装，没有隔离属性，Gatekeeper 不拦；不公证、不分发（D12）。
- CI 的 `mac` 任务（macOS runner）只在 `mac/` 或它读的仓库文件（`tools/theme/`、固件的 sprite、`ci.yml`）有改动时跑：`swift build`、测试程序、画图（PNG 作为 artifact 上传）、`build-app.sh`；不装 [源 .github/workflows/ci.yml]。版本号取仓库最新的 `v*` tag。

## 8. 测试

### 8.1 核心（先写失败的测试：RED → GREEN → REFACTOR）

`swift run CardputerCoreTests`，打印每条失败的断言和位置，有失败就退出码非 0。

- **契约**：`Fixtures/` 里 filing/v1 的合法向量全能解析；`v` 不对、未知 `filing`、`handed` 没有 `queue` 的拒绝；带不认识的字段和枚举的行照样显示"未知"。
- **argv**：每个动作只产出 id、key、枚举；非法的在跑命令前就拒绝；搜索词只走 stdin；`--other` 只加在别的电脑的项目上。
- **撤销**：`undo` 对象 → argv，用自己的动词表；不认识的 `op` 拒绝；`undo:null` 时 ⌘Z 提示"这一步不能撤销"。
- **筛选和选中**：每种按钮组合映射成固定的 `cpa filing ls` 参数；动作后选中下一条"新"的。
- **退路**：`cpa rec ls --json` 的行 → 简版列表；读开放文件时跳出根目录、符号链接、超大文件都拒绝。
- **能力检测**：假 Runner 给退出码 2、超时、坏 JSON、`v:2`，各得到对的"没有 / 出错"。
- **查找和执行**：临时目录里的假 `cpa`（顺序、realpath、组可写的拒绝）；一个直接按参数数组跑的假可执行文件，测超时杀进程、4 MiB 截断、环境变量白名单。
- **刷新调度**：看得见时 3 s 一次，看不见时零次；同一种调用不叠；失败退避。
- **display_safe**、**中文时间**（"3 分钟前"、"~08:10"）、**菜单栏的点**（由 status 推出）。
- **规则扫描**：`Sources/` 里没有 `URLSession`、`import Network`、`NWConnection`、`cpad.sock`、`Application Support/CardputerAssistant`、`/bin/sh`、`/bin/zsh`；不分大小写地没有任何 app 的名字（docs/25 §2.1）；进程只在 `ProcessRunner.swift` 里启动；每个文件不超过 400 行。
- **令牌**：`Theme.swift` 的色值等于 `tools/theme/tokens.py`。

### 8.2 界面：画成 PNG 来看

`swift run CardputerShots --out DIR` 用 `Fixtures/` 的假数据，把 §2 表里每一屏的每种状态画出来，再拼一张总览，打开逐张看：

- **真窗口**：`MainWindow` 放进一个屏幕外的 `NSWindow`（标题栏、侧栏、页面都是 app 里那一份），`cacheDisplay` 读回像素，1200×800，不要屏幕录制权限，屏幕上什么都不出现。
- **整页**：每一页按它自己的高度再画一张（`NAME-full.png`，`ImageRenderer`），窗口底下切掉的卡片也看得到；集成页的每个 MCP 标签各一张。
- **菜单栏**：`MenuBarLabel`（Mando、点、数字）在浅色和深色菜单栏上各一行，四种点都在；弹出框单独画。
- **`--live`**：同样这些屏用这台 Mac 的真 `cpa`、`cpa-stt` 和笔记目录画（只读：命令先过一张只读白名单，写的命令根本不会启动）；图里是真文字，留在本机、不提交。

测试只查每张都生成了、不是一张纯色图；好不好用、好不好看靠人看。

### 8.3 本机端到端

用临时的 `CPA_HOME` 和笔记目录（放几段假录音）启动 app，点一遍 1–9、`x`、⌘Z，再用 `cpa filing ls --json` 核对。真的 `~/CardputerNotes` 只读打开看一次；开发时不在真数据上点任何写的动作。

## 9. 分期

| 期 | 内容 | 依赖 |
|---|---|---|
| **v1（现在）** | §2–§8 全部，中文 | 看：今天就能用（简版）；归档动作：docs/23 P1 合并；本地转写：`wt-stt` 合并 |
| v1.1 建议 | 建议卡和"批准"；"全部按建议归"（先列出来，确认一次）；登录时打开 | G3：建议器进公开接口 |
| v1.2 英文 | 文案表加英文，跟随系统语言；浅色 | — |
| 以后 | 网络戳（docs/23 §4.3）、按档案筛（docs/18 v0.8）、语音采集行（#52） | 各自的设计 |

## 10. 决定和要你拍板的事

### 10.1 决定

不回复就按默认做。

| # | 决定 | 默认（推荐） | 备选 | 为什么 |
|---|---|---|---|---|
| D1 | App 的身份 | 自己的原生 app，公开接口的一个普通客户端 | WhisperWoof 里的一页（docs/23 §5） | 你要的是"所有 app 都能接"；自己的 app 只用公开接口，就是证明 |
| D2 | 技术 | SwiftUI + SwiftPM，只要命令行工具 | Electron / 本地网页 | 原生、省内存、不开端口；这台 Mac 没有 Xcode 也能造 |
| D3 | 读写边界 | 读 `cpa … --json` 和笔记目录的开放文件；写只跑你点的 `cpa` 命令 | 连 `cpad.sock` | socket 是 cpad 自己的；走它，别的 app 就学不了 |
| D4 | 缺的东西 | 补进 `cpa … --json`（G1–G3），app 先用公开的退路 | app 读 `config.json` 自己猜 | 补一次，所有 app 都有 |
| D5 | 刷新 | 看得见每 3 s；看不见不轮询，只用 FSEvents 更新菜单栏数字 | 一直 30 s 轮询 | 省电，不打扰 cpad |
| D6 | 没装 `cpa filing` | 简版"没项目的录音"，只读，写明怎么解锁 | 什么都不显示 | 今天就能用；不在 app 里重做归档判断 |
| D7 | 交给 AI 后在哪开 session | 只排队，到设备看板上按 Enter，一律 plan（docs/23 D7） | app 里开 | ADR 0002：只有设备上的 Enter 证明人在 |
| D8 | cpad 服务 | 只显示状态和可复制的 `cpa service install`；app 不启停 cpad | "重启 cpad"按钮 | 重启会打断正在拉的录音；很少需要 |
| D9 | 本地转写 | 设置里一个开关，背后是 `cpa-stt use/off/setup/service install`；默认关 | app 自己跑 whisper | ADR 0004；同一时间只能有一个转写程序 |
| D10 | 集成页 | 每种接口一张卡，片段来自 `cpa mcp setup`，一键复制；从不改别的 app 的配置 | 一键写进 Claude / Codex 的配置 | 改别人的配置要你自己看过；app 里没有客户端专用代码 |
| D11 | 菜单栏 | 图标 + 状态点 + 待归档数；关窗后只留菜单栏 | 只有窗口 | 不开窗也知道有没有要归的 |
| D12 | 打包 | 脚本组装 `.app`、ad-hoc 签名、装到 `~/Applications`；不进沙箱、不公证 | 沙箱 + Developer ID 公证 | 沙箱会挡住 `cpa` 写自己的目录；只在你的 Mac 上用 |
| D13 | 外观 | 深色，Mando 令牌和 sprite，系统字体 | 跟随系统浅色 / 深色 | 和设备、WhisperWoof 一致；浅色放 v1.2 |
| D14 | 语言 | v1 中文，文案集中在一张表；v1.2 加英文 | 一开始就双语 | 先把你每天用的做好 |
| D15 | 侧栏 | app 自己排的侧栏 + 页面 | 系统 `NavigationSplitView`（macOS 26 的玻璃侧栏） | 玻璃侧栏会把页面推到标题栏下、离屏截不到；自己排的每次一样、截图就是真窗口 |
| D16 | 建议（G3 以前） | 文字里正好提到一个项目时建议它，Enter 批准，写明是"文字里提到了" | 等本地小模型 | 需求是"点批准就行"；先用最笨也最可预期的办法，⌘Z 能撤 |

### 10.2 要你拍板

1. **合并 `feat/filing-p1`**（docs/23 P1）👤：今天 main 上的 `cpa` 还不会归档，打开 app 看到的是只读的"没项目的录音"。合并后在仓库目录里跑一次 `uv tool install --force ./daemon`，待归档、1–9、这台 Mac 的名字就都能用了。
2. **"登录时打开"要不要？** 👤 **默认：不加**。不开的话重启以后菜单栏没有数字，要自己打开一次 app；加的话 app 要写一个登录项，这是"只有你点的 cpa 命令会写东西"之外的第一个例外，所以等你说。
3. **docs/23 的 P2（WhisperWoof 里的 Cardputer 页）还做吗？** 👤 **默认：先不做**。这个 app 就是待归档的界面；WhisperWoof 想接，照集成页的做法接，和 Codex、Cursor 一样。docs/23 §5、D12 和 §7.1 的 P2-a/b 加一条带日期的修订；P2-c 里录音契约的向量照旧同步（WhisperWoof 转写要用）。备选：两个都做，WhisperWoof 页照 docs/23 §5。

## 修订记录

- **2026-10-26（评审之后）**：项目页：编号是 1–9 菜单（两下换号）、≡ 拖动、双击名字改名、key 只在悬停里；没输入名字时空行不放灰按钮，写怎么做；旧 cpa 时按钮都不出现，更新命令和设置里同一条；整页截图不再是黄块。英文：侧栏"2 still on the device"、名字长度不说 bytes、日期写月份名、"跟随 Mac"写现在是哪种、"交给 AI：排进哪个项目的待办"。

- **2026-10-26**：加"项目"页（§2.6：项目是有名字的分组，新建、改名、换号、连目录、删除，都经 `cpa project … --json`）；设备页的 1–9 表指向它；界面中文和英文（§2.7）；能力表加 `cpa project list`；契约副本换到 `e812e79`（加 `linked`、`folder` 和 2 个向量）。

- **2026-10-22（三份评审之后）**：顶上的数和侧栏、菜单栏一致（含别的电脑的项目）；筛选一行；按钮改名（排进待办 / 开工）、去掉没写出来的 `h`；简版只留能做的；转写失败说原因、可以重新转写；录音页标题用文字；设备页 1–9 全列、"不记网络"；这台 Mac 的名字改用 `cpa host name --json`（字节数 ≤24），"这台 Mac"只认名字；集成页先是"去向"（docs/25 §5.7），再是"给开发者"五张卡和"复制接入说明"，MCP 标签来自 cpa、片段只复制正文、加"其他 MCP 客户端"、命令带绝对路径并各自写能不能用；撤销进编辑菜单；退出码 2 只在探测时当"没有"；选的路径排第一；超时变硬、文件读离开主线程、播放一段一段读、旧的读取不覆盖新的、看不见时每分钟问一次状态；侧栏自己排（D15）；建议的权宜办法（D16）；契约副本换到 `8baed54`（加 `host` 文档和 9 个向量）；缺口表扩到 G1–G10；截图画真窗口、整页、菜单栏和 `--live`。
