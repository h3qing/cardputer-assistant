[English](README.md) · **中文**

# 协议 v0 契约

固件 `cpa-fw` 和 daemon `cpad` 之间的 NDJSON 协议。**规范以 [docs/05-architecture.md §6](../docs/05-architecture.zh-CN.md) 为唯一真相源**；本目录是它的机器可读版本，两端的契约测试都读这里的同一份文件：

- 固件：`firmware/app/test/core/test_codec_vectors`、`test_link_flows`、`test_limits_match_json`，以及 `firmware/app/test/ui/test_rec_flows`（`flows/rec/`）；
- daemon：`daemon/tests/contract/`。

```
protocol/
├── limits.json                     # 05 §6.1–6.4、§6.7 的数值和格式（两端读取或镜像）
├── schema/v0/common.schema.json    # 共享定义：信封、ID、受限字符串、枚举
├── schema/v0/<type>.schema.json    # 每种消息一个（05 §6.5 全部类型，含 debug.*）
├── schema/meta/                    # 向量文件自身的格式
├── vectors/v0/messages/<type>.json # 每种消息：合法用例 + 非法用例
├── vectors/v0/flows/<name>.json    # 多条消息的时间线：握手、TX 被扣住、重放去重……
├── vectors/v0/flows/rec/<name>.json # 主机要文件（file.*）的流程：要有录音引擎的设备才答得上
├── vectors/v0/flows/app/<name>.json # 设备上的应用发 rec.* 的流程（v0.4.1）：链路核心自己发不出这些请求
├── tools/                          # check_vectors.py（契约校验）及其测试
│   ├── fixtures/v02_cases.sha256   # v0.2 每个用例的摘要：只许新增，不许改（test_additive.py）
│   ├── fixtures/v031_cases.sha256  # v0.1–v0.3.1 每个用例和流程（含 flows/rec/）的摘要，同上
│   └── fixtures/v040_cases.sha256  # 到 v0.4.0 为止每个用例和流程（含 flows/rec/）的摘要，同样只许追加
├── recording/                      # 录音文件契约（cpad ↔ WhisperWoof），见 recording/v1/README.md
├── filing/                         # 归档契约：`cpa filing … --json` 的输出（docs/23），见 filing/v1/README.md
└── setup/                          # 首次设置契约：`cpa setup check --json` 的行（docs/27 §2.6），见 setup/v1/README.md
```

## 校验

```bash
uv run --with 'jsonschema>=4.23,<5' python protocol/tools/check_vectors.py          # 退出码 0，打印每种类型的用例数
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/tools -q          # 工具自身的测试
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/recording -q      # 录音文件契约 v1
uv run --with 'jsonschema>=4.23,<5' python protocol/recording/tools/check_recording.py
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/filing -q         # 归档契约 v1
uv run --with 'jsonschema>=4.23,<5' python protocol/filing/tools/check_filing.py
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/setup -q          # 首次设置契约 v1
uv run --with 'jsonschema>=4.23,<5' python protocol/setup/tools/check_setup.py
```

`check_vectors.py` 检查：

1. 每个 schema 都是合法的 Draft 2020-12，并且 `limits.json` 和 schema 里的同名限制一致（`schemas.LIMIT_POINTERS`）；
2. 每种消息都有向量文件，合法、非法用例各至少 1 个；反过来每个向量文件都有 schema；
3. 合法用例解码为 ok；非法用例解码出声明的错误码（`E_BAD_FIELD` 还要对上唯一的 `field`）；
4. `outbox.item` 不接受任何执行类 kind（`spawn`、`reply`、`feedback`、`agent.spawn`、`agent.reply`），并且每个都有对应的非法用例；
5. `agent.spawn` 覆盖 05 §9.2 的全部恶意正文；
6. `file.*` 带齐 docs/12 M3-P 要求的非法用例（`check_vectors.REQUIRED_FILE_CASES`），每个合法的 `file.data` 用例的 `crc` 都等于 `b64` 解码后字节的 CRC-32；v0.4.1 的消息带齐 docs/13 §12.2 验收要求的非法用例（`rec_rules.REQUIRED_REC_CASES`）；
7. 20 条必需的流程都在（主机发 `file.*` 的在 `flows/rec/`，设备发 `rec.*` 的在 `flows/app/`，其余在 `flows/`），并满足下面"流程"一节的不变式。

另外，`tools/test_additive.py` 用 `tools/fixtures/v02_cases.sha256` 守住 v0.2 的每个消息用例和每条流程：只能在末尾追加，已有用例一个字节都不能改（摘要是用例的规范 JSON：键排序、紧凑、ASCII 转义）。这份摘要在 M3 改动之前生成，以后不再重新生成。`tools/fixtures/v031_cases.sha256` 用同样的方法守住 v0.3.1 发布时的每个消息用例和每条流程（含 `flows/rec/`），在 v0.4.0 改动之前从 `v0.3.1` 标签生成，以后也不再重新生成。`tools/fixtures/v040_cases.sha256` 用同样的办法守住到 v0.4.0 为止的全部用例和流程（含 `flows/rec/`），在 v0.4.1 改动之前生成；v0.4.0 合入时把 v0.3.2 的 `debug.codec` 和 V40-P 的 `debug.state.ui` 新增的用例追加进去，此外不再改动。

## 版本规则（05 §6.9）

- 信封里的 `v` 是主版本号，v0 期间固定为 `0`。收到其他值回 `E_VERSION`。
- **只新增**字段或消息类型时，`v` 不变：旧的一端忽略看不懂的字段；看不懂的类型回 `E_UNKNOWN_TYPE`（是回复时直接忽略）。所以所有 schema 都允许未知字段，不写 `additionalProperties: false`。
- **改变语义或删除字段**时，`v` 加 1，新建 `schema/v1/` 和 `vectors/v1/`，旧版本目录保留到两端都不再支持为止。
- 任何协议改动的顺序：先改 05 §6 → 再改这里的 schema 和向量（RED，两端的契约测试变红）→ 最后改两端实现。
- 能力通过主机 `hello` 的 `agent` 和 `caps` 协商。§6.10 预留的类型名（`voice.*`、`approval.*`、`log`、`pair.*`）在实现之前不建 schema。

