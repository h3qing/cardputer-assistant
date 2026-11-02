[English](23-mac-inbox.md) · **中文**

# 23 · Mac 上的待归档：没归属的便条排成一列，一键归好或交给 AI

> 状态：**设计稿，待你拍板**（§8）· 日期：2026-10-22（同日按四份评审修订：用户、隐私安全、契约架构、可行性；评审之间的分歧和取舍见 §9）· 角色：架构（Mac 待归档）· 基于 origin/main `aaa9747`（v0.4.0 加 #55，daemon 已含 v0.4.1 的录音编辑事件）· 对应 #54，关联 #52、#53、#19、#34、#13、#46、#24。
> 需求：
> - M1：没归属的便条排成一列，每条写着文字、哪台设备、什么时候；能筛选；能一下归档或交给 AI 跑工作流；录的时候直接按 1–9 归最好，但 1–9 各是什么不好记。
> - M2：软件在不同设备、不同网络下可能表现不同；每段录音要记下当时的网络环境和连着的电脑的名字，因为有好几台 Mac，不同 Mac 上的东西不要混在一起。
> - M3：编辑器里按住 G0 加几段语音（#52，docs/22）：要有一个结构能记下不同的语音便条。
> 依据：docs/15 §1、§2、§3.4–§3.12、§4.6；docs/17 §6.3；docs/18 §1.3、§2、§4、§7、§15；docs/20 D4、D10；docs/21 §5.3–§5.4（`wt-settings`）；docs/22 §2、§3.7、§4.7、§5、§7、§8（`wt-voicecap`）；docs/14 §1.2、§2.3、§5.2（`feat/destinations`，未合并）；ADR 0002、0004；录音契约 v1；`protocol/README.md` 第 20 条；daemon 的 `recordings/`、`archive/`、`mcp/`、`cli/`、`hub/reduce.py`、`queue/`、`events/`、`control/`；固件 `decode_host.cpp`；WhisperWoof PR #149–#152（都还开着）；本机只读的录音目录、事件计数、进程启动时间；本机网络命令实测（不带定位权限）。
> 标记：**[源]** 读过的代码或文档 · **[测]** 本机实测 · **[算]** 推算 · **[推]** 推断 · **[设]** 设计假设，可以改 · **[待]** 要用测试验证 · **👤** 需要你。
> 范围：只写设计。没有接设备、没有开串口、没有改代码；`~/CardputerNotes` 只读了 manifest 和目录列表。
> 路径缩写：`cpad/` = `daemon/src/cpad/`；`dst/` = `feat/destinations`（`b5ad361`）的 `daemon/src/cpad/dest/`；`WW/` = WhisperWoof 工作区 `feat/cardputer-w4-summaries`（`c6b3060`，PR #152 的头，含 #149、#151，已合入 #150）。

## 0. 一页摘要

**你要先做的两件事：**
1. **合并 WhisperWoof #149–#152**，列表里才有字。目前录音都还没转写；在 docs/22 VC-1（cpad 自己转写）之前，转写靠 WhisperWoof。
2. **重启一次 cpad**（`cpa service install`，会先停再起 LaunchAgent [源 cpad/service/launchd.py:79-80]）。正在跑的 cpad 是在 v0.4.1 合并之前启动的，所以 `cpa rec ls` 把有项目的录音也报成没项目（§1.3）。

**设计：**
1. **你要的**：说完一句没归属的话，Mac 上有一列"待归档"，每条有文字、哪台设备、什么时候，能按日期、文字、项目筛；一下归到项目，或者交给 AI。**录的时候按 1–9 仍是最快的路**，#53 让录音页显示 1–9 是谁，Mac 上的待归档是兜底。
2. **今天**：这台 Mac 上的录音大多没项目（便条和会议都有），都还没转写；没有"已归档"这个状态（§1.3）。
3. **模型**（§2）：待归档 = 既没有有效项目、也没有有效去向的便条和会议；项目 key 是别的电脑的，另成一组"别的电脑的项目"。五个状态：新、已归档、已交给 AI、看过了、藏起来。**状态全记在 `events.jsonl`，一个动作只写一条记录**；复用 `rec.edited`、`rec.hidden`、`queue.added`、`queue.done`，加几个可选字段（`done`、`by`、`voice`、`bytes`、`sha8`）和 manifest 的 `env`；不新建文件，不进 WhisperWoof。
4. **标准接口**（§3）：`cpa filing ls|show|file|hand|done|hide|reopen|triage`，JSON 输出是有 schema 和向量的契约 `protocol/filing/v1/`。MCP 只加两个只读工具 `list_unfiled`、`get_filing_item`；**不做 MCP 写工具**（D5）。
5. **交给 AI**（§3.4）：`hand` 只排队，正文由 cpad 自己从转写里取、清洗、截到 500 B，从不进 argv；会议不能交（里面有别人的话）。**开 session 仍然只在设备看板上按 Enter**，固定 plan 模式；Mac 上直接开是备选，要另写 ADR（D7）。👤
6. **环境戳**（§4，M2）：P1 只盖两样：**哪台 Mac 取回的**（你起的名字，默认空，点一下"家 / 公司"）和**录音时是不是连着这台**（能证明才写 `here`）。**口袋里录的（多数）只知道哪台 Mac、什么时候取回；录音时在哪，要等 v0.8 在设备上选档案。** 网络指纹设计好了（§4.3），只在你是"一台笔记本来回带"时才做（D8）。👤
7. **戳只能发现混了，挡不住**：今天任何一台 cpad 都会把设备上所有东西拉走。真正分开靠 docs/18 v0.8（§4.5、§8.2 第 1 条）。👤
8. **界面**（§5）：WhisperWoof 新的 Cardputer 页：左筛选（日期、搜索、项目多选）、中列表、右详情；`1`–`9` 一下归档并跳到下一条，空格播放，`x` 看过了，`q` 交给 AI，⌘Z 可以连着撤销；每个动作有 5 秒提示"已归到 3 acmeshop · ⌘Z 撤销"。只通过 `cpa` 的 JSON 和开放文件说话。PR 5/5 拆成 5a 只读、5b 动作，由你合并。
9. **没装 WhisperWoof**：`cpa filing triage` 在终端里一条一条过，按键一样；不开网页、不开端口（§3.6）。
10. **分期**（§7）：P1 只动 cpad（约 1,800 行代码、2,000 行测试；加网络指纹再 +400 / +400），单独发版，不等固件、不等 VC-1；P2 是 WhisperWoof 页（约 +2,300）；P3 是设备端戳和 #53。
11. **18 条决定**都有默认值（§8.1），**要你拍板的只有 2 件**（§8.2）。

## 1. 你要的，和今天差在哪

### 1.1 需求

- 没归属的便条排成一列（§2.1）。
- 每行有文字、设备、时间（§3.2、§5.1）。
- 按日期、文字、项目、状态、类型筛；设备、连接只在出现两个以上的值时才给（§3.1、§5.1）。
- 一下归档；交给 AI；docs/14 合并后能送到任意配置好的去向（§2.2、§3.4）。
- 录的时候按 1–9 仍是主路；设备上要看得见 1–9（§1.2、§7（#53））。
- 软件按环境表现不同：按电脑，docs/18 把 Mac 绑到档案（自动）；按网络，只提示、不自动切（§4.5、§8.2 第 1 条）。
- 每段录音盖 Mac 端的戳：哪台 Mac 取回、录音时是不是连着它；口袋里录的只知道这些（§4.2）。
- 有好几台 Mac，东西不要混：别的电脑的项目单独一组、不能一键归；真正分开靠 docs/18（§2.1、§4.5、§8.2）。
- 待归档给语音采集留位置（§2.5）。

### 1.2 目标：按你的标准数按键

1. **归一条按一下**：打开页面时第一条"新"的已经选中、焦点在列表上；动作做完自动跳到下一条"新"的。
2. **全程不打字**：归档从 1–9 里选；日期是按钮；给电脑起名点按钮；搜索框可以用 WhisperWoof 自己的听写键说进去。
3. **录的时候归好仍是最快的路**：#53 在录音页显示 1–9 的名字；Mac 上的待归档只收漏网的。
4. **每一下都看得见结果**：5 秒提示写清楚归到了哪；⌘Z 能连着退好几步。
5. **稳**：什么都不删；一个动作只写一条记录，不会停在"归了一半"。
6. **一切在本机**，没有 app 专用的集成（#34、ADR 0004）。

| 做什么 | 设备上（录的时候） | Mac 页 | 终端 `cpa filing triage` |
|---|---|---|---|
| 归到 3 号项目 | 录音时按 `3`：1 下 | `3`：1 下，自动跳下一条 | `3`：1 下 |
| 先听再归（今天的都没字） | — | 空格、`3`：2 下 | `p`、`3`：2 下 |
| 看过了，不需要项目 | — | `x`：1 下 | `x`：1 下 |
| 别的电脑的项目，归到这台的 3 号 | — | `3`、Enter：2 下 | `3`、`y`：2 下 |
| 交给 AI 排队 | （#52 的编辑器语音）| `q`、`3`：2 下（待归档的行本来就没项目）| `q`、`3` |
| 交给 AI，再开 session | 看板上选中、Enter | `s`、`3`：2 下，再到设备看板上按 Enter（D7）| `s`、`3`，同左 |
| 找上周某个项目的便条 | — | "全部"、"上周"、项目：3 下 | `cpa filing ls --filing all --from … --project …` |
| 撤销 | — | ⌘Z，可以连按 | `u` |
| 藏起来（设备录音应用和 AI 也看不到）| 录音应用里隐藏 | 详情里的按钮 | `cpa filing hide ID` |

没有字的行，`q`、`s` 是灰的，写着"还没转写，先归档或等转写"，按了也不会先动再报错。

### 1.3 今天的样子

- **这台 Mac 上** [测]：`~/CardputerNotes/recordings/` 里的会话有便条也有会议，大多没项目，都没有 `dest`。每个目录只有 `manifest.json` 和 `seg-000.wav`，没有任何转写文件。`events.jsonl` 里有 `rec.archived`、`rec.stale`，没有 `rec.result`、`rec.edited`。所以今天的待归档**全都没有字**。
- **没有"已归档"**，只有零散的信号：

| 信号 | 从哪来 | 谁认 |
|---|---|---|
| `manifest.project` | 录音时按的数字，归档时写一次；只看第 0 段 [源 cpad/recordings/manifest.py:73-79] | 几乎所有读者 |
| `rec.edited{project}` | 之后改的项目 [源 cpad/recordings/records.py:96-98] | 只有 `cpa rec ls/show` 和设备的 `rec.list` [源 cpad/recordings/sessions.py:81-89] |
| 第 0 段 `dest` | 录音时的去向键 [源 protocol/README.md 第 20 条] | 今天的 cpad 只认 `todo`（笔记打勾），别的都当普通笔记行 [源 cpad/notes/render.py:19] |
| `rec.hidden` | 设备上隐藏 | `rec.list`、`cpa rec ls`、所有 MCP 读 |

- **`cpa rec ls --json` 把每条都报成 `project:null`，是版本错位，不是读的逻辑错** [测]：
  - 正在跑的 cpad 是在 v0.4.1（`129c101`）合并之前启动的；`$CPA_HOME` 里也没有 v0.4.1 才会建的 `host_id`。
  - 老 cpad 回的 `{"cmd":"rec"}` 行里没有 `project` 键；新的 `cpa` 在上面套编辑时用 `row.get("project")`，于是得到 `None` [源 cpad/recordings/sessions.py:81-83]。manifest 里有几条是有项目的，`rec.archived` 里也是。
  - 两边都看不出错位：`__version__` 一直是 `"0.1.0"` [源 cpad/__init__.py:3；daemon/pyproject.toml:3]。P1-A 修：控制回复加整数 `api`，`cpa status` 发现 cpad 比 `cpa` 老就提示"重启 cpad"；`_edited` 在行里没有 `project` 时退回 `rec.archived` 的项目。
