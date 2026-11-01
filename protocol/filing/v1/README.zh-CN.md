[English](README.md) · **中文**

# 归档契约 v1（`cpa filing … --json`、`cpa project … --json`）

"待归档"（docs/23）的对外接口：别的程序（WhisperWoof 的 Cardputer 页、脚本、终端）用 `execFile` 跑 `cpa`，读它打印的一份 JSON。**`cpa` 是唯一的写入方**：状态只在 `$CPA_HOME/events.jsonl` 里，每个动作一条记录；本契约只管 `cpa` 打印出来的形状。规范来源：[docs/23-mac-inbox.md](../../../docs/23-mac-inbox.zh-CN.md) §2.2、§3.1–§3.4；本目录是它的机器可读版本。

```
protocol/filing/
├── v1/
│   ├── README.md                 # 本文件
│   ├── schema/                   # ls / show / result / error / projects / host / row 的 JSON Schema（Draft 2020-12）+ common
│   ├── vectors/valid/*.json      # 必须被接受的文档
│   ├── vectors/invalid/*.json    # 必须被拒绝的文档，每个带拒绝原因
│   └── VECTORS.sha256            # 每个向量文件的 sha256（WhisperWoof 复制向量时核对）
└── tools/check_filing.py         # 参考校验器 + 契约自检，测试在同目录
```

```bash
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/filing -q
uv run --with 'jsonschema>=4.23,<5' python protocol/filing/tools/check_filing.py   # 退出码 0
```

daemon 的 `tests/contract/test_filing_contract.py` 让每个子命令真跑一遍，输出都要过 `check_filing.py`；`cpa export` 的 `data/filing.jsonl` 每一行按 `row` 校验；MCP 的 `list_unfiled`、`get_filing_item` 的结果分别按 `ls`、`show` 校验。

**MCP**（2026-10-22，docs/23 P1-E）：`cpa mcp` 的只读工具 `list_unfiled` 返回 `ls` 这份文档，`get_filing_item` 返回 `show`，各加一个 `trust`（docs/15 §3.7）；超过输出上限时加 `truncated:true`（`ls` 少给几行，`next_cursor` 从最后一行后面接着；`show` 截断 `sentences`，加 `next_offset`）。和 CLI 的差别：`filing` 不能是 `hidden`，藏起来的从不返回；`fw` 永远是 null；`prof`、`clips` 是 null；行里每个字段按允许列表取。没有 MCP 写工具。

## 命令和文档

| 命令 | 成功时打印 | 写什么（一个动作一条，或者什么都不写） |
|---|---|---|
| `cpa filing [ls] --json [筛选]` | `ls` | — |
| `cpa filing show ID --json` | `show` | — |
| `cpa filing file ID KEY\|--none [--other] --json` | `result` | `rec.edited{project, by:"cli"}` |
| `cpa filing done ID --json` | `result` | `rec.edited{done:true, by:"cli"}` |
| `cpa filing hide ID --json` | `result` | `rec.hidden{by:"cli"}` |
| `cpa filing reopen ID --json` | `result` | 撤掉当前状态的那一个原因：`rec.unhidden`、`queue.done{qid, by:"undo"}`（还没开 session 的排队项）或 `rec.edited{done:false}` |
| `cpa filing hand ID [KEY] [--spawn] [--other] --json` | `result` | `queue.added{origin:"inbox", from_item:ID, project, intent:"todo"\|"spawn", voice:true, bytes, sha8}`（见下面"交给 AI"） |
| `cpa project ls\|list --json` | `projects` | — |
| `cpa project add [--key K] [--slot N] [--folder 目录] --json -- 名字` | `projects`（加完以后的） | `projects.json` |
| `cpa project rename KEY --json -- 新名字` | `projects` | `projects.json` |
| `cpa project move KEY N --json`（N 上原来的项目换到 KEY 原来的号） | `projects` | `projects.json` |
| `cpa project folder KEY 目录\|--none --json` | `projects` | `projects.json` |
| `cpa project rm KEY --json`（录音和排队项保留这个 key） | `projects` | `projects.json` |
| `cpa host name --json [-- 名字 \| --clear]` | `host`（读、设、清以后的名字） | `config.json` 的 `host_label`（不写事件） |
| 以上任何一个失败 | `error` | — |