## Schema 约定

- 每个 schema 用 `x-dir` 声明方向：`h2d`（主机→设备）、`d2h`（设备→主机）。
- 信封：不是回复的消息（主机主动发的、设备的请求）用 `common#/$defs/request`，**禁止带 `re`**；回复用 `common#/$defs/reply`，**必须带 `re`**。
- 可选字段可以省略，也可以是 `null`（05 §6.2）。
- 自定义关键字（标准校验器会忽略，`check_vectors.py` 会执行）：
  - `x-maxBytes`：按 **UTF-8 字节**限制字符串长度（`maxLength` 数的是字符）；
  - `x-uniqueBy`：对象数组里某个字段不能重复（如 `slot`、`sid`、`qid`）；
  - `x-maxPerKey`：对象数组里同一个字段值最多出现几次（队列每个项目 ≤4）；
  - `x-b64Len`：对象里 `data` 指定的 base64 字符串解码后必须恰好是 `len` 指定的整数那么多字节（`file.data` 的 `b64` 和 `n`），不一致判为 `E_BAD_FIELD b64`。
- `pattern` 按 ECMA-262 理解：`$` 只匹配字符串末尾（Python `re` 的 `$` 还会匹配末尾换行之前，工具里已经纠正）。

## 接收端判定顺序（两端的 codec 都按这个顺序）

参考实现：[`tools/wire.py`](tools/wire.py)。

| 步骤 | 条件 | 结果 | 回复吗 |
|---|---|---|---|
| 1 | 丢掉所有 `\r` 后，一行（含 `\n`）超过 `line_max`=2048 字节 | `E_TOO_LARGE` | 否（拿不到 `id`），只计数 |
| 2 | 不以 `{` 开头 | 噪声 | 否，只计数 |
| 3 | 不是 JSON 对象（含 `NaN`、截断、尾随垃圾） | `E_PARSE` | 否，只计数 |
| 4 | `v` 缺失或不是整数 / `v`≠0 | `E_BAD_FIELD v` / `E_VERSION` | 见下 |
| 5 | `t`、`id`、`re` 格式不对 | `E_BAD_FIELD t\|id\|re` | 见下 |
| 6 | 不认识的 `t` | 带 `re`：忽略；否则 `E_UNKNOWN_TYPE` | 见下 |
| 7 | 任何字符串里有非法 UTF-8 | `E_BAD_FIELD <路径>` | 见下 |
| 8 | 不符合该类型的 schema | `E_BAD_FIELD <路径>` | 见下 |

**谁要回复**：只有"`id` 合法、不带 `re`"的消息才会得到回复，而且恰好一条；带 `re` 的消息（回复）永远不被回复，出错也只计数。`field` 用 `a.b[0].c` 形式的路径，例如 `sessions[3].name`。

**每种请求的回复类型**：

| 请求 | 正常回复 | 出错时 |
|---|---|---|
| `hello`（主机） | `hello`（设备，`role:"device"`） | `error`（如 `E_VERSION`） |
| `ping` | `pong` | `error`（如未握手时 `E_NOT_READY`） |
| `time.set`、`*.snapshot`、`debug.key`、`debug.reboot` | `ack` | `error` |
| `debug.state`（请求） | `debug.state`（设备摘要，带 `re`） | `error` |
| `outbox.item` | `outbox.ack`（`stored` / `duplicate`） | `outbox.ack`（`rejected`，必须带 `code`） |
| `agent.spawn`、`agent.reply` | `agent.result`（`ok:true`） | `agent.result`（`ok:false`，必须带 `code`） |
| `queue.done`、`inbox.dismiss` | `ack` | `error` |
| `file.list`（v0.3） | `file.items` | `error`（`E_BUSY` msg `live`/`full`、`E_NOT_READY`） |
| `file.get`（v0.3） | `file.data` | `error`（`E_BUSY`、`E_UNKNOWN_ITEM`、`E_STORAGE`、`E_NOT_READY`） |
| `file.del`（v0.3） | `ack`（文件不存在也回 `ack`） | `error`（`E_BUSY`、`E_BAD_FIELD size`/`sha256`） |
| `debug.rec`（v0.3，测试版） | `ack` | `error` |
| `debug.mic`（v0.3.1，测试版） | `ack` | `error` |
| `debug.codec`（v0.3.2，测试版） | `ack` | `error`（`E_BUSY live`） |
| `debug.lang`（v0.4.3，测试版） | `ack` | `error` |
| `rec.list`（v0.4.1，设备发） | `rec.items` | `error`（`E_BAD_FIELD`、`E_INTERNAL`；旧 cpad 回 `E_UNKNOWN_TYPE`） |
| `rec.act`（v0.4.1，设备发） | `ack` | `error`（`E_UNKNOWN_ITEM`、`E_BAD_FIELD`、`E_INTERNAL`；旧 cpad 回 `E_UNKNOWN_TYPE`） |
| `dests.snapshot`（v0.5） | `ack` | `error`（不认识它的旧固件回 `E_UNKNOWN_TYPE`；主机只推给 `revs` 里有 `dests` 的固件） |

设备处于 Attached（还没握手）时只回复 `hello`，其他主机消息一律回 `error{code:E_NOT_READY}`。

## 向量格式

### 消息：`vectors/v0/messages/<type>.json`