- **真实存在、但这台 Mac 今天碰不到的缺口**：MCP 的 `list_recordings` / `get_recording`、待办、采集、导出、今日笔记的标签都只看 manifest，不看 `rec.edited` [源 cpad/archive/recordings.py:269-291；cpad/recordings/results.py:85-91]。今天 0 条 `rec.edited`，所以还没表现出来；P1-A 让所有读者用同一个"有效项目"。
- **别的缺口**：MCP 的项目筛选表达不了"没项目" [源 cpad/mcp/schemas.py:48、137-140]；`cpa rec` 按设计从不显示文字 [源 cpad/cli/rec.py:1-2]；写的路只有 `cpa rec unhide` 和只能排 `todo` 的 `queue_add` [源 cpad/cli/rec.py:142-169；cpad/mcp/backend.py:155-166]；`device.seen` 只有 `dev`、序列号、固件，没有电脑和网络 [源 cpad/events/records.py:97-98]。
- **命名**："inbox"已经被占了三处：cpad 里设备上的"等你"列表（`cpad/inbox/need.py`、线上的 `inbox.dismiss`，schema 在 `protocol/schema/v0/inbox.dismiss.schema.json`）[源 cpad/inbox/need.py:1-7；cpad/protocol/specs.py:212]；WhisperWoof 自己的转写收件箱（`core/cardputer/inbox-*.js`）；docs/15 C 期 `send_to_device` 的目标。所以对外的名字一律跟代码包 `cpad/filing/` 走：命令 `cpa filing`、契约 `protocol/filing/v1/`、MCP `list_unfiled`；界面叫"待归档"。

## 2. 模型

### 2.1 什么算待归档

一个录音会话 R 在"待归档"里，当且仅当：
1. 已归档（有 `manifest.json`）。设备上还没拉过来的段不列，列表顶上只说"设备上还有 N 段"。
2. **没有有效项目**，或者有效项目不是这台 Mac 登记过的 key。
   - 有效项目 = 日志里最后一条 `rec.edited{project}` 或 `hand` 出的排队项（§2.2）的项目；都没有时，便条取 manifest 的项目，**会议取最后一个已落地段的项目或槽位**（从 `rec.seg.stored` 读，不等 #46 修 manifest 的"只看第 0 段" [源 cpad/recordings/manifest.py:73-79]）。（2026-10-22，合并 main）#50 已经修了：`manifest.json` 现在也取最后一段的项目或槽位 [源 cpad/recordings/manifest.py:77-84]，和折叠一致；折叠照旧读 `rec.seg.stored`。
   - 设备的 1–9 缓存来自另一台 Mac 时，manifest 会照写一个这台 Mac 不认识的 key。这种行进**单独一组"别的电脑的项目"**，行里带 `asked`（录音时按的 key），界面写"录音时按的是 acme（不是这台 Mac 的项目）"。
3. **没有有效去向**。有效去向 = 最后一条 `dest.decided` 的 `dest`，不是 `daily`，并且没有被 `dest.dropped`、`dest.expired` 或最终的 `dest.failed` 结束；去向是 `queue` / `session` 的，还要真有 `from_item=R` 的排队项。docs/14 合并以前：只有第 0 段 `dest:"todo"` 算；别的 `dest`、或者有 `dest.downgraded` 的，都算没有去向（今天的 cpad 把它们当普通笔记行 [源 cpad/notes/render.py:19]）。所以一段按住 G0 再按 Q、却没选项目的零打字便条（docs/22 §3.6）会出现在这里，不会两头都不管。
   （2026-10-22，docs/25 R1-D）暂存中的（`dest.decided` 的 `action:"hold"`：等确认、等设备）不算有效去向：这样的录音照样在待归档里，同时出现在"等你送"（`cpa dest pending`）。从 Mac 送出去（`cpa filing send`）不改归档状态：它交给了别的 app，项目还是要归。
4. 状态是"新"（§2.2）。
5. manifest 里没有 `cap`（docs/22 §7：带 `cap` 的是某条采集的一段，从落地那一刻就知道）。采集按 §2.5 处理。

- **没有字的照样列**，状态写清楚：拉取中、等转写、转写中、转写程序跑不了、只有转写、有纪要、转写失败、超时没转（`rec.stale`，24 h）。没字也能归：空格听一遍再按数字。
- 另有一个筛选"**没转出文字**"：不管状态，列出所有失败或超时的，免得一段话悄悄没了字。cpad 支持重转（docs/22 VC 的 `cpa rec retry`）以后才显示"重转"按钮。
- **不收打字的笔记**：它们已经在今日笔记里，有字可搜 [设]。

### 2.2 状态

| 状态 | 意思 | 怎么到 | 记成什么（一个动作一条） | 离开待归档？ |
|---|---|---|---|---|
| 新 | 还没决定 | 归档时没项目、没去向 | — | 在 |
| 已归档 | 归到项目 P | 录音时按数字；设备录音应用改；`cpa filing file`；Mac 页按数字 | 现有 `rec.edited{project}`，加可选 `by:"cli"`（设备改的不带 `by`：没有就是设备）| 离开 |
| 已交给 AI | 从它建了排队项 | `cpa filing hand` | 现有 `queue.added`，`origin:"inbox"`、`from_item:R`、`project`、`intent`，加 `voice:true`、`bytes`、`sha8`（§3.4）| 离开；在"已交给 AI"里看排队中、已开、已完成 |
| 看过了 | 看过了，不需要项目 | `cpa filing done`；Mac 页 `x` | `rec.edited{done:true, by}`（新字段）| 离开；**到处照样搜得到** |
| 藏起来 | 设备录音应用、`cpa rec ls`、MCP 都不再列 | `cpa filing hide`；Mac 页详情里的按钮 | 现有 `rec.hidden`，加可选 `by` | 离开；**文件一个不删**；有打开的排队项时拒绝（`E_ALREADY`）|

- **退回"新"**：`reopen ID` 撤掉当前状态的那一个原因：`rec.edited{done:false}`、`rec.unhidden`，或者把 `hand` 的排队项撤回：`queue.done{qid, by:"undo"}`（排队项已经开了 session 时回 `E_ALREADY`）。改项目用 `file`，退成没项目用 `file --none`。
- （2026-10-22，实现）**有打开的排队项时 `file` 也拒绝**（`E_ALREADY`，`field:"queue"`）：排队项保留它的项目，先 `reopen` 撤回再归。不然"交给 AI、再归到别处"以后连着撤销回不到最开始，行上的项目也和排队项跑的项目对不上。
- （2026-10-22，实现）**设备正在开的时候撤回**：`queue.spawned` 在开完（最多约 20 s）以后才记，撤回可能落在中间。折叠把之后又记了 `queue.spawned` 的排队项仍算已交给 AI（`open:false`），不会回到"新"、被再交一次。
- （2026-10-22，合并 main）**`cpa queue reopen`（docs/20 U12，`queue.reopened`）也重开交出去的排队项**：折叠把它重新算成打开的 `hand`，撤回过的也重新算数，直到再撤回一次。所以重开以后 `file`、`hide`、再 `hand` 照样回 `E_ALREADY`，`cpa filing reopen` 能撤回它 [源 cpad/filing/fold.py:122-124、143-147；tests/unit/test_filing_fold.py:111-131]。还没定：排队项关掉以后录音又归到了别的项目、再重开，行上是后来的项目，排队项跑原来的项目；要不要把重开当成重新选了排队项的项目。
- **撤回排队项用 `queue.done` 加 `by:"undo"`**，不另造事件：老 cpad 的折叠本来就在 `queue.done` 时关掉这一项 [源 cpad/queue/fold.py:11、50]；新折叠看到 `by:"undo"` 就不再算"已交给 AI"。
- **状态由纯函数折叠得出**，照 `queue/fold.py` 的写法；判断顺序：隐藏 → 藏起来；有 `from_item=R`、没被撤回的排队项 → 已交给 AI；有效项目已登记或有有效去向 → 已归档；`done` → 看过了；否则 → 新（有效项目是别的电脑的 key 时进"别的电脑的项目"组）。
- **老 cpad 不会坏** [源]：`rec.edited{done}` 能过 `CHECKS`，什么都不改 [源 cpad/recordings/index.py:45-69]；`QueueItem` 丢掉不认识的字段 [源 cpad/queue/fold.py:36-40]。但老 cpad 不认 `origin:"inbox"` 的 plan 规则，所以 `hand` 发现 cpad 在跑而 `api` 太旧时回 `E_UNAVAILABLE`、不排队。

### 2.3 状态记在哪

| 方案 | 好处 | 问题 |
|---|---|---|
| **A `events.jsonl`（推荐）** | 已有文件锁和 fsync [源 cpad/events/log.py:87-100]；cpad 运行时别的进程追加，3 s 内被 housekeeping 收进来 [源 cpad/app.py:354-361]，`cpa rec unhide` 就是这么做的 [源 cpad/cli/rec.py:142-153]；排队项本来就只存在这里 [源 docs/15 §2.1]；重放幂等 | 不是开放文件：由 `cpa export` 整理成 `data/filing.jsonl` 导出 |
| B 录音目录里再放 `filing.json` | 别的程序直接读 | 录音契约要多一个写入者、改单写者表；同一个状态存两处会漂移；语音采集不在录音目录里 |
| C 单独的索引文件 | 读得快 | 第二个真相来源；原子写和恢复都要另做 |

- **一个动作一条记录**：`append` 把几行一次 `os.write` 再 fsync [源 cpad/events/log.py:99-100]，崩溃时可能只留下前几行，读的一方只跳过撕裂的最后一行 [源 cpad/events/log.py:58-67]。所以每个动作只写一条：`file` 只写 `rec.edited`，`hand` 只写 `queue.added`（"已归档到 P"由折叠从它推出来），一条采集的归档只写一条 `item.edited`。撕裂的尾巴等于这个动作没做，重做一次就收敛。
- **导出**：`cpa export` 加 `data/filing.jsonl`（每行一条，形状同 §3.2 的行，`hidden` 也在，隐藏不是删除）；做网络指纹时再加 `data/networks.json`。`CPA-EXPORT.json` 的计数和导出的 README 跟着加，layout 仍是 1（§6.2）。
- **版本**：事件只新增字段。对外契约是 `cpa filing … --json` 的形状（§3.2），规则照录音契约 v1：只新增、不写 `additionalProperties:false`、改含义才升 v2 [源 protocol/recording/v1/README.md"向前兼容"]。
- **永远不放进 WhisperWoof**：它不存状态，每次问 `cpa`；它的设计也明确不把录音放进自己的数据库 [源 WW/docs/design/cardputer-inbox.md:248-253]。

### 2.4 归档以后今日笔记怎么办

今日笔记只追加、从不改写 [源 cpad/notes/writer.py:36-67]；每一行由一条事件画出，所以启动时能重放 [源 cpad/recordings/route.py:1-6、69-82；cpad/app.py:122]。
- **结果到之前归的**：之后写的那一行直接用有效项目。要改 `results.result_record`，今天它抄的是 `rec.archived` 的上下文 [源 cpad/recordings/results.py:85-91]。
- **结果到之后归的**：`rec.edited` 只带 `rec` 和 `project`，画不出一行 [源 cpad/recordings/records.py:96-98]。所以：
  - **只有 cpad 写这一行**，CLI 不碰笔记（`NotesWriter` 不加锁，两个写的人会撞 [源 cpad/notes/writer.py:55-67]）；
  - cpad 在 housekeeping 收到 `rec.edited{project}` 或 `hand` 的排队项时，如果这一段已经有 `rec.result`，追加一条自带上下文的 `rec.filed{rec, project, dir, mode, started_ms, uncertain, title, n}`，由 `route.rec_entry` 画成 `- ~HH:MM #项目 ↪ 归档：<标题或"语音"> ^cpa-<id>-f<n>-<key>`，按块 id 去重，旧行不动（`-u` 后缀是先例 [源 cpad/recordings/route.py:62-66]）；（2026-10-22，实现）`n` 是这段录音的第几条 `rec.filed`：只用 `-f-<key>` 时，归到 cpa、改成 blog、再撤回 cpa，第三行的块 id 已经有了，笔记会停在 blog；
  - 今天 housekeeping 不写笔记 [源 cpad/app.py:354-361]，P1-A 加上；启动时 backfill 重放 `rec.filed`，并给日志里还没有 `rec.filed` 的归档补一条。