（2026-10-22，docs/25 R1-D）`cpa filing send ID DEST --json` 不在本契约里：它和 `cpa dest send` 是同一个命令，打印 [`protocol/dest-cli/v1`](../../dest-cli/v1/README.zh-CN.md) 的 `sent` 或 `error`（`error` 的形状和这里一样，错误码多几个）。

- `row` 不单独打印：它是 `ls` 的 `rows[]`、`show` 的 `row`，也是 `cpa export` 里 `data/filing.jsonl` 的一行（那里藏起来的也在）。
- 不带 `--json` 时打印给人看的几行（不在契约里）；写命令最后一行是撤销命令，如 `undo: cpa filing file 3QZK1M7-00042-0A1F --none`。
- `cpa filing triage`（2026-10-22，docs/23 P1-F）没有 `--json`，不在契约里：它在终端里一条一条过，按键写的就是上表 `file`、`done`、`hand` 的记录，撤销是 `reopen` 或 `file` 回到之前的项目，一样带 `by:"cli"`。
- 退出码：0 成功（包括 `changed:false`）；2 参数不对（`E_BAD_ARG`、`E_UNKNOWN_PROJECT`）；1 这条录音的状态不允许（其余错误码）。
- 带 `--json` 时，连 argparse 不认的参数（这个 `cpa` 还没有的选项或命令，例如以后的 `--net`、`--prof`）也打印 `error`：`E_BAD_ARG`、`field:"argv"`，退出码 2（2026-10-22）。**退出码 2 而 stdout 是空的**，说明这个 `cpa` 比这份契约还旧：更新 `cpa`。

### 筛选（`ls`）

默认只列"新"、不含别的电脑的项目，新的在前，一页 200 行。

| 参数 | 意思 |
|---|---|
| `--filing new\|filed\|handed\|done\|hidden\|all` | 状态；`all` 是除了藏起来以外的全部 |
| `--orphans` | 加上别的电脑的项目那一组（`asked` 不为 null 的新行） |
| `--from` / `--to YYYY-MM-DD` | 录音目录的日期，两端都含 |
| `--device ID` | 行里的 12 位短 id |
| `--link here\|unknown` | 录音时是不是连着这台 Mac；没有戳的算 `unknown` |
| `--mode memo\|meeting` | |
| `--project KEY`（可以给几次）\| `--none` | 这台 Mac 登记过的 key；`--none` 是没有项目的 |
| `--tx …`（可以给几次） | 转写状态，取值见 `row.tx` |
| `--text 字` \| `--text -` | 字面子串、不分大小写，搜标题、便条正文和整段转写；`-` 从 stdin 读（1–200 字），WhisperWoof 的搜索词走这里，不进 argv |
| `--limit N`、`--cursor C` | 每页 1–1000 行；`C` 是上一页的 `next_cursor`，筛选要和上一页一样 |
| `--no-text` | `title`、`text` 给 null |

给人用的两个便利（2026-10-22，只在命令行，不在契约里，MCP 没有）：只给 `--project` 不给 `--filing` 时按 `all` 列（"新"的行从来没有项目，不然总是空的）；`--from`、`--to` 也认 `today`、`yesterday`（这台 Mac 的本地日期）。程序调用请照样明确给 `--filing` 和日期。

`counts` 不受筛选影响：`new` 是这台 Mac 的新行，`other_mac` 是别的电脑的项目那一组的新行，`clips_waiting` 在语音采集（#52）合并前是 0，`on_device` 是设备上还没拉过来的段数，只问正在跑的 cpad，没在跑时是 null。

### 写命令的规则（docs/23 §3.3）

