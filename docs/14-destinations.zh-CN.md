[English](14-destinations.md) · **中文**

# 14 · 采集去向（#24）：一个键直达一个地方，绑定在 Mac 上配置

> 状态：**设计定稿，待实现** · 日期：2026-10-21 · 角色：架构（去向）· 目标版本：v0.5.0（#24 的里程碑），和 v0.4.1 并行开发，v0.4.1 合并后再接线。
> 依据：#24 的需求和评论；docs/13 §9（全部）、§11.2、§12、§13、§18 第 6、7 条；docs/13 的采集去向调研稿（2026-10-21，未入库，下称"调研稿"）；05 §6.1–§6.9、§7.3、§7.6；daemon 的 `notes/render.py`、`notes/writer.py`、`hub/*`、`queue/fold.py`、`events/log.py`、`events/records.py`、`cli/main.py`、`config/settings.py`、`config/files.py`、`protocol/{codec,specs,validate,messages}.py`、`recordings/route.py`、`logging/redact.py`；固件 `lib/input/keymap.cpp:43-100`、`lib/protocol/messages.h`、`test/core/test_codec_vectors/test_main.cpp:56,76`；`protocol/schema/v0/*`、`protocol/tools/*`；`.github/workflows/ci.yml`。
> 标记：**[源]** 读过的代码或文档 · **[算]** 用脚本实算 · **[设]** 设计假设，可以改 · **[待]** 要用测试或真机验证 · **👤** 需要你。
> 范围：只写去向。没有接设备、没有跑固件、没有改代码。屏幕示意按 240×135 画，一行约 40 个 ASCII 字符或 20 个汉字。

> **修订（2026-10-22，docs/25 R1-B，#61）**：本文的种类和策略已被 [docs/25](25-route-to-any-app.zh-CN.md) §2.2、§5.2 取代，读代码以那里为准：
> - **删掉 `plugin`**（连同原因 `cloud_optin`、§1.2 的 WhisperWoof 执行器、§4.2 的 `plugin` 字段、§9 的 `add plugin.*`）；新种类 `url.<名>`（预填链接）和 `app.<名>`（剪贴板 + 把 app 调出来，你按 ⌘V），都由随 cpad 发的预设（纯数据）加上：`cpa dest add --preset P` / `--app BUNDLE`；`paste` 可以带名字。
> - **策略看 `fl`**（`l` 现场、`c` 离开 Mac、`p` 要项目、`k` 每条在设备上确认），不再写死 `ARCHIVE_KINDS`、`live`、`session`；§7.2 线上的 `live`、`cloud`、`proj` 换成一串 `fl`；§10.4 改成"不认识的种类照样显示，`fl` 缺了当 `lck`"。
> - **名字 ≤12 个字符**（§2.2 的 16 会被 main 的固件拒收）；§7.6 最坏行长 1843 B → 1507 B。
> - **前置检查**加 0 `meeting`、4 `other_profile`、9 `too_long`；§5.3 的表换成 docs/25 的 12 列表（多一种来源 `origin:"mac"`：有人在这台 Mac 上确认了这一条）；§5.4 不变式 2 换成 docs/25 §5.2 的写法；排队项的 `uncertain` → `voice`（docs/22 §5.4 第 8 条）。
> - 每个去向带 `profiles`（docs/18），所有 `dest.*` 审计行带 `prof`；新事件 `dest.handed`（交给了别的 app，不知道它收没收）。§8.2、§11.1 的"离开 Mac"清单加 `url`、`app`。

## 0. 一页摘要

1. **你要的**（#24）：不同的快捷键把一条采集直接送到不同的地方，不用每次选；哪个键去哪里在 Mac 上配置。已经定下的规则：在线时绑定的去向**马上执行**；离线时先存着、同步时投递，但**"现场动作"**（复制到剪贴板、粘贴到光标处、开 session）要等连上以后在设备上按一次 Enter；离线采集**永远不开 session**；云端去向**逐个开启**，每次投递记审计。
2. **模型**：每条采集 = 内容 + 去向 id。去向有 8 种（§2）：今日笔记、待办复选框、项目队列、开 session、剪贴板、光标处粘贴、追加到文件（如 Obsidian 收件箱）、云端插件。策略按种类定，不按单个去向定。
3. **按键**：N T Q S 固定（今日笔记、待办、队列、session）；b c f j k o u x y 这 9 个字母可以在 Mac 上自由绑定（默认 C = 剪贴板）；其余字母保留给应用和系统（§3）。同一个字母三种用法：全局 Opt+字母 = 记一条去那里；采集页 Opt+字母 = 改去向；按住 G0 再按字母 = 这段话存到那里。
4. **配置**：`$CPA_HOME/destinations.json`（0600，`version:1`，严格校验，**任何密钥都放不进去**），只用 `cpa dest ls/add/rm/bind/test` 改（§4、§9）。插上 Mac 时推一份 `dests.snapshot` 给设备，设备缓存下来，离线也照这份绑定走（§7）。
5. **判定**：纯函数 `decide(capture, dest, online, origin)`（§5）。不认识、没开、这个版本还没有、没确认过云端、默认选中却不是本地归档、没有文字、要项目却没项目——一律**降级成今日笔记**，绝不降级成粘贴或执行。剪贴板只在"在线 + 刚按的键 + 打字"或"在设备上确认过"时执行；`decide()` 永远不开 session，只会排队。
6. **先落档，再投递**：每条采集先照旧写今日笔记那一行（待办是复选框），再按判定投递。投递失败或者在等确认的，进设备"等你"里的**待处理**（`pending.snapshot`，D5-B），按 Enter 执行，Opt+D 丢掉，24 小时不处理就过期；今日笔记那一行永远在。
7. **审计**：`events.jsonl` 新增 `dest.decided / delivered / failed / dropped / expired / changed`，**不含正文**，只有字节数和 sha256 前 8 位。`cpa audit --leaves-mac` 一眼看出"什么离开过这台 Mac"（§8）。
8. **协议只新增，`v` 仍为 0，不新增错误码**：本分支（D5-A）**只交付 1 个新消息** `dests.snapshot`（≤12 项，最坏 1843 B）。`pending.snapshot`（≤8 项）和 `dest.send`（回 `ack` / `error`）是后续工作包 **D5-B**，和它们自己的固件登记（F5-0b）同批合并；在那之前，离线的剪贴板、失败的投递到不了设备的"等你"，也没法在设备上确认。`note` 正文和便条 `file.items` 加可选 `how`、`live`；设备 `revs` 加 `dests`（D5-B 再加 `pending`）；主机 `hello.caps` 加 `dest`（D5-B）（§7）。
9. **实现**：daemon 新包 `cpad/dest/`（15 个新文件），现有文件改 5 个：`cli/main.py` 注册子命令、`protocol/specs.py` 登记 `dests.snapshot` 的校验器、`protocol/README.md` 加说明、`protocol/tools/test_schemas.py` 和 `daemon/tests/unit/test_host_lines_never_start_with_word.py` 各认识这个新类型（§12.4）。**合并约束**：这 1 个新 schema 一进 main，固件的 `test_codec_vectors` 就会变红，必须和固件 codec 的类型登记（F5-0，只登记 `dests.snapshot`）同一批合并；D5-B 的 2 个 schema 同理，和 F5-0b 同批（§7.8）。

## 1. 起点和边界

### 1.1 今天已有的路（docs/13 §9.1）

G0 短按打字 → 今日笔记；Opt+Q → 项目队列；Opt+S → 确认后开 session（离线变排队）；回复 session；按住 G0 说话 → 录音目录 + 今日笔记一行；Opt+R → 会议纪要 + 今日笔记一行。v0.4.1 再加 Opt+T 和"按住 G0 时按 T"写待办复选框 [源 docs/13 §9.3]。

### 1.2 这份设计负责什么，不负责什么

| 负责（v0.5） | 不负责 |
|---|---|
| 8 种去向的模型；`destinations.json`；`decide()`；待处理、过期、重启恢复；剪贴板和"追加到文件"两个适配器；审计；`cpa dest`、`cpa audit`；`dests.snapshot`（`pending.snapshot`、`dest.send` 在后续的 D5-B）；设备上"去哪"页、采集页、"等你"的显示规则 | 光标处粘贴和云端插件的执行器（v0.7，交给 WhisperWoof，本版只占名字，判定一律"这个版本还没有"）；情境档案 `prof`（v0.8，#19）；改投任意一条旧笔记（本版只能改投"待处理"里的）；会议纪要里的待办逐条外发（v0.6） |

### 1.3 和 v0.4.1 的分工（两条并行的分支）

v0.4.1 拥有 `hub/`、`notes/`、`events/`、`recordings/`、`cli/`、`control/`、`config/`，会加 `outbox.item` 的 `note.dest`（只发 `"todo"`）、`file.items.dest`、`hello.caps.todo`，不认识的 `dest` 写普通一行并记 `dest.downgraded{item, dest}` [源 docs/13 §11.1、§12.2]。**本设计对 v0.4.1 只有 3 个要求**（请 v0.4.1 的负责人照做，否则 v0.5 要改它的 schema）：

1. `note.dest` 和 `file.items[].dest` 在 schema 里用**模式**，不要用 `enum:["todo"]`。用枚举的话，v0.4.1 的 cpad 收到 v0.5 固件发来的 `dest:"clip"` 会回 `E_BAD_FIELD`，条目被拒，违背"旧 daemon 当普通笔记存"的降级承诺。模式用 §7.5 的 `DEST_ID`。
2. `item.received` 记录的 `body` 里带上 `dest`；便条的 `rec.result` 记录带上 `dest`。v0.5 的 `cpad/dest` 从这两种记录里读去向。
3. 今日笔记那一行的格式不因 `dest` 改变（`todo` 除外，写复选框）。

## 2. 去向模型

### 2.1 八种去向（策略按种类定）

| 种类 `kind` | 设备上叫 | 做什么 | 类别 | 要项目 | 离开 Mac | 本版有执行器 |
|---|---|---|---|---|---|---|
| `daily` | 今日笔记 | 今日笔记一行：`- HH:MM #key 文本 ^cpa-<id>`（现状）| 归档 | 否 | 否 | 有（就是 NotesWriter）|
| `todo` | 待办 | 今日笔记里的复选框 `- [ ] HH:MM #key 文本 ^cpa-<id>`（§18 第 6 条；v0.4.1 实现）| 归档 | 否 | 否 | 有（v0.4.1）|
| `queue` | 项目队列 | `queue.added`，`intent=todo`；语音来的 `uncertain=true` | 记录，不执行 | 是 | 否 | 有（写记录）|
| `session` | 开 session | **永远只变成** `intent=spawn` 的排队项；真正开 session 仍是 v0.1 的确认框 + `agent.spawn` | 执行 | 是 | 否（只排队；真正开 session 时正文经 Claude Code 发出，是 v0.1 的老规则）| 有（写记录）|
| `clip` | 剪贴板 | `pbcopy`，正文从 stdin 进 | 现场 | 否 | 否（但剪贴板历史工具会记）| 有 |
| `paste` | 光标处 | 粘贴到 Mac 前台程序的光标处 | 现场 | 否 | 否 | **没有**（v0.7，经 WhisperWoof）|
| `file` | 你起的名字 | 往你指定的 `.md` / `.txt` 文件追加一行，按块 ID 幂等（如 Obsidian 收件箱）| 归档 | 否 | 看文件夹，**默认算"离开"** | 有 |
| `plugin` | 你起的名字 | WhisperWoof 的 MCP 插件（Todoist 等），cpad 不存任何密钥 | 云端 | 否 | 是 | **没有**（v0.7）|

- **现场**：会改动 Mac 当下的状态。离线采集的、语音的（文字是后来才有的）都不会自动补做，要在设备上按 Enter。
- **执行**：`decide()` 从不执行，只排队（§5.4）。
- **云端** 和 **会离开 Mac 的文件夹**：要在 Mac 上显式开启（`cpa dest add` 时输入 y），开启记一条 `dest.changed`。开启后在线、离线都自动投递，每次记审计（docs/13 §9.2 的规则）。

