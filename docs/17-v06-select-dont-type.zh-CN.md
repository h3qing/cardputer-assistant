[English](17-v06-select-dont-type.md) · **中文**

# 17 · v0.6「选，不打字」：等你的事都能选着办，待办在设备上确认和勾掉

> 状态：**设计稿（已按两轮对抗评审修订），等你拍板 §17** · 日期：2026-10-21 · 角色：架构（v0.6）· 目标版本：v0.6.0 和 v0.6.1（docs/13 §15）。
> 依据：#16；#33/#35（hooks，已在 main）；daemon `agents/claude_code/{hooks,hook_state,hook_settings,adapter,argv,blockers}.py`、`inbox/need.py`、`hub/{snapshots,reduce}.py`、`projects/registry.py`、`control/{client,server}.py`、`cli/hooks.py`、`config/settings.py`；固件 `modules/{home,inbox}/*`、`lib/shell/{confirm,shell_menu}.cpp`、`lib/help/menu_model.cpp`、`lib/module_api/module.h`、`lib/core/{domain,app_state}.h`、`lib/input/keymap.cpp`（v0.4.0，`release/v0.4.0`）、`src/module_list.cpp`；`protocol/limits.json`、`schema/v0/*`、`tools/{flows,check_vectors}.py`；v0.4.1 工作树的 `notes/{render,writer}.py`、`recordings/{route,service}.py`、`app.py`、`rec.items`、`hello.caps`、`tools/rec_rules.py`、`tools/test_additive.py`；v0.5 工作树的 `dest/router.py`；docs/05 §6.10、§7.5、§8；docs/11 §1.2、§6.7、§7.5–§7.9、§18；docs/13 §3、§4.1、§5.2、§5.6、§6.1–§6.2、§10、§14、§15；docs/14 §1.2、§2.3、§3.1、§3.2、§5、§7、§10.3；docs/15 §2.3、§3.4、§3.6、§5；docs/16 §4、§7、§11（App SDK，进行中）；gates/cli-gates.md G3、§5a、§7；SECURITY.md；docs/02 §3、§4、§7（同类产品）；Claude Code 2.1.306 的 `--help` 和二进制字符串（来自调研摘要，本稿没有重新核对）。
> 标记：**[源]** 读过的代码或文档 · **[测]** 已有实测 · **[算]** 用脚本实算 · **[推]** 推断 · **[设]** 设计假设，可以改 · **[待]** 要用闸门或真机验证 · **👤** 需要你。
> 范围：只写 v0.6 的选项式回复、会议待办确认、待办应用和设备审批。没有接设备、没有跑 CLI、没有改代码。情境档案见 docs/18。屏幕示意按 240×135 画，一行约 40 个 ASCII 字符或 20 个汉字。评审意见的处理见文末附录。

## 0. 一页摘要

**你要的**（#16、docs/13 §15）：在设备上做决定只靠选，不靠打字。完成标准：**回复 session、确认待办、勾掉待办，三件事都不用打字。**

**现在差在哪**（§1.2）：

1. "等你"只知道一个 session 在等，不知道为什么。hook 已经算出原因（等权限、等批计划、在提问、空闲），但没有传到设备。
2. 设备对一个**正在等**的 session 什么都做不了。回复只对空闲的后台 session 有效；等权限、提问的会被改成排队反馈，而排队反馈不会自动送达。从设备开的 session 默认是 plan 模式 [源 registry.py `DEFAULT_MODE`]，所以最常见的"等你"很可能就是"批准计划" [推]。
3. 回复只能打字。设备只能打 ASCII，你又是用中文口述。
4. 会议里 AI 提的待办只记了条数；待办只能在 Obsidian 里勾。

**分两步发**（§17 第 1 条）：

- **v0.6.0**（固件 + cpad）：
  1. **"等你"按原因给选项**（§3）：`sessions.snapshot` 每行多一个 `why`。在"等你"里按 Enter，设备向 Mac 要这一行的**选择页**（`choice.get`）；按数字选一项，确认后发出 `agent.pick`。**设备从不发正文**，只发"第几项"和 Mac 给的一次性令牌 `tok`。
  2. **预设回复**（#16，§4）：空闲的后台 session 显示"1 继续 / 2 看起来不错，接着做"（第 2 项只在看得到它最后说了什么时才出现）。每个项目可以在 Mac 上配（`$CPA_HOME/replies.json`，`cpa reply`）。送达仍走现有的 stop → `--resume <sessionId> -- <text>`（不带任何别的 flag）。万一 fork 出副本，cpad 先把副本停掉。
  3. **会议待办逐条确认**（§6）：一条一条看。T = 我的待办；Q = 给 AI；Opt+G = 送到别的去向；Opt+D = 跳过。三种都经 docs/14 的去向路由，`cpa audit` 看得到。`summary.md` 一个字不改。
  4. **待办应用 Opt+W**（§7），三个标签：我的（每日笔记里带 `^cpa-` 块 ID 的复选框，Enter 勾掉或取消）、待确认（会议待办）、给 AI（各项目的队列）。勾掉只改那一行的 1 个字节，改之前核对这一行没被别人改过。
- **v0.6.1**（只动 Mac 端，先过闸门 G-D1）：
  5. **设备审批**（§5，**默认关**，`cpa decide on` 开启）：经 Claude Code 的 `PermissionRequest` hook，在设备上处理等权限、等批计划、AskUserQuestion。**设备不在、不答、超时、出任何错，都回落到 Mac 上的正常对话框，从不自动允许。** 高风险的对象（命令、路径、网址）**原样显示**，显示不全就不上设备；高风险的"允许"要按 **Y**，菜单也替你按不了。G-D1 不通过，v0.6.1 就不发，v0.6.0 不受影响。

**协议**（v0.6.0 一次定完，v0.6.1 不再改协议）：
- 新增 6 个消息：`choice.get`、`choice.sheet`、`agent.pick`、`todo.list`、`todo.items`、`todo.act`。只读的那一对不放在 `agent.*` 名下，所以"`agent.*` 只从确认框发出"这条规则不变。
- 已有消息加 6 个可选字段：`sessions.snapshot` 行的 `why`、`queue.snapshot` 的 `review`、主机 `hello.caps` 的 `choose`、`decide`、`todo_app`，设备 `hello` 的 `feats`。
- `v` 仍为 0，**不新增错误码**。
- 两个大回复 `choice.sheet`（最坏 1290 B）和 `todo.items`（最坏 1699 B）[算]，和 `rec.items` 共用"最多一个在途"的通道，所以设备接收缓冲的最坏情况不变，还是 7424 / 8192 B。
- 6 个新 schema 要和固件 codec 的登记同一批合并（§9.7）。

**安全**（§13）：
- 执行仍然只走"在线请求 + 设备确认框（400 ms 防误触）+ 不重试"。选择页打开时没有预选项，开页 400 ms 内的 Enter 无效。
- v0.6.0 新开的数据流：Stop hook 多转发一段**结尾摘录**（≤120 B，和现有的开头摘录同样清洗）；cpad 改每日笔记里的一个字节。
- v0.6.1 新开的数据流：审批时把工具参数的短摘要送到设备。SECURITY.md 里"工具参数不离开 hook 进程"要改写：它只在你 `cpa decide on` 之前成立。设备审批**不带 PIN 锁**，没锁的设备能批准命令，这一点写进 SECURITY.md。

**需要你拍板 7 件事**（§17），都有默认值。

## 1. 起点和缺口

### 1.1 今天有什么

| 部件 | 现状 | 证据 |
|---|---|---|
| hooks（#33，已在 main） | `cpa hook` 只转发白名单字段，从不输出，0.5 s 内退出 0；`hook_state.merge()` 算出 phase、原因 `why`（permission / plan / question / input / login / error）和 `msg`。`msg` 是 `last_assistant_message` **开头**的 ≤120 B 摘录，`excerpt()` 会删掉 `` ` ``、`*`、`# ` 和格式字符 | [源 hooks.py:1-8、32-33、48-63；cli/hooks.py:1-5；hook_state.py:1-13] |
| 原因送到设备 | **没有**。`merge_pairs()` 只返回 `(info, project)`，把 `why` 和 `msg` 丢了；`sessions.snapshot` 的行只有 `st` 和 `need` | [源 hook_state.py:175-178；sessions.snapshot.schema.json] |
| 对话框等待的宽限 | 等对话框的 hook 状态只有 `NEEDS_GRACE_S = 2 s`，之后和它不一致的轮询结果就赢 | [源 hook_state.py:8-9、25-26、148-154] |
| "等你"的顺序 | `_order` 只分"要你 / 在跑 / 其他"三组，组内按 `since`；快照最多 10 行 | [源 hub/snapshots.py:57-59、62-80] |
| 回复 | 条件：链路 Ready、`caps.reply`、后台、空闲。`experimental_bg_reply` 默认 false。做法：先 stop，再 `--bg --resume <sessionId> -- <text>`。fork 一次，这一轮运行里回复就全关（`E_REPLY_FORKED`），但**副本不会被停掉** | [源 adapter.py:127-153；argv.py:51；confirm.cpp:27-35；settings.py:60] |
| `since` | 只在轮询看到 status 变了时才重新计时；两次轮询（3 s）之间跑完的一轮看不出来 | [源 inbox/need.py:3、30-33] |
| 等权限、提问的 session | 设备判成 `NotIdle`，改存排队反馈，之后不会自动再送 | [源 confirm.cpp:32；05 §7.5] |
| G3 | stop → resume 不带 flag 时 ID 不变 ✅。模型是否真的回复**没验证**，当时 CLI 没登录。2.1.306 的 `--help` 写着：session 还在运行时 `--resume` 会另开一个副本 | [测 cli-gates.md §4；调研] |
| "等你"应用 | 行 = `need=true` 的 session。Enter 进入打字反馈，再确认；Opt+D 忽略（只在线）；详情行是 `claude attach <short>` | [源 inbox_logic.cpp:38-70；inbox_module.cpp:37-57] |
| 菜单 | 浮层上的"本页"列出那一层的绑定，Enter 关菜单后**重放**那个键（docs/13 原则 4） | [源 menu_model.cpp:70、134；shell_menu.cpp:138-146] |
| 会议待办 | `result.json.actions[]`：最多 20 条，`text` 最多 200 字，带 `owner`、`due`、`confident`。cpad 只记条数 `actions_n`，笔记里写"待办 N"。v0.4.1 的 `rec.items.todo_n` = `summary.md` 里没勾的条数 | [源 docs/11 §7.6、§6.7；v0.4.1 recordings/service.py:235-241] |
| 待办 | v0.4.1 写成 `- [ ] HH:MM #key 正文 ^cpa-<id>`。`NotesWriter` 只追加，按块 ID 幂等；**每次启动都重放整个事件历史**，所以你在 Obsidian 里删掉的行，下次启动会被补回来 | [源 v0.4.1 notes/render.py:48；writer.py:55-71；app.py:118-122] |
| 在途通道 | `queue.done`、`inbox.dismiss`、`rec.*` 共用 1 个，超时 10 s；`agent.*` 另有 1 个，超时 30 s。协议工具把所有 `agent.*` 都归到后一个 | [源 limits.json；docs/13 §10.2；tools/flows.py:75-79] |
| 控制 socket | 一行请求最多 1024 B；`client.request` 的连接和读用同一个超时 | [源 control/server.py:27；control/client.py:14-29] |

### 1.2 缺口

| # | 缺口 | v0.6 怎么补 |
|---|---|---|
| 1 | 设备不知道 session 为什么在等 | 快照行加 `why`；选择页里给 1–3 行说明（§3.1、§3.2） |
| 2 | 正在等的 session 在设备上动不了 | 设备审批（§5，v0.6.1），默认关 |
| 3 | 回复要打字 | 预设回复（§4） |
| 4 | 会议待办没人确认；待办勾不掉 | 逐条确认（§6）；待办应用（§7） |

### 1.3 负责什么，不负责什么

| 负责 | 不负责（去哪） |
|---|---|
| **v0.6.0**：快照行的原因；选择页（`choice.get` / `choice.sheet` / `agent.pick`）；预设回复和 `replies.json`；会议待办逐条确认（经去向路由）；待办应用的三个标签；勾掉和取消。**v0.6.1**：设备审批（hook 的 `--decide` 模式、摘要、等待簿、风险分键） | 应用骨架和检查工具 `new_app.py`、`check_apps.py`：归 docs/16（App SDK），本版只用它们。交互式和桌面端 session 的回复：仍在 Mac 上处理，mods 适配器进待办池（§4.4）。语音回复：v0.7，走录音契约 v1（§17 第 6 条）。离线勾待办。排队反馈在 session 空闲后重发：待办池。PIN 锁：待办池（§13）。情境档案：docs/18。通用的主机驱动界面 `ui.*`：v0.7，本版的选择页是它的雏形 |

### 1.4 和其他文档的分工

- **docs/14**：D5-B 的"待处理"排在"等你"最上面。v0.6 的顺序是：待处理 → 要你决定的 session（权限、计划、提问）→ "会议待办 N 条待确认"这一行 → 其他要你看的 session（登录、出错、空闲）。会议待办的 T、Q、Opt+G 都交给 docs/14 的路由（`router.on_capture`），所以 docs/14 §1.2 留给 v0.6 的"会议纪要里的待办逐条外发"在这里一起做（§6）。
- **docs/15**：MCP 只读文件。勾掉的待办写回文件，MCP 立刻看得到。会议待办的"已处理"要反映到 `list_todos`（§12）。勾待办是 cpad 第一次**原地改**每日笔记，打破了"cpad 只追加"，记在差异表里。
- **docs/16**：待办应用照 App SDK 的约定写（`expect_app_contract`、`check_apps.py`），主页格子放进第一方九宫格第 4 行的空位，不占 docs/16 给应用留的那一行。
- **docs/18**：v0.6 新增了几样**离线也会显示的 Mac 数据**：快照行的 `why`、`queue.snapshot.review`、"给 AI"标签（来自 `queue.snapshot`）。v0.8 要把它们和现有的 session、队列一样按档案分开（docs/18 §3.6）。选择页和待办页是一次性的：链路离开 Ready、锁屏、换了一台 Mac（`host_id` 变了）都会清掉（§3.6），不进任何缓存。v0.6.1 的 cpad 只在设备报告"当前档案归这台 Mac"时才让 hook 等（docs/18 §7.1 D9）。

## 2. 原则

1. **设备从不发正文去执行**：预设回复和审批只发 `tok` 加编号。预设文字、拒绝说明、答案标签都在 Mac 上。打字回复（现有的 `agent.reply`）照旧保留。
2. **执行只走在线请求 + 设备确认框 + 不重试**（继承 SECURITY.md）。`agent.*` 只能从 `confirm()` 发出（继承 module.h）；只读的 `choice.get` 不在 `agent.*` 名下。
3. **选项绑在令牌上**：设备看到什么，Mac 就执行什么。Mac 那边情况变了，令牌作废，回 `E_NOT_REPLYABLE`，绝不"换成相近的那一项"。情况变了包括：session 开始跑了、又跑完了一轮、对话框已经有人在终端答了、预设改了。
4. **审批默认关；出错就回落到 Mac 对话框，从不自动允许。**
5. **按风险分键，防误触**：高风险的"允许"在确认框里按 Y，Enter 和菜单都替代不了；选择页打开时没有预选项，开页 400 ms 内的 Enter 无效。
6. **文件是真相**：待办以每日笔记为准；`summary.md` 归 WhisperWoof，cpad 不写 [源 录音契约 v1 单写者表]。
7. **一个字母一件事**（延伸 docs/13 原则 2）：T 待办、Q 给 AI、Opt+G 选去向、Opt+D 处理或跳过、数字 = 项目。选择页是唯一的例外（§3.3）。
8. **看到的就是要执行的**：设备上要你批准的对象（命令、路径、网址）原样显示，不删字、不压空白、不脱敏；做不到原样显示，这一项就留在 Mac 上处理（§5.2）。

## 3. "等你"：每一行都能选

### 3.1 原因从 hook 到设备

- `hooks.py`：Stop 事件除了现有的开头摘录 `msg`，再转发一段**结尾摘录** `tail`：`last_assistant_message` 的最后 ≤2000 个字符经同一个 `excerpt()` 清洗，取最后 ≤120 B。问题通常在结尾。白名单 `KEYS` 加 `tail`，daemon 的 `parse()` 照样严格校验。
- `merge_pairs()` 改成返回 `(info, project, why, msg, tail, at)`，`at` 是 hook 状态的时间；`inbox/need.Tracked` 加这几个字段。
- `sessions.snapshot` 的行加可选字段 `why`，**只在 `need=true` 时出现**：
  - 取值 `permission`、`plan`、`question`、`input`、`login`、`error`；
  - 用模式 `^[a-z]{1,10}$`，不用枚举。设备遇到不认识的值，当作没有这个字段；
  - 没有 hook 时用 `waiting_for` 回退：permission → `permission`，question → `question`，dialog → 不给。
- **v0.6.1 起，等待簿是真相**：cpad 正替一个 session 拿着 hook 时，这一行一律 `need=true`、`why` 取等待簿里的，不管轮询怎么说。原因：hook 还在跑时对话框可能还没出现，`agents --json` 很可能报 busy [待 G-D1 d]，按 2 s 宽限这一行会在几秒内消失，而 hook 还要等 100 s。
- **顺序**：`_order` 在"要你"这一组里先按原因排：权限、计划、提问 → 登录、出错 → 空闲；同一类里等得最久的在前。快照只放 10 行时，最该你处理的不会被挤掉。
- `msg`、`tail` 不进快照（10 行 × 120 B 放不下），只在选择页里给（§3.2）。
- 设备上的写法：权限、计划、提问、空闲、登录、出错。