- 采集的归档行用条目的块 id：`^cpa-<条目 id>-f<n>-<key>`。
- （2026-10-22，实现）便条自己那一行待办（`- [ ] … ^cpa-<id>`）留着录音时的 `#标签`，之后的"↪ 归档"那行不是待办，所以 MCP `list_todos` 对块 id 是录音的待办行也用有效项目（打字的笔记 id 从来不是录音 id，照旧用自己的标签）。
- 看过了、藏起来：不写笔记。

### 2.5 语音采集（#52）

- docs/22 里每一段语音是一条带 `cap` 的便条，采集按 id 引用它们（`body.voice`，最多 4 段），拼好时记 `vcap.assembled` [源 docs/22 §2.1、§5.6]。
- **带 `cap` 的录音从不单独成行**（看 manifest 的 `cap`，落地就有，不等折叠）。采集是一行"采集 · 2 段"：打的字，加每段的转写或"还没转写"，各自能播。
  - 编辑器里 Opt+D 删掉的段**永远不列**；
  - 还在等条目的段不列，列表顶上计数："另有 2 段语音在等它的条目"；
  - `vcap.orphan` 的段（没人认领，docs/22 §2.4）按普通便条单独成行。
- 排队、开 session、回复这几种采集本来就有项目，算已归档（在"已交给 AI"里看）。只有去向是 `daily`、又没项目的"记一条"采集进待归档。
- **归一条采集 = 一条新事件 `item.edited{ref, project, by}`**，由 vcap 的折叠读：采集的项目在 `item.received.body.project`，往各段上写 `rec.edited` 改不了它。段本身不写任何编辑。
- 一段只属于一条采集，先到的算 [源 docs/22 §5.7]，所以不会被归两次。VC-2 的 `vcap` 折叠合并以前没有采集，这一节什么都不做。

## 3. 标准接口：`cpa filing` 和 MCP

### 3.1 命令

| 命令 | 做什么 | 写什么 |
|---|---|---|
| `cpa filing` = `cpa filing ls [--json]` | 默认只列"新"、不含别的电脑的项目，新的在前 | — |
| 筛选 | `--from/--to YYYY-MM-DD`、`--device ID`、`--link here\|unknown`、`--mode memo\|meeting`、`--project KEY`（可以给几次）`\|--none`、`--filing new\|filed\|handed\|done\|hidden\|all`、`--tx …`（§3.2 的枚举）、`--text 字` 或 `--text -`（从 stdin 读，字面子串、不分大小写，和 MCP `search` 同规则 [源 cpad/archive/search.py:1-9]）、`--orphans`（加上别的电脑的项目）、`--limit`、`--cursor`；做网络指纹时加 `--net ID`；v0.8 起加 `--prof` | — |
| `cpa filing show ID [--json]` | 全文、逐句转写（带 mm:ss）、纪要里的待办、音频文件名、固件版本、排队项的正文 | — |
| `cpa filing file ID KEY` / `--none` | 归到项目 / 退成没项目；别的电脑的项目那一组要加 `--other`（界面和 triage 先问一句再加）| `rec.edited{project, by:"cli"}` |
| `cpa filing hand ID [KEY] [--spawn]` | 交给 AI（§3.4）| `queue.added` |
| `cpa filing done ID` / `hide ID` / `reopen ID` | 看过了 / 藏起来 / 退回"新" | `rec.edited{done}` / `rec.hidden` / 反过来 |
| `cpa filing triage` | 终端里一条一条过（§3.6）| 同上 |
| `cpa project ls --json` | `{"v":1, "projects":[{key, name, slot, ok}]}`，不给本机路径（今天只打印文字，还带路径 [源 cpad/cli/commands.py:29-33]）| — |
| `cpa host name -- 名字` / `--clear` [`--json`] | 给这台 Mac 起名（§4.2）；（2026-10-22，实现）`--json` 打印契约的 `host` 文档 `{v, host}`（读、设、清都是），WhisperWoof 的起名横幅靠它知道起没起过 | `config.json` 的 `host_label` |
| `cpa net …` | 只在做网络指纹时有（§4.3）| `$CPA_HOME/networks.json` |
| `cpa filing send ID DEST` | docs/14 D5-A 合并以后：送到任意开着的去向（§3.4）。（2026-10-22，docs/25 R1-D 实现）只收 `clip`、`file.*`、`url.*`、`app.*`；`--json` 打印契约 `protocol/dest-cli/v1` 的 `sent` 或 `error` | 经 docs/14 的路由：`dest.decided{origin:"mac", how:"pick"}`，再 `dest.handed` / `delivered` / `failed` |

ID 是录音会话 id 或采集的条目 id，都是 MonoId，不分大小写。

`cpa filing` 只从 `events.jsonl` 和文件算，只向 cpad 要"设备上还有几段"（cpad 没在跑时 `counts.on_device` 是 `null`）。所以 cpad 和 `cpa` 版本错位时（§1.3）列表也不会算错。

（2026-10-22，实现）
- 这一问只等 0.5 s：cpad 卡住时列表照样出来，`on_device` 是 `null`，不会碰到 WhisperWoof 的 5 s 超时（`hand` 检查 cpad 版本等 2 s）。
- 给人用的两个便利（只在命令行，不进契约、MCP 没有）：只给 `--project` 时按 `--filing all` 列（"新"的行从来没有项目）；`--from`、`--to` 也认 `today`、`yesterday`。
- 带 `--json` 时，argparse 不认的参数（例如以后的 `--net`、`--prof`）也打印错误文档 `E_BAD_ARG`、`field:"argv"`，退出码 2；更早的 `cpa` 这时 stdout 是空的，WhisperWoof 照"更新 cpa"处理。

### 3.2 契约 `filing/v1`：`ls --json` 的一行

```json
{"v":1, "counts":{"new":3, "other_mac":0, "clips_waiting":0, "on_device":0}, "next_cursor":null, "rows":[{
  "id":"…-0002Q-0002", "kind":"memo", "filing":"new", "why":["unfiled"], "asked":null,
  "at":{"wall_ms":1792639380000, "tz_min":480, "uncertain":false}, "dur_ms":24000,
  "device":"5c0e9a7b3d21",
  "env":{"host":"公司", "link":"unknown"},
  "prof":null, "project":null, "starred":false,
  "tx":"waiting", "title":null, "text":null, "intent":null,
  "clips":null, "queue":null}]}
```

- `filing`：`new`、`filed`、`handed`、`done`、`hidden`。字段名不叫 `state`：MCP 的 `list_recordings` 里 `state:"done"` 是"转写好了" [源 docs/15 §3.4]。
- `why`：`unfiled`、`orphan`（项目是别的电脑的）、`no_text`（失败或超时）。`asked`：orphan 行里录音时按的 key，别的行是 `null`。
- `tx`：沿用 docs/15 §3.4 录音的 `state` 枚举（`pulling`、`waiting`、`processing`、`blocked`、`transcript`、`done`、`failed`），加 `stale`。
- `device`：`HMAC-SHA256(id_key, "dev|"+dev)` 的前 6 字节（12 位十六进制）。**不给 `dev`**：它是设备出厂的 MAC 地址 [源 firmware/app/lib/hal/hal_platform.cpp:39-40]，docs/15 特意不给 MCP [源 docs/15 §3.4"不提供"]。`id_key` 是 `$CPA_HOME/id_key`，32 字节随机、0600，不导出、不进日志，和 `host_id` 一样 [源 cpad/config/host.py:1-5]。每台 Mac 一把，所以两台 Mac 上的 id 连不起来。
- **不给 `dir`**：docs/15 原则 6 是"给 id，不给路径"；WhisperWoof 自己扫目录（§5.3）。
- `text`：`memo.text`，没有就拿转写的前几句；经 `display_safe()`（§3.3）后最多 280 字。`--no-text` 时不给 `title`、`text`。
- `env`：老录音为 `null`；做网络指纹时多 `net:{id, kind, when, label}`，`label` 是读的时候从 `networks.json` 查的，不是存下来的（§4.3）。
- `clips` 只在采集行；`queue` 是交给 AI 后的 `{qid, intent, open}`。
- **可扩展的枚举**（`kind`、`why`、`tx`、`link`，以后会有 `capture`、`other`、`net.kind`）在 schema 里用 `x-knownValues` 标出，照 `marks[].k` 的先例 [源 protocol/recording/v1/README.md:55]：读的一方遇到不认识的值，那一项照"未知"显示，不拒绝整行。
- **契约目录**：`protocol/filing/v1/{README.md, schema/{ls,show,result,error,projects,host}.schema.json, vectors/{valid,invalid}/, VECTORS.sha256}`，做网络指纹时加 `networks.schema.json`；检查器 `protocol/filing/tools/check_filing.py`，有 `REQUIRED_INVALID` 列表。schema 只用 WhisperWoof 校验器支持的关键字（不能用 `oneOf`、`else`、`not`、`format`、`patternProperties`、`exclusiveMinimum`、`additionalProperties`），不然它启动时就抛错 [源 WW/src/whisperwoof/core/cardputer/contract-schema.js:16-37、76-81]。（2026-10-22，实现）检查器照它的做法用允许列表（`KEYWORDS`，加 `x-` 注解），`default`、`$comment` 这类也会被挡住；`host` 文档是 `cpa host name --json` 的输出；长度按码点算，向量里有超出基本平面的字。
- **文字在哪**：`text` 只到你的终端、app 页面和（经 `list_unfiled`）你开的 MCP 客户端。事件里：`rec.*` 从不带转写的句子 [源 cpad/recordings/records.py:17]；`rec.result` 带标题和 `memo_text`（今日笔记要用）[源 cpad/recordings/records.py:76-81]；`hand` 把 ≤500 B 抄进 `queue.added.text`，和设备上打的排队项一样（§3.4）。

### 3.3 写命令的规则

1. **argv 里只有 id、项目 key、枚举**，每个都先校验：MonoId；`^[a-z0-9][a-z0-9-]{0,11}$` 而且已登记 [源 cpad/projects/registry.py:1-6]（唯一的例外：撤销时恢复成这条录音自己带过的 key，即 manifest 里那个别的电脑的 key；（2026-10-22，实现）也包括之前给它设过、后来这台 Mac 删掉的 key）；枚举。正文从不进 argv。WhisperWoof 的搜索词走 stdin（`--text -`）。名字（电脑名、网络名）只能是固定按钮的值，或者你在终端里自己打的，一律放在 `--` 后面，`-` 开头拒绝。
2. **一个动作一条记录**，fsync 后才返回（§2.3）。
3. **幂等**：已经是这个状态就不写，回 `changed:false`；已经有打开的排队项时再 `hand` 回 `E_ALREADY`。
4. **统一的输出**（schema 在 `protocol/filing/v1/`）：成功 `{"ok":true, "v":1, "id", "filing", "changed", "undo":{op, id, args}|null}`；失败 `{"ok":false, "v":1, "error":"E_…", "field"?}`，错误码固定（`E_BAD_ARG`、`E_UNKNOWN_ID`、`E_UNKNOWN_PROJECT`、`E_NO_TEXT`、`E_ALREADY`、`E_UNAVAILABLE`），退出码 0 / 1 / 2，不回显输入。
5. cpad 没在运行也能写。
6. **撤销**：`undo` 是个中立的对象，`cpa` 打印成 argv，WhisperWoof 不照抄 argv，而是用自己的动词表重建、再校验 id。`file` 的撤销是 `{op:"file", args:{project:<之前的值或 null>}}`；`done`、`hide`、`hand` 的撤销是 `{op:"reopen"}`；排队项开过 session 的 `hand` 没有撤销（`undo:null`）。撤销本身又是一条普通事件，不存在"撤销失败卡在一半"。
7. **`display_safe()`**：放在 `cpad/filing/text.py`，所有给人看的输出（终端、JSON 里的 `title` / `text`、triage 的提示）都过它：去掉 C0、DEL、C1（U+0080–009F，含 U+009B）、双向控制符（U+202A–202E、U+2066–2069）、U+2028 / U+2029；（2026-10-22，实现）还有单独的代理码点（U+D800–DFFF：转写程序截断的半个表情符号，JSON 里合法，却让任何 UTF-8 输出出错，一段转写就能让整个列表、导出和 `hand` 失败）。今天的 `NO_CONTROL` 只管 C0 和 DEL [源 cpad/protocol/validate.py:19]；这些字符可能来自 LLM 的标题、`summary.md` 或第三方转写程序（docs/22 §4.2）。