### 2.2 去向 id

- **内置**：`daily`、`todo`、`queue`、`session`、`clip`、`paste`。
- **自定义**：`file.<名字>`、`plugin.<名字>`，名字是 `^[a-z0-9][a-z0-9-]{0,15}$`。例如 `file.sb`、`plugin.todoist`。
- **id 自带种类**（点号前面就是 `kind`），所以离线采集记下的是 id，事后你改了按键也不会送错地方；同一个 id 也不可能被改成另一种去向。调研稿里的 `drev`（目录版本）因此不需要了。
- 线上的通用模式（给以后的新种类留余地）：`^[a-z]{3,7}(\.[a-z0-9][a-z0-9-]{0,15})?$`，最长 24 个字符。

### 2.3 写出来的样子

| 去向 | 写到哪里 | 一行的格式 |
|---|---|---|
| `daily` | `~/CardputerNotes/YYYY-MM-DD.md` | 现状，一字不改 |
| `todo` | 同上 | `- [ ] HH:MM #cpa 文本 ^cpa-<id>`（v0.4.1）|
| `file.*` | 你指定的文件 | `- YYYY-MM-DD HH:MM #cpa 文本 ^cpa-<id>`；时间不确定时前面加 `~`；控制字符换成空格；按行尾的块 ID 去重，永不修改、永不删除已有的行 |
| `queue` / `session` | `events.jsonl` 的 `queue.added` | `origin:"dest"`、`from_item:<id>`；正文控制字符换成空格、去掉首尾空白，截到 `text_max`（500 B UTF-8），不截半个字（`router.queue_text`），所以以后一定能开 session；截完是空的或以 `-` 开头（永远开不了 session）就降级成今日笔记，`reason` 记 `bad_text` |
| `clip` | Mac 剪贴板 | 原文（语音是 `memo.text`，最多 2000 字）|

**今日笔记那一行不加"→ 去向"后缀**（调研稿 §2.3 提过）：加了就要改 `notes/render.py`，而且没有 `dest` 的行要和 v0.3 逐字节相同。去了哪里看 `cpa audit`。

## 3. 按键：哪个字母去哪

### 3.1 字母分三组

| 组 | 字母 | 规则 |
|---|---|---|
| 固定 | `n` 今日笔记、`t` 待办、`q` 项目队列、`s` 开 session | 永远开着、字母不能改；和 v0.1 / v0.4.1 的 Opt+N/T/Q/S 完全一致 |
| 可绑定 | `b c f j k o u x y`（9 个）| 在 Mac 上自由绑定，一个字母只能绑一个去向；默认只有 `c` = 剪贴板 |
| 保留 | `a d e g h i l m p r v w z` | 不能绑去向：H/I/M/P/R/V/L 是应用和系统键，D 是列表里的"处理"，G 是选择器，A/E/W 是 docs/13 §5.2 预留，Z 是测试里"没绑定"的样例 [源 docs/13 §5.2；keymap.cpp:43-100] |

- 设备只认这张表里的字母；快照里给了别的字母，设备照样显示这一行，但不绑键（§10.4）。
- 固件的静态测试守住"可绑定字母在全局层和采集层都没有别的绑定"。

### 3.2 一个字母，三种用法（docs/13 §3 原则 2）

| 在哪 | 按什么 | 效果 |
|---|---|---|
| 任何页面（没有浮层） | Opt+字母 | 打开采集页，去向预设成这个字母的去向（Opt+N/T/Q/S 已经是这样）|
| 采集页 | Opt+字母 | 把这一条的去向改成它（回复 session 的采集页不能改）|
| 采集页 | Opt+G | 去向选择器：列出全部可用去向，包括没绑字母的 |
| 按住 G0 录便条 | 字母（不按 Opt）| 这段话存到那里；1–9 / 0 仍是归项目 |

去向的解析顺序（越具体越优先）：本次明确按的键或在选择器里选的（`how:"key"` / `"pick"`）→ 应用的默认（`how:"app"`，v0.6 的待办应用才有）→ 今日笔记。**默认只能是本地归档**：`how:"app"` 选中的去向如果是现场、执行或离开 Mac 的，cpad 一律降级（§5.2）。

## 4. `destinations.json`

### 4.1 示例

```json
{
  "version": 1,
  "dests": [
    {"id": "clip", "slot": 5, "key": "c", "enabled": true},
    {"id": "file.sb", "slot": 7, "key": "o", "label": "reading-list", "enabled": true,
     "leaves_mac": true, "optin": "2026-10-22T09:14:03.120+00:00",
     "file": {"root": "~/Notes/reading-list", "path": "inbox/cardputer.md"}}
  ]
}
```

文件不存在 = 只有内置去向的默认值。内置去向没写进文件的，按 §4.4 补上。

### 4.2 字段

| 字段 | 类型 | 谁可以有 | 说明 |
|---|---|---|---|
| `version` | `1` | 顶层，必填 | 比 cpad 认识的新：拒绝加载并报错，不猜 |
| `dests` | 数组，≤12 项 | 顶层，必填 | 顺序不重要，按 `slot` 排 |
| `id` | 字符串 | 每项，必填 | §2.2 |
| `slot` | 1–12 | 每项 | 设备上的显示顺序和选择器编号；不能重复。内置默认 1–6，自定义默认取 7 起第一个空位 |
| `key` | 单个字母或 `null` | 每项 | §3.1。固定的四个写了也只能是自己的字母；`null` = 不绑 |
| `label` | 1–24 B UTF-8 | 每项 | 设备上显示的名字；不能有控制字符、`"`、`\`（这样线上不需要转义，§7.6）|
| `enabled` | 布尔 | `clip`、`paste`、自定义 | 固定的四个永远开着 |
| `leaves_mac` | 布尔 | 只有 `file.*` | 默认 `true`。只有你确定那个文件夹不会被同步出去时才写 `false`（`cpa dest add --local`）。`plugin.*` 永远是 `true`，内置永远是 `false` |
| `optin` | ISO 时间或 `null` | `leaves_mac` 为真的 | 你在 `cpa dest add` 里输入 y 的时间。为 `null` 时这个去向不生效（`cpa dest ls` 显示"需要确认"）|
| `file` | `{root, path}` | 只有 `file.*`，必填 | `root`：绝对路径或 `~` 开头；`path`：相对 `root` 的文件路径。规则见 §4.3 |
| `plugin` | `{name}` | 只有 `plugin.*`，必填 | WhisperWoof 的插件名，`^[a-z0-9-]{1,16}$`。没有 URL，没有 token |

### 4.3 校验（`parse_dests()`，纯函数）

- **严格**：顶层只能有 `version`、`dests`；每项只能有上表的字段。多一个字段就整份拒绝。所以 token、API key、密码在结构上就放不进来——cpad 永远不存第三方密钥，插件的密钥留在 WhisperWoof。
- `id` 唯一；`slot` 唯一；`key` 唯一，而且符合 §3.1。`daily`、`todo`、`queue`、`session` 不能 `enabled:false`。
- `file`：`path` 不能是绝对路径，不能有 `..`，任何一段都不能以 `.` 开头，扩展名只能是 `.md` 或 `.txt`。`root` 和最终文件都不能落在 `~/Desktop`、`~/Documents`、`~/Downloads`、`~/Library`（含 iCloud Drive）和 `$CPA_HOME` 下面。解析时只能按字面查 `~` 开头的 `root`；shell 会把 `--root ~/Documents` 展开成 `/Users/<你>/Documents` 再交给 `cpa`，所以 `cpa dest add` 在保存前对 `root` 和最终文件取 realpath，对照 `$HOME` 下的这几个目录和 `$CPA_HOME` 再查一遍（`paths.check_place`），不合格就报错、什么都不写。**为什么卡扩展名和隐藏段**：追加文件是"往任意文件写一行你说的话"，指向 `~/.zshrc` 就成了代码执行；只许 `.md` / `.txt`、不许隐藏路径，把这条路堵死。
- 投递时（不是解析时）再做一遍：`root` 和目标都取 realpath，目标必须在 `root` 里面；最后一级用 `O_NOFOLLOW` 打开，是符号链接就拒绝；父目录不存在就报"需要设置"，**不替你建目录**；新建的文件权限 0600。
- 解析失败时（坏 JSON、多了字段、`version` 比 cpad 新），daemon 不退出：**只用固定的四个**（今日笔记、待办、队列、session）继续跑，`clip`、`paste` 关掉且不绑字母（`catalog.fail_closed()`），`cpa status` 和 `cpa dest ls` 显示错误（和 `projects.json` 坏了的处理一样）[源 cli/main.py `_offline_status`]。**不回到 §4.4 的默认值**：默认值里剪贴板开着、占着 C，你关掉剪贴板、把 C 给了别的去向以后，一份写错的或被新版 cpad 写过的文件会让 Opt+C 重新把私密文字复制到剪贴板。配置出错时一律"关着"，和路径被拒一样。

### 4.4 内置默认值

| id | slot | key | label | 默认 |
|---|---|---|---|---|
| `daily` | 1 | n | 今日笔记 | 永远开 |
| `todo` | 2 | t | 待办 | 永远开 |
| `queue` | 3 | q | 项目队列 | 永远开 |
| `session` | 4 | s | 开 session | 永远开 |
| `clip` | 5 | c | 剪贴板 | 开（只在本机）|
| `paste` | 6 | — | 光标处 | 本版没有执行器，不出现在设备上 |

没有内置的 `file.*`：**Obsidian 收件箱默认没有，等你指定文件**（§18 第 7 条）。

## 5. 判定：`decide(capture, dest, online, origin)`

### 5.1 四个输入

| 输入 | 含义 |
|---|---|
| `capture` | 一条采集：`ref`（条目 id 或录音会话 id）、`source`（`typed` 打字 / `voice` 便条）、`dest`（设备记下的去向 id）、`how`（`key` / `pick` / `app`；缺省按 `key`，因为 v0.4.1 只在按键时才带 `dest`；不认识的值按 `app`，即最严）、`project`（孤儿条目当作没有）、`text`（便条没转写成就是空）|
| `dest` | 按 id 在目录里找到的去向；找不到是 `None` |
| `online` | **此刻**设备链路是否 Ready（能不能在设备上弹出待处理、让你确认）|
| `origin` | 这条采集是怎么到 cpad 的：`live`（连着 Mac 时刚按的）、`queued`（离线存的、晚到的、语音的）、`confirmed`（你刚在设备上对待处理按了 Enter）|

**`origin` 怎么算**（纯函数 `origin_of()`）：打字的条目只有同时满足下面三条才是 `live`，否则一律 `queued`：

1. 正文里 `live:true`：设备在你按 Enter 保存时链路是 Ready（新固件才发；旧固件不发 → `queued`，安全的一边）；
2. 条目 id 的前两段（boot）等于本次 `hello.boot`；
3. 设备现在的 `up_ms`（最近一次 `hello` / `pong` 的值加上之后经过的时间）减去条目的 `at.up_ms`，在 0 到 30 s 之间（`LIVE_WINDOW_MS`）。

不依赖墙钟，所以设备没对过时也算得准。语音一律 `queued`：按键时还没有文字。

### 5.2 前置检查：不满足就降级成今日笔记

按顺序检查，第一条不满足的就是原因，结果是 `downgrade`，去向记成 `daily`。今日笔记那一行在这之前已经写了，所以降级不丢任何东西。

| # | 条件 | `reason` |
|---|---|---|
| 1 | 目录里没有这个 id | `unknown` |
| 2 | 这个版本没有执行器（`paste`、`plugin.*`）| `unavailable` |
| 3 | 关着 | `disabled` |
| 4 | 会离开 Mac，但没有 `optin` | `no_optin` |
| 5 | `how` 不是 `key` / `pick`，而去向是现场、执行或离开 Mac 的 | `not_explicit` |
| 6 | 没有文字（便条没转写成，或只有转写没有 `memo.text`）| `no_text` |
| 7 | 要项目却没有项目 | `no_project` |

**绝不**降级成粘贴、复制或执行；**绝不**猜一个"相近的"去向。

路由层（`router`，不在纯函数 `decide()` 里）还有一条：判定是排队（`queue` / `session`）时，正文先按 §2.3 清洗、截断；截完仍然不能开 session（空的、以 `-` 开头）就降级成今日笔记，`reason` 记 `bad_text`。

### 5.3 真值表（前置检查都通过之后）

记号：**D** 投递；**H✓** 暂存，等你在设备上按 Enter（`await_confirm`）；**H⏚** 暂存，等设备连上（`await_device`）；**Q** 排队 `intent=todo`；**Q~** 同上加 `uncertain=true`；**S** 排队 `intent=spawn`；**S~** 同上加 `uncertain=true`。

| 种类 | 打字·live·在线 | 打字·live·离线 | 打字·queued·在线 | 打字·queued·离线 | 语音·live·在线 | 语音·live·离线 | 语音·queued·在线 | 语音·queued·离线 | confirmed·在线 | confirmed·离线 |
|---|---|---|---|---|---|---|---|---|---|---|
| `daily` `todo` `file` | D | D | D | D | D | D | D | D | D | D |
| `plugin`（已确认开启）| D | D | D | D | D | D | D | D | D | D |
| `queue` | Q | Q | Q | Q | Q~ | Q~ | Q~ | Q~ | Q / Q~ | Q / Q~ |
| `session` | S | S | S | S | S~ | S~ | S~ | S~ | S / S~ | S / S~ |
| `clip` | **D** | H⏚ | H✓ | H⏚ | H✓ | H⏚ | H✓ | H⏚ | **D** | H⏚ |
| `paste` | H✓ | H⏚ | H✓ | H⏚ | H✓ | H⏚ | H✓ | H⏚ | **D** | H⏚ |

- "打字·live·离线"和"语音·live"实际上不会出现（`live` 要求链路在、语音一律 `queued`），表里照样有定义，测试 8 种 × 2 × 2 × 3 = 96 种组合全覆盖。
- `confirmed` 那两列，`Q / Q~`、`S / S~` 取决于这条原本是打字还是语音。
- `clip` 只有两种情况会执行：你连着 Mac 刚按的键（按键本身就是确认，#24"在线马上执行"）；或者你在设备上对待处理按了 Enter。
- `paste` 每次都要在设备上确认（确认框显示前台 app 名，docs/13 §9.5）；本版前置检查第 2 条就把它降级了，这一行是给 v0.7 定的规矩。
- `plugin` 离线也投递，是 docs/13 §9.2 定的"你开启过的才自动投递，每次记审计"；本版同样被第 2 条降级。

### 5.4 不变式（测试直接断言）

1. `decide()` 的结果永远不是"开 session"。在线 Opt+S 仍走 v0.1 的确认框 + `agent.spawn`，根本不经过 `decide()`；从 outbox、便条、`dest.send` 来的 `session` 一律变排队项。**离线采集永远不开 session。**
2. 现场类（`clip`、`paste`）只有在 `online` 为真、并且 `origin` 是 `confirmed`（或打字的 `clip` 是 `live`）时才是 D。同步时、重启后、重试时都不会自动复制或粘贴。
3. 去向只来自 `capture.dest`，不看正文。会议里有人说"发到 Slack"，什么都不会发生。
4. `decide()` 是纯函数：同样的输入永远得到同样的结果；不读文件、不读时钟。

## 6. 一条带去向的采集怎么走

### 6.1 顺序（沿用 05 §7.3 的"Append 先于 Send"）

```
打字：设备 outbox.item(note, dest, how, live)
  decide() ─▶ 一批 Append：item.received、dest.decided、（排队时）queue.added
             → fsync → outbox.ack(stored) → WriteNote（今日笔记一行，待办是复选框）
             └─ 结果是 D 且要执行器（clip、file）→ 投递 → dest.delivered | dest.failed

