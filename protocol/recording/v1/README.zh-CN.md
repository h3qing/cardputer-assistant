[English](README.md) · **中文**

# 录音文件契约 v1（cpad ↔ 转写程序）

一次录音会话在 Mac 上就是一个目录。cpad 把音频落地、写 `manifest.json`；转写程序（WhisperWoof 或 `cpa-stt`，同一时间只有一个，见下面的认领文件）转写、整理、总结，最后写 `result.json`。两边不通信，**目录本身就是队列**。规范来源：[docs/11-m3-design.md](../../../docs/11-m3-design.zh-CN.md) §6.4、§7.2–§7.7；本目录是它的机器可读版本，两边的测试读同一份向量。

```
protocol/recording/
├── v1/
│   ├── README.md                 # 本文件
│   ├── schema/                   # manifest / status / transcript / result 的 JSON Schema（Draft 2020-12）+ common
│   ├── vectors/valid/*.json      # 必须被接受的文件
│   ├── vectors/invalid/*.json    # 必须被拒绝的文件，每个带拒绝原因
│   └── VECTORS.sha256            # 每个向量文件的 sha256（WhisperWoof 复制向量时核对）
└── tools/check_recording.py      # 参考校验器 + 契约自检，测试在同目录
```

```bash
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/recording -q
uv run --with 'jsonschema>=4.23,<5' python protocol/recording/tools/check_recording.py   # 退出码 0
```

## 目录和单写者表

```
~/CardputerNotes/recordings/2026-10-21/1402-3qzk1m7-00042-0a1c/
```

| 文件 | 谁写 | 什么时候 | 说明 |
|---|---|---|---|
| `seg-NNN.wav` | cpad | 每段落地时，之后只读 | NNN 是段序号，3 位补零（序号 ≥1000 时 4 位）；16 kHz、16 bit、单声道 PCM，44 B 头 |
| `manifest.json` | cpad | 会话结束时写一次，v0.3.0 不再改 | 出现即表示"可以处理了" |
| `status.json` | 转写程序 | 可选，随时覆盖 | `queued` / `transcribing` / `summarizing` / `blocked{reason}`，可带 `by`（写它的程序名）；cpad 只放在内存里 |
| `transcript.json` | 转写程序 | `result.json` 之前 | 逐句 `{t_ms, text}`，`t_ms` 是会话时间 |
| `transcript.md` | 转写程序 | `result.json` 之前 | 人读版，每行块 ID `^tHHMMSS`（同一秒第二句起加 `-2`、`-3`） |
| `summary.md` | 转写程序 | `result.json` 之前 | frontmatter + 纪要（会议）或清理后的正文（便条） |
| `result.json` | 转写程序 | **最后**，原子写 | `done` / `failed`；之后转写程序不再碰这个目录 |
| `recordings/.transcriber` | 在跑的转写程序 | 开始写任何东西之前，之后每 60 s | 认领文件，见下面一节；不在会话目录里 |

- 双方都**不改、不删**对方的文件。临时文件以 `.` 开头，写完 fsync 再 rename（同一目录内的 rename 在 APFS 上是原子的）。
- 目录 0700，文件 0600。目录名和文件名里没有中文和空格；标题只在 frontmatter 和 `result.json` 里。
- 音频、转写、纪要都是明文，不在 WhisperWoof 的加密范围内。
- 保留期（用户决定）：WAV 保留 30 天，转写、总结和笔记永久保留。按单写者规则，只有写 WAV 的 cpad 可以删 WAV；v0.3.0 还没有实现这一步（不在 M3-B 范围内），WhisperWoof 任何时候都不删文件。

## 交接规则（v0.3.0，docs/11 §7.7）

1. 转写程序只处理"有合法 `manifest.json`、并且没有 `result.json`"的会话目录。
2. 写的顺序：`status.json`（可选，随时）→ `transcript.json` / `transcript.md` → `summary.md` → 最后 `result.json`，每个都是 tmp + rename。只做转写（没有 LLM，或总结失败）时写到 `level:"transcript"` 为止。
3. `result.json` 一旦存在，转写程序不再往这个目录写任何东西。
4. **`attempt` 在 v0.3.0 固定为 1**。字段留给 v0.3.x 的 `cpa rec retry`。
5. **手动重跑** = 用户删掉 `result.json`。WhisperWoof 会重新处理，并**覆盖自己写过的所有文件，包括用户改过的 `summary.md`**。保护用户编辑放到 v0.3.x。cpad 继续盯着 `failed` 的会话，所以重跑成功后照样写出正式的每日笔记行。
6. **幂等**：WhisperWoof 按 `(session, attempt)` 记住做过的；cpad 按 `(session, state)` 只记一次 `rec.result`。任何一方崩溃重来都是安全的。