### 3.4 交给 AI

- `hand ID KEY`：KEY 省略时用有效项目；没有项目，或者项目是别的电脑的，回 `E_UNKNOWN_PROJECT`（Mac 页接着让你按数字）。
- **只收便条和采集**：会议回 `E_BAD_ARG`（`field:"id"`）。会议里有别人的话，docs/17 §6.3 要求进队列的就是你看到的那 ≤96 B、带"会议·"；docs/22 §8 第 1 条说永远不把会议转写拼进排队正文。会议的待办走 docs/17 §6 一条一条确认。
- **正文 cpad 自己取**：`memo.text`，没有就拼 `transcript.json` 的句子；用 `queue_text()` 清洗（控制字符换空格、截到 500 B 不截半个字），截完是空的或以 `-` 开头就拒 [源 dst/router.py:100-106；docs/22 §5.3]。没字回 `E_NO_TEXT`。**不加任何后缀**：和 docs/14、docs/22 用同一个函数，同一份正文规则。docs/14 先合并就直接用它的；P1-C 先做就把它放进 `cpad/filing/text.py`，docs/14 合并时改成引用这里。
- **记录**：一条 `queue.added{origin:"inbox", from_item:ID, project, intent:"todo"|"spawn", voice:true, bytes, sha8}`。`bytes`、`sha8` 和 docs/22 §5.6 一样：正文的 UTF-8 字节数和 sha256 前 8 位。`voice:true` 是 docs/22 §5.4 第 2 条的字段；不用 `uncertain`，那是"可能已开过"（docs/22 §5.4 第 6 条）。
- **看板和开 session**：P1-C 在 `hub/reduce.py` 的 `AgentSpawn` 分支加一条：排队项 `origin == "inbox"` 时一律 `plan`，不看项目配的模式 [源 cpad/hub/reduce.py:172]；看板摘要以"语音·"开头（docs/22 §3.7；摘要在 `queue_rows` 里生成 [源 cpad/hub/snapshots.py:101-106]）。按 `origin` 判断，是因为 `QueueItem` 今天会丢掉 `voice` [源 cpad/queue/fold.py:36-40]，docs/22 也还没合并；它合并以后两条规则改成认 `voice`，`hand` 出的项照样适用。
- **`--spawn`（D7 默认）**：只排成 `intent=spawn`，开要到设备看板上选中它、在确认框上按 Enter，这是 ADR 0002 现有的路 [源 docs/adr/0002 第 3 条；docs/22 §5.4 第 1–2 条]。Mac 页按 `s` 以后提示"已排队 q12 · 到设备看板上按 Enter 开（plan）"，详情里显示按 qid 读回来的排队正文（≤500 B），你在设备上开的就是这段。
- **Mac 上直接开（D7 备选，👤）**：今天任何以你的身份跑的进程都能连 `cpad.sock`（0600，只按 Unix 用户区分 [源 cpad/control/server.py]），也能跑 `cpa …`；Codex 在伪终端里跑命令，TTY 也证明不了有人在。所以"客户端说它弹过框"（`--yes`）不能当确认。选了备选就这样做：
  - 确认归 cpad：收到 `{"cmd":"inbox_start","qid","sha8"}` 时，cpad 自己用 `/usr/bin/osascript`（参数数组）弹一个固定文案的框，里面是按 qid 读回、过了 `display_safe()` 的正文；sha8 对不上回 `E_BUSY`；只接受 `origin:"inbox"`、还没开过、10 分钟内排的项；
  - 不能复用设备那条 reducer 分支：它会给设备回一个设备不认识的 `agent.result`，而且控制客户端 5 s 就超时、开 session 要 20 s [源 cpad/hub/reduce.py:90-102、159-174；cpad/control/client.py:15；cpad/agents/claude_code/adapter.py:34]。要新的 `inputs.MacSpawn(qid, project, re="mac-<n>")`，和设备共用一个纯函数 `_spawn_checks()`，`mode="plan"` 显式传；完成时只追加、不发给设备；控制处理先 `events.poll()` 再分派，马上回 `{"ok":true,"accepted":qid}`，调用方等 `queue.spawned` 或 `agent.spawn_failed`；记 `agent.spawned{via:"mac", parent}`；
  - 要写 ADR 0005，修订 ADR 0002 第 3 条、docs/15 §3.6、docs/22 §5.4 第 1–2 条和 `hub/reduce.py` 开头"只执行在线的 agent.* 请求"那句；并写明：Mac 上的确认只防手滑，不防本机程序，一个有 shell 的 agent 本来就能直接跑 `claude`；
  - 多出约 250 行代码、350 行测试。
- **"工作流"不包括云端插件**：WhisperWoof 的 Todoist、Notion、Slack 等插件 [源 WW/src/whisperwoof/bridge/plugin-bridge.js:16-20] 不出现在这里。（2026-10-22，docs/25）改成：**任何你开启过、标了云的去向**（`url.*`、`app.*`、会同步的 `file.*`）都能经 `cpa filing send` 送出去，每个都是你加的时候确认过、`cpa audit --leaves-mac` 看得到；仍然没有 app 专用的插件。
- **送到别的去向（P1-G，等 docs/14 D5-A）**：M1 的"交给 AI 跑工作流"，#24 的"按键 = 去向"。`cpa filing send ID DEST` 交出 `Capture{ref:ID, source:"voice", dest, how:"pick", project, text}`，经 `dest/router._decided` 判定 [源 dst/router.py:117-126]，所以 `dest.decided` 记进 `cpa audit`、`bad_text` 照样降级。CLI 能送到任何开着的去向（`todo`、开了 `optin` 的 `file.*`、`queue`、`session`，`session` 也只排队）。那时 `hand` 就是 `send ID queue|session`，只剩一条路。docs/14 §1.2"改投任意一条旧笔记不负责"要加一条带日期的说明：没归档的录音可以经 `cpa filing send` 改投。
- （2026-10-22，docs/25 R1-D 实现）`cpa filing send ID DEST` 和"等你送"的 `cpa dest send REF [DEST]` 是同一条路：
  - 交出 `Capture{ref:ID, source:"voice", how:"pick", project:有效项目, text:memo.text}`，**从不拿拼起来的转写**；`origin:"mac"`（docs/25 §5.2）；先跑 `decide()`，被拒就什么都不写，回固定的错误码（`E_NEEDS_DEVICE`、`E_NO_TEXT`、`E_UNKNOWN_DEST`、`E_DEST_OFF`、`E_OTHER_PROFILE`、`E_BUSY` 等，见 `protocol/dest-cli/v1`）。
  - 会议只能去本机文件（`leaves_mac:false` 的 `file.*`）；别的回 `E_BAD_ARG`（`field:"id"`），和 `hand` 一样。
  - **和上面原计划不同**：`daily`、`todo`、`queue`、`session` 不收（`E_BAD_ARG`，`field:"dest"`）。今日笔记那一行已经写了；`todo` 要改写笔记里那一行，cpad 还做不到（docs/14 `NO_REROUTE`）；交给 AI 仍是 `cpa filing hand ID [KEY] [--spawn]`，它的规则（plan、`origin:"inbox"`、500 B）不变。
  - 最多一次：锁里先写开始投递的 `dest.decided`（同一条还在送时回 `E_BUSY`），再交出去，再记结果；失败的留在"等你送"，只有你再送一次才再试。
- MCP 没有、也永远不会有能开 session 的工具 [源 docs/15 §3.6"永远不会有的写入"]。

### 3.5 MCP

| 工具 | 默认 | 说明 |
|---|---|---|
| `list_unfiled` | 开（和别的只读工具一样）| 和 `cpa filing ls --json` 同一个函数、**完全同样的筛选**；`filing` 只能是 `new\|filed\|handed\|done`，"all"也**从不含藏起来的**；别的电脑的项目要 `orphans:true` 才给；`outputSchema` 直接 `$ref` 契约的行 schema（（2026-10-22，实现）改成内联的副本：MCP 客户端自己编译 `outputSchema`，跟不过 `$ref` 到 `protocol/`；`tests/contract/test_filing_contract.py` 钉住字段一致，见 docs/15 §3.4）；文字按 docs/15 §3.7 标成不可信数据 |
| `get_filing_item{id}` | 开 | 返回 `show` 的 schema；藏起来的回"没有这条" |

- **MCP 的行比 CLI 少**：没有 `env.net`（地点和时间连起来就是行踪），没有 `fw`、没有 `dev`、没有 `dir`。`env.host` 给：它只是你起的名字（D10），默认空。
- **没有写工具**（D5）。#54 里提过"可选的 MCP 写工具"，但没人要，WhisperWoof 走 CLI 写，本地 agent 也能跑 `cpa filing file`。以后真要做，先得：给 docs/15 §3.6 和 ADR 0004 加一条带日期的修订（归档和"看过了"是只追加、可撤销的注释）；不给"藏起来"；`hand` 只能 `intent:"todo"`、写 `origin:"mcp"`；界面上 `by:"mcp"` 的显示"AI 改过"并能筛；工具名 `file_item`、`set_filing_state`、`hand_item`。
- **顺手修**：`list_recordings` / `get_recording` 改用有效项目，行里加 `env.host`、`env.link`；`list_queue`、`list_captures` 跳过来源（`from_item`）已藏起来的项，补上 docs/15 §3.4"隐藏的录音不返回"那条。

### 3.6 没装 WhisperWoof：终端就够

`cpa filing triage` 一屏一条：

```
3/7   10-20 09:15  0:24  录音时：没连电脑  取回：公司（这台）
  （还没转写：q、s 先不能用）
  1-9 归档  0 退回  x 看过了  q 交给AI  s 交给AI（到设备上开）  p 播放  u 撤销  n 跳过  Esc 退出
  1 acmeshop  2 cardputer
→ 3 acmeshop（u 撤销）
```