```json
{
  "type": "agent.spawn",
  "valid":   [{"name": "...", "dir": "d2h", "msg": {...}, "line_b64": "...（可选）"}],
  "invalid": [{"name": "...", "dir": "d2h", "msg": {...}, "code": "E_BAD_FIELD", "field": "text"},
              {"name": "...", "dir": "h2d", "line_b64": "...", "code": "E_TOO_LARGE"}]
}
```

- `dir`：这条消息在线上的方向，接收方就是做判定的一端。
- 合法用例必须有 `msg`（解码结果，用于语义深比较）。带 `line_b64` 时，它是线上的原始字节（恰好一行，以 `\n` 结尾），解码后必须等于 `msg`。
- 非法用例在 `msg` 和 `line_b64` 中二选一。只给 `msg` 时，线上字节是它的规范序列化：`json.dumps(msg, ensure_ascii=False, separators=(",", ":")) + "\n"`，UTF-8 编码（05 §6.1）。
- 非法用例只含**一个**错误，所以 `field` 是唯一的。
- `code` 只会是接收端判定得出的那几种：`E_PARSE`、`E_TOO_LARGE`、`E_VERSION`、`E_UNKNOWN_TYPE`、`E_BAD_FIELD`。

### 流程：`vectors/v0/flows/<name>.json`

一条时间线，`steps` 按 `t`（毫秒）不减的顺序排列。三种步骤：

| 步骤 | 字段 | 含义 |
|---|---|---|
| 线上的一行 | `t`、`from`（`host`/`device`）、`msg` 或 `line_b64`，可选 `deliver_t`、`lost`、`outcome`、`note` | `t` 是发出时间。`deliver_t` 是到达对方的时间（只用于设备 TX 被扣住）；`lost: true` 表示对方永远收不到；`outcome` 是接收方的判定，默认 `ok`，也可以是 `noise`、`ignored` 或错误码 |
| 事件 | `t`、`event`，可选 `args`、`note` | `detach`（拔线：两端连接状态清空）、`daemon_restart`（daemon 进程重启，主机 ID 重新编号）、`device_reboot`（设备重启，设备 ID 重新编号，回到 Attached）、`user_spawn`（用户在确认框按下 Enter；`args` 给出 `project`、`text`） |
| 期望 | `t`、`expect`，可选 `note` | 这一刻应当成立的状态 |

`initial` 用同样的键描述开始时的状态。期望里可用的键：

| 键 | 含义 | 由工具复算？ |
|---|---|---|
| `host.credit_used` | 主机主动发出、还没收到回复、也还没过 5s 回收期的字节数（含 `\n`） | 是 |
| `device.outbox_inflight` | 设备已发出、还没收到 `outbox.ack` 的条目数（同一 ID 重发不重复计数） | 是 |
| `device.agent_inflight` | 设备在途的 `agent.*` 请求数（30s 超时后视为结果未知） | 是 |
| `device.link` | `Detached` / `Attached` / `Ready` | 否，供两端测试断言 |
| `device.outbox_pending` | 设备 outbox 里还没被 ack 的条目总数（含在途） | 否 |
| `host.link` | `offline` / `handshaking` / `ready` | 否 |
| `host.items_stored` | daemon 已按 `(dev, id)` 存下的不同条目数 | 否 |
| `host.queue_added` | `queue.added` 事件数 | 否 |
| `host.spawn_calls` | daemon 实际调用 agent spawn 的次数 | 否 |
| `host.reboot_detected` | 最近一次设备 `hello`/`pong` 是否让主机判定设备重启过 | 否 |

`check_vectors.py` 对每条流程检查的不变式（实现见 [`tools/flows.py`](tools/flows.py)）：

- 每一行都解码为它声明的 `outcome`，方向符合 schema 的 `x-dir`；
- 送达的主机主动消息恰好得到一条设备回复（之后发生 `detach`、`device_reboot`、`daemon_restart` 时，还没回复的可以不回）；
- 回复只能回应对方已经送达的消息；
- 主机 credit ≤ `host_inflight_max`，超过 `credit_reclaim_ms` 没回复的部分收回；设备在途上限 outbox ≤4、agent ≤1、queue/inbox ≤1（v0.4.1 的 `rec.list`、`rec.act` 也占这一个名额，docs/13 §10.2）；主机的 `hello` 一旦送达设备（重新握手），设备在途的请求全部清空，outbox 条目退回待发；
- 上表中"由工具复算"的期望值与复算结果一致；
- v0.3 的主机侧 `file.*` 不变式（docs/11 §5.4，实现见 [`tools/file_flows.py`](tools/file_flows.py)）：
  - 主机只在它最近收到的 `hello`/`pong` 带着 `files`、`files.live` 为 false、`files.rev` 不为 0 时才发 `file.list`/`file.get`/`file.del`（没有 `files` 的旧设备永远不会收到 `file.*`），并且没回复的 `file.*` 不超过 `file_inflight_max`=4 个；
  - 任何 `file.*` 收到 `E_BUSY` 之后，直到之后有一个 `hello`/`pong` 显示 `files.live:false`，主机不再发 `file.*`；
  - 每个 `file.*` 恰好一个回复，而且类型对得上：`file.list`→`file.items`、`file.get`→`file.data`（`fid`、`off` 相同，`n`≤`len`）、`file.del`/`debug.rec`/`debug.mic`/`debug.codec`→`ack`，或者 `error`；
  - `file.del` 只在主机已经收到这个文件 `[0, size)` 的每个字节之后才发（`deliver_t` 之后才算收到，`lost` 的不算；连接断开、daemon 重启后已收到的字节仍然算，因为它们在 `.part` 里）。
