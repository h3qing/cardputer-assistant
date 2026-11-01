[English](README.md) · **中文**

# 去向预设 v1（docs/25 §2.3）

一份预设 = 一个 app 的接法，纯数据。cpad 随 wheel 发一些（`daemon/src/cpad/dest/presets/v1/`），你自己的放 `$CPA_HOME/presets/*.json`（权限 0600），或者 `cpa dest add --preset-file PATH`。代码里不出现任何预设里的 bundle id 或链接 scheme（`daemon/tests/unit/test_dest_no_app_in_code.py`）。

- `preset.schema.json`：格式。未知字段一律拒收，所以没有地方放密钥。
- `vectors.json`：cpad 必须收下的（`valid`）和必须拒收的（`invalid`）。`schema: true` 表示光靠 schema 就拒收；其余的是 cpad 另外的规矩，见下。

cpad 在 schema 之外还检查：

1. **模板的形状**（`urllib` 解析，不是字符串拼）：`<scheme>://<host>/<path>?名=值&…`；scheme、host、path 是字面量；`{text}` 恰好一次，而且是一个完整的查询值；`{p}`、`{project}` 同样只能是完整的查询值；字面量的值最长 16 个字符，参数名不许像 `key|token|auth|secret|sig`；不许 `#` 和 `\`。`focus` 用同一个 scheme，不带 `{text}`。
2. **能接到哪里**（`presets/v1/deny.json`，数据）：拒绝的 scheme（`http`、`https`、`file`、`data`、`javascript`、`applescript`、`shortcuts`、`x-apple*` 等）、路由（`/mcp/install`、`run-shortcut`、`advanced-uri`、`x-callback-url`）和处理 app（快捷指令、终端、iTerm、脚本编辑器）。加的时候还要求 scheme 在那个 app 的 `CFBundleURLTypes` 里登记过，并记下它的 bundle id 和签名 Team ID；送的时候对不上就 `E_HANDLER_CHANGED`，什么都不打开。
3. **`effect`**：缺了 = 不知道，在这台 Mac 上测试并答"出现了、没自动发"（`cpa dest test ID --send`，`cpa dest verify ID --draft`）之前一律当 `act`（每条在设备上翻完全文按 Enter）。只有随 cpad 发的预设能测成只填草稿；写了 `act` 的、你自己的，永远是 `act`。
4. **`map` 的值**（`--map 项目=值或链接`）：要匹配 `map.pattern`；贴整条链接时用 `map.from_link` 取出值；以已知密钥前缀开头的拒收。
5. `label` 最多 24 个 UTF-8 字节（schema 只能数字符）。

交给 app 的字都带一个同一行的头：`[Cardputer 语音·可能有错字或别人的话·#项目] 正文`（打字是 `[Cardputer 打字·#项目]`），控制字符（含换行）换成空格，长度按 UTF-16 码元数。