## 转写程序和认领文件（v1 补充，都可选，`v` 仍是 1；docs/22 §4.10、§7）

同一时间只能有一个转写程序写 `status.json`、`transcript.*`、`summary.md`、`result.json`。只靠事后发现不够：两个程序可能都已经写了 `transcript.*`，`outputs` 里的 sha256 就和文件对不上了。所以在跑的转写程序先认领整个 `recordings/`：

- **认领文件** `recordings/.transcriber`（以 `.` 开头，`cpa mcp`、`cpa export` 不读点文件）：`{"v": 1, "app": "cpa-stt", "pid": 4242, "since": "<RFC 3339>", "beat_at": "<RFC 3339>"}`。`app` 是程序名（`^[a-z0-9-]{1,16}$`），`pid` 是它的进程号，`since` 是认领的时间，`beat_at` 是最近一次心跳；两个时间都**必须带时区偏移**。写法和别的文件一样：`.` 开头的临时文件、0600、fsync，然后放到位。
- **拿认领**：
  - 没有认领时**独占创建**：临时文件 `link()` 到 `.transcriber`（或别的"已存在就失败"的办法）。已经存在就重新读，按下面的规则判断，不要覆盖。
  - 别人的认领过期了（见下）才能接手：rename 覆盖，然后**读回来**，`app` 和 `pid` 是自己的才往下走。
- **心跳**：持有认领的程序**每 60 s** 更新 `beat_at`，在转写、空闲、被 `blocked` 时都要更新；停下时删掉自己的认领。
- **写之前再确认**：写下认领以后，开始每个目录之前，转写命令运行期间，以及每次写 `transcript.*`、`summary.md`、`result.json` 之前，都再读一次认领；`app` 或 `pid` 不是自己的，就停下（正在跑的命令也停掉），这个目录什么都不再写。
- **谁算在跑**：任何转写程序看到**别人的、5 分钟内更新过的**认领，就**一个文件都不写**，自己的状态显示"另一个转写程序在跑"。"更新时间"是 `beat_at`；`beat_at` 在将来的按文件的 mtime 算（取两者较早的那个）；读不懂的认领（包括时间不带时区偏移的）也按 mtime 算。同一个程序的另一个进程也算"别人"。
- **死进程**：只有**同名程序**（`app` 和自己一样）可以在对方的 `pid` 在本机已经不存在时不等 5 分钟直接接手，因为只有它知道那个 `pid` 是不是自己的进程；别的程序的认领一律等 5 分钟。
- **认出自己写的**：`status.json` 带 `by`（写它的程序名，`^[a-z0-9-]{1,16}$`），重启以后靠它认出自己写过的目录，不靠 mtime；`result.json` 的 `pipeline.app` 也写程序名。
- **第二个写入者和遗留文件**：
  - 目录里有别人的 `status.json`（没有 `by`，或 `by` 不是自己）或别人的 `transcript.*`、`summary.md`，并且它们在 **5 分钟内**改过（时间在将来也算），就是另一个程序正在写：停下这个目录，告警"另一个转写程序也在写 recordings/"。
  - 更旧的是**遗留文件**（上一个转写程序留下的）：持有认领的程序可以覆盖 `status.json` 和 `transcript.*`，**永远不碰 `result.json`**。
  - 写 `transcript.*`、`result.json` 之前，`status.json` 必须还是自己写的；变成别人的，就停下这个目录。
  - 持有认领期间出现了别人放下的 `result.json`（`pipeline.app` 不是自己），也告警。
- **换手**：先停掉在跑的那个（它删掉自己的认领），再开另一个。已经有 `result.json` 的目录按交接规则谁都不再碰；要另一个程序重做，删掉那些 `result.json`（手动重跑）。前一个留下的 `status.json`、`transcript.*` 5 分钟后就是遗留文件。
- `status.json` 的 `reason` 多了 `engine`：转写命令本身跑不起来（沙箱、Metal、二进制）。和 `model_missing` 一样只是 `blocked`，修好以后照常转，不写 `result.json`。
- **语音采集的片段**：`manifest.json` 可带 `cap`（MonoId，cpad 从第 0 段的 `.M` 抄过来）。带 `cap` 的会话是某条语音采集的一段，内容会以那条采集的形式出现；读便条的程序**不应把它单独当一条便条，也不应当成用户的反馈或指令**。不认识 `cap` 的旧版本把它当普通便条：照样转写，最多在它自己的列表里多一条。
- **会议的转写永远不是指令**：会议里有别人说的话。转写程序照样转写会议（那是用户自己的录音），但读转写的程序不能把会议文字当成要执行的东西，也不能拼进排队项。