### 3.2 选择页：按原因给选项

| `why` | 条件 | `kind` | 选项（cpad 生成） | 选了以后 |
|---|---|---|---|---|
| permission | v0.6.1，设备审批开着，hook 正在等（§5） | `permission` | 1 允许这一次；2 本 session 都允许（默认不出现，§5.6）；3 拒绝：先停下等我；4 拒绝：换个办法 | 经 hook 回答对话框 |
| plan | 同上，工具是 ExitPlanMode | `plan` | 1 批准，改文件仍会问；2 不批准：继续完善计划 | 同上 |
| question | 同上，AskUserQuestion，只有 1 个问题、单选 | `question` | 1–4 = Claude 给的选项 | 同上，带上答案 |
| input / error，或者没有原因、status 是 idle（没装 hook） | 后台、空闲、`caps.reply` | `reply` | 这个项目的预设，最多 5 个 | stop → resume（§4） |
| 以上都不满足 | — | `none` + 原因码 | 没有 cpad 给的选项 | — |

- **`none` 的原因码**（`why` 字段），中文由设备自己配：
  - `interactive`：交互式 session，在 Mac 上处理；
  - `no_decide`：这台 Mac 没开设备审批（v0.6.0 里所有权限、计划、提问都是这一种）；
  - `mac_only`：开了，但这一项只能在 Mac 上处理（命令显示不全、有疑似密钥、MCP 工具、多个问题或多选、子 agent 发起的）；
  - `released`：等得太久，已经交回 Mac 的对话框；
  - `elicit`：MCP 服务在向你要输入 [源 hook_state.py:83-84]；
  - `quota`：额度用完，等恢复后在 Mac 上继续 [源 hook_state.py:85-86]；
  - `no_reply`：回复功能没开；
  - `login`：Mac 上的 claude 没登录；
  - `busy`：session 正在跑；
  - `gone`：session 已经不在了。
- **设备自己加的一行**（不编号，数字选不到它）：`reply` 页最后一行是"打字回复…"，`none` 页最后一行是"打字留言（排队）…"。两者都是今天在这一行上按 Enter 的行为：打字、确认，由确认框决定直接回复还是存成排队反馈 [源 confirm.cpp:27-35]。所以 `none` 页不比今天少功能。所有页的提示栏都写"`` ` ``返回（留给 Mac）"。
- **说明行**：最多 3 行 `lines`，每行 ≤64 B。
  - 权限：要批准的对象原样占 1–2 行，再加一行说明（§5.2）；
  - 计划：计划的前 3 行，另给 `total`（计划共几行）；
  - 提问：问题；
  - 回复：session 最后说的话，取 `tail`（结尾，≤120 B，分两行），标"它最后说"。没有 `tail` 时这一页写"（看不到它最后说了什么）"，并且不给带 `when:"seen"` 的预设（§4.1）。
- **倒计时**：审批页带 `ttl_s`，也就是 hook 还会等多少秒。到 0 以后对话框留在 Mac 上。

### 3.3 按键

| 键 | 作用 |
|---|---|
| 1–6 | 直接选第 N 项，弹确认框 |
| `;` `.` | 移动。**开页时没有选中项**，第一次按才落到第 1 项 |
| Enter | 选中的那项，弹确认框；"打字回复…" / "打字留言（排队）…"那一行进打字页。没有选中项时不起作用；**开页 400 ms 内的 Enter 一律忽略** |
| Opt+D | 忽略这个 session（和列表里一样，只在线）。v0.6.1 里如果 cpad 正拿着它的 hook，同时立刻放手，对话框留在 Mac 上 |
| `` ` `` / Backspace | 回到列表，什么都不做。hook 继续等，超时后对话框留在 Mac 上 |

- **为什么用数字**：#16 要的就是编号预设（例如 1 继续 / 2 看起来不错 / 3 暂停）。选择页里没有"项目"这个概念，所以数字在这里是"第几项"。这是原则 7 的例外，只在选择页里有效（§17 第 2 条）。
- **为什么不预选**：v0.4 里在"等你"的行上按 Enter 是一个无害的打字页，你已经习惯连按。如果开页就停在第 1 项（最宽松的"允许这一次"或"继续"），Enter、Enter、过 400 ms 再 Enter 就批准了。现在要么按数字（明确选了哪一项），要么先按 `;` `.`。
- **菜单**："本页"里写"1-6 选这一项（按 1-6）"，按 Enter 不执行，和 docs/13 §6.1"一组键的行只作说明"一致。

### 3.4 确认框（按风险分键）

| 选项 | 确认框写什么 | 接受键 |
|---|---|---|
| 允许（高风险：命令、联网、项目外的文件、项目里的敏感文件）；批准计划 | 再写一遍对象，加"只这一次"；计划写"只看到前 3 行（共 N 行）" | **Y**（Enter 无效） |
| 允许（低风险：项目里改普通文件）；本 session 都允许；回答问题；预设回复 | 选项和对象 | Enter |
| 拒绝；不批准计划 | 选项 | Enter |

- 都有 400 ms 防误触 [源 confirm.cpp:56]；`` ` `` 取消。
- 新增按键层 `Layer::ConfirmHigh`：`y` → Confirm，返回键和 `` ` `` → Cancel。Y 是普通字母，不会产生幽灵键；`test_keymap_static` 会检查 [推]。
- **菜单替你按不了 Y**：在 ConfirmHigh 上打开 Tab 菜单，"本页"里"Y 允许"那一行**只作说明**（`ev.kind = None`），行尾写"要直接按 Y"。否则 Tab → 选中这一行 → Enter 会重放 `y`，绕过"只认 Y"。这是 docs/13 原则 4（菜单行就是替你按键）的一个例外，记在差异表里；`test_menu_model` 和 `test_menu_dispatch` 加断言。
- `ConfirmKind::Pick` 离线时**不排队**，提示"离线，没发送"（`Route::Unavailable`）。选项和令牌都只在这一刻有意义。

### 3.5 离线和异常

| 情况 | 设备怎么做 |
|---|---|
| 离线 | "等你"显示上次的快照，标"○ 离线"。Enter 打开一个小页面："离线，连上 Mac 再选"，下面一行"打字留言（排队）…"，同样不预选。选择页、审批、预设都不可用 |
| 主机没有 `caps.choose`（旧 cpad） | Enter 走今天的路 |
| 在途通道正忙 | 提示"Mac 正忙，稍后再试"（docs/13 §10.2） |
| `agent.pick` 回 `E_NOT_REPLYABLE` | 提示"情况变了，没发送"，关掉选择页，列表随快照刷新 |
| `agent.pick` 30 s 没有回复 | 提示"Mac 没回应，不确定是否送达"。**不重试** |
| 锁屏 | 看不到"等你"。锁屏卡片只写"等你 N"。要先解锁才能选 |

### 3.6 什么时候清掉

选择页（`ChoiceSheet`）和待办页（`TodoPage`）只在内存里、只在打开时有效。下面任一情况发生，设备立刻清掉它们，回到列表：

- 链路离开 Ready（拔线、超时、cpad 重启）；
- 锁屏；
- 收到的主机 `hello` 里 `host_id` 和上一次不同（换了一台 Mac）；
- 选择页打开超过 120 s（令牌也过期了）。

选择页里有命令和路径，所以锁屏之后、换到另一台 Mac 之后都不能再显示。

## 4. 预设回复（#16）

### 4.1 在 Mac 上配：`$CPA_HOME/replies.json`

```json
{"version": 1,
 "default": [{"label": "继续"}, {"label": "看起来不错，接着做", "when": "seen"}],
 "projects": {"cpa": [{"label": "跑全部测试", "text": "跑全部测试，修好失败的再停。"}]}}
```

- **规则**（纯函数 `parse_replies()`，严格校验）：
  - `version` 必须是 1；顶层和每一项都不许有多余字段，所以结构上放不进 token；
  - `default` 最多 5 项；`projects.<key>` 最多 5 项，**整组替换**这个项目的默认；
  - `label` 1–36 B，不含控制字符、`"`、`\`；
  - `text` 可选，默认等于 `label`，必须通过 `check_text()`（1–500 B、不含控制字符、去掉首尾空白后不以 `-` 开头）[源 argv.py:22-27]；
  - `when` 可选，只有一个取值 `seen`：只在选择页有 `tail`（你看得到它最后说了什么）时才给这一项。"看起来不错"这类话，没看到内容就不该出现；
  - 文件 0600。
- **为什么不放进 `projects.json`**：`dump_registry()` 只写已知字段 [源 registry.py `dump_registry`]，加进去的字段会被下一次 `cpa project add` 抹掉。
- **命令行**（在 Mac 上打字，不在设备上）：`cpa reply ls [--project k]`、`cpa reply add [--project k] <label> [--text …] [--seen]`、`cpa reply rm`、`cpa reply reset`、`cpa reply status`（能不能用、为什么）。
- **内置默认**（文件不存在时）：继续 / 看起来不错，接着做（`when:"seen"`）。`text` 都等于 `label`，Claude 读得懂中文。原草稿里的"先停一下，等我"去掉了：对一个空闲的 session 发这句话，只是一次 stop + resume（有 fork 风险、跑一轮模型），效果和 Opt+D 忽略一样。

### 4.2 设备只发编号

1. 设备发 `choice.get{sid}`，cpad 回 `choice.sheet{tok, kind:"reply", lines, opts}`。
2. `tok` 绑定这些东西：sid、session 当时的 `(status, since)`、hook 状态的 `at`、`msg` 和 `tail` 合起来的 sha256 前 8 位、`replies.json` 的内容哈希、这一页的预设顺序。
3. 设备发 `agent.pick{sid, tok, n}`。cpad 逐条核对：
   - 令牌存在、没用过、不超过 120 s；
   - session 还在列表里，是后台、空闲，`since` 没变；
   - hook 状态的 `at` 和摘录哈希没变。两次轮询之间跑完的一轮，`since` 看不出来，但会有一次新的 Stop hook，`at` 和 `tail` 就变了；
   - `replies.json` 的哈希没变。
4. 都对上了，才展开成文字，交给 `adapter.reply()`。
5. 记一条 `agent.replied{sid, preset:n}`。不记正文，现有记录只多一个可选字段。

### 4.3 怎么安全送到 session（fork 风险）

- **只送后台、空闲的 session**。交互式的、正在跑的、正在等的，一律不 resume：2.1.306 对还在运行的 session 执行 `--resume`，会另开一个副本 [调研]。
- pick 时 cpad **重新 list 一次**，status 必须是 idle，`since` 必须没变；否则回 `E_NOT_REPLYABLE`。
- argv 照旧：`stop <short>`，再 `--bg --resume <完整 sessionId> -- <text>`，**不带任何别的 flag** [测 G3 4b]。
- fork 检测照旧，三个信号任一出现就算：stderr 里有 "started a copy"；stdout 的 id 不是原来的 short；`--all` 里多出同名的新 sessionId。
- **fork 以后先停副本**：副本的 id 来自 stdout 或 `--all` 的差集，cpad 立刻 `claude stop <副本>`，记 `agent.reply_forked{sid, copy_stopped}`；然后才回 `E_REPLY_FORKED`，这一轮运行里 `caps.reply` 变成 false，设备的回复页变成 `none/no_reply`。否则副本会在同一个工作树里和原 session 一起跑"继续"。停不掉（找不到副本 id，或 stop 失败）时，记 `copy_stopped:false`，设备提示"出现了副本，请在 Mac 上看"。
- **G3′（新闸门，v0.6.0 发布前，👤 约 5 分钟）**：在已登录的 2.1.306 上跑 cli-gates.md §7 的 G3 脚本。要看到三件事：模型确实回复了；ID 不变；stderr 里没有 "started a copy"。通过以后，把这个 CLI 版本写进 cpad 的常量 `VERIFIED_CLI["reply"]`。
- **默认开不开**（§17 第 3 条）：`experimental_bg_reply` 的默认值改成 `"auto"`：本机 `claude --version` 和 G3′ 验过的版本**主版本、次版本相同，并且不低于它**时才开；否则关，`cpa reply status` 写明原因（"CLI 2.2.0 没有验证过，重跑 G3′"）。你也可以在 `config.json` 里写 `true` 或 `false` 固定下来。G3′ 不通过，常量留空，`"auto"` 就等于关。
- 排队反馈仍然不会自动送达。"session 空闲后在设备上确认重发"进待办池。

### 4.4 交互式 session

仍然"在 Mac 上处理"。cardputer-cli-buddy 用 mods 的 `$.prompt.submit` 以用户身份往终端里注入文字 [源 02 §3 第 73 行、§4 第 93 行]。这是 2026-10 才有的新功能，会把任意文字打进你正在用的终端。v0.6 不做，进待办池。

## 5. 设备审批：权限、计划、提问（v0.6.1，默认关）

v0.6.1 只改 Mac 端：协议、选择页、ConfirmHigh 都已经在 v0.6.0 里（§9、§10），设备上那一套用假主机测过。G-D1（§5.7）是 v0.6.1 的第一件事，a、b 两项不成立，v0.6.1 就不发。

### 5.1 流程

```
Claude Code：后台 session 要用 Bash，弹权限对话框
 └ PermissionRequest hook：cpa hook --decide（stdin 是工具参数，最多读 1 MiB）
    1. 照旧转发白名单事件（≤0.5 s，#33 的老路）
    2. brief()：算摘要和风险事实。完整参数只在 hook 进程里看
    3. 连 cpad.sock（连接 ≤0.2 s），发 {cmd:"decide"}
    4. 第一阶段：0.5 s 内必须收到 {held:true} 或 {pick:null}；收不到就不输出、退出
    5. 第二阶段（只在 held 之后）：最多再等 110 s，等 {pick:…}
cpad：
    不满足条件 → 立即回 {pick:null} → hook 不输出 → Mac 上照常弹对话框
    满足 → 记进等待簿，回 {held:true}；这一行 need=true、why 取等待簿的
设备：
    等你 "!权限" → Enter → choice.get → 选择页 → 1 → 确认框按 Y → agent.pick
cpad：
    令牌对得上 → 回给 hook {pick:{act:"allow"}} → agent.result ok → 记 agent.decided
hook：
    打印固定形状的 JSON（§5.5），退出 0 → Claude Code 执行这个工具
```

**为什么分两阶段**：hook 对每个 session 的每个权限请求都会跑（matcher 是 `*`）。如果 cpad 的事件循环卡住了，单阶段的 hook 会一直等到 110 s 才放手；要是终端里的对话框要等 hook 结束才出来 [待 G-D1 f]，你在终端里的每一个权限请求都会被拖住将近 2 分钟。两阶段以后，cpad 卡住最多拖 0.7 s。

**cpad 立即回 null 的情况**（任一）：
- `decide` 没开，或者决定用的 handler 没通过核对（§5.8）；
- 本机 CLI 版本不在验证过的范围里，或者这次 hook 输入的形状和 G-D1 抓到的不一样（§5.8，`reason:"shape"`）；
- 设备链路不是 Ready，或者设备 `hello.feats` 里没有 `choose`（旧固件，§9.6）；
- 设备报告当前档案不归这台 Mac（docs/18 §7.1 D9，`reason:"other_profile"`）；
- session 不是后台的（v0.6.1 只管后台，§5.7 f）；
- session 不属于登记过的项目，或者 hook 的 `cwd` 和列表里这个 session 的 `cwd` 对不上；
- 摘要标成 `unsupported`（§5.2）；
- 请求来自子 agent（hook 输入带 `agent_id`）[设，G-D1 m]；
- **重复**：同一个 `(sid, tool_use_id)` 已经在等（比如 user 和 project 两份 settings 都装了 handler），后来的这个回 null（`reason:"dup"`），先来的继续等。hook 输入里没有 `tool_use_id` 时 [待 G-D1 n]，用"同一个 sid、同一个工具、摘要哈希相同、2 s 以内"判断；
- 同一个 session 来了一个**不同**的新请求：旧的先回 null，只留最新的（一个 session 同时只有一个对话框）。

**等待中**：
- 设备断开：1 s 内对所有等待项回 null；
- cpad 的上限 100 s 到了：回 null，记 `agent.decide_fallback{sid, reason:"timeout"}`；
- 你在设备上对这一行按 Opt+D：`inbox.dismiss` 到了 cpad，它对这个 sid 的等待项立刻回 null（`reason:"dismissed"`），不用再干等 100 s；
- hook 被 Claude Code 结束（比如你在终端里答了，或者 handler 超时）：连接断开，等待簿删掉这一项，之后的 pick 回 `E_NOT_REPLYABLE`，设备上这一行变成 `none/released`。

**超时**：连接 0.2 s、第一阶段 0.5 s；第二阶段 cpad 100 s < hook 110 s < settings 里 handler 的 `timeout` 120 s。这样一定是 cpad 先放手。

### 5.2 摘要 `brief()`（只在 hook 进程里跑）