便条：录音管线（v0.4.1，recordings/service.py）
  rec.result(dest, how, memo_text) 自己一批 Append ─▶ Appended ─▶ router.on_appended：
     fold_pending ─▶ 折叠后仍在 captures 里（没判定过）的便条 ─▶ decide()
     ─▶ 第二批 Append：dest.decided、（排队时）queue.added
     └─ 结果是 D 且要执行器 → 投递 → dest.delivered | dest.failed
```

- 没有 `dest`、或 `dest` 是 `daily` 的条目：**完全是现在的路径**，不写任何 `dest.*` 事件。
- 便条的 `rec.result` 和它的 `dest.decided` **不在同一批**。两批之间崩溃时，下次启动折叠历史后这条便条仍在 `captures` 里（没判定），就在那时判定，只判定一次。反过来，判定过的便条**永远不再判定**：只看折叠以后的状态（`router.undecided_memos`），从不直接看 `Appended` 那一批里的 `rec.result`——启动时那一批就是整个 `events.jsonl`。
- `todo` 的投递就是复选框那一行本身（v0.4.1），不再另调执行器，只记 `dest.decided`。
- 设备收到 `stored` 就提示（§10.5）。剪贴板失败会在一秒内出现在"等你"里。

### 6.2 待处理（纯函数 `fold_pending(events)`，照 `queue/fold.py` 的做法）

```
dest.decided(hold) ──▶ hold ──(设备 Enter：dest.send go)──▶ dest.decided(confirmed, deliver) ──▶ running
   │                    │                                                                      ├─▶ dest.delivered（移出）
   │                    ├─(设备 Opt+D：dest.send drop)──▶ dest.dropped（移出）                  └─▶ dest.failed ──▶ failed
   │                    └─(24 小时没处理)──▶ dest.expired（移出）                                       │
dest.decided(deliver) ──▶ running ──▶ delivered / failed                                       (Enter 重试；Opt+D 丢)
```

- 设备只看到 `hold` 和 `failed`；`running` 只在 Mac 上。
- **过期**：`HOLD_TTL_S = 24 h`。只丢投递，今日笔记那一行还在。
- **重启恢复**：没有结果的 `running` 项——`file` 重新投递（按块 ID 幂等，不会写两遍）；`clip` 记 `dest.failed{code:"E_INTERRUPTED", retryable:false}`，变成待处理，等你按 Enter。**重启永远不会自动复制。**
- **自动重试**：只对归档类（`file`）、可重试的失败（`E_DEST_IO`），在 daemon 启动时（`resume(startup=True)`）和设备每次连上时（`resume(startup=False)`）重试，最多共 3 次（`MAX_ATTEMPTS`）。之后只能在设备上按 Enter 重试。设备连上时**不**把正在跑的投递当成中断：文件不会同时投两遍，剪贴板不会在 `pbcopy` 还在跑时记 `E_INTERRUPTED`。`startup` 没有默认值，调用方必须说清是哪一种。
- **折叠是幂等的**：开始一次投递的 `dest.decided` 自己带 `attempt`，折叠直接取，不累加；其余更新都是覆盖。所以同一批记录折叠两遍结果不变。hub 仍然只折叠一遍：启动时那一个 `Appended(history)` 就是唯一的一遍。
- 待处理需要正文才能投递，但 `dest.*` 事件不带正文。所以 `fold_pending` 记住"带了非 daily 去向、还没有最终结果"的条目的正文（从 `item.received.body.text` 或 `rec.result.memo_text` 取），一有最终结果就忘掉。正文本来就在这两种记录里（05 §7.3），不会因为去向多存一份到磁盘。

### 6.3 便条

- 去向记在第 0 段的 `.M`，经 `file.items[].dest`、`how` 交给 cpad（v0.4.1 起有 `dest`，v0.5 加 `how`）；录音契约 v1 不改。
- `rec.result` 出来时按 §5 判定，文字取 `memo_text`（≤2000 字）；进队列或 session 时只取清洗后的前 500 B（§2.3），完整的话还在今日笔记那一行。只有转写、没有整理（`level=transcript`）的便条没有 `memo_text`，本版按 `no_text` 降级：今日笔记那一行已经链接了转写全文。
- 会议忽略 `dest`（docs/13 §2.2 末段）。

## 7. 协议新增（只新增，`v` 仍为 0）

### 7.1 总表

| 变化 | 方向 | 内容 | 兼容性 |
|---|---|---|---|
| `dests.snapshot`（新）| H→D 状态推送，设备回 `ack` | `rev`、`dests[≤12]` | 只推给 `revs` 里有 `dests` 键的固件 |
| `pending.snapshot`（新，**D5-B**）| H→D 状态推送，设备回 `ack` | `rev`、`n`、`items[≤8]` | 只推给 `revs` 里有 `pending` 键的固件 |
| `dest.send`（新，**D5-B**）| D→H 在线请求 | `ref`、`op`（`go` / `drop`）、`dest?` | 设备只在主机 `hello.caps.dest:true` 时发；回 `ack` 或 `error` |
| 设备 `hello` / `pong` 的 `revs` 加 `dests`（D5-B 加 `pending`）| D→H | uint32；0 = 还没有 | 旧 daemon 忽略多出的键 [源 validate.py `obj()`] |
| 主机 `hello.caps` 加 `dest`（D5-B）| H→D | 布尔 | 旧固件忽略 |
| `outbox.item` 的 `note.body` 加 `how?`、`live?` | D→H | §7.5 | 旧 daemon 忽略 |
| `file.items[]` 加 `how?` | D→H | 只有便条第 0 段 | 旧 daemon 忽略 |

**本分支（D5-A）只交付 `dests.snapshot`**：1 个 schema、1 个向量文件、1 个校验器。`pending.snapshot`、`dest.send`（以及 `revs.pending`、`caps.dest`）是 D5-B：2 个 schema、2 个向量文件、`dest/specs.py` 里的 2 个校验器、`parse_dest_send` 和 `pending_snapshot` 编码、README 两行、§7.6 的两条最坏行，必须和固件登记 F5-0b 同批合并（§7.8）。路由层这边已经备好：`router.on_send` 收任何有 `id`、`ref`、`op`、`dest` 的对象（`SendRequest`）。

**不新增错误码**（和 docs/13 §12.2 一致）：找不到 `ref` → `E_UNKNOWN_ITEM`；去向不认识、关了、这个版本没有、或者是 `todo`（本版改投不到待办，§7.4）→ `E_BAD_FIELD`，`field:"dest"`，什么都不记；`go` 重新判定后**降级**了（去向在这期间被删、被关、撤销了确认，或者改投到要项目的去向却没有项目，或者正文不能排队）→ 照样记 `dest.decided`（`downgrade`），回 `E_BAD_FIELD`，`field:"dest"`，这一项离开待处理，今日笔记那一行还在——设备据此显示"没投递，留在今日笔记"，不会当成已复制；这一项正在投递 → `E_BUSY`；投递失败 → `E_INTERNAL`（这一项留在待处理里，状态是 `failed`）。

### 7.2 `dests.snapshot`

```jsonl
{"v":0,"t":"dests.snapshot","id":"h12","rev":2918374651,"dests":[{"id":"daily","slot":1,"label":"今日笔记","kind":"daily","key":"n"},{"id":"todo","slot":2,"label":"待办","kind":"todo","key":"t"},{"id":"queue","slot":3,"label":"项目队列","kind":"queue","key":"q","proj":true},{"id":"session","slot":4,"label":"开 session","kind":"session","key":"s","proj":true},{"id":"clip","slot":5,"label":"剪贴板","kind":"clip","key":"c","live":true},{"id":"file.sb","slot":7,"label":"reading-list","kind":"file","key":"o","cloud":true}]}
```

| 字段 | 规则 |
|---|---|
| `rev` | uint32，**内容哈希**：行内容的规范 JSON 取 CRC-32，是 0 就用 1。设备把快照缓存在 `/cfg/dests.json`，重启后在 `revs.dests` 里报缓存的 rev。用哈希而不是计数器，是因为 daemon 重启后计数器从 1 重来，会和设备缓存里旧的 rev 1 撞上；哈希只在内容相同时才相等，多台 Mac 也不会互相混淆 |
| `dests` | 只有**可用**的去向（开着、本版有执行器、需要确认的已确认），按 `slot` 排；最多 12 项；`x-uniqueBy: slot` |
| `id` | §2.2 的通用模式，≤24 字符 |
| `slot` | 1–12 |
| `label` | 1–24 B UTF-8，不含控制字符、`"`、`\` |
| `kind` | `^[a-z]{3,7}$`（用模式不用枚举：以后加新种类，旧固件只会隐藏这一行，不会整份拒绝）|
| `key` | 可选，`^[a-z]$`；设备只认 §3.1 的字母 |
| `live` / `cloud` / `proj` | 可选布尔，只在为真时出现：现场类 `clip` / `paste`（离线的要事后确认）/ 会离开 Mac / 要项目。"确认"标记设备按 `kind:"session"` 自己判断 |