1. **argv 里只有 id、项目 key、枚举**，每个都先校验：id 是 MonoId（不分大小写，回给你的是大写）；key 符合 `^[a-z0-9][a-z0-9-]{0,11}$` 并且是这台 Mac 登记过的，唯一的例外是这条录音自己带过的 key：录的时候按的，或者之前给它设过、后来这台 Mac 删掉了的（只有撤销会用到它）。正文从不进 argv：`hand` 的正文由 `cpa` 自己从录音取。
2. **一个动作一条记录**：在 `events.jsonl` 的文件锁里按锁住时的日志**再决定一次**再追加（别的进程刚写的也算进去），fsync 以后才回答。cpad 没在跑也能写。CLI 从不写今日笔记，归档后的那一行由 cpad 写（docs/23 §2.4）。
3. **幂等**：已经是这个状态就不写，`changed:false`、`undo:null`。
4. **拒绝**（`error` 文档，不回显输入，`field` 只说是哪个参数）：
   - `E_BAD_ARG`：id、key、枚举、日期、游标不合法；`file` 既没给 KEY 也没给 `--none`（或两个都给了）；别的电脑的项目那一组不带 `--other`（`file`、`hand`，`field:"other"`）；`hand` 一场会议（`field:"id"`：会议里有别人的话，docs/23 D16）。
   - `E_UNKNOWN_ID`：没有这段已归档的录音（还在拉取的也算没有）。
   - `E_UNKNOWN_PROJECT`：这台 Mac 没有这个 key；`hand` 没给 KEY，录音的有效项目又不是这台 Mac 的。
   - `E_ALREADY`：状态不允许：`done` 只给"新"的（已归档、已交给 AI、藏起来的回 `field:"filing"`）；有打开的排队项时 `file`、`hide`、`hand`（`field:"queue"`：排队项保留它的项目，先 `reopen` 撤回）；`hand` 一条藏起来的（`field:"filing"`）；`reopen` 一条已归档的（改项目用 `file`，退成没项目用 `file --none`，`field:"filing"`）或排队项已经开了 session 或做完了的（`field:"queue"`）。
   - `E_UNAVAILABLE`：`config.json` 读不了，或者 `projects.json` 读不了（`cpa project ls`，`field:"projects"`）；`hand` 时正在跑的 cpad 比这个 `cpa` 老、或者没回答（`field:"cpad"`：老的 cpad 不会把这一项按 plan 开；先 `cpa service install` 重启）。
   - `E_NO_TEXT`：`hand` 的录音还没有能用的字（`field:"text"`）：没有便条正文也没有转写，或者清洗、截断以后是空的、以 `-` 开头。
5. **撤销**：`undo` 是中立的对象 `{op, id, args}`，调用方用自己的动词表重建命令、再校验 id，不照抄。`file` → `{op:"file", args:{project:之前的有效项目或 null}}`（`project` 一定在；之前的值根本不是 key 时给 null）；`done`、`hide`、`hand` → `{op:"reopen"}`；`reopen` → `{op:"done"}` 或 `{op:"hide"}`；撤回排队项、或者撤销会被 `hide` 拒绝（有打开的排队项）时没有撤销（`undo:null`）。撤销本身又是一条普通事件。连着撤销（新的在前，一步一个）每一步都回到那个动作之前的状态。

### 交给 AI（`hand`，docs/23 §3.4）

- **只排队**：写一条 `queue.added`，`origin:"inbox"`、`from_item` 是录音 id、`project` 是 KEY（不给就是有效项目）、`intent` 是 `todo`，加 `--spawn` 是 `spawn`。开 session **只在设备看板上**选中它按 Enter（ADR 0002），不管项目配的什么模式**一律 plan**；看板上的摘要以"语音·"开头。Mac 上开不了。
- **正文 `cpa` 自己取**：便条正文（`memo_text`），没有就把 `transcript.json` 的句子用空格接起来；经 `queue_text()`：`display_safe()`、去首尾空白、截到 500 B（不截半个字，不加后缀）；截完是空的或以 `-` 开头回 `E_NO_TEXT`。`voice:true`，`bytes` 是正文的 UTF-8 字节数，`sha8` 是它 sha256 的前 8 位十六进制。`cpa filing show ID` 的 `queue_text` 是按 qid 读回的这段正文。
- **只收便条**：会议回 `E_BAD_ARG`（`field:"id"`），它的转写从不读进来。
- 一段录音同时只有一个打开的排队项；排队项开过 session 或做完以后可以再交一次。
- 撤销是 `reopen`：`queue.done{qid, by:"undo"}`，老的 cpad 也会关掉这一项；已经开了 session 的不能撤（`E_ALREADY`，`field:"queue"`）。设备正在开它（开完才记 `queue.spawned`）时撤回的，开完以后仍算已交给 AI，`open:false`，不能再交一次。
- `result.queue` 是交完以后的 `{qid, intent, open}`。