**对象**（`obj`）是要你批准的那样东西本身，**原样**送到设备，用新的 `brief_obj()`，它从不删字：
- 只许 ASCII 可打印字符（0x20–0x7E），单行，≤128 B（设备上分两行显示，每行 ≤64 B）；
- 脱敏规则（见下）一条都不能命中；
- 任一条不满足，这一项就标 `unsupported`，回 Mac 上处理。

ASCII 的限制是有意的：设备字库对 ASCII 全覆盖，非 ASCII 的字符可能缺字形、可能是长得像的字，看到的就不一定是会执行的。

| 工具 | 对象 `obj` | 说明行 `note` | 风险 |
|---|---|---|---|
| Bash | 完整的命令，原样 | "Claude 说：" + `description`（模型写的，经 `excerpt()`，≤64 B，有才给） | 高 |
| Edit / MultiEdit / Write / NotebookEdit，文件在项目里、不在敏感清单上（§5.3） | 相对路径（项目里的路径允许 BMP 内的可打印字符，同样不删字；做不到就 `unsupported`） | "改 N 行"或"新文件 N 行" | **低** |
| 同上，文件是项目里的敏感文件，或在项目外 | 绝对路径，`~/` 开头，原样 | 同上 | 高 |
| Read / Grep / Glob / LS（只有读项目外才会问） | 路径或搜索词，原样 | — | 高 |
| WebFetch | 完整的网址，原样（不止主机名：查询串里可能带着要发出去的数据） | — | 高 |
| WebSearch | 搜索词，原样 | — | 高 |
| `mcp__<server>__<tool>` | — | — | `unsupported`：参数形状各异，显示不全 |
| ExitPlanMode | — | 计划的前 3 个非空行（`excerpt()`，每行 ≤64 B），`total` = 计划的总行数 | 高（批准计划） |
| AskUserQuestion | — | 问题（≤128 B，两行）；选项标签（每个 ≤36 B，最多 4 个）；只支持 1 个问题、单选，否则 `unsupported` | 低 |
| 其他 | — | — | `unsupported` |

- 计划、问题、说明都是模型写的文字，不会被执行，所以可以经 `excerpt()` 清洗；**对象不经 `excerpt()`**。评审发现 `excerpt()` 会把 `rm -rf *` 显示成 `rm -rf`、把 `` echo `id` `` 显示成 `echo id` [源 hooks.py:32-33]，原草稿就是这样把"看到的"和"会执行的"弄岔了。
- **脱敏**（用于说明行；对象只检查，命中就 `unsupported`）：
  - `NAME=value`，`NAME` 含 token、secret、passw(or)d、api_key、auth、cookie、session（不分大小写）→ `NAME=***`；
  - `--token x`、`--password=x`、`--api-key x` 这类 → 值换成 `***`；
  - `Authorization:`、`Bearer` 后面的值 → `***`；
  - URL 里的 `user:pass@` → `***@`；
  - 连续 ≥24 个 `[A-Za-z0-9+/=_-]` 字符 → `***`。所以 `echo <一长串 base64>|base64 -d|sh` 这种命令一律留在 Mac 上。
- 摘要、问题和标签都只经 0600 的 `cpad.sock` 交给 cpad。cpad 只放在内存里，`events.jsonl` 和日志只记长度和 sha256 前 8 位（§13）。

### 5.3 风险分级

- **低**：回答 AskUserQuestion；或者改文件，并且同时满足下面五条：
  1. 工具是 Edit、MultiEdit、Write、NotebookEdit 之一；
  2. 目标文件的 realpath 在 session 的 `cwd` 里（hook 算），而这个 `cwd` 又在**登记过的项目根目录**里（cpad 用列表里的 `SessionInfo.cwd` 查）。只看 hook 的 `cwd` 不够：`cwd` 可以是任何目录；
  3. 项目根目录本身是安全的：不是 `$HOME`，不是 `/`，也不是 `$CPA_HOME`、`~/.claude`、`notes_dir` 的上级目录。项目根可以登记成任意绝对路径 [源 registry.py:67-68]，登记成 `$HOME` 时，"项目里"就把 `~/.claude`、`~/.zshrc` 都包进来了；
  4. 相对路径不在**敏感清单**上 [设]：`.claude/**`、`.mcp.json`、`.git/**`、`.envrc`、`.husky/**`、`.vscode/**`、`.github/workflows/**`、`package.json`。改这些文件等于改权限或者埋下以后会自动执行的代码；
  5. hook 报的 `cwd` 的 realpath 哈希，等于列表里这个 session 的 `cwd` 的 realpath 哈希 [源 agents/base.py `SessionInfo.cwd`]。
- **高**：其他一切，包括 Bash、联网、项目外的读写、敏感清单上的文件，还有批准计划。
- **谁算的**：cpad 按上面五条自己算 `tier`，不用 hook 报的等级。但第 2 条里"在不在项目里"、realpath 都是 hook 在本机算出来报给 cpad 的。hook 是我们自己的程序，和 cpad 在同一个用户下；同一用户下的恶意进程本来就不在防护范围内 [源 SECURITY.md]。cpad 能做的是交叉核对：第 3、5 条用它自己的登记表和列表，敏感清单用对象本身（改项目里的文件时，对象就是项目里的相对路径）再查一遍。

### 5.4 cpad 的等待簿（纯函数 `choices/book.py`）

- `Pending{sid, tuid, tok, kind, tool, tier, obj, note, lines, labels, total, since, deadline}`，按 sid 存，同时最多 16 项 [设]。
- 函数：`hold`、`answer(tok, n)`、`expire(now)`、`link_down()`、`closed(sid)`、`dismissed(sid)`、`is_dup(sid, tuid, key, now)`。返回新的簿和要做的效果（回给哪个 hook 连接什么），不改原对象。
- 令牌：`secrets.token_urlsafe(9)`，12 个字符，用一次就作废。

### 5.5 hook 只输出固定的几种形状 [待 G-D1 定稿]

```jsonc
// 允许一次
{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"allow"}}}
// 拒绝（message 是 hook 里的常量；"先停下等我"在 G-D1 l 确认后加 "interrupt":true）
{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"deny","message":"…"}}}
// 回答 AskUserQuestion（updatedInput = hook 自己读到的 tool_input，加上 answers）
{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"allow",
  "updatedInput":{"questions":[…原样…],"answers":{"<问题原文>":"<选中的标签>"}}}}}
// 本 session 都允许：默认不出现，形状见 §5.6
// 其他一切情况：不输出任何东西 → Mac 上照常弹对话框
```

- **字符串从哪来**：要么是 hook 自己的常量（拒绝说明），要么来自 hook 自己读到的 `tool_input`（问题原文、选中的标签）。cpad 发给 hook 的只有三样：`act`（allow / always / deny / answer）、`i`（选项序号）、`m`（拒绝说明编号）。所以就算有别的进程冒充 cpad，最多也只能让 hook 输出这几种形状之一。同一用户下的恶意进程本来就不在防护范围内 [源 SECURITY.md]。
- **拒绝说明**（中文常量）：
  - `stop`："用户在 Cardputer 上拒绝了这一步。先停下，等用户的下一条消息。"G-D1 l 确认 `interrupt:true` 能让 session 停下时，同时带上它，不只靠模型听话；
  - `alt`："用户在 Cardputer 上拒绝了这一步。换一个不需要这一步的办法。"
  - `plan`："用户还没批准这个计划。继续完善计划，先不要开始实现。"
- 不带 `--decide` 的 handler 照旧"从不输出"。

### 5.6 "本 session 都允许"和批准计划

- **"本 session 都允许"默认不出现**。只有 G-D1 i 同时确认了两件事，才会给低风险的改文件加上这一项：
  1. 绝对路径规则的写法（我的推断是 `Edit(//<目录>/**)` 这种双斜杠形式 [推]）。范围取**核对过的 session `cwd`**（§5.3 第 2、5 条：它在登记的项目根里、和列表对得上），和判低风险用的是同一个目录；不取文件所在的目录，也不取 hook 自己报的、没核对过的路径；
  2. 同一个 `updatedPermissions` 里能再加几条 session 级的 `ask` 规则，覆盖敏感清单（§5.3 第 4 条），并且 `ask` 优先于这条 `allow`。否则"都允许"以后，改 `.git/hooks/*` 也不会再问。

  两件事有一件不成立，这一项就不做，低风险的改文件每次都回到设备上按一次 Enter。规则只写 `destination:"session"`，**不写进任何 settings 文件**；高风险从不提供"总是允许"。选项标签是"本 session 都允许改项目文件"（36 B，正好是 `label` 的上限）。
- **批准计划**：只给"批准，改文件仍会问"，不给"批准并自动接受修改"。确认框写"只看到前 3 行（共 N 行）"，按 Y。批准后 session 进哪个模式要 G-D1 h 实测 [待]：
  - 实测是"仍会问"：之后每次改文件都会回到设备上；
  - 实测是"自动接受修改"：确认框改写成"批准后会直接改文件"，仍按 Y。

### 5.7 闸门 G-D1（v0.6.1 的第一件，👤 在你已登录的终端里约 20 分钟）

照 cli-gates.md 的做法：每段一个临时空目录、`plan` 或 `manual` 模式、无害的 prompt，结束时 `stop` 加 `rm`。cpad 换成一个只回固定答案的小测试服务，不接设备。抓到的 hook 输入存成 `daemon/tests/fixtures/hooks/permission-request-2.1.306-*.json`，之后的单元测试都用它。

| # | 要确认 | 不成立时 |
|---|---|---|
| a | hook 不输出 → 照常弹对话框 | **v0.6.1 不发**，v0.6.0 不受影响 |
| b | allow 的形状 → 工具执行 | 同上 |
| c | deny + message → 模型看到这句话 | 拒绝只给"拒绝"，不带说明 |
| d | 后台 session 在 hook 等待期间，`agents --json` 的 status 是什么 | 等待簿已经是真相（§3.1），这里只决定 `hook_state` 要不要再改 |
| e | handler 超时以后：对话框还在，session 一直等到有人 `claude attach` | 按实测写进 §5.1 |
| f | 交互式终端：hook 跑着的时候对话框是同时显示（谁先答算谁的），还是等 hook 结束才显示 | 同时显示：以后可以加 `decide_interactive`（默认关）；等 hook 结束：交互式永远不管 |
| g | AskUserQuestion：allow 加 `updatedInput.answers` → 模型收到答案 | 提问页变成 `none/mac_only` |
| h | ExitPlanMode allow 之后进哪个模式 | 见 §5.6 |
| i | `updatedPermissions` 的形状、绝对路径规则的写法、session 级 `ask` 规则能不能盖过 `allow` | "本 session 都允许"不做 |
| j | handler 的 `timeout` 能不能设到 120 s；hook 输入里有没有 `cwd` | 按实测缩短超时；没有 `cwd` 就全当高风险 |
| k | 后台 session 被你用 `claude attach` 打开着时，终端里的对话框是不是也要等 hook | 是：后台 session 的等待上限从 100 s 降到 20 s，`cpa decide status` 写明 |
| l | deny 带 `interrupt:true` 时 session 会不会停下 | 不加 `interrupt`，只靠拒绝说明 |
| m | 子 agent 的权限请求（`agent_id` 有值）长什么样、对话框在哪 | 维持"子 agent 一律回 null" |
| n | hook 输入里有没有 `tool_use_id` | 去重改用"sid + 工具 + 摘要哈希 + 2 s" |

### 5.8 开关、handler 和 CLI 版本

- **handler 核对**：cpad 启动时和每次 settings 文件变了时，读 `~/.claude/settings.json` 和登记项目里的 `.claude/settings*.json`，确认决定用的 handler 在、只在一处。`caps.decide` 只有在 `decide:true` **并且** handler 核对通过时才是 true。
- **重装不丢**：现在的 `cpa hooks install` 会先删掉所有 `args` 以 `hook` 开头的 handler 再装普通的 [源 hook_settings.py:78-83、119-127]，重跑一次就悄悄把决定用的 handler 换掉了。改成：`install` 读配置里的 `decide`，开着就装决定用的那一版。
- **两份 settings 都装了**：`cpa decide status` 和 `cpa hooks install` 都警告；等待簿按 §5.1 去重，一个对话框只拿一次。
- **CLI 版本**：`VERIFIED_CLI["decide"]` 记 G-D1 验过的版本。本机版本和它主版本、次版本相同并且不低于它，才算可用；`cpa decide status` 在版本比验证的新时提示"比验证过的新，建议重跑 G-D1"。另外每个请求都做一次**形状自检**：hook 输入缺了 G-D1 抓到的必需字段，或多了不认识的 `decision` 相关字段，就回 null（`reason:"shape"`）。Claude Code 更新很勤，只认一个精确版本的话，每次更新审批就关掉了。

## 6. 会议待办逐条确认

### 6.1 数据从哪来，写到哪

- **来源**：`result.json` 的 `actions[]`，当作不可信输入，沿用 `results.py` 的校验（≤64 KB、只读已知字段）[源 docs/11 §6.6]。只看 `state:"done"`、`level:"summary"`、最近 14 天 [设] 的会议。
- **`ref`**：`<rec>.a<n>.<sha6>`，`sha6` 是这条待办原文的 sha256 前 6 位。`result.json` 重新生成以后，同一个序号上的文字变了，旧的决定就对不上，这一条重新算"待确认"，不会把决定落到别的条目上。
- **显示的文字**：原文先按 `router.queue_text()` 清洗（控制字符换成空格、去掉首尾空白），再截到 96 B，不截半个字，截了就加"…"。
- **四种处理**，T、Q、Opt+G 都交给 docs/14 的路由 `router.on_capture()`，`Capture{ref, source:"voice", dest, how, project, text}`，`origin:"confirmed"`（你刚在设备上按的）。`source` 记成 `voice`，因为这些文字来自转写；所以进队列的一律 `uncertain=true`：

| 键 | 处理 | 去向 | 写到哪 | 事件 |
|---|---|---|---|---|
| T | 我的待办 | `todo`，`how:"key"` | **今天**的每日笔记追加一行，格式就是 v0.4.1 的待办行 `- [ ] HH:MM #proj 文本 ^cpa-<rec>-a<n>`，文本 = 清洗后的全文加"（[会议：标题](recordings/…/summary.md)）" | `todo.mine{ref, rec, n, text, project, title, dir, at}`（内容事件，和 `item.received` 一样带正文，供 `NotesWriter` 写那一行）；路由记 `dest.decided`；写成以后记 `todo.written{ref}` |
| Q | 给 AI | `queue`，`how:"key"` | — | 路由记 `dest.decided` 和 `queue.added{origin:"dest", from_item:ref, uncertain:true}`；正文是**设备上显示的那段**（§6.3） |
| Opt+G | 送到别的去向 | 你在列表里选的，`how:"pick"` | 按 docs/14 §5 判定（剪贴板、文件等） | 路由的 `dest.*` 事件；进队列类的去向同样只用显示的那段 |
| Opt+D | 跳过 | — | 不写任何文件 | `todo.skipped{ref}` |

- **`summary.md` 不碰**：它归 WhisperWoof [源 录音契约 v1 单写者表；docs/13 §8.4]。所以同一条待办在 `summary.md` 里还是 `- [ ]`。两处计数要扣掉已处理的（M8）：
  - `rec.items.todo_n` = `summary.md` 里没勾的条数**减去**这场会已经处理过的条数，最少为 0 [源 v0.4.1 recordings/service.py:235-241]；
  - docs/15 的 `list_todos` 给会议里的条目加 `decided`，`status=open` 不再返回已处理的（§12）。
- **为什么走路由**：docs/14 §1.2 把"会议纪要里的待办逐条外发"留给了 v0.6；走同一个路由，`cpa audit` 能看到每一条去了哪；清洗规则也只有一份（`queue_text()`：以 `-` 开头的文字降级、不另外删字）。路由判成降级（比如 `bad_text`）时，设备提示"没送出：<原因>"，这一条仍是"待确认"。
- **幂等**：
  - 每条 `ref` 第一个决定生效，**跳过除外**：跳过什么都没写，以后可以改成 T、Q 或 Opt+G；
  - 同一个决定再发一次，回 `ack`，所以重试是安全的；
  - 不同的决定（跳过以外），回 `E_BAD_FIELD`（`field:"op"`），设备刷新。
- **重启不会补回你删掉的行**：`NotesWriter` 每次启动都重放整个历史，只看文件里有没有那个块 ID [源 writer.py:55-71；app.py:118-122]。所以 `todo.mine` 的那一行写成以后记一条 `todo.written{ref}`；重放时跳过已经有 `todo.written` 的 `todo.mine`，只补"事件记了、行没写成"的那些。你在 Obsidian 里删掉的行，下次启动不会回来。（普通笔记行也有同样的问题，那是现有行为，进待办池，不在本版改。）
- **改主意**：我的待办，在待办应用里勾掉，或者在 Obsidian 里删掉那一行；给 AI 的，在"给 AI"里按 Opt+D 完成；跳过的，在录音应用里打开那场会，重新决定。

### 6.2 流程与按键

- **入口**：
  - "等你"里的"会议待办 N 条待确认"一行（N 来自 `queue.snapshot.review`）；
  - 待办应用的"待确认"标签；
  - 录音应用的详情页按 Enter，只看这一场会，跳过的也列出来（v0.4.1 里详情页的 Enter 什么都不做 [源 docs/13 §8.2]）。