- v0.4.1 的设备侧 `rec.*` 不变式（docs/13 §12.2，实现见 [`tools/rec_flows.py`](tools/rec_flows.py)）：
  - 设备只在它最近收到的主机 `hello` 带着 `caps.recs:true` 时才发 `rec.list`/`rec.act`（`detach`、`device_reboot` 之后要重新收到；`daemon_restart` 设备看不见，不影响）；
  - 回复类型对得上：`rec.list`→`rec.items` 或 `error`，`rec.act`→`ack` 或 `error`；`rec.items` 的条数不超过请求的 `max`；
  - `rec.list` 的 `cursor` 必须是设备之前收到的某个 `rec.items` 的 `next`，原样带回（设备不解析、不自己造）。

必需的流程：`handshake`、`tx_parking`、`outbox_replay_dup_ack`、`reboot_detect`、`credit_window`、`offline_spawn_becomes_queue`、`agent_result_unknown`；v0.3 新增 `old_daemon_ignores_files`、`old_device_no_files`，以及放在 `flows/rec/` 的 `file_pull_resume`、`file_del_idempotent`、`files_rev_relist`、`file_list_paging`、`file_busy_while_live`；v0.4.1 新增 `caps_absent`、`old_daemon_ignores_dest`、`new_daemon_old_firmware`，以及放在 `flows/app/` 的 `rec_list_paging`、`rec_act_hide`、`rec_list_old_daemon`。另有 `envelope_rules`，覆盖 05 §6.2 的处理规则。

**`flows/app/`**（v0.4.1）：设备发 `rec.list`/`rec.act` 的流程必须放在这里（`check_vectors.py` 会检查）。这些请求只有设备上的应用（录音应用）才会发，`core/test_link_flows` 的链路核心发不出来；固件由 V41-R/V41-F 的应用测试重放，daemon 的 `tests/contract/test_flows.py` 三个目录都要重放。

**`flows/rec/`**：主机发 `file.list`/`file.get`/`file.del` 的流程必须放在这里（`check_vectors.py` 会检查）。固件 `core/test_link_flows` 只用 v0.2 的链路核心重放 `flows/` 下的流程，答不了 `file.*`；`flows/rec/` 由 `ui/test_rec_flows` 对着完整的 Runtime（录音引擎跑在主机上）重放。daemon 的 `tests/contract/test_flows.py` 两个目录都重放。流程里的 MonoId 都用设备真能发出的 epoch（32 位，第一个字符 ≤`3`，05 §5.9），录音文件契约的向量也一样（`tools/test_file_vectors.py` 检查）。

## 本契约对 05 §6 的补充和收紧

05 §6 没有写死、由本契约定下来的地方。改动任何一条都按上面的版本规则走。

1. **字段格式**：`dev` 是 12 位小写十六进制（MAC 去掉分隔符，例如 `020000000001`）；`fw` 匹配 `^cpa-fw/[!-~]{1,24}$`，别的前缀判为 `E_BAD_FIELD fw`；`app` 是 1–32 个可见 ASCII；`rst` 匹配 `^[a-z0-9_]{1,16}$`；`short` 是 1–16 个可见 ASCII（不含空格）；`boot` 是 MonoId 的前两段。
2. **数值范围**：`up_ms`、`rev`、`age_s` 是 uint32；`pending`、`bat_mv`、`run`、`need`（项目）、`queued`、`total`、`other.*` 是 0–65535；`wall_ms` 和 `epoch_ms` 一样 ≥1735689600000。
3. **正文**：note、queue、spawn、reply 的正文是 1–500 **字节** UTF-8，不能含控制字符（U+0000–U+001F、U+007F），所以一定是单行。协议层允许非 ASCII（设备输入层只产生 0x20–0x7E）。含 `\u0000` 的正文判为非法，**不做替换**；daemon 把控制字符替换成空格只用于协议以外的输入（如 `cpa queue add`）。
4. **项目名、session 名、队列摘要**同样不能含控制字符；`agent.result.name` 沿用 session 名的 32 字节上限（daemon 按字符边界截断）。
5. **条件必填**：`queue` 条目的 `intent=feedback` 必须带 `sid`；`outbox.ack` 的 `status=rejected` 必须带 `code`；`agent.result` 的 `ok=false` 必须带 `code`。
6. **唯一性**：快照里 `slot`（projects）、`sid`（sessions）、`qid`（queue）都不能重复。
7. **sessions.snapshot 的 slot 是 1–9**：没登记项目的 session 不逐条下发，只计入 `projects.snapshot.other`。
8. **error**：`field` 是 1–48 个可见 ASCII，`msg` 是 1–80 个可打印 ASCII。这样主机的任何回复都不超过 `host_reply_max`=256 字节（05 §6.3 的 credit 计算依赖这一点）。
9. **debug.\***（只在测试版固件里有，正式固件回 `E_UNKNOWN_TYPE`）：
   - `debug.key{keys[1..3], hold_ms?}`：同时按下这几个键，按住 `hold_ms`（默认 50，最多 5000）后松开；键名取 `common#/$defs/keyName`（hwtest 的 4×14 布局加 `g0`）。回复 `ack`。
   - `debug.state`：主机请求不带字段；设备回复同名消息，带 `up_ms`、`link`、`mod`（前台模块）、`overlay`（`none`/`compose`/`confirm`）、`overlay_ms?`、`key_ms?`、`outbox{pending, inflight, bad}`、`revs`、`need`、`projects[≤9]{slot, run, need, queued}`。`*_ms` 都是设备的 `up_ms` 时间戳，HIL 用它们算延迟。
   - `debug.reboot`：设备先回 `ack`，再 `esp_restart()`。
10. **`\r`**：接收方丢掉一行里所有的 `\r`；2048 字节的上限按丢掉之后的长度算。
11. **`limits.json`** 除 §6.7 的表以外，还收录了 §6.1–6.4 里两端都要用的常数（重试间隔、心跳、超时、credit 回收、每圈处理行数、错误回复限速）。
12. **设备 `hello` 的两个可选字段（v0.2，docs/09 §6）**：只新增，`v` 仍为 0。
    - `store`：`"sd"` / `"int"` / `"none"`，本次开机选定的主存储（SD 卡、内部 `cpafs`、没有可用存储）；其他取值判为 `E_BAD_FIELD store`。
    - `launcher`：布尔值，固件是否在 bmorcelli Launcher 下运行；不是布尔值判为 `E_BAD_FIELD launcher`。
    - 两个字段都可以省略或为 `null`，含义相同：不知道（v0.1 设备）。固件不设置时，编码结果和 v0.1 逐字节相同；daemon 显示为 `-`。
