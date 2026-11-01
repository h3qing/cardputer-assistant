[English](0002-protocol-v0.md) · **中文**

# 0002 · 协议 v0：单行 2048 B / 正文 500 B、credit 流控、执行只走在线请求

- 状态：已接受
- 日期：2026-10-19
- 相关：05 §6（规范）、05 §1.1、03 §3、04 S5/S6、[protocol/](../../protocol/)

## 背景

- 链路只有 USB-Serial/JTAG（HWCDC）。core 2.0.17 的 HWCDC 接收队列满了会**悄悄丢字节**，`setRxBufferSize` 必须在 `begin` 之前调用（✅ 源码）；设备的第一条回复会被扣在 TX 里，直到主机下次写入（✅ 03 §3）。
- 设备没有 PSRAM，空闲堆约 281KB（✅ 03 §1），单条消息解析的峰值预算 ≤8KB（2KB 行缓冲加 `JsonDocument`）。
- 产品原则：离线记下的东西只会变成笔记或排队项，绝不会在你不在场时被执行（04 原则 2、S5）。

## 候选方案

| 方案 | 结论 | 理由 |
|---|---|---|
| 三份快照合成一条 | 不采用 | 9 个项目加 10 个 session 会超过 2048 B |
| 不做流控，只把 RX 缓冲开大 | 不采用 | HWCDC 溢出时静默丢字节，出了问题无从察觉 |
| 执行类请求也进 outbox，重连后重放 | 不采用 | 违反 S5：离线时误触或丢失的设备可以在你不在场时开 session |
| MessagePack | 推迟 | v0 优先可调试性；ArduinoJson 自带 MessagePack，v0.3 无线链路需要压缩时再评估 |

## 决定

1. **分帧与上限**：NDJSON，一行一个 JSON 对象，**单行 ≤2048 B（含 `\n`）**，两个方向相同。用户正文（note、queue、spawn、reply）**1–500 B UTF-8、单行、无控制字符**。500 B 的正文即使每个字符都要转义（`"`、`\`），加上信封也小于 2048 B。设备接收缓冲设为 8192 B。
2. **credit 流控**：
   - 主机主动发出、还没收到回复的字节总数 **≤4096 B**（接收缓冲的一半）；5 s 没有回复就收回这部分 credit，靠 `pong` 里的 `revs` 对账补发丢失的快照。
   - 设备对每条主机主动发来的消息**恰好回复一条**（`hello`、`pong`、`ack`、`error` 或测试版的 `debug.state`），credit 才算得清。
   - 设备主动请求的在途上限：`outbox.item` ≤4、`agent.*` ≤1、`queue.done`/`inbox.dismiss` ≤1；主机对它们的每条回复 ≤256 B。最坏情况下设备接收缓冲里有 4096 + 6×256 = 5632 B，小于 8192 B。
   - 设备每圈最多处理 4 行入站数据，每秒最多回复 5 条 `error`。
3. **执行只走在线请求**：
   - outbox 里只有 `note` 和 `queue` 两种 kind。契约测试断言 `spawn`、`reply`、`feedback`、`agent.spawn`、`agent.reply` 作为 kind 一律非法。
   - 开 session（`agent.spawn`）和回复（`agent.reply`）只在链路 Ready、并且用户在确认框里按下 Enter 时发出；不持久化，不自动重试，同时最多 1 条。
   - 链路不是 Ready 时确认，结果是一条 `queue` 条目（`intent=spawn|feedback`）。在线请求 30 s 没有结果或链路断开时，文本存成 `uncertain` 的排队项。daemon 收到排队项只记录，从不执行。

## 后果

- 好处：两端的缓冲和延迟都有确定的上限；丢字节可以自愈（rev 对账）；"离线永不执行"由契约测试保证，而不是靠实现者自觉。
- 代价：daemon 序列化快照时要在超过 2048 B 时从尾部减条目；每个新的主机主动消息都必须有回复；离线时想开的 session 要回到在线状态后再从队列里开一次。
- 落地：机器可读的限制在 [`protocol/limits.json`](../../protocol/limits.json)，schema 和向量在 [`protocol/`](../../protocol/)，`check_vectors.py` 进 CI。固件用 `limits.h` 镜像 `limits.json` 并有测试比对；daemon 直接读取。
- 重新评估：v0.2 语音分块传输带宽不够时（04 D13），或 v0.3 换无线链路时。改语义或删字段时 `v` 加 1。