- 每按一个键，下面打一行结果"→ 3 acmeshop（u 撤销）"，再跳到下一条。
- 别的电脑的项目：按数字先问"录音时按的是 acme（不是这台 Mac 的项目）。归到 3 acmeshop？y/n"。（2026-10-22，实现）按 `0` 问"不要 acme，当成没项目？y/n"，结果是"→ 不再算 acme"。
- （2026-10-22，实现）下一条出来之前敲下的键丢掉：连按两下 `2` 只归屏幕上那一条，不会把没看见的下一条也归了（`q`/`s` 和它的数字之间、数字和 `y` 之间不丢）。开着中文输入法时全角的 `２`、`ｘ`、`？` 当成对应的键，别的中文字提示"切到英文再按"。`u` 说回到了哪个状态（"↩ 撤销了：回到待归档"）。转写失败或超时的便条写"q、s 不能用，p 听一遍再归"。播放失败会说。
- `p` 用 `/usr/bin/afplay` 播 `seg-*.wav`（参数数组）。藏起来不在这里，用 `cpa filing hide ID`。
- 不开网页、不开端口，和 docs/15 §3.12"localhost HTTP 现在不做"一致。
- （2026-10-22，docs/25 R1-D、D4）**先过"等你送"**：`cpa dest pending` 的几条（暂存的，再是项目常用去向预选的）排在待归档前面，一屏一条："等你送 1/2  → Grok  #acmeshop  建议"。↩ 送到预选的去向，`g` 再按一个字母送到那个字母的去向（`cpa dest ls` 看字母，如 `x` 是 Grok），`d` 不送了（今日笔记那一行还在），`n` 跳过，Esc 退出。送出去的撤不回（已经在别的 app 里了），所以这里没有 `u`。待归档的便条上也能按 `g` 再按字母送出去；会议不行。

## 4. 环境戳（M2）

### 4.1 谁能知道什么

| 信息 | 设备 | 这台 cpad | 用法 |
|---|---|---|---|
| 录音时连着哪台电脑 | 只知道链路 Ready；握手后不留 Mac 的任何信息，重启后更没有 [源 integrate/v041 firmware/app/lib/link/link_session.cpp:74-86] | 能证明"连着我"：同一次开机、同一段连续连接里录的 [源 cpad/recordings/service.py:75-90；wall.py:38-45] | `link` |
| 这台电脑叫什么 | — | `host_label`，今天默认不设 [源 cpad/config/settings.py:65] | `host` |
| 网络名 SSID | 没有 WiFi：开了会少 31.7–34.3 KB 堆，低于 150 KB 底线 [源 docs/21 §5.3] | macOS 26 上 `networksetup`、`ipconfig`、CoreWLAN、`ioreg` 读出来都是 `<redacted>`，要定位权限 [测] | 不用 |
| 网络指纹 | — | 网关 MAC（`arp`、RA、DHCPv6 三个来源一致）、网关 IP 和网段、接口类型：不要权限、约 35 ms、不发包 [测] | `net.id`，只在 §4.3 做的时候 |
| 取回时间 | — | manifest 的 `written_at` | 显示 |

**口袋里录的**（不连电脑，多数便条）：设备什么都不知道，Mac 只知道是哪台 Mac、什么时候取回的。录音时在哪，要等 v0.8 你在设备上选的档案（§4.4）。

### 4.2 Mac 端的戳（P1-D1）

```json
"env": {"host":"公司", "link":"here"}
```

- **`host`**：取回这段录音的 Mac 当时的 `host_label`，没设就是 `null`。
  - **默认不设**（D10）：macOS 的电脑名常常是"某某的 MacBook Pro"，带你的真名，会进每个 manifest、导出和 MCP 回复。
  - 起名：WhisperWoof 第一次打开 Cardputer 页时顶上一条"给这台 Mac 起个名字：家 / 公司"（两个按钮，点一下就是 `cpa host name -- 公司`）；终端里 `cpa host name -- 名字`。名字过 `host.valid_label`（1–24 B、没有控制字符、能编码成 UTF-8）[源 cpad/config/host.py:52-54]，`-` 开头拒绝；（2026-10-22，实现）C1、U+2028/9、双向控制符也拒绝：`display_safe()` 会在每一行里去掉它们，存下的名字和显示的就不一样了；不合法的名字会让设备拒收整个主机 hello [源 firmware/app/lib/protocol/decode_host.cpp:17-18]，所以写之前就挡住。
  - 设备"控制"页上本来就显示 `host_label`，起了名字以后，设备上也看得见"连着：公司"——这就是 M2 要的"连着的电脑的名字"。
  - manifest 里存的是当时的名字：manifest 只写一次，这是"那时候是哪台 Mac 取回的"的历史记录。以后改名不改旧录音。
- **`link:"here"` 只在能证明时写**：段的开机号等于当前连接的开机号，并且 `link_since.up_ms ≤ 第 0 段的 up_ms ≤ 最近一次 pong 的 up_ms`。断线必然重新 hello（ping 5 s、超时 15 s），会重置 `link_since`。cpad 重启、Mac 睡眠只会多出 `unknown`，**绝不会多出假的 `here`**。
- **什么时候算、怎么不拖慢落地**：第 0 段落地时，用一个纯函数从内存里的链路状态和配置算出 `env`，任何异常都给 `env:null`，不跑任何命令；记进 `rec.seg.stored`；归档时抄进 manifest（新的可选顶层字段，manifest 仍然只写一次）和 `rec.archived`。`_store` 在 `file.del` 等的那次 fsync 之前 [源 cpad/recordings/service.py:111-140]，所以这里一步都不能慢。
- **不嵌套 `v`**：manifest 自己的 `v` 管它。
- **契约**：录音契约 v1 加可选的 `env`、一个合法向量、重算 `VECTORS.sha256`。schema 只用 WhisperWoof 校验器支持的关键字（§3.2），cpad 加一个测试钉住录音 schema 用到的关键字集合。WhisperWoof 忽略它不认识的属性 [源 WW/src/whisperwoof/core/cardputer/contract-schema.js:187-195]，它复制的向量由一个 PR 同步（P2-c，你合并）。`env` 和 docs/18 D5 的 `prof` 是两个键。

### 4.3 网络指纹（P1-D2，只在 §8.2 第 1 条答"一台笔记本"时做）

两台 Mac 时，"哪台 Mac 取回的"已经分开了家和公司，网络指纹不带来新信息；一台笔记本来回带时，它是 docs/18 P8-B1"按网络提示切档案"的输入。所以设计放在这里，P1 不做。做的时候：

- **只存 `net:{id, kind, when}`**，不存名字。名字只在 `$CPA_HOME/networks.json`（0600，`{"v":1,"names":{id:名字}}`），读和导出时再查。这样 `cpa net forget` 真能忘掉：地点名不会留在只写一次的 manifest 和事件里。
- **指纹**：`id = HMAC-SHA256(id_key, "v1|net|"+类型+"|"+网关MAC+"|"+网关IPv4/前缀长度)` 的前 6 字节。
  - 为什么不用普通的加盐哈希：盐就在旁边时，路由器厂商前缀约 2,000 个 × 2^24 ≈ 3.4e10 种，一块 GPU 几秒穷举完 [算]。
  - 每台 Mac 一把 `id_key`，家里和公司的 id 连不起来；`cpa net reset` 换一把新的（所有网络名字作废，设备的短 id 也跟着变）。
  - IPv6-only 用 RA 的源链路地址；VPN 时用第一个非隧道接口；VRRP/HSRP 的虚拟 MAC 会在不同的公司网络里重复，所以带上网段。
- **原始的网关 MAC、IP、网段只在内存里**，永远不写进归档、事件、日志、导出、MCP。路由器的 LAN MAC 常常紧挨着它的 Wi-Fi BSSID，而 BSSID 在公开的定位库里，所以它离位置只差一步。
- **采样**：在自己的定时器上用 `create_subprocess_exec`，绝对路径、参数数组、2 s 超时：`/sbin/route -n get default`、`/usr/sbin/arp -n <网关>`、`/usr/sbin/ipconfig getsummary <接口>`、`/usr/sbin/scutil --nwi`。连上那一刻和连接期间每 60 s 一次。落地时只取内存里 ≤60 s 的最近一次。不用 `system_profiler`（16 s，会触发扫描）和 `wdutil`（要 sudo）[测]。
- **解析器白名单**：只取 `InterfaceType`、`Router`、IPv4 地址和前缀，其余输出马上丢掉（App 有定位权限时 `ipconfig getsummary` 会打印 SSID 和 BSSID）。解析出错或超时，日志只记固定的原因码加 `type(exc).__name__`，从不记读到的那一行。
- **`when`**：`here` 时取录音开始前最近的一次，`when:"record"`；否则取落地那一刻，`when:"pull"`，界面写"取回时在…"。什么都读不到就是 `null`，不猜。
- **筛选只认录音时**：`--net` 和界面上"录音时网络"那一组只匹配 `when:"record"`；取回时的网络只作为文字显示。不然一条公司的便条在家取回，会出现在"家"下面。
- **起名字**（每个网络一次，之后自动）：`cpa net` 打印"此刻：Wi-Fi · 家"或"未命名"；`cpa net name --id X -- 名字`（1–16 个字，没有控制字符和双向控制符，`-` 开头拒绝）；`cpa net forget --id X`；`cpa net off|on`；`cpa net reset`。WhisperWoof 上只有固定按钮"家 / 公司 / 外面"，自由的名字只在终端里起。
- `secure_home` 的文件表加上 `id_key`、`networks.json` [源 cpad/config/settings.py:157-166]。MCP 永远拿不到 `net`。

### 4.4 设备端的戳（P3，跟 docs/18 v0.8）

- **`prof`**（docs/18 D5）：录音开始时设备上的档案，是你在设备上选的，这才是诚实的"录音时的情境"。
- **`lk`**：第 0 段多 1 位"开始录时链路 Ready"，约 7 B（`,"lk":1`）；`.M` 的 512 B 最坏情况还剩约 11 B [算]。Mac 这样读：`lk=0` → 没连电脑；`lk=1` 而且能证明是这台 → `here`；`lk=1` 证明不了 → `other`（另一台电脑，不知道是谁）；没有这个字段 → `unknown`。
- **不做**：在 `.M` 或线上带"上一台 Mac"的摘要，这违反 docs/18 §4.2（设备从不回显 `host_id`、不透露别的 Mac）；设备扫 WiFi，要过 G-WIFI、约 32 KB 堆，而且录音前不能扫，会让每次开始录晚 1.6–4.7 s [源 docs/21 §5.4]。

### 4.5 筛选和情境档案怎么用它

- **按电脑**：自动。docs/18 v0.8 把每台 Mac 绑到一个档案，设备只把档案 P 的东西交给绑定 P 的 Mac，每个档案一份 1–9。
- **按网络**：只提示、不自动切。网络指纹（如果做）只是一个建议，要你在设备上确认（docs/18 原则 1）；SSID、网关都能伪造。
- **戳只是注释**：从不决定档案、从不路由、从不改项目。
- **戳能发现、不能阻止**：今天任何一台 cpad 都会拉走设备上的所有东西 [源 docs/18 §1.1]；设备缓存里的 3 号，到了另一台 Mac 上可能是别的项目 [源 cpad/recordings/manifest.py:73-79]。所以 v0.8 之前：
  - 别的电脑的项目单独一组，数字后要再确认一次，MCP 默认不给（§2.1、§3.5）；
  - `--host` 和"电脑"筛选不做：每台 Mac 的 `~/CardputerNotes` 只有它自己取回的，`host` 永远只有一个值。合并两台 Mac 的归档时再加。
- v0.8 起：待归档加 `--prof`；`hand` 出的排队项带录音的 `prof`（docs/18 原则 4：派生的东西跟着来源走）。

## 5. 界面：WhisperWoof 的 Cardputer 页

### 5.1 位置和布局