**什么时候推**：设备 `hello` 或 `pong` 里的 `revs.dests` 和主机当前 rev 不同时（插上 Mac 时就是这个时机，05 §6.3 规则 6）；`destinations.json` 变了以后（daemon 按 mtime 重新加载，和 `projects.json` 一样）。

### 7.3 `pending.snapshot`（D5-B，本分支不交付）

```jsonl
{"v":0,"t":"pending.snapshot","id":"h13","rev":40211337,"n":2,"items":[{"ref":"3QZK1M7-0000C-0005","dest":"clip","st":"hold","src":"typed","text":"周五前给 Alice 发报价"},{"ref":"K3M9Q2T-00042-0A1C","dest":"file.sb","st":"failed","src":"voice","text":"买 USB-C hub，顺便看看扩展坞"}]}
```

| 字段 | 规则 |
|---|---|
| `rev` | 同上，内容哈希（设备不缓存它，重启报 0，于是一定会收到）|
| `n` | 待处理总数（0–65535），可能比 `items` 多 |
| `items` | 最旧的在前，最多 8 项；`x-uniqueBy: ref` |
| `ref` | MonoId（打字条目 id 或录音会话 id）|
| `dest` | 去向 id |
| `st` | `hold` / `failed` |
| `src` | `typed` / `voice` |
| `text` | 正文预览，1–48 B UTF-8，按字节截断、不截断半个字，控制字符换成空格（复用 `hub/snapshots.label`）|

预览定 48 B：一行最多约 20 个汉字，再长设备也显示不下；而且 8 项全是 `"` 这种要转义的字符时也放得进一行（§7.6）。

### 7.4 `dest.send`（D5-B，本分支不交付；路由层 `router.on_send` 已就绪）

```jsonl
{"v":0,"t":"dest.send","id":"d21","ref":"3QZK1M7-0000C-0005","op":"go"}
{"v":0,"t":"ack","id":"h22","re":"d21"}
{"v":0,"t":"dest.send","id":"d23","ref":"K3M9Q2T-00042-0A1C","op":"go","dest":"file.sb"}
{"v":0,"t":"dest.send","id":"d25","ref":"K3M9Q2T-00042-0A1C","op":"drop"}
```

- `op:"go"`：执行这一条待处理。带 `dest` 就是**改投**到另一个去向（`how` 记成 `pick`）。按 `origin:"confirmed"` 重新判定；判定是降级时回 `E_BAD_FIELD dest`（§7.1），不回 `ack`。
- **改投不到 `todo`**（`router.NO_REROUTE`）：这一条在今日笔记里已经是普通一行，变成复选框要 hub 另写一行，而 `NotesWriter` 只按 `item.received` 写、按块 ID 去重，现在没有这个效果。所以 `dest:"todo"` 回 `E_BAD_FIELD dest`、不记任何事件，设备的改投列表里也不列待办。等 hub 有了"为已有条目写一行复选框"的效果，再放开。改投到 `daily` 可以：那一行本来就在，只记 `dest.decided`，回 `ack`。
- `op:"drop"`：丢掉这一条的投递（今日笔记那一行还在）。
- 只对"待处理"里的 `ref` 有效；改投任意一条旧笔记以后再做。
- 和 `queue.done`、`inbox.dismiss`、`rec.*` 共用"最多一个在途"的通道，10 秒超时（docs/13 §10.2）。投递要等执行器返回（`pbcopy` 最多 2 s）才回 `ack`。

### 7.5 已有消息的可选字段

| 字段 | 在哪 | 规则 |
|---|---|---|
| `dest` | `note.body`、`file.items[]` | v0.4.1 加。模式 `DEST_ID = ^[a-z]{3,7}(\.[a-z0-9][a-z0-9-]{0,15})?$`（§1.3 第 1 条）|
| `how` | 同上 | `^[a-z]{1,8}$`，已知值 `key` / `pick` / `app`；不认识的值按最严的 `app` 处理 |
| `live` | 只有 `note.body` | 布尔或 `null`：保存时链路是 Ready |

`queue`、`agent.*` 不变：打字的 Opt+Q / Opt+S 仍然发 v0.1 的形状。

### 7.6 行长（用 `json.dumps` 紧凑格式、`ensure_ascii=False` 实算 [算]）

| 行 | 怎么构造 | 字节 |
|---|---|---|
| `dests.snapshot` 12 项最坏 | id 24 字符、`label` 24 B、`kind` 7 字符、三个布尔都给 `false`、`key` 给 `null`（4 B，比 `"z"` 长 1 B）、`rev` 和 `id` 取最长 | **1843** |
| 同上，但 `label` 是 24 个 `"` | 只为说明：所以 `label` 禁止 `"` 和 `\` | 2131（超了）|
| 上面 §7.2 的 6 项示例 | — | 539 |
| `pending.snapshot` 8 项最坏（D5-B）| 预览 48 个 `"`（每个转义成 2 字节）| 约 1676，D5-B 用 `rev` 4294967295 重新实测 |
| `pending.snapshot` 8 项，48 B 中文（D5-B）| — | 约 1292 |
| `outbox.item` 500 B 正文 + `dest` / `how` / `live` | — | 723（全是 `"` 时 1223）|
| `dest.send` 最长（D5-B）| — | 约 129 |

上限都是 2048 B。快照照样过一遍 `codec.fit_list`（和现有快照一样）。本分支的 `protocol/tools/test_dest_worst_lines.py` 守住 `dests.snapshot` 那两行；D5-B 给另外两个类型加上它们的最坏行。

### 7.7 新旧混用

| 组合 | 结果 |
|---|---|
| 新 cpad + 旧固件 | 旧固件不报 `revs.dests`，cpad 不推新快照；不发 `dest`，一切照旧 |
| 旧 cpad（v0.4.1）+ 新固件 | 没有 `caps.dest`、收不到快照：设备只用固定的 N/T/Q/S；绑定的字母不生效；不发 `dest.send`。万一发出了 `dest:"clip"`，v0.4.1 当普通笔记存（前提是 §1.3 第 1 条）|
| 新 cpad + 新固件、你改了绑定 | daemon 重新加载 → 新 rev → 推送；离线期间按旧绑定记下的条目存的是 id，照 id 投递，id 没了就降级 |

### 7.8 合并约束（重要）

固件的 `test_codec_vectors` 断言 `vectors/v0/messages` 里的文件数等于 `kMsgTypeCount`、每个向量类型都能被固件解码、`schema/v0` 里的 schema 数等于 `kMsgTypeCount + 1` [源 test_main.cpp:56、76]，CI 每次都跑 `pio test -e native` [源 ci.yml]。daemon 这边 `test_every_message_type_has_a_validator` 也要求 schema 和 `codec.KNOWN_TYPES` 一一对应。所以：

- 本分支（D5-A）只加 **1 个** schema、1 个向量文件（`dests.snapshot`），同时在 `protocol/specs.py` 登记 daemon 的校验器——daemon 和 `protocol/tools` 的检查都是绿的。
- **固件 CI 会红，直到固件 codec 认识 `dests.snapshot`**（`MsgType` 枚举、名字表、解码器表各加一项，只做字段校验，不做界面）。这一步是工作包 F5-0（§14），只登记这一个类型，必须先于或同批于本分支进 main。F5-0 如果照旧稿登记 3 个类型，`schema 数 == kMsgTypeCount + 1` 照样红。（2026-10-22：F5-0 随 docs/25 R1-A 和本分支同批做了，`dests.snapshot` 是第 31 个消息；设备只解码，握手后照旧回 `E_UNKNOWN_TYPE`。）
- D5-B 加 `pending.snapshot`、`dest.send` 时同理：2 个 schema 和固件登记 F5-0b 同批合并。

## 8. 审计：`dest.*` 事件和 `cpa audit`

### 8.1 事件（`events.jsonl` 仍是唯一的权威数据；全部**不含正文**）

| 事件 | 字段 | 什么时候 |
|---|---|---|
| `dest.decided` | `item, src, want, dest, kind, action, reason, how, origin, intent?, attempt?` | 每次判定（`want` 是设备要的，`dest` 是实际的；降级时 `dest` 是 `daily`）。`attempt` 只在这次判定开始一次投递时有，`fold_pending` 直接取它，所以折叠是幂等的（§6.2）|
| `dest.delivered` | `item, dest, kind, by, bytes, sha8, ref?, leaves_mac, attempt` | 执行器成功。`ref` 是不透明的引用（块 ID）|
| `dest.failed` | `item, dest, kind, code, retryable, attempt` | 执行器失败，或重启时中断 |
| `dest.dropped` | `item, dest, by` | 你在设备上按 Opt+D |
| `dest.expired` | `item, dest` | 24 小时没处理 |
| `dest.changed` | `op(add/rm/bind), id, by, enabled?, key?, leaves_mac, optin?` | `cpa dest add/rm/bind`；打开离开 Mac 的去向就是这一条 |

- `by`：`auto`（同步或在线时自动）/ `device`（你在设备上确认）/ `cli`。
- `bytes`、`sha8`：正文的 UTF-8 字节数和 sha256 前 8 位，和 `RedactFilter` 的规则一样 [源 logging/redact.py]。
- `code` 只在 Mac 里用，不上线：`E_DEST_IO`（可重试）、`E_DEST_PATH`（路径被拒）、`E_DEST_MISSING`（找不到 `pbcopy`）、`E_DEST_TIMEOUT`、`E_INTERRUPTED`。
- v0.4.1 写的 `dest.downgraded{item, dest}` 照样读，当成 `action=downgrade`。

### 8.2 `cpa audit`

```
cpa audit [--since 7d|24h|30m|<ISO>] [--leaves-mac] [--dest <id>] [--json]
```

按时间列出上面的事件：时间、事件、去向、条目、详情（`action/reason` 或 `code`）、字节数、`sha8`、是否离开 Mac、谁触发的。默认最近 7 天。`--leaves-mac` 只列**去向**送出 Mac 的东西：会离开 Mac 的 `file.*`（v0.7 起还有 `plugin.*`）的投递和开启记录，只有长度和哈希。开 session 把正文交给 Claude Code 是 v0.1 的老规则，不是一次"投递"：记在 `agent.spawned` / `queue.spawned` 里，不在 `cpa audit` 里（§11 第 1 条）。不需要 daemon 在跑（直接读 `events.jsonl`）。

## 9. 命令行 `cpa dest`