- **一次一条**，决定完自动跳到下一条；跳过去以后 **400 ms 内的 T、Q、Opt+D、Opt+G 一律忽略**，连按两下不会决定两条。`` ` `` 是"稍后"，什么都不记。
- **按 Q 之后选项目**：默认是这场会录音时归的项目，按 Enter 就用它；按 1–9 直接选别的（数字 = 项目，原则 7）；会议没归项目时必须选。
- **按 Opt+G**：用 `lib/ui/pick_list` 列出 `dests.snapshot` 里可用的去向（docs/14 §10.1 的同一份），`;` `.` 选，Enter 定。不预选。要项目的去向，接着选项目。
- **不弹确认框**：这几种处理都不执行任何东西。进了队列的，以后要开 session 仍然要你在设备上确认。docs/11 §1.2 说的"AI 待办要先在设备上确认才进队列"，这一页本身就是那个确认。

### 6.3 会议里的文字是不可信的

会议待办来自转写和 LLM，里面可能有别人说的话。所以：

- **进队列的就是你看到的**：Q（和 Opt+G 选了进队列类的去向）只排设备上显示的那段（≤96 B）。原文更长时，这一页底下多一行"Q 只排显示的这段，全文在纪要里"。T 写进笔记的是全文：笔记只是记录，不会被执行。
- **开 session 时看得出来源**：cpad 给 `queue.snapshot` 生成的摘要，对来自会议的排队项以"会议·"开头，所以开 session 的确认框上就能看到。
- **一律 plan 模式**：从会议来的排队项开 session，不管项目配的 `spawn_permission_mode` 是什么，都用 `plan` [源 registry.py `DEFAULT_MODE`]。
- 去向只由你按的键决定，不看文字（docs/14 §5.4 第 3 条）：会议里有人说"发到 Slack"，什么都不会发生。

## 7. 待办应用（Opt+W）

### 7.1 三个标签

| 标签 | 内容 | 来源 | 离线 |
|---|---|---|---|
| 我的 | 没勾的复选框。刚勾掉的留在列表里，离开页面后才消失 | 最近 30 天的每日笔记里带 `^cpa-` 块 ID 的行 | 不能看 |
| 待确认 | 会议待办，一条一条处理 | §6 | 不能看 |
| 给 AI | 各项目的排队项 | `queue.snapshot`，设备上已有，最多 12 条、每个项目最多 4 条 [源 limits.json] | 看上次同步的 |

`,` `/` 换标签，`;` `.` 移动；翻到最后一行时自动要下一页（和录音应用一样）。

### 7.2 我的：列出和勾掉

- **列出**：
  - cpad 扫 `notes_dir` 下最近 `todo_days`（默认 30）天的 `YYYY-MM-DD.md`，取匹配 `^- \[( |x|X)\] … \^cpa-[a-z0-9-]+\s*$` 的行，新的在前；
  - 显示的文字去掉时间、`#项目`、`[音频](…)` 这类链接（只留链接文字），≤96 B。
- **只列带 `^cpa-` 块 ID 的行** [设]：这些行都是 Cardputer 来的，包括打字、语音和会议确认。你在 Obsidian 里自己写的复选框没有稳定的 ID，v0.6 不列（§17 第 5 条）。
- **`ref`**：日期 + 块 ID + 这一行的 sha256 前 6 位，由 cpad 生成，设备原样带回。
- **勾掉（`op:"done"`）和取消（`op:"undo"`）**，cpad 在 `NotesWriter` 的同一个线程上按顺序做，所以和 cpad 自己的追加不会交错：
  1. 目录的 realpath 必须是 `notes_dir`；用 `O_RDWR | O_NOFOLLOW` 打开文件，`fstat` 确认是普通文件；
  2. 读文件，找**唯一**一行以这个块 ID 结尾的行；找不到，回 `E_UNKNOWN_ITEM`；
  3. 这一行的哈希要等于 `ref` 里的。不等，说明这一行刚在别处改过（比如 Obsidian），回 `E_BUSY`；
  4. 算出 `[` 后面那个字节的偏移，用 `os.pwrite` **只写这 1 个字节**（空格 ↔ `x`），其他字节一个不动；`fsync`；
  5. 再读回这一行核对：是预期的新内容就成功；不是（极小的窗口里 Obsidian 恰好整文件重写），记 `todo.tick_lost{ref}`，回 `E_BUSY`；
  6. 记 `todo.ticked{ref, done}`，不含正文。
- **设备上的提示**：`E_BUSY` 也可能是别的原因（文件正被写），所以一律提示"没改成，已刷新"，并重新列出。
- **为什么不整文件重写**：临时文件 + `os.replace` 在读和换之间有一个窗口，会盖掉 Obsidian 在这期间保存的内容。改 1 个字节、改完再核对，这个窗口小到只剩一次 `pwrite`。
- **为什么直接改那一行，不只记一条"完成"事件**：只记事件的话，Obsidian 里仍然显示没勾，文件和设备就对不上了；docs/15 也是以文件为准 [源 docs/15 §3.4]。
- **Obsidian 开着这个文件时** [待]：它应该会发现外部修改并重新载入 [推]。H6 里手动试一次：Obsidian 开着当天的笔记时，在设备上勾掉一条。
- **离线不能勾**：要改的是 Mac 上的文件，而 outbox 里只放笔记和排队项（继承 SECURITY.md）。

### 7.3 待确认、给 AI

- **待确认**：就是 §6 的那个页面。
- **给 AI**：
  - 按项目分组；
  - Enter = 开 session，和项目页的队列标签走同一条路：确认框 → 带 `qid` 的 `agent.spawn`；
  - Opt+D = 完成（`queue.done`）；
  - 超过 12 条时，最后一行写"还有 N 条：Mac 上 cpa queue ls"。

### 7.4 怎么进

- **Opt+W**。W 原来留给 WhisperWoof，#34 之后不需要了。它在 docs/14 §3.1 和 docs/16 §4.2 里都是第一方的保留字母，不占可绑定的那 9 个，所以不会和你在 v0.5 里绑过的去向冲突。
- **主页**：v0.4.1 的第一方格子是 4 行，第 4 行只有"L 锁屏"和"Tab 菜单"，中间空着 [源 docs/13 §5.6]。"W 待办"放进这个空位，第一方格子仍是 4 行；docs/16 给第三方应用留的那一行不受影响。
- Opt+T 仍是"记一条待办"（v0.4.1）。在待办应用里按 Opt+T 也是记一条，带 `how:"key"`（你明确按了 Opt+T），记完回到列表。docs/14 §3.2 说的 `how:"app"`（应用的默认去向）在这个应用里用不上：它没有"不按键就存"的路径。
- **为什么不让 Opt+T 打开应用**：docs/14 §3.2 定了"Opt+字母 = 记一条去那个去向"，N T Q S 是固定的四个。改 T 会让菜单"记到哪"那一节多出一个例外（§17 第 4 条）。代价是"待办"用了两个字母：T 记一条、W 打开应用。这和 docs/13 原则 2（一个字母一件事）不完全一致，记在差异表里。
- docs/16 说设备的模块列表只有 8 个位置 [源 docs/16 §10]。v0.6 之后是主页、项目、等你、系统、录音、待办 6 个。

## 8. 屏幕示意

"等你"（v0.6，有原因，会议待办排在要你决定的 session 后面）：
```
● 在线 ↑0 !4               13:42 83%
等你 (4)
▸!权限 cpa/fix-login     2分 bg
 !计划 cpa/架构设计       9分 bg
 会议待办 3 条待确认 ›
  空闲 notes/周报         1时 bg
Enter选 Opt+D忽略 ;.移动  Tab菜单
```

审批：要运行命令（v0.6.1，高风险，开页时没有选中项）：
```
cpa/fix-login · 命令 · 高风险  96秒
git push origin fix/login
Claude 说：跑测试并推送
 1 允许这一次
 2 拒绝：先停下等我
 3 拒绝：换个办法
1-3选 ;.移动 `返回        Tab菜单
```

确认框（高风险，要按 Y）：
```
允许运行这条命令？（高风险）
git push origin fix/login
cpa · fix-login · 只这一次
以后的命令还会再问。
Y 允许                   ` 取消
```

在它上面按 Tab，"本页"里那一行写"Y 允许（要直接按 Y）"，Enter 不执行。

审批：项目里改文件（低风险；"本 session 都允许"默认不出现）：
```
cpa/fix-login · 要改文件
src/auth/login.py · 改 12 行
 1 允许这一次
 2 拒绝：先停下等我
 3 拒绝：换个办法
1-3选 ;.移动 `返回        Tab菜单
```

批准计划：
```
cpa/架构设计 · 计划等你批准  高风险
1. 拆出 brief.py，加脱敏
2. hook 增加 --decide 模式
3. 加 12 个单元测试   （共 41 行）
 1 批准，改文件仍会问
 2 不批准：继续完善计划
1-2选 ;.移动 `返回        Tab菜单
```

提问：
```
web/landing · 在问你
首页主色用哪个？
 1 蓝色（现有品牌色）
 2 绿色
 3 橙色
其他答案要打字：claude attach 4c1e
1-3选 ;.移动 `返回        Tab菜单
```

预设回复（空闲的后台 session，v0.6.0）：
```
notes/周报 · 空闲 1 小时
它最后说：…草稿在 draft.md。要不要
我再补一段上周的数据？
 1 继续
 2 看起来不错，接着做
   打字回复…
1-2选 ;.移动 Opt+D忽略    Tab菜单
```

没有选项（`none`，v0.6.0 里的权限请求都是这样）：
```
cpa/fix-login · 等权限
这台 Mac 没开"设备审批"。
在 Mac 上处理：claude attach 8f3a
 打字留言（排队）…
`返回 ;.移动              Tab菜单
```

离线：
```
cpa/fix-login · 等权限      ○ 离线
离线，连上 Mac 再选。
 打字留言（排队）…
`返回 ;.移动              Tab菜单
```

会议待办，一条一条：
```
会议待办 2/3 · 客户同步 10/21
给 Alice 发报价单
负责人 我 · 截止 周五 · 把握 高
T 我的待办（今日笔记）
Q 给 AI → #cpa（1-9 换项目）
Opt+G 送到别处   Opt+D 跳过
T Q Opt+G Opt+D `稍后     Tab菜单
```

待办 · 我的：
```
● 在线 ↑0 !2               13:42 83%
待办 [我的] 待确认3 给AI
▸[ ] 周五前给 Alice 发报价 #cpa 10/21
 [ ] 买 USB-C hub               10/20
 [x] 打电话给水管工             10/19
 [ ] 改 README 的按键表    #cpa 10/18
Enter勾/取消 Opt+T记一条   Tab菜单
```

待办 · 给 AI：
```
待办  我的 待确认3 [给AI]
#cpa ▸修 login 的测试          排队
      会议·重构 brief.py
#web  会议·首页改蓝色
Enter开session… Opt+D完成  Tab菜单
```

待办，离线：
```
待办 [我的] 待确认 给AI      ○ 离线
连上 Mac 才能看和勾待办。
"给AI"能看上次同步的队列。
`返回                     Tab菜单
```

主页（v0.6，W 放进第 4 行的空位）：
```
▸P 项目 跑3   I 等你 !4   V 录音 录2
 N 记一条     T 记待办    R 开会
 Q 排队给AI   S 开session M 系统
 L 锁屏       W 待办 3    Tab 菜单
!权限 cpa/fix-login 2 分钟
```

- 字形：`[ ]`、`[x]`、`›` 代替 ☐ ☑ ⚠ 这类可能不在 efontCN_12 里的字 [待]。每个新页面都在设备测试和模拟器（`cpa-sim shots`）里画一遍，断言 `missing_glyphs == 0`（docs/13 §19 的做法）。
- 锁屏卡片不变，只显示数量。

## 9. 协议新增（只新增，`v` 仍为 0）

v0.6.0 一次定完，包括 v0.6.1 才用到的字段（`caps.decide`、`tier`、`ttl_s`、`total`、`permission` 等 `kind`）。v0.6.1 只改 Mac 端的实现。

### 9.1 总表

| 变化 | 方向 | 内容 | 兼容性 |
|---|---|---|---|
| `sessions.snapshot` 行加 `why?` | H→D | §3.1，只在 `need` 时出现 | 旧固件忽略 |
| `queue.snapshot` 加 `review?` | H→D | 会议待办待确认的条数（count） | 旧固件忽略 |
| 主机 `hello.caps` 加 `choose?`、`decide?`、`todo_app?` | H→D | 布尔或 null。`choose`：回答 `choice.get`、`agent.pick`；`decide`：设备审批开着、handler 核对通过、CLI 版本可用；`todo_app`：回答 `todo.*`。不叫 `todos`，免得和 v0.4.1 的 `caps.todo`（"能写待办行"）看混 | 旧固件忽略 |
| 设备 `hello` 加 `feats?` | D→H | 词的数组，每个 `^[a-z][a-z0-9_]{0,11}$`，最多 8 个；v0.6 固件发 `["choose"]` | 旧 daemon 忽略多出的键 [源 docs/14 §7.1，`validate.py obj()`] |
| `choice.get`（新） | D→H 请求 | `sid` | 只读，走共享在途通道（10 s）；设备先看 `caps.choose` |
| `choice.sheet`（新） | H→D 回复 | `sid`、`tok`、`kind`、`why?`、`tier?`、`ttl_s?`、`total?`、`obj_lines?`、`lines[≤3]`、`opts[≤6]` | **第二个大回复**，见 §9.5 |
| `agent.pick`（新） | D→H 请求 | `sid`、`tok`、`n` | 执行类，走 agent 通道（30 s）；只从 `confirm()` 发出；回现有的 `agent.result` |
| `todo.list`（新） | D→H 请求 | `view`（mine / review）、`max`（1–4）、`cursor?` | 走共享通道；先看 `caps.todo_app` |
| `todo.items`（新） | H→D 回复 | `items[≤4]`、`n?`、`next?` | **第三个大回复**，同一个通道 |
| `todo.act`（新） | D→H 请求 | `ref`、`op`、`project?`、`dest?` | 走共享通道；回 `ack` 或 `error` |

**命名**：只读的那一对叫 `choice.*`，不叫 `agent.*`。协议工具把所有 `agent.*` 都归到 agent 通道 [源 tools/flows.py:75-79]，固件也规定 `agent.*` 只从确认框发出 [源 module.h:49-51]；只读请求放在 `agent.*` 名下，两条都会被打破。docs/05 §6.10 预留的 `approval.request` / `approval.decision` 不用：审批和预设回复共用"拉一页选项、按编号选"这一种形状，这两个名字继续留着，v0.6 不占。

### 9.2 示例

```jsonl
{"v":0,"t":"choice.get","id":"d31","sid":"8f3a21c0"}
{"v":0,"t":"choice.sheet","id":"h32","re":"d31","sid":"8f3a21c0","tok":"k7Qm2xT9aB4c","kind":"permission","tier":"high","ttl_s":96,"obj_lines":1,"lines":["git push origin fix/login","Claude 说：跑测试并推送"],"opts":[{"n":1,"act":"allow","label":"允许这一次"},{"n":2,"act":"deny","label":"拒绝：先停下等我"},{"n":3,"act":"deny","label":"拒绝：换个办法"}]}
{"v":0,"t":"agent.pick","id":"d33","sid":"8f3a21c0","tok":"k7Qm2xT9aB4c","n":1}
{"v":0,"t":"agent.result","id":"h34","re":"d33","ok":true}
{"v":0,"t":"todo.list","id":"d41","view":"mine","max":4}
{"v":0,"t":"todo.items","id":"h42","re":"d41","n":7,"next":"m2","items":[{"ref":"20261021-3qzk1m7-0000c-0005.3f9a1c","text":"周五前给 Alice 发报价","st":"open","project":"cpa","day":1021}]}
{"v":0,"t":"todo.act","id":"d43","ref":"20261021-3qzk1m7-0000c-0005.3f9a1c","op":"done"}
{"v":0,"t":"ack","id":"h44","re":"d43"}
{"v":0,"t":"todo.list","id":"d45","view":"review","max":4}
{"v":0,"t":"todo.items","id":"h46","re":"d45","n":3,"items":[{"ref":"k3m9q2t-00042-0a1c.a1.7c20e1","text":"给 Alice 发报价单","st":"new","project":"cpa","day":1021,"meta":"客户同步 · 我 · 周五"}]}
{"v":0,"t":"todo.act","id":"d47","ref":"k3m9q2t-00042-0a1c.a1.7c20e1","op":"queue","project":"cpa"}
{"v":0,"t":"todo.act","id":"d49","ref":"k3m9q2t-00042-0a1c.a2.19bd04","op":"send","dest":"file.sb"}
```

权限页 `lines` 的前 `obj_lines` 行（1 或 2）就是对象本身（§5.2），设备照原样画，确认框里再画一遍；说明行在后面。

### 9.3 字段规则

| 字段 | 规则 |
|---|---|
| `tok` | 不透明，`^[A-Za-z0-9_-]{1,16}$`，设备原样带回，用一次就作废 |
| `kind` | `^[a-z]{1,12}$`，已知值 `reply`、`permission`、`plan`、`question`、`none`。设备遇到不认识的值，当成 `none` |
| `why`（`choice.sheet`） | 只在 `kind:"none"` 时出现，取值见 §3.2，`^[a-z_]{1,12}$` |
| `tier` | `low` / `high`，只在 `permission`、`plan`、`question` 页出现 |
| `ttl_s` | 0–255，hook 还会等多少秒 |
| `total` | 0–65535，只在 `plan` 页出现：计划共几行 |
| `obj_lines` | 0–2，只在 `permission` 页出现：`lines` 的前几行是要批准的对象 |
| `lines` | ≤3 项，每项 1–64 B UTF-8，不含控制字符（可以含 `"`，命令里常有） |
| `opts` | ≤6 项，`x-uniqueBy: n`；`n` 1–6；`act` 是 `^[a-z]{1,8}$`，已知值 `reply`、`allow`、`always`、`deny`、`answer`；`label` 1–36 B，不含控制字符。设备按 `act` 和 `tier` 决定确认框的接受键，`act` 不认识的项不显示 |
| `view` | `^[a-z]{1,8}$`，已知值 `mine`、`review` |
| `ref` | 不透明，`^[A-Za-z0-9_.-]{1,48}$`，cpad 生成，设备原样带回 |
| `todo.items[]` | `ref`、`text`（1–96 B）、`st` 必填；可选 `project`、`day`（MMDD，101–1231）、`meta`（≤32 B，cpad 拼好的"会议标题 · 负责人 · 截止"）。`st` 是 `^[a-z]{1,8}$`：mine 页取 open / done，review 页取 new / mine / queue / sent / skip |
| `op` | `^[a-z]{1,8}$`，已知值：mine 页 `done`、`undo`；review 页 `mine`、`queue`、`send`、`skip`。用模式不用枚举 |
| `dest` | 只和 `op:"send"` 一起出现，docs/14 的 `DEST_ID` 模式，≤24 字符 |
| `cursor` / `next` | 和 `rec.*` 相同：`^[A-Za-z0-9_-]{1,32}$`，不透明 [源 docs/13 §12.2] |