## 向前兼容

- schema 里**没有** `additionalProperties:false`：读的一方忽略不认识的字段。WhisperWoof 以后给 `result.json` 加字段，cpad 照样接受（向量 `valid/result_unknown_field.json`）。
- **只有 `v` 不是整数 1 时整份拒绝**（缺失、`2`、`"1"` 都算）：WhisperWoof 对这样的 manifest 写 `failed / E_VERSION`；cpad 忽略这样的 `result.json` 并记日志。
- `marks[].k` 是**可扩展的枚举**：v0.3.0 只写 `star`（schema 里的 `x-knownValues`），格式是 `^[a-z][a-z0-9_]{0,15}$`。读的一方跳过不认识的 `k`，不拒绝整个文件。以后加 `todo` 之类不需要升 `v`。
- `env`（docs/23 §4.2，可选，新增字段不升 `v`）：老 cpad 写的 manifest 没有它，第 0 段没落地时也没有；读的一方把"没有"当成 `null`。`env.link` 也是可扩展的枚举（`here` / `unknown`，以后设备端的戳会加 `other`），不认识的值照"未知"显示，不拒绝整个文件（向量 `valid/manifest_memo_env.json`）。`env` 和 `cap` 互不相干，一份 manifest 可以两个都带（向量 `valid/manifest_clip_env.json`）。
- schema 只用 WhisperWoof 校验器支持的关键字（`contract-schema.js`，PR #151；不能用 `oneOf`、`else`、`not`、`format`、`patternProperties`、`additionalProperties` 等），不然它启动时就抛错；`x-` 开头的注释（如 `x-knownValues`、`x-maxBytes`）它跳过。cpad 的 `tests/contract/test_recording_contract.py` 钉住这个关键字集合。
- 改变已有字段的含义或删除字段才升到 v2，新建 `v2/`。

## 校验顺序（参考实现 `check_recording.validate`）

| 步骤 | 条件 | 结果 |
|---|---|---|
| 1 | 不是 JSON 对象 | `E_INVALID`，field `(root)` |
| 2 | `v` 不是整数 1 | `E_VERSION`，field `v`；其余一概不看 |
| 3 | `result.json` 超过 64 KiB | `E_INVALID`，field `(size)`（读文件时按磁盘上的字节数；向量按紧凑 JSON 算） |
| 4 | 不符合 schema | `E_INVALID`，field 是出错的路径，如 `segments[1].file` |
| 5 | `manifest.json` 的段序号规则（下面） | `E_INVALID`，field 同上 |

`pattern` 按 ECMA-262 理解：`$` 只匹配字符串末尾，所以 `"seg-000.wav\n"` 不合法（Python 的 `re` 默认会放过，工具里已经纠正；JS 的正则本来就是这样）。

**manifest 的段序号规则**（schema 表达不了，在 schema 全部通过之后检查）：

- `segments[].i` 严格递增；`gaps` 严格递增；`gaps` 里的序号不能同时是已落地的段；
- 段序号和 `gaps` 合起来恰好是 `0..max`（缺段必须写进 `gaps`，包括最后一个已落地段是 `last:false` 时的 k+1）；
- `segments[].file` 等于 `seg-%03d.wav`（按 `i`）；
- `segments[].bytes` = 44 + 2 × `samples`。

**转写程序另外要查的**（和文件系统有关，不在本契约的 JSON 校验里，docs/11 §7.8）：目录的 realpath 在根目录之内；目录和文件都不是符号链接（lstat）；每段 ≤20 MB；总时长 ≤4 小时；WAV 头是 PCM 16k / 16 bit / 单声道，数据长度和文件大小一致；整个文件的 SHA-256 等于 `segments[].sha256`。不合格的写 `failed / E_INVALID`；符号链接之类直接忽略，不写任何东西。

**cpad 另外要查的**（docs/11 §6.6）：`result.json` 的 `session` 等于 manifest 的 `session`；`outputs.*.path` 是会话目录里的文件名（schema 已经保证没有 `/`、不以 `.` 开头）。