| 命令 | 做什么 |
|---|---|
| `cpa dest ls [--json]` | 列出目录：slot、字母、id、种类、名字、状态（开 / 关 / 需要确认 / 本版没有）、本机还是离开 Mac。`destinations.json` 有错时先打印错误。不显示任何正文 |
| `cpa dest add <id> [--key K] [--label L] [--slot N] [--root DIR --path FILE] [--local] [--yes]` | 内置的：打开（`clip`）或改名字、slot、字母。`file.<名字>`：新建或修改，必须给 `--root` 和 `--path`；不加 `--local` 就算离开 Mac。**打开任何离开 Mac 的去向前**，先打印一屏说明（会发什么：只发你用这个键记的那一条文字，不发音频和会议全文；发到哪里；每次都记审计），要你输入 `y`（或给 `--yes`），然后写 `optin` 并记 `dest.changed`。`plugin.<名字>` 本版拒绝："需要 WhisperWoof（v0.7）" |
| `cpa dest rm <id>` | 自定义的：删除；`clip`：关掉；固定的四个：报错（"今日笔记 / 待办 / 队列 / session 是固定去向"）。记 `dest.changed` |
| `cpa dest bind <id> <字母>` / `cpa dest bind <id> -` | 绑定或解绑。字母已被别的去向占用时报错，**不悄悄抢**："o 已经绑给 file.sb，先 cpa dest bind file.sb -"。字母不区分大小写，存小写 |
| `cpa dest test <id>` | 只做探测，不碰任何内容：`clip` 看 `/usr/bin/pbcopy` 在不在；`file` 走一遍 §4.3 的路径规则、父目录在不在、能不能写、最后一级是不是符号链接；`queue` / `session` 提示"采集时要带项目"；`paste` / `plugin` 报"本版没有"。退出码 0 = 可用，1 = 不可用 |

- 都只改 `destinations.json`（原子写、0600），daemon 按 mtime 自己重新加载并推新快照。CLI 是这个文件唯一的写入者；以后 WhisperWoof 的设置页（W8）也经 cpad.sock 让 cpad 写。
- 输出用英文，和现有的 `cpa` 子命令一致。

## 10. 设备上看到什么

### 10.1 Tab 菜单的"去哪"页

docs/13 §6 的"去哪"页列的是"去哪个应用"。v0.5 在它**最上面**加一节"记到哪"，内容来自缓存的 `dests.snapshot`：

```
菜单 · 去哪         本页 [去哪] 控制
记到哪 · Mac 上设置 · 已同步
▸Opt+N  今日笔记           默认
 Opt+T  待办
 Opt+Q  项目队列           要项目
 Opt+S  开 session         确认
 Opt+C  剪贴板             仅在线
 Opt+O  reading-list       云
;.选 Enter做 ,/换页 字母直达 Tab关
```
（往下翻：一行"按住 G0 再按字母：说的话存到那里"；再往下是 docs/13 §6.3 原有的 Opt+H 主页……）

- 每行左边是键，右边的标记：**默认**（今日笔记）、**要项目**、**确认**（开 session 会先弹确认框）、**仅在线**（现场类：离线记的，连上后要到"等你"里按 Enter）、**云**（会离开 Mac）。
- **Enter 等于按 Opt+那个字母**（菜单只做"按键重放"，docs/13 §6.2、§10.1），也就是打开采集页、去向已经选好。没有字母的去向写"（无键）采集页 Opt+G 里选"，是说明行，Enter 不执行。
- 标题行的状态：已同步 / "离线 · 用上次同步的" / "还没同步：只有 N T Q S"（从没收到过快照，或者 cpad 太旧）。
- 菜单测试新增：每个去向行按 Enter 的效果和直接按 Opt+字母完全相同；没有字母的行不执行任何动作。

### 10.2 采集页和便条画面

```
记 → 剪贴板 · #cpa          仅在线 ● ↑0
周五前给 Alice 发报价_
                              21/500
Enter存 Opt+G选去向 Opt+0-9项目 Tab菜单
```
```
● REC 0:07 → reading-list · #cpa
（电平条）
按住时：T待办 C剪贴板 O second… 1-9项目
松开 G0 结束
```

标题栏在保存之前就写着去向。离线时现场类去向后面写"仅在线"，提醒这一条要事后确认。

### 10.3 "等你"里的待处理（排在 session 前面）

```
等你 (3)
▸复制  周五前给 Alice 发报价      Enter
 失败  reading-list：买 USB-C hub…
 !     cpa/架构设计 等你 9 分钟
Enter做 Opt+G改投 Opt+D丢    Tab菜单
```

- Enter：`dest.send go`；Opt+G：选一个别的去向再 `go`（列表里不列待办：本版改投不到待办，§7.4）；Opt+D：`drop`（不用确认：今日笔记那一行还在）。
- `go` 回 `E_BAD_FIELD dest`：提示"没投递，留在今日笔记"（去向在这期间被关、被删，或者没有项目），这一项离开列表。
- 主机没有 `caps.dest` 时不显示这一节。这一节和 `dest.send`、`pending.snapshot` 一起在 D5-B / F5 才有。

### 10.4 设备端规则

- 快照只做显示和按键解析；**判定只在 cpad**。设备上能按的去向，cpad 仍然会再判一遍。
- 只认 §3.1 的字母；快照里重复的字母取第一个；不认识的 `kind` 不显示。
- 缓存写到 `/cfg/dests.json`（和 `/cfg/projects.json` 同样的存储规则），开机读出来，在 `revs.dests` 里报它的 rev。
- **锁屏时**：按住 G0 录便条时可以按去向字母（docs/13 §7.1 的规则不变）；"去哪"页、待处理的预览、选择器都要解锁后才看得到。

### 10.5 提示

| 情况 | 提示 |
|---|---|
| 在线打字 → 剪贴板，收到 `stored` | 已复制到 Mac 剪贴板 |
| 离线打字 → 剪贴板 | 已存；连上 Mac 后到"等你"按 Enter 复制 |
| 归档类 | 已存：reading-list（离线时："已存；连上 Mac 后存进 reading-list"）|
| 云 | 已存；连上 Mac 后发到 X（云）|
| 语音 → 剪贴板 | 已存；转写好了到"等你"按 Enter 复制 |

## 11. 隐私与安全

1. **去向送出 Mac 的东西都列得出来**：`cpa dest ls` 的"离开 Mac"列；`cpa audit --leaves-mac`。本版只有一种去向会离开 Mac：你开启过、文件夹会被同步的 `file.*`（云端插件 v0.7 才有）。`--leaves-mac` 只管这些投递和开启记录。**不包括开 session**：`session` 去向只排队（`cpa dest ls` 显示"local"），真正开 session 时正文经 Claude Code 发出，是 v0.1 的老规则，要在设备上确认，记在 `agent.spawned` / `queue.spawned`，不在 `cpa audit` 里。
2. **默认全在本机**：默认开着的只有今日笔记、待办、队列、session、剪贴板。离开 Mac 的去向要在 Mac 上输入 `y` 才生效，设备上打不开。
3. **只发你明确选中的那一条文字**：默认去向只能是本地归档（§5.2 第 5 条）；从不按内容决定去向；不发音频、会议全文和纪要正文。
4. **审计不记正文**，和事件日志一起永久保留。正文只在已有的 `item.received` / `rec.result` 里。
5. **配置里没有密钥**：严格校验，结构上放不进 token；插件密钥留在 WhisperWoof。
6. **防注入**：正文只作为数据交给执行器：`pbcopy` 从 stdin 读，argv 里永远没有正文；文件只追加一行，控制字符换成空格；路径限定在你给的 `root` 里、只许 `.md` / `.txt`、不许隐藏路径和符号链接、避开 TCC 目录和 `$CPA_HOME`。
7. **剪贴板**：复制出去的字同时在今日笔记和事件日志里，也会进剪贴板历史工具（WhisperWoof 自己就有一个）。说明和采集页都提醒：别用它复制密码或 token。`pbcopy` 显式设 `LANG=en_US.UTF-8`、`LC_ALL=en_US.UTF-8`，`PATH` 只给 `/usr/bin:/bin`，超时 2 s。
8. **日志**：沿用 `RedactFilter`，错误只给错误码；`CPA_LOG_CONTENT=1` 的语义不变。
9. **锁屏**：待处理预览和"去哪"页都要解锁后看（§10.4）。

## 12. daemon 实现：`cpad/dest/`

### 12.1 新文件

| 文件 | 内容 | 纯？ |
|---|---|---|
| `dest/__init__.py` | **必须为空**：`protocol/specs.py` 导入 `cpad.dest.specs` 时会先执行它，这里一导入 `snapshot` 就会经 `codec` 绕回 `specs`，循环导入 | — |
| `dest/model.py` | 类型、常量、种类表、`Dest` / `Capture` / `Decision` | 是 |
| `dest/catalog.py` | 内置默认、整份校验、查询、不可变的增删改 | 是 |
| `dest/config.py` | `destinations.json` 的解析、导出、读写 | 解析和导出是 |
| `dest/policy.py` | `decide()`、`origin_of()` | 是 |
| `dest/capture.py` | 从 `item.received` / `rec.result` 记录构造 `Capture` | 是 |
| `dest/records.py` | `dest.*` 事件构造 | 是 |
| `dest/pending.py` | `fold_pending()` | 是 |
| `dest/router.py` | 把判定变成"要追加的记录 + 要跑的投递 + 回复"；便条钩子 `on_appended` | 是 |
| `dest/paths.py` | `file.*` 的路径规则 | 解析是，`O_NOFOLLOW` 打开不是 |
| `dest/adapters.py` | `ClipAdapter`、`FileAdapter` | 否（唯一做 I/O 的地方）|
| `dest/specs.py` | `dests.snapshot` 的校验器和限制常量（D5-B 再加 2 个）（**只 import `protocol.validate` 和 `protocol.limits`**，否则 `protocol.specs` ↔ `dest` 循环导入）| 是 |
| `dest/snapshot.py` | `dests.snapshot` 的数据类（`DestRow`、`DestsSnapshot`）、`content_rev`、行构建、装进一行（`codec.fit_list`）| 是 |
| `dest/audit.py` | 审计行的筛选和格式化 | 是 |
| `dest/cli.py` | `cpa dest …`、`cpa audit` | 否 |

每个文件 ≤400 行，函数 ≤50 行，McCabe ≤10（ruff C901）；数据一律 `@dataclass(frozen=True, slots=True)`，映射用 `MappingProxyType`。

### 12.2 公开 API（实现者照这个写；名字和签名是契约）