### 9.4 错误码（不新增）

| 情况 | 回什么 |
|---|---|
| sid 不在列表里 | `choice.get` → `error E_UNKNOWN_SESSION`；`agent.pick` → `agent.result E_UNKNOWN_SESSION` |
| 令牌不认识、用过、过期；session 状态变了；又跑完了一轮；hook 已经放手；预设改了 | `agent.result E_NOT_REPLYABLE` |
| `n` 超出范围；`agent.pick` 本身不合法 | `agent.result E_BAD_FIELD`。`hub/reduce.failure_reply()` 今天只对 `agent.spawn`、`agent.reply` 回 `agent.result` [源 reduce.py:207-226]，要加上 `agent.pick` |
| 预设回复 fork 了 | `agent.result E_REPLY_FORKED`（现有） |
| 同一个令牌的 pick 正在处理 | `agent.result E_BUSY` |
| `ref` 找不到；会议待办的原文变了（`sha6` 对不上） | `error E_UNKNOWN_ITEM` |
| 那一行刚被别处改过，或者文件在写 | `error E_BUSY` |
| `op` 对这个 `ref` 不适用，或者已经做了不同的决定 | `error E_BAD_FIELD`，`field:"op"` |
| `dest` 不认识或者用不了 | `error E_BAD_FIELD`，`field:"dest"` |
| `project` 没登记 | `error E_UNKNOWN_PROJECT` |
| 写文件失败 | `error E_INTERNAL`（`msg` 不带路径） |

### 9.5 行长 [算]

用 `json.dumps(ensure_ascii=False, separators=(",",":"))` 加换行实算；`id`、`re` 取 24 字符，数字取上限，可选字段全填。

| 行 | 怎么构造 | 字节 |
|---|---|---|
| `choice.sheet` 最坏 | 3 行说明各 64 个 `"`（转义后翻倍）、6 个选项标签各 36 个 `"`，`tok` 16、`kind`/`why` 12、`total`、`ttl_s`、`obj_lines` 全带 | **1290** |
| `choice.sheet`，中文 | 说明 3 × 21 个汉字、标签 6 × 12 个汉字 | 879 |
| `todo.items` 4 项最坏 | `text` 96 个 `"`、`meta` 32 个 `"`、`ref` 48 字符、`next` 32 字符 | **1699** |
| `todo.items` 4 项，中文 | — | 1179 |
| `todo.items` **5 项**最坏 | — | 2086（超了，所以每页最多 4 项） |
| `choice.get` / `agent.pick` / `todo.list` / `todo.act`（带 24 字符的 `dest`） | — | 74 / 105 / 126 / 187 |
| `agent.result` 带 `E_NOT_REPLYABLE` 和 16 字符的 `why` | — | 约 165，≤ `host_reply_max` 256 |
| `sessions.snapshot` 10 行最坏，**没有** `why` | 名字全是 32 个 `"` | 1998 |
| 同上，**有** `why` | — | 2188，`fit_list` 截成 9 行，`total` 仍是真实数 [源 snapshots.py:43-46] |
| 同上，名字是 32 B ASCII（`<key>/<slug>` 的常见情况） | — | 1868 |
| 设备 `hello` 多出的 `feats` | 8 个 12 字符的词 | 130（加进设备 `hello` 的最坏行，P6 重算） |
| **控制 socket** 的 `decide` 请求（v0.6.1，`json.dumps` 默认参数，非 ASCII 转成 `\uXXXX`） | permission：`obj` 128 个 `\`、`note` 64 B；plan：3 × 64 B 加 `total`；question：2 × 64 B 加 4 × 36 B 标签；`sid` 36、`tuid` 64、`tool` 64 | 706 / 716 / **888**，≤ `REQUEST_MAX` 1024 [源 control/server.py:27] |

- **接收缓冲**：`choice.sheet` 和 `todo.items` 都是对共享通道上请求的回复。这个通道同时只有一个在途，所以设备接收缓冲的最坏情况仍是 4096 + 5×256 + 2048 = 7424 B，小于 8192 B [源 rec_rules.py `LARGE_HOST_REPLIES`]。`rec_rules.LARGE_HOST_REPLIES` 加这两个类型；`APP_REQUESTS` 加 `choice.get`、`todo.list`、`todo.act`。`agent.pick` 不进 `APP_REQUESTS`：它走 agent 通道，回的是小的 `agent.result`。
- `decide` 请求的三种形状各自校验字段（permission 只许 `obj`、`note`；plan 只许 `lines`、`total`；question 只许 `lines`、`labels`），所以三种的最坏情况不会叠加。`test_decide_size.py` 守住 888 B。

### 9.6 新旧混用

| 组合 | 结果 |
|---|---|
| 新 cpad + 旧固件 | 旧固件忽略 `why`、`review` 和多出的 `caps`，不发新请求。旧固件永远不会 pick，所以 cpad 只在设备 `hello.feats` 里有 `choose` 时才让 hook 等，否则一律立即回 null（`reason:"old_fw"`） |
| 旧 cpad + 新固件 | 没有 `caps.choose`、`caps.todo_app`：Enter 走今天的打字路；待办应用显示"Mac 端 cpad 需要升级"，"给 AI"标签照常可用 |
| v0.6.0 cpad + v0.6 固件 | `caps.decide` 不出现或为 false；权限页是 `none/no_decide` |
| v0.6.1 cpad 但 `decide` 关 | `caps.decide:false`；同上 |

### 9.7 合并约束

- 固件的 `test_codec_vectors` 断言 schema 数等于 `kMsgTypeCount + 1`；daemon 的 `test_every_message_type_has_a_validator` 要求 schema 和 `codec.KNOWN_TYPES` 一一对应 [源 docs/14 §7.8]。
- 所以 6 个新 schema（P6）**必须和固件 codec 的登记（F6-0）同一批进 main**。
- 协议工具同批改：`check_vectors.EXEC_KINDS` 加 `agent.pick`，`outbox.item` 加一个 `kind:"agent.pick"` 的非法用例（`protocol/README.md` 第 4 条要求每个执行类 kind 都有）[源 check_vectors.py:61、README.md:37]；`flows.py` 的通道归类不用改（`choice.*`、`todo.*` 落在 misc，`agent.pick` 落在 agent）。
- 合并前先给 v0.4.1 及以前的向量拍一份 `fixtures/v041_cases.sha256`，`test_additive.py` 守住旧用例一字不改 [源 test_additive.py]。

## 10. 固件

v0.6.0 一次把设备端做完，包括审批要用的高风险确认框；v0.6.1 只在 HIL 发现问题时才动固件。

### 10.1 模块和组件

| 部件 | 做什么 |
|---|---|
| `modules/inbox`（改） | 行上显示原因；Enter：有 `caps.choose` 就发 `choice.get`，否则走今天的打字路；离线时打开"连上 Mac 再选"小页；选择页是"等你"的第二层；"会议待办 N 条"一行打开待办应用的"待确认" |
| `lib/ui/pick_list`（新，纯函数） | 带键名的选择列表：画行、处理数字和 `;` `.`、算 240 px 里放得下几行；**支持"没有选中项"**和开页 400 ms 的 Enter 防误触。v0.5 如果已经抽出了共享的列表组件，就在它上面加这两点 |
| `modules/todo`（新） | 待办应用的三个标签、逐条确认页（含跳过后 400 ms 防连按）、项目选择、去向选择。照 docs/16 的 App SDK 写，过 `expect_app_contract` 和 `check_apps.py` |
| `modules/home`（改） | "W 待办 N"放进第 4 行的空位；"最急的一条"带原因 |
| `lib/shell/confirm`（改） | `ConfirmKind::Pick`：按 `act` 和 `tier` 选 `Layer::Confirm` 或 `Layer::ConfirmHigh`；离线时不排队 |
| `lib/help/menu_model`（改） | ConfirmHigh 层的接受行只作说明（`ev.kind = None`），行尾写"要直接按 Y" |
| Runtime（改） | 共享的大回复解码区（`RecPage`、`ChoiceSheet`、`TodoPage` 共用一块静态内存）；`agent.pick` 的结果交给发起的应用；§3.6 的清除时机（链路离开 Ready、锁屏、`host_id` 变化） |

### 10.2 接口新增（只新增，都有默认值）

```cpp
// module.h
enum class RequestKind : uint8_t { QueueDone, InboxDismiss, RecList, RecAct, ChoiceGet, TodoList, TodoAct };
// agent.* 仍然不在这里：agent.pick 只从 confirm() 发出（module.h:49-51 的规则不变）
enum class ConfirmKind : uint8_t { Spawn, Feedback, Pick };
struct ConfirmSpec { /* 原有字段 */ Tok tok; uint8_t pick_n; PickAct act; Tier tier; };  // 对象和摘要放进原有的 text
struct ModuleResult { /* 原有字段 */ const ChoiceSheet* sheet; const TodoPage* todos; };
// domain.h
enum class SessionWhy : uint8_t { None, Permission, Plan, Question, Input, Login, Error };
enum class SheetKind : uint8_t { None, Reply, Permission, Plan, Question };
enum class PickAct : uint8_t { Unknown, Reply, Allow, Always, Deny, Answer };
enum class Tier : uint8_t { Low, High };
struct SessionRow { /* 原有字段 */ SessionWhy why; };
struct ChoiceSheet { Sid sid; Tok tok; SheetKind kind; NoneWhy why; Tier tier; uint8_t ttl_s; uint16_t total;
                     uint8_t obj_lines; uint8_t n_lines; SheetLine lines[3]; uint8_t n_opts; SheetOpt opts[6]; };
struct TodoRow { TodoRef ref; TodoText text; TodoSt st; Opt<ProjectKey> project; uint16_t day; Opt<TodoMeta> meta; };
struct TodoPage { uint8_t n; TodoRow rows[4]; uint16_t total; Opt<RecCursor> next; };
// app_state.h
struct LinkInfo { /* 原有字段 */ bool cap_choose; bool cap_decide; bool cap_todo_app; };
struct QueueData { /* 原有字段 */ uint16_t review; };
```

### 10.3 按键表新增

| 层 | 键 | 动作 |
|---|---|---|
| Global | Opt+W | `Action::Todos`，打开待办应用 |
| ConfirmHigh（新） | `y` | Confirm（菜单里只作说明，不重放） |
| ConfirmHigh | 返回键、`` ` `` | Cancel |
| ConfirmHigh | Tab | 菜单（和 Confirm 层一样） |

选择页的数字、逐条确认页的 T / Q / 1–9 都是应用自己处理的字符，不进全局表，由应用的 `help()` 交给菜单（docs/13 §6.2）。逐条确认页的 Opt+G、Opt+D 和 docs/14 的"等你"待处理一样由应用处理。全部新绑定要过 `test_keymap_static` 的幽灵键检查。

### 10.4 预算 [算，按结构体字段估，H6 实测]

| 项 | 增加 | 上限 |
|---|---|---|
| AppState | `SessionRow.why` 10 B + 能力位 1 B + `review` 2 B ≈ 13 B | 4096 B（v0.3.1 是 3488 B，v0.4.0 加了 4 B；v0.4.1 之后的余量以 V41-H 实测为准 [待]） |
| 等你应用对象 | `ChoiceSheet` 约 465 B | 每个应用 ≤1 KB |
| 待办应用对象 | 4 行 × 约 195 B + 游标和界面状态 ≈ 830 B | 每个应用 ≤1 KB |
| 大回复解码区（静态） | 取三者最大的，约 830 B | 静态 RAM 增长和空闲堆 ≥150 KB、最大连续块 ≥64 KB 一起测 |
| 堆 | 0：不用堆、不阻塞（docs/13 §10.3） | — |

## 11. cpad

### 11.1 新文件

| 文件 | 版本 | 内容 |
|---|---|---|
| `choices/presets.py` | v0.6.0 | `replies.json` 的解析、默认值、`when:"seen"`、读写 |
| `choices/sheet.py` | v0.6.0 | 纯函数 `build_sheet(tracked, pending, presets, caps, now) -> (ChoiceSheet, Binding)`；`none` 的原因码 |
| `choices/pick.py` | v0.6.0 | 纯函数 `check_pick(binding, tracked, presets_hash, now) -> None | code`（含 hook `at` 和摘录哈希） |
| `todos/mine.py` | v0.6.0 | 扫描、`ref` 的生成和解析；`tick(path, ref, done)`，按 §7.2 只写 1 个字节并核对 |
| `todos/review.py` | v0.6.0 | 纯函数：把 `rec.result`、`todo.*`、`dest.decided`、`queue.added(from_item=<rec>.a…)` 折叠成待确认列表和每场会的已处理条数；`load_actions(dir)` 读 `result.json` 并校验；`display_text()` |
| `todos/render.py` | v0.6.0 | 纯函数 `todo_entry(event)`：`todo.mine` 在每日笔记里的那一行（v0.4.1 的待办行格式） |
| `todos/service.py` | v0.6.0 | `Todos.on_device_request(msg) -> 回复`（照 v0.4.1 `Recordings` 的做法）；T、Q、Opt+G 调 `dest.router.on_capture()` |
| `cli/reply.py`、`cli/todo.py` | v0.6.0 | §11.5 |
| `agents/claude_code/brief.py` | v0.6.1 | 纯函数 `brief(tool, tool_input, cwd, root) -> Brief`、`brief_obj(text)`（不删字，做不到就 `unsupported`）、`redact(text)`、敏感清单（§5.2、§5.3） |
| `agents/claude_code/decide_io.py` | v0.6.1 | 纯函数：`request(doc, brief)` 组 hook → cpad 的请求；`parse_request(doc)` 是 daemon 侧的严格校验（按 kind 的字段白名单、长度、说明行等于 `excerpt()`、对象满足 `brief_obj()` 的规则、形状自检）；`output(answer, tool_input)` 组 hook 的输出（§5.5） |
| `agents/claude_code/decide_client.py` | v0.6.1 | hook 侧的两阶段客户端：连接 0.2 s、第一行 0.5 s、第二行 110 s（§5.1） |
| `choices/book.py` | v0.6.1 | 等待簿、令牌、去重（§5.4） |
| `cli/decide.py` | v0.6.1 | §11.5 |

### 11.2 改动的现有文件

| 文件 | 版本 | 改什么 |
|---|---|---|
| `agents/claude_code/hooks.py` | v0.6.0 | Stop 多转发 `tail`；`KEYS` 加 `tail` |
| `agents/claude_code/hook_state.py` | v0.6.0 | `merge_pairs()` 带上 `why`、`msg`、`tail`、`at` |
| `inbox/need.py` | v0.6.0 | `Tracked` 加 `why`、`msg`、`tail`、`hook_at` |
| `agents/claude_code/adapter.py` | v0.6.0 | fork 以后先 `stop` 副本，再回 `E_REPLY_FORKED`（§4.3） |
| `hub/{state,inputs,effects,reduce,snapshots}.py` | v0.6.0 | `choice.get`、`agent.pick`；`failure_reply()` 加 `agent.pick`；快照行的 `why` 和 `_order`；`queue.snapshot.review`；来自会议的排队项摘要加"会议·"、开 session 一律 `plan` |
| `recordings/service.py` | v0.6.0 | `_counted()` 的 `todo_n` 扣掉已处理的 |
| `notes/writer.py` | v0.6.0 | `_entry()` 认识 `todo.mine`；写成以后由 hub 记 `todo.written`；`backfill` 跳过已有 `todo.written` 的 `todo.mine` |
| `config/settings.py` | v0.6.0 / v0.6.1 | `experimental_bg_reply` 默认 `"auto"`、`todo_days`（默认 30）；v0.6.1 加 `decide`（默认 false） |
| `protocol/{specs,messages}.py` | v0.6.0 | 6 个新类型和新字段（属于 P6） |
| `app.py` | v0.6.0 / v0.6.1 | 把 `todo.*` 转交给 `Todos`；v0.6.1 接上 decide |
| `agents/claude_code/hook_settings.py` | v0.6.1 | 决定用的 handler：`PermissionRequest`，`args:["hook","--decide"]`，`timeout:120`；`install()` 读配置里的 `decide`；`verify()` 找出所有装了的地方 |
| `cli/hooks.py` | v0.6.1 | `--decide`：转发事件以后再走 §5.1 第 3–5 步；只输出 §5.5 的形状；任何异常都不输出，退出 0 |
| `control/server.py` | v0.6.1 | `decide` 命令先回第一行，再挂起连接，由 hub 回第二行 |