## 字段

完整定义见 `schema/`，例子见 `vectors/valid/`。

- **row**：`id`、`kind`、`filing`、`why`、`asked`、`at{wall_ms, tz_min, uncertain}`、`dur_ms`、`device`、`env`、`prof`、`project`、`starred`、`tx`、`title`、`text`、`intent`、`clips`、`queue`，**每个字段都在**。
  - `filing`：`new`、`filed`、`handed`、`done`、`hidden`（封闭的枚举）。不叫 `state`：MCP 的 `list_recordings` 里 `state` 是转写状态。
  - `project`：有效项目，只在它是这台 Mac 的 key 时给，否则 null；`asked` 只出现在别的电脑的项目那一组的新行里，是录音时按的 key。
  - `device`：`HMAC-SHA256(id_key, "dev|"+dev)` 的前 6 字节（12 位十六进制）。**从不给设备自己的 id**（出厂 MAC 地址）；每台 Mac 一把 `id_key`，两台 Mac 上的 id 连不起来。cpad 第一次启动这一版之前 `id_key` 还没有，这时是 null。
  - `env`：Mac 端的戳（docs/23 §4.2）`{host, link}`，两个字段都在（`host` 没起名时是 null，`link` 不是枚举词时是 null），老录音整个为 null。永远没有网络。
  - `title` ≤80 字、`text` ≤280 字（按码点，不按 UTF-16：U+20000 和表情符号都算一个），都过 `display_safe()`：没有 C0、DEL、C1（U+0080–009F）、U+2028/2029、双向控制符（U+202A–202E、U+2066–2069）和单独的代理码点（U+D800–DFFF，转写程序截断的半个表情符号；`pattern` 用 `u` 标志匹配时一对代理码点是一个字符）。`text` 是便条正文，没有时是转写的前几句。
  - `prof`（v0.8 的情境档案）和 `clips`（语音采集）在那之前是 null，schema 不限定它们的类型。
  - `queue`：交给 AI 以后的 `{qid, intent, open}`；`filing:"handed"` 时必有。
- **show**：`row`、`text`（完整的便条正文，≤2000 字）、`sentences`（`[{t_s, text}]`，没有转写时 null）、`todos`（纪要里的待办，≤20 条）、`audio`（`seg-NNN.wav` 文件名，不带目录；30 天清理后为空）、`fw`、`queue_text`（交给 AI 的排队正文，≤500 字）。所有文字都过 `display_safe()`。
- **result**：`ok:true`、`id`、`filing`（动作之后的状态）、`changed`、`undo`，和 `queue`（动作之后这段录音的排队项，同 `row.queue`；`hand` 以后就是新排的那一项）。
- **error**：`ok:false`、`error`（上面的固定错误码）、可选的 `field`（`^[a-z][a-z0-9_]{0,15}$`）。
- **projects**：`[{key, name, slot, ok, linked, folder}]`，按 `slot` 排，和设备上的 1–9 一致；`name` 过 `display_safe()`；**不给本机路径**；`ok:false` 表示连着的项目目录不在了。
  （2026-10-26，docs/25 §8）项目先是一个**有名字的分组**，代码目录可有可无：`linked` 说连没连目录，`folder` 是那个目录路径的最后一段（≤64 字，不含 `/`），没连时是 null；老的 `cpa` 不给这两个字段，读的一方当作 `linked:true`、`folder:null`。只有连了目录的项目才有 hook、按 cwd 认项目和开 session（没连的项目在设备上开 session 回 `E_UNKNOWN_PROJECT`）。
  写命令（`add`、`rename`、`move`、`folder`、`rm`）成功时打印改完以后的这份文档；失败时是 `error`，什么都不写：`E_BAD_ARG`（`field` 是 `name`、`key`、`slot`、`folder`、`mode` 或 `argv`，退出码 2）、`E_UNKNOWN_PROJECT`（`field:"key"`，退出码 2）、`E_ALREADY`（key 或号已经有人用了、9 个号都满了，`field` 是 `key` 或 `slot`，退出码 1）、`E_UNAVAILABLE`（`projects.json` 读不了，`field:"projects"`，退出码 1）。名字放在 `--` 后面（可以是中文，1–24 字节，同 `cpa host name` 的规则）；不给 `--key` 时 key 从名字里的英文字母和数字来，没有的话是 `p<号>`；不给 `--slot` 时用第一个空号。
