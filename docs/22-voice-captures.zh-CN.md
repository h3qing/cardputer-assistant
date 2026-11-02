[English](22-voice-captures.md) · **中文**

# 22 · 语音采集：编辑器里按住 G0 说话，一条采集挂几段语音

> 状态：**设计稿第 2 版，待你拍板**（§15，3 件）· 日期：2026-10-22 · 角色：架构（语音采集）· 基于 `integrate/v041`（`e4c2c68`）。第 2 版按评审改过：23 条意见全部成立、全部采纳，范围砍掉约一半（见文末"评审意见怎么处理的"）。
> 需求（2026-10-22）："排队给 AI"很好用，但它的编辑器只能打字，键盘很小，打字不现实。文本框里也应该能按住 G0 说话；设备上转写不了，就先显示这里有一段语音，同一个编辑器里可以加好几段语音；总之要有一个结构把不同的语音便条记下来。细节交给设计者定。
> 依据：docs/04 §8.4；docs/13 §7.1、§9、§13；docs/14（`feat/destinations` `b5ad361`，未合并）§2、§3、§5.3、§5.4；docs/15 §1、§2；docs/17 §6.1、§6.3、M10；docs/18 §2 第 6 条、§3.4；docs/19 §6.6、§6.9、§10；docs/20 U4、U6、U15、U16、U17、U25、U26，D4、D5、D6、D8；ADR 0004；录音契约 v1（`protocol/recording/v1/README.md` 和 schema）；`protocol/README.md`；`protocol/limits.json`；`schema/v0/{outbox.item,common,hello,pong,queue.snapshot,rec.act}.schema.json`；固件 `lib/shell/{compose,shell,runtime_outbox,confirm}.*`、`lib/rec/{rec_core,rec_meta,rec_msgs}.h`、`lib/core/app_state.h`、`lib/input/keymap.cpp`、`modules/board/board_logic.cpp`；正在做的分支（只读，未提交）：`feat/ux-decisions` 的 `shell.cpp`、`compose.cpp`、`keymap.cpp`，`fix/memo-hold` 的 `g0_gesture.h`、`shell_g0.cpp`、`shell.cpp`、`rec_core.h`、`keymap.*`；daemon `hub/reduce.py`、`agents/claude_code/argv.py`、`recordings/{route,results,contract,history}.py`、`config/settings.py`、`service/launchd.py`；WhisperWoof PR #149–#152（都还开着）；本机 whisper.cpp 和沙箱实测（§4.5、§4.8）。
> 标记：**[源]** 读过的代码或文档 · **[测]** 本机实测 · **[算]** 用实测数字或脚本推算 · **[设]** 设计假设，可以改 · **[待]** 要用测试或真机验证 · **👤** 需要你。
> 范围：只写设计。没有接设备、没有开串口、没有改代码。屏幕示意按 240×135 画，一行约 40 个 ASCII 字符或 20 个汉字，颜色和零件用 docs/19 的 Mando 主题。
> 证据路径（临时文件，不进仓库）：`$VB` = `<scratch>/voicecap-bench`（测速脚本、合成音频、结果）；`$VC` = `<scratch>/vcap-calc`（行长 `lines.py`、`lines2.py`，字库 `efont.py`，沙箱 `conn.py`、`sbx/profile.sb`，语言 `lang/`）。**没有用你的录音**：测速用 macOS 合成的语音（`say -v Tingting`、`say -v Samantha`），识别率要你用真话复测（H-VC5）。

## 0. 一页摘要