- 侧栏主组里 Meetings 后面加一项 **Cardputer**，lucide 的 Inbox 图标，旁边是待归档数 [源 WW/src/components/ControlPanelSidebar.tsx:73-97]。不放进 Meetings：Meetings 是数据库里的笔记，而设计文档不把录音放进 WhisperWoof 的数据库 [源 WW/docs/design/cardputer-inbox.md:248-253]。
- **三栏**，照 Notes 页 [源 WW/src/whisperwoof/ui/notes/VoiceNotesView.tsx:265-420] 和 DESIGN.md §6（玻璃只用于导航，内容用实心卡片，一个焦糖色强调）：
  - **筛选栏**（玻璃）：
    - 状态：待归档（N，默认）、别的电脑的项目（N，有才出现）、已交给 AI、看过了、没转出文字、全部；
    - 日期：今天 / 本周 / 上周 / 全部（按钮，不用打字）；
    - 搜索框：照 Notes 页的搜索框 [源 WW/src/whisperwoof/ui/notes/VoiceNotesView.tsx:294-300]，可以用 WhisperWoof 自己的听写键说进去；`/` 进搜索框，Esc 回列表；
    - 项目 1–9，可以多选，可以把行拖上去；编号和**这台 Mac 给设备的 1–9** 一样，来自 `cpa project ls --json`；
    - 便条 / 会议；设备、录音时是否连着这台、录音时网络（有两个以上的值才出现）。
  - **列表**：开头一句话"7 条待归档，今天 3 条"（DESIGN §11）；按天分隔；每行是标题或第一句（没字时"便条 · 0:24 · 还没转写"）、相对时间、时长、小标签、状态（图标加字加颜色，不只靠颜色，DESIGN §7）、"待办"标签、★。别的电脑的项目那一行多一句"录音时按的是 acme（不是这台 Mac 的项目）"。
  - **详情**：标题；"录于 08:10（录音时没连电脑）· 13:00 由 公司 取回"，时间不确定时加"~"；项目选择；播放；逐句转写（`[mm:ss]`）；`summary.md`（链接显示成纯文字，不渲染图片）；交给 AI 以后显示按 qid 读回的排队正文；按钮"归到…"（唯一的主按钮）、"交给 AI"、"看过了"、"藏起来（设备和 AI 也看不到）"、"在 Finder 中显示"。
  - **空状态**：Mando 的头和"都归好了"。

### 5.2 按键

单键只在焦点在列表上时起作用，在搜索框里打字不会归档。

| 键 | 做什么 |
|---|---|
| ↑ ↓ / `j` `k` | 移动 |
| `1`–`9` | 归到这个编号的项目，跳到下一条；别的电脑的项目那一组先问一句，Enter 确认 |
| `0` | 退回待归档 |
| 空格 | 播放 / 停 |
| Enter | 打开详情 |
| `q` | 交给 AI 排队；没项目时接着按数字 |
| `s` | 交给 AI、排成"要开 session"；没项目时接着按数字；之后到设备看板上按 Enter |
| `x` | 看过了 |
| `/` | 进搜索框 |
| ⌘Z | 撤销；可以连按，退回这次打开页面以来的好几步，撤回的那一行重新选中 |

- **打开页面时**第一条"新"的已经选中，焦点在列表上。
- **每个动作都有反馈**：5 秒的提示"已归到 3 acmeshop · ⌘Z 撤销"（"已排队 q12 · 到设备看板上按 Enter 开"）；按错数字一眼就看到。
- **没字的行**：`q`、`s` 是灰的，写"还没转写，先归档或等转写"。
- 动作成功后选中跳到下一条"新"的；失败就地说清楚（例如"cpad 太旧，先重启"），不跳。

### 5.3 数据怎么来

- **状态和文字只问 `cpa`**：打开页面、`fs.watch` 盯 `notes_dir/recordings`（2 s 防抖，**从不盯 `$CPA_HOME`**）、每 30 s、窗口获得焦点时跑 `cpa filing ls --json`；详情跑 `cpa filing show ID --json`。`cpa rec ls --json` 实测 0.11 s [测]。
- **音频读开放文件**：main 进程用自己的扫描按 id 找目录（`O_NOFOLLOW`、必须在根目录内、不跟符号链接 [源 WW/src/whisperwoof/core/cardputer/session-files.js:17-89]），读 `seg-*.wav`（≤20 MB）交给页面播；不用 `cpa` 给的路径（契约里也没有路径）。
- **执行 `cpa`**：`execFile(cpa, argv, {shell:false, timeout:5000, maxBuffer:4 MiB, env:buildCpaEnv()})`。
  - `buildCpaEnv()` 是真正的白名单：`HOME`、`USER`、`LOGNAME`、`LANG`、`LC_*`、`TMPDIR`、`CPA_HOME`，`PATH` 固定为 `/usr/bin:/bin:/usr/sbin:/sbin`。现有的 `buildSidecarEnv` 是黑名单，`PYTHONPATH`、`PYTHONHOME`、`DYLD_LIBRARY_PATH`、`DYLD_FRAMEWORK_PATH` 都会传给 Python 写的 `cpa` [源 WW/src/whisperwoof/bridge/sidecar-env-pure.js:35-46]。
  - `cpa` 的位置在 main 里找一次（`~/.local/bin`、`/opt/homebrew/bin`、`/usr/local/bin`，或你在原生对话框里选），取 realpath，要求属于当前用户或 root、组和其他人不可写，并在设置里显示。
  - 参数先校验；输出在纯模块里按 `protocol/filing/v1` 校验形状；stdout 从不进日志；搜索词经 stdin 传。
  - 撤销：main 用自己的动词表从 `undo` 对象重建 argv、校验 id，不照抄。
- **页面只发 id、项目 key 和枚举**给 main，从不发路径或文字。新 IPC 用 `cardputer-tab-*` 前缀（现有的 `whisperwoof-cardputer-*` 被一个只允许三个调用的测试钉住 [源 WW/src/whisperwoof/core/cardputer/inbox-shell.test.ts:308-319]），**再加一个同样的钉住测试**：列出每一个 `cardputer-tab-*` 调用，断言没有一个带路径或文字。
- **没装 `cpa`**：只列一个普通的"录音"列表（不叫待归档、侧栏没有数字），顶上一句"装好 cpa 才能归档"。不在 WhisperWoof 里重做一遍归档的判断，那会把 §1.3 的"说法不一"带回来。
- **转写**：P1 时由 WhisperWoof 转写（"转写 Cardputer 录音"开关开）；VC-1 后由 cpad 转写（开关关，docs/22 §4.7）；待归档两种都能用。
- WhisperWoof 设计文档里"以后和 cpad 之间开一个 socket"那一行 [源 WW/docs/design/cardputer-inbox.md:252-253] 改成"通过 `cpa` 命令行"，和 docs/15 §4.6、#34 一致。

### 5.4 PR 5/5

- 基于 #152；约 26 个文件、+2,200 行（1,900–2,600）[算：对比 Notes 页 #124 的 17 个文件 +813、#152 的 +2,679]。
  - **5a 只读**：列表、详情、筛选（日期、搜索、多选）、播放、在 Finder 中显示、`buildCpaEnv`、IPC 钉住测试，加下面的修复，约 +1,400；要 P1 的 `cpa filing ls/show --json`。
  - **5b 动作**：按键、提示、多步撤销、别的电脑的项目的确认、起名按钮，约 +800；要 P1 的写命令。
- **顺手修 #151 的三处**：
  - 设置页的"打开"文件夹按钮没反应：`openExternal("file://…")` 被只放行 http、https、mailto 的处理器拒掉，而且不报错 [源 WW/src/whisperwoof/ui/settings/CardputerSection.tsx:77-84；WW/src/helpers/ipcHandlers.js:1810-1817]。改成 main 里的 `shell.openPath`；**这个 IPC 不收参数**，main 用自己存的根目录，`lstat` 必须是真目录、不能以 `.app` 结尾（`openPath` 能启动应用包）。
  - 积压时每段一条系统通知：积压 N 段时打开收件箱就弹 N 条 [源 WW/src/whisperwoof/core/cardputer/inbox-runner.js:168-173]。改成每一轮一条"3 段录音好了"，点它打开 Cardputer 页。
  - 原生对话框和页面里显示的文字过同一套 `display_safe` 规则（§3.3）。
- 你合并；agent 只开 PR。

## 6. 安全和隐私

### 6.1 规则

| 规则 | 怎么做到 | 依据 |
|---|---|---|
| 只在本机 | cpad、`cpa`、WhisperWoof 都在这台 Mac；音频和文字不上传；"工作流"不含云端插件（2026-10-22，docs/25：只经你为这一条选的、开启时确认过的标了云的去向离开 Mac，`cpa audit --leaves-mac`）| #34；ADR 0004 |
| 不开端口 | WhisperWoof 用 `execFile` 调 `cpa`；MCP 走 stdio；终端版不开网页 | docs/15 §3.1、§3.12 |
| 参数数组 | `cpa`、`afplay`、`route`、`arp`、`ipconfig`、`scutil`、`claude` 都不经 shell；正文永远在 `--` 后 | [源 cpad/agents/claude_code/argv.py:47-48] |
| 正文不进 argv | 命令只收 id、key、枚举；名字放 `--` 后、`-` 开头拒绝；搜索词走 stdin；cpad 自己取正文、清洗、截断 | §3.3、§3.4 |
| 路径检查 | 只认 MonoId；契约里没有路径；目录由各方自己扫描得出，realpath 必须在根目录内，`O_NOFOLLOW`，限大小 | [源 cpad/recordings/results.py:94-149；WW session-files.js] |
| 家和公司不混 | `env` 只做注释；别的电脑的项目单独一组、要再确认、MCP 默认不给；隔离靠 docs/18 | §2.1、§4.5；docs/18 §2 |
| 设备的 MAC 不外给 | 行里是 `id_key` 算的短 id；`dev`、`fw`、`dir` 不进 MCP | docs/15 §3.4 |
| 电脑名不带真名 | `host_label` 默认空，只用你起的名字 | §4.2 |
| 位置级数据不落盘（做指纹时）| 网关 MAC、IP、网段只在内存里算 HMAC；解析器白名单；出错只记原因码；不读 SSID、不要定位权限；名字只在 `networks.json`；MCP 拿不到 `net` | §4.3 |
| 文字在哪 | `rec.*` 不带转写句子；`rec.result` 带标题和 `memo_text`；`hand` 把 ≤500 B 抄进 `queue.added.text`（带 `bytes`、`sha8`），和设备上的排队项一样；审计只记长度；WhisperWoof 只记类型和错误码 | §3.2；docs/15 §3.9 |
| 藏起来就是藏起来 | 有打开的排队项时不让藏；`list_queue`、`list_captures` 跳过来源已藏起来的项；`list_unfiled` 从不给藏起来的 | §2.2、§3.5；docs/15 §3.4 |
| AI 客户端只能读 | 待归档只有两个只读 MCP 工具；没有写工具；永远不能开 session | docs/15 §3.6 |
| 开 session 只在设备上确认 | `hand --spawn` 只排队；`origin:"inbox"` 一律 plan、摘要带"语音·"；ADR 0002 不变 | ADR 0002；docs/22 §5.4 |
| 有 shell 的 agent | 它能做 `cpa` 能做的一切（归档、交给 AI），但开不了 session；边界是它自己的 shell 审批 | §3.4 |
| 转写是不可信数据 | `queue_text` 清洗；`display_safe()` 去掉 C0、C1、双向控制符、U+2028/9；`summary.md` 的链接显示成文字、不显示图片；MCP 标成不可信 | docs/15 §3.7；docs/22 §5.4 |
| 会议不进队列 | `hand` 拒收会议；会议待办走 docs/17 §6 | docs/17 §6.3；docs/22 §8 |
| 什么都不删 | "藏起来"就是 `rec.hidden`，文件不动；每个动作可撤销 | [源 cpad/cli/rec.py:1-8] |
| 单写者 | cpad 不碰转写程序的文件；待归档的状态不写进录音目录；今日笔记只有 cpad 写 | 录音契约 v1；§2.4 |
| `cpa` 本身可信 | WhisperWoof 检查 `cpa` 的属主和权限；环境变量白名单；`openPath` 不收参数 | §5.3、§5.4 |

### 6.2 要跟着 P1 修订的文档

这些文档写的是代码现在的行为，所以修订和改代码的那个 PR 一起合，不提前改。