13. **主机发出的行只有两种**（docs/09 §6）：以 `{` 开头的 JSON 对象，或者空行（`\n`，即 nudge，用来放出被扣住的设备回复，见 ADR-0001 §5.2）。主机绝不发出以字母或其他字符开头的行。原因：Launcher 的串口控制台在启动画面期间就开着，收到的每一行都当命令解析；以 `{` 开头的行只会得到 `ERR unknown command…`，以字母开头的行却可能正好是一条真命令（例如 `flash overwrite`，见 docs/gates/launcher.md）。daemon 的 `tests/unit/test_host_lines_never_start_with_word.py` 守住这一点。
14. **录音传输（v0.3，docs/11 §5）**：只新增，`v` 仍为 0，任何 schema 都不写 `additionalProperties:false`。
    - 设备 `hello` 和 `pong` 的可选字段 `files{n 0–256, rev uint32, live bool}`：`rev` 为 0 表示开机恢复扫描还没做完；`live` 表示录音状态机不在 Idle。没有 `files`（或为 `null`）就是不支持录音传输（v0.2、nosd），主机不发 `file.*`。不带录音的固件编码结果和 v0.2 逐字节相同。
    - 新消息：`file.list{after?, max? 1–4}` → `file.items{items[≤4], more}`；`file.get{fid, off uint32, len 1–1380}` → `file.data{fid, off, n 0–1380, size, crc, b64}`（`n`>0 时必须有 `b64`，`n`=0 时没有）；`file.del{fid, size, sha256?}` → `ack`；测试版的 `debug.rec{seg_ms 10000–600000}` → `ack`。`items` 的每一项见 `common#/$defs/fileItem`；`size` 含 44 B 头，最大 44 + 600000×32。
    - `debug.state` 的回复加可选的 `rec{live, mode, seg, files_n, files_rev}`（空闲时 `mode`、`seg` 为 null）。`rec` 里还可以带录音健康计数（HIL 的 G-A2 / G-A4 / G-A6 直接断言用；统计当前这次录音，空闲时是上一次录音的值，下一次开始录音时清零；省略或 null 表示这个构建不测）：`ring_high`（麦克风环形缓冲里同时等着写卡的满块数的最高值，0–65535）、`mic_missing`（采集回调找不到空闲块重新挂上的次数，每次都可能丢一块，G-A2 要求 0）、`rot_ms_max`（最慢一次换段的耗时，ms）、`heap_free` / `heap_block`（录音期间最低的空闲堆和最大空闲块，字节），后四个都是 uint32。
    - `limits.json` 加了 §5.5 的全部键；能在 schema 里对上的都登记在 `schemas.LIMIT_POINTERS`，其余（`file_inflight_max`、`rec_min_ms`、`memo_max_ms`、`meeting_max_ms`、`seg_ms_default`）是两端实现用的常数。
    - 最坏行长（`tools/test_worst_lines.py`，`id`/`re` 24 字符，每个数取 schema 允许的最大值，布尔取 `false`）：`file.data`（n=1380）2023 B，`file.items`（4 项、每项 16 个标记和全部可选字段）1988 B，带 `files` 的 `pong` 238 B，都 ≤2048。
    - `enc`、`files.bin`、`revs.recs` 是留给以后的名字，v0.3 不建（`git grep -nE '"(enc|bin)"' -- protocol/schema/v0 protocol/limits.json` 应当找不到）。v0.4.1 的 `caps.recs` 是主机 `hello` 的能力位，和 `revs.recs` 无关；`revs` 里仍然没有 `recs`。
15. **录音文件契约**（cpad 写 `manifest.json`，WhisperWoof 写其余文件）不是线上协议，单独放在 [`recording/v1/`](recording/v1/README.zh-CN.md)，有自己的 schema、向量、`VECTORS.sha256` 和 `check_recording.py`。
16. **麦克风增益（v0.3.1，只在测试版固件里有）**：只新增，`v` 仍为 0；正式固件回 `E_UNKNOWN_TYPE`，CI 和 release 工作流检查正式固件里没有 `debug.(key|state|reboot|rec|mic)` 这几个名字。
    - `debug.mic{pga 0–10, vol 0–255, mag 1–64}` → `ack`。三个字段都必填。`pga` 是 ES8311 REG14 的 PGA 档位，每档 3 dB（0–10 对应 0 到 +30 dB）。`vol` 是 REG17 的 ADC 音量，每档 0.5 dB，191（0xBF）是 0 dB。`mag` 是 M5Unified 的 `magnification`，`over_sampling` 固定为 1 时，2 就是不放大。
    - 设置从下一次开始录音起生效，正在录的那一段保持原来的增益。设备重启后回到固件里的 `kMicGain`。只给 HIL 标定（`mic_cal`）用。
    - 回复规则和 `debug.rec` 一样，没握手时回 `E_NOT_READY`。
17. **codec 冷启动钩子（v0.3.2，只在测试版固件里有，#31）**：只新增，`v` 仍为 0；正式固件回 `E_UNKNOWN_TYPE`，CI 和 release 工作流的检查加上了 `debug.codec`（`debug.(key|state|reboot|rec|mic|codec)`）。
    - `debug.codec{warm}` → `ack`。`warm` 是必填的布尔值。设备在麦克风关着时对 ES8311 做软复位（REG00 = 0x1F），codec 回到刚上电的状态：下一次开始录音时，它的输出还会被压成 0 大约 1 秒。`warm` 为 true 时，软复位之后再跑一遍开机时的 codec 预热，等于模拟一次真正的上电。
    - 正在录音（包括还在"准备"的时候）回 `E_BUSY`，`msg` 为 `live`，什么也不做。没握手时回 `E_NOT_READY`。只给 HIL 的 `rec_cold_start` 和设备测试用。
