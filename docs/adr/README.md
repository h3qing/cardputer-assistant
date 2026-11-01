**English** · [中文](README.zh-CN.md)

# Architecture Decision Records (ADR)

Every decision that affects the architecture, the protocol, dependencies or a security boundary is written up as an ADR. How:

1. Copy [template.md](template.md) and name it `NNNN-kebab-case-title.md`. Numbers go up in order and are never reused.
2. Commit it first with status `Proposed`. Once the discussion settles, change it to `Accepted`.
3. Do not rewrite the conclusion of an accepted ADR. To overturn it, write a new one, change the old one's status to `Superseded by NNNN`, and link the two both ways.

| No. | Title | Status |
|---|---|---|
| [0001](0001-toolchain.md) | Firmware toolchain (A: espressif32@6.10.0 / B: pioarduino 54.03.21-2) | See the ADR (maintained by the G2 toolchain spike) |
| [0002](0002-protocol-v0.md) | Protocol v0: limits, credit flow control, execution only through online requests | Accepted |
| [0003](0003-silent-recording-and-consent.md) | Recording is silent; the meeting confirm box and `consent:"ack"` stay; ● is always visible while recording | Accepted |
| [0004](0004-local-data-access-mcp.md) | Local data access: open files are the backup, a stdio MCP server is the interface, no app-specific integrations | Proposed |

Each of the "questions for you to decide" in [04 §16](../04-prd.md) also gets its own ADR once it is confirmed.