### 11.3 控制 socket 的 `decide`（v0.6.1）

```jsonc
// hook → cpad（只走 0600 的 cpad.sock）
{"cmd":"decide","sid":"<完整 session UUID>","tuid":"toolu_…","kind":"permission","tool":"Bash",
 "obj":"git push origin fix/login","note":"Claude 说：跑测试并推送","inside":false,
 "cwd_h":"<realpath(cwd) 的 sha256 前 16 个十六进制字符>","sub":false}
// cpad → hook，第一行（0.5 s 内）
{"ok":true,"held":true}                      // 或 {"ok":true,"pick":null}：回落到 Mac 对话框
// cpad → hook，第二行（只在 held 之后）
{"ok":true,"pick":{"act":"allow"}}           // 或 {"act":"deny","m":"stop"} / {"act":"answer","i":1} / {"act":"always"} / null
```

- `parse_request` 照 `hooks.parse()` 的规矩：按 kind 的字段白名单，多一个就拒绝；说明行必须等于 `excerpt(line)`；对象必须满足 `brief_obj()` 的规则；`labels` ≤4 × 36 B。改文件时 `inside` 表示对象是项目里的相对路径还是 `~/` 开头的绝对路径。
- 风险等级 `tier` 由 cpad 按 §5.3 算：登记表、列表里的 `cwd`、敏感清单都是 cpad 自己查的；`inside` 和 realpath 是 hook 报的事实（§5.3 末尾说明了为什么可以接受）。
- 请求不送绝对路径：`cwd` 只送哈希，cpad 用列表里的 `SessionInfo.cwd` 算同样的哈希来比。这样一行请求最坏 888 B（§9.5）。

### 11.4 事件（`events.jsonl`）

| 事件 | 字段 | 正文 |
|---|---|---|
| `agent.decided` | `sid, tool, tier, kind, act, n, obj_len, obj_sha8` | 不含 |
| `agent.decide_fallback` | `sid, tool, reason`（off / handler / version / shape / offline / old_fw / other_profile / not_bg / unregistered / cwd / unsupported / subagent / dup / timeout / replaced / dismissed / closed） | 不含 |
| `agent.replied` | 现有字段加 `preset?` | 不含 |
| `agent.reply_forked` | `sid, copy_stopped` | 不含 |
| `todo.mine` | `ref, rec, n, text, project, title, dir, at` | **含**（和 `item.received` 一样，是笔记的来源） |
| `todo.written` | `ref` | 不含 |
| `todo.skipped` | `ref` | 不含 |
| `todo.ticked` / `todo.tick_lost` | `ref, done` / `ref` | 不含 |
| `dest.decided`、`queue.added` | docs/14 的路由照常写，`from_item` 是会议待办的 `ref` | 和现在一样 |

### 11.5 命令行

| 命令 | 作用 |
|---|---|
| `cpa reply ls / add / rm / reset / status` | §4.1、§4.3 |
| `cpa todo ls [--done] [--days N]`、`cpa todo review` | 只读，排查用 |
| `cpa decide on [--yes]`（v0.6.1） | 写入决定用的 handler（只显示改动，加 `--yes` 才写，备份规则沿用 `hook_settings`），并设 `decide:true` |
| `cpa decide off` | 设 `decide:false`，把 handler 换回普通的 |
| `cpa decide status` | 开没开、handler 在哪几处、CLI 版本和验证过的版本、最近一次回落的原因 |
| `cpa decide log [--since]` | 列出 `agent.decided` 和 `agent.decide_fallback`，不含正文 |

## 12. 和本地 MCP（docs/15）的关系

- **勾掉的待办**直接写回每日笔记，`list_todos` 下一次就看得到。两边以同一份文件为准。
- **会议待办**：`list_todos` 返回 `summary.md` 里的条目时，加一个可选字段 `decided`（mine / queue / sent / skip / null），用 `todos.review` 的折叠函数算；`status=open` 不返回已处理的，免得和每日笔记里那一行重复，也免得一直显示"还有 3 条"。这是 docs/15 B 期的一处小改动，由 docs/15 那条线来做。
- **`get_status`** 可以加 `decisions_waiting`（只有数量）。可选。
- **MCP 永远不能做的事**：回答审批、选选项、开 session、回复 session、修改已有内容，包括勾待办（docs/15 §3.6"永远不会有的写入"）。`cpa mcp` 不转发 `cpad.sock`，只发一条写死的只读请求 [源 docs/15 §5]。原草稿里"以后加 MCP 勾掉待办写入工具"那句和 docs/15 §2.3、§3.6 矛盾，已删除。
- 审批摘要从不落盘，所以 MCP 读不到它们。
- **"cpad 只追加"的例外**：docs/15 §2.1、§2.3 和 docs/14 §2.3 都写着 cpad 对每日笔记只追加。v0.6 起有一个例外：在设备上勾掉或取消一个带 `^cpa-` 块 ID 的待办时，cpad 改那一行的 1 个字节。记在差异表里，docs/15 的那两处要跟着改一句。

## 13. 安全

**信任边界不变**：物理 USB 加上当前登录的 macOS 用户 [源 SECURITY.md]。

| 变化 | 版本 | 风险 | 怎么应对 |
|---|---|---|---|
| 设备能批准工具调用 | v0.6.1 | 拿到**没锁**的设备、又插在你的 Mac 上的人，能批准一条后台 session 正在等的命令。以前最多只能以 plan 模式开 session | 默认关，`cpa decide on` 才开；高风险要按 Y，菜单替代不了；开页不预选、400 ms 防误触；确认框再显示一遍对象；令牌只用一次；只管后台 session；每次决定都记 `agent.decided`；锁屏时看不到也选不了，选择页一锁屏就清掉。**锁屏不是 PIN 锁**（docs/05 §8 把审批和可选的 PIN 锁放在一起；v0.6.1 不做 PIN，§17 第 1 条），README 和 SECURITY.md 写明 |
| 看到的和执行的不一样 | v0.6.1 | 摘要清洗把命令改了样，你批准的不是会执行的 | 对象原样显示，只许 ASCII、单行、≤128 B、不命中脱敏；做不到就留在 Mac 上（§5.2）；hypothesis 测试守住"显示的等于执行的，或者拒绝" |
| "项目里"被当成低风险 | v0.6.1 | 项目根登记成 `$HOME`，或者改 `.git/hooks`、`.claude/settings.json` | §5.3 的五条，敏感清单进高风险 |
| cpad 卡住拖住终端 | v0.6.1 | 每个权限请求都要等 hook | 两阶段握手，cpad 不回第一行最多拖 0.7 s（§5.1） |
| 工具参数的摘要离开 hook 进程 | v0.6.1 | 命令或路径里可能有密钥 | 只在 decide 模式下；对象命中脱敏规则就不上设备，说明行先脱敏；只经 0600 的 `cpad.sock`；cpad 只放内存，日志和事件只有长度和哈希；设备只放内存、不写 SD；锁屏、断线、换 Mac 都清掉 |
| hook 开始往 stdout 输出 | v0.6.1 | 输出会进 Claude 的决策 | 只有 §5.5 的固定形状；字符串只来自常量或 hook 自己读到的 `tool_input`；cpad 只给 `act`、`i`、`m` |
| 自动允许 | — | — | 不存在：没有设备上的那一次 pick，cpad 永远不回 allow；任何异常都是"不输出" |
| "本 session 都允许" | v0.6.1 | 范围太大，盖住敏感文件 | 默认不出现；只有 G-D1 确认能用 `ask` 规则盖住敏感清单时才给；只限低风险、核对过的 session 目录（在登记的项目根里）、这一个 session，不写 settings 文件；高风险从不提供 |
| Stop 多转发结尾摘录 | v0.6.0 | 多了 ≤120 B 的模型输出离开 hook | 和现有的开头摘录同样清洗、同样只经 `cpad.sock`；`events.jsonl` 不记，只放内存 |
| 预设文字 | v0.6.0 | 被改成参数注入 | `replies.json` 严格校验、0600、`check_text()`；argv 照旧，正文放在 `--` 之后 |
| 预设回复 fork | v0.6.0 | 副本在同一个工作树里跟着跑 | fork 以后先停副本；默认只在验证过的 CLI 版本上开（§4.3） |
| 改每日笔记 | v0.6.0 | 写坏你的笔记，或者写到别处 | 只改带我们块 ID 的那一行的 1 个字节；`O_NOFOLLOW`、目录限定在 `notes_dir`；行哈希核对、改完再读回核对；和 cpad 的追加在同一个线程；从不删行；每次都记 `todo.ticked`，可以取消 |
| `summary.md` | v0.6.0 | 和 WhisperWoof 抢着写 | 不写（单写者表） |
| 会议里有人"说进"指令 | v0.6.0 | 待办文字是转写出来的 | 只当数据：进队列的只是你看到的那段；开 session 时显示"会议·"、一律 plan 模式；仍要你在设备上确认 [源 docs/11 §7.9]（§6.3） |
| 同用户下的恶意进程冒充 hook 或 cpad | — | 伪造审批请求，或者伪造回答 | 不在防护范围内（SECURITY.md）。冒充 cpad 最多让 hook 输出那几种形状；伪造请求只会让设备多一页，你不选就什么都不发生 |

**SECURITY.md 要改的地方**：
- v0.6.0（DOC6）：hooks 那一行加"Stop 还会转发结尾的 ≤120 B 摘录"；"每日笔记"加"在设备上勾待办时，cpad 改那一行的 1 个字节"。
- v0.6.1（DOC61）：hooks 那一行写明"开启 `cpa decide` 后，权限请求的短摘要会送到设备"；新增"设备审批"一行；"已知风险"加一条"锁屏不是 PIN 锁，开了设备审批以后，没锁的设备能批准命令"；docs/05 §8 的"v0.2：审批按风险分键"标成"v0.6.1 已做（默认关，没有 PIN 锁）"。

## 14. 测试（一律先写测试：RED → GREEN → REFACTOR）

### 14.1 daemon

| 测试文件（新） | 版本 | 先写的断言 |
|---|---|---|
| `tests/unit/test_hook_tail.py` | v0.6.0 | Stop 转发 `tail`，是结尾 ≤120 B；`parse()` 拒绝超长、不干净的 `tail` |
| `test_choices_sheet.py` | v0.6.0 | §3.2 的表逐行对照；`none` 的每个原因码（含 `elicit`、`quota`、`released`）；预设整组替换；没有 `tail` 时不给 `when:"seen"` 的预设；`lines` 截断不截半个字 |
| `test_choices_pick.py` | v0.6.0 | 令牌、状态、`since`、hook `at`、摘录哈希、预设哈希逐项不符时回对应的错误码；**两次轮询之间跑完一轮**（`since` 不变、`at` 变了）时拒绝 |
| `test_snapshot_order.py` | v0.6.0 | 要你决定的（权限、计划、提问）排在空闲前面；11 个 session 时被截掉的不是要你决定的 |
| `test_presets_config.py` | v0.6.0 | 往返；`version: 2` 拒绝；多一个字段（专门测 `token`）拒绝；`label` 含 `"`、超长拒绝；`text` 以 `-` 开头拒绝；`when` 只认 `seen`；文件 0600 |
| `test_reply_fork.py` | v0.6.0 | fork 的三个信号各自触发：先对副本 `stop`，再回 `E_REPLY_FORKED`；找不到副本时 `copy_stopped:false` |
| `test_reply_policy.py` | v0.6.0 | `"auto"`：同主次版本且不低于验证版本才开；更高的次版本关；常量为空时关 |
| `test_failure_reply_pick.py` | v0.6.0 | 不合法的 `agent.pick` 回 `agent.result E_BAD_FIELD`，不是 `error` |
| `test_todos_mine.py` | v0.6.0 | 扫描规则（含 `[X]`）；`ref` 往返；勾掉和取消前后文件只差 1 个字节；行哈希不符回 `E_BUSY`；在读和写之间模拟 Obsidian 整文件重写，读回核对发现后回 `E_BUSY` 并记 `todo.tick_lost`；符号链接拒绝；块 ID 出现两次时拒绝 |
| `test_todos_review.py` | v0.6.0 | 折叠；`ref` 带原文哈希，`result.json` 重新生成后旧决定不落到新条目上；第一个决定生效，跳过可以改；同一决定再发回 `ack`；T、Q、Opt+G 都经 `router.on_capture()`，写出 `dest.decided`；Q 的正文等于显示的那段；`queue_text()` 降级时回错误、条目仍待确认 |
| `test_todos_render.py` | v0.6.0 | `todo.mine` 那一行等于 v0.4.1 待办行格式；写成以后有 `todo.written`；**删掉那一行再启动，不会补回来**；事件记了、行没写成时，启动补一次 |
| `test_rec_todo_n.py` | v0.6.0 | `todo_n` 扣掉已处理的条数，最少 0 |
| `test_meeting_spawn.py` | v0.6.0 | 来自会议的排队项摘要以"会议·"开头；开 session 一律 `plan` |
| `test_brief.py` | v0.6.1 | 每种工具的对象、说明和风险等级；**hypothesis**：对任意命令，要么设备上的对象逐字节等于会执行的命令，要么 `unsupported`；专门的用例：`*`、`` ` ``、`# `、`\n`、零宽空格、全角字符、129 B 的命令、命中脱敏的命令；金丝雀：放进 `tool_input` 的 20 种密钥写法都不会出现在摘要里 |
| `test_tier.py` | v0.6.1 | 项目根是 `$HOME`、`/`、`~/.claude` 的上级时一律高风险；敏感清单上的每一项都是高风险；hook 的 `cwd` 哈希和列表对不上时 `unsupported` |
| `test_decide_io.py` | v0.6.1 | `parse_request` 按 kind 拒绝多余字段、超长、不干净的行；`output()` 对每种 `act` 产生的 JSON 等于 G-D1 抓到的形状；`answer` 的 `updatedInput` 保留原来的 `questions`；不认识的 `act` 不输出；形状自检 |
| `test_decide_client.py` | v0.6.1 | 假服务器接受连接但永远不回：hook 0.7 s 内退出、没有输出；第一行回 null：没有输出；回 held 后断开：没有输出 |
| `test_decide_size.py` | v0.6.1 | 三种 kind 的最坏请求都 ≤1024 B（888 B） |
| `test_hook_decide_cli.py` | v0.6.1 | `main(["hook","--decide"])`：cpad 不在时没有输出、退出 0、0.5 s 内结束；回 allow 时 stdout 只有那一行；stdin 超过 1 MiB 时排空、不输出 |
| `test_choices_book.py` | v0.6.1 | `hold`、`answer`、`expire`、`link_down`、`closed`、`dismissed`、同 sid 替换；同 `(sid, tuid)` 的第二个请求回 `dup`；令牌只用一次；函数不改原对象 |
| `test_hook_settings_decide.py` | v0.6.1 | `decide:true` 时 `hooks install` 装的是决定用的 handler；重跑 `install --yes` 后审批仍然开着；user 和 project 两份都装了时警告，一个对话框只拿一次 |
| `tests/integration/test_decide_e2e.py` | v0.6.1 | fake-claude 加真实 cpad，加 `cpa hook --decide` 读 G-D1 的 fixture，加一个假设备：pick 后 hook 输出 allow；设备断开后 1 s 内输出为空；Opt+D（`inbox.dismiss`）后立刻为空；超时后输出为空，并记 `decide_fallback`；设备 `hello.feats` 里没有 `choose` 时立即为空（`old_fw`）；**等待中的 session 在 11 个 session 里仍在快照上，`need=true`** |
| 安全金丝雀 | v0.6.1 | 摘要里放一个特殊字符串，跑完整个流程后，`events.jsonl`、日志、stderr 里都找不到它 |

覆盖率沿用 `--cov-fail-under=80`；`choices/`、`todos/`、`brief.py`、`decide_io.py` 的目标是 90% 以上（除了 IO 都是纯函数）。

### 14.2 协议和固件