```python
# dest/model.py
Kind = Literal["daily", "todo", "queue", "session", "clip", "paste", "file", "plugin"]
Source = Literal["typed", "voice"]
How = Literal["key", "pick", "app"]
Origin = Literal["live", "queued", "confirmed"]
Action = Literal["deliver", "hold", "queue", "downgrade"]
Reason = Literal["archive", "cloud_optin", "live_key", "confirmed", "await_confirm", "await_device",
                 "queue", "never_auto_exec", "unknown", "unavailable", "disabled", "no_optin",
                 "not_explicit", "no_text", "no_project", "bad_text"]   # bad_text: router only (§5.2)
By = Literal["auto", "device", "cli"]

DAILY = "daily"
BUILTIN_IDS: tuple[str, ...] = ("daily", "todo", "queue", "session", "clip", "paste")
FIXED_KEYS: Mapping[str, str]          # {"daily": "n", "todo": "t", "queue": "q", "session": "s"}
BINDABLE_KEYS: frozenset[str]          # frozenset("bcfjkouxy")
RESERVED_KEYS: frozenset[str]          # frozenset("adeghilmprvwz")
DEST_ID = r"^[a-z]{3,7}(\.[a-z0-9][a-z0-9-]{0,15})?$"
CUSTOM_NAME = r"^[a-z0-9][a-z0-9-]{0,15}$"
DESTS_MAX = 12
LABEL_MAX = 24                          # UTF-8 bytes
LIVE_WINDOW_MS = 30_000
HOLD_TTL_S = 86_400
MAX_ATTEMPTS = 3
VOICE_TEXT_MAX = 2000                   # characters

@dataclass(frozen=True, slots=True)
class KindSpec:
    kind: Kind
    live: bool              # changes the Mac's current state: never replayed unattended
    needs_project: bool
    available: bool         # this cpad has an executor (False: paste, plugin)
    custom: bool            # instances are "<kind>.<name>"

KINDS: Mapping[Kind, KindSpec]

@dataclass(frozen=True, slots=True)
class FileOpts:
    root: str
    path: str

@dataclass(frozen=True, slots=True)
class PluginOpts:
    name: str

@dataclass(frozen=True, slots=True)
class Dest:
    id: str
    kind: Kind
    label: str
    slot: int
    key: str | None = None
    enabled: bool = True
    leaves_mac: bool = False
    optin: str | None = None
    file: FileOpts | None = None
    plugin: PluginOpts | None = None

@dataclass(frozen=True, slots=True)
class Capture:
    ref: str                     # outbox item id or recording session id (MonoId)
    source: Source
    dest: str                    # dest id chosen on the device
    how: How = "key"
    project: str | None = None   # None for orphans
    text: str | None = None      # None / "" = nothing to deliver
    stamp: str = ""              # "YYYY-MM-DD HH:MM", "~" prefix when the time is uncertain

@dataclass(frozen=True, slots=True)
class Decision:
    action: Action
    dest: str                    # effective dest id ("daily" on downgrade)
    reason: Reason
    intent: Literal["todo", "spawn"] | None = None
    uncertain: bool = False

@dataclass(frozen=True, slots=True)
class Delivery:                  # here, not in router.py: records / adapters / router all use it (no import cycle)
    key: str                     # f"{capture.ref}:{dest.id}"; adapters are idempotent by it
    capture: Capture
    dest: Dest
    attempt: int = 1
    by: By = "auto"
    re: str | None = None        # dest.send id to answer when it finishes

@dataclass(frozen=True, slots=True)
class DeliveryResult:
    ok: bool
    code: str | None = None      # E_DEST_IO E_DEST_PATH E_DEST_MISSING E_DEST_TIMEOUT
    retryable: bool = False
    ref: str | None = None       # opaque, goes to dest.delivered (block id for files)

def kind_of(dest_id: str) -> Kind | None: ...
def spec(dest: Dest) -> KindSpec: ...                 # KINDS[dest.kind]

# dest/catalog.py
@dataclass(frozen=True, slots=True)
class Catalog:
    dests: tuple[Dest, ...]                           # every builtin present, sorted by slot
    problem: str | None = None                        # destinations.json error; fail_closed() in use

class DestsError(ValueError): ...
def defaults() -> Catalog: ...
def fail_closed() -> Catalog: ...                     # fixed four only; clip / paste off and unbound (§4.3)
def validate(dests: Iterable[Dest]) -> Catalog: ...   # whole-catalog rules (§4.3); raises DestsError
def find(cat: Catalog, dest_id: str) -> Dest | None: ...
def by_key(cat: Catalog, key: str) -> Dest | None: ...
def usable(cat: Catalog) -> tuple[Dest, ...]: ...     # enabled, available, opted in: what the device sees
def state_of(dest: Dest) -> Literal["on", "off", "needs_optin", "unavailable"]: ...
def put(cat: Catalog, dest: Dest) -> Catalog: ...     # add or replace by id, then validate
def remove(cat: Catalog, dest_id: str) -> Catalog: ...  # custom: delete; clip/paste: disable; fixed: DestsError
def bind(cat: Catalog, dest_id: str, key: str | None) -> Catalog: ...  # never steals a key: DestsError
def free_slot(cat: Catalog) -> int: ...              # first free slot >= 7

# dest/config.py
FILE_NAME = "destinations.json"
VERSION = 1
def dests_path(paths: Paths) -> Path: ...             # paths.home / FILE_NAME (no edit to config/settings.py)
def parse_dests(doc: Any) -> Catalog: ...             # strict (§4.3); raises DestsError
def dump_dests(cat: Catalog) -> dict[str, Any]: ...   # parse_dests(dump_dests(c)) == c
def load_dests(path: Path) -> Catalog: ...            # missing file -> defaults(); raises DestsError
def load_catalog(path: Path) -> Catalog: ...          # never raises: fail_closed() with problem set
def save_dests(path: Path, cat: Catalog) -> None: ... # config.files.write_json (atomic, 0600)

# dest/policy.py
@dataclass(frozen=True, slots=True)
class DeviceClock:
    boot: str                                         # hello.boot
    up_ms: int                                        # device up_ms now (last hello/pong + elapsed)

def origin_of(live: bool | None, item_id: str, at_up_ms: int, clock: DeviceClock | None,
              window_ms: int = LIVE_WINDOW_MS) -> Origin: ...
def decide(capture: Capture, dest: Dest | None, online: bool, origin: Origin) -> Decision: ...

# dest/capture.py
def stamp(wall_ms: int | None, received_at: str, tz: tzinfo | None = None) -> str: ...
def capture_from_item(record: Mapping[str, Any], tz: tzinfo | None = None) -> Capture | None: ...
    # item.received, kind note, body.dest present and != "daily"; else None
def capture_from_result(record: Mapping[str, Any], tz: tzinfo | None = None) -> Capture | None: ...
    # rec.result of a memo with dest != "daily"; text = memo_text[:VOICE_TEXT_MAX]

# dest/records.py
DEST_EVENTS: frozenset[str]   # decided delivered failed dropped expired changed (+ v0.4.1 "dest.downgraded")
def digest(text: str) -> tuple[int, str]: ...         # (UTF-8 bytes, sha256[:8])
def dest_decided(cap: Capture, decision: Decision, origin: Origin, at: str,
                 attempt: int | None = None) -> dict[str, Any]: ...   # attempt only when a delivery starts
def dest_delivered(d: Delivery, *, at: str, ref: str | None = None) -> dict[str, Any]: ...
def dest_failed(d: Delivery, *, code: str, retryable: bool, at: str) -> dict[str, Any]: ...
def dest_dropped(ref: str, dest: str, *, by: By, at: str) -> dict[str, Any]: ...
def dest_expired(ref: str, dest: str, *, at: str) -> dict[str, Any]: ...
def dest_changed(op: Literal["add", "rm", "bind"], dest: Dest, *, by: By, at: str) -> dict[str, Any]: ...

# dest/pending.py
@dataclass(frozen=True, slots=True)
class PendingItem:
    capture: Capture
    dest: str                                         # effective dest id
    st: Literal["hold", "running", "failed"]
    since: str                                        # ISO time of the dest.decided
    attempt: int = 0
    code: str | None = None
    retryable: bool = False

@dataclass(frozen=True, slots=True)
class PendingState:
    items: tuple[PendingItem, ...] = ()               # oldest first
    captures: Mapping[str, Capture] = field(default_factory=lambda: MappingProxyType({}))
                                                      # refs with a non-daily dest and no final outcome yet

def fold_pending(events: Iterable[Mapping[str, Any]], start: PendingState | None = None) -> PendingState: ...
def find(state: PendingState, ref: str) -> PendingItem | None: ...
def visible(state: PendingState) -> tuple[PendingItem, ...]: ...   # hold and failed

# dest/router.py   (imports model, catalog, policy, records, pending, events.records.queue_added,
#                   agents.claude_code.argv.check_text, hub.snapshots.truncate_utf8, notes.render.clean)
class SendRequest(Protocol):                          # D5-B's DestSend satisfies it
    id: str; ref: str; op: str; dest: str | None

NO_REROUTE: frozenset[str] = frozenset({"todo"})      # go with dest=todo -> E_BAD_FIELD dest (§7.4)

@dataclass(frozen=True, slots=True)
class Reply:
    code: str | None = None                           # None = ack
    field: str | None = None

@dataclass(frozen=True, slots=True)
class Step:
    records: tuple[Mapping[str, Any], ...] = ()       # to append (one batch, before anything else)
    deliveries: tuple[Delivery, ...] = ()             # to run after the append
    reply: Reply | None = None                        # dest.send answer now; None = later or not a request

def queue_text(text: str | None) -> str | None: ...   # cleaned, <= text_max bytes; None = could never be spawned
def on_capture(cat: Catalog, cap: Capture, *, online: bool, origin: Origin, at: str) -> Step: ...
def route(cap: Capture, ctx: RouteContext) -> Step: ...
def undecided_memos(pend: PendingState) -> tuple[Capture, ...]: ...   # voice captures folded, never decided
def on_appended(cat: Catalog, pend: PendingState, records: Iterable[Mapping[str, Any]], *, online: bool,
                at: str) -> tuple[PendingState, Step]: ...  # fold, then route undecided_memos (§12.5)
def on_send(cat: Catalog, pend: PendingState, req: SendRequest, *, online: bool, at: str) -> Step: ...
def on_result(d: Delivery, result: DeliveryResult, *, at: str) -> Step: ...
def expire(pend: PendingState, *, now: float, at: str) -> Step: ...
def resume(pend: PendingState, cat: Catalog, *, at: str, startup: bool) -> Step: ...
    # startup=True once at app start-up, startup=False at every link-up; no default (§6.2)

# dest/paths.py
class PathRefused(ValueError): ...                    # .reason: "outside_root" "symlink" "denied" "suffix" "hidden" "no_parent"
DENIED_UNDER: tuple[str, ...] = ("Desktop", "Documents", "Downloads", "Library")   # relative to $HOME
SUFFIXES: tuple[str, ...] = (".md", ".txt")
def check_spec(opts: FileOpts) -> None: ...           # lexical rules at parse time; raises PathRefused
def check_place(opts: FileOpts, home: Path, denied: Iterable[Path] = ()) -> None: ...
    # check_spec + deny list for absolute roots too (cpa dest add, before saving; denied = ($CPA_HOME,))
def target_path(opts: FileOpts, home: Path, denied: Iterable[Path] = ()) -> Path: ...
    # delivery time: realpath containment + deny list; raises PathRefused
def append_line(path: Path, line: str, marker: str) -> bool: ...  # O_APPEND|O_CREAT|O_NOFOLLOW, 0600, fsync;
                                                                  # False when marker already ends a line

# dest/adapters.py
@dataclass(frozen=True, slots=True)
class Probe:
    state: Literal["ok", "needs_setup", "missing", "unavailable"]
    reason: str = ""

class Adapter(Protocol):
    def probe(self, dest: Dest) -> Probe: ...         # never touches content
    def deliver(self, d: Delivery) -> DeliveryResult: ...

PBCOPY = "/usr/bin/pbcopy"
class ClipAdapter:
    def __init__(self, run: Callable[..., subprocess.CompletedProcess] = subprocess.run,
                 exe: str = PBCOPY, timeout_s: float = 2.0) -> None: ...
class FileAdapter:
    def __init__(self, home: Path, denied: Iterable[Path] = ()) -> None: ...
def file_line(cap: Capture) -> tuple[str, str]: ...   # (line, marker) — "- {stamp} #proj text ^cpa-<ref>"
def make_adapters(home: Path, denied: Iterable[Path] = ()) -> Mapping[Kind, Adapter]: ...
    # {"clip": ClipAdapter(), "file": FileAdapter(home, denied)}
def probe(dest: Dest, adapters: Mapping[Kind, Adapter]) -> Probe: ...  # unavailable / per-kind rules (§9)

# dest/specs.py  (imports only cpad.protocol.validate and cpad.protocol.limits)
SLOT_MAX = 12
DESTS_MAX = 12
LABEL_MAX = 24                                        # UTF-8 bytes
def dest_specs() -> dict[str, Check]: ...             # {"dests.snapshot"}; D5-B adds "pending.snapshot", "dest.send"

# dest/snapshot.py
@dataclass(frozen=True, slots=True)
class DestRow:
    id: str
    slot: int
    label: str
    kind: str
    key: str | None = None
    live: bool | None = None                          # only True or None on the wire
    cloud: bool | None = None
    proj: bool | None = None

@dataclass(frozen=True, slots=True)
class DestsSnapshot:
    T: ClassVar[str] = "dests.snapshot"
    rev: int
    dests: tuple[DestRow, ...]
    id: str = ""

def content_rev(rows: Sequence[Any]) -> int: ...      # zlib.crc32 of canonical JSON, 0 -> 1
def dest_row(dest: Dest) -> DestRow: ...
def dest_rows(cat: Catalog) -> tuple[DestRow, ...]: ...
def dests_snapshot(cat: Catalog) -> DestsSnapshot: ...      # fitted with codec.fit_list; rev = content_rev
def load_snapshot(path: Path) -> DestsSnapshot: ...         # load_catalog + dests_snapshot

# D5-B, not in this branch: dest/specs.py PENDING_MAX = 8, PREVIEW_MAX = 48 (UTF-8 bytes), and in a new module
@dataclass(frozen=True, slots=True)
class PendingRow:
    ref: str
    dest: str
    st: str
    src: str
    text: str

@dataclass(frozen=True, slots=True)
class PendingSnapshot:
    T: ClassVar[str] = "pending.snapshot"
    rev: int
    n: int
    items: tuple[PendingRow, ...]
    id: str = ""

@dataclass(frozen=True, slots=True)
class DestSend:
    T: ClassVar[str] = "dest.send"
    id: str
    ref: str
    op: Literal["go", "drop"]
    dest: str | None = None

def pending_rows(state: PendingState) -> tuple[tuple[PendingRow, ...], int]: ...   # (rows <= 8, total)
def pending_snapshot(state: PendingState) -> PendingSnapshot: ...
def parse_dest_send(msg: Mapping[str, Any]) -> DestSend: ... # decoded + validated (e.g. messages.Unhandled.raw)
def device_revs(revs: Mapping[str, Any]) -> tuple[int | None, int | None]: ...   # (dests, pending); None = old fw

# dest/audit.py
@dataclass(frozen=True, slots=True)
class AuditRow:
    at: str
    ev: str
    item: str | None
    dest: str | None
    detail: str
    by: str | None = None
    bytes: int | None = None
    sha8: str | None = None
    leaves_mac: bool = False

def parse_since(text: str, now: float) -> str: ...    # "7d" "24h" "30m" or ISO -> ISO lower bound
def audit_rows(events: Iterable[Mapping[str, Any]], *, since: str | None = None,
               leaves_mac: bool = False, dest: str | None = None) -> tuple[AuditRow, ...]: ...
def format_rows(rows: Iterable[AuditRow]) -> list[str]: ...

# dest/cli.py
def add_parsers(sub: Any) -> None: ...                # registers `cpa dest {ls,add,rm,bind,test}` and `cpa audit`
def dest_ls(paths: Paths, args: argparse.Namespace) -> int: ...
def dest_add(paths: Paths, args: argparse.Namespace) -> int: ...
def dest_rm(paths: Paths, args: argparse.Namespace) -> int: ...
def dest_bind(paths: Paths, args: argparse.Namespace) -> int: ...
def dest_test(paths: Paths, args: argparse.Namespace) -> int: ...
def audit(paths: Paths, args: argparse.Namespace) -> int: ...
```