## 文件内容摘要

完整定义见 `schema/`，例子见 `vectors/valid/`。

- **manifest.json**：`v`、`kind:"cardputer.recording"`、`session`（第 0 段的 MonoId）、`cap?`（语音采集，见上）、`attempt`、`mode`（`memo`/`meeting`）、`project`（`{key, name?}` 或 null）、`consent?`（只有会议，`"ack"` 表示开始前用户在"请先告知在场的人"确认框里按了 Enter；只代表用户确认过，不代表别人同意）、`started{wall_ms, tz_min, uncertain}`、`duration_ms`（≤4 h）、`lead_ms`（第 0 段开头 codec 冲击的长度，0–2000）、`audio{16000, 1, 16}`、`segments[]{i, file, id, t0_ms, samples, bytes, sha256（整个文件）, recovered, repaired}`、`gaps[]`、`marks[]{t_ms, k}`、`device{dev, fw}`、`written_at`、`env?{host, link}`（docs/23 §4.2：第 0 段落地时 cpad 在内存里算一次。`host` 是取回它的 Mac 当时的名字，你用 `cpa host name` 起的，1–24 B、没有控制字符，和主机 hello 的 `host_label` 同一条规则，没起名就是 `null`，从不用 macOS 的电脑名；`link` 只在 cpad 能证明录第 0 段开头时设备连着它才是 `here`，否则 `unknown`，绝不写假的 `here`。只是注释，从不决定项目或去向）。
- **status.json**：`v`、`session`、`attempt`、`state`、`reason?`（`state:"blocked"` 时必填：`busy` / `model_missing` / `disabled` / `engine`）、`by?`、`at`。
- **transcript.json**：`v`、`session`、`attempt`、`lead_ms?`、`engine{stt}`、`sentences[]{t_ms, text}`。
- **result.json**：`v`、`session`、`attempt`、`state`（`done`/`failed`）、`level`（`done` 时必填：`transcript` 要有 `outputs.transcript_json` 和 `transcript_md`，`summary` 还要有 `outputs.summary`）、`mode`、`title?`（≤80 字）、`memo?`（`{text ≤2000 字, intent note|todo}`，只有便条）、`outputs{name: {path, sha256}}`、`actions?`（≤20 条，`text` ≤200 字，`owner`/`due` ≤40 字，`confident` 只给有明确承诺和负责人的）、`pipeline?`、`error`（`done` 时必须为 null；`failed` 时必须是 `{code: E_VERSION|E_INVALID|E_AUDIO|E_STT|E_LLM|E_IO, retryable}`）、`finished_at`。
- 时间戳都是带时区偏移的 RFC 3339（`2026-10-21T14:45:10.120+08:00`）。"字"按 Unicode 码点计（JSON Schema 的 `maxLength`）。

## 向量格式

```json
{"doc": "manifest", "name": "…", "note": "…（可选）", "data": {…文件内容…}}
{"doc": "result", "name": "…", "reason": {"code": "E_INVALID", "field": "outputs.summary.path"}, "data": {…}}
```

- `doc` 是 `manifest` / `status` / `transcript` / `result` 之一，`data` 就是那个文件的 JSON 内容。
- 合法向量必须得到 `ok`；非法向量必须**恰好**得到 `reason` 里的那个 `code` 和那一个 `field`（每个非法向量只含一个错误）。
- `check_recording.py` 还要求下面这些非法用例都在（`REQUIRED_INVALID`）：段文件名里有 `../`、段文件名是绝对路径、`outputs.*.path` 带 `/`、`session` 不是 MonoId、`attempt` 为 0、段序号不连续又没有对应的 `gaps`、`sha256` 不是十六进制、`actions` 超过 20 条、`title` 超过 80 字、`memo.text` 超过 2000 字、未知的 `mode`、`state:"done"` 却带 `error`、`v=2`（manifest 和 result 各一个）；以及至少一个带未知顶层字段的合法 `result.json`。

改动任何向量后重新生成摘要（在本目录下）：

```bash
shasum -a 256 vectors/valid/*.json vectors/invalid/*.json > VECTORS.sha256
```

WhisperWoof 把 `vectors/` 和 `VECTORS.sha256` 复制到 `src/whisperwoof/core/cardputer/fixtures/`，在 `SOURCE` 文件里写明来源提交，测试核对摘要（docs/12 M3-W）。