| 测试 | 断言 |
|---|---|
| `protocol/tools/test_v06_worst_lines.py` | §9.5 每一行：schema 合法，字节数和表里一致，≤2048 B；`todo.items` 5 项超限；设备 `hello` 带 `feats` 的最坏行 |
| `test_additive.py` 加 `v041_cases.sha256` | 旧向量一字不改 |
| `check_vectors.py` | `agent.pick` 在 `EXEC_KINDS` 里，`outbox.item` 有 `kind:"agent.pick"` 的非法用例 |
| 固件 `test_codec_vectors` | 6 个新类型都能解码，并且往返相等 |
| 固件 `test_choice_sheet`（新） | 开页没有选中项；开页 400 ms 内 Enter 无效；没有选中项时 Enter 无效；数字直接选；`;` `.` Enter；`act` 不认识的项不显示；高风险的 allow 进 ConfirmHigh，Enter 无效、Y 有效；离线时 pick 不排队 |
| 固件 `test_menu_model` / `test_menu_dispatch` | ConfirmHigh 上的"本页"里接受行 `ev.kind == None`；对它按 Enter 不产生 Confirm |
| 固件 `test_inbox_why`（新） | 原因的写法；没有 `caps.choose` 时 Enter 走老路；离线时 Enter 打开"连上 Mac 再选"；`none` 页最后一行是"打字留言（排队）…" |
| 固件 `test_sheet_clear`（新） | 链路离开 Ready、锁屏、`host_id` 变化、超过 120 s 时，选择页和待办页都被清掉 |
| 固件 `test_todo_logic`（新） | 三个标签；逐条确认按 T / Q / 1–9 / Opt+G / Opt+D；跳过后 400 ms 内的按键无效；`E_BUSY` 时提示"没改成，已刷新"并刷新；翻页 |
| 固件 `test_mod_todo`（新，docs/16） | `expect_app_contract<TodoModule>()` |
| 固件 `test_keymap_static` | Opt+W 和 ConfirmHigh 层不产生幽灵键，不和别的层冲突 |
| 设备测试和 `cpa-sim shots` | §8 每个页面画一遍，`missing_glyphs == 0`，每行放得进 240 px |
| `check_apps.py`（docs/16） | 待办应用通过 |

### 14.3 HIL 和人工

| 场景 | 版本 | 在哪跑 | 看什么 |
|---|---|---|---|
| `tools/hil/scenarios/choose.py`（新） | v0.6.0 | `cpa-sim serve` 和真机 | 假主机下发各种 `choice.sheet`（包括 permission、plan、question）→ 按数字 → 确认 → `agent.pick`；高风险要按 Y，菜单按不了；开页连按 Enter 什么都不发 |
| `todo.py`（新） | v0.6.0 | 同上，临时的 `notes_dir` | 列出、勾掉、取消；在两步之间改文件，得到 `E_BUSY` |
| `review.py`（新） | v0.6.0 | 同上，构造的 `result.json` | 一条一条：T、Q（换项目）、Opt+G、跳过；`summary.md` 前后逐字节相同；`cpa audit` 里看得到 |
| 旧 cpad / 旧固件 | v0.6.0 | 同上 | §9.6 的每一行 |
| `decide.py`（新） | v0.6.1 | `cpa-sim serve` + 真实 cpad + fake-claude + hook fixture | 端到端：选了以后 hook 输出 allow；拔线以后回落；Opt+D 以后回落 |
| 👤 G3′ | v0.6.0 | 你的终端 | §4.3 |
| 👤 G-D1 | v0.6.1 | 你的终端 | §5.7 |
| 👤 真机一轮（约 15 分钟） | v0.6.0 | 真机加真实 Claude Code | 用预设回复一次；会议待办确认 3 条（T、Q、跳过）；Obsidian 开着当天的笔记时勾掉一条待办。**全程不打字** |
| 👤 真机一轮（约 15 分钟） | v0.6.1 | 同上 | 后台 session 在临时目录里：批准一次计划；允许一次改文件；用 Y 允许一条无害命令；答一次提问。**全程不打字** |

## 15. 工作包

**v0.6.0**：

| 编号 | 内容 | owns_paths | 依赖 | 验收 |
|---|---|---|---|---|
| **G6** | 闸门 G3′；记下验证过的 CLI 版本；抓 Stop hook 的 fixture | `docs/gates/v06-gates.md`、`tools/gates/g3p.sh`、`daemon/tests/fixtures/hooks/stop-2.1.306-*.json` | 无 | G3′ 有实测结论；`VERIFIED_CLI["reply"]` 的值写进闸门记录 |
| **P6** | 6 个 schema、新字段、向量、README、行长、协议工具 | `protocol/schema/v0/{choice.get,choice.sheet,agent.pick,todo.list,todo.items,todo.act}.schema.json`（新）、`protocol/schema/v0/{common,sessions.snapshot,queue.snapshot,hello}.schema.json`、`protocol/vectors/v0/messages/{上面 6 个}.json`（新）、`protocol/vectors/v0/messages/outbox.item.json`（加非法用例）、`protocol/vectors/v0/flows/app/*`、`protocol/tools/{rec_rules,check_vectors,test_v06_worst_lines}.py`、`protocol/tools/fixtures/v041_cases.sha256`、`protocol/README.md`、`daemon/src/cpad/protocol/{specs,messages}.py` | 无 | `pytest protocol/tools`；`check_vectors.py`；daemon 全部测试绿；**和 F6-0 同批合并** |
| **F6-0** | 固件共享接口和 codec 登记；按键表新增；菜单的只作说明行 | `firmware/app/lib/protocol/*`、`firmware/app/lib/module_api/module.h`、`firmware/app/lib/core/{domain,app_state}.h`、`firmware/app/lib/input/{keymap.h,keymap.cpp}`、`firmware/app/lib/help/{action_help,menu_model}.cpp`、`firmware/app/test/core/{test_codec_vectors,test_keymap_static,test_menu_model}` | P6 | `pio test -e native` 全绿；`static_assert(sizeof(AppState) <= 4096)`；头文件从这里起冻结（docs/13 §17） |
| **D6-C** | 原因、选择页、预设、fork 停副本、快照顺序 | `daemon/src/cpad/choices/{presets,sheet,pick}.py`（新）、`cli/reply.py`（新）、`agents/claude_code/{hooks,hook_state,adapter}.py`、`inbox/need.py`、`hub/{state,inputs,effects,reduce,snapshots}.py`、`config/settings.py`（reply 那几行）、`app.py`（choice 那几行）、对应的 `tests/unit/*` | P6、G6 | §14.1 的 v0.6.0 选择页、预设、fork、顺序测试 |
| **D6-T** | 待办：我的、确认（经路由）、渲染、服务、CLI、计数 | `daemon/src/cpad/todos/*`（新）、`cli/todo.py`（新）、`notes/writer.py`、`recordings/service.py`（`_counted` 一处）、`app.py`（`todo.*` 那几行）、`hub/snapshots.py`（只加 `review`，在 D6-C 之后合并）、对应的 `tests/unit/*` | P6；docs/14 的 `dest/router.py` 已合入 | §14.1 的 todos 测试；`summary.md` 前后逐字节相同 |
| **F6-I** | "等你"的原因、选择页、Pick 确认框、离线小页、清除时机 | `firmware/app/modules/inbox/*`、`firmware/app/lib/ui/pick_list.*`（新）、`firmware/app/lib/shell/{confirm,runtime,shell}.*` | F6-0 | `test_choice_sheet`、`test_inbox_why`、`test_sheet_clear`；v0.1–v0.5 回归 |
| **F6-T** | 待办应用、主页那一格 | `firmware/app/modules/todo/*`（新）、`firmware/app/modules/home/*`、`firmware/app/src/module_list.cpp` | F6-0；docs/16 的 App SDK 已合入 | `test_todo_logic`、`test_mod_todo`；`check_apps.py`；主页测试；`missing_glyphs == 0` |
| **H6** | 集成、HIL、人工验收 | `tools/hil/scenarios/{choose,todo,review}.py`（新）、`docs/07-hil-checklist.md`、`docs/releases/v0.6.0.md` | 以上全部 | §14.3 的 v0.6.0 各行全过；你的真机一轮全程不打字 |
| **DOC6** | 文档 | `SECURITY.md`、`README.md`、`docs/04-prd.md` §8（按键表）、`docs/14-destinations.md` §2.3 和 `docs/15-local-data-access.md` §2.1、§2.3（"只追加"的例外）、`CHANGELOG.md` | 无（发布说明在 H6 之后定稿） | §13 列的 v0.6.0 改动都在 |

**v0.6.1**（v0.6.0 发布以后）：

| 编号 | 内容 | owns_paths | 依赖 | 验收 |
|---|---|---|---|---|
| **G61** | 闸门 G-D1；抓 fixture | `docs/gates/v061-gates.md`、`tools/gates/g_d1.sh`、`daemon/tests/fixtures/hooks/permission-request-2.1.306-*.json` | v0.6.0 | §5.7 每一项都有实测结论；不成立的项按"不成立时"一栏改设计；a、b 不成立就停在这里 |
| **D61-H** | decide hook：摘要、对象、风险、两阶段客户端、输出、settings、`cpa decide` | `daemon/src/cpad/agents/claude_code/{brief,decide_io,decide_client}.py`（新）、`agents/claude_code/hook_settings.py`、`cli/hooks.py`、`cli/decide.py`（新）、`config/settings.py`（decide 那几行）、对应的 `tests/unit/*` | G61 | §14.1 的 v0.6.1 hook 侧测试加金丝雀；`decide` 关时行为和 v0.6.0 逐字节相同（现有 hook 测试不改） |
| **D61-C** | 等待簿、hub 接线、等待簿是真相、Opt+D 放手、去重 | `daemon/src/cpad/choices/book.py`（新）、`choices/sheet.py`（审批的三种页）、`hub/{state,inputs,effects,reduce,snapshots}.py`、`control/server.py`、`app.py`（decide 那几行） | D61-H | `test_choices_book.py`、`test_decide_e2e.py` |
| **H61** | 集成、HIL、人工验收 | `tools/hil/scenarios/decide.py`（新）、`docs/07-hil-checklist.md`、`docs/releases/v0.6.1.md` | 以上全部 | §14.3 的 v0.6.1 各行全过 |
| **DOC61** | 文档 | `SECURITY.md`、`README.md`、`docs/05-architecture.md` §8（标注）、`CHANGELOG.md` | 无 | §13 列的 v0.6.1 改动都在 |

```
v0.6.0：
G6 ──► D6-C
P6 ══ F6-0（同批）──► F6-I、F6-T
P6 ──► D6-C ──► D6-T（snapshots.py 跟在后面）
docs/16 App SDK ──► F6-T；docs/14 router ──► D6-T
全部 ──► H6（发 v0.6.0）；DOC6 并行
v0.6.1：
G61 ──► D61-H ──► D61-C ──► H61（发 v0.6.1）；DOC61 并行
```

**合并约定**：P6 和 F6-0 同批（§9.7）。D6-C 和 D6-T 都改 `hub/snapshots.py` 和 `app.py`，D6-C 先合并，D6-T 跟上。原草稿里的 T6-A（`check_apps.py`、`new_app.py`）删掉了：docs/16 已经在做 `tools/apps/` 下的同一套东西，本版只依赖它。

## 16. 风险

| 风险 | 怎么应对 |
|---|---|
| hook 的契约和设想的不一样（输出形状、超时、和对话框的先后） | 审批放进 v0.6.1，G-D1 是它的第一件；a、b 两项不成立就不发，v0.6.0 照常；输出形状用 fixture 固定 |
| CLI 升级后行为变了 | 只认验证过的主次版本、不低于验证版本；每个请求做形状自检；`cpa decide status` 提示；回落永远是"Mac 上的对话框" |
| 你没看到，hook 一直挂着，session 被拖住 | 只在设备在线、后台 session、当前档案归这台 Mac 时才挂；最多 100 s；设备上有倒计时；Opt+D 立刻放手；cpad 卡住最多拖 0.7 s |
| 摘要漏出密钥 | 对象命中脱敏就不上设备；说明行脱敏加金丝雀测试；不落盘；锁屏不显示；默认关 |
| 在设备上批准了不该批准的 | 对象原样显示；高风险要按 Y、菜单替代不了；开页不预选；令牌绑定看到的内容；敏感文件算高风险；批准计划不给"自动接受修改" |
| 预设回复 fork | 现有的三个信号；先停副本；G3′；只在验证过的 CLI 版本上默认开 |
| 预设回复送到"它刚又说了一轮"的 session | 令牌绑 hook 的 `at` 和摘录哈希（§4.2） |
| Obsidian 和设备同时改同一个文件 | 只写 1 个字节，写前核对行哈希、写后读回核对；`E_BUSY` 后刷新；H6 人工验证 [待] |
| `summary.md` 和每日笔记里各有一份 | `todo_n` 和 `list_todos` 都扣掉已处理的（§6.1、§12）；`summary.md` 不碰 |
| 你在 Obsidian 里删掉的待办又回来 | `todo.written` 让重放跳过已经写过的（§6.1） |
| 6 个新 schema 让固件 CI 变红 | P6 和 F6-0 同批（§9.7） |
| 快照加了 `why` 以后，最坏情况只放得下 9 行 | `fit_list` 本来就会截，`total` 是真实数；常见名字放得下 10 行（§9.5）；要你决定的排在前面，不会被截掉 |
| 应用对象接近 1 KB | 每页 4 项；`TodoRow` 字段都有上限；H6 实测 [待] |
| 新字形不在字库里 | 用 ASCII 代替；`missing_glyphs == 0` |
| 数字在选择页里是"第几项"，别处是"项目" | 只在选择页里；提示栏和菜单写明；逐条确认页里数字仍是项目 |
| v0.5 的共享组件或 docs/16 的 App SDK 还没合入 | `lib/ui/pick_list` 先放在本版，v0.5 做了就换；F6-T 等 docs/16 |

## 17. 需要你拍板

你不回复，就按默认值做。

1. **设备审批放进 v0.6.1**：v0.6.0 先发原因、预设回复、会议待办和待办应用；审批在 G-D1 通过以后单独发 v0.6.1，只改 Mac 端；默认关，要你在 Mac 上运行 `cpa decide on` 才开；只管后台 session；**不带 PIN 锁**，锁屏不是安全锁这件事写进 SECURITY.md。**默认：同意**。备选：放进 v0.6.0 一起发；或者推到 v0.7，和一个可选的 PIN 锁一起做（docs/05 §8 原来是这样配对的）。
2. **选择页用数字 1–6 直接选**（#16 要的就是编号预设）。这是"数字 = 项目"的唯一例外，只在选择页里有效；开页没有选中项，Enter 要先选。**默认：同意**。备选：只用 `;` `.` 加 Enter。
3. **预设回复默认可用**：G3′ 通过以后，`experimental_bg_reply` 默认 `"auto"`，只在 CLI 的主次版本和验证过的一样、并且不低于它时才开。**默认：同意**。备选：一直默认关，你在 `config.json` 里自己开。
4. **待办应用用 Opt+W**，"W 待办"放进主页第 4 行的空位；Opt+T 仍是"记一条待办"。**默认：同意**。备选：Opt+T 打开待办应用，打字记待办要按两下。
5. **待办怎么写、怎么勾**：
   - 会议里确认成"我的"的，写进**确认那天**的每日笔记；
   - 勾掉就直接把那一行的 `[ ]` 改成 `[x]`，只改 1 个字节，带冲突检查；
   - 只列带 `^cpa-` 块 ID 的行。

   **默认：同意**。备选：写进开会那天的笔记；也列你在 Obsidian 里手写的复选框（按行号加哈希定位，文件改得多时更容易对不上）。
6. **语音回复放 v0.7**：按住 G0 说完，在设备上看转写再确认；按 #34 走录音契约 v1，不做 WhisperWoof 专用的路。**默认：同意**。备选：提前到 v0.6，代价是要先有 v0.5 的"待处理"和转写回传。
7. **会议待办可以送到任意去向（Opt+G）**：docs/14 §1.2 把这件事留给了 v0.6，本版和 T、Q 一起走去向路由。**默认：同意，在 v0.6.0 做**。备选：v0.6.0 只做 T 和 Q，Opt+G 以后再加（写进差异表）。

## 附：和已有文档相比定下来的差异

| 原来 | 这里 | 为什么 |
|---|---|---|
| docs/13 §14、§15：v0.6 没有审批，"审批分级"在待办池 | v0.6.1 做设备审批，默认关，排在 v0.6.0 之后、要先过 G-D1 | #16 的"等你"里最常见的是等批准；拆成 v0.6.1，v0.6.0 不被 hook 契约拖住（§17 第 1 条） |
| docs/05 §8：审批按风险分键，和可选的 PIN 锁放在一起；v0.2 做 | v0.6.1，默认关，不带 PIN 锁 | 先要 hooks（#33）和 G-D1；PIN 锁进待办池，SECURITY.md 写明锁屏不是安全锁 |
| docs/05 §6.10：预留 `approval.request` / `approval.decision` | 用 `choice.get` / `choice.sheet` / `agent.pick`；`approval.*` 继续保留、不占 | 审批和预设回复是同一种"拉一页、按编号选"；只读请求不能放在 `agent.*` 名下（§9.1） |
| docs/13 原则 4：菜单行就是替你按那个键 | ConfirmHigh 上的"Y 允许"只作说明，不重放 | 否则 Tab + Enter 就绕过了"高风险只认 Y" |
| docs/13 原则 2：一个字母只代表一件东西 | "待办"占了两个字母：T 记一条，W 打开应用 | docs/14 §3.2 把 Opt+T 定成"记一条待办"，打开应用只能另用一个字母 |
| docs/13 §4.1：待办应用"主页按 T" | Opt+W，主页格子 W，放在第 4 行的空位 | 主页格子只重放按键（docs/13 §6.2），而 Opt+T 是记一条待办 |
| docs/13 §10.4、§15：v0.6 做 `check_apps` 和 `new_app` | 归 docs/16（App SDK，`tools/apps/`），本版只依赖 | docs/16 已经在做同一套东西 |
| docs/13 §10.4：v0.7 才有主机驱动的 `ui.list` / `ui.pick` | v0.6 先做专用的选择页 | 审批要绑令牌、要走 agent 通道、要按风险分键；通用的 `ui.*` 到 v0.7 再从这里抽 |
| docs/11 §18：`reply.choices` 放在 cpad.sock v1 | 走设备协议 v0 | 选的人在设备上；cpad.sock v1 仍是 v0.7 |
| docs/14 §1.2：会议纪要里的待办逐条外发在 v0.6 | 在 v0.6.0 做：T、Q、Opt+G 都走去向路由 | 一份清洗规则、一份审计（§6.1） |
| docs/14 §2.3、docs/15 §2.1、§2.3：cpad 对每日笔记只追加 | 勾待办时改那一行的 1 个字节 | 待办以文件为准，Obsidian 里要看到勾（§7.2） |
| SECURITY.md：工具参数从不离开 hook 进程 | `cpa decide on` 之后，脱敏的短摘要和原样的对象会离开 | 不看命令就批准更危险 |
| docs/11 §1.2：AI 待办要先在设备上确认才进队列 | 逐条确认页就是这个确认；不另弹确认框 | 进队列不等于执行 |

