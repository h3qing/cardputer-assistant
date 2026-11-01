[English](README.md) · **中文**

# 架构决策记录（ADR）

每个影响架构、协议、依赖或安全边界的决定都写成一篇 ADR。写法：

1. 复制 [template.md](template.zh-CN.md)，命名为 `NNNN-短横线标题.md`，编号顺延，不复用。
2. 先以 `提议` 状态提交，讨论定下来后改成 `已接受`。
3. 已接受的 ADR 不改写结论；推翻它时新写一篇，把旧的状态改成 `已被 NNNN 取代` 并互相链接。

| 编号 | 标题 | 状态 |
|---|---|---|
| [0001](0001-toolchain.zh-CN.md) | 固件工具链（A：espressif32@6.10.0 / B：pioarduino 54.03.21-2） | 见该文（由 G2 工具链 spike 维护） |
| [0002](0002-protocol-v0.zh-CN.md) | 协议 v0：上限、credit 流控、执行只走在线请求 | 已接受 |
| [0003](0003-silent-recording-and-consent.zh-CN.md) | 录音不出声；会议确认框和 `consent:"ack"` 保留；录音时 ● 永远看得见 | 已接受 |
| [0004](0004-local-data-access-mcp.md) | 本地数据访问：开放文件是备份，stdio MCP 服务是接口，不做 app 专用集成 | 提议 |

[04 §16](../04-prd.zh-CN.md) 里"需要你拍板的问题"，确认后也各写一篇 ADR。