- `DestsError` 继承 `ValueError`，所以 `cli/main.py` 现有的 `except (..., ValueError, OSError)` 直接接住，打印 `error: …`、退出码 2。
- `ClipAdapter.deliver`：`run([exe], input=text.encode("utf-8"), env={"LANG": "en_US.UTF-8", "LC_ALL": "en_US.UTF-8", "PATH": "/usr/bin:/bin"}, capture_output=True, timeout=timeout_s, check=False)`；`FileNotFoundError` → `E_DEST_MISSING`；`TimeoutExpired` → `E_DEST_TIMEOUT`；返回码非 0 → `E_DEST_IO`。现场类的失败一律 `retryable=False`（§5.4 第 2 条）。
- `FileAdapter.deliver`：`target_path` → `file_line` → `append_line`；`PathRefused` → `E_DEST_PATH`（不可重试）；`OSError` → `E_DEST_IO`（可重试）。`ref` 是块 ID。
- `on_capture` 的投递只对 `clip`、`file`（以后 `plugin`、`paste`）产生；`daily`、`todo` 只记 `dest.decided`；排队时 `records` 里多一条 `events.records.queue_added(qid=None, origin="dest", from_item=cap.ref, text=queue_text(cap.text), …)`，`qid` 照旧在追加时由 `assign_qids` 分配。`queue_text` 保证 `check_text(text) is None`，排出来的项以后一定能开 session。

### 12.3 复用了什么（只 import，不改）

| 现有的 | 用在哪 |
|---|---|
| `notes.render.clean`、`block_id` | `file_line` 的正文清洗和幂等标记 |
| `notes.writer.NotesWriter._append` 的做法（行尾标记正则、`O_APPEND`、fsync）| `paths.append_line` 照着写，多一个 `O_NOFOLLOW`（`_append` 是私有方法，而且会在笔记目录里 mkdir，不适合写进你的库）|
| `config.files.write_json` / `read_json`、`FILE_MODE` | `destinations.json` 读写 |
| `config.settings.Paths`、`cpa_home`、`secure_home` | CLI |
| `events.log.EventLog`、`events.records.iso`、`queue_added` | `dest.changed` 追加、审计读取、排队记录 |
| `protocol.validate` 的组合子、`protocol.limits.LIMITS` | `dest/specs.py` |
| `protocol.codec.fit_list`、`protocol.messages.to_wire` | 快照装进一行、编码 |
| `hub.snapshots.label` / `truncate_utf8` | 快照里的名字和预览；排队正文的截断 |
| `agents.claude_code.argv.check_text` | 排队正文能不能开 session（`router.queue_text`）|
| `logging.redact` 的 sha8 规则 | `records.digest` 用同一个算法 |
| `queue.fold` 的折叠写法 | `pending.fold_pending` |
| `cli.main` 的 `fn(paths, args, deps)` 约定 | `dest/cli.add_parsers` |

### 12.4 对现有文件的改动（只有这 5 个）

| 文件 | 改动 |
|---|---|
| `daemon/src/cpad/cli/main.py` | `from cpad.dest import cli as dest_cli`；`build_parser()` 里加一行 `dest_cli.add_parsers(sub)`。模块文档串的命令列表可以顺手加 `dest`、`audit` |
| `daemon/src/cpad/protocol/specs.py` | `from cpad.dest.specs import dest_specs`；`SPECS` 里加一行 `**dest_specs(),` |
| `protocol/README.md` | 回复表加 `dests.snapshot` 一行；末尾加第 24 条（字段、`rev`、最坏行长、合并顺序；写这份稿时是第 17 条）|
| `protocol/tools/test_schemas.py` | 期望的类型集合加 `dests.snapshot` |
| `daemon/tests/unit/test_host_lines_never_start_with_word.py` | 主机消息样本里加一条 `DestsSnapshot` |

`config/settings.py` **不改**：路径由 `dest/config.dests_path(paths)` 算。不改 `limits.json`、`common.schema.json`、`outbox.item.schema.json`（它们的说明性更新跟 v0.4.1 合并后的集成一起做，见 §12.5）。

### 12.5 集成（v0.4.1 合并之后，单独一个小 PR，D5-I）

1. `protocol/messages.py`：`NoteBody` 加 `how`、`live`；`HostHello.caps` 加 `dest`；`DeviceHello` / `Pong` 保留原始 `revs`（或加 `dests`、`pending` 两个可选字段）。`events/records._plain_body` 带上 `dest`、`how`、`live`。
2. `hub/state.py`：`HubState` 加 `dests: Catalog`、`pending: PendingState`、`dest_published: (rev_dests, rev_pending)`、`clock: DeviceClock | None`。
3. `hub/reduce.py`：
   - `OutboxItem(note)`：在 v0.4.1 的 `dest.downgraded` 分支的位置，换成 `capture_from_item` → `origin_of` → `router.on_capture`；`Append((received, *step.records))` 之后照旧 `Send(stored)`、`WriteNote`，最后每个 `Delivery` 一个新效果 `RunDelivery`。
   - `Appended`：`pending, step = router.on_appended(state.dests, state.pending, records, online=…, at=…)`；效果里**先** `Append(step.records)`，再每个 `Delivery` 一个 `RunDelivery`。便条**只**按折叠以后的状态判定：折叠完仍在 `pending.captures` 里（没判定过）、`source` 是 `voice` 的才判定（`undecided_memos`），**绝不**直接拿这一批里的 `rec.result` 去判定——启动时这一批就是整个 `events.jsonl`，那样会把每条旧便条再排一次队、再记一遍审计，丢掉或过期的剪贴板也会回到"等你"。`step.records` 追加以后经下一个 `Appended` 回来折叠，`on_appended` 返回的状态里不含它们。v0.4.1 写过复选框、没有 `dest.*` 记录的待办便条，第一次启动时会补一条 `dest.decided`（`deliver/archive`，不投递）。
   - `LinkReady` / `Reconcile`：设备报了 `revs.dests` / `revs.pending` 才推 `dests_snapshot` / `pending_snapshot`（后者 D5-B）；`LinkReady` 时再跑一次 `router.resume(…, startup=False)`：正在跑的投递不动。
   - 新的设备请求 `dest.send`（D5-B）→ `parse_dest_send` → `router.on_send`。改投到 `todo` 回 `E_BAD_FIELD dest`，直到 hub 有"为今日笔记里已有的条目写一行复选框"的效果（那时 `router` 再给 `Step` 加一项要 hub 先写的笔记行，写完才回 `ack`）；`go` 降级时照样追加 `dest.decided`，回 `E_BAD_FIELD dest`。
   - 新输入 `DeliveryFinished(delivery, result)` → `router.on_result` → 追加记录、按需回 `ack` / `error`、重推 `pending.snapshot`（D5-B）。
4. `hub/runner.py`：`RunDelivery` 用 `asyncio.to_thread(adapter.deliver, d)` 跑，按顺序一个一个来，结果作为 `DeliveryFinished` 回到 reduce（和 `RunSpawn` 一样）。
5. `app.py`：启动时先 `load_catalog` 放进 `HubState`，再派发现有的那一个 `Appended(history)`——这就是历史唯一的一次折叠，**不要**再另外 `fold_pending(log.load())`——然后 `router.resume(…, startup=True)`；按 mtime 重载 `destinations.json`（照 `_reload_projects`）；housekeeping 里每分钟 `router.expire`。
6. 协议说明性更新：`common.schema.json` 的 `revs` 写上 `dests`、`pending`；`outbox.item` 写上 `how`、`live`；`fileItem` 写上 `how`；`hello` 写上 `caps.dest`；各加合法和非法向量。

## 13. 测试（一律先写测试：RED → GREEN → REFACTOR）