18. **spawn 失败的原因（v0.4，#25）**：只新增，`v` 仍为 0。`agent.result` 加可选字段 `why`（`^[a-z][a-z_]{0,15}$`），只在 `ok=false`、`code=E_SPAWN` 而且 Mac 知道原因时出现：
    - `untrusted`：Claude Code CLI 还没信任这个项目目录（`claude --bg` 的 stderr 以 `Workspace not trusted.` 开头）。桌面端的信任记录和 CLI 分开，所以在桌面端用过的目录也可能没被 CLI 信任。修复只要做一次：在终端里进入项目目录跑一次 `claude`，接受信任提示。cpad 从不替用户接受。
    - `login`：daemon 的环境里 `claude auth status` 显示 `loggedIn:false`（或 stderr 说没登录）。这时 cpad 不启动 session，直接回这个原因。修复：在终端里 `claude auth login`。
    - 设备把不认识的取值当作没有这个字段。旧固件忽略 `why`，照旧显示 `E_SPAWN`。固件还不往返这个字段，所以 `vectors/` 里暂时没有带 `why` 的用例（固件的向量测试要求解码后重新编码逐字段相等）；daemon 侧由 `tests/contract/test_spawn_why.py` 对照参考 schema 检查，并确认带 `why` 的回复仍然 ≤`host_reply_max`。
19. **`debug.state.ui`（v0.4.0，只在测试版固件里有，docs/13 §12.1）**：只新增，`v` 仍为 0。设备回复加可选的 `ui{menu, locked}`，两个都是布尔值、都必填：`menu` 表示 Tab 菜单开着，`locked` 表示设备锁着（Opt+L）。省略或 `null` 表示 v0.4.0 之前的构建；固件不设置时，编码结果和 v0.3.1 逐字节相同。菜单开着时 `overlay` 仍报它下面的那一层；`overlay` 的取值**不新增**，因为 v0.3.1 的 cpad 只认 `none`/`compose`/`confirm`，新取值会让它回 `E_BAD_FIELD`。v0.3.1 的 cpad 忽略 `ui`（daemon `tests/contract/test_old_cpad_compat.py` 用逐字节固定的 v0.3.1 `specs.py` 验证）。
20. **录音应用和采集去向（v0.4.1，docs/13 §8、§9.3、§11.1、§12.2）**：只新增，`v` 仍为 0，任何 schema 都不写 `additionalProperties:false`，不新增错误码。录音文件契约（`recording/v1/`、`manifest.json`）一字不改。
    - 主机 `hello` 新增可选字段：`caps.recs`、`caps.todo`（布尔）；`host_id`（`^[a-z0-9-]{8,32}$`，cpad 第一次运行时随机生成，v0.8 之前设备不拿它做判断）；`host_label`（1–24 字节，不含控制字符，只显示在"控制"页）。都可以省略或为 `null`，省略、`null`、`false` 含义相同：不支持。旧固件忽略这些字段。
    - `dest`：去向 id，`common#/$defs/destId`，`^[a-z][a-z0-9-]{0,11}(\.[a-z0-9-]{1,12}){0,2}$`，最多 24 字节。出现在 `outbox.item` 的 `note` 正文（`body.dest`）和 `file.items` 的每一项（只有便条的第 0 段会带）。省略、`null`、`"daily"` 都表示今日笔记；v0.4.1 固件只会发 `"todo"`。收到不认识的 `dest`，cpad 一律存成普通的今日笔记（docs/13 §9.2），绝不降级成粘贴或执行；旧 cpad 忽略这个字段，效果相同。
    - `rec.list{filter, max, cursor?}`（设备请求）：`filter` 是 `all`/`starred`/`memo`/`meeting`，`max` 是 1–`rec_list_max`=6，都必填；`cursor` 是上一页 `rec.items` 的 `next`，原样带回，没有就是第一页。
    - `rec.items{items, next?}`（主机回复）：`items` 最多 `max` 条 `common#/$defs/recRow`，同一个 `rec` 不重复；还有下一页时带 `next`。`recRow` 必填 `rec`（MonoId）、`mode`（`memo`/`meeting`）、`st`（cpad `sessions.state_of` 的取值：`on_device`、`pulling`、`archived`、`processing`、`blocked`、`done`、`transcript`、`failed`、`stale`）；可选 `wall_ms`（开始的墙钟时间，没有时 cpad 用归档时间）、`dur_ms`（uint32）、`project`、`starred`、`marks`（录音时打的 ★ 数）、`title`（1–`rec_title_max`=48 字节，不含控制字符）、`todo_n`（`summary.md` 里没勾的待办数）。排序和翻页由 cpad 决定：按墙钟时间新的在前，**不能只按 MonoId**（NVS 擦除后 epoch 会变，docs/13 §8.1）；隐藏的会话不列出。
    - `cursor` / `next`：不透明字符串，`^[A-Za-z0-9_-]{1,32}$`（`rec_cursor_pattern`，`rec_cursor_max`=32）。设备不解析；cpad 解不开时回 `E_BAD_FIELD cursor`。
    - `rec.act{rec, op, project?}`（设备请求）：`op` 是 `star`/`unstar`/`tag`/`hide`，没有删除（docs/13 §8.4）。`tag` 必须带 `project`：项目 key，或者 `null` 表示不归属；其他 `op` 的 `project` 只能省略或为 `null`。回复 `ack`；找不到这个会话回 `E_UNKNOWN_ITEM`。cpad 只追加事件（`rec.edited`、`rec.hidden`），不写、不移、不删任何文件，也不改 `manifest.json`。
    - 在途：`rec.list`、`rec.act` 和 `queue.done`、`inbox.dismiss` 共用设备"最多 1 个在途"的名额。设备只在最近一次主机 `hello` 带 `caps.recs:true` 时才发；旧 cpad 收到会回 `E_UNKNOWN_TYPE`（`flows/app/rec_list_old_daemon`）。
    - **`host_reply_max` 的唯一例外是 `rec.items`**（`rec_rules.LARGE_HOST_REPLIES`）：它只受 `line_max` 限制。因为它占的是那 1 个在途名额，设备接收缓冲的最坏情况从 4096 + 6×256 变成 4096 + 5×256 + 2048 = 7424 字节，仍然小于 8192。05 §6.3 第 4 条写着同一句。
    - 最坏行长（`tools/test_worst_lines.py`）：`rec.items` 6 条、每条所有字段取最大值、标题 48 字节中文、`next` 32 字符时 1580 B；标题 48 个 `"`（每个转义成 2 字节，没有更贵的字符）时 1868 B。`file.items` 4 项都带 `dest:"todo"` 时 2044 B；`dest` 超过 5 字节时 4 项放不下（24 字节时 2124 B），设备按 `file.items` 本来就允许的方式少列几项、`more:true`（固件 `file_fit_page`），3 项带 24 字节 `dest` 是 1621 B。
    - `limits.json` 新增 `rec_list_max`、`rec_title_max`、`rec_cursor_max`、`rec_cursor_pattern`、`host_label_max`、`host_id_pattern`、`dest_pattern`、`dest_max`，都登记在 `schemas.LIMIT_POINTERS`。