1. **你要的**：打开"排队给 AI"（以及记一条、开 session）的编辑器时，也能按住 G0 说话；设备上转写不了，就先显示"这里有一段语音"；同一个编辑器里可以加好几段；还要一个结构把这些语音管起来。
2. **模型**（§2）：**一条采集 = 去向 + 可以不打的字 + 0–4 段语音片段**。片段是带采集引用 `cap` 的便条。条目里**写明**哪些段挂上了（`voice`）、哪些段拿掉了（`dropped`）；草稿没发出条目就关掉时，设备补发一个"墓碑"。Mac 从不靠猜：哪条都没提到的片段一律补一行笔记，不会悄悄消失。
3. **设备上**（§3）：编辑器里按住 G0 = 给这一条加一段，草稿里多一行 `① 0:07 已传 Mac`。设备上不显示转写文字（以后再做），只显示"这里有一段语音"。**只有 Enter 保存**：G0 单击不再保存，连按两下是免按住录一段。松手后直接打字、按 Enter 都行：这一下先结束这段，再交给编辑器。回复的编辑器、菜单、确认框、锁屏下仍照 D4 录独立的便条，录音页开口前就写明是哪一种。
4. **不会一下丢掉**：有语音时 Backspace、`` ` `` 都不会直接退出；放弃时 Enter = 打的字丢掉、语音存成笔记，Opt+D = 连语音一起丢。
5. **Mac 上转写**（§4）：新的独立程序 **`cpa-stt`**，和 WhisperWoof 一样只是录音契约的一个客户端；它跑你配置的一条本地命令（示例配置：whisper.cpp large-v3-turbo、自动判断语言、贪心、VAD），包在收紧的沙箱里（不能联网，只能写自己的临时目录和 Metal 缓存；实测不变慢）。**默认关**，你跑一次 `cpa-stt use` 才开。cpad 里没有任何转写引擎的代码。
6. **实测** [测]：自动判断语言时，10 秒的话约 1.7 秒出字，30 秒约 2.2 秒。强制 `-l zh` 时，一整句英文被转成乱码，所以默认 `auto`（§4.8）。
7. **拼起来**（§5）：几段都有结果了，cpad 把"打的字 + 各段文字"拼成正文，按去向写排队项、今日笔记一行。
8. **安全**（§5.4、§8）：语音**只排队，从不自动开 session**。从语音项开 session 要在设备上一页页翻完**全文**、在最后一页按 Enter；cpad 核对设备确实取过全文；一律 plan 模式；语音开的 session 不接受设备回复。回复类的排队项不能当新 session 开（顺手堵上今天就有的这个洞）。排队、开 session 的语音一条最多 40 秒，拼出来超过 500 B 的开 session 项不进队列。
9. **协议只新增，`v` 仍为 0**（§6）：`outbox.item` 加 `cap`、`voice`、`dropped`、`drop`；`file.items` 和 `.M` 加 `cap`；`queue.snapshot` 的项加 `voice`；一对新消息 `queue.get` / `queue.text`（取全文）。没有新快照，没有新 caps，不新增错误码。加了新字段的行最坏 1429 B（`outbox.item`，上限 2048 B）。录音契约 v1 只加可选字段和一个认领文件。
10. **分期**（§11）：**VC-1 只动 Mac**（cpa-stt，约 3 天），随时能发，单独就有用：你现在的 13 段便条一段都没转写。VC-2（协议 + 固件 + cpad，约两周）在 D4 和 memo-hold 两个分支合并以后。**要你拍板 3 件**（§15），都有默认值。

## 1. 你要的，和今天差在哪

### 1.1 需求拆开

- 编辑器里要能说话："排队给 AI"打开时今天只能在很小的键盘上打字（§3.1）。
- 编辑器里按住 G0 说话（§3.1、§3.2）。
- 设备上转写不了，先显示语音占位，转写在 Mac 上（§3.4、§3.6、§4）。
- 同一个编辑器加好几段语音，也就是一条采集挂几段（§2、§3.4）。
- 一个记下不同语音便条的结构：片段和采集的关系（§2）。
- 细节交给设计者：按 docs/20 的小设备原则定，只把改动你习惯的地方拿来拍板（§15）。

### 1.2 今天打字的采集怎么走 [源]

1. 编辑器是纯函数 `compose_key()`：只收 ASCII 0x20–0x7E [`compose.cpp:14-15`]，1–500 字节；Opt+N/Q/S 换种类，Opt+0–9 换项目；排队和开 session 要项目。
2. Enter 或 G0 单击（`keymap.cpp:81-82`）→ 保存。开 session、回复 → 确认框 → 在线发 `agent.spawn` / `agent.reply`，执行不了（离线、session 在等权限）就存成排队项（`confirm.cpp:42、65`）；记一条 → 笔记；排队给 AI → 排队项。
3. 条目和录音用**同一个 MonoId 序列**（`runtime_outbox.cpp:47`），MonoId 的前 13 位是开机号，同一次开机里 id 的字符串顺序就是产生顺序。
4. cpad：`item.received`，排队的再加 `queue.added`，记一条的马上写今日笔记一行（`hub/reduce.py:125-137`），回 `outbox.ack(stored)`。
5. 看板上选中排队项按 Enter，**不管 `intent`** 都发 `agent.spawn{qid}`（`board_logic.cpp:69-74`）；cpad 用排队项的全文当 prompt（`reduce.py` `_spawn_text`）。确认框里只看得到 ≤80 B 的摘要（`queue_text_max`）。

### 1.3 今天便条怎么走 [源]

1. 按住 G0 满 500 ms 开始录，松开结束；短于 1 s 丢掉；最长 5 分钟。`fix/memo-hold` 加了 600 ms 的松手宽限（宽限内再按下接着录同一段）和"连按两下 = 免按住录"（`g0_gesture.h`，`kDoubleTapMs = 350`），没有空格锁定。
2. 段结束时写 `.M`（≤512 B，未知字段忽略，`rec_meta.h`）。录音会话 id = 第 0 段的 MonoId，`Started` 事件就带着它（`rec_msgs.h:51`）。结束信号可能丢：收尾 10 s 没等到就回到 Idle（`rec_core.h` `kFinishGiveUpMs`）。重启时没写 `.M` 的段由开机扫描救回，标 `rcv`。
3. cpad 看 `hello` / `pong` 里的 `files{n, rev, live}`，有新段就 `file.list` / `file.get` 拉过来，落地、校验、`file.del`。`pong` 每 5 s 一次。
4. 最后一段落地写 `manifest.json`；之后由转写程序写 `transcript.*`、`result.json`；cpad 每 3 s 扫一次，看到 `result.json` 记 `rec.result`，24 h 没结果记 `rec.stale`，两种都写今日笔记一行（`recordings/route.py`、`results.py`）。

### 1.4 缺口

| 缺什么 | 现状 | 证据 |
|---|---|---|
| 编辑器下按住 G0 | v0.4.0 什么都不录。`feat/ux-decisions` 正按 D4 改成"录一段独立的便条，草稿留着" | docs/20 U4；该分支 `shell.cpp:120` |
| 草稿和录音的关系 | 没有 | — |
| 设备上转写 | 做不到：没有 PSRAM，可用堆约 180 KB；最小的 whisper 模型 77.7 MB | docs/19 §10；[测] |
| Mac 上转写 | 没有任何程序在写 `result.json`：15 个录音目录（13 段便条 1.0–50.7 秒、2 场会）都没有。WhisperWoof #149–#152 没合并 | `~/CardputerNotes/recordings/*/manifest.json`（只读了 manifest） |
| 开 session 前看全文 | 确认框只有 80 B 摘要，发出去的是 500 B 全文 | §1.2 第 5 条 |
| 回复类排队项 | 看板照样能把它开成一个**新** session | §1.2 第 5 条 |

## 2. 模型：一条采集 = 去向 + 可以不打的字 + 0–4 段片段

### 2.1 定义

```
Capture {
  ref      条目 id（outbox.item 的 MonoId）
  dest     去向：daily / todo / queue / session —— 你开的编辑器（docs/14 合并后：编辑器当前的去向）
  project  项目（queue、session 必须有）
  typed    你打的字，可以没有
  cap      采集引用：第一段开录前从共用的 MonoId 序列取的 id
  voice    挂上的片段（录音会话 id），按录的顺序，0–4 个
  dropped  拿掉的片段，0–4 个，和 voice 不重叠
}
Clip = 一段 mode=memo 的录音，第 0 段的 .M 带 cap
```

- **`cap` 在第一段开录之前就定了**：设备取一个 MonoId 当 `cap`，放进 `StartSpec`，写进每一段的 `.M`。Mac 收到片段时，不用等条目就知道"这是某条采集的一段"。
- **草稿在 `Started` 时就记下这一段**（会话 id 这时已知），状态是"录音中"；结束时补上时长；**只有 `Discarded`**（短于 1 s，什么都没存）才把它从草稿里去掉。结束信号丢了（收尾 10 s 放弃），这一段照样留在草稿里、照样进 `voice`，由 Mac 判断它到底来没来（§5.2）。
- **每个有 `cap` 的草稿，最后都恰好发一个提到它的 `outbox.item`**：要么是条目本身（记一条、排队），要么是墓碑（§2.5）。所以每一段的结局都由设备写明，Mac 不推断。
- **顺序可以核对**：同一次开机里 `cap` < 每一段的会话 id < 条目 id。草稿只在内存里，跨不了重启，所以三者的开机号必须相同（§5.7）。

### 2.2 片段为什么是"带 `cap` 的便条"，不是新的 `mode`

| 做法 | 旧的一端 | 结论 |
|---|---|---|
| `mode:"clip"` | **整份拒收**：录音契约把"未知的 `mode`"列为必需的非法向量，WhisperWoof 会写 `failed/E_INVALID`；v0.4.1 cpad 解 `file.items` 时 `mode` 是枚举，回 `E_BAD_FIELD`，拉取卡住 [源] | 不要 |
| 一条采集一个录音会话，几段续录 | 要改录音引擎、恢复扫描、段序号 | 不要 |
| **便条 + 可选字段 `cap`** | `.M`、`file.items`、`manifest.json` 一律忽略未知字段，当普通便条 [源] | **采用** |

字段名不用 `clip`：docs/14 里 `clip` 已经是"剪贴板"去向的 id。

### 2.3 每种编辑器变成什么

| 编辑器 | 有语音时按 Enter | 设备发什么 | 几段都有结果以后 |
|---|---|---|---|
| 记一条（Opt+N）、待办（Opt+T） | 存，不用打字 | `outbox.item{kind:"note", dest?, cap, voice, dropped?}` | 今日笔记一行：打的字 + 各段全文 + 音频链接（待办是复选框）|
| 排队给 AI（Opt+Q） | 存 | `outbox.item{kind:"queue", intent:"todo", cap, voice, dropped?}` | 排队项，摘要以"语音·"开头；今日笔记一行 |
| 开 session（Opt+S） | **不弹确认框，直接存成排队项**：这时还没有文字，什么都开不了 | `outbox.item{kind:"queue", intent:"spawn", cap, voice, dropped?}` | `intent=spawn` 的语音排队项，到看板上翻完全文才开（§3.8） |
| 回复 | **不挂语音**：按住 G0 照 D4 录独立的便条（§3.1） | — | — |

- 开 session 变成排队项，和 docs/14 §5.4 第 1 条一致："从 outbox、便条来的 `session` 一律变排队项"。
- 回复不挂：语音回复会变成一个只能记录、永远送不到 session 的排队项，session 一直等着（docs/17 §17 第 6 条的语音回复放到 VC-3）。
- 零打字（不开编辑器，按住 G0 时按 Q / S）这一版不做，和 docs/14 F5 的"按住 G0 再按字母"一起做（VC-3）。

### 2.4 片段的结局：Mac 只看设备写明的

| 结局 | 怎么认 | Mac 上 |
|---|---|---|
| **挂上** | 在某个条目的 `voice` 里，拼接时已经有结果 | 文字进那条采集的正文 |
| **补充** | 在 `voice` 里，但结果比拼接晚到（§5.2） | 今日笔记补一行"补充"：文字 + 音频链接；**不进队列** |
| **拿掉** | 在某个条目或墓碑的 `dropped` 里 | 不写笔记、不进队列；记 `rec.hidden`，录音应用里看不到（和"隐藏"一样，`cpa rec unhide` 能找回）|
| **没挂上**（孤儿）| 带 `cap`，哪个条目、墓碑都没提到它，**而且**设备换了开机号重新连上、outbox 已清空（`pong.pending == 0`） | 今日笔记一行"语音片段（没挂上条目）"：文字 + 音频链接 |
| **等着** | 以上都还不是 | 什么都不写；`cpa status` 里算"在等条目的片段" |

- **从不靠"`cap` 相同但不在 `voice` 里"推断删除**。
- **同一次开机里不判孤儿**：草稿开着过夜也没关系，条目一到就认领。重启以后 outbox 里存着的条目会先发完（`pending == 0`），所以"换了开机号、outbox 清空"以后还没人提到的片段，才真的没人要了。
- 什么都不丢：每一段音频都留在录音目录里，按录音契约的保留期处理（WAV 30 天，转写永久）。

### 2.5 墓碑：草稿关了却没发条目

有 `cap` 的草稿在下面几种情况下关掉、却没有条目：放弃时选了"连语音一起丢"（§3.3）；或者语音全拿掉了，最后走的是确认框 → `agent.spawn`（在线开 session）；或者语音全拿掉了、字也删光了直接退出。这时设备**自动**发一个墓碑：

```
outbox.item{kind:"note", body:{text:"（语音）", cap, dropped:[…], drop:true}}
```

cpad 收到：不写笔记、不进队列，`dropped` 里的片段记 `rec.hidden`，这条采集结束。墓碑走 outbox，离线也不丢。旧 cpad 不认 `drop`，会写一行"（语音）"笔记，只在"新固件 + 旧 cpad"时出现，没有害处（§6.5）。

## 3. 设备上

### 3.1 按住 G0

| 情况 | 按住 G0 录的是 | 录音页开口前写 | 松手以后 |
|---|---|---|---|
| 编辑器在最上层（没锁、菜单和确认框都没开）；种类是记一条、待办、排队给 AI、开 session；去向是今日笔记、待办、队列、session；不到 4 段；（排队、开 session）语音还没满 40 秒 | **片段**，挂到这一条 | "语音片段 · 第 2 段"，"加到 (给AI · #cardputer)" | 草稿多一行 `② 0:07`；太短（<1 s）提示"太短，没加上" |
| 同上，但已经 4 段 | 独立的便条（D4） | "语音便条" | 提示"最多 4 段：这段存成了便条" |
| 同上，但排队、开 session 的语音已满 40 秒 | 独立的便条 | "语音便条" | 提示"这一条的语音满 40 秒了：这段存成了便条" |
| 回复的编辑器 | 独立的便条 | "语音便条" | 提示"回复要打字：这段存成了便条" |
| 去向是剪贴板、粘贴、插件、文件（docs/14 合并以后） | 独立的便条 | "语音便条" | 提示"这个去向不收语音：这段存成了便条" |
| Tab 菜单开在编辑器上 | 独立的便条 | "语音便条" | 提示"菜单开着：这段存成了便条" |
| 确认框开着 | 独立的便条 | "语音便条" | 提示"这段存成了便条；要加到这一条，先 ` 回编辑器"（D6）|
| 锁着（不管下面是什么） | 独立的便条 | "语音便条" | 和今天一样 |
| 没开编辑器 | 便条（今天的行为） | "语音便条" | — |

- **一条规则**：看得见草稿、而且这一条收得下，才挂；否则就是 D4 的独立便条，录音页上开口前就写明是哪一种。挂不挂**不看 Mac 端是什么版本**（§6.5），同一个手势的意思不随看不见的状态变。
- **菜单**：`feat/ux-decisions` 的 `dispatch()` 先把按键交给菜单（`shell.cpp:117`），菜单关掉后可能重放这次按住。所以"菜单开着"要在菜单收到 G0 按住**之前**记下来，重放时照独立便条录，有测试锁住。
- **为什么回复不挂**：见 §2.3。

### 3.2 G0 的其他用法、松手宽限、录片段时按别的键

- **G0 单击不再保存**（今天 `keymap.cpp:82` 是保存）。`fix/memo-hold` 让"连按两下"成了免按住录音；编辑器里单击要是马上保存，连按两下就成了"存走草稿，再打开一条新笔记"（第二下落在全局的快速笔记上）。所以编辑器里：单击只提示"Enter 保存 · 按住 G0 说话"；350 ms 内再按一下 = **免按住录一段**（挂不挂照 §3.1），再按一下 G0 结束。**Enter 是唯一的保存键，G0 在哪儿都是说话**（§15 第 1 条）。
- **松手宽限**（600 ms）：宽限内再按下，接着录同一段。要加第二段：松手、等录音页收起（约 0.6 s）、再按住。
- **录片段时按别的键**（docs/20 U17：按了就要有反应）：

| 什么时候 | Enter | `` ` `` | 字母、数字、Backspace | Tab、Opt+L |
|---|---|---|---|---|
| 按着 G0 | 提示"松开 G0 再按" | 同左 | 同左（数字不改项目："项目和去向看编辑器"）| 照 memo-hold：菜单盖在录音上 / 锁屏、接着录 |
| 松手宽限、免按住、收尾 | **先结束这一段**，然后保存（带上这一段）| 先结束这一段，然后弹"放弃这一条？" | 先结束这一段，然后照常进编辑器（打字、删字）| 同上 |

  - "先结束"：宽限和免按住的录音马上收尾；收尾中的就等它收完。
  - 按键按顺序排着（最多 8 个），等这段**定了**（`Saved` / `Discarded` / `Failed`，或 10 s 放弃）再交给编辑器；第 9 个起提示"等这段存好"。所以"松手、马上按 Enter"存下的条目一定带着刚录的这段。
- **D8（预录，没定）**：开头约 0.65 秒录不进去，每一段都一样；先看到 ● REC 再说。预录做了，片段自动受益。

### 3.3 编辑器里的键

| 键 | 没有语音（今天） | 有语音 |
|---|---|---|
| Enter | 保存 | 保存；没打字也行（占位正文，§6.3）|
| G0 单击 | 保存 → **改成**提示 | 提示"Enter 保存 · 按住 G0 说话" |
| Backspace（没字时）| 退出（issue #15）| **不退出**，提示"Opt+D 拿掉一段 · ` 放弃" |
| `` ` ``（D5）| 没字时直接退出，有字时问 | **一律问**"放弃这一条？"（下图）|
| Opt+D | — | 拿掉**最后一段**，提示"已拿掉 ②"；没有语音时提示"没有语音可拿掉" |
| Opt+N / T / Q / S | 换种类 | 换种类；语音超过 40 秒时不能换成排队、开 session（提示"语音超过 40 秒：只能记一条"）|
| docs/14 的 Opt+字母 | 换去向 | 只能换成今日笔记、待办、队列、session；别的提示"这个去向不收语音" |

- 编辑层的 Opt+D 今天没有绑定；和列表里"Opt+D = 处理掉"一个意思。叫"拿掉"不叫"删"：音频还在 Mac 上，只是不进这一条（§2.4）。
- 有 `cap` 的草稿不管怎么关，都恰好发一个条目或墓碑（§2.5）。

放弃时（有语音）：

```
╭──────────────────────────────────────╮
│放弃这一条？    语音 2 段 · 0:19       │
│                                      │
│[Enter] 打的字丢掉，语音存成笔记       │
│[Opt+D] 连语音一起丢掉                 │
│[`] 回去接着写                         │
╰──────────────────────────────────────╯
```

- Enter：发 `note{text:"（语音）", cap, voice, dropped?}`，语音进今日笔记，什么都不丢。默认键选最不丢东西的那个。
- Opt+D：发墓碑，所有片段都进 `dropped`。
- `` ` `` / Fn+`` ` ``：回编辑器（和今天的放弃提示一样）。

### 3.4 编辑器屏幕（Mando 主题）

docs/19 §6.9 的编辑页：标题在 (4,13) 的中性胶囊里；正文在 Plain 卡 (4,28,232,92) 里，内边距 6，5 行；卡底一行左边放警告、右边放字数。挂了片段以后：

```
[头] ↑0                         周三 09:41 94%
(排队给 AI → cardputer)
╭──────────────────────────────────────╮
│fix wifi page_                        │  打的字（Ink），至少 1 行
│① 0:07  已传 Mac                      │  片段：胶囊里是序号和时长
│② 0:12  待传                          │
│                                      │
│                                      │
│语音 2 段 · 0:19/0:40          14/500 │
╰──────────────────────────────────────╯
Enter保存 G0按住说话 Opt+D拿掉 Tab菜单
```

- **片段行**：开头是 7×9 的手画麦克风（`tools/theme/gen_icons.py`，和状态栏的小狗头同一条管线，Accent 色；efontCN_12 画不出 🎤）；圆圈序号（①–④ 字库里有 [测]）和时长放在高 12 的中性胶囊里；后面是状态（§3.6，Ink3）。
- **行数**：每段 1 行，打的字占剩下的，至少 1 行；4 段 + 1 行字正好 5 行。放不下时打的字只显示末尾和光标（和今天一样）。
- **只有语音、没打字**：打字那一行写 `（可以不打字）`（Ink3）加光标。
- **卡底左边**：`语音 N 段 · 总时长`；排队、开 session 写成 `0:19/0:40`，剩不到 10 秒时用 Warn。开 session 以 `-` 开头的警告优先。
- **提示栏**：有语音时 `Enter保存 G0按住说话 Opt+D拿掉 Tab菜单`；空编辑器时把今天的 `Opt N/Q/S 类型` 换成 `G0按住说话`（换类型留在菜单"本页"里）——编辑器里能说话这件事不显眼，空编辑器就该教这一下。两条都要过 `hint_fits` 第一档，放不下就去掉 `Tab菜单` [待 测试]。
- **不动画**：片段行只在状态变了时重画（docs/19"只有状态变了才动"）。

### 3.5 录片段时（docs/19 §6.6 的便条页，盖在编辑器上）

```
[●0:07] [头] ↑0                  周三 09:41 94%
  ▟▙      语音片段                   第 2 段
  ▛▜      ● 0:07  REC              还能 0:21
  ▐▌      ┃┃·┃┃┃·┃┃┃┃┃·┃┃┃┃·┃┃┃┃┃·┃┃┃
 (在听)   加到 (给AI · #cardputer)
[G0]松开加上  连按两下=免按住
```

- 和便条页只差几处：标签"语音片段"、"第 N 段"、"存到"换成"加到"，胶囊里是编辑器的种类和项目；没有 `1-9选项目`。
- **排队、开 session 有 40 秒总额度**：右边显示"还能 0:21"；用完这一段自己结束，结束页写"满 40 秒，这段到此"（U6：自己停下的都要说为什么）。设备把剩下的额度当这一次录音的上限传给录音引擎（`StartSpec` 加 `max_ms`，引擎今天已经会在 5 分钟上限处自己停）。记一条、待办每段照便条的 5 分钟。

### 3.6 片段行的状态（设备只写自己知道的）

| 状态 | 设备怎么知道 | 写什么 |
|---|---|---|
| 时长未知 | 结束信号丢了（收尾 10 s 放弃） | `时长未知 · 照样带上` |
| 在设备上 | 本机文件索引里还有这段 | 在线：`待传`；离线：`连上 Mac 后传` |
| 已传 Mac | 索引里没了（Mac 拉走后 `file.del`） | `已传 Mac` |

- **不用等**：Enter 随时能按。Mac 收到条目后会等片段到齐、转写完再拼（§5）。
- 设备上**看不到转写文字**：要一条新的 Mac→设备快照，放到 VC-3。这一版里转写文字第一次出现在设备上，是看板上的"语音·…"摘要和开 session 前的全文（§3.8）。

### 3.7 保存以后

| 情况 | 提示 |
|---|---|
| 排队给 AI，有语音 | 已存 · 转写后进队列 |
| 开 session，有语音 | 已排队 · 转写后到看板开（plan）|
| 记一条、待办，有语音 | 已保存 · 转写后进今日笔记 |
| 放弃，语音存成笔记 | 打的字已丢 · 语音存成笔记 |
| 放弃，连语音一起丢 | 已丢掉（音频在 Mac 上，已隐藏）|

### 3.8 看板和确认框：开 session 前翻完全文

- **摘要**：语音排队项的摘要由 cpad 生成，`语音·` 加**语音部分**的开头（不是打的字的开头），≤80 B（§5.3）。`queue.snapshot` 的项多一个 `voice:true`。
- **开 session 要先看全文**：在看板上选中语音项按 Enter，设备发 `queue.get{qid}`，拿回全文 `queue.text`（≤500 B），在确认框里分页显示，每页 5 行：

```
╭──────────────────────────────────────╮
│开 session · plan · 语音       1/3    │
│fix wifi page 把设置页的WiFi部分改成英 │
│文，再跑一遍test shell的单元测试，然后 │
│……                                    │
│                                      │
│[Enter]下一页  [`]不开                 │
╰──────────────────────────────────────╯
```

  - Enter 翻到下一页；**只有最后一页**的 Enter 才是"开"（写"Enter 开（plan：只出计划）"）。每一页都有确认框的 400 ms 防误触，连按也翻不过去。
  - 看到的就是要发的：全文原样分页，不删字、不省略；截断过的结尾带"…（截断）"（§5.3）。
  - 没拿到全文（离线、超时、旧 cpad）：写"没拿到全文：开不了"，不开。
  - cpad 也核对：这次连接里没给设备发过这一项的全文，就拒绝 `agent.spawn{qid}`（§5.4）。
- **回复类的排队项**（`intent=feedback`，不管是不是语音）：看板上按 Enter 只提示"回复类的不能开新 session：在 Mac 上处理"；Opt+D 处理掉照旧。cpad 也拒绝（§5.4）。
- "可能已开过，先看 Sessions"只给 `uncertain`（U25）用，和语音无关。

### 3.9 设备端要改什么（VC-2f）

| 文件 | 改什么 |
|---|---|
| `lib/rec/rec_msgs.h`、`rec_core.*`、`rec_meta.*` | `StartSpec` 加 `Opt<MonoId> cap` 和 `max_ms`；`.M` 第 0 段写 `cap`；`Started` / 结束事件的会话 id 交给 Shell |
| `lib/shell/compose.*` | `ComposeState` 加 `cap` 和 `clips[4]{id, ms, st}`、`dropped[4]`；`Started` 加段、`Discarded` 去段；有语音时 `cancel()`、`delete_char()` 不退出；Opt+D 拿掉最后一段；有语音时空文字能存；放弃提示的 Enter / Opt+D；种类、去向的限制；关草稿时的墓碑 |
| `lib/input/keymap.cpp`、`lib/shell/shell*.cpp`（`shell_g0.cpp`）| 编辑层去掉 G0 单击 = 保存，单击提示、连按两下免按住；§3.1 的判断（含"菜单开着"在重放前记下）；录片段时按键先结束这段、排队 8 个、定了再交给编辑器；开 session 有语音时不进确认框，直接存排队项 |
| `lib/shell/shell_render.cpp`、`lib/theme`、`tools/theme/gen_icons.py` | 片段行、麦克风小图、卡底、提示栏、录片段页的几处文字、放弃提示 |
| `lib/shell/confirm.*`、`modules/board/board_logic.cpp` | 语音项先取全文、分页、最后一页才开；回复类排队项不开 |
| `lib/protocol/*` | 编码 `body.cap/voice/dropped/drop`、`file.items[].cap`、`queue.get`；解码 `queue.snapshot[].voice`、`queue.text` |
| `lib/core/app_state.h` | 排队项加 `voice`（每项 1 B）|

### 3.10 为什么不在设备上转写

没有 PSRAM，空闲堆约 180 KB [源 docs/19 §10]；最小的 whisper 模型 `ggml-tiny.bin` 77.7 MB，large-v3-turbo 1.62 GB [测]。设备只录、存、显示，这是 docs/02 就定下的分工。

## 4. Mac 上：转写程序 `cpa-stt`

### 4.1 谁来转写

| 方案 | 做法 | 结论 |
|---|---|---|
| **A. 独立的 `cpa-stt`** | 一个单独的程序、单独的 LaunchAgent，只读写录音目录，和 WhisperWoof 一样是录音契约的一个客户端；它跑你配置的一条本地命令 | **采用**。cpad 不多任何角色、不碰转写引擎；转写再慢也拖不住串口（另一个进程）|
| B. WhisperWoof | #149–#152 合并后，开它的 Cardputer 收件箱 | 仍然可以，契约不变；两个只能开一个（§4.10）|
| C. cpad 自己跑转写命令 | — | 不要：cpad 里会长出引擎专用的代码（ADR 0004），转写和串口在同一个进程里 |
| D. 两个同时开 | — | 不允许：契约是单写者（§4.10）|

### 4.2 `cpa-stt` 是什么

- 只用 Python 标准库，放在仓库顶层 `stt/`（评审建议的 `tools/cpa-stt` 改了位置：它要装到你的 Mac 上，`tools/` 下都是开发工具）。（VC-1 落地时放进了 `daemon/`，见 §4.13。）
- 自己的 LaunchAgent `com.cardputer-assistant.cpa-stt`，写法照 cpad 的 `service/launchd.py`；自己的配置 `$CPA_HOME/stt/config.json`、日志 `$CPA_HOME/stt/log/`。
- 和 cpad 之间**只有录音目录里的文件**：它读 `manifest.json`，写 `status.json`、`transcript.*`、`result.json`；cpad 的 3 s 扫描照旧捡 `result.json`。（另外只读 cpad 的 `config.json` 和 `projects.json`，见 §4.13。）
- 它不知道条目、队列、去向。它只认 `manifest.json` 里的 `cap`，用来"有人在等的片段先转"。

### 4.3 命令约定（`cpa-stt` 和转写命令之间）

任何本地程序守这几条就能用（whisper.cpp、sherpa-onnx、你自己的脚本）：

| 项 | 约定 |
|---|---|
| 输入 | 一个 WAV：16 kHz、16 bit、单声道 PCM，44 B 头（就是 `seg-000.wav`，不复制）。占位符 `{wav}`（必须恰好一次）、`{offset_ms}`（可选，跳过开头的 `lead_ms`）。占位符只能占 argv 的一整个元素 |
| 输出 | stdout，UTF-8，每行一句；行首可以有 `[HH:MM:SS.mmm --> HH:MM:SS.mmm]`，有就当这句的 `t_ms`，没有记 0。空输出 = 没听到 |
| 退出码 | 0 成功；非 0、被信号杀掉、超时都算失败 |
| 运行环境 | 不经 shell；`argv[0]` 是存在的绝对路径；环境变量只给 `PATH=/usr/bin:/bin`、`HOME`、`LANG=en_US.UTF-8`（cpa-stt 自己有 `__CF_USER_TEXT_ENCODING` 时也传下去，免得 CoreFoundation 去读真实主目录的 `~/.CFUserTextEncoding`，#71）；工作目录是一个空的 0700 临时目录；stdin 关着；stdout 最多读 256 KiB（VC-1 改成随时长增加，§4.13）；**stderr 丢掉，不进日志**（里面可能有转写文字）|
| 沙箱 | 一律包在 §4.5 的沙箱里，**没有关掉的选项** |
| 超时 | 每段 `max(timeout_s, 30 + 2 × 时长秒数)`，`timeout_s` 默认 120 |
| 并发 | 一次一段，每段一个进程（模型加载 0.4 s，换来不常驻 2 GB 内存）|

这份约定是 `cpa-stt` 自己的配置格式，不是 cpad 对外的契约；cpad 对外仍然只有文件布局和 MCP 两份（§4.12）。

### 4.4 配置：引擎参数是数据，不是代码

```json
{
  "provider": "off",
  "recordings_dir": "~/CardputerNotes/recordings",
  "argv": null,
  "sandbox_mach": ["com.apple.MTLCompilerService", "com.apple.system.opendirectoryd.membership"],
  "meetings": false,
  "timeout_s": 120,
  "drop_lines": []
}
```

- `provider`：`off`（**默认**）/ `command`。没有 `auto`，不去任何地方找模型。
- 仓库带一份**示例配置** `stt/examples/whisper-cpp.json`（数据）：

```json
{"provider": "command",
 "argv": ["/opt/homebrew/bin/whisper-cli", "-m", "<MODEL>", "-l", "auto", "-bs", "1", "-bo", "1",
          "-np", "--vad", "-vm", "<VAD_MODEL>", "-f", "{wav}"]}
```

  你把 `<MODEL>`、`<VAD_MODEL>` 换成自己机器上的路径（比如这台 Mac 上已有的 `~/.cache/openwhispr/whisper-models/ggml-large-v3-turbo.bin`），跑 `cpa-stt use 文件`。`cpa-stt` 只检查路径存在、占位符合规，然后跑一次自检（§4.6）。它**不认识** whisper.cpp：换成别的引擎只是换一份配置。
- `-l auto`：强制 `-l zh` 时英文句子会变成乱码（§4.8）；`auto` 每段多约 0.5 s。
- 不给 `-ot {offset_ms}`：whisper.cpp 1.8.3 把 `-ot` 算在 VAD 留下的语音上，不是算在文件上。和 `--vad` 一起用时，真便条开头的一两个词被切掉，有一条整条变成幻觉句、被过滤后成了"没听到"（§4.13）。150 ms 的 `lead_ms` 交给 VAD 和 whisper 自己；`{offset_ms}` 留给把偏移算在文件上的引擎。
- 不给 `--prompt`：提示词能纠正个别词，也会带错词、在静音上原样冒出来（§4.8、§4.9）。要用就自己写进 `argv`，`cpa-stt` 会把它加进幻觉句过滤。（VC-1 改了：示例配置把项目名当提示词，见 §4.13。）
- `meetings`：默认 `false`，只转便条和片段。会议长、有别人的话，交给 WhisperWoof 的会议流水线 [设]。（VC-1 改成默认 `true`，见 §4.13。）
- `sandbox_mach`：沙箱里允许连的系统服务；默认这两个是给 whisper-cli 用 Metal 实测出来的（§4.5）。别的引擎要别的服务，在这里加。
- `drop_lines`：你自己补的幻觉句。

### 4.5 沙箱：挡联网、挡乱写 [测]

每次运行生成这样一份配置（`WD` 是这次的临时工作目录，`C` 是 `getconf DARWIN_USER_CACHE_DIR`）：

```
(version 1)(allow default)(deny network*)
(deny file-write* (require-not (subpath WD)))
(allow file-write* (literal "/dev/null") (literal "/dev/dtracehelper") (subpath C/com.apple.metal))
(deny mach-lookup)
(allow mach-lookup (global-name "com.apple.MTLCompilerService")
                   (global-name "com.apple.system.opendirectoryd.membership"))
```

| 实测（whisper-cli 1.8.3，macOS 26，Apple 芯片） | 结果 |
|---|---|
| 10 秒片段，`-l zh` 贪心，3 次 | 1.15 / 1.15 / 1.17 s（不包沙箱 1.16 s）；文字和不包沙箱的逐字相同 |
| 30 秒片段，同上 | 1.64–2.20 s（不包 2.18 s）；文字相同 |
| 10 秒 / 30 秒 / 6 秒英文，`-l auto` | 1.66–1.70 / 2.18–2.75 / 1.65 s |
| Metal 缓存目录不让写 | 每次重新编译 Metal 库，**多 5 秒**（"loaded in 4.988 sec"）|
| 不放行 `MTLCompilerService` | Metal 建库失败，whisper-cli 直接崩（退出码 134）|
| 不放行 `opendirectoryd.membership` | 也是每次多约 5 秒 |
| 沙箱里连本机端口 / 解析域名 | `Operation not permitted` / `Could not resolve host` |
| 沙箱里写 `$HOME` | `Operation not permitted`；写工作目录可以 |
| 沙箱里 `open -a TextEdit` | 找不到应用（LaunchServices 连不上）|

- **它挡什么**：命令自己联网、通过系统服务（下载服务、LaunchServices 的 `open`）间接联网、往工作目录和 Metal 缓存以外写文件。
- **它不挡什么**：读文件。命令能读你的文件，但送不出去、也写不到别处。文档和 CLI 一律说"挡直接联网和乱写"，不说"禁网"。
- `sandbox-exec` 已被苹果标为不推荐，但 macOS 26 上还在 [测]。**找不到它就停**：写 `blocked{engine}`，绝不在沙箱外跑。
- 放行的两个服务是对 whisper-cli 实测的，在 LaunchAgent 下要再测一次（H-VC6）。

### 4.6 运行规则

- **找活**：启动时和之后每 2 s 扫一遍录音目录 [设]，挑"有合法 `manifest.json`、没有 `result.json`、`mode=memo`（`meetings:true` 时含会议）、也没有别人在写"的目录。
- **顺序**：manifest 带 `cap` 的片段 → 其他便条（新的先）→ 会议。
- **自检**：启动时、换配置后、以及任何一次失败之后，先对内置的 1 秒静音 WAV 跑一次命令。
  - **自检失败 = 环境的问题**（模型或二进制不在、`sandbox-exec` 没了、Metal 起不来）：只写 `status.json` `blocked{reason}`，`reason` 是 `model_missing`（路径不在）或新的 `engine`（命令跑不起来），**不写 `result.json`**。修好以后（`cpa-stt retry`，或改配置自动触发）这些目录照常转，不会因为环境坏过一次就永久失败。
  - **自检通过、只有这一段失败 = 这个文件的问题**：隔 30 s、2 min、8 min 再试，三次都不行写 `result.json` `failed{E_STT, retryable:true}`；WAV 头不对写 `failed{E_AUDIO, retryable:false}`。和 WhisperWoof 的退避一样 [源 PR #151]。
- **重跑**：`cpa-stt retry` 清掉 `blocked`、删掉**自己写的** `failed` 的 `result.json`（`pipeline.app == "cpa-stt"`），重新转。这是你在终端里下的命令，等于契约里"用户删掉 `result.json`"。
- **日志**：只记会话 id、时长、耗时、退出码，不记文字。
- **和 cpad 互不阻塞**：两个进程。cpad 的串口收发（ack、pong）不受转写影响；集成测试量一段 60 s 转写期间 cpad 的 pong 延迟（§12.1）。

### 4.7 写什么文件（照录音契约 v1 的"转写程序"角色）

| 文件 | 内容 |
|---|---|
| `status.json` | 开始前写 `transcribing`，带 `by:"cpa-stt"`（契约新加的可选字段，§7）。重启以后靠 `by` 认出自己写的，不靠记 mtime |
| `transcript.json` | `{v:1, session, attempt:1, lead_ms, engine:{stt:<argv[0] 的文件名>}, sentences:[{t_ms, text}]}` |
| `transcript.md` | 每行 `- [MM:SS] 文字 ^tHHMMSS`，同一秒第二句起加 `-2` |
| `result.json` | **最后写**：`done`、`level:"transcript"`、两个 `outputs` 带 sha256、`pipeline:{app:"cpa-stt"}`、`error:null`；便条和片段另加 `memo:{text, intent:"note"}`（全文，截到 2000 字）|

- 每个文件：`.` 开头的临时文件、0600、`O_NOFOLLOW`、fsync、rename；`result.json` 用 `link()` 放到位，**已经有就放弃**，绝不覆盖别人的结果。
- `memo.text` 让 cpad 的 `rec.result` 直接带上 `memo_text`，拼采集只看事件。`intent` 永远是 `note`：`cpa-stt` 没有 LLM。

### 4.8 实测：whisper.cpp large-v3-turbo [测]

环境：Apple 芯片的 Mac，macOS 26；Homebrew `whisper-cpp 1.8.3`，Metal；模型在文件缓存里。音频：合成语音，16 kHz、16 bit、单声道（和设备一样）。每次起一个新进程，各 3 次取中位数。

| 片段 | `-l zh` 默认（beam 5）| `-l zh` 贪心 | `-l auto` 贪心 |
|---|---|---|---|
| 3 s | 1.17 s | 1.17 s | — |
| 10 s | 1.17 s | 1.19 s | 1.67 s |
| 20 s | 1.69 s | 1.17 s | — |
| 30 s | 1.69 s | 1.67 s | 2.18 s |
| 45 s | 2.20 s | 2.20 s | — |
| 60 s | 2.70 s | 2.20 s | 约 2.7 s [算] |

- 拆开看：加载模型 380–470 ms；encoder 每个 30 秒窗口约 390 ms；`auto` 多跑一遍 encoder 判语言。峰值内存约 1.98 GB。
- 贪心和 beam 5 在 ≤20 秒的片段上转出的字完全一样；贪心更快，在 30 秒接缝处丢得更少。
- **语言**：
  - 一句 6 秒的英文（"Please fix the wifi settings page and then run the test shell unit tests…"）：`-l zh` 转成"нут果需要Amazon後Projut權"（2.68 s）；`-l auto` 和 `-l en` 逐字正确（1.65 s / 1.16 s）。
  - 中英混说（"把 settings page 改成英文，然后跑一下 npm run test，再开一个 pull request"）：`auto` 和 `zh` 的结果相同（"Settings Page""NPM Runetest""Pool Request"）；`-l en` 会输出繁体字。
  - 所以默认 `auto`。
- **中文识别**（合成语音，只作参考）：中文基本都对；英文词错得多："AI"→"RE"，"cardputer"→"Cartputer"。
- **提示词**：只给项目名能纠正 cardputer；给长词表会带出新错（"rec act"→"WiFi"）。示例配置不给（§4.4）。
- 你现在的 13 段便条共约 159 秒（中位数 7.4 秒）[源 manifest]，全部转一遍约 20–25 秒 [算]。
- **冷启动**（模型不在文件缓存里）没测，估计多 0.3–1 秒 [待 H-VC6]。

### 4.9 两个要防的问题 [测]

**30 秒接缝丢句**：45 秒和 60 秒的片段，默认解码在 30 秒接缝处整句丢了（约 17 个字）；贪心只丢了最后三个字。对策：示例配置用贪心；排队、开 session 每条语音最多 40 秒；记一条的长段按静音切块放到 VC-3 [待 真人录音]。

**静音和噪声上的"幻觉"**：

| 输入 | 不加 VAD | 加 VAD |
|---|---|---|
| 5 秒粉红噪声 | "明镜需要您的支持 欢迎订阅明镜" | 空 |
| 5 秒纯静音 | "优优独播剧场——YoYo Television Series Exclusive" | 空 |

三道拦截，任何一道拦住就算"没听到"：示例配置带 `--vad`；`cpa-stt` 跑命令前自己算 PCM 的 RMS，低于门限直接记空（WhisperWoof 标定的门限约合 int16 27.5 [源 PR #151]）；已知的幻觉句整句去掉（上面两句、"字幕由Amara.org社区提供"之类，**加上配置里 `--prompt` 的那句话**和 `drop_lines`），列表在代码里，有测试。空转写：`sentences` 为空，`result.json` 仍是 `done`，不带 `memo`。

### 4.10 一次只能有一个转写程序

- 契约规定 `transcript.*` 和 `result.json` 只有转写程序写，同一时间只能有一个。光靠事后发现不够：两边可能都已经写了 `transcript.*`，`outputs` 里的 sha256 就和文件对不上了。
- **认领文件**（契约新加，§7）：`recordings/.transcriber`，内容 `{v:1, app, pid, since, beat_at}`。在跑的转写程序每 60 s 更新 `beat_at`；任何转写程序看到**别人的、5 分钟内更新过的**认领，就一个文件都不写，自己的状态写"另一个转写程序在跑"。
- `cpa-stt` 默认关，所以装上 VC-1 不会悄悄抢走 WhisperWoof 以后要做的目录。
- **WhisperWoof 那边要加同样的规则**（另一个仓库，👤）：#151 合并前加上读写 `.transcriber`。加上之前：两个只能开一个；`cpa-stt` 发现目录里有别人的 `status.json`（`by` 不是它）或 `result.json` 的 `pipeline.app` 不是它，就停下这个目录，并在 `cpa-stt status` 和 cpad 的 `cpa status` 里告警"另一个转写程序也在写 recordings/"。
- **换手**：`cpa-stt off` 删掉自己的认领、停下；之后开 WhisperWoof 就行，反过来同理。`cpa-stt` 写过的目录有 `result.json`，按契约别人不会再碰；要让 WhisperWoof 重做总结，删掉那些 `result.json`（契约的"手动重跑"）。

### 4.11 命令行

| 命令 | 做什么 |
|---|---|
| `cpa-stt status` | 开没开、认领、命令和模型在不在、沙箱在不在、最近一次自检、`blocked` 的原因、最近 10 段的耗时、有没有第二个写入者。不打印任何文字 |
| `cpa-stt use 文件` | 读配置，检查 `argv[0]` 和占位里的路径存在（取 realpath）、占位符合规，写进 `$CPA_HOME/stt/config.json`，跑自检，装上并启动 LaunchAgent（VC-1 不装，见 §4.13）|
| `cpa-stt off` | 停下、删掉自己的认领、卸 LaunchAgent |
| `cpa-stt test FILE.wav` | 对你给的文件在同样的沙箱里跑一次，打印耗时和文字（只在你的终端里）|
| `cpa-stt retry` | §4.6 的重跑 |
| `cpa status`（cpad，改）| 多两行：转写程序（谁在认领 / 没开 / `blocked` 的原因），"语音采集：N 条在等转写，M 段片段在等条目"（VC-1 只做了认领这一行，见 §4.13）|
| `cpa vcap redo REF`（cpad，新）| §5.2：转写后来补上了，把一条已经降级的语音采集重新拼一次 |

输出用英文，和现有的 `cpa` 子命令一致。

### 4.12 和 ADR 0004、docs/15 的关系

ADR 0004 定的是"不写 app 专用的集成"，docs/15 §1 第 3 条写"不写 app 专用的代码。对外只有两份契约：文件布局和 MCP"。ADR 0004 的背景里本来就写着转写程序是"WhisperWoof today, any program tomorrow"。本文的做法：

- cpad 里**没有**转写引擎的代码，对外仍然只有那两份契约；
- `cpa-stt` 是录音契约的又一个客户端，和 WhisperWoof 同一个位置；它不认识任何引擎，whisper.cpp 只出现在示例配置（数据）里；
- 不再用 Spotlight 找别的 app 的模型、不克隆别的 app 的缓存。

需要明写进去（DOC 包，随 VC-1，👤 第 3 条）：

- **ADR 0004** 加一条决定："本仓库可以带独立的录音契约客户端（如 `cpa-stt`）。它和第三方转写程序地位相同，只通过录音契约和 cpad 交换数据；引擎参数只在用户的配置里。"
- **docs/15 §1 第 3 条**后加一句："`cpa-stt` 的命令约定是它自己的配置格式，不是对外契约。"

### 4.13 实现说明：VC-1 落地时和上文不同的地方（2026-10-22）

- **位置**：`cpa-stt` 在 `daemon/src/cpa_stt/`，和 `cpa` 同一个包、各自的入口（`daemon/pyproject.toml`），不是仓库顶层的 `stt/`。仍然只用标准库，外加同一个包里 cpad 的几个小工具模块（配置文件读写、校验器、录音契约的读取、projects.json、launchd），不 import 守护进程；cpad 不 import 它，cpad 里也没有转写引擎的代码。示例配置是 `daemon/src/cpa_stt/examples/whisper-cpp.json`，按包数据读（`importlib.resources`，和 #57 一样）。
- **和 cpad 的边界**：除了录音目录，它还**只读** cpad 的两个文件：`$CPA_HOME/config.json`（笔记目录在哪）和 `projects.json`（项目名当提示词）。不写 cpad 的任何文件，也不连 `cpad.sock`。ADR 0004 那条照这个写。
- **会议默认也转**（`meetings: true`）：都是你自己的录音。会议的文字照旧永远不当指令（§8 第 1 条），`result.json` 里没有 `memo`。
- **模型放在 `$CPA_HOME/models`**：示例配置里写填空记号 `<MODEL:ggml-large-v3-turbo.bin>`、`<MODEL:ggml-silero-*.bin>`、`<WHICH:whisper-cli>`、`<PROJECTS>`，`cpa-stt use` 填好再检查、自检。`cpa-stt setup` 可以用 Spotlight（`mdfind`）找已有的 ggml 模型并建软链接到那里：不复制、不下载、不覆盖真文件。Spotlight 不搜 `~/.cache` 这样的隐藏目录，这台 Mac 上要直接给路径：`cpa-stt setup ~/.cache/openwhispr/whisper-models/ggml-large-v3-turbo.bin`。这和第 2 版"不用 Spotlight 找模型"不同：它只在你跑 `setup` 时找，只建链接，示例配置以外的引擎照样只是一份配置。
- **VAD 有模型才加，项目名当提示词**：`ggml-silero-*.bin` 在 `$CPA_HOME/models` 里时示例配置才带 `--vad`；项目名拼成 `--prompt`。提示词照 §4.9 进幻觉句过滤；你的 `drop_lines` 和提示词只按整句去掉，内置的几个长幻觉标记（Amara.org、明镜……）按包含去掉。
- **不默认装 LaunchAgent**：`cpa-stt use` 只写配置、跑自检（自检不过就不保存）。`cpa-stt run --once` 转完等着的就退出，`cpa-stt watch` 每 2 s 轮询，要开机就跑再 `cpa-stt service install`；`cpa-stt off` 会卸掉它。
- **认领**：另一个 `cpa-stt` 进程也算"别人"；它的 pid 在本机已经不在时可以不等 5 分钟直接接手。`cpa-stt run --once` 结束时删掉自己的认领。
- **真便条实测**（2026-10-22，你 15 条便条的副本，1–80 s，large-v3-turbo + Silero VAD，在沙箱里，对照 `-l zh` 不加 VAD 的转写）：最早的示例配置带 `-ot {offset_ms}`，15 条里 8 条开头的一两个词被切掉或听错，一条 3.5 s 的整条被 whisper 换成"请订阅我的频道"、过滤后成了"没听到"。去掉 `-ot` 以后 15 条开头都在，10 条和对照逐字相同，其余差几个字；80 s 那条对照本身在中间丢了约 25 s（单独切出来转过，是真话），`cpa-stt` 没丢。每条耗时：14 s 以内约 1.7 s，33 s 2.2 s，51 s 2.7 s，80 s 3.7 s。项目名提示词把"卡普特""购键"纠正成 Cardputer、Go 键，也多改错一两个字。15 个 `result.json` 都过契约检查器和 cpad 的读取；沙箱里的 curl 连不上本机端口（退出码 7），也解析不了域名（退出码 6）。
- **实测补充**（合成语音、在沙箱里）：一段 7 秒、先英文后中文的会议，`-l auto` 判成中文（p = 0.63）后英文那句整句没有出来。会议里换语言时要留意，记进 H-VC5。
- 上面"真便条实测"的数字以 10 条逐字相同为准（构建报告正文里写成了 11）。下面评审后的修改没有动命令和参数，只多拦了几句繁体的幻觉句，所以这些数字照样适用。

**评审后的修改（2026-10-22，三份评审）**

- **认领**：没有认领时用 `link()` 独占创建，两个同时起来的只有一个拿到；接手过期认领后读回来，确认是自己的才往下走。空闲和 `blocked` 时也每 60 s 续，不会让别人以为它停了。转写命令跑着时每 0.2 s 看一次认领：不是自己的了（或者要停下）就杀掉整个进程组，什么都不再写；写 `transcript.*` 和 `result.json` 之前再看一次。`beat_at` 在将来时按文件的 mtime 算，不带时区偏移的当读不懂（看 mtime），cpad 的 `cpa status` 同样处理；以前这样的认领会一直挡着。
- **遗留文件和第二个写入者**：别人的 `status.json`、`transcript.*`、`summary.md` 在 5 分钟内改过，才算"另一个转写程序正在写"；更旧的是遗留，持有认领的程序可以覆盖 `status.json` 和 `transcript.*`，永远不碰 `result.json`。这样 §4.10 的换手（包括 `cpa-stt` 被 `blocked` 时写下的状态）不会把目录卡住。写之前还要确认 `status.json` 仍是自己写的；持有认领期间别人放下的 `result.json` 也算第二个写入者。这些都记进 `cpa-stt status` 的告警。
- **停下**：`cpa-stt off`、SIGTERM、Ctrl-C 在下一个目录之前就停，正在跑的命令 0.2 s 内被杀掉；再按一次 Ctrl-C 直接退出。`off` 先卸 LaunchAgent，再清掉死进程的认领。
- **恢复**：`cpa-stt retry` 留下一个文件 `$CPA_HOME/stt/retry`，在跑的 `watch`（或 LaunchAgent）下一轮就读到并清掉 `blocked`；以前 `watch` 会用内存里的状态把它盖掉。被 `engine` 挡住时每分钟最多再自检一次，环境修好后不用重启就接着转。
- **一个目录出问题不拖累别的**：读不了或写不了（权限）的目录只记 `io_error`（会话 id 和 errno，不记路径），按 30 s / 2 min / 8 min 重试，最后能写就写 `failed / E_IO`（可重试），别的目录照转。等重试时 `status.json` 写 `queued`，不再一直显示 `transcribing`。会议关着时，连 manifest 不合格的会议目录也不碰；段是符号链接的目录不写 `blocked`。
- **长会议**：stdout 的上限随时长增加（256 KiB + 每秒音频 160 B，约 whisper 输出量的十倍）；超了算这一次失败（重试，最后 `failed / E_STT`），不会写一份悄悄截断的转写。
- **幻觉句**：`-l auto` 有时出繁体字，繁体的那几句也拦住（謝謝觀看、感謝觀看、請訂閱我的頻道、中文字幕志願者 李宗盛，以及带 明鏡需要您的支持、歡迎訂閱明鏡、點點欄目、優優獨播劇場、索兰娅/索蘭婭 的整行）。什么都没听到时 `transcript.md` 写"（没听到）"，和设备一致；终端输出仍是英文。
- **日志**：别人在写的目录只记一次，不再每 2 s 一行；日志到 1 MiB 轮换一次（留一份 `.1`）；空闲的 `watch` 不再每轮重写 `state.json`。
- **命令行**：`cpa-stt use` 不带文件时保留你原来的其他设置（`meetings`、`drop_lines`、`timeout_s`、`recordings_dir`），只换命令和沙箱服务，并提醒"同一时间只能有一个转写程序，WhisperWoof 的收件箱开着就先关掉"。`status` 在开着却没有程序持有认领时写"running: no, new recordings wait"，并写 LaunchAgent 装没装。`watch` 开头说在看哪个目录、怎么停，之后每转完一条打印会话 id、时长和耗时（不打印文字），状态变了才再打印。模型不见了会写出是哪个文件、怎么重新链接。
- **`setup`**：先看已知目录 `~/.cache/openwhispr/whisper-models`（§4.4 提到的那个，Spotlight 不搜），再用 Spotlight；找到的先列出来，你答 y（或加 `--yes`）才链接。不管是找到的还是你给的路径，链接的文件必须是你自己（或 root）的，它和它所在的目录别人不能改，不在 `/Volumes` 下，开头是 ggml 的魔数 `lmgg`。链接不是复制：别的 app 挪走或删掉它，录音就停在 `model_missing`，重新 `setup` 再 `use`。
- **cpad**：`config.json` 坏了时 `cpa status` 不再报错退出，转写程序一行显示 none。

**评审提了、这次没做的**

- cpad 的 `cpa status` 只显示谁持有认领，不显示 `blocked` 的原因，也没有"另一个转写程序也在写"的告警（§4.10、§4.11 写了要有）。这两样在 `cpa-stt status` 里；cpad 要显示就得读 `cpa-stt` 自己的状态文件，和"cpad 不认识转写程序"冲突。`cpa status` 的 `recordings:` 行仍有 `blocked=N`。
- `cpa status` 看的是 cpad 的 `notes_dir/recordings`，不读 `cpa-stt` 的 `recordings_dir`：`cpa-stt` 看别的目录时，cpad 的录音目录本来就没有转写程序，显示 none 是对的。
- 转写命令降优先级（`taskpolicy -c utility`）：终端里跑和 LaunchAgent（`ProcessType: Background`）下优先级不同，85 s 的合成语音实测 3.2 s / 3.6 s（utility）/ 5.1 s（`taskpolicy -b`，代替 background）。和 WhisperWoof 口述抢不抢 GPU 还没量，放进 H-VC6：先量口述延迟再定。
- 以后每日笔记给"仅转写"的便条直接显示 `memo_text` 时，"[全文]" 要链到 `transcript.md`（`cpa-stt` 不写 `summary.md`）。现在"仅转写"的那一行链的就是 `transcript.md`。

## 5. Mac 上：把采集拼起来（cpad）

### 5.1 流程

```
设备 outbox.item(queue, cap:C, voice:[R1,R2])
  └─ cpad：item.received（body 带 cap、voice）→ fsync → outbox.ack(stored)
           有 voice 的条目：不马上记 queue.added，也不马上写今日笔记（还没有文字）
设备 file.items（R1、R2 带 cap:C）── 拉过来 ── 归档 ── cpa-stt ── result.json ── rec.result(R1)、rec.result(R2)
  └─ vcap.fold：条目要的片段都有结果了（或确定来不了）→ 拼正文（§5.3）
       ├─ 能排队：queue.added{origin:"voice", voice:true, intent} + vcap.assembled   ← 同一批追加；再写今日笔记一行
       └─ 不能排队：vcap.assembled{action:"down"/"too_long", reason} + 今日笔记一行
```

- 片段和条目哪个先到都行。条目通常先到；录下一段时设备对 `file.*` 回 `E_BUSY`，片段要等你松手才拉。
- 有 `cap`、没有 `voice` 的条目（语音全拿掉了，字照常存）：按今天的规则处理打的字，`dropped` 记 `rec.hidden`，记 `vcap.closed`。
- 墓碑（`drop:true`）：只记 `rec.hidden` 和 `vcap.closed`，别的什么都不写（§2.5）。

### 5.2 什么时候算"齐了"

一段片段的"最终结果"是下面之一：

1. `rec.result{done}`（有字或空）或 `rec.result{failed}`；
2. `rec.stale`：归档后 `transcribe_wait_h`（24 h）没有结果，比如转写程序没开、或者 `blocked`；
3. **确定来不了**：设备连着、`files.n == 0`、`files.live == false`，连续两次 `pong`（约 10 s）都这样，这段还没到 Mac。设备手里已经一段录音都没有了，它不会再来。

- **只有第 3 条算"缺段"**，所以拔线离线多久都没关系：设备手里还有录音时，Mac 一直等。
- 条目要的每一段都有了最终结果，就拼。有段失败、过期或缺段，正文后面加"（另有 1 段没转出来）"，照样建；一个字都没有就降级成今日笔记一行（`reason:"no_text"`）。
- **晚到的片段**：拼完以后，`voice` 里的某段才有了结果（比如结束信号和索引都丢了，下次开机被恢复扫描找回来；或者 `rec.stale` 以后转写程序才修好），写一行"补充"笔记（§5.5），**不进队列**——你当时排的那件事已经按缺这一段的样子处理了。
- **转写程序修好以后**：已经因为 `rec.stale` 降级的采集不会自己重新排队（隔了一天自动排进去会吓人）；"补充"行里写着 `cpa vcap redo <ref>`，你想排就跑一下。

### 5.3 正文怎么拼

1. 打的字在前（`text` 不是占位符 `（语音）` 时，§6.3）；
2. 各段文字按 `voice` 的顺序接上，段之间一个空格；取 `memo_text`，没有就读 `transcript.json` 的句子（外部转写程序只到 `level:"transcript"` 时）；
3. 照 docs/14 §2.3 的 `queue_text()` 清洗：控制字符换空格、去首尾空白；
4. **长度**（`text_max` 500 B，约 35–40 秒的话）：
   - 去向是今日笔记、待办（记一条）：没有限制，笔记里是全文；
   - `intent=spawn`：**超过 500 B 不进队列**，降级成今日笔记一行，`reason:"too_long"`。被截掉的可能正是"……先别删 Y"这种限定；
   - `intent=todo`：截到 500 B（含结尾的"…（截断）"），不截半个字。开 session 前的全文页能看到这个结尾（§3.8）；
5. 截完是空的，或以 `-` 开头（`check_text()` 会拒）→ 降级成今日笔记，`reason:"bad_text"`。
6. **摘要**：`语音·` + 语音部分的开头，按 `hub/snapshots.label` 截到 80 B。打的字通常是几个词，看全文页就有。

设备上的 40 秒额度（§3.5）让 `too_long` 很少发生：中文口述大约每秒 4 个字、12 B，40 秒约 480 B [算]。说得特别快才会超。

### 5.4 排队和开 session 的规则

1. **语音只排队，不执行**：cpad 对从 `voice` 来的东西只会写 `queue.added`，**永远不调** `agent.spawn` / `agent.reply`。开 session 只走老路：看板上选中、确认框上按 Enter。离线采集永远不开 session（docs/14 §5.4 第 1 条）。
2. **先取全文才能开**：`queue.get{qid}` 回 `queue.text`，cpad 在内存里记下"这次连接给设备发过这一项的全文"。`agent.spawn{qid}` 指向语音项、而这次连接没发过它的全文 → 回 `E_BAD_FIELD`，不开。设备换连接要重新取。
3. **一律 plan**：从语音项开 session，不管项目配的模式，都用 `--permission-mode plan`（和 docs/17 §6.3 对会议待办的规定一样）。改 `hub/reduce.py` 的 `SpawnRequest`：排队项 `voice` 为真时用 `plan`。
4. **语音开的 session 不接受设备回复**：`queue.spawned` 记下它来自语音项；`agent.reply` 指向这样的 session → `E_NOT_REPLYABLE`（设备写"无法回复"）。原因：`resume_argv` 不带权限参数（`argv.py:70`），回一句"好"可能就批准了计划，用什么模式执行不清楚（H-VC8 去测）。在 Mac 上照常能处理。
5. **回复类的排队项永远不开新 session**（不管是不是语音）：`agent.spawn{qid}` 指向 `intent=feedback` 的项 → `E_BAD_FIELD`；设备看板上也不发（§3.8）。今天确认框把"session 在等"的回复存成这种项（`confirm.cpp:42`），看板却能把它开成一个新 session，这一条把它堵上。
6. **去向只看你按的键**：编辑器的种类和去向。转写里说"开个 session""发到 Slack"，什么都不会发生（docs/13 §9.2、docs/14 §5.4 第 3 条）。
7. **转写是不可信数据**：清洗、长度规则、`-` 开头降级（§5.3）；正文永远在 argv 的 `--` 后面；会话名照旧由 `session_name()` 生成。
8. **不用 `uncertain`**：那是 U25 的"可能已开过"。docs/14、docs/17 跟着改（§13.1）。

> **2026-10-26 补记（docs/25 D23、§7.6）**：语音片段和语音便条**从不直接开 agent session**，也不直接送到 agent：它们先进 Mac app 的"待归档"，由你手工交给某个 agent 或送到某个 app。上面"排队、开 session"的语音只会变成排队项，开 session 仍要在设备看板上翻完全文按 Enter，而且只对连了代码目录的项目（项目现在可以只是一个分组，docs/25 D24）。

### 5.5 今日笔记

每条语音采集一行，块 ID 是条目的（`^cpa-<条目 id>`）。各段文字照 `route.py` 的规则：超过 280 字截到第一句，加 `[全文](…/transcript.md)`。

| 情况 | 一行的样子 |
|---|---|
| 记一条 | `- 14:02 #cardputer fix wifi page 语音：①把设置页的WiFi部分改成英文。[音频](recordings/2026-10-22/1402-…/seg-000.wav) ②再跑一遍单元测试。[音频](…) ^cpa-…` |
| 待办 | 同上，开头是 `- [ ] `（v0.4.1 的复选框）|
| 排队给 AI | `- 14:02 #cardputer 给 AI（q12）：fix wifi page 语音：①… [音频](…) ②… [音频](…) ^cpa-…` |
| 开 session | 同上，前缀是 `开 session（排队 q13）` |
| 太长，没进队列 | `- 14:02 #cardputer 语音（太长，没进队列，要开就重说短一点）：全文 [音频](…) ^cpa-…` |
| 没转出文字 | `- 14:02 #cardputer 语音（没转出文字，没进队列）：[音频 1](…) [音频 2](…) ^cpa-…` |
| 补充 | `- 14:20 #cardputer 补充（^cpa-<条目 id> 的第 2 段，没进队列；cpa vcap redo <条目 id>）：文字 [音频](…) ^cpa-<会话 id>` |
| 没挂上 | `- 14:02 #cardputer 语音片段（没挂上条目）：文字 [音频](…) ^cpa-<会话 id>` |

**不重复写**：

- `hub/reduce.py` 今天对 `kind:"note"` 马上写一行（`fx.WriteNote`）：条目带 `voice` 时不写，由拼好的那一行代替。
- `recordings/route.py` 对 `rec.result` **和** `rec.stale`（"语音（未转写）"）都出行：带 `cap` 的片段两种都不出行，由采集那一行、"补充"或"没挂上"代替。拿掉的片段什么行都没有。
- 打字的排队项仍然不写今日笔记（现状不变）；语音的写，是因为片段不再有自己的那一行，不写的话这段话只在录音目录里。

### 5.6 事件（`events.jsonl`，新事件不含正文）

| 事件 | 字段 | 什么时候 |
|---|---|---|
| `item.received` | 现有字段，`body` 多 `cap`、`voice`、`dropped`、`drop` | 条目、墓碑到了（正文本来就在这里）|
| `rec.result`、`rec.stale` | 现有字段，多 `cap`（从 manifest 抄）| 片段有了结果 |
| `vcap.assembled` | `ref, cap, dest, clips, got, missing, action(queue/note/down/too_long), reason?, bytes, sha8` | 拼好的那一刻，和 `queue.added` 同一批 |
| `vcap.closed` | `ref, cap, dropped, why(typed/tombstone)` | 没有 `voice` 的条目、墓碑 |
| `vcap.late` | `rec, ref` | 写了"补充"那一行 |
| `vcap.orphan` | `rec, cap` | 写了"没挂上"那一行 |
| `vcap.redo` | `ref` | `cpa vcap redo` |
| `rec.hidden` | 现有事件，多 `why:"dropped"` | 片段被拿掉 |
| `queue.added` | 现有字段，`origin:"voice"`，多 `voice:true` | 同 `vcap.assembled` |

`bytes`、`sha8` 和 docs/14 §8.1 一样：正文的 UTF-8 字节数和 sha256 前 8 位。

### 5.7 引用校验（不过就当这段不存在，记日志）

- `voice`、`dropped` 各是不重复的 MonoId，各最多 4 个，互不重叠；有其中之一就必须有 `cap`；`drop:true` 只在 `kind:"note"`、没有 `voice`、`dropped` 不空时；
- 片段是同一台设备（`dev`）录的、`mode=memo`、`cap` 和条目的相同。**会议不能当片段**：会议里有别人的话（docs/17 §6.3），带 `cap` 的会议整份当没有 `cap`；
- 开机号相同，并且 `cap` < 片段会话 id < 条目 id（同一个 MonoId 序列）；
- 一段只能属于一个条目，先到的算；
- v0.8 起和条目在同一个情境档案里（docs/18 §3.4）。设备在有浮层、在录音时不换档案（docs/18 §2 第 6 条）。

### 5.8 重启和幂等

- `vcap.fold` 是纯函数，照 `queue/fold.py` 的写法：从 `item.received`、`rec.result`、`rec.stale`、`hello`（开机号）、`pong`（`pending`、`files`）和 `vcap.*` 折叠出"在等的采集"和"在等条目的片段"。**有 `vcap.assembled` 的条目、有 `vcap.late` / `vcap.orphan` 的片段不再处理**（`vcap.redo` 之后的那一次除外），所以每次启动重放整个日志不会重复排队、重复写行。
- "确定来不了"（§5.2 第 3 条）只看**本次运行**收到的 `pong`，不从日志里推。
- 崩在"拼好了、还没追加"之间：下次启动折叠时这条还在等，就在那时拼一次（和 docs/14 §6.1 便条的做法一样）。

### 5.9 接到 docs/14 的路由（v0.5）

docs/14 合并以后，vcap 不再自己写 `queue.added`，而是交出 `Capture{ref, source:"voice", dest, how:"key", project, text}`，`origin:"queued"`，由 `decide()` 判定。**片段只会出现在去向是今日笔记、待办、队列、session 的采集里**：设备在别的去向上不挂（§3.1、§3.3），cpad 收到挂在别的去向上的 `voice` 就按"降级到今日笔记"处理（`dest.downgraded`），不交给剪贴板、粘贴、插件或文件。所以 docs/14 §5.3 的"插件离线也投递语音项"碰不到语音采集，"音频和文字不离开 Mac"成立。VC-2 **不用等 docs/14**。

## 6. 协议新增（只新增，`v` 仍为 0，不新增错误码）

### 6.1 总表

| 变化 | 方向 | 内容 | 旧的一端 |
|---|---|---|---|
| `outbox.item` 的 `note.body`、`queue.body` 加 `cap?`、`voice?`、`dropped?`；`note.body` 加 `drop?` | D→H | `cap`：MonoId；`voice`、`dropped`：1–4 个不重复的 MonoId，互不重叠；有 `voice` 或 `dropped` 必须有 `cap`；`drop:true` 时只有 `dropped` | 旧 cpad 忽略，存成普通笔记、排队项（§6.3）|
| `file.items[]` 加 `cap?` | D→H | MonoId，只有片段的第 0 段带；和 `dest` 不同时出现 | 旧 cpad 忽略，当普通便条 |
| `.M` 加 `cap` | 设备内部 | 同上 | 旧固件的恢复扫描忽略未知字段 |
| `queue.snapshot` 的项加 `voice?` | H→D | 布尔：这一项来自语音，开之前要看全文 | 旧固件忽略 |
| **`queue.get`**（新）| D→H 请求 | `{qid}`；回 `queue.text` 或 `error{E_UNKNOWN_ITEM}`；和 `queue.done` 同一类在途上限（1）| 旧 cpad 回 `E_UNKNOWN_TYPE` |
| **`queue.text`**（新）| H→D 回复 | `{re, qid, text}`，`text` 是 `userText`（1–500 B）| — |
| `limits.json` | — | `voice_max:4`、`voice_queue_ms_max:40000`、`voice_text:"（语音）"`，登记进 `schemas.LIMIT_POINTERS` | — |

不加 `hello.caps`、不加快照：设备挂不挂片段不看 Mac 端版本（§6.5）。协议 README 新加第 23 条。

### 6.2 例子

```jsonl
{"v":0,"t":"outbox.item","id":"3QZK1M7-0000C-000A","kind":"queue","at":{"up_ms":98000,"wall_ms":1792440045000},"body":{"text":"fix wifi page","project":"cardputer","intent":"spawn","cap":"3QZK1M7-0000C-0006","voice":["3QZK1M7-0000C-0007","3QZK1M7-0000C-0009"],"dropped":["3QZK1M7-0000C-0008"]}}
{"v":0,"t":"outbox.item","id":"3QZK1M7-0000C-0010","kind":"note","at":{"up_ms":131000},"body":{"text":"（语音）","cap":"3QZK1M7-0000C-000C","dropped":["3QZK1M7-0000C-000D"],"drop":true}}
{"v":0,"t":"queue.get","id":"d40","qid":"q13"}
{"v":0,"t":"queue.text","id":"h41","re":"d40","qid":"q13","text":"fix wifi page 把设置页的WiFi部分改成英文，再跑一遍test shell的单元测试。"}
```

### 6.3 只说了话、没打字时 `text` 写什么

`text` 必填（`userText` 至少 1 字节）。改成可以不填，旧 cpad 会回 `E_BAD_FIELD`，条目进设备的 `bad/`。所以设备在没打字时写占位符 **`（语音）`**（12 B，全角括号）：设备键盘只能打 ASCII，你**不可能打出**这几个字，新 cpad 看到它就知道"没打字"。墓碑也用它。

### 6.4 行长 [算 `$VC/lines.py`、`$VC/lines2.py`，按 `wire.canonical_line` 的规则]

| 行 | 构造 | 字节 |
|---|---|---|
| `outbox.item` 排队，最坏 | 500 B ASCII 正文、12 字符项目、`sid`、`uncertain`、`cap`、4 个 `voice`、4 个 `dropped` | 929（没有新字段时 712）|
| 同上，正文 500 个 `"` | 每个转义成 2 字节 | **1429** |
| 墓碑 | 4 个 `dropped` | 321 |
| `file.items` 4 项，每项所有字段取最大并带 `cap` | — | 2096（超 2048）→ 设备照 `file_fit_page` 一页放 3 项、`more:true`：1600。和 v0.4.1 长 `dest` 的先例一样 |
| `queue.get` | 最长的 id 和 qid | 63 |
| `queue.text` | 500 B ASCII / 166 个汉字 / 500 个 `"` | 593 / 591 / 1093 |
| `queue.snapshot` | 12 项、各 80 个 `"`、带 `voice` | 3291（今天 3135）：今天就超，照旧过 `codec.fit_list` 分页 |

上限都是 2048 B（`line_max`）。`test_worst_lines.py` 加这几行。

### 6.5 新旧混用

| 组合 | 结果 |
|---|---|
| 新固件 + 旧 cpad | 片段照样挂（设备不看 Mac 版本）。旧 cpad 把片段当普通便条（各写一行），条目存成"（语音）"或打的字，墓碑写一行"（语音）"。什么都不丢，只是多几行。看板上不会有带 `voice` 的项，设备不会发 `queue.get` |
| 旧固件 + 新 cpad | 不发 `cap`；`cpa-stt` 照样给所有便条转写；带 `voice` 的项只可能来自新固件 |
| 新固件 + 新 cpad，`cpa-stt` 没开 | 条目和片段都到了 Mac、一直等转写；24 h 后 `rec.stale`，采集降级成"没转出文字"一行。`cpa status` 一直写着"转写程序没开：N 条语音采集在等" |
| 新 cpad + 降级回旧固件，队列里还有语音项 | 旧固件不知道要先取全文，按 Enter 发 `agent.spawn{qid}`；cpad 没发过全文，回 `E_BAD_FIELD`（§5.4 第 2 条），开不了 |

不看版本的好处：离线、重启以后，同一个手势的意思不变；设备不用把 Mac 的能力存进 `/cfg`（今天只存 `projects.json`，`runtime_outbox.cpp:15-17`）。

### 6.6 合并约束

`queue.get`、`queue.text` 是新的消息类型。固件的 `test_codec_vectors` 断言 schema 数等于 `kMsgTypeCount + 1`（docs/14 §7.8）。所以 schema、向量（VC-2p）必须和固件 codec 登记这两个类型（VC-2f 的第一步，只做字段校验）**同一批合并**；daemon 的 `test_every_message_type_has_a_validator` 同理。

## 7. 录音契约 v1 补充（都可选，`v` 仍是 1）

| 变化 | 内容 |
|---|---|
| `manifest.json` 加 `cap?` | MonoId，cpad 从 `.M` 抄过来。加合法向量 `valid/manifest_clip.json` |
| `status.json` 加 `by?` | 写它的程序名（`^[a-z0-9-]{1,16}$`）。加合法向量 |
| `status.json` 的 `reason` 加 `engine` | 转写命令跑不起来（沙箱、Metal、二进制）。cpad 的 `recordings/contract.py:95` 校验这个集合，**同一个版本**一起改 |
| 认领文件 `recordings/.transcriber` | `{v:1, app, pid, since, beat_at}`；§4.10 的规则。以 `.` 开头，MCP 和 `cpa export` 本来就不读点文件（ADR 0004 规则 5）|
| README 加一句规则 | 带 `cap` 的会话是某条采集的一段语音，内容会以那条采集的形式出现；读便条的程序**不应把它单独当一条便条、也不应当成用户的反馈或指令**。不认识 `cap` 的旧版本把它当普通便条：照样转写，最多在它自己的列表里多一条 |
| 单写者表 | "WhisperWoof"改成"转写程序（WhisperWoof 或 `cpa-stt`；同一时间一个，用 `.transcriber` 认领）" |

重算 `VECTORS.sha256`。

- **你给开发提反馈的便条**：开发流程里把便条当你的反馈来读的做法，要跳过带 `cap` 的会话（和"只读便条、不读会议"同一类规则）。VC-1 以后便条都有 `transcript.json`，那边可以直接读，不用自己再跑 whisper。
- 录音应用（Opt+V）照旧列出挂上的片段（它是音频的历史）；拿掉的片段被隐藏。

## 8. 隐私与安全

1. **会议永远不当指令**：会议不能当片段（设备在编辑器里开不了会；Mac 忽略会议上的 `cap`）；会议转写永远不拼进排队正文。
2. **只排队，不执行**；去向只看按键；开 session 前在设备上翻完全文、cpad 核对取过全文、一律 plan；语音开的 session 不接受设备回复；回复类的项永远不开新 session（§5.4）。
3. **转写是不可信数据**：清洗、长度规则、`--` 之后（§5.3）。
4. **不离开 Mac**：转写命令在本机跑，包在挡联网、挡乱写的沙箱里，找不到沙箱就不跑（§4.5）；cpad 和 `cpa-stt` 不上传、不下载；新事件只有字节数和哈希；日志只有 id、时长、耗时、退出码；命令的 stderr 丢掉。片段只会进今日笔记、待办、队列、session 四种去向（§5.9）。
5. **看不见草稿就不挂**：锁着、菜单、确认框上按住 G0 录的是独立便条，录音页开口前写明（§3.1）。
6. **录到别人**：片段就是便条，规矩一样（docs/13 §13）：没有提示音，● 和计时永远看得见。
7. **单写者**：同一时间只有一个转写程序；认领文件；`result.json` 不覆盖别人的（§4.10）。
8. **命令本身**：不经 shell、绝对路径、环境变量白名单、超时、输出上限、空的工作目录（§4.3）。
9. **两台 Mac**（v0.8 之前）：片段和条目可能被不同的 Mac 收走，这时两边都拼不成，片段在各自那边变成"没挂上"的行，什么都不丢。我仍假设你只用一台 Mac（docs/13 §13 第 7 条）[设]。

## 9. 时延：从松开 G0 到看板上出现语音项 [算]

在线，按过 Enter，一段 d 秒的话：

| 步骤 | 耗时 | 依据 |
|---|---|---|
| cpad 看到新段 | 0–5 s（等下一次 `pong`）| `ping_interval_ms` 5000 |
| 拉过来 | ≈ 0.062 d：10 s → 0.6 s，40 s → 2.5 s | v0.4.0 实测 513 KB/s |
| `cpa-stt` 发现 | 0–2 s | §4.6 |
| 转写（`auto`、沙箱）| 10 s → 1.7 s，30 s → 2.2 s，40 s → 约 2.5 s | §4.5、§4.8 |
| cpad 捡到 `result.json` | 0–3 s | `results.py` 扫描间隔 |
| 拼、排队、推 `queue.snapshot` | <0.1 s | — |
| **合计** | **10 s：约 2.4–12.4 s；40 s：约 5–15 s** | |

- 设备上的片段行在拉走以后变成"已传 Mac"：0–5 s 加拉取时间。
- 连着录几段时，前面的段要等你松手才拉（录音中 `file.*` 回 `E_BUSY`）。
- 离线：插上以后先拉再转，4 段 10 秒的话约 4 × (0.6 + 1.7) ≈ 9 s，加上等 `pong` 和两边的扫描。
- 想再快（段落盘时设备主动 `pong`、cpad 不等 3 s 扫描）放到 VC-3 [设]。

## 10. 预算

| 项目 | 增加 | 上限 / 现在 | 依据 |
|---|---|---|---|
| AppState | 排队项 `voice` 12 × 1 B | 4 KB；现约 3.5 KB | docs/13 §10.3、docs/19 §10 |
| 草稿（Shell）| `cap` + 4 段 {id, 时长, 状态} + 4 个 `dropped` ≈ 0.2 KB；排着的按键 8 个 ≈ 0.1 KB | — | [算] |
| 全文页（Runtime）| `queue.text` 501 B + 分页状态 | 静态 RAM 96 KB；现约 66.3 KB | [算] |
| 接收缓冲 | 最长的新行 1093 B（`queue.text`）| 8192 B | §6.4 |
| flash | 代码约 4 KB [估]；麦克风小图 7×9 = 63 B | 镜像警告线 1,835,008 B | docs/19 §10 |
| SD 卡 | 32 KB/s：10 秒片段 320 KB；最坏一条（记一条）4 × 5 分钟 = 38.4 MB；排队、开 session 最多 40 秒 = 1.3 MB | — | `limits.json` |
| loop | 片段行只在状态变了时重画 | 一步 ≤50 ms | [待 `test_runtime_budget`] |
| Mac 内存 | whisper 峰值约 1.98 GB，一次一个进程，转完就退 | — | [测] |
| Mac 算力 | 10 秒片段约 1.7 s；一天 50 段约 1.5 分钟 | — | [算] |
| Mac 磁盘 | 每段 `transcript.*` 几 KB；`events.jsonl` 每条采集多约 250 B | — | [算] |

## 11. 分期和工作包

一律先写失败的测试（RED → GREEN → REFACTOR）。大小：S ≤1 天，M 2–4 天，L 约 5 天 [估]。

| 包 | 内容 | 动哪里 | 依赖 | 大小 |
|---|---|---|---|---|
| **VC-0**（别的分支，不归本文）| D4：浮层下按住 G0 录独立的便条；松手宽限、连按两下免按住 | `feat/ux-decisions`、`fix/memo-hold` | — | — |
| **VC-1 cpa-stt**（只动 Mac）| §4 全部：配置和校验、认领文件、扫描和排序、命令运行器（沙箱、环境、超时、输出上限）、自检和 `blocked`、stdout 解析、RMS 门限和幻觉句、契约文件写入、重跑、LaunchAgent、CLI；示例配置 | 新 `stt/`（纯函数：`config`、`argv`、`profile`、`parse`、`gate`、`order`、`claim`、`contract_out`；I/O：`runner`、`service`、`cli`）| 无，**随时能发** | M（约 3 天）|
| **VC-1c 契约** | §7 全部；cpad `contract.py` 认 `engine`；cpad 的 `cpa status` 显示转写程序；ADR 0004、docs/15 的两句 | `protocol/recording/v1`、daemon、docs | 和 VC-1 同批 | S |
| **VC-2p 协议** | §6 的 schema、向量（合法 / 非法）、`limits.json`、README 第 23 条、`test_worst_lines` | `protocol/` | 和 VC-2f 的 codec 登记同批合并（§6.6）| S（1 天）|
| **VC-2f 固件** | §3.9 的全部 | 固件 | **D4 和 memo-hold 都合并以后**：两个分支都改了 `shell.cpp` 的 G0 分发和 `rec_gesture`（memo-hold 让它返回 `bool`），本包在同一处加判断 | L（约 5 天）|
| **VC-2d cpad** | `cpad/vcap/`（`fold.py`、`assemble.py` 纯函数）；`item.received`、`rec.result`、`rec.stale` 带新字段；去掉重复的笔记行；`queue.added` / `QueueItem` 带 `voice`；摘要；`queue.get` / `queue.text`；开 session 的全文核对和 plan；拒绝回复语音 session、拒绝开回复类的项；`rec.hidden`；墓碑；补充和没挂上；`cpa vcap redo`；`cpa status` | daemon `protocol/{specs,messages}.py`、`events/records.py`、`queue/fold.py`、`hub/{reduce,snapshots}.py`、`recordings/{manifest,route}.py`、`cli/main.py` | VC-2p；VC-1 不是硬依赖（没人转写时采集会等）| M（约 4 天）|
| **VC-2h 真机** | §12.2 的闸门 | — | 以上 | S（和你约 40 分钟）|
| **WW**（另一个仓库，👤）| WhisperWoof 认 `.transcriber` | WhisperWoof #151 | VC-1c | S |
| **VC-3**（以后）| 设备上显示片段文字和"太长"提示（一条新的 Mac→设备快照）；零打字的 G0+Q/S（随 docs/14 F5）；MCP 字段；语音回复（docs/17 §17 第 6 条）；长段按静音切块；录音应用里的"片段·"标题；`cpa vcap ls`；更快的发现 | — | — | — |

- **发版**：VC-1 跟下一个 cpad 版本走。VC-2 跟 D10 定的 v0.4.1 稳当版之后、下一次"固件 + 协议"的版本走（v0.4.2 或 v0.5）👤。
- **顺序**：VC-1 + VC-1c → VC-2p（RED）→ VC-2f 的 codec 登记 + VC-2p 同批合并 → VC-2f 其余、VC-2d 并行 → 真机闸门。
- 合计：VC-1 约 3.5 天；VC-2 约 10 个工作日，固件和 cpad 并行时约两周内。

## 12. 测试和真机闸门

### 12.1 测试（先写，RED）

| 层 | 测什么 |
|---|---|
| 固件 native | 编辑器下按住 G0：挂上；`cap` 只在第一段时取；`Started` 加段、`Discarded` 去段、结束信号丢了照样带上；第 5 段、满 40 秒、回复编辑器、菜单开着（含菜单关掉后重放的那次按住）、确认框、锁着都是独立便条；G0 单击不保存、只提示；连按两下 = 免按住片段；宽限内再按不算新的一段；宽限、免按住、收尾时 Enter / `` ` `` / 字母先结束这段再生效，排 8 个、第 9 个提示；有语音时 Backspace、`` ` `` 不退出；Opt+D 拿掉最后一段；放弃提示的 Enter（存成笔记）和 Opt+D（墓碑）；有 `cap` 的草稿每种关法都恰好发一个条目或墓碑；种类和去向的限制；开 session 有语音不进确认框；看板：语音项先取全文、分页、只有最后一页开、每页 400 ms 防误触、取不到不开；回复类的项不开；片段行的三种状态；`hint_fits` 第一档；`test_text_width` 量新文案；`.M` 带 `cap` 时最坏仍 ≤512 B；编解码向量 |
| 协议 | 合法：带 `cap`、1–4 个 `voice`、`dropped` 的笔记和排队项；墓碑；`queue.get`、`queue.text`；带 `voice` 的 `queue.snapshot`。非法：5 个 `voice`、重复、`voice` 和 `dropped` 重叠、不是 MonoId、有 `voice` 没有 `cap`、`drop` 带 `voice`、`drop` 在排队项上、501 B 的 `queue.text` → `E_BAD_FIELD`；`test_worst_lines` 的新行；旧 cpad 兼容（用 v0.4.1 的 `specs.py` 解带新字段的 `file.items` 和 `outbox.item`）|
| 录音契约 | `manifest.cap`、`status.by`、`reason:"engine"` 的合法向量；`check_recording` 通过；`VECTORS.sha256` |
| `cpa-stt` 单元 | 配置校验（`argv[0]` 绝对路径且存在、`{wav}` 恰好一次、占位符占整个元素）；沙箱配置的文字（工作目录、Metal 缓存、两个服务）；stdout 解析（有 / 没有时间戳、空、超长）；RMS 门限；幻觉句含配置里的提示词；契约文件过 `check_recording.validate`；`result.json` 已存在时不覆盖；认领：别人的新鲜认领 → 不写，过期的 → 接手；别人的 `status.json`（`by` 不同）→ 跳过；重启后认得自己的 `status.json`；自检失败 → `blocked`、不写 `result.json`；单段失败 → 退避后 `failed`；`retry`；超时杀进程；stderr 不进日志；片段先转 |
| `cpa-stt` 集成 | 假命令（打印固定文字的小脚本）跑通；沙箱里的假命令连本机端口、写 `$HOME` 都失败；`sandbox-exec` 不在时 `blocked{engine}`、不在沙箱外跑；真 whisper-cli 的冒烟测试默认跳过（`CPA_TEST_WHISPER=1` 才跑）|
| cpad 单元 | `vcap.fold`：条目先到 / 片段先到；`dropped` 和墓碑（不写行、`rec.hidden`）；语音全拿掉后打字开 session（墓碑）；结束信号丢了的片段：离线时一直等、`files.n == 0` 两次 `pong` 后才算缺；晚到的片段写"补充"、不进队列；同一次开机草稿开 25 h 不成孤儿；换开机号 + `pending == 0` 才成孤儿；`too_long`（spawn 降级、todo 截断带标记）；`bad_text`；`rec.stale` 降级和 `cpa vcap redo`；会议当片段被拒；一段两个引用；顺序不对被拒；重放整个日志不重复（hypothesis：任意转写文字要么得到能开的排队项，要么降级）；不重复写行（带 `voice` 的笔记不马上写；带 `cap` 的 `rec.result`、`rec.stale` 不出行）；摘要以"语音·"加语音开头；`queue.get` / `queue.text`；没取全文的 `agent.spawn` 被拒、取过的用 plan；回复语音 session 被拒；开回复类的项被拒 |
| cpad 集成 | 假转写程序（写契约文件的小脚本）跑通"条目 + 2 段 → 排队项 + 今日笔记行 + 快照"；一段 60 s 的假转写期间 cpad 的 pong 延迟不变 |
| 模拟器 | `firmware/sim/scripts/ux/voice_capture.cpas`（编辑器里两段、Opt+D、Enter）、`voice_capture_offline.cpas`、`voice_capture_discard.cpas`（两种放弃）、`voice_spawn_confirm.cpas`（翻页开）；`tools/sim/cpad_sim.py` 加 `queue.get` 和一个假转写 |

### 12.2 真机闸门（👤 要和你约时间，约 40 分钟）

| 闸门 | 做法 | 通过 |
|---|---|---|
| H-VC1 在线挂 3 段 | 排队给 AI 的编辑器里按住 G0 说三段，每段 5–15 秒，Enter | 片段行变成"已传 Mac"；15 秒内看板出现"语音·…"，今日笔记有一行 |
| H-VC2 离线 | 不插线录 2 段 + Enter，插上 | 两段转完、排队项出现；离线时片段行写"连上 Mac 后传" |
| H-VC3 草稿开着重启 | 挂 1 段时长按电源重启，再连上 | 今日笔记出现"语音片段（没挂上条目）"一行 |
| H-VC3b 录到一半重启 | 片段录到一半长按电源 | 恢复扫描救回的段（`rcv`，没有 `cap`）成为一段普通便条，有自己的"语音：…"一行；写进发布说明 |
| H-VC4 宽限、连按、松手就按 | 松手 300 ms 内再按；编辑器里连按两下说话、再按 G0；松手马上按 Enter；松手马上打字 | 都是同一段；连按两下没存走草稿；Enter 存下的条目带着刚录的段；打的字一个不丢 |
| H-VC5 你的真话 | 👤 在终端里对你自己的几段便条跑 `cpa-stt test`；再录几段：纯中文、纯英文、中英混说、带代码名字（`test_shell`、`npm run test`）| 你看字对不对；英文没变成乱码 |
| H-VC6 LaunchAgent | `cpa-stt` 在 LaunchAgent 下用沙箱转一段，记冷启动耗时；同时看 cpad 的 pong | Metal 可用、没有多出的 5 秒；沙箱不挡读模型和 WAV；pong 不受影响 |
| H-VC7 不挂的情况 | 锁着、Tab 菜单开着、回复编辑器里按住 G0 说话 | 录音页开口前写"语音便条"；都是独立便条，草稿没变 |
| H-VC8 plan 模式怎么结束 | 从语音项开一个 session，等它出完计划 | 记下它停在 `waiting` 还是 `idle`；设备回复被拒、写"无法回复"；结果写进 VC-3 的语音回复设计 |

## 13. 和别的文档、正在做的分支的关系

### 13.1 要跟着改的地方（DOC，随各包）

| 文档 | 现在写的 | 改成 |
|---|---|---|
| docs/04 §8.4、docs/20 D4 | 编辑层 G0 单击 = 保存；编辑器下按住 G0 录独立的便条 | G0 单击只提示、连按两下免按住；编辑器在最上层、这一条收得下时挂到草稿上（§3.1）|
| docs/14 §2.1 表、§5.3 的 `Q~` / `S~`、§13 测试 | 语音来的排队项 `uncertain=true` | `voice=true`；`uncertain` 只留给 U25；语音只进四种去向（§5.9）|
| docs/14 §6.3 | 只有转写、没有整理的便条没有 `memo_text` | VC-1 以后 `cpa-stt` 写的便条有 `memo_text`；判断看有没有 `memo_text`，不看 `level` |
| docs/17 §6.1 | 会议待办进队列"一律 `uncertain=true`" | 同上，用 `voice` 加"会议·"摘要；开之前也可以用 `queue.get` 翻全文 |
| 录音契约 v1 README | 单写者是 WhisperWoof；没有 `cap` | §7 |
| ADR 0004、docs/15 §1 | — | §4.12 的两句 |
| 协议 README | — | 第 23 条：语音采集（本文 §6）|

### 13.2 正在做的分支

- **`feat/ux-decisions`（D4、D5、D6）**：D4 那一行（`shell.cpp:120`）是本文的起点：本文在它前面加"挂不挂"的判断，菜单开着的情况要在菜单收到按住之前判断（§3.1）。D5 的 `` ` `` 直接退出，在有语音时改成先问（§3.3）。D6 的"确认框 ` 回编辑器"直接用上。
- **`fix/memo-hold`**：松手宽限、连按两下免按住对每一段都一样；它的连按两下就是"编辑器里 G0 单击不再保存"的原因（§3.2）。它改了 `rec_gesture`（返回 `bool`）和 G0 的分发，VC-2f 要在它之后。
- **`docs/settings-design`（docs/21）**：转写语言是 `cpa-stt` 的配置（默认 `auto`），说什么话和界面用什么语言是两回事。英文界面下片段行的文字（`待传`、`已传 Mac`）跟着界面翻译。
- **docs/18（v0.8）**：`item.received` 带 `prof` 以后，§5.7 的档案校验直接用它。

## 14. 风险与待验证

| 风险 | 怎么验证 / 缓解 |
|---|---|
| **真人语音的识别率没测**：数字都来自合成语音 | 👤 H-VC5；时延主要看时长，基本可信 |
| `cpa-stt` 默认关，你忘了开，语音采集一直等 | `cpa status` 一直提示；24 h 后降级成笔记行；发布说明第一步就是 `cpa-stt use` |
| `sandbox-exec` 哪天被去掉 | 找不到就 `blocked{engine}`，不在沙箱外跑；`cpa status` 写明 |
| 放行的两个系统服务只对 whisper-cli 1.8.3 测过 | 自检会暴露（崩或慢）；`sandbox_mach` 能加；H-VC6 |
| WhisperWoof 还不认认领文件 | 两个只开一个；`cpa-stt` 默认关；发现第二个写入者就停手、告警（§4.10）|
| 说得特别快，40 秒拼出超过 500 B | 开 session 的不进队列，今日笔记写明"太长"；这一版设备上没有提示（VC-3）|
| whisper 输出字库外的字，设备的摘要和全文页上画成 □ | `auto` 对中文输出简体（实测）；H-VC5 看；需要时 VC-3 加繁转简 |
| 30 秒接缝丢句 | 贪心；排队、开 session 最多 40 秒；记一条的长段切块放 VC-3 |
| 幻觉句没被拦住 | 三道拦截（§4.9）；`drop_lines` 随遇到的补充 |
| 草稿开着时重启 | 片段变成"没挂上"的行，什么都不丢（H-VC3）；docs/20 U16 的草稿持久化做了以后一起存 |
| 录到一半重启 | 救回的段没有 `cap`，成为普通便条（H-VC3b）；要连上，得在开录时就把 `cap` 写到恢复扫描读得到的地方，放 VC-3 |
| 语音开的 session 出完计划停在哪 | H-VC8；这一版设备一律不能回复它 |
| G0 单击不再保存，改了你的习惯 | 单击时提示"Enter 保存"；§15 第 1 条 |
| 新固件 + 旧 cpad 多出"（语音）"行 | 两边同一个版本发；只多行、不丢东西（§6.5）|
| 转写占 GPU，和你在 Mac 上听写抢 | 一次一段、每段 1–3 秒；必要时降低 QoS [待] |

## 15. 需要你拍板

都有默认值，不回复就按默认做。别的参数（一条最多 4 段、排队和开 session 最多 40 秒、正文 500 B、放弃时的两个选择）按设计走，不单独拍板。

1. **编辑器里按住 G0 = 给这一条加一段**（记一条、待办、排队给 AI、开 session；回复不挂），**G0 单击不再保存，只有 Enter 保存**，连按两下是免按住录一段。**默认：同意**。备选：G0 单击等 350 ms 没有第二下再保存（多一种"有没有第二下"的状态，不推荐）。
2. **语音只排队**：开 session 的编辑器里有语音时 Enter 只存排队项；从语音项开 session 要在设备上翻完全文、在最后一页按 Enter，一律 plan；语音开的 session 设备上不能回复。**默认：同意**。
3. **转写用独立的 `cpa-stt`，默认关**：你跑一次 `cpa-stt use`（用 whisper.cpp 示例配置：large-v3-turbo、自动判断语言、贪心、VAD、收紧的沙箱）才开；ADR 0004 和 docs/15 各加一句（§4.12）。**默认：同意**。备选：不装 `cpa-stt`，等 WhisperWoof（#149–#152 合并、并认 `.transcriber`）。

## 附：评审意见怎么处理的

评审一共提了 9 条高、9 条中、5 条低，外加"范围砍掉"一节。逐条核对过代码和三个正在做的工作区（`compose.cpp` 的 `cancel()` / `delete_char()`、`keymap.cpp:82`、`g0_gesture.h`、`shell_g0.cpp`、`feat/ux-decisions` 的 `dispatch()`、`rec_key_event`、`rec_core.h` 的 `kFinishGiveUpMs`、`rec_msgs.h` 的 `Started`、`runtime_outbox.cpp:15-17`、`app_state.h` 的 `LinkInfo`、`board_logic.cpp:69-74`、`reduce.py` 的 `WriteNote` 和 `_spawn_text`、`argv.py` 的 `resume_argv`、`route.py` 的 `rec.stale`、`limits.json` 的 `queue_text_max`、`common.schema.json` 的 `files.n`、docs/14 §5.3），全部成立，全部采纳。另外补测了两件事：收紧的沙箱（第 16 条）和强制中文对英文的影响（第 18 条）。

| 评审意见 | 处理 |
|---|---|
| **1** 删掉的片段是猜出来的：放弃、在线开 session、结束信号丢了都会出错 | **采纳**。条目加 `dropped`；有 `cap` 的草稿不发条目就关时自动发墓碑 `note{cap, dropped, drop:true}`；哪儿都没提到的片段一律当"没挂上"，从不当"删掉"；草稿在 `Started` 时加段、只在 `Discarded` 时去段；放弃提示加 Opt+D"连语音一起丢"（§2.1、§2.4、§2.5、§3.3、§5.1）|
| **2** 24 h"缺段"计时一过，晚到的片段就看不见了 | **采纳**。去掉 24 h 计时；"缺段"只在设备连着、`files.n == 0`、不在录、连续两次 `pong` 时成立；晚到的片段写"补充"行、不进队列（§5.2、§5.5）|
| **3** 有语音、没打字时，一个键就把草稿丢了 | **采纳**。有语音算"不空"：`cancel()` 一律先问，`delete_char()` 不退出、提示"Opt+D 拿掉一段 · ` 放弃"；native 测试（§3.3、§12.1）|
| **4** 写的是空格锁定，memo-hold 做的是连按两下；编辑器里连按两下会存走草稿 | **采纳**。§3.2 按 memo-hold 实际的连按两下重写；编辑层去掉 G0 单击 = 保存，Enter 是唯一的保存键，连按两下 = 免按住片段；拿来拍板（§15 第 1 条）；native 测试和 H-VC4 |
| **5** 宽限和收尾时按的键被吞掉，没有反应 | **采纳**。宽限、免按住、收尾时 G0 以外的键先结束这一段，按顺序排着（8 个），这段定了再交给编辑器；按着 G0 时提示"松开 G0 再按"（§3.2）|
| **6** plan 不是硬闸门：确认框只看得到 80 B；plan session 怎么结束没验证 | **采纳**。语音项开之前取全文、分页、只有最后一页能开，每页 400 ms 防误触；cpad 核对这次连接发过全文；摘要用语音部分的开头；新闸门 H-VC8；语音开的 session 不接受设备回复；删掉了"在这里按 Enter 就是在设备上看过"（§3.8、§5.3、§5.4）|
| **7** 截断口述的指令 | **采纳**。`intent=spawn` 超过 500 B 不进队列、降级 `too_long`；`intent=todo` 截断并带"…（截断）"；排队、开 session 的语音一条最多 40 秒，记一条仍 5 分钟。设备上的"太长"提示要一条新的 Mac→设备消息，放 VC-3，这一版靠 40 秒额度让它很少发生，今日笔记写明（§3.5、§5.3、§14）|
| **8** 语音回复是死路，还可能开出一个新 session | **采纳**。回复的编辑器这一版不挂语音，按住 G0 录独立便条并提示"回复要打字"；cpad 拒绝 `agent.spawn{qid}` 指向回复类的项，设备看板上也不发——不管是不是语音，今天就有的洞一起堵上（§2.3、§3.1、§3.8、§5.4 第 5 条）|
| **9** 没管 docs/14 的去向 | **采纳**。只有今日笔记、待办、队列、session 四种去向挂片段；别的去向录独立便条并提示；有语音时不能把去向改成别的；cpad 收到挂在别的去向上的 `voice` 降级到今日笔记（§3.1、§3.3、§5.9）|
| **10** "能力存在 `/cfg`"是错的 | **采纳**，选"一律挂"：去掉 `caps.vcap`，挂不挂不看 Mac 版本；旧 cpad 只会多几行"（语音）"和单独的便条行（§6.5）|
| **11** 今日笔记重复 | **采纳**。带 `voice` 的笔记不马上 `WriteNote`；带 `cap` 的 `rec.result` 和 `rec.stale` 都不出行（§5.5）|
| **12** 录到一半重启，片段和草稿断开 | **采纳**（写明，不在这一版修）：救回的段没有 `cap`，成为普通便条；新闸门 H-VC3b、写进发布说明；开录时就写 `cap` 放 VC-3（§12.2、§14）|
| **13** 靠 mtime 认自己的 `status.json`，重启就认不出 | **采纳**。`status.json` 写 `by:"cpa-stt"`，契约加这个可选字段（§4.7、§7）|
| **14** 环境的问题变成永久失败 | **采纳**。先跑自检：环境的问题只写 `blocked{model_missing / engine}`、不写 `result.json`，修好就照常转；只有单个文件的问题写 `failed`；`cpa-stt retry`、`cpa vcap redo`（§4.6、§4.11、§5.2）|
| **15** 和 WhisperWoof 抢着写；`auto` 悄悄选了 whisper.cpp | **采纳**。默认 `off`，没有 `auto`；契约加认领文件 `.transcriber`，两边都要守（WhisperWoof 那边列为 👤 的 WW 包）；写明怎么换手（§4.4、§4.10）|
| **16** "禁网沙箱"说过头了 | **采纳**，并实测了收紧的配置：只能写工作目录和 Metal 缓存，系统服务只放行两个；连端口、解析域名、写 `$HOME`、`open` 都失败；耗时不变（不放行 Metal 缓存会多 5 秒、不放行编译服务会崩，都记下了）。文案改成"挡直接联网和乱写"；找不到 `sandbox-exec` 就 `blocked`，没有关沙箱的选项（§4.5）|
| **17** 和"不写 app 专用的代码"冲突 | **采纳**，选评审推荐的做法：转写放进独立的 `cpa-stt`，和 WhisperWoof 同一个位置；它只认通用的命令约定，whisper.cpp 只在示例配置里；去掉 Spotlight 找模型和克隆别的 app 的缓存；ADR 0004、docs/15 的两句修改写明在 §4.12，拿来拍板（§15 第 3 条）|
| **18** 中英混说 | **采纳**，并实测：强制 `-l zh` 把一句英文转成乱码，`auto` 逐字正确、混说时和 `zh` 相同，所以默认 `auto`（+0.5 s）；H-VC5 加纯英文、混说、代码名字；配置里的提示词进幻觉句过滤（§4.4、§4.8、§4.9）|
| **19** Opt+D 只能拿掉最后一段、不能撤销，音频和转写还在 | **采纳**。叫"拿掉"不叫"删"，提示"已拿掉 ②"；拿掉的片段记 `rec.hidden`，录音应用里看不到，`cpa rec unhide` 能找回；设备上删未拉走的音频没有做（今天设备上没有删录音的路径）（§2.4、§3.3）|
| **20** 草稿开超过 24 h 就把片段判成孤儿 | **采纳**。同一次开机里不判孤儿，只在换了开机号、outbox 清空以后判（§2.4）|
| **21** Tab 菜单开在编辑器上 | **采纳**。菜单开着时录独立便条（看不见草稿就不挂，和锁着一样），在菜单收到按住之前判断，有测试（§3.1）|
| **22** 合并顺序 | **采纳**。VC-2f 依赖 D4 和 memo-hold 两个分支（§11、§13.2）|
| **23** 转写不能拖住串口 | **采纳**。转写在另一个进程里（`cpa-stt`），cpad 不受影响；集成测试量 60 s 转写期间的 pong 延迟，H-VC6 真机再看（§4.6、§12）|
| **范围** VC-2z 零打字 G0+Q/S | **砍掉**，随 docs/14 F5 做（VC-3）|
| **范围** `vcap.snapshot`、`revs.vcap`、`caps.vcap`、看板上的"语音转写中" | **推迟到 VC-3**；这一版片段行只显示时长和"待传 / 已传 Mac"（§3.6）|
| **范围** MCP 字段、录音应用的"片段·"标题、`cpa vcap ls`、`cpa transcriber setup`、`hint:"projects"` | **砍掉或推迟**（VC-3）。拍板从 6 件减到 3 件 |

## 附：证据索引

- 固件：`lib/shell/compose.cpp:14-15`（ASCII）、`44-65`（`submit`、`cancel`、`delete_char`）、`76-84`（放弃提示）；`lib/input/keymap.cpp:80-82`（编辑层 Enter、G0 单击 = 保存）；`lib/shell/runtime_outbox.cpp:15-17`（`/cfg` 只存 `projects.json`）、`47`（共用 MonoId）；`lib/core/app_state.h:15-22`（`LinkInfo` 的能力只在内存里）；`lib/rec/rec_msgs.h:21-28`（`StartSpec`）、`51-62`（`Started` 带会话 id、`Discarded`）；`lib/rec/rec_meta.h`（`.M` ≤512 B、未知字段忽略、`rcv`）；`lib/rec/rec_core.h`（`kFinishGiveUpMs`）；`lib/shell/confirm.cpp:42、65`（session 在等时存成回复类排队项）；`modules/board/board_logic.cpp:69-74`（看板对任何排队项都开 session）；`lib/protocol/protocol_limits.h:31`（`kQueueTextMax = 80`）。
- 正在做的分支（只读，未提交）：`fix/memo-hold` 的 `lib/input/g0_gesture.h`（`kDoubleTapMs = 350`、`HandsFree`）、`lib/shell/shell_g0.cpp`（`g0_down`、`hands_free`）、`lib/shell/shell.cpp`（`rec_key_event` 在宽限中只认数字、`RecLatch`）、`lib/rec/rec_core.h`（`kMemoGraceMs = 600`）、`lib/input/keymap.*`（`G0Double → RecHandsFree`，没有空格锁定）；`feat/ux-decisions` 的 `lib/shell/shell.cpp:115-124`（`dispatch()`：菜单先于 D4 那一行）、`lib/input/keymap.cpp`（D5：`` ` `` = 退出）、`lib/shell/compose.cpp`。
- daemon：`hub/reduce.py:125-137`（`outbox.item`；记一条马上 `WriteNote`）、`149-157`（`_spawn_text` 用全文）、`160-174`（`agent.spawn`；`172` 的 `SpawnRequest` 用项目的模式）、`177-188`（`agent.reply`）；`agents/claude_code/argv.py:66-71`（`spawn_argv` 带 `--permission-mode`，`resume_argv` 不带）；`recordings/route.py:59-71`（便条行、`rec.stale` 也出行）；`recordings/results.py`（3 s 扫描、`transcribe_wait_h`）；`recordings/contract.py:95-100`（`status.json` 的 `reason` 集合）；`recordings/history.py:124-132`（隐藏只是隐藏）；`config/settings.py:63`（`transcribe_wait_h = 24`）；`service/launchd.py:19`（LaunchAgent 的写法）。
- 协议：`limits.json`；`schema/v0/outbox.item.schema.json`、`common.schema.json`（`userText`、`files{n, rev, live}`、`intent`）、`pong.schema.json`（`pending`、`revs`）、`queue.snapshot.schema.json`（≤12 项、`text` ≤80 B）、`rec.act.schema.json`；`tools/test_worst_lines.py`、`tools/wire.py`（`canonical_line`）；`recording/v1/README.md`（单写者表、`status.json` 的 `blocked{reason}`、交接规则、向前兼容）。
- 文档：docs/02；docs/04 §8.4；docs/13 §7.1、§9.2、§10.3、§13；docs/14（`b5ad361`）§2.1–§2.3、§3、§5.3、§5.4、§6.1、§6.3、§7.8、§8.1、F5；docs/15 §1、§2；docs/17 §6.1、§6.3、§17 第 6 条、M10；docs/18 §2 第 6 条、§3.4；docs/19 §6.6、§6.9、§10；docs/20 U4、U6、U15、U16、U17、U25、U26、D4–D8、D10；ADR 0004（背景"WhisperWoof today, any program tomorrow"、规则 5）。
- 外部：WhisperWoof PR #149–#152（2026-10-22 仍是 OPEN），#151 正文（收件箱默认关、静音门限 0.00084、退避 30 s / 2 min / 8 min、`result.json` 用 link 放置）。
- 本机 [测]：`whisper-cpp 1.8.3`（Homebrew）；`ggml-large-v3-turbo.bin` 1,624,555,275 B；`ggml-tiny.bin` 77,691,713 B；Apple 芯片的 Mac、macOS 26；`$VB/out/bench.json`、`bench-greedy.json`、`*-vad.txt`、`noise05.txt`、`silence05.txt`（第 1 版的测速和幻觉）；`$VC/sbx/profile.sb`（§4.5 的沙箱配置）和 `$VC/sbx/wd/`（各配置的耗时和输出：只禁网 1.19 s；禁写不放行 Metal 缓存 6.66 s、"loaded in 4.988 sec"；全禁系统服务退出码 134、"Unable to reach MTLCompilerService"；只放行编译服务 7.0 s；最终配置 10 秒 1.15–1.17 s、30 秒 1.64–2.20 s，和不包沙箱逐字相同）；沙箱探测（`$VC/conn.py` 连端口 `Operation not permitted`、`curl` 解析失败、`touch $HOME/…` 失败、`open -a TextEdit` 失败）；`$VC/lang/`（`say -v Samantha` 英文、`say -v Tingting` 混说，`-l zh/auto/en` 的输出和耗时）；`$VC/lines2.py`（§6.4）；`$VC/efont.py`（efontCN_12：①–④、…、· 有，🎤 没有）；`~/CardputerNotes/recordings/*/manifest.json`（只读了 manifest：15 个会话，13 段便条 1.0–50.7 秒，2 场会，没有 `result.json`）。