- **host**（2026-10-22）：`{v:1, host}`，这台 Mac 的名字，没起名时 null（默认）。读的时候打印现在的名字，设、清以后打印新的。名字 1–24 字节、不以 `-` 开头、不带 `display_safe()` 会去掉的字符（存的和显示的一样）；不合法时是 `error`（`E_BAD_ARG`，`field:"name"`，退出码 2），什么都不写。

## 向前兼容

- schema 里**没有** `additionalProperties:false`：读的一方忽略不认识的字段。
- **可扩展的枚举**（`row.kind`、`why`、`tx`、`env.link`、`intent`、`queue.intent`、`result.undo.op`）用 `x-knownValues` 标出、格式是 `^[a-z][a-z0-9_]{0,15}$`，照录音契约 `marks[].k` 的先例：读的一方遇到不认识的值，那一项照"未知"显示，不拒绝整行。以后会有 `kind:"capture"`、`link:"other"`。
- **只有 `v` 不是整数 1 时整份拒绝**（缺失、`2`、`"1"`、`true` 都算）；`row` 单独出现时（导出的一行）没有 `v`。
- 改变已有字段的含义或删除字段才升到 v2，新建 `v2/`。
- schema 只用 WhisperWoof 校验器认识的关键字：`check_filing.KEYWORDS` 是和它的 `contract-schema.js` 一样的允许列表（加上 `x-` 开头的注解），它在启动时对别的关键字一律抛错，所以 `oneOf`、`else`、`not`、`format`、`default`、`$comment`、`uniqueItems`、`additionalProperties` 等都不能用；自检按它走的地方（`properties`、`$defs`、`anyOf`、`allOf`、`items`、`if`、`then`）查。可以为 null 的对象和数组写成 `"type": [..., "null"]`，出错的路径才能指到里面的字段。

## 校验顺序（参考实现 `check_filing.validate`）

| 步骤 | 条件 | 结果 |
|---|---|---|
| 1 | 不是 JSON 对象 | `E_INVALID`，field `(root)` |
| 2 | `v` 不是整数 1（`row` 不查） | `E_VERSION`，field `v`；其余一概不看 |
| 3 | 不符合 schema | `E_INVALID`，field 是出错的路径，如 `rows[0].text` |

`pattern` 按 ECMA-262 理解（`$` 只匹配字符串末尾）。

## 向量格式

```json
{"doc": "ls", "name": "…", "data": {…}}
{"doc": "row", "name": "…", "reason": {"code": "E_INVALID", "field": "project"}, "data": {…}}
```

- `doc` 是 `ls` / `show` / `result` / `error` / `projects` / `host` / `row` 之一。
- 合法向量必须得到 `ok`；非法向量必须**恰好**得到 `reason` 里的那个 `code` 和那一个 `field`。
- `check_filing.py` 还要求：每种文档至少一个合法向量；至少一个合法向量里有一行带不认识的字段、又带不认识的 `kind`、`why` 或 `tx`；以及 `REQUIRED_INVALID` 里的非法用例都在：`v=2`（`ls`、`result`）、行 id 不是 MonoId、`text` 超过 280 字、`title` 带双向控制符、`text` 带终端转义、`text` 带单独的代理码点、`env.link` 不是枚举词、`env.host` 超过 24 字或带双向控制符、`asked` 不是 key、`device` 是 MAC 地址、未知的 `filing`、`handed` 却没有 `queue`、不是"新"的行带 `asked`、`project` 不是 key、转写句子带控制字符、`audio` 给的是路径、`changed:false` 却有 `undo`、未知的错误码、大写的项目 key、项目的 `folder` 给的是路径、电脑名带控制字符。

改动任何向量后重新生成摘要（在本目录下）：

```bash
shasum -a 256 vectors/valid/*.json vectors/invalid/*.json > VECTORS.sha256
```

WhisperWoof 把 `vectors/` 和 `VECTORS.sha256` 复制到自己的 fixtures，在 `SOURCE` 文件里写明来源提交（docs/23 P2-c）。