| 文档 | 修订 | 随哪个包 |
|---|---|---|
| docs/15 §1 第 3 条、§4.6；ADR 0004 决定第 3 点 | 对外的本地契约从两份变三份：`protocol/…/v1` 下有 schema 的 CLI `--json` 输出，是人操作的通道，不用开启，`rec.*` 记录带 `by:"cli"`（交给 AI 是 `origin:"inbox"`，撤回是 `by:"undo"`）；WhisperWoof 走它，不走 `cpa mcp` | P1-B |
| docs/15 §2.4 | 导出加 `data/filing.jsonl`（做指纹时加 `data/networks.json`）；`CPA-EXPORT.json` 计数和导出 README；layout 仍是 1 | P1-B |
| docs/15 §3.4 | 新工具 `list_unfiled`、`get_filing_item`；`list_recordings` 用有效项目、加 `env.host`、`env.link`；`list_queue`、`list_captures` 跳过来源已藏起来的项 | P1-E |
| docs/22 §5.4 第 3 条、§3.7 | plan 和"语音·"也认 `origin:"inbox"` | P1-C |
| docs/14 §1.2 | 没归档的录音可以经 `cpa filing send` 改投 | P1-G |
| `hub/reduce.py` 开头的说明 | 只在选了 D7 备选时改 | ADR 0005 |
| WW `docs/design/cardputer-inbox.md:252-253` | socket → `cpa` 命令行 | P2-a |

## 7. 分期、工作包、测试

一律先写失败的测试（RED → GREEN → REFACTOR）。P1 能全在没有设备的情况下测：`tests/support/rec_shell.py`、`tests/unit/test_reduce.py` 都在，hypothesis 已经是依赖。

### 7.1 工作包

| 包 | 内容 | 动哪里 | 代码 / 测试（行）[算] | 依赖 |
|---|---|---|---|---|
| **P1-A 折叠和读者** | 待归档折叠（纯函数）；有效项目（含会议的最后一段、`hand` 的排队项）和有效去向；所有读者统一用有效项目；`result_record` 用有效项目；`rec.filed` 和它的笔记行（housekeeping 写、backfill 重放）；`_edited` 的退路；控制回复的 `api` 和 `cpa status` 的提示 | 新 `cpad/filing/{fold,rows,text}.py`；改 `archive/{recordings,todos,events_view,export}.py`、`recordings/{results,route,records,sessions}.py`、`app.py`、`control/server.py`、`cli/main.py` | 500 / 600 | 无 |
| **P1-B CLI 和契约** | `cpa filing` 的 ls、show、file、done、hide、reopen，筛选、错误码、撤销对象、`display_safe`；`cpa project ls --json`；`cpa host name`；`protocol/filing/v1/` 的 schema、向量、检查器；导出的 `filing.jsonl` | 新 `cpad/cli/filing.py`、`protocol/filing/`；改 `cli/main.py`、`cli/commands.py`、`archive/export.py` | 600 / 550 | P1-A |
| **P1-C 交给 AI** | `hand`（一条 `queue.added`，会议拒收，`api` 检查）；`queue_text` 共用；`origin:"inbox"` → plan 和"语音·"；`queue.done{by:"undo"}` 的折叠；有排队项时拒绝藏起来 | `cpad/filing/`、`hub/reduce.py`、`hub/snapshots.py`、`queue/fold.py` | 160 / 260 | P1-B |
| **P1-D1 电脑和连接** | `link_since`；纯函数算 `env{host, link}`，进 `rec.seg.stored` / manifest / `rec.archived`；录音契约加 `env` 和向量；钉住 schema 关键字的测试 | `recordings/{service,manifest,records}.py`、`protocol/recording/v1/` | 150 / 200 | 无 |
| **P1-D2 网络指纹（看 Q1）** | 采样定时器、白名单解析器、HMAC、`id_key` 的网络部分、`networks.json`、`cpa net`、金丝雀测试 | 新 `cpad/config/netenv.py`、`cpad/cli/net.py`；改 `config/settings.py` | 400 / 400 | P1-D1；§8.2 第 1 条答 B |
| **P1-E MCP** | `list_unfiled`、`get_filing_item`；`list_recordings` / `get_recording` 用有效项目、加 `env.host`、`env.link`；`list_queue`、`list_captures` 跳过来源已藏起来的；`outputSchema` 引用契约的测试（实现：内联副本加字段一致的测试，§3.5）| `cpad/mcp/{catalog,tools,schemas,backend}.py` | 180 / 220 | P1-A、P1-B |
| **P1-F 终端** | `cpa filing triage`：结果行、别的电脑的项目的确认 | 新 `cpad/cli/triage.py` | 200 / 170 | P1-B |
| **P1-G 送到去向** | `cpa filing send ID DEST` 经 docs/14 的路由；`hand` 并进去 | `cpad/filing/`、`dest/` | 150 / 200 | docs/14 D5-A；P1-C |
| **P2-a** | WhisperWoof 5a 只读 | WW | +1,400 | P1-B 发版；#149–#152 合并 |
| **P2-b** | WhisperWoof 5b 动作 | WW | +800 | P2-a、P1-C |
| **P2-c** | WhisperWoof 同步录音契约向量（`env`）和 `filing/v1` 的向量，带 `SOURCE` 文件 | WW 的 fixtures | +80 | P1-B、P1-D1 |
| **P3-a** | 设备 `prof`、`lk` | 固件 `rec_meta`、`file.items`、schema | 跟 docs/18 P8-F0/F2 | docs/18 |
| **P3-b** | #53 录音页显示 1–9；和 Mac 页用同一份 `projects.json` 编号 | 固件录音页 | 跟 #53 | `feat/ux-decisions` |

**合计** [算]：P1（A–F，不含 D2、G）约 1,790 行代码、2,000 行测试；加 D2 约 2,190 / 2,400；P1-G 另算。P2 约 +2,280。选了 D7 备选再加约 250 / 350。

```
你：合并 WW #149–#152（或 docs/22 VC-1）── 列表里才有字
P1-A ─► P1-B ─► P1-C、P1-E、P1-F
P1-D1（独立）─► P2-c；Q1 答 B ─► P1-D2
docs/14 D5-A ─► P1-G
P1-B 发版 + #149–#152 合并 ─► P2-a ─► P2-b（还要 P1-C）
docs/18 P8 ─► P3-a；feat/ux-decisions ─► P3-b
```

### 7.2 测试

- **折叠**（hypothesis 性质测试）：任意顺序的事件流，状态唯一、和重放次数无关；跑完 `undo` 回到原状态（包括别的电脑的项目恢复成原来的 key）；**撕裂的尾巴等于动作没做，重做一次就收敛**；带 `cap` 的段从不单独成行；删掉的段从不列；在等条目的段只计数；`vcap.orphan` 的段单独成行；一段不会被归两次。
- **有效去向**：没项目的零打字 Q 便条算待归档；有 `dest.downgraded` 的算没去向；`todo` 算有去向；`dest.decided{queue}` 没有对应排队项时算没去向。
- **有效项目**：manifest 有 / 没有 × `rec.edited` 有 / 没有 × `hand` 有 / 没有 × key 登记 / 没登记 × 便条 / 会议（会议取最后一段），CLI、MCP、待办、采集、导出、今日笔记给出同一个答案；老 cpad 的行没有 `project` 时 `cpa rec ls` 仍对；cpad 比 `cpa` 老时 `cpa status` 提示重启。
- **今日笔记**：结果后归档写一条 `rec.filed` 和一行；backfill 重放不重复；CLI 从不写笔记。
- **CLI**：每个子命令的 JSON 过 `check_filing.py`；`invalid/` 向量全被拒；非法 id、key、枚举、名字回固定错误码且不回显；cpad 在跑、没跑都能写；`display_safe` 去掉 C1、双向控制符、U+2028/9。
- **交给 AI**：会议回 `E_BAD_ARG`；`-` 开头、超长、控制字符、空转写，要么清洗要么拒绝；`origin:"inbox"` 的项在设备上开时即使项目配了 `acceptEdits` 也是 plan，摘要以"语音·"开头；cpad 的 `api` 太旧时回 `E_UNAVAILABLE`、不排队；有打开的排队项时 `hide` 回 `E_ALREADY`；`queue.done{by:"undo"}` 以后不再算已交给 AI，老折叠也关掉它；[待] 从这种排队项开的 plan session 调不了 cpa 的 MCP 写工具（`queue_add` 也不行）。
- **MCP**：`list_unfiled` 的输出过契约的行 schema；没有 `dev`、`fw`、`dir`、`env.net`；从不返回藏起来的；不加 `orphans:true` 不给别的电脑的项目；`tools/list` 里没有任何能开 session 或改归档的工具；`list_queue`、`list_captures` 跳过来源已藏起来的项。
- **环境戳**：here / unknown 的边界（别的开机、连上之前、重新 hello 之后）；算 `env` 的函数抛异常时 `env:null`，`rec.seg.stored` 不晚一步；电脑名的校验（表情符号、控制字符、超长、`-` 开头）；录音 schema 的关键字集合被钉住。
- **网络指纹（做的时候）**：解析器跑脱敏样本（Wi-Fi、以太网、VPN、IPv6-only、没有 arp 记录、带 SSID/BSSID 的输出）；解析失败和超时只记原因码；**金丝雀**：假的网关 MAC、IP、SSID、BSSID 不出现在 manifest、`events.jsonl`、日志、导出、MCP 输出里；`id_key` 不在导出里；`cpa net off` 后 `net` 为 `null`；`--net` 只匹配 `when:"record"`。
- **终端**：伪终端脚本按 `1`–`9`、`u`、`x`、别的电脑的项目的 `y`/`n`，断言事件和结果行。
- **WhisperWoof**（vitest，node 环境）：纯模块（筛选、分组、按键、行构建、argv 构建、撤销重建、输出校验）；假 `execFile` 的 `cpa` 客户端（超时、非 0 退出、坏 JSON）；`buildCpaEnv` 只放白名单；`cardputer-tab-*` 钉住测试；`openPath` 拒绝 `.app`；`renderToStaticMarkup` 渲染（提示、灰掉的 `q`/`s`、别的电脑的项目那句话）；main 接线。
- **端到端**：临时 `CPA_HOME` 和笔记目录，放 4 段假录音（一段有转写、一段失败、一段属于采集、一段是别的电脑的 key），跑一遍 `cpa filing triage`，再用 `cpa filing ls --json` 断言。

## 8. 决定和要你拍板的事

### 8.1 决定

不回复就按默认做。

