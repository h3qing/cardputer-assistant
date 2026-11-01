[English](README.md) · **中文**

# 首次设置契约 v1（`cpa setup check --json`）

docs/27 §2.3–§2.6 的机器可读版本：`cpa setup check --json` 每行打印一行 JSON（JSON Lines），一行是这台 Mac 的一项设置。Mac app 的首次设置页（docs/27 O6）和终端都读它；`cpa` 只读，不改任何东西，5 s 内回来（不用 Spotlight、不起登录 shell、不联网、不开串口）。

```
protocol/setup/
├── v1/
│   ├── README.md                 # 本文件
│   ├── schema/row.schema.json    # 一行的 JSON Schema（Draft 2020-12）
│   ├── vectors/valid/*.json      # 必须被接受的行
│   ├── vectors/invalid/*.json    # 必须被拒绝的行，每个带拒绝原因
│   └── VECTORS.sha256
└── tools/check_setup.py          # 参考校验器 + 契约自检，测试在同目录
```

```bash
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/setup -q
uv run --with 'jsonschema>=4.23,<5' python protocol/setup/tools/check_setup.py   # 退出码 0
```

daemon 的 `tests/contract/test_setup_contract.py` 把每种事实组合下的每一行都过一遍 `check_setup.py`，并核对 `mac/Fixtures/setup-v1` 是同一份拷贝。

## 一行

| 字段 | 意思 |
|---|---|
| `v` | 1 |
| `id` | `service notes name import device profile disk stt hooks claude mcp dest`（`x-knownValues`，以后只增）；今天的 `cpa` 打印 `service notes name device disk stt hooks claude`（`claude`：后台服务认不认得 Claude Code，`config.json` 的 `claude_bin`；没找到时说装好以后再 `cpa service install`） |
| `state` | `ok` 好了 · `act` 要你点 · `wait` 等设备 · `warn` 能用但该改 · `off` 可选、没设置 · `skip` 你说不用 · `na` 不适用 · `err` 坏了 |
| `required` | 必需的行（`service notes device`）才数进"还差 N 步" |
| `blocked_by` | 这些行好了之前，这一行的按钮是灰的（设备一行等后台服务和笔记目录） |
| `title`、`detail` | 给人看，过 `display_safe` |
| `fix[]` | 按钮：`{op, label?, cand?}`。`op` 只能是动词表里的：`service.install`、`hooks.install`、`host.name`、`stt.setup`；`notes.dir`、`device.bind`、`device.forget`、`settings.import`、`setup.skip` 留给 docs/27 O2–O6。app 用自己的动词表拼 argv，不照抄行里的任何字符串；不认识的 `op` 不给按钮 |

**从不出现**：`host_id`、USB 序列号（`serial`）、`dev`。板子只用设备码（`sha256("cpa-dev:" + 去冒号小写的序列号)` 前 4 位，大写，docs/27 D6）。`check_setup.py` 在任何深度看到这三个键都判非法。

## `--quiet`

不打印，必需的行都 `ok` 时退出 0，否则 1。安装脚本用它决定最后说"好了"还是列出还差什么。