## 附：证据索引

- **issue**：#16（编号预设、Mac 上按项目配、自由文字靠语音、去掉打字 CER 检查）；#33（hooks）；#34（不做 app 专用集成，WhisperWoof 的 W 不再需要）；#19（情境档案，见 docs/18）。
- **daemon（`wt-design`，即 main）**：
  - `agents/claude_code/hooks.py:1-8、21-34、48-63、115-128`：白名单、`MARKUP` / `HEADING`（删 `` ` ``、`*`、`# `）、`excerpt()`、`parse()`；
  - `hook_state.py:1-13、25-26、83-86、148-154、175-178`：原因、`NEEDS_GRACE_S`、elicitation 和 quota、`_lag_s`、`merge_pairs()` 丢掉 `why`；
  - `hook_settings.py:23-28、78-83、119-127`：handler 超时 5 s、`PermissionRequest` 的 matcher 是 `*`、`is_ours()`、`install()` 先全删再装；
  - `adapter.py:127-153`：reply 和 fork 检测（不停副本）；`argv.py:22-27、51-52`：`check_text`、`resume_argv`；
  - `inbox/need.py:1-7、30-33、37-47`：`since` 的含义、等你的规则；`hub/snapshots.py:43-46、57-59、62-80`：`fit_list`、`_order`、会话行；`hub/reduce.py:141-160、207-226`：`_spawn_text`、`failure_reply`；
  - `agents/base.py:18-33`：`SessionInfo.cwd`；`control/client.py:14-29`、`control/server.py:27、88-90`：一个超时、`REQUEST_MAX = 1024`；
  - `projects/registry.py:67-68`：项目根只要求是绝对路径；`DEFAULT_MODE = "plan"`；`dump_registry()` 只写已知字段；
  - `cli/hooks.py:1-8`：从不输出、永远退出 0；`config/settings.py:58-60`：`inbox_idle_s`、`experimental_bg_reply`。
- **v0.4.1 工作树（`wt-v041-daemon`）**：`notes/render.py:30-50`（待办复选框行）；`notes/writer.py:55-71`（按块 ID 幂等追加）；`app.py:113-122`（启动时重放整个历史）；`recordings/service.py:235-241`（`todo_n` 来自 `summary.md`）；`protocol/schema/v0/{rec.items,hello,common,queue.snapshot}.schema.json`；`protocol/README.md` 第 4 条（执行类 kind 的非法用例）、第 213 行（`rec.items` 是唯一的大回复，7424 B）；`tools/{rec_rules,flows,check_vectors,test_additive}.py`（`LARGE_HOST_REPLIES`、`APP_REQUESTS`、`_request_kind`、`EXEC_KINDS`）。
- **v0.5 工作树（`wt-destinations`）**：`dest/router.py:100-104`（`queue_text`）、`135-137`（`on_capture`）；docs/14 §1.2、§2.3、§3.1、§3.2、§5、§10.1、§10.3。
- **docs/16 工作树（`wt-app-sdk`，进行中）**：§4.1（应用格子在九宫格下面一行）、§4.2（W 是第一方保留字母）、§7（`check_apps.py`）、§10（模块列表 8 个位置）、§11（第一方应用）。
- **固件（`release/v0.4.0`）**：`lib/input/keymap.cpp:43-122`；`lib/module_api/module.h:31-55、89-121`；`lib/core/app_state.h:99-113`；`lib/core/domain.h:67-91`；`modules/inbox/*`；`modules/home/home_logic.h:23-33`（v0.4.0 的 3×3 格子）；`lib/shell/confirm.cpp:27-35、55-61`；`lib/help/menu_model.cpp:70、134`；`lib/shell/shell_menu.cpp:138-146`；`test/core/test_menu_model`、`test/ui/test_menu_dispatch`；`src/module_list.cpp`；`firmware/sim/README.md`（`cpa-sim shots`、`serve`）。
- **协议**：`protocol/limits.json`（`line_max` 2048、`host_reply_max` 256、`device_rx_buffer` 8192、两个在途上限、`text_max` 500、可打字符 32–126）；`schema/v0/common.schema.json`（16 个错误码、`agentText`）；`agent.reply`、`agent.result`、`sessions.snapshot`、`inbox.dismiss` 的 schema。
- **文档**：docs/05 §6.10、§7.5、§8；docs/11 §1.2、§6.6–§6.8、§7.5–§7.9、§18；docs/13 §3、§4.1、§5.2、§5.6、§6.1–§6.2、§7.1、§10、§12.2、§14、§15、§19；docs/15 §2.1、§2.3、§3.4、§3.6、§5；gates/cli-gates.md §0、§1、§4、§5a、§7；SECURITY.md。
- **同类产品**（docs/02 §3、§4、§7）：sohsatoh/cardputer-cli-buddy（PermissionRequest hook 和终端对话框谁先答算谁的；AskUserQuestion 2–4 个选项；ExitPlanMode 没在设备上处理；密钥只走 USB）；haochen2115/cli-buddy（用 PermissionRequest 不用 PreToolUse，出错回落到对话框）；loml13/cardputer-claude-mcp（按风险分键：普通 Enter、危险 Y；设备不在就回落到终端；回复带 HMAC 签名）；Hardware Buddy（只有"允许一次"和"拒绝"）；yuk1-kondo/cardputer-agent-deck（Fn+1..6 预设命令）；session-hub（远程允许 / 拒绝，先答的算，出错回落）。
- **行长**：§9.5 的数字由 `json.dumps(..., ensure_ascii=False, separators=(",", ":"))` 加换行实算（控制 socket 那一行用 `json.dumps` 的默认参数，和 `control/client.py` 一致），脚本在 scratchpad 的 `v06calc/rev.py` 里，不进库；P6 的 `test_v06_worst_lines.py` 和 D61-H 的 `test_decide_size.py` 会照同样的构造重算。

## 附：评审意见怎么处理的

评审一共提了 8 条高、15 条中、13 条低，外加"补测试"一节。逐条核对过代码（`excerpt()`、菜单重放、`NEEDS_GRACE_S`、`_order`、`client.request`、`_stop_and_resume`、`since`、确认框防误触、`module.h`、`flows.py`、`EXEC_KINDS`、`failure_reply`、`hooks install`、`REQUEST_MAX`、`NotesWriter.backfill`、`todo_n`），全部成立，全部采纳；L10 部分采纳。

| 评审意见 | 处理 |
|---|---|
| **H1** 设备上的命令摘要不是会执行的命令（`excerpt()` 删 `*`、`` ` ``、`# `，只给第一行，`***` 能藏东西） | **采纳**。新原则 8"看到的就是要执行的"；对象改用不删字的 `brief_obj()`：只许 ASCII 可打印、单行、≤128 B、不命中脱敏，做不到就 `unsupported`；命令在前、`description` 标"Claude 说"；网址给全；MCP 一律留在 Mac 上；hypothesis 测试加专门用例（§5.2、§14.1 `test_brief.py`） |
| **H2** Tab 菜单绕过"只认 Y" | **采纳**。ConfirmHigh 的接受行只作说明（`ev.kind = None`），记为 docs/13 原则 4 的例外；`test_menu_model` / `test_menu_dispatch` 加断言（§3.4、§10.1、差异表） |
| **H3** "项目里改文件"算低风险、"本 session 都允许"可能盖住整个家目录 | **采纳**。低风险的五条：在 `cwd` 里且 `cwd` 在登记的项目根里、项目根不是 `$HOME` / `/` / 敏感目录的上级、不在敏感清单上、hook 的 `cwd` 和列表对得上；"本 session 都允许"默认不出现，只有 G-D1 i 确认绝对路径写法和 `ask` 规则能盖住敏感清单时才给，范围取核对过的 `cwd`，和判低风险的目录一致（§5.3、§5.6、§5.7 i） |
| **H4** 被拿着的请求从"等你"里掉出去；最新的请求最先被截掉 | **采纳**。等待簿是真相：拿着时这一行一律 `need=true`、`why` 取等待簿的；`_order` 按原因排，权限、计划、提问在空闲前面；两条都有测试（§3.1、§14.1） |
| **H5** cpad 卡住会拖住所有权限请求，包括终端里的 | **采纳**。两阶段握手：0.2 s 连接、0.5 s 内必须回 `held` 或 null，否则 hook 静默退出；假服务器"接受连接但不回"的测试；G-D1 加 k 行（后台 session 被 `claude attach` 打开时）（§5.1、§5.7、§14.1 `test_decide_client.py`） |
| **H6** fork 出的副本没人停 | **采纳**。fork 以后先 `stop` 副本再回 `E_REPLY_FORKED`，记 `copy_stopped`；默认只在 G3′ 验过的主次版本上开（§4.3、§17 第 3 条） |
| **H7** 令牌看不出两次轮询之间跑完的一轮；"它说"显示的是开头 | **采纳**。令牌再绑 hook 的 `at` 和摘录哈希；Stop 新增结尾摘录 `tail` 并用它显示；没有 `tail` 时不给 `when:"seen"` 的预设（如"看起来不错"）（§3.1、§4.1、§4.2） |
| **H8** 连按三下 Enter 就批准了 | **采纳**。开页没有选中项，Enter 要先选；开页 400 ms 内的 Enter 无效；有测试（§3.3、§10.1、§14.2） |
| **M1** `agent.sheet` 打破"`agent.*` 只从确认框发出"和协议工具；`agent.pick` 不在 `EXEC_KINDS`；`failure_reply` 不认它；没提 docs/05 §6.10 | **采纳**。只读的那一对改名 `choice.get` / `choice.sheet`（落在共享通道）；`agent.pick` 进 `EXEC_KINDS`、加 outbox 非法用例、进 `failure_reply`；docs/05 §6.10 的 `approval.*` 写进差异表（§9.1、§9.4、§9.7） |
| **M2** 审批和 docs/13 路线图矛盾，PIN 锁没交代 | **采纳**。默认改成 v0.6.0 先发预设和待办，审批在 G-D1 之后作为 v0.6.1；PIN 锁的推迟写进 §13、§17 第 1 条和差异表 |
| **M3** T6-A 和 docs/16 重复；主页第 4 行占了 docs/16 的位置 | **采纳**。删掉 T6-A，F6-T 依赖 docs/16；W 放进 v0.4.1 第一方格子第 4 行的空位，不新增一行（§7.4、§15） |
| **M4** "以后加 MCP 勾待办"和 docs/15 矛盾；原地改笔记没进差异表 | **采纳**。删掉那句；"只追加"的例外写进 §12 和差异表，DOC6 改 docs/14、docs/15 的对应句子 |
| **M5** 和 docs/14 矛盾：逐条外发被推迟、T 另起一套、清洗规则两份 | **采纳**。T、Q、Opt+G 都走 `router.on_capture()`（`origin:"confirmed"`、`how:"key"/"pick"`），清洗用 `queue_text()`，待办行用 v0.4.1 的格式；Opt+G 让 docs/14 §1.2 的"逐条外发"在 v0.6.0 兑现，列为 §17 第 7 条（§6.1、§6.2） |
| **M6** 删掉的待办重启后又回来 | **采纳**。写成以后记 `todo.written`，重放跳过；"删掉、重启、仍然没有"的测试。普通笔记行的同一问题是现有行为，进待办池（§6.1、§14.1） |
| **M7** 勾待办整文件重写；"4 个字节"写错；漏了 `[X]` | **采纳**。`O_NOFOLLOW` 打开、核对行哈希、`pwrite` 1 个字节、`fsync`、读回核对，和 `NotesWriter` 同一个线程；改成 1 个字节；匹配 `[X]`（§7.2、§13） |
| **M8** 计数不会减 | **采纳**。`todo_n` 扣掉已处理的；docs/15 的 `status=open` 不返回已处理的（§6.1、§12） |
| **M9** 决定绑在序号上，不绑文字 | **采纳**。`ref` 带原文的 `sha6`，对不上就不认（§6.1、§9.4） |
| **M10** 不可信的会议文字变成你只看到一半的 prompt | **采纳**。Q 只排设备上显示的那段；开 session 时摘要以"会议·"开头；一律 plan 模式（§6.3）。评审建议的"先翻页看完全文"没做：只排看到的那段更简单，效果相同 |
| **M11** 档案之间会漏；选择页断线后不清 | **采纳**。§1.4 改写，列出 v0.6 新增的离线数据交给 docs/18 按档案分；选择页和待办页在断线、锁屏、换 Mac 时清掉（§3.6）。docs/18 已同步 |
| **M12** 重装 hooks 会悄悄删掉决定用的 handler；两份 settings 会拿两次 | **采纳**。`install` 读配置的 `decide`；`caps.decide` 要 handler 核对通过；按 `(sid, tool_use_id)` 去重（§5.1、§5.8） |
| **M13** 对拿着的请求按 Opt+D，hook 还要干等 100 s | **采纳**。`inbox.dismiss` 让 cpad 立刻放手（`reason:"dismissed"`）（§3.3、§5.1） |
| **M14** `none` 页少了今天的打字留言；离线时 Enter 直接进打字 | **采纳**。`none` 页最后一行是"打字留言（排队）…"，`reply` 页最后一行是"打字回复…"（都不编号）；离线时 Enter 打开"连上 Mac 再选"小页，打字是第二项（§3.2、§3.5、§8） |
| **M15** 只认一个精确的 CLI 版本，每次更新审批就关 | **采纳**。认同主次版本、不低于验证版本；每个请求做形状自检；`cpa decide status` 提示（§5.8） |
| **L1** 标签超长 | **采纳**。改成"本 session 都允许改项目文件"（36 B）（§5.6） |
| **L2** 引用错了原则号 | **采纳**。统一写"原则 7（延伸 docs/13 原则 2）"（§2、§3.3） |
| **L3** `caps.todos` 和 `caps.todo` 容易看混 | **采纳**。改名 `caps.todo_app`（§9.1） |
| **L4** 缺 `none` 原因码 | **采纳**。加 `released`、`elicit`、`quota`；`multi` 并进 `mac_only`（§3.2） |
| **L5** 说 cpad 不信 hook 的等级，但等级依赖 hook 报的 `inside` | **采纳**。§5.3 写明哪些是 cpad 自己查的、哪些是 hook 报的事实，并用列表里的 `cwd` 交叉核对 |
| **L6** 预设 3"先停一下"没用 | **采纳**。从默认里去掉（§4.1） |
| **L7** "先停下等我"用 `interrupt` | **采纳**。G-D1 加 l 行，确认后带上 `interrupt:true`（§5.5、§5.7） |
| **L8** 子 agent 的权限请求 | **采纳**。一律回 null（`reason:"subagent"`），G-D1 加 m 行观察（§5.1、§5.7） |
| **L9** 控制 socket 一行 1024 B | **采纳**。三种形状分别校验，最坏 888 B，写进 §9.5，`test_decide_size.py` 守住 |
| **L10** 逐条确认时连按两下会决定两条 | **部分采纳**。每次跳到下一条后 400 ms 内的按键无效；"跳过"以后可以改（它什么都没写）。没做定时撤销窗口：T 写的行、Q 进的队列各有自己的撤销路（§6.1、§6.2） |
| **L11** W 和 T 违背 docs/13 原则 2；Opt+T 在应用里的 `how` | **采纳**。写进差异表；应用里的 Opt+T 带 `how:"key"`（§7.4） |
| **L12** 批准计划要说明只看到前 3 行 | **采纳**。新字段 `total`，确认框写"只看到前 3 行（共 N 行）"（§3.4、§9.3） |
| **L13** `E_BUSY` 的提示太具体 | **采纳**。一律"没改成，已刷新"（§7.2） |
| **补测试** | **采纳**。不合法的 `agent.pick` 回 `agent.result`；11 个 session 时被拿着的请求仍在快照上；锁屏和断线清掉选择页；`hooks install` 保留决定用的 handler；两份 settings 只拿一次；重装后审批仍开着——都在 §14 |
| **增量检查**（新字段和消息都是只新增的；唯一的协议层问题是 M1） | 同意。M1 处理后，旧 daemon 和旧固件的兼容性见 §9.6 |