| 测试文件（新）| 先写的断言 |
|---|---|
| `tests/unit/test_dest_policy.py` | §5.3 的 96 种组合逐格对照（表就是测试的预言）；§5.2 七条前置检查各自的优先顺序；§5.4 的四条不变式（hypothesis：任何输入下 `decide()` 都不出"开 session"；现场类只在 `online` 且 `confirmed` 或打字 `live` 时才是 `deliver`）；`origin_of` 的边界：`live` 缺失、`false`、换了 boot、29.9 s / 30.1 s、`up_ms` 回绕（负差值）|
| `tests/unit/test_dest_config.py` | 往返 `parse(dump(c)) == c`；文件不存在 = 默认；`version: 2` 拒绝；顶层和每项多一个字段都拒绝（专门测 `token`、`api_key`）；`id` / `slot` / `key` 重复；固定字母改不了、固定去向关不掉；保留字母拒绝；`label` 超 24 B、含 `"` `\` 控制字符拒绝；`file.path` 绝对、`..`、隐藏段、`.sh` 扩展名、`~/Documents` 下拒绝；`leaves_mac` 无 `optin` → `needs_optin`；`put` / `remove` / `bind` 不改原对象；`bind` 不抢字母；坏文件（含 `version: 2`、多一个字段）时 `load_catalog` 返回 `fail_closed()` 加 `problem`，**任何现场类都不可用、不占字母** |
| `tests/unit/test_dest_router.py` | 在线打字 `clip` → 1 条 `dest.decided`（带 `attempt`）+ 1 个投递；离线 `clip` → `hold`、没有投递；`queue` / `session` → `queue.added`（`origin:"dest"`、语音 `uncertain`）；2000 字带换行的便条进队列，`check_text(queue.added.text) is None`（hypothesis：任何正文都是能开 session 的排队项，否则 `bad_text` 降级）；降级只有记录没有投递；`dest.send go` → `confirmed` 投递、回复延后；改投到关掉的去向或 `todo` → `E_BAD_FIELD dest`、不记录；`go` 后降级（去向被删、被关、没项目）→ 记 `dest.decided` 并回 `E_BAD_FIELD dest`；`drop` → `dest.dropped` + `ack`；不在待处理的 `ref` → `E_UNKNOWN_ITEM`；`running` 再 `go` → `E_BUSY`；`expire` 只动超过 24 h 的；`resume`：`startup` 没有默认值，`file` 重投、`clip` 变 `E_INTERRUPTED`、可重试的失败最多 3 次，连上时不动正在跑的；便条钩子 `on_appended`：新便条判定一次，含 `rec.result` + `dest.decided` 的历史再重放（每次重启）不再判定，丢掉或过期的不回来，崩溃留下的没判定便条启动时判定一次 |
| `tests/unit/test_dest_pending.py` | 每种事件的进出；`captures` 在最终结果后清掉；`fold(全部) == fold(前半, fold(后半))` 的增量性质；把同一批事件折叠到它们自己产生的状态上结果不变（幂等，`attempt` 不再累加）|
| `tests/unit/test_dest_adapters.py` | `ClipAdapter`：假 `run` 收到的 argv 只有 `[PBCOPY]`、正文只在 `input`、env 带 UTF-8 locale、超时和缺命令的错误码、中文原样；`FileAdapter`（临时目录当 `$HOME`）：根外路径、符号链接（父目录和最后一级）、拒绝的目录、扩展名、隐藏段、父目录不存在、新文件 0600、同一个 `key` 投两次只写一行、已有文件末尾没换行时先补换行；`probe` 从不写文件 |
| `tests/unit/test_dest_snapshot.py`、`protocol/tools/test_dests_snapshot.py` | 快照行按 `slot` 排、只含 `usable`；坏的 `destinations.json` 只给固定的四个；`rev` 内容相同则相同、内容变则变、从不为 0；12 项最坏 ≤2048 B；`dest_specs()` 的常量和 schema 文件里的数一致（读 JSON 比对）；向量文件经 `codec.decode_line` 得到声明的结果（现有 `test_codec_vectors` 自动覆盖）。D5-B 加 `test_dest_wire.py`：预览按字节截断不截半个字、`parse_dest_send`、另外 2 个 schema 的常量 |
| `tests/unit/test_dest_audit.py` | 只列 `dest.*`（含 v0.4.1 的 `dest.downgraded`）；`--since` 的几种写法；`--leaves-mac`、`--dest` 过滤；输出里永远没有正文（构造带正文的 `item.received`，断言输出不含它）|
| `tests/integration/test_dest_cli.py` | 用 `main(argv)` 跑，`CPA_HOME` 和 `HOME` 指到临时目录：`ls` / `add` / `rm` / `bind` / `test` 的输出和退出码；`--root` 给绝对路径的 `$HOME/Documents`、`$HOME/Library/...`、`$CPA_HOME` 一律拒绝、什么都不写；`add file.x` 不加 `--yes` 时等输入，输入不是 `y` 就不改文件；加了 `--yes` 写 `optin` 并追加 `dest.changed`；`destinations.json` 权限 0600；`audit` 在 daemon 没跑时也能用 |
| `protocol/tools/test_dest_worst_lines.py` | §7.6 `dests.snapshot` 的最坏行（布尔 `false`、`key` 为 `null`）：schema 合法，正好 1843 B，≤2048 B；`label` 全是 `"` 时 2131 B 超限。D5-B 加另外两条 |

覆盖率目标沿用 `--cov-fail-under=80`；`dest/` 自己目标 90% 以上（除 `cli.py` 外都是纯函数）。

## 14. 工作包与顺序

| 编号 | 内容 | 依赖 | 在哪 |
|---|---|---|---|
| **D5-A** | `cpad/dest/` 全部新文件和测试；**1 个** schema（`dests.snapshot`）、1 个向量文件、`test_dest_worst_lines.py`、`test_dests_snapshot.py`；§12.4 的 5 处改动 | 无 | 本分支 `feat/destinations` |
| **F5-0** | 固件 codec 认识 `dests.snapshot`（`MsgType`、名字表、解码器表；只校验字段）。**只登记这一个**：登记 3 个的话 `schema 数 == kMsgTypeCount + 1` 照样红 | D5-A 的 schema | 固件分支；**必须先于或同批于 D5-A 进 main**（§7.8）|
| **D5-B** | `pending.snapshot`、`dest.send`：2 个 schema、2 个向量文件、`dest/specs.py` 的 2 个校验器、`PendingSnapshot` / `pending_rows` / `pending_snapshot`、`DestSend` / `parse_dest_send`、`device_revs`、README 两行、§7.6 的两条最坏行（`rev` 取 4294967295 实测）、`test_dest_wire.py` | D5-A | 后续分支；**和 F5-0b 同批进 main** |
| **F5-0b** | 固件 codec 认识 `pending.snapshot`、`dest.send` | D5-B 的 schema | 固件分支；和 D5-B 同批 |
| D5-I | §12.5 的接线（`dest.send` 那部分要 D5-B）| v0.4.1 合并、D5-A、D5-B | v0.4.1 之后 |
| F5 | `lib/core/dests`（快照解析、缓存、字母规则、`resolve_dest`）；全局和采集页的 Opt+字母、G0+字母；Opt+G 选择器；"去哪"页的"记到哪"一节；"等你"里的待处理（改投列表不含待办）；`dest.send`；`note.live` / `how` | F5-0、F5-0b、v0.4.0/v0.4.1 的菜单和"等你" | 固件 |
| H5 | HIL：离线记 3 条（待办、剪贴板、`file.sb`）→ 插上 Mac：待办和文件自动投递，剪贴板只进待处理、按 Enter 后剪贴板里是 UTF-8 中文；在线 Opt+C 立即复制；`cpa dest rm file.sb` 后离线记的那条降级成今日笔记 | 以上全部 | 真机 👤 |

本分支（D5-A）的完成检查（都在 worktree 根目录下跑）：

```bash
cd daemon && uv run ruff check . && uv run ruff format --check . && uv run pytest --cov=cpad --cov-fail-under=80 -q
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/tools -q
uv run --with 'jsonschema>=4.23,<5' python protocol/tools/check_vectors.py
python3 tools/repo/check_sizes.py --warn 400 --fail 800 protocol tools daemon/src daemon/tests
uvx lizard==1.24.1 -l python -L 50 -w daemon/src
```

## 15. 风险与待验证

| 风险 | 应对 |
|---|---|
| 每个新 schema 都让固件 CI 变红 | §7.8：D5-A 的 1 个和 F5-0 同批，D5-B 的 2 个和 F5-0b 同批；D5-A 不碰固件 |
| D5-B 之前，离线的剪贴板、失败的投递到不了设备 | 它们照样落档（今日笔记那一行、`dest.decided` / `dest.failed`），D5-B 一合并，`fold_pending` 从 `events.jsonl` 里算出来就推给设备 |
| v0.4.1 把 `dest` 定成枚举 | §1.3 第 1 条，开工前和 v0.4.1 负责人确认 |
| `pbcopy` 在 LaunchAgent 下的中文 [待] | 显式 UTF-8 locale；H5 用中文实测 |
| LaunchAgent 能不能写 `~/Notes/...` [待] | `~/Notes` 不在 TCC 名单里 [推]；写不进去返回 `E_DEST_IO`，条目已经落档，进待处理 |
| 笔记库会被同步上云 | `file.*` 默认算"离开 Mac"，要输入 `y`；审计可查 |
| 在线打字复制的"刚按"判断 | 设备 `live` 标记 + 同一个 boot + 30 s 窗口，三条都满足才算；旧固件一律 `queued`（安全的一边）|
| 待处理太多 | 快照只发最旧的 8 条加总数；24 小时过期 |
| 两个 Mac（v0.8 之前）| 每台 Mac 推自己的目录，`rev` 是内容哈希不会混；离线条目会被下一台插上的 Mac 投递（docs/13 §13 第 7 条的现状）|

## 16. 需要你拍板

你不回复，就按默认值做。

1. **可绑定的字母**：`b c f j k o u x y`，默认只有 C = 剪贴板。**默认：同意**。
2. **全局 Opt+字母 = 记一条去那里**（和 Opt+N/T/Q/S 一样），所以菜单"去哪"页的每个去向都能按 Enter。**默认：同意**。
3. **在线打字按 Opt+C 立即复制**（#24 的"在线马上执行"），离线的和语音的要到"等你"按 Enter。**默认：同意**。
4. **待处理 24 小时过期**（只丢投递，今日笔记那一行还在）。**默认：同意**。
5. **追加到文件默认算"离开 Mac"**，除非你在 `cpa dest add` 时加 `--local`。**默认：同意**。另外请告诉我 Obsidian 收件箱写到哪个库的哪个文件（§18 第 7 条），没有回复就先不建。
6. **今日笔记那一行不标去向**，去向看 `cpa audit`。**默认：同意**。

## 附：和调研稿、docs/13 相比定下来的差异

| 调研稿 / docs/13 | 这里 | 为什么 |
|---|---|---|
| id `agent.spawn`、`ww.plugin.<id>` | `session`、`plugin.<名字>`；id 的点号前就是种类 | 一眼看出种类，事后改不成别的种类 |
| 条目带 `drev`、`prof` | 不带 | id 自带种类，改名不会送错；情境是 v0.8 |
| 外发类每次确认 | 开启过的自动投递，每次审计 | docs/13 §9.2 的定稿规则 |
| `pending.snapshot` 预览 80 B | 48 B | 屏幕放不下；转义最坏情况也能放进一行 |
| `dest.result` 新类型、4 个新错误码 | 用 `ack` / `error` 和现有错误码 | 少一个类型，固件不用加错误码 |
| 快照 `rev` 是计数器 | 内容哈希 | 设备会缓存快照，daemon 重启后计数器会撞 |
| 今日笔记行尾标"→ 去向" | 不标 | 不改 `notes/render.py`；无 `dest` 的行和 v0.3 逐字节相同 |
| `file.append` 路径规则 | 再加：只许 `.md` / `.txt`、不许隐藏段、`~/Library` 整个拒绝 | 堵住"往 `~/.zshrc` 追加一行"这类代码执行 |

## 附：证据索引

- #24（需求和已定规则）；#19（主机身份，"Cheap now"）。
- docs/13 §2 原则 2、§5.2（保留字母）、§6.2（菜单只做按键重放）、§7.1（锁屏）、§9、§10.2（在途通道）、§11、§12.2（不新增错误码）、§13、§18 第 6、7 条。
- 固件：`lib/input/keymap.cpp:43-100`（现有绑定）；`lib/protocol/messages.h:48`（`kMsgTypeCount`）；`test/core/test_codec_vectors/test_main.cpp:56、76`（schema 数和向量数的断言）；`.github/workflows/ci.yml`（`pio test -e native`）。
- daemon：`notes/render.py:18、22`（`clean`、`block_id`）；`notes/writer.py:55`（`_append`）；`hub/snapshots.py:34、38、129`（`truncate_utf8`、`label`、`publish`）；`protocol/codec.py:118、131`（`serialize`、`fit_list`）；`protocol/specs.py:77、314`（`request()`、`SPECS`）；`protocol/validate.py`（`obj()` 允许多余字段）；`protocol/messages.py`（`_pick` 丢掉不认识的字段、`Unhandled`）；`events/records.py:21、43`；`queue/fold.py`；`cli/main.py:148、162、206`；`config/files.py`；`recordings/records.py:66`（`rec.result` 带 `memo_text`）；`app.py:326、345`（按 mtime 重载）。
- 协议：`protocol/README.md`（版本规则、接收端判定顺序）；`schema/v0/common.schema.json`（`revs`、`errorCode`）；`tools/schemas.py:113`（`x-uniqueBy` 只支持一个键）；`tools/test_worst_lines.py`（最坏行的写法）。
- 行长：§7.6 的数字由 `json.dumps(..., ensure_ascii=False, separators=(",", ":"))` 加换行实算。
