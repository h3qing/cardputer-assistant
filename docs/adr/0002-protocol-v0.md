**English** · [中文](0002-protocol-v0.zh-CN.md)

# 0002 · Protocol v0: 2048 B per line / 500 B body, credit flow control, execution only via online requests

- Status: accepted
- Date: 2026-10-19
- Related: 05 §6 (spec), 05 §1.1, 03 §3, 04 S5/S6, [protocol/](../../protocol/)

## Context

- The only link is USB-Serial/JTAG (HWCDC). In core 2.0.17, when the HWCDC receive queue is full it **silently drops bytes**, and `setRxBufferSize` must be called before `begin` (✅ source code). The device's first reply is held in TX until the host writes again (✅ 03 §3).
- The device has no PSRAM. Free heap is about 281KB (✅ 03 §1). The peak budget for parsing one message is ≤8KB (2KB line buffer plus `JsonDocument`).
- Product principle: what you capture offline only ever becomes a note or a queue item. It is never executed while you are not there (04 principle 2, S5).

## Options

| Option | Verdict | Reason |
|---|---|---|
| Merge the three snapshots into one message | Rejected | 9 projects plus 10 sessions exceed 2048 B |
| No flow control, just a bigger RX buffer | Rejected | HWCDC drops bytes silently on overflow; problems would go unnoticed |
| Put execution requests in the outbox too and replay them on reconnect | Rejected | Violates S5: a device that is bumped by accident or lost while offline could open sessions while you are not there |
| MessagePack | Deferred | v0 favours debuggability; ArduinoJson has MessagePack built in; re-evaluate when the v0.3 wireless link needs compression |

## Decision

1. **Framing and limits**: NDJSON, one JSON object per line, **≤2048 B per line (including `\n`)**, the same in both directions. User bodies (note, queue, spawn, reply) are **1–500 B UTF-8, single line, no control characters**. Even if every character of a 500 B body needs escaping (`"`, `\`), it still fits in 2048 B with the envelope. The device receive buffer is set to 8192 B.
2. **Credit flow control**:
   - The total bytes the host has sent on its own initiative and not yet seen a reply for is **≤4096 B** (half the receive buffer). If no reply comes within 5 s, that credit is reclaimed, and lost snapshots are re-sent by reconciling the `revs` in `pong`.
   - The device replies **exactly once** to each message the host sends on its own initiative (`hello`, `pong`, `ack`, `error` or, in test builds, `debug.state`). That keeps the credit count exact.
   - In-flight limits for device-initiated requests: `outbox.item` ≤4, `agent.*` ≤1, `queue.done`/`inbox.dismiss` ≤1. Each host reply to them is ≤256 B. Worst case, the device receive buffer holds 4096 + 6×256 = 5632 B, below 8192 B.
   - The device handles at most 4 inbound lines per loop and sends at most 5 `error` replies per second.
3. **Execution only via online requests**:
   - The outbox has only two kinds: `note` and `queue`. Contract tests assert that `spawn`, `reply`, `feedback`, `agent.spawn` and `agent.reply` are always invalid as a kind.
   - Opening a session (`agent.spawn`) and replying (`agent.reply`) are sent only when the link is Ready and the user presses Enter in the confirm dialog. They are not persisted, not retried automatically, and at most 1 is in flight.
   - Confirming while the link is not Ready yields a `queue` item (`intent=spawn|feedback`). If an online request has no result after 30 s, or the link drops, the text is saved as an `uncertain` queue item. The daemon only records queue items; it never executes them.

## Consequences

- Benefits: buffers and latency on both ends have fixed upper bounds. Dropped bytes heal themselves (rev reconciliation). "Offline never executes" is guaranteed by contract tests, not by implementer discipline.
- Costs: when the daemon serialises a snapshot over 2048 B it must drop entries from the tail. Every new host-initiated message must get a reply. A session you wanted to open while offline has to be opened again from the queue once you are back online.
- Implementation: machine-readable limits live in [`protocol/limits.json`](../../protocol/limits.json); schemas and vectors in [`protocol/`](../../protocol/); `check_vectors.py` runs in CI. The firmware mirrors `limits.json` in `limits.h`, with a test that compares them; the daemon reads it directly.
- Re-evaluate: when v0.2 chunked voice transfer runs short of bandwidth (04 D13), or when v0.3 moves to a wireless link. Bump `v` when semantics change or a field is removed.
