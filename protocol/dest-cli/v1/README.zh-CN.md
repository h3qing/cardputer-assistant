[English](README.md) · **中文**

# 从 Mac 送的契约 v1（`cpa dest … --json`、`cpa filing send --json`）

"等你送"和从 Mac 送到 Grok Bot 等任何去向（docs/25 §5.4、§5.5）的对外接口：Mac app（docs/24）和你的脚本用 `execFile` 跑 `cpa`，读它打印的一份 JSON。**`cpa` 是唯一的写入方**：状态在 `$CPA_HOME/events.jsonl` 和 `destinations.json`；本契约只管 `cpa` 打印出来的形状。规范来源：[docs/25-route-to-any-app.md](../../../docs/25-route-to-any-app.zh-CN.md) §3.1、§3.4、§5.4、§5.5、§7.4。

```
protocol/dest-cli/
├── v1/
│   ├── README.md                 # 本文件
│   ├── schema/                   # pending / sent / dropped / usual / tested / error + common（Draft 2020-12）
│   ├── vectors/valid/*.json      # 必须被接受的文档
│   ├── vectors/invalid/*.json    # 必须被拒绝的文档，每个带拒绝原因
│   └── VECTORS.sha256            # 每个向量文件的 sha256
└── tools/check_dest_cli.py       # 参考校验器 + 契约自检，测试在同目录
```

```bash
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/dest-cli -q
uv run --with 'jsonschema>=4.23,<5' python protocol/dest-cli/tools/check_dest_cli.py   # 退出码 0
```

daemon 的 `tests/contract/test_dest_cli_contract.py` 让每个命令真跑一遍（假的 LaunchServices 和剪贴板），输出都要过 `check_dest_cli.py`。schema 只用 WhisperWoof 校验器认识的关键字（和 `protocol/filing/v1` 同一份允许列表），所以一个校验器读两份契约。

## 命令和文档

| 命令 | 成功时打印 | 写什么 |
|---|---|---|
| `cpa dest pending [--expired] --json` | `pending` | — |
| `cpa dest send REF [DEST] [--again] [--by cli\|app] --json` | `sent` | `dest.decided{origin:"mac", how:"pick", attempt}`，再 `dest.handed` / `dest.delivered` / `dest.failed` |
| `cpa filing send ID DEST [--again] [--by cli\|app] --json` | `sent` | 同上 |
| `cpa dest drop REF [--by cli\|app] --json` | `dropped` | `dest.dropped` |
| `cpa dest usual [PROJECT [DEST\|-]] --json` | `usual` | 改了时：`destinations.json` 的 `usual`，加 `dest.changed{op:"usual", project, id}` |
| `cpa dest test ID --send --json` | `tested` | 交出去了时：`dest.handed{item:"test-NNNN"}` |
| 以上任何一个被拒 | `error` | — |

- **`sent`**：`outcome` 是 `handed`（交给了别的 app：`via:"url"` 预填链接，`via:"clip"` 剪贴板 + 把 app 调出来；它收没收不知道）、`delivered`（本机文件、剪贴板去向）或 `failed`（`code`，退出码 1，这条留在 `pending` 里；`E_INTERRUPTED`：送的时候出错或被打断，比如 app 的 30 秒超时杀掉了 `cpa`，这时也先写下 `dest.failed` 再退出）。一条从 Mac 送、已经 `running` 超过 60 秒的（进程没了，连 `dest.failed` 都没来得及写），`pending` 当它 `failed`、`E_INTERRUPTED` 列出来，可以再送或不送。`why` 说链接为什么改走剪贴板：`too_long`（D9）、`draft_busy`（同一个 Bot 30 分钟内填过一条，D10）。从不带正文。Mac app 的提示照它写："已填进 Grok · 到 Grok 里按回车发出"（`handed` + `url`，5 秒）、"在剪贴板 · 2 分钟内到 Grok 按 ⌘V"（`handed` + `clip`，一直留着，直到你关掉；带"再复制"，就是 `--again`）。
- **`pending`**：`rows[]` 每行 `ref, why, dest, label, kind, fl, st, code, project, src, at, text`。`why`：`held`（`st` 是 `hold` 或 `failed`，`failed` 带 `code`）、`usual`（项目的常用去向预选的建议，D21：从不自己送）、`expired`（只在 `--expired` 时）。`dest` 是预选的去向，`⌘↩` 就是不带 DEST 的 `cpa dest send REF`。`text` 过了 `display_safe()`，最多 280 字。`fl` 是去向的标志串（docs/25 §2.2），读不懂当 `lck`；去向删掉了时 `label` 是它的 id、`fl` 是 null。
- **`tested`**：一次性测试（docs/25 §3.1，D12）。`handed:false` + `E_DRAFT_BUSY`：这个 Bot 30 分钟内被真的便条填过草稿，测试会被它悄悄丢掉，所以没交；先在那边发掉或清空。`--json` 什么都不问；调用方自己问"出现'测试 <marker>'了吗？""自动发出去了吗？"，再跑 `cpa dest verify ID --draft`（出现了、没发）或 `--act`；没出现时让用户贴 Bot 的链接，跑 `cpa dest map ID '*=链接'` 再测。
- **`error`**：形状和 `protocol/filing/v1` 的 `error` 一样，`error` 是可扩展枚举（不认识的照原样显示）。`field` 只说是哪个参数，从不回显值。退出码 2：`E_BAD_ARG`、`E_UNKNOWN_DEST`、`E_UNKNOWN_PROJECT`；其余 1。

| `error` | `field` | 什么时候 |
|---|---|---|
| `E_BAD_ARG` | `id` | 不是录音 id；或者一场会议送到今日笔记、待办、本机文件以外的地方 |
| `E_BAD_ARG` | `ref`、`dest`、`project`、`argv` | 不是等你送里的 id；不是合法的去向 id，或者是 `daily`、`todo`、`queue`、`session`（交给 AI 用 `cpa filing hand`）；不是项目 key；argparse 不认的参数 |
| `E_UNKNOWN_ID` | `id`、`ref` | 没有这段已归档的录音；等你送里没有这一条（包括刚被另一个 `cpa` 送掉或不送了的）|
| `E_UNKNOWN_PROJECT` | `project` | 不是这台 Mac 登记过的项目 |
| `E_UNKNOWN_DEST` | `dest` | 没有这个去向 |
| `E_DEST_OFF` | `dest` | 关着、还没允许离开 Mac，或者这个版本没有执行器（`paste`）|
| `E_OTHER_PROFILE` | `dest` | 这个去向是另一个档案的（docs/18）|
| `E_NO_TEXT` | `text` | 还没有便条正文（转写开着吗？`cpa-stt use`）|
| `E_TOO_LONG` | `text` | 要在设备上确认的去向，语音超过 40 s 或正文超过 500 B |
| `E_NEEDS_DEVICE` | `dest` | 这个去向可能自己发出去（`fl` 含 `k`）：只认设备上的确认；只填草稿的话先 `cpa dest test ID --send` 测一次 |
| `E_BUSY` | `ref` | 这一条正在送 |
| `E_ALREADY` | `dest` | 这一条已经交给（或写进）这个去向了；加 `--again` 再送一次。同一条不会因为多按一下就填两次 |
| `E_UNAVAILABLE` | `dests` 或没有 | `destinations.json` 用不了；或读不了配置、日志 |

被拒时什么都不写。带 `--json` 时 argparse 不认的参数也打印 `error`（`E_BAD_ARG`，`field:"argv"`，退出码 2）；**退出码 2 而 stdout 是空的**，说明这个 `cpa` 比这份契约还旧。