21. **`debug.state.diag` 和录音健康计数（gh #21，只在测试版固件里有）**：只新增，`v` 仍为 0，任何 schema 都不写 `additionalProperties:false`，不新增错误码。
    - 设备回复加可选的 `diag{…}`，16 个字段都必填：`heap_free`、`heap_min`、`heap_block`、`stack_left`、`audio_stack_left`、`loop_n`、`loop_max_us`、`loop_win_max_us`、`gap_max_us`、`gap_win_max_us`、`loop_over_20`、`loop_over_50`、`loop_over_100`、`loop_over_500` 是 uint32，`ghost_n`、`kb_reinit_n` 是 count。省略或 `null` 表示没有这个对象的构建；固件不设置时，编码结果和以前逐字节相同。
    - 含义：`heap_free`、`heap_block`、`stack_left` 是系统页每秒采样一次的数（空闲堆、最大空闲块、loop 任务栈从没用到的字节）；`heap_min` 是开机以来最低的空闲堆；`audio_stack_left` 是音频任务栈从没用到的字节，没有音频任务（没有 SD 卡）时为 0。`loop_*` 由 `src/main.cpp` 的 `loop()` 用 `micros()` 计时，一圈包括键盘扫描：`loop_n` 是圈数（到 2^32 回绕），`loop_max_us` 是开机以来最长的一圈，`gap_max_us` 是开机以来相邻两圈起点之间最长的间隔（一圈加上两圈之间别的任务占用的时间），`*_win_max_us` 是同样的最大值但只看最近 15–30 秒（两个 15 秒的桶），`loop_over_20/50/100/500` 是超过这些毫秒数的圈数。`ghost_n`、`kb_reinit_n` 是系统页上的"幽灵"和"重置"。不给 loop 计时的平台（主机模拟器、设备测试程序）把 `loop_*` 都报 0。
    - 测试版固件从这一版起填第 14 条的录音健康计数：音频任务在每次开始录音时清零，每写完一块（128 ms）更新一次，空闲时保留上一次录音的值。`ring_high`、`mic_missing` 来自 `MicRing`（`mic_missing` 数的是采集回调找不到空闲块、或者重新挂块失败的次数）；`rot_ms_max` 是换段的那一块从写入开始到写完的耗时（含这一块本身的写入）；`heap_free` / `heap_block` 是开始录音时和每写一块之后的采样里最低的。本次开机还没录过音时这五个字段都省略：对测试版固件来说，第 14 条的"省略 = 不测"也包括"还没测到"。正式固件不计时 loop，也不采样这些计数。
    - 兼容：旧 cpad 不看 `diag`（daemon `tests/contract/test_old_cpad_compat.py` 用 v0.3.1 的 `specs.py` 逐个检查 `debug.state` 回复向量）。最坏行长（`tools/test_debug_state_diag.py`）：9 个项目、`rec` 带全部健康计数、`ui` 和 `diag`，每个数都取 schema 允许的最大值，1464 B ≤ 2048。
22. **结果未知的开 session（v0.4.1，docs/20 U25）**：只新增，`v` 仍为 0，不新增错误码，`limits.json` 不变。
    - `queue.snapshot` 的每一项加可选字段 `uncertain`（布尔或 `null`）。为 true 表示这一项是设备在 `agent.spawn` 结果未知（超时或断线，`outbox.item` 的 `body.uncertain`）时存下的，Mac 上可能已经开了这个 session。cpad 只在为 true 时写这个字段，所以普通的项和 v0.4.0 逐字节相同；省略、`null`、`false` 含义相同。
    - 新固件把它带进 `QueueRow.uncertain`，从这一项开 session 时确认框写"可能已开过，先看 Sessions"；旧固件忽略这个字段，照旧显示。新用例只追加在 `queue.snapshot.json` 末尾（合法：`uncertain:true` 和 `null`；非法：字符串 → `E_BAD_FIELD items[0].uncertain`），`tools/test_queue_uncertain.py` 守住它是可选的布尔值。
    - 不是线上协议、但一起改的：cpad 下发 session `name` 时去掉它自己项目的 `key/` 前缀（设备显示时自己加，docs/20 U14，05 §7.6）；`events.jsonl` 新增 `queue.reopened`，带着原项的 `qid, project, text, intent, sid, origin, uncertain`（`cpa queue reopen`，docs/20 U12，05 §7.3、§7.7）。