| # | 决定 | 默认（推荐） | 备选 | 为什么 | 见 |
|---|---|---|---|---|---|
| D1 | 待归档收什么 | 没有有效项目、也没有有效去向的便条和会议；别的电脑的项目单独一组；采集一条一行；没字的照样列；打字笔记不收 | 只收便条 | 有去向的已经有地方了；没去向的 Q 便条不能两头不管 | §2.1 |
| D2 | 状态和"不要了"的键 | 新、已归档、已交给 AI、看过了、藏起来；一个 `x` = 看过了，藏起来是详情里的按钮 | `e` 已处理、`x` 不要了两个键 | 少记一个键（M1 就在抱怨记键）；藏起来会影响设备和 AI，不该是一个键 | §2.2、§5.2 |
| D3 | 状态记在哪 | `events.jsonl`，一个动作一条记录；导出成 `data/filing.jsonl` | 录音目录里的 `filing.json`；单独的索引 | 已有锁和 fsync；一条记录不会只写进一半 | §2.3 |
| D4 | 归档以后今日笔记 | 结果前归的直接用新项目；结果后归的由 cpad 追加自带上下文的 `rec.filed`，画成 `-f<n>-<key>` 那一行，旧行不改 | 不补 | 笔记只追加；一行由一条事件画出，能重放；只有 cpad 写笔记 | §2.4 |
| D5 | 接口 | `cpa filing` + 契约 `protocol/filing/v1`；MCP 只有只读的 `list_unfiled`、`get_filing_item` | 加三个默认关的 MCP 写工具 | 没人要写工具；docs/15 §3.6 说 MCP 不改已有内容；WhisperWoof 走 CLI | §3 |
| D6 | 文字 | 终端、页面、MCP 给 ≤280 字（过 `display_safe`）；`rec.*` 不带转写句子；`hand` 抄 ≤500 B 进排队项 | 只给标题 | 不看文字就没法归；排队项的规则和设备上打的一样 | §3.2 |
| D7 | 交给 AI 以后在哪开 session | 只排成 `intent=spawn`，在设备看板上按 Enter，一律 plan；ADR 0002 不改 | Mac 上开：cpad 自己弹框、按 sha8 绑定正文、新的 `MacSpawn` 输入、写 ADR 0005（+250 / +350 行）| Mac 上的确认挡不住本机程序，只有设备上的 Enter 证明人在；备选也只防手滑 | §3.4 👤 |
| D8 | 环境戳做多少 | P1 只做电脑名和"录音时是否连着这台"；网络指纹等 §8.2 第 1 条答"一台笔记本"再做 | 现在就做网络指纹（+400 / +400 行）| 两台 Mac 时电脑已经分开了家和公司，指纹不带来新信息 | §4.2、§4.3 👤 |
| D9 | 网络指纹（做的时候） | 本机密钥的 HMAC（类型 + 网关 MAC + 网段），只存 id；名字只在 `networks.json`；解析器白名单；不读 SSID | 普通加盐哈希 | 加盐哈希几秒就能穷举；地点名要能真的忘掉 | §4.3 |
| D10 | 电脑名 | `host_label` 默认空；点一下"家 / 公司"或 `cpa host name -- 名字` | 默认取 macOS 电脑名 | 电脑名常带真名，会进每个 manifest、导出和 MCP 回复 | §4.2 |
| D11 | 设备端戳 | 跟 docs/18 v0.8：`prof` 加 1 位 `lk`；不做"上一台 Mac"摘要，不做设备扫 WiFi | — | 设备不透露别的 Mac；扫 WiFi 堆不够、还让开录变慢 | §4.4 |
| D12 | 界面 | WhisperWoof 新的 Cardputer 页，日期按钮、搜索、项目多选，5 秒提示加多步撤销；PR 5/5 拆 5a / 5b；只经 `cpa` JSON 和开放文件 | 放进 Meetings 页 | 录音不进 WhisperWoof 的数据库；按错能马上看到、马上撤 | §5 |
| D13 | 没有 WhisperWoof 时 | `cpa filing triage` 终端单键 | 本地网页 | 不开端口（docs/15 §3.12） | §3.6 |
| D14 | 发版 | P1 单独发，只动 Mac，不等固件、不等 VC-1；P2 等 #149–#152 合并和 P1；P1-G 等 docs/14；P3 跟 v0.8 | P1 拆两版 | 没字的行也能先听再归；重转按钮等 cpad 支持再显示 | §7 |
| D15 | 别的电脑的项目 | 单独一组，显示录音时按的 key；数字后再按 Enter；MCP 默认不给 | 当成普通待归档，一个数字归 | 它多半是在一台 Mac 上该归的便条被另一台取回，一个数字就混了 | §2.1、§3.5 |
| D16 | 会议能不能交给 AI | 不能（`E_BAD_ARG`）；会议待办走 docs/17 §6 一条一条确认 | 交前 500 B | 会议里有别人的话；docs/17、docs/22 都不让它进队列 | §3.4 |
| D17 | 撤回交给 AI | `queue.done{by:"undo"}` | 新事件 `queue.cancelled` | 老 cpad 也会关掉这一项，不用让设备和别的读者学新事件 | §2.2 |
| D18 | 对外的名字 | `cpa filing`、`protocol/filing/v1`、MCP `list_unfiled`；界面叫"待归档" | `cpa inbox` | "inbox"已经是设备的"等你"、WhisperWoof 的转写收件箱 | §1.3 |

### 8.2 要你拍板

1. **你是 A 好几台 Mac（例如家里一台、公司一台），设备在它们之间来回带，还是 B 一台笔记本来回带？**（docs/18 §15 第 1、9 条。）
   - **A（默认）**：设备只把属于每台 Mac 的录音交给那台 Mac。这要把 docs/18 的设备端档案（协议、固件、cpad 三块）提前到 P1 之后做。在那之前，待归档用"别的电脑的项目"一组兜住。网络指纹不做，电脑名已经够分。
   - **B**：笔记本按网络提示你切档案（docs/18 P8-B1），网络指纹（P1-D2）做，作为它的输入。
   - 连带的一个小问题，M2 要求软件按设备和网络表现不同：**默认按电脑自动分，按网络只提示、不自动切**。
2. **D7：交给 AI 以后，能不能在 Mac 上直接开 session？** **默认：不能**，在设备看板上按 Enter（ADR 0002 不改，plan 模式）。代价是人在 Mac 前也要拿起设备按一下。备选：Mac 上由 cpad 自己弹框确认（§3.4），要写 ADR 0005；它只防手滑，不防这台 Mac 上别的程序。

## 9. 评审之间的分歧和取舍

四份评审（用户、隐私安全、契约架构、可行性）的修正都已经写进上面各节。下面是它们意见不一致的地方。

| 分歧 | 选了 | 为什么 |
|---|---|---|
| D7 默认：Mac 上确认就开（原稿）vs 回到设备上确认（隐私、契约、可行性都指出 `--yes` 是客户端自证） | 设备上确认；删掉 `--yes` 和 `--start` | 三份评审一致：Mac 上的任何确认都能被本机程序代答 |
| D7 备选怎么做：客户端读回正文、带 sha8（隐私）vs cpad 自己弹框（契约） | 两者合起来：cpad 弹框，框里是按 qid 读回的正文，用 sha8 绑定 | 确认归 cpad，不靠客户端的旗标；sha8 保证框里显示的就是要开的 |
| MCP 写工具：修订 docs/15 后保留（隐私、契约）vs 不做（用户、可行性） | 不做，修订条件写在 §3.5 | 没人要；WhisperWoof 走 CLI；省约 150 / 200 行 |
| MCP 行里的电脑名：不给（隐私）vs 给 `host` 和 `link`（可行性） | 给 `host`、`link`，不给 `net` | `host_label` 改成默认空、只用你起的名字，真名的顾虑没了；`net` 是行踪 |
| 设备怎么标：每台 Mac 的序号 `d1`（隐私）vs HMAC id（契约） | HMAC id | 不用存一张对照表；两台 Mac 上连不起来 |
| 撤回交给 AI：新事件 `queue.cancelled`（契约）vs `queue.done{by:"undo"}`（可行性） | `queue.done{by:"undo"}` | 老 cpad 照样关掉这一项 |
| 电脑名默认：截断、校验 macOS 电脑名（可行性）vs 默认空（隐私、契约） | 默认空；可行性评审的校验规则用在你起的名字上 | 真名会进每个 manifest；不合法的名字会让设备拒收 hello |
| 别的电脑的项目：一个数字归、显示按的 key（用户）vs 数字后再确认（隐私） | 显示按的 key，数字后按 Enter | 一个数字最容易把家和公司混在一起；多按一下只在这一组 |
| `hand` 走 docs/14 的路由（契约）vs 一个动作一条记录（可行性） | P1 写一条 `queue.added`，和路由共用 `queue_text`；docs/14 合并后 `send` 走路由，`hand` 并进去 | 路由还没合并；它一次写两条，所以折叠要求排队项真的在 |
| 网络指纹：按"现在做"来审（隐私、契约、可行性）vs 等 Q1（用户） | 设计按三份评审改好留在 §4.3；P1 不做，等 Q1 | 两台 Mac 时它不带来新信息；一台笔记本时它正好是 P8-B1 的输入 |
| 排队正文末尾"（录音 <id>）"和 `read_transcript` 提示：只给便条（隐私）vs 和路由同一条规则（契约） | 去掉后缀 | 会议已经不能交；和 docs/14、docs/22 用同一个 `queue_text` |
| 对外的名字：`cpa inbox`（原稿、#54 标题）vs 换掉（契约） | `cpa filing` 一套 | "inbox"已经被占了三处；界面照样叫"待归档" |

## 附：证据索引

- daemon（origin/main `aaa9747`）：`recordings/manifest.py:1-10、73-79、93-122`；`recordings/service.py:75-91、111-140、170-185`；`recordings/records.py:1-17、76-81、92-106`；`recordings/sessions.py:70-89`；`recordings/index.py:45-69`；`recordings/results.py:85-91、94-149`；`recordings/route.py:1-6、62-82`；`recordings/wall.py:38-45`；`archive/recordings.py:265-291`；`archive/search.py:1-9`；`cli/rec.py:1-8、142-169`；`cli/commands.py:29-33、49-60`；`cli/main.py:156-206`；`mcp/schemas.py:46-50、136-140`；`mcp/config.py:34-58`；`mcp/limits.py:56-57`；`mcp/backend.py:147-166`；`hub/reduce.py:1-5、90-102、124-195`；`queue/fold.py:11-55`；`events/log.py:58-67、87-104`；`events/records.py:1-5、49-75、97-98`；`agents/claude_code/argv.py:1-60`；`agents/claude_code/adapter.py:34`；`projects/registry.py:1-6、21-23`；`control/server.py:3-4、54-66`；`control/client.py:15`；`config/host.py:1-5、52-54`；`config/settings.py:63-65、157-166`；`protocol/validate.py:19`；`notes/render.py:19`；`notes/writer.py:36-67`；`inbox/need.py:1-7`；`protocol/specs.py:212`；`service/launchd.py:79-80`；`app.py:118-126、354-361`；`__init__.py:3`。
- 固件：`app/lib/protocol/decode_host.cpp:10-24`；`app/lib/hal/hal_platform.cpp:39-40`。
- 协议：`protocol/README.md` 第 20 条；`protocol/recording/v1/README.md`（单写者表、交接规则、向前兼容、第 55 行的 `x-knownValues`）；`protocol/schema/v0/inbox.dismiss.schema.json`。
- 文档：docs/15 §1、§2.1–§2.4、§3.4、§3.6、§3.7、§3.9、§3.12、§4.6；docs/17 §6.2–§6.3；docs/18 §1.3、§2、§4.2、§7、§15；docs/21 §5.3–§5.4（`wt-settings`）；docs/22 §2.1–§2.4、§3.6–§3.7、§4.7、§5.1–§5.9、§7、§8（`wt-voicecap`）；docs/14 §1.2、§5.2、`dst/router.py:1-136`（`feat/destinations` `b5ad361`）；ADR 0002 第 3 条、ADR 0004。
- WhisperWoof（`WW/`，`c6b3060`）：`src/components/ControlPanelSidebar.tsx:73-97`；`src/whisperwoof/ui/notes/VoiceNotesView.tsx:265-420`（搜索框 294-300）；`src/whisperwoof/ui/settings/CardputerSection.tsx:77-84`；`src/helpers/ipcHandlers.js:1810-1817`；`src/whisperwoof/core/cardputer/{session-files.js:17-89, inbox-runner.js:168-173, inbox-shell.test.ts:308-319, contract-schema.js:16-37、76-91、187-195}`；`src/whisperwoof/bridge/{sidecar-env-pure.js:35-46, plugin-bridge.js:16-20}`；`docs/design/cardputer-inbox.md:248-253`；`DESIGN.md` §6、§7、§11。
- 本机 [测]（2026-10-22，只读）：`~/CardputerNotes/recordings/*/manifest.json`（各模式、项目和 `dest`）；目录列表（没有转写文件）；`events.jsonl` 的事件类型计数；`cpa rec ls --json`（0.11 s，每行都是 `project:null`）；正在跑的 cpad 早于 v0.4.1、`$CPA_HOME` 里没有 `host_id`；`networksetup`、`ipconfig getsummary`、`system_profiler`、CoreWLAN（JXA）、`ioreg`、`route`、`arp`、`scutil` 在 macOS 26 上的输出，值都已脱敏，没有保存。