23. **归档契约（docs/23 P1-B，#54）**：和录音契约一样不是线上协议，单独放在 [`filing/v1/`](filing/v1/README.zh-CN.md)：`cpa filing … --json` 和 `cpa project ls --json` 打印的 JSON（列表的行、详情、写命令的结果、错误、项目），有自己的 schema、向量、`VECTORS.sha256` 和 `check_filing.py`。协议 v0 和录音契约一字不改。（2026-10-22，P1-C）`cpa filing hand` 排的项是普通的 `queue.added`（`origin:"inbox"`），设备看板照常显示，摘要以"语音·"开头，开的时候一律 plan；协议 v0 也不改。（2026-10-22，P1-E）`cpa mcp` 的只读工具 `list_unfiled`、`get_filing_item` 返回同样的 `ls`、`show` 文档（加 `trust`；从不含藏起来的，`fw` 为 null）。
24. **采集去向（v0.5，[docs/14 §7](../docs/14-destinations.zh-CN.md)）**：只新增，`v` 仍为 0，不新增错误码。
    - `dests.snapshot{rev, dests[≤12]}`（主机→设备，回 `ack`）：设备可以提供的去向，按 `slot` 排。每行 `{id, slot 1–12, label, kind, key?, fl?}`：`id` 匹配 `^[a-z]{3,7}(\.[a-z0-9][a-z0-9-]{0,11})?$`（内置去向就是种类名，自定义的是 `<种类>.<名字>`，名字最长 12 字符，所以 `id` 总在 `limits.json` 的 `dest_pattern` 里，docs/25 §2.2）；`label` 是 1–24 字节 UTF-8，不能有控制字符、`"` 和 `\`，所以线上从不转义；`kind` 匹配 `^[a-z]{3,7}$`，用模式不用枚举；`key` 是单个小写字母或 null；`fl` 匹配 `^[a-z]{0,8}$`（docs/25 §2.2，按 `l c p k` 的顺序）：`l` 现场（离线记的要到"等你"里按 Enter）、`c` 会离开这台 Mac、`p` 要项目、`k` 每条在设备上翻完全文按 Enter，cpad 每行都发，没有标志也发 `""`。`slot` 不能重复；`key` 可以重复，设备取第一行。（2026-10-22，docs/25 R1-B：`fl` 代替原来的 `live`、`cloud`、`proj` 三个布尔，名字从 16 改成 12。）
    - 设备只做显示和按键解析，判定只在 cpad：只绑自己字母表里的字母（docs/14 §3.1）；不认识的 `kind` 照样显示，行为只看 `fl`，`fl` 里不认识的字母忽略，没有 `fl`（或为 null）当 `lck`，宁可多确认（docs/25 §4.6）；多出来的字段忽略。快照缓存在 `/cfg/dests.json`，开机后在 `revs.dests` 里报缓存的 `rev`（0 = 没有）。
    - `rev` 是内容哈希（各行规范 JSON 的 CRC-32，是 0 就用 1），不是计数器：cpad 重启后计数器会从头来，和设备缓存里的旧 rev 撞上。主机只在 `revs.dests` 和自己的 rev 不同时推；`revs` 里没有 `dests` 的固件永远收不到。
    - 最坏行长（`tools/test_dest_worst_lines.py`，12 行、每个字段取上限、`id` 20 字符、`fl` 8 个字母、`key` 取 `null`——`null` 比 `"z"` 长 1 字节）：1507 B（原来三个布尔、16 字符的名字时是 1843 B）。`label` 仍禁止 `"` 和 `\`，这样任何一行都不用转义。
    - **合并顺序**：固件的 `test_codec_vectors` 要求向量和 schema 的类型数等于 `kMsgTypeCount`，所以固件 codec 和这个 schema 同批认识 `dests.snapshot`（2026-10-22，docs/25 R1-A：第 31 个消息，排在 `rec.act` 后、`debug.*` 前；正式固件 25 个）。这一版固件只会解码，不收：握手后收到照旧回 `E_UNKNOWN_TYPE`，也不在 `revs` 里报 `dests`，所以 cpad 不会推给它；缓存、显示和按键随 F5（docs/25 §4.6）。向量里没定义的字段用 `x_` 开头（固件的向量测试只忽略这种）。
25. **界面语言（v0.4.3，只在测试版固件里有，[docs/21 §7](../docs/21-settings.zh-CN.md)）**：只新增，`v` 仍为 0，不新增错误码，`limits.json` 不变。
    - `debug.lang{lang}`（主机→设备，回 `ack`）：`lang` 必填，`"zh"` 或 `"en"`。设备走和设置页语言行按 Enter 同一条切换路径：马上整屏重画、弹切换提示、写 NVS（录音中先记着，录音结束再写）。和 `debug.key` 一样要先握手，没握手回 `E_NOT_READY`。HIL 夹具开始时读 `debug.state.ui.lang`，需要时用它设成中文，结束时恢复原来的语言。
    - `debug.state.ui` 加可选的 `lang`（`"zh"` / `"en"`）：现在显示的语言。省略表示 v0.4.3 之前的构建；`ui` 的 `menu`、`locked` 不变。最坏行长（`tools/test_debug_lang.py`）1476 B ≤ 2048。
    - 正式固件回 `E_UNKNOWN_TYPE`；CI 和 release 工作流检查正式固件里没有 `debug.(key|state|reboot|rec|mic|codec|lang)`。固件 codec、schema 和向量同批认识它（测试版第 32 个消息，排在 `debug.codec` 后；正式固件仍是 25 个）。
