[English](07-hil-checklist.md) · **中文**

# 07 · HIL 检查单（v0.1、v0.2、v0.3、v0.4.0）

> 06 WP5 的交付物。上半部分是 `tools/hil/run_e2e.py` 在真机上跑出的自动场景结果（05 §9.3 第 1–11 项），下半部分是必须由人动手、动眼或者要另一台硬件的检查。
> **v0.2（M2-D，10 号文档）的部分在 [§5](#5-v02m2sd迁移与-launcher)**：设备测试、SD / 迁移 / Launcher 场景、Launcher 闸门 G-L1–G-L9 和 SD 人工项。§0–§4 是 v0.1 的记录，原样保留。
> **v0.3（M3-H，12 号文档）的部分在 [§6](#6-v03m3录音)**：录音设备测试（G-A6）、录音 HIL 场景（G-A2、G-A4）、v0.2 回归（G-R）和录音人工项（G-A5 耳听（v0.3.1 起不适用）、G-A7、G-A8、G-A9、G-A10、G-A12、G-G5、G-W2、G-E2E）。
> **v0.4.0（V40-H，13 号文档 §17）的部分在 [§7](#7-v040主页tab-菜单锁屏)**：主页、Tab 菜单、锁屏的设备测试和 HIL 场景 `nav` / `menu` / `lock`，v0.3.2 冷启动的 `test_mic_cold` / `rec_cold_start`，v0.1–v0.3.2 回归（插卡和 nosd），以及两项人工检查（揣着锁屏的设备过一天、通读会议确认框文案）。真机部分**待 Lead 跑**，结果栏留空。
> **标记**：`[x]` = 已完成并记下日期和结果；`[ ]` = 还没做。标 **needs user** 的项需要你本人（按键、拨开关、拔线、看屏幕、另一块板子、全新的 macOS 账户、登录过的 claude），编排里的 agent 没有、也不能替你做，结果栏留空，**没有伪造任何结果**。
> 06 WP5 的验收要求这份清单全部勾选（`! grep -q '\[ \]'`）。在 needs user 的项做完以前，这条验收按设计是不通过的。

## 0. 环境

| 项 | 值 |
|---|---|
| 设备 | Cardputer ADV，MAC `02:00:00:00:00:01`，USB 序列号同 MAC，端口 `/dev/cu.usbmodem1101` |
| 固件 | `cardputer-adv-test`（`-DCPA_TEST_HOOKS=1`），`cpa-fw/0.1.0`，2026-10-19 20:33 构建并刷入（WP4 fix 之后，源码没有更新的改动） |
| Mac 端 | `cpad 0.1.0`（本仓库 `daemon/`，Python 3.12.13），在 `run_e2e.py` 进程里运行，临时 `$CPA_HOME`、临时 notes_dir、临时项目目录 |
| Claude | `daemon/tests/fixtures/bin/fake-claude`（2.1.272 的输出形状），由 config 的 `claude_bin` + `spawn_env` 指定；**没有**启动任何真实的 Claude session |
| 系统 | Apple 芯片的 Mac，macOS 26 |

## 1. 怎么跑

在仓库根目录执行（脚本自己检查端口有没有被别的程序占用，然后在临时目录里搭好 daemon，结束后删掉）：

```bash
cpa pause 1 || true     # 如果你装了 cpad 服务，先让它放开串口（或 uv run --project daemon cpa pause 600）
uv run --project daemon python tools/hil/run_e2e.py --scenarios all --report "$TMPDIR/hil.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios soak --minutes 60 --report "$TMPDIR/soak.json"
uv run --project daemon pytest tools/hil/tests -q   # 脚本自身的单元测试
```

- `--scenarios` 可以是逗号列表；`all` = 下表第 1–10 项。`soak` 和 `real_claude_smoke` 只在点名时运行，后者还必须单独运行并给出 `--project-dir`。
- 报告：`--report` 指定的文件里每个场景一个 JSON 对象（`ok`、`checks[]`、指标），另外每个场景单独写一份 `<报告名>.<场景>.json`，并在 stdout 上逐行输出。失败的场景还会带上 daemon 状态和最后 60 行串口记录（`wire_tail`）。每个场景都带 `device_not_ready`：设备回 `E_NOT_READY` 的次数（见 §3）。
- `--keep` 保留临时目录（`$CPA_HOME/logs/cpad.log`、`events.jsonl`、笔记、fake-claude 的 `calls.jsonl`），用来排查。

### 实现方式（和 05 §9.3 字面写法不同的地方）

| 场景 | 做法 |
|---|---|
| 驱动设备 | daemon 就在测试进程里运行，测试版固件的 `debug.key` / `debug.state` / `debug.reboot` 走 daemon 自己的 `DeviceLink.request_raw`（同一套 ID、credit 窗口和空行 nudge）。串口对象外面包了一层 tap（`serial_factory`），记录两个方向的每一行；SafeOpen 照常执行 |
| 3 离线补发、8 离线开 session | 设备只在 Ready 时接受 `debug.key`。所以先在 Ready 时把按键整批塞进设备的注入队列（16 条）：开头 4 次各 5 秒的单独 Shift（不产生按键事件），然后才是真正的按键；接着立刻 `cpa pause 120`。设备 15 秒收不到数据就退回 Attached（离线），这之后才轮到真正的按键。限于队列长度，离线补发的 3 条笔记每条 2 个字符 |
| 4 丢 ack | cpad 没有测试开关。由 tap 在写入串口之前吞掉第一条 `outbox.ack`（daemon 以为已经发出），设备 3 秒后重发，daemon 回 `duplicate` |
| 9 延迟 | 看板：从写 fake-claude 的 `state.json` 到第一次 `debug.state` 回复显示新计数（主机计时，含 0.1 秒的轮询间隔，偏保守）；每次翻转都紧接在上一次被看到之后，也就是刚好在 daemon 3 秒轮询之后，接近最坏相位。Compose：用设备时间戳 `overlay_ms − key_ms`（G0 按下到 Compose 打开，注入的 G0 按住 50 ms，G0Tap 在松开时触发）；主机从发出到看到 Compose 的时间一并记录 |
| 10 中文 | 名字取自 06 里引用的本机真实 session 名和 daemon 的脱敏 fixture，没有读 `~/.claude`。缺字用固件链接的 `efontCN_12` 字形表在主机上逐字核对（`harness/glyphs.py`，读 PlatformIO libdeps 里的 M5GFX 源码）；屏幕上实际看起来怎样要人看（§4 第 9 项） |
| 11 浸泡 | daemon 的 `poll_s` 临时改成 1 秒，fake-claude 每秒翻转一次状态，测试脚本每秒额外发一个 `ping`，每分钟在设备上打一条笔记。"TX 卡住" = 任何一条主机请求 5 秒内没有回复 |

## 2. 自动场景结果

**最终一次完整验收（plug_filter 修复之后）**：2026-10-19。固件是 `cardputer-adv-test`，21:44 构建，包含 `lib/link/plug_filter` 去抖修复（见 §3），21:46 用 `pio run -e cardputer-adv-test -t upload` 刷入，没有按键。

- 21:46–21:53：`--scenarios handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk`，**10/10 PASS，退出码 0**。
- 21:53–22:03：`--scenarios soak --minutes 10`，**PASS，退出码 0**。
- 刷入前 native 测试 215/215 通过，`tools/hil/tests` 121 个测试通过。
- 修复前的结果：21:29–21:35 那次完整运行也是 10/10 PASS。同一天更早还有一次运行是 9/10：延迟场景因为 §3 的问题中止。下面括号里写的"修复前"数值都来自这几次运行。

- [x] **1 握手不复位**（2026-10-19，PASS）
  - 首次握手 0.003 s，`cpa pause` 后重新打开端口也是 0.003 s，两次都只发了 1 次 hello（上限 3 s）。修复前是 0.003–0.029 s。
  - boot 前后都是 `039T61N-0000E`，没变。`up_ms` 从 7088 连续增长到 9168，和主机时钟差 0 ms。daemon 没有判定重启。
  - 刷机后第一次开机，`rst=unknown`。这是正常的：USB 触发的复位在固件里读到的原因就是 0，见 03 §4。
- [x] **2 速记闭环**（2026-10-19，PASS）
  - G0 → 输入 → Enter，0.103 s 后笔记出现在 notes_dir（上限 2 s）。修复前是 0.101–0.122 s。
  - 只写了一次，`item.received` 一条，设备 outbox 清空。
- [x] **3 离线补发**（2026-10-19，PASS）
  - 暂停期间入库 0 条。恢复后设备 hello 报告 `pending=3`。
  - 3 条笔记各出现一次，正好新增 3 条，outbox 清空。
- [x] **4 丢 ack 去重**（2026-10-19，PASS）
  - 第一条 ack 被吞掉，设备共发了 2 次，第二次 daemon 回 `duplicate`。
  - 笔记一条，`item.received` 一条，outbox 清空。
- [x] **5 debug.reboot 重新同步**（2026-10-19，PASS）
  - boot id 从 `039T61N-0000E` 变成 `-0000F`。新的 `up_ms` 是 1444、`rst=software`。
  - daemon 计 1 次重启，重发了 `time.set` 和三份快照。1.65 s 后重新 Ready，之后设备 revs 和主机一致。修复前是 1.65–5.06 s。
  - 这个场景里出现 1 次 `E_NOT_READY`，是预期的：刚重启的设备处于 Attached，daemon 的下一个 ping 会收到它，然后重新握手。
- [x] **6 看板计数**（2026-10-19，PASS）
  - fake-claude 给项目 1 两个 session（busy + waiting）。设备上 slot 1 显示 `run=1 need=1`，0.15 s 更新（上限 5 s）。
  - 修复前是 2.5–3.1 s。这个时间取决于写入 fake-claude 时离 daemon 下一次 3 秒轮询还有多久，这次刚好赶在轮询之前。最坏相位见第 9 项。
- [x] **7 开 session 的 argv**（2026-10-19，PASS）
  - fake-claude 只被调用一次，argv 是 `--bg --name cpa/hil-spawn-gw5m --permission-mode plan -- hil spawn gw5m`，cwd 是项目根目录（realpath）。
  - 设备收到 `agent.result ok`，short 是 `a0000001`。有 `agent.spawned` 事件。
- [x] **8 离线开 session**（2026-10-19，PASS）
  - Confirm 在离线时按下，恢复后设备 hello 报告 `pending=1`。
  - daemon 记下 `queue.added`：qid `q1`，intent `spawn`，origin `device`，不是 uncertain。
  - fake-claude 开 session 0 次，设备从没发出 `agent.spawn`，设备上项目 1 显示排队数。
- [x] **9 延迟**（2026-10-19，PASS）
  - 看板：100 次翻转，`p95_board_s` = 3.098 s（上限 5 s）。p50 3.015 s，max 3.128 s，poll 3 s。
  - Compose：30 次 G0，`p95_compose_ms` = 51 ms（上限 300 ms，用设备时间戳）。主机看到的 p95 是 112.9 ms。
  - 这个场景跑了 309.8 s，`E_NOT_READY` 0 次。修复前，第一次完整运行在 168 s 时出现 1 次（§3）。
- [x] **10 中文渲染**（2026-10-19，PASS）
  - 6 个中文名的快照被 ack，没有错误回复。其中一个是 54 字节的长名，被 daemon 按字符边界截成 `登录页面改版的会议录`（30 字节）。
  - 项目页和收件箱都打开过。`efontCN_12`（7545 个字形）对所有名字和项目名都是 **0 缺字**。
- [x] **11 浸泡 10 分钟，修复后**（2026-10-19 21:53–22:03，PASS；**编排要求本次只跑 10 分钟**）
  - 设备上打了 10 条笔记，0 丢失、0 重复。
  - 600 次每秒翻转，1126 份快照。2028 次主机请求全部有回复，0 次 TX 卡住。回复时间 p50 2.8 ms、p95 22.0 ms、最长 112.3 ms，0 次超过 1 s，0 次空行 nudge。
  - 端口一次也没重新打开，结束时 outbox 为空。
  - **`device_not_ready` = 0**。cpad 日志里整段只有开头那 1 次 `device ready`（boot `039T61N-0000F`），没有在同一条链路上再次握手。
- [x] **11 浸泡 20 分钟，修复前**（2026-10-19，PASS；编排要求只跑 20 分钟）
  - 设备上打了 20 条笔记，0 丢失、0 重复。
  - 1199 次翻转，4076 次请求全部有回复，0 次 TX 卡住。
  - 但 `device_not_ready` = 4：cpad 在同一条链路上重新握手 3 次（+293 s、+667 s、+1065 s，boot 没变）。这就是 §3 的问题。
- 修复效果的统计意义：修复前大约每 300–400 s 掉一次 Ready（20 分钟浸泡里 4 次 `E_NOT_READY`、3 次重新握手）。修复后，延迟场景加浸泡共约 920 s 处于持续流量下，0 次掉线。按修复前的频率，期望 2–3 次，一次都不出现的概率约 5–10%。这和"修复有效"一致，但时间太短，还不能算证明。60 分钟浸泡（下一项）才是 G6 的判定。
- [ ] **needs user** · **11 浸泡 60 分钟**（G6 v0.1 的要求）
  - 怎么做：留出 1 小时，运行 `uv run --project daemon python tools/hil/run_e2e.py --scenarios soak --minutes 60 --report "$TMPDIR/soak.json"`。
  - 通过标准：报告里 `soak.ok` 为 true（0 丢失、0 重复、0 次 TX 卡住）。另外也看 `soak.device_not_ready`，修复后应该是 0。
  - 日期：＿＿ 结果：＿＿

## 3. 发现的问题

- [ ] **needs user**（固件负责人）· **设备在 USB 一直插着、主机一直 Ready 时偶尔掉出 Ready**（回 `E_NOT_READY`）。
  - 证据：2026-10-19 第一次完整验收，延迟场景进行到 168 s 时一次 `debug.state` 收到 `error E_NOT_READY`，当时脚本把它当成失败中止（之后改成等重新握手再重发，并计数）；同一场景单独重跑 310 s 没有出现。20 分钟浸泡里出现 4 次 `E_NOT_READY`，cpad 的日志在同一条链路上记了 3 次额外的 `device ready`（+293 s、+667 s、+1065 s；boot 都是 `039T61N-0000C`，没有 `link closed`，所以不是重启也不是端口重开），离上一次存笔记分别 38 s、52 s、30 s。
  - 推断的原因（没有在设备上证实）：期间主机每秒都有数据，设备 15 s 收不到数据的超时不可能触发，剩下的路径只有 `UsbTransport::plugged()`（`HWCDC::isPlugged()`）短暂为假 → Detached → Attached。Arduino core 2.0.17 的 `HWCDC.cpp` 第 47–68 行用 FreeRTOS tick hook 判断 SOF：允许漏掉的 tick 数（5）只在"断开→连上"时才重置，连接期间零星漏掉的 SOF（比如 tick 和 1 kHz 的 SOF 相位漂移、Flash 写入时 tick 被推迟）会一直累积，累计第 6 次就报一次"拔出"。
  - 影响：cpad 会自己重新握手，所以平时只是状态栏短暂闪一下、在途的 outbox 条目退回重发（去重保证不重复）。但如果正好有 `agent.spawn` 在途，设备会把它当成"结果未知"存成 uncertain 排队项，而 Mac 上的 session 可能已经开出来了。
  - 日期：＿＿ 结论：＿＿

## 4. 需要人做的检查

每一项做完以后把 `[ ]` 改成 `[x]`，填上日期和结果。

- [ ] **needs user** · **1. G1 键盘：抄打 10 分钟，CER ≤0.5%，没有超过 1 秒的卡顿**
  - 怎么做：刷正式版固件（`pio run -e cardputer-adv -t upload`），daemon 运行。准备一段已知的英文 ASCII 文本，按 G0 逐句抄打，每句 Enter 保存一次（单条最多 500 字节），共 10 分钟。
  - 判定：把当天笔记里的正文和原文逐字比较，CER =（替换 + 删除 + 插入）/ 原文字符数。期间留意有没有超过 1 秒才上屏的情况；系统页（Opt+M）的"幽灵 N 重置 N"记下来。
  - 日期：＿＿ CER：＿＿ 最长卡顿：＿＿ 幽灵 / 重置：＿＿
- [ ] **needs user** · **2. 10 次真实断电（G10）**
  - 怎么做：daemon 先 `cpa pause 1800`（让条目留在设备上）。每次：G0 输入一条带编号的笔记，看到"已保存"后**立刻**把侧面开关拨到 OFF 并拔掉 USB 线，等几秒，再插上、拨回 ON。重复 10 次，然后 `cpa pause 1` 让 daemon 恢复。
  - 判定：10 条笔记全部出现在 notes_dir，没有重复；系统页没有"不变式被打破"。
  - 日期：＿＿ 结果：＿＿/10，重复：＿＿
- [ ] **needs user** · **3. 拔掉再插上 USB**
  - 怎么做：daemon 运行、设备 `● 在线`。开关保持 ON，拔掉 USB 线 10 秒（期间用电池运行，记 1–2 条笔记），再插上。重复 3 次。
  - 判定：拔线时状态栏变成 `○ 离线`，插回后几秒内回到 `● 在线`；离线时记的笔记同步过来；设备没有重启（系统页 boot 不变）。
  - 日期：＿＿ 结果：＿＿
- [ ] **needs user** · **4. 电池时关背光与唤醒**
  - 怎么做：拔掉 USB 线（开关 ON），60 秒不按键；再分别用普通键和 G0 唤醒。再接一个**只能充电、没有数据**的充电器重复一次。
  - 判定：60 秒后背光关闭；普通键只点亮屏幕、不触发动作；G0 点亮后直接进入速记；只接充电器时也按电池处理（60 秒后关背光）。
  - 日期：＿＿ 结果：＿＿
- [ ] **needs user** · **5. G0 长按和下载模式（E5 演练）**
  - 怎么做：固件运行时按住 G0 超过 0.5 秒；再短按一次。然后做一次 E5 恢复演练：侧面开关拨到 OFF，拔线，等几秒再插上。
  - 判定：长按弹出"语音在 v0.2 支持"，短按打开速记；运行中按 G0 从不进入下载模式；E5 的恢复步骤（README）有效。
  - 日期：＿＿ 结果：＿＿
- [ ] **needs user** · **6. 插一块别的 ESP32-S3 板子（E16）**
  - 怎么做：daemon 运行。(a) 已绑定序列号时插上另一块 `303A:1001` 的板子；(b) `cpa device forget` 解除绑定、拔掉 Cardputer，只插那块板子。
  - 判定：(a) daemon 完全不碰它；(b) 6 秒内放开端口，30 秒内不再尝试（`cpa status` 的 `last_release` / `backoff_s`、`logs/cpad.log`）。
  - 日期：＿＿ 结果：＿＿
- [ ] **needs user** · **7. README 快速上手在全新 macOS 账户里计时（≤15 分钟）**
  - 怎么做：新建一个 macOS 用户，从 `git clone` 开始，严格按 README"快速上手"做到设备显示 `● 在线` 和项目列表，全程计时。包括 README 第 3 步里出厂固件第一次进下载模式的按键顺序（G0 + BtnRst，⏳ 未在 ADV 上验证）。
  - 判定：≤15 分钟，没有 README 以外的步骤；记下卡住的地方。
  - 日期：＿＿ 用时：＿＿ 问题：＿＿
- [ ] **needs user** · **8. 真实 claude 冒烟（只能人工触发）**
  - 怎么做：在已登录的终端里（`claude auth status` 为 `loggedIn: true`），接上测试版固件，运行 `uv run --project daemon python tools/hil/run_e2e.py --scenarios real_claude_smoke --project-dir "$(mktemp -d)"`。它会从设备以 plan 模式开**一个**真实 session（正文 "Reply with the single word OK"），确认它出现在那个临时目录下，再 `claude stop` + `claude rm`。
  - 判定：报告里 `real_claude_smoke.ok` 为 true；`claude agents --json --all` 里不再有它。
  - 日期：＿＿ 结果：＿＿
- [ ] **needs user** · **9. 中文渲染目视检查**
  - 怎么做：跑 `--scenarios cjk --keep`（或者在真实环境里打开项目页 Opt+1 和收件箱 Opt+I），看屏幕。
  - 判定：名字可读、没有 □（字形表核对为 0 缺字）、截断的长名末尾没有半个字、行不重叠。
  - 日期：＿＿ 结果：＿＿

## 5. v0.2（M2）：SD、迁移与 Launcher

> M2-D（[10 号文档](10-m2-work-packages.zh-CN.md)）的交付物。自动部分由独占设备的 agent 在 2026-10-20 跑完；Launcher 闸门和 SD 人工项都要人动手，标 **needs user**，结果栏留空。§5.9 第 1、2 条修好以后的最终复跑和刷回正式版见 §5.10。

### 5.0 环境

| 项 | 值 |
|---|---|
| 设备 | 同 §0（MAC `02:00:00:00:00:01`，`/dev/cu.usbmodem1101`），USB 供电，侧面开关 OFF |
| SD 卡 | 开发卡：SDXC 512GB，FAT32（FatFs 格式化，1 个 FAT，簇 32 KB，见 sd-card.md §3），卡上有 `/cpa/` 目录；全程插着 |
| 固件 | `release/v0.2.0` 的 HEAD `a6114eb`（PR #18；固件没有本地改动，`tools/hil` 是本次还没提交的改动）。`cardputer-adv-test`（`-DCPA_TEST_HOOKS=1`）和 `cardputer-adv-test-nosd`（再加 `-DCPA_STORAGE_NO_SD=1`），都用 `pio run -e <env> -t upload` 刷入，没按任何键，没碰 G0 |
| Mac 端 | `run_e2e.py` 进程里的 `cpad`（本仓库 `daemon/`），临时 `$CPA_HOME`、notes_dir、项目目录，fake-claude；用户自己的 LaunchAgent `cpad 0.1.0` 全程 `cpa pause 5400`（中途续过），没碰用户的 `~/CardputerNotes`、`~/.claude` 和 LaunchAgent 配置 |
| 刷机注意 | `firmware/app/scripts/pre_upload.py` 在上传前会执行 `cpa pause 120`，这会把上面的长暂停**缩短**成 120 s，之后真实的 cpad 可能在 HIL 进行中接上设备。所以本次所有 `pio run -t upload` / `pio test` 都在一个**不含 `cpa`** 的 `PATH` 里运行（只放了 `pio`、`uv`、`uvx` 的链接），日志里是 `pre_upload: skip: cpa not found`。见 §5.9 第 2 条：已修，§5.10 的最终复跑在正常 `PATH` 下进行，日志里是 `pre_upload: skip: already paused for 3446 s` |

### 5.1 怎么跑

```bash
cpa pause 5400; lsof /dev/cu.usbmodem*          # 串口要空
cd firmware/app
pio test -e cardputer-adv-test -f 'device/test_storage' -f 'device/test_sd_store' -f 'device/test_runtime_budget'
pio run -e cardputer-adv-test -t upload && cd ../..
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,sd_store,sd_bulk,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk --report "$TMPDIR/hil-sd.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios back_keys --report "$TMPDIR/hil-keys.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios soak --minutes 10 --report "$TMPDIR/soak-sd.json"
# 迁移（10 号文档 M2-D 验收的顺序；state 文件在三个阶段之间传递种下的条目）
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty --report "$TMPDIR/m0.json"
(cd firmware/app && pio run -e cardputer-adv-test-nosd -t upload)
uv run --project daemon python tools/hil/run_e2e.py --scenarios migrate_seed --state "$TMPDIR/mig.json" --report "$TMPDIR/m1.json"
(cd firmware/app && pio run -e cardputer-adv-test -t upload)
uv run --project daemon python tools/hil/run_e2e.py --scenarios migrate_check --state "$TMPDIR/mig.json" --report "$TMPDIR/m2.json"
(cd firmware/app && pio run -e cardputer-adv-test-nosd -t upload)
uv run --project daemon python tools/hil/run_e2e.py --scenarios migrate_drained --state "$TMPDIR/mig.json" --report "$TMPDIR/m3.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk --report "$TMPDIR/hil-nosd.json"
uv run --project daemon pytest tools/hil/tests -q
```

- 新参数：`--state FILE`（只给 `migrate_*` 用；每个迁移阶段必须单独运行，`migrate_check` / `migrate_drained` 要求文件已由 `migrate_seed` 写好）；`--expect-launcher`（`launcher_boot` 必须带它；带了它而设备 `hello.launcher` 不是 true 时，整次运行在第一个场景之前就失败）。
- `all` 仍然只是 v0.1 的 10 个场景；v0.2 的场景都要点名。报告的 `meta` 里多了 `store`、`launcher`、`expect_launcher`；`handshake` 的结果里也带 `store` / `launcher`。

### 5.2 实现方式

| 场景 | 做法 |
|---|---|
| "离线"记很多条（`sd_store`、`sd_bulk`、`migrate_seed`） | 设备只在 Ready 时接受 `debug.key`，注入队列只有 16 条，所以没法在真离线时打几十条笔记。改为由 tap 扣住设备发出的 `outbox.item`（`Tap.hold_rx`），直到下一条设备 `hello`：设备一直 Ready、照常收按键，但 daemon 一条也听不到，条目全部留在设备的存储上，效果和离线一样。下一次握手（重启或重新打开端口）结束扣留，同时设备把在途条目退回待发（05 §5.8）。tap 现在只把完整的行交给 daemon（daemon 自己的分行器本来也要等换行） |
| `sd_store` | 扣住 → 打 3 条 → `debug.state` 待发 3 → `debug.reboot` → 新 boot 的 hello 报 `pending=3`、`store=sd` → 每条恰好送达一次 |
| `sd_bulk` | 扣住 → 打 100 条（`<字母>00`–`<字母>99`）→ 待发 100 → `cpa pause 2` 再恢复（重新握手）→ 计时从重新打开后设备 `hello` 到达，到最后一条的 `stored` ack 发出（都是 tap 上的主机时间） |
| 迁移三阶段 | `migrate_seed`（nosd）：确认 `store=int` 且开始时待发 0，扣住后打 20 条，把设备号、boot 号和 20 条正文写进 `--state`，扣留一直持续到运行结束（不会送达）。`migrate_check`（test）：首次 hello `store=sd`、`pending=20`、同一台设备；20 条每条恰好一次，本次运行收到的 id 正好是这 20 条的 id、都以种下时的 boot 号开头（迁移保留 id）；把 id 写回 state。`migrate_drained`（nosd）：首次 hello `store=int`、`pending=0`，本次运行一条也没收到 |
| `launcher_boot` | 本次运行第一次握手的 hello：`launcher==true`、`rst=="software"`、`store` 是三种之一。要放在任何会重启设备的场景之前（选择顺序已保证它排第一） |
| `back_keys`（#15） | `debug.key` 注入：G0 打开 Compose → 空的时候按一次 `bksp` 关掉；有一个字时第一次 `bksp` 删字、仍在 Compose，第二次才关；Opt+I 收件箱、Opt+M 系统页，各按一次左上角的 `` ` `` 回到 board |

### 5.3 设备测试（`pio test -e cardputer-adv-test -f 'device/*'`，插卡）

2026-10-20 17:00–17:05，`-f 'device/test_storage' -f 'device/test_sd_store' -f 'device/test_runtime_budget'`（`test_sd_absent` 要拔卡，见 §5.7 SD-3）。结果：18 个用例 16 过 2 不过，两个不过的都是 v0.1 的 LittleFS 保存计时（§5.9 第 1 条）。

- [x] **`test_runtime_budget`**（2026-10-20，PASS 2/2，SD 已挂载且为主存储，工作负载含一次 SD 上的拒收 ack）
  - `stack_left=5844` B（≥4 KB）· `heap_free=235048` B（229.5 KB，≥150 KB）· `heap_min=224484` B · `max_block=212980` B（208.0 KB，≥64 KB）。
  - `sent=22 pending=0 bad=1 store_errors=0 missing_glyphs=0`；8 KB 串口突发 `got=8192 bad=0`。
- [x] **`test_sd_store`**（2026-10-20，PASS 7/7，root `/cpatest`，测完删掉）
  - 挂载：`state=3`（Ok）`size_mib=479772` `hz=20000000`；挂载 + `prepare()` **17.6 ms**（≤200 ms），挂载增加的堆 **15068 B**（≤16 KB，余量只有约 1.3 KB）。
  - 200 条保存，每条立即 ack：p50 9.1 ms、**p95 10.5 ms**、最长 51.1 ms；新 epoch 目录的第一条 **120.9 ms**（≤250 ms）。
  - 200 条全部积压：p50 15.3 ms、**p95 20.2 ms**、最长 90.9 ms（≤150 ms）；第一条 78.2 ms。
  - list 还原出 MonoId；LFS → SD 迁移 50 条：moved 50、failed 0，共 2805 ms（**每条 56 ms**；09 §5 估算约 40 ms）。
- [x] **`test_storage`**（2026-10-20，**FAIL 7/9**，跑了两次；后续见 §5.9 第 1 条）
  - 通过：`cpafs` 挂载、标签和大小（`CPA_PART launcher=0 cpafs=1572864 legacy_spiffs=0`，32 个分片）、读写改名、NVS 开机计数（`boot=20 (was 19)`）、分片列表。
  - 不过（第 1 次运行）：LittleFS 200 条保存，立即 ack 的 p95 **101.4 ms**（p50 41.4 ms、最长 107.4 ms），积压的 p95 **120.2 ms**（p50 47.0 ms、最长 195.0 ms），上限都是 100 ms。第 2 次运行：立即 ack 的通过，积压的仍不过。
  - 这是 v0.1 就有的 LittleFS 慢（09 §5 写明"LittleFS 积压 200 条时 108–158ms，是已知问题"），SD 为主时不走这条路；但测试还按 ≤100 ms 断言，所以 `-f 'device/*'` 不会全绿。已交回固件负责人（§5.9 第 1 条）。
  - **修复后复跑（2026-10-20 17:48–17:50，PASS 9/9，跑了两次）**：两个计时用例改名为 `*_within_200ms`，只断言 p95 ≤ 200 ms，100 ms 目标改为打印 `CPA_SAVE_TARGET`。第 2 次（`-v`）：立即 ack 的 p95 74.9 ms（`met`），积压的 p95 147.0 ms、最长 185.0 ms（`missed (known on LittleFS)`）。见 §5.10。
- [ ] **needs user** · **`test_sd_absent`**：要拔卡，见 §5.7 SD-3。

### 5.4 自动场景结果：`cardputer-adv-test`，插卡（SD 为主）

固件 17:06 刷入（`cpa-fw/0.2.0`，`hello.store=sd`、`launcher=false`）。

- 17:07–17:14 第一次完整运行：12/13。`handshake` 的 `no_reboot_detected` 不过：它要求 daemon 的重启计数为 0，但同一次运行里 `sd_store` 已经重启过一次设备（计数 1）。这是 harness 的问题，不是设备的：改成比较场景前后的计数（`scenarios/link.py`），重跑。
- **17:14–17:22 第二次完整运行（验收命令）：13/13 PASS，退出码 0**；接着 `back_keys` 1/1 PASS。

- [x] **sd_empty**（2026-10-20，PASS）`hello.store=sd`，连上时 `pending=0`。
- [x] **sd_store**（2026-10-20，PASS）
  - 3 条在 daemon 听不到的情况下存到 SD，`debug.state` 待发 3；`debug.reboot` 后新 boot（`039T61N-0000S` → `-0000T`）的 hello 报 `store=sd`、`pending=3`。
  - 3 条各送达一次，本场景收到的 id 正好是这 3 条（第一次运行同样 PASS）。重启时 1 次 `E_NOT_READY`，是预期的（同 §2 第 5 项）。
- [x] **sd_bulk**（2026-10-20，PASS）
  - 100 条积压在 SD 上（打字 50.7 s），重新握手时 hello 报 `pending=100`。
  - 从设备 hello 到最后一条 `stored` ack：**1.55 s**（上限 5 s；第一次运行 1.546 s）。100 条每条恰好一次，没有别的或重复的 id，结束时 outbox 为空。
- [x] **v0.1 的 10 个场景，SD 为主**（2026-10-20，10/10 PASS）
  - handshake：首次和重开都是 0.003 s、1 次 hello；boot 不变，`up_ms` 连续（漂移 0 ms）；场景前后重启计数都是 1（没有新的重启）。结果里 `store=sd`、`launcher=false`、`rst=software`。
  - capture：Enter 到笔记 **0.104 s**（上限 2 s），写一次。
  - offline_replay：真离线（`cpa pause 120`）3 条，恢复时 `pending=3`，各一次。
  - dup_ack：第一条 ack 被吞，设备发了 2 次，第二次回 `duplicate`，笔记一条。
  - reboot：重新 Ready **1.34 s**，`rst=software`，重发 `time.set` 和三份快照，revs 一致。
  - board：slot 1 显示 `run=1 need=1`，3.08 s 更新（上限 5 s；取决于离 daemon 3 s 轮询的相位）。
  - spawn：argv 一字不差（`--bg --name cpa/hil-spawn-… --permission-mode plan -- …`），cwd 是项目根。
  - offline_spawn：离线 Confirm → `queue.added`（intent spawn，不是 uncertain），fake-claude 0 次调用。
  - latency：看板 `p95_board_s` **3.116 s**（上限 5 s），Compose `p95_compose_ms` **50 ms**（上限 300 ms，设备时间戳）；主机看到的 p95 125.9 ms。310.6 s 内 `E_NOT_READY` 0 次。
  - cjk：6 个中文名被 ack，长名截成 `登录页面改版的会议录`，`efontCN_12` 0 缺字。
- [x] **back_keys（#15 单键返回）**（2026-10-20，PASS 8/8）
  - G0 打开 Compose → 空的时候按 `bksp` → Compose 关闭（`board/none`）。
  - 打一个字后第一次 `bksp` 删字、仍是 `board/compose`；第二次 `bksp` 才关闭。
  - Opt+I 收件箱 → `` ` `` → `board/none`；Opt+M 系统页 → `` ` `` → `board/none`。
- [x] **浸泡 10 分钟，SD 为主**（2026-10-20 17:22–17:32，PASS 6/6，退出码 0）
  - 设备上打了 10 条笔记，0 丢失、0 重复。
  - 600 次每秒翻转，1118 份快照，2020 次主机请求全部有回复，0 次 TX 卡住；回复 p50 2.7 ms、p95 17.9 ms、最长 37.4 ms，0 次超过 1 s，0 次空行 nudge。
  - 端口一次也没重新打开，`device_not_ready` = 0，结束时 outbox 为空。
  - 60 分钟浸泡仍是 §2 的 needs user 项（v0.1 的 G6），这次编排只要求 10 分钟。

### 5.5 迁移（nosd → test → nosd）和 nosd 回归

- [x] **迁移三阶段**（2026-10-20 17:32–17:34，**4/4 PASS**，每步退出码 0；固件都用 `pio run -e <env> -t upload` 换，保留 NVS 和 `cpafs`）
  - 前置 `sd_empty`（test）：`store=sd`，`pending=0`。
  - `migrate_seed`（nosd，5/5）：`store=int`，开始时待发 0；20 条（`mn00`–`mn19`）在 daemon 听不到的情况下存进 `cpafs`，待发 20，一条也没送达；state 记下 boot `039T61N-0000W`。
  - `migrate_check`（test，7/7）：首次 hello `store=sd`、`pending=20`、同一台设备；20 条每条恰好一次，收到的 id 正好是 `039T61N-0000W-0000` … `-000K` 这 20 个（迁移保留了 id），没有别的 id；结束时 outbox 为空。
  - `migrate_drained`（nosd，5/5）：首次 hello `store=int`、`pending=0`，`debug.state` 待发 0，本次一条也没收到：迁移已经把 `cpafs` 清空。
  - 没验证的：开机迁移超过 0.5 s 时的"正在迁移到 SD 卡 i/n"画面和系统页的 `已迁移 20`，要人看屏幕（20 条按 §5.3 的每条 56 ms 算约 1.1 s，应该会显示）。
- [x] **v0.1 的 10 个场景，`cardputer-adv-test-nosd`（卡仍插着）**（2026-10-20 17:33–17:40，**10/10 PASS，退出码 0**）
  - handshake 的结果里 `store=int`、`launcher=false`（nosd 不碰 SD 总线，内部 `cpafs` 为主，和 v0.1 一样）；首次和重开握手都是 0.003 s。
  - capture 0.101 s；reboot 重新 Ready 1.66 s；latency `p95_board_s` 3.092 s、`p95_compose_ms` 51 ms；cjk 0 缺字。
  - 只有 reboot 场景里 1 次预期的 `E_NOT_READY`。v0.1 的行为没变。
- [x] **结束状态**（2026-10-20 17:40）：按编排要求刷回 `cardputer-adv-test`（不是 08 §4 第 5 条的正式版，后面的复跑阶段还要用），`sd_empty` PASS（`store=sd`、`pending=0`）；用户自己的 cpad 仍在暂停中（17:41 最后一次 `cpa pause 5400`，到 19:11 左右），串口空闲。最终状态见 §5.10：18:02 已刷回正式版，daemon 已恢复。

### 5.6 Launcher 闸门（launcher.md §8，09 §9.4）

前提：Launcher 用 Lead 定的正式版 **2.9.1**（官方 release 资产，1,422,720 B；下载后先核对大小，再记下 SHA-256）。测试镜像用 §5.8 的命令打。

- [x] **G-L1 装 Launcher，装我们的固件（实际走串口安装）**
  - 怎么做：先做 §5.8 的第 0 步。用网页刷写器或 esptool 装 Launcher 2.9.1；把 `cpa-v0.2.0-hil.bin` 放到卡上 `/cpa/fw/`；在 Launcher 里选 SD → 这个文件 → 安装。如果 Launcher 读不了这张 512GB 卡（很可能有同样的 ACMD41 问题，sd-card.md §9），改用 Launcher 的串口安装 `flash overwrite`；要不要给上游提修复，先问用户。
  - 判定：我们的固件能启动；`hello` 正常；系统页第三行是 `启动 Launcher`；插着卡时存储行是 `存储 SD …`，拔卡开机时是 `存储 内部 448…K`（`cpafs` = 0x70000，大小只在内部为主时显示）。然后跑 `uv run --project daemon python tools/hil/run_e2e.py --scenarios launcher_boot,handshake,capture,reboot --expect-launcher --report "$TMPDIR/hil-launcher.json"`，退出码 0（`launcher_boot`：`hello.launcher==true`、`rst=="software"`）。
  - 日期：2026-10-20 结果：**通过（部分）**。Launcher 2.9.1（官方 release，1,422,720 B，SHA-256 `2adff1ad67b5e08319dae682dc4edb01f1d857767c1738bdd0ccd097a8acb024`）用 esptool 写到 0x0，刷完后经 USB 复位直接进入 Launcher（这时还没有 ota 分区）。接着用 Launcher 串口命令 `flash firmware <名字> <大小>`（READY/ACK 分块，约 10.7 KB/s）依次装了 `factory-v0.9`（出厂 app，2,600,704 B）和 `cpa-v0.2.0`（`cpa-v0.2.0-rc1-app.bin`，880,240 B），两个都装上了。用户按 RST、再按 Enter 进入 Launcher 菜单后完成安装，我们的固件启动后 daemon 显示 `link: ready fw=cpa-fw/0.2.0 store=sd launcher=yes`。Launcher 能否直接读这张 512GB 卡：没有测（走的是串口，不需要读卡）。`launcher_boot` HIL 没有跑；系统页需要目视检查，留给用户。**注意**：Launcher 的串口端口一出现，daemon 就会抢着去连。要先执行 `cpa pause`，否则会打断串口安装（daemon 会记一条 `no_reply` 然后放开端口）。另外，串口安装**不会建 `cpafs` 数据分区**（`flash firmware` 只写 app），所以在 Launcher 下、插着 SD 卡时，存储走 SD；没插卡时就没有存储（`store=none`），这一点待验证。
- [ ] **needs user** · **G-L2 四种复位**
  - 怎么做：Launcher 下分别：按 RST 键；拨电源开关（OFF 再 ON，USB 拔掉才算真断电，03 §2）；`debug.reboot`（HIL 的 `reboot` 场景）；`pio` 的 USB 复位（只复位、不上传）。
  - 判定：前两种进 Launcher 启动画面；后两种直接回到我们的固件。
  - 日期：＿＿ 结果：RST ＿＿ 开关 ＿＿ debug.reboot ＿＿ USB 复位 ＿＿
- [ ] **needs user** · **G-L3 和 Bruce 1.16.1 来回切换 10 次**
  - 判定：我们的 outbox、epoch、开机计数都在；Bruce 的 `spiffs` 没被动过。
  - 日期：＿＿ 结果：＿＿/10
- [ ] **needs user** · **G-L4 Launcher 下 `cpafs` 的容量**
  - 怎么做：不插卡开机（或装 `--env cardputer-adv-test-nosd` 打出的镜像），`cpa pause 1800` 断开 daemon，一直存笔记到被拒（"已满"）。
  - 判定：记下条数（09 §5 估算约 80 条），回填 launcher.md 6.4 第 2 项。最后 `cpa pause 1` 让它们同步完。
  - 日期：＿＿ 条数：＿＿
- [x] **G-L5 系统页"回到 Launcher"**：**v0.2 不适用**（09 §1.2：v0.2 不做回到 Launcher 的菜单项），不测。
- [ ] **needs user** · **G-L6 Launcher 下升级（09 §4.4）**
  - 怎么做：第 0 步：状态栏 ↑N 为 0（或者插卡开机一次，让迁移清空 `cpafs`，在系统页确认）。然后用覆盖安装（Launcher 提示空间不够时的"Use <标签> partition"，或串口 `flash overwrite`）装新的测试镜像；不要删掉旧版本又留着别的版本。
  - 判定：已确认的数据一条不丢；daemon 收到的重复条目都判为 `duplicate`（`events.jsonl` 里没有第二条 `item.received`）。
  - 日期：＿＿ 结果：＿＿
- [ ] **needs user** · **G-L7 SD 20 / 25 MHz 挂载并比对校验（裸机、插扩展帽、GPIO5 拉高）**
  - 已有：裸机 20 / 25 MHz 的结果在 sd-card.md §4（25 MHz 实际是 20 MHz）。本次 device `test_sd_store` 也报 `hz=20000000`（§5.3）。
  - 还缺：插扩展帽、GPIO5 拉高两种情况，要人装硬件。
  - 日期：＿＿ 实际时钟：＿＿ 吞吐：＿＿
- [ ] **needs user** · **G-L8 Launcher 的 U 盘模式在 macOS 26 上挂载这张卡**
  - 注意：macOS 的 U 盘修复（PR #448）只在 beta 里，2.9.1 没有。用 beta 测，或者记为"2.9.1 不支持"。
  - 判定：能挂上，用时 ≤30 s。
  - 日期：＿＿ 版本：＿＿ 结果：＿＿
- [ ] **needs user** · **G-L9 daemon 在跑时从 Launcher 开机 10 次（新增，09 §7 第 3 条）**
  - 怎么做：Mac 连着、daemon（LaunchAgent）在跑，设备在 Launcher 下装好测试镜像。每次：拨开关 OFF、拔线、再插线、拨 ON（真冷启动，会先进 Launcher 启动画面约 5 s），用秒表或 `cpa status` 轮询记下从上电到 `link: ready` 的时间；同时看 `cpa status` 的 `last_release` / `backoff_s` 和 `logs/cpad.log` 有没有 `no_reply` 挂起 30 s。
  - 判定：10 次都在 12 s 内 `link ready`。不通过就报告给 Lead，由 Lead 派发 M2-C 的条件项（已绑定设备 `no_reply` 只挂起 3 s）。
  - 日期：＿＿ 10 次用时：＿＿ 出现 30 s 挂起：＿＿

### 5.7 SD 人工项（09 §9.4）

- [ ] **needs user** · **SD-1 10 次真实断电（SD 为主存储）**
  - 怎么做：插卡、`cardputer-adv-test` 或正式版固件，daemon 先 `cpa pause 1800`。每次：G0 输入一条带编号的笔记，看到"已保存"后**立刻**把开关拨到 OFF 并拔线，等几秒再插上、拨 ON。10 次后 `cpa pause 1`。
  - 判定：10 条全部出现在 notes_dir、没有重复；系统页没有"存储错误"。**按低强度证据记录（R16）**：FAT 的 0 丢失在协议层面由 native `core/test_outbox_fat` 证明，10 次人工断电只是抽样。
  - 日期：＿＿ 结果：＿＿/10，重复：＿＿
- [ ] **needs user** · **SD-2 运行中拔卡**
  - 判定：拔卡后的那次保存最多卡一次（≤2 s），之后按键正常；保存给出"保存失败（E_STORAGE）"，系统页显示 `存储错误 N，请重启`；重新插卡并重启后恢复正常，卡上没发出去的条目重启后补发。
  - 日期：＿＿ 最长卡顿：＿＿ 结果：＿＿
- [ ] **needs user** · **SD-3 不插卡开机：device `test_sd_absent`**
  - 怎么做：拔卡，`cpa pause 600`，`cd firmware/app && pio test -e cardputer-adv-test -f 'device/test_sd_absent'`。
  - 判定：`hal_sd_mount` 返回 Absent，耗时 ≤500 ms。超了先记下 MISO 空闲电平，再试 GPIO39 内部上拉。另外看一眼系统页：`存储 内部 1.5M` + `SD：未检测到卡`。
  - 日期：＿＿ 耗时：＿＿ 结果：＿＿
- [ ] **needs user** · **SD-4 插一张 exFAT 卡**
  - 判定：系统页显示 `SD 卡无法使用：请在 Mac 上格式化为 FAT32`，存储退到内部。
  - 日期：＿＿ 结果：＿＿
- [ ] **needs user** · **SD-5 用 `diskutil` 重新格式化开发卡，再跑 device `test_sd_store`**
  - 怎么做（用户已授权清空这张卡）：卡放进 Mac 读卡器，`diskutil list` **核对两遍盘号**，然后 `diskutil eraseDisk FAT32 CPA MBRFormat /dev/diskN`；插回设备，`cpa pause 600`，`cd firmware/app && pio test -e cardputer-adv-test -f 'device/test_sd_store'`。
  - 判定：通过；记下 p95、新建目录的第一条耗时、写停顿（2 个 FAT、对齐未知，R17），和 §5.3 里 FatFs 格式化的这张卡对比。
  - 日期：＿＿ 盘号（核对两遍）：＿＿ p95：＿＿ 第一条：＿＿ 最长：＿＿
- [ ] **needs user** · **SD-6 再测 2–3 张卡（至少一张 ≤32GB、出厂 FAT32）**
  - 每张跑 device `test_sd_store` 和 HIL `sd_empty,sd_store`。
  - 日期：＿＿ 卡 / 结果：＿＿
- [ ] **needs user** · **SD-7 Mac 磁盘工具"急救"查丢失簇**
  - 在 SD-1 和 SD-2 之后做。判定：没有交叉链接；丢失簇的数量记下来（设备上没有 fsck，丢失簇回收不了，sd-card.md §6.3）。
  - 日期：＿＿ 结果：＿＿

### 5.8 Launcher 下的手动安装步骤，和打测试镜像的命令

```bash
# 打测试版合并镜像（cpa-v0.2.0-hil.bin 在 Launcher 里显示为 cpa-v0.2.0-hil，不超过 20 个字符）
cd firmware/app && pio run -e cardputer-adv-test
python3 scripts/make_image.py --env cardputer-adv-test --tag v0.2.0-hil --out "$TMPDIR/dist"
# G-L4 用的无卡版：把 env 换成 cardputer-adv-test-nosd
```

2026-10-20 已按上面的命令打过一次：`cpa-v0.2.0-hil.bin` 951,200 B、`cpa-v0.2.0-hil-app.bin` 885,664 B（分区表检查通过）。

安装（09 §4.4）：
0. **第 0 步**：清空内部待发：状态栏 ↑N 为 0；或者插卡开机一次让迁移清空 `cpafs`，在系统页确认。两个方向（整片 → Launcher、Launcher → 整片）都要做。
1. 用网页刷写器或 esptool 装 Launcher 2.9.1。**不要在开机时按住 G0**，除非就是要进 Launcher 菜单（03 §2 的锁存问题只在按住 G0 上电时出现）。
2. 把 `cpa-v0.2.0-hil.bin` 放到 SD 卡 `/cpa/fw/`（Mac 读卡器，或 Launcher 的 U 盘模式）。
3. Launcher 里选 SD → `/cpa/fw/cpa-v0.2.0-hil.bin` → 安装。读不了卡就用串口 `flash overwrite`。
4. 升级时用覆盖安装（"Use <标签> partition" 或 `flash overwrite`），或删除后立刻重装；不要删了旧版本又留着别的版本不管。
5. 写 `cpa-<tag>.bin` 到 0x0 会清空 NVS（epoch 重新生成，安全）；`pio run -t upload` 不碰 NVS。

### 5.9 发现的问题

- [x] （固件负责人，M2-A）· **1. device `test_storage` 的两个 LittleFS 保存计时不过**
  - 证据：§5.3。第 1 次运行立即 ack 的 p95 101.4 ms、积压的 p95 120.2 ms（最长 195.0 ms）；第 2 次运行积压的仍不过。上限都是 100 ms。
  - 09 §5 已把"LittleFS 积压 200 条时 108–158 ms"列为已知问题（v0.2 插卡时 outbox 在 SD 上，p95 10–20 ms），但测试没有跟着改，所以 `pio test -e cardputer-adv-test -f 'device/*'` 不会全绿。需要固件负责人决定：放宽 LittleFS 的上限、把它标成已知（只打印不断言），或者修 LittleFS 的慢。
  - 日期：2026-10-20 结论：**放宽并保留回归保护**。100 ms 目标（G3）只在 SD 上断言（`test_sd_store`）；`test_storage` 打印 `CPA_SAVE_TARGET <label> p95_us=… target_us=100000 met|missed (known on LittleFS)`，断言 p95 ≤ 200 ms（`kLfsSaveP95MaxUs`；丢了分片的 513 ms 仍会被抓到）。真机复跑 9/9 两次（§5.3、§5.10）。LittleFS 本身没有变快，仍是已知问题。
- [x] （固件负责人，M2-B，`firmware/app/scripts/pre_upload.py`）· **2. 上传前的 `cpa pause 120` 会缩短更长的暂停**
  - `cpa pause N` 是"从现在起暂停 N 秒"，不是"至少暂停 N 秒"（`daemon/src/cpad/app.py:348`：`_paused_until = clock() + seconds`；`cpa pause 1` 就是靠这一点结束暂停的）。所以先 `cpa pause 5400` 再 `pio run -t upload` / `pio test`，暂停会变成 120 s；之后用户自己的 cpad 可能在 HIL 或设备测试进行中接上设备，测试版固件上的测试笔记就会进用户真实的 notes_dir。
  - 本次的绕法：在不含 `cpa` 的 `PATH` 里跑 pio（§5.0）。建议：`pre_upload.py` 先看 `cpa status`，已经暂停得更久就不动；或者 daemon 给 `pause` 加"只延长不缩短"的选项。
  - 日期：2026-10-20 结论：**已修**（`pre_upload.py`）。上传前先读 `cpa status --json` 的 `paused_s`：剩余 ≥120 s 就跳过（`skip: already paused for N s`），不足才 `cpa pause 120`；读不到状态时退回原来的 `cpa pause 120`，上传永远不因它失败。真机验证（§5.10，`cpa` 在 `PATH` 里）：先 `cpa pause 3600`，两次 `pio test` 和一次 `pio run -t upload` 之后暂停剩 3431.5 s（没被缩短），上传日志是 `pre_upload: skip: already paused for 3446 s`。
- [ ] （HIL 负责人，M2-D，`tools/hil/scenarios/link.py`）· **3. 紧跟在 `sd_store` 后面跑时，`handshake` 拿到的是重启前的 hello**
  - 证据：§5.10 的 `--scenarios sd_store,handshake,capture` 一次运行（17:50:53）中，`handshake` 4/6：`same_boot_after_reopen` before `039T61N-00013` / after `039T61N-00014`，`up_ms_continuous` 9506 → 4607 ms（漂移 −12032 ms）。但 `no_reboot_detected` 通过（场景前后重启计数都是 1），`-00014` 正是 `sd_store` 自己 `debug.reboot` 之后的 boot，所以设备在 `handshake` 里没有重启。紧接着单独跑 `handshake,capture`（17:51:34）是 2/2 PASS、6/6。
  - 原因：`handshake` 用 `handshakes(tap.since())[-1]` 当"重开前"的握手，它取的是最后一次打开端口后**第一条**设备 hello。软件重启时 USB 串口没断开、tap 没有新的 `open` 记录，重启后的 hello 是同一个窗口里的第二条，于是"重开前"取到了 `-00013` 的旧 hello。§5.4 的验收顺序是 `sd_store` → `sd_bulk` → `handshake`，`sd_bulk` 中途重新握手过，所以把这个问题盖住了。
  - 建议：先在 `tools/hil/tests` 加一个"一个 open 窗口里有两条设备 hello"的失败用例，再让 `handshake` 用重开前最后一条设备 hello（或场景开始时的 `rig.link.hello`）。设备和 daemon 不用改。
  - 日期：2026-10-20 结论：＿＿
- 记录，不算问题：
  - SD 挂载增加的堆 15068 B，离 16 KB 的上限只剩约 1.3 KB（§5.3）。以后 `max_files` 或缓冲变大就会超。
  - 迁移每条 56 ms，比 09 §5 估算的约 40 ms 慢；200 条约 11 s，只发生一次。
  - harness 自己的问题已在本次修好：`handshake` 的"没有重启"检查原来要求重启计数为 0，在同一次运行里跑过 `sd_store` 之后会误报（§5.4）。

### 5.10 修复后的最终复跑，刷回正式版（2026-10-20 17:48–18:03）

固件树：`a6114eb` 加上 §5.9 第 1、2 条的未提交修复（只改了 `scripts/pre_upload.py`、`test/device/test_storage`、`platformio.ini` 的注释，`src/` 和库没变，测试版 app 仍是 885,664 B）。这次 `cpa` 在 `PATH` 里，用户自己的 cpad 先 `cpa pause 3600`，`lsof` 确认串口空闲；没按任何键，没碰 G0。

```bash
cd firmware/app
pio test -e cardputer-adv-test -f 'device/test_storage'        # 跑了两次，第 2 次加 -v
pio run -e cardputer-adv-test -t upload && cd ../..
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_store,handshake,capture --report "$TMPDIR/hil-rerun.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,capture --report "$TMPDIR/hil-hc.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios soak --minutes 10 --report "$TMPDIR/soak.json"
(cd firmware/app && pio run -e cardputer-adv -t upload)        # 正式版
cpa pause 1 && cpa status
```

- [x] **`pre_upload.py` 不再缩短暂停**（§5.9 第 2 条）：上传日志 `pre_upload: skip: already paused for 3446 s`（正式版上传时是 2974 s）；`cpa pause 3600` 之后经过两次 `pio test` 和一次上传，暂停还剩 3431.5 s。
- [x] **device `test_storage`**（17:48–17:50，**PASS 9/9，两次**，60.6 s / 44.3 s）
  - 第 2 次（`-v`）：立即 ack 的 p50 31.4 ms、**p95 74.9 ms**、最长 84.4 ms → `CPA_SAVE_TARGET acked … met`；积压的 p50 35.0 ms、**p95 147.0 ms**、最长 185.0 ms → `missed (known on LittleFS)`，都在 200 ms 以内。`CPA_PART launcher=0 cpafs=1572864 legacy_spiffs=0`，`boot=34 (was 33)`。
- [x] **HIL `sd_store,handshake,capture`**（17:50:53，2/3，退出码 1：harness 的问题，不是设备的）
  - `sd_store` PASS 8/8：3 条存在 SD 上，`debug.reboot`（`039T61N-00013` → `-00014`）后 hello 报 `store=sd`、`pending=3`，每条恰好一次。
  - `capture` PASS 5/5：Enter 到笔记 0.104 s。
  - `handshake` FAIL 4/6：`same_boot_after_reopen`、`up_ms_continuous` 不过，因为"重开前"的 hello 取成了重启前的那条。场景前后重启计数都是 1，设备没有重启。见 §5.9 第 3 条。
- [x] **HIL `handshake,capture` 单独跑**（17:51:34，**2/2 PASS，退出码 0**）：`handshake` 6/6，首次和重开都是 0.003 s、boot 不变、漂移 0 ms，`store=sd`；`capture` 0.103 s。
- [x] **浸泡 10 分钟，SD 为主**（17:51:45–18:01:56，**PASS 6/6，退出码 0**）
  - 10 条笔记，0 丢失、0 重复。
  - 600 次翻转，1126 份快照，2028 次主机请求全部有回复，0 次 TX 卡住：p50 2.8 ms、p95 18.6 ms、最长 37.7 ms，0 次超过 1 s，0 次 nudge。
  - 端口没重新打开过，`device_not_ready` = 0，结束时 outbox 为空。
- [x] **刷回正式版 `cardputer-adv`**（18:02:10–18:02:21，`pio run -e cardputer-adv -t upload`，SUCCESS）
  - RAM 63,444 B（19.4%），app 880,240 B 写入 0x10000，"Hard resetting via RTS pin" 后正常启动，没进下载模式。
  - `firmware.bin` 里没有 `debug.key`。
- [x] **daemon 恢复**（18:02:27）：`cpa pause 1` 之后 `cpa status` 显示 `link: ready  dev=020000000001 fw=cpa-fw/0.2.0 pending=0`，boot `039T61N-00015`，`backoff_s` 0。
  - 但同一行是 `store=- launcher=-`。原因：用户的 LaunchAgent（`com.cardputer-assistant.cpad`，pid 86881）10:31:17 就启动了，比加上这两个字段的 `4ce5e50`（14:51）早，进程里跑的还是旧代码。新的 `cpa` 客户端会显示这两个字段，但旧进程不报。
  - 正式版上的 `store=sd` 用本仓库的 daemon 另外核对过：18:03:09 先 `cpa pause 600`，再跑 `run_e2e.py --scenarios handshake`，6/6 PASS，`hello.store=sd`、`launcher=false`、`fw=cpa-fw/0.2.0`，`rst=unknown`（esptool 用 RTS 复位后的值）。18:03:17 再 `cpa pause 1`，`link: ready` 同上。
- [x] **needs user**（或 Lead）· **重启 LaunchAgent，让 `cpa status` 显示 `store=sd`**
  - 怎么做：`launchctl kickstart -k gui/$(id -u)/com.cardputer-assistant.cpad`，然后看 `cpa status` 的 `link: ready … store=sd launcher=false`。
  - 为什么没做：这是用户自己的服务。它是 `daemon/` 的 editable 安装，重启后就会对真实的 notes_dir 跑当前工作区的 daemon 代码。编排只让 agent 用 `cpa pause`，所以留给用户或 Lead。
  - 日期：2026-10-21 结果：v0.3 最终复跑时由 agent 按 Lead 的指示执行（§6.8 最后一项）：`cpa status` 显示 `link: ready  dev=020000000001 fw=cpa-fw/0.3.0 pending=0 store=sd launcher=no`（新的 `cpa` 把 `launcher=false` 显示成 `no`）。

## 6. v0.3（M3）：录音

> M3-H 阶段 1（[12 号文档](12-m3-work-packages.zh-CN.md)）的交付物。自动部分由独占设备的 agent 在 2026-10-21 跑完；需要人耳、人手（断电、拔卡、带出门用一天）或真实 WhisperWoof 的项标 **needs user**，结果栏留空，**没有伪造任何结果**。设计依据见 [11 号文档](11-m3-design.zh-CN.md) §11、§14，闸门实测见 [gates/m3-device-gates.md](gates/m3-device-gates.zh-CN.md)，SD 驱动修复见 [gates/sd-card.md](gates/sd-card.zh-CN.md) §11。
> **§6.7 第 1–5 条修好以后的最终复跑在 §6.8**（2026-10-21 04:21–04:48）：device 套件 20/20、录音 HIL 6/6（`rec_soak` 缩到 5 分钟）、`rec_reboot` 再 4/4、G-R 14/14；设备已刷回正式版 v0.3，用户的 cpad 已重启到新代码并 `link: ready`、`store=sd`。§6.0–§6.5 是修复前那一轮的记录，原样保留。
> **v0.3.1 的增益标定、核对和录音回归在 §6.9**（2026-10-21 12:18–12:41）：`kMicGain` 定为 `{9, 0xC4, 2}`，`mic_cal` 8/8、`mic_verify` 5/5，录音回归全过；设备已刷回正式版 v0.3.1，用户的 cpad `link: ready`、`fw=cpa-fw/0.3.1`。
> **v0.3.2（#31，开机后的第一条录音）的设备测试 `test_mic_cold`、HIL `rec_cold_start` 和回归在 §6.10**（2026-10-21 17:20–17:32）：`test_mic_cold` 3/3，`rec_cold_start` 4 次里 3 次 15/15（第 1 次是音量太低，只差"开头听得到音"3 项），录音回归 8/8；`mic_verify` 因为 Mac 音量 25 没跑成；设备已刷回正式版 v0.3.2，用户的 cpad `link: ready`、`fw=cpa-fw/0.3.2`。

### 6.0 环境

| 项 | 值 |
|---|---|
| 设备 | 同 §0（MAC `02:00:00:00:00:01`，`/dev/cu.usbmodem1101`），USB 供电 |
| SD 卡 | 同 §5.0 的开发卡（512GB FAT32）。固件链接的是打了 CPA patch 5 的 `third_party/SD`（sd-card.md §11），9.6 MB 的 5 分钟段可以写 |
| 固件 | `release/v0.3.0` 的 HEAD `689f66a`（PR #20，固件没有本地改动）。`cardputer-adv-test` 和 `cardputer-adv-test-nosd`，都用 `pio run -e <env> -t upload` 刷入，没按任何键，没碰 G0（录音手势全部走 `debug.key`）。⚠️ 这个构建的 hello 和 `.M` 里的版本串仍是 `cpa-fw/0.2.0`（§6.7 第 4 条） |
| Mac 端 | `run_e2e.py` 进程里的 `cpad`（本仓库 `daemon/`），临时 `$CPA_HOME`、notes_dir、项目目录，fake-claude；WhisperWoof 一侧是 `tools/hil/harness/fakewoof.py`（契约 v1 的确定性实现，不转写，正文由场景给定）。用户自己的 LaunchAgent `cpad 0.1.0` 全程 `cpa pause 5400`（中途续过），每次动设备前 `lsof /dev/cu.usbmodem*` 为空；没碰用户的 `~/CardputerNotes`、`~/.claude` 和 LaunchAgent 配置 |
| 隐私 | 录音里只有房间底噪。所有 WAV、转写、纪要都在 run 结束时随临时目录删除；唯一一次 `--keep`（第一次试跑）看完立即删掉。报告和日志里只有数字、id 和状态 |

### 6.1 怎么跑

```bash
cpa pause 5400; lsof /dev/cu.usbmodem*          # 串口要空
cd firmware/app
pio test -v -e cardputer-adv-test -f 'device/test_rec_sd' -f 'device/test_rec_rotation' -f 'device/test_rec_budget' \
  -f 'device/test_runtime_budget' -f 'device/test_sd_store'          # G-A6 等设备测试
pio run -e cardputer-adv-test -t upload && cd ../..
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_memo,rec_meeting,rec_resume,rec_reboot,rec_live_hold,rec_rotation,rec_soak --report "$TMPDIR/hil-rec.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,sd_store,sd_bulk,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk,back_keys --report "$TMPDIR/hil-v02-regress.json"
(cd firmware/app && pio run -e cardputer-adv-test-nosd -t upload)
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_nosd,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk --report "$TMPDIR/hil-nosd.json"
(cd firmware/app && pio run -e cardputer-adv-test -t upload)          # 留在测试固件上，等复跑
uv run --project daemon pytest tools/hil/tests -q                     # harness 自己的单元测试（含 fakewoof 和录音检查）
```

- 新场景都要点名，`all` 仍然只是 v0.1 的 10 个。`rec_nosd` 要 nosd 固件，不能和其他 `rec_*` 放在同一次运行里（`select.validate` 直接拒绝）。
- 超时：`rec_*` 600 s，`rec_rotation` 1200 s，`rec_soak` 1800 s（`select.TIMEOUT_S`）。`rec_soak` 固定 10 分钟（G-A2），不看 `--minutes`。
- 报告里每个 `rec_*` 场景带 `transfer`（tap 上第一个 `file.get` 到最后一个 `file.data` 的 app 层有效吞吐，每段和合计）和设备时钟对账的数字（`start_latency_ms`、`diff_samples`）。

### 6.2 实现方式

| 场景 | 做法 |
|---|---|
| 开始 / 结束录音 | 全部用 `debug.key` 按真实按键：便条 = `g0` 按住 `hold_ms`（500 ms 时 G0Long 开始，松开停止）；会议 = Opt+R → 等 `debug.state.overlay=confirm` → 600 ms 单独的 Shift（过 400 ms 保护）→ Enter；结束同样 Opt+R → Shift → Enter。`debug.rec` 只用来设 `seg_ms`。项目：便条前先 Opt+1 打开项目 1（上下文槽位），会议开始后按 `1` 打标签 |
| 设备时钟对账（G-A4 和样本数检查） | `debug.state.key_ms` 是最后一次有键按下时的设备 `millis()`；第 0 段 `.M` 的 `at.up_ms` 是 `Mic.begin` 之后那一刻的 `millis()`（`audio_task.cpp` `begin_session`）。开始时延 = `at.up_ms −` 开始键的 `key_ms`；墙钟 = 结束键的 `key_ms − at.up_ms`；`diff_samples` = 各段样本数之和 − 墙钟 × 16。整条链都在设备的一个时钟上，不受 USB 时延影响。便条的结束时刻是 G0 按下 + `hold_ms` |
| 标记 | 每次按空格后立刻读 `key_ms`；期望的会话时间 = `key_ms − at.up_ms`，和 manifest `marks[].t_ms` 比，容差 ±400 ms（★ 按已经写下的样本数打点，最多落后一两块） |
| 落地检查 | 每段：大小 = manifest `bytes`、44 B 头和标准头逐字节相同、整文件 SHA-256 = manifest `sha256`、`bytes = 44 + 2 × samples`；`file[44:]` 的 SHA-256 = `rec.seg.stored` 里设备 `.M` 的值（`rcv` 段没有）；会话目录 0700、文件 0600；manifest 过契约 v1 参考校验器 |
| fakewoof | 场景期间每 0.5 s 扫一次 `<notes_dir>/recordings`：合法 manifest + 音频校验通过 → `status.json`（queued / transcribing / summarizing）→ `transcript.json` / `.md` → `summary.md` → 最后 `result.json`（done / summary），全部 `.tmp` + fsync + rename、0600；非法 manifest → `failed` + `E_INVALID` / `E_VERSION`。便条正文以"待办"开头就是 todo。会议给 1 条待办 |
| `rec_resume` | 3 段会议（30 s 段，录 65 s）。tap 上看到第二段的 `file.data` 到了 256 KiB 以后 `cpa pause 4`；端口放开 1 s 后记下 `.part` 大小；重连后第二段的第一个 `file.get` 偏移必须正好是 `.part − 64 KiB` |
| `rec_live_hold` | 同样的 3 段会议；第一段拉到 100 KiB 时按住 G0 4.5 s 开始便条。E_BUSY 窗口 = tap 上第一个回给 `file.*` 的 `E_BUSY` 到之后第一个 `files.live:false` 的 pong / hello。判定：窗口里的 `E_BUSY` 都是 `live`；窗口里发出的 `file.*` 都得到 `E_BUSY`；最多 1 个迟到的 `file.data`（音频任务在 Start 之前已经取走的那一次读，11 §4.7）；daemon 在 E_BUSY 到达 20 ms 以后不再发 `file.*`（20 ms 以内的是和 E_BUSY 在线上交错的请求，daemon 那时还没读到 E_BUSY）；被打断的文件不从 0 重来；便条排在会议剩下的段前面 |
| `rec_reboot` | (a) 10 s 段，`debug.state` 显示第 1 段后再录 5 s，`debug.reboot`：第 1 段要作为 `rcv` 段落地，会话完整；丢失 = 重启前读到的 `up_ms` −（`at.up_ms` + 样本数 / 16），上限 1.5 s。(b) `debug.state.rec.seg` 一变成 1 就（20 ms 轮询）`debug.reboot`：新段的 44 B 头在 1 s 后的第一次 fsync 才落盘，所以通常是"尾部缺段"：会话以 `gaps:[1]` 收尾。每次重启后检查新 boot 的 hello 是 `store=sd` 且带 `files`（不是就再重启一次，好继续检查会话，但这一项记失败） |
| `rec_rotation`（G-A4） | `seg_ms` 10 s（`debug.rec` 允许的最小值），录 305 s：30 次换段、31 段。每个整段都必须正好 160,000 样本，`t0_ms` 连续，`diff_samples` 在 1 块（2048 样本 = 128 ms）以内 |
| `rec_soak`（G-A2） | 10 分钟会议，默认 5 分钟段（两个 9.6 MB 的段，正是 sd-card §11 修好的写法）。录音期间每秒一个 ping，fake-claude 每秒翻转一次会话、daemon `poll_s=1` 推 `sessions.snapshot`。只统计录音期间发出的请求。"丢帧"用设备时钟对账判断（`debug.state` 没有 mic 丢块计数，§6.7 第 6 条） |
| `rec_nosd` | nosd 固件：hello 没有 `files`；`debug.state` 没有 `rec`；G0 按住 1.5 s 后没有任何浮层、outbox 不变；Opt+R 后没有确认框；整个场景 daemon 没发过 `file.*` |
| 30 天保留（只做干检查） | `rec_memo` 结束后用 daemon 内存里的索引调 `cpad.recordings.retention.due`：现在不到期；30 天 + 1 分钟后到期；`transcript.json`、`summary.md` 都在；本次运行没有任何 `rec.audio.swept`。不真的删文件 |

### 6.3 设备测试（G-A6 等，`cardputer-adv-test`，插卡）

2026-10-21 02:10–02:15，`pio test -v -e cardputer-adv-test -f 'device/test_rec_sd' -f 'device/test_rec_rotation' -f 'device/test_rec_budget' -f 'device/test_runtime_budget' -f 'device/test_sd_store'`（`pre_upload: skip: already paused`，没缩短暂停）。结果：3 个套件通过，2 个不过（§6.7 第 1、2 条）。

- [x] **`test_rec_budget`（G-A6 的自动部分）**（PASS 3/3，20.1 s）
  - `CPA_REC_BUDGET idle_free=275532 idle_block=262132 rec_free=269080 rec_block=258036 xfer_free=274988 audio_stack_left=2404 ring_high=1`
  - 空闲 269.1 KB / 256.0 KB（≥150 / ≥64 KB ✅）；录音 + 提示音 262.8 KB / 252.0 KB（≥96 / ≥32 KB ✅）；传输 268.5 KB（≥128 KB ✅）；音频任务栈余量 2,404 B（≥1.5 KB ✅）；环形缓冲最多积压 1 块（共 6 块）。
  - ⚠️ 这是只有音频任务和 SD 的精简测试程序，不是整个 app（屏幕、三个模块、链路都没起）。整个 app 的空闲堆见下一条；整个 app 在录音 + 提示音时的堆没有测到（§6.7 第 6 条）。
- [x] **`test_runtime_budget`（整个 app，录音缓冲开机分配）**（PASS 2/2，17.2 s）
  - `CPA_BUDGET stack_left=5788 heap_free=185856 heap_min=176312 max_block=163828 sent=22 pending=0 bad=1 store_errors=0 missing_glyphs=0 rec=1 audio_stack_left=3720`；`CPA_RX_DONE got=8192 bad=0`。
  - 空闲堆 181.5 KB、最大块 160.0 KB（≥150 / ≥64 KB ✅），loop 栈余量 5,788 B（≥4 KB ✅），音频任务栈余量 3,720 B。和 G-A0 的 v0.2 基线（234,836 / 208,884，m3-device-gates §2）比，少了 48,980 B，其中约 38 KB 是开机分配的录音缓冲（11 §4.7），约 4 KB 是第 3 个文件名额。
- [x] **`test_rec_rotation`（G-A4 的短版本）**（PASS 1/1，108.3 s）
  - `CPA_ROT segments=4 samples=1521664 wall_ms=95000 expect=1520000 ring_high=1 stack_left=2372`：3 个 30 s 段都是 480,000 样本（逐样本），总样本比墙钟多 1,664（104 ms，在 1 块以内）。
- [ ] **`test_sd_store`**（**FAIL 6/7**，28.3 s；§6.7 第 2 条）
  - 不过的是 v0.2 的 `test_write_read_remount_within_200ms_and_16kb`：`CPA_SD_MOUNT us=18143 heap_delta=19204`，挂载增加的堆 19,204 B > 16,384 B。v0.2 是 15,068 B（§5.3）；多出来的约 4.1 KB 正是 11 §4.5 把 `max_files` 从 2 提到 3 的代价，§5.9 的"记录"里已经预言过。
  - 其余 6 项通过：挂载 18.1 ms；200 条立即 ack 的 p50 9.3 ms / p95 10.9 ms / 最长 49.1 ms，第一条 76.4 ms；200 条积压 p50 15.5 ms / p95 20.3 ms / 最长 79.2 ms；LFS → SD 迁移 50 条每条 48 ms。
- [ ] **`test_rec_sd`**（**崩溃**，§6.7 第 1 条）
  - `test_mount_and_clean`、`test_memo_commits_with_the_hardware_sha` 通过，第 3 个 `test_cut_off_session_is_recovered_at_boot` 在 `loopTask` 上栈溢出（`Stack canary watchpoint triggered (loopTask)`），设备反复重启、每次都重跑前两项；手动停掉 `pio test`（Ctrl-C），立即刷回 app 测试固件。后两项（`test_get_and_del`、`test_clean_up`）没跑到，卡上 `/cpatest` 留下了这个测试写的文件（只在测试根目录下，app 不碰）；03:36 再跑 `test_sd_store` 时它的第一项已经把 `/cpatest` 清掉（§6.5 收尾）。
- [x] **`test_mic_gain`**（v0.3.1 新增，不用卡；2026-10-21 **4 次里 3 次 PASS**，第 1 次在刚从 v0.3.0 刷过来时，见下；从 v0.3.1 正式版刷过来后的第 4 次 PASS）
  - 怎么跑：`cpa pause 600`；`cd firmware/app && ~/.local/bin/pio test -v -e cardputer-adv-test -f 'device/test_mic_gain'`；跑完 `pio run -e cardputer-adv-test -t upload` 刷回 app 测试固件（`mic_cal` 要用），或者按 m3-device-gates §5.2 第 8 步刷回正式版。
  - 测的是正式的 `MicRing` 路径：`start({10, 0xC5, 2})` 后 REG14 / REG17 读回 0x1A / 0xC5，样本的最大公约数是 1（`mag` 2 不放大）；`stop` 后用 M5Unified 自己的值 `{0, 0xBF, 16}` 再开，读回 0x10 / 0xBF（上一档没有残留），每个样本都是 8 的倍数（`over_sampling` 1）；写寄存器一次都没走后备。每次开始输出一行 `CPA_MIC …` 的数字，共两行。
  - 判定：全部 PASS。这是 `mic_cal` 之前的前提：寄存器写不进去时 `mic_cal` 只会看到增益对不上。
  - 日期：2026-10-21 12:18–12:20 结果：**第 1 次 FAIL 2/3，第 2、3 次 PASS 3/3**（每次 11–16 s）。`CPA_MIC` 两行（第 2、3 次一样）：`pga=10 vol=0xC5 mag=2 reg14=0x1A reg17=0xC5 gcd=1 gain_failures=0`、`pga=0 vol=0xBF mag=16 reg14=0x10 reg17=0xBF gcd=8 gain_failures=0`。
  - 第 1 次是刚从 v0.3.0 正式版（`cpa-fw/0.3.0`，开始和结束都放提示音）刷过来后的第一次：`{10, 0xC5, 2}` 那一档前 512 ms 的 8,192 个样本**全是 0**（`gcd=0`），寄存器读回却是 0x1A / 0xC5，`gain_failures=0`；同一次的后半段正常（0x10 / 0xBF，`gcd=8`）。第 2、3 次都在一个测试镜像之后跑，全过。
  - 之后没有再出现：`mic_cal` 的 16 段、`mic_verify` 的 2 段、§6.9 录音回归的 7 段，开头都不是全 0；其中包括刷机后的第一段和每次重启后的第一段。原因不明，猜测是 codec 从上一个固件（用过扬声器）留下的状态启动；v0.3.1 正式版不再打开扬声器。后果是录音开头约半秒没声音。2026-10-21 从 v0.3.1 正式版（`cpa-fw/0.3.1`，已经跑起来、从没开过扬声器）刷过来后又跑了一次：**PASS 3/3**（16 s），开头不是全 0。和"上一个固件用过扬声器"的猜测一致；v0.3.1 之后不会再有这种上一个固件。

### 6.4 自动场景结果：录音（`cardputer-adv-test`，插卡）

固件 02:15 刷入（`hello.store=sd`、`launcher=false`）。02:27–02:39 分场景试跑并修了 harness 自己的 3 个问题（见本节末尾），**02:41:36–03:03:52 跑验收命令**（上面 §6.1 第 3 条）：**6/7 PASS，退出码 1**，唯一不过的是 `rec_live_hold` 的一项 harness 判定（下面第 5 条，判定已改）。之后又把 `rec_live_hold,rec_reboot` 连跑两次（见第 5、6 条）。

- [x] **rec_memo**（PASS 18/18，6.1 s）
  - 按住 G0 4 s（先 Opt+1）：一段 `seg-000.wav`，3,328 ms、53,248 样本，`lead_ms` 150，项目 `cpa`。从 G0 按下到第一个样本 **607 ms**（含 500 ms 长按门限；≤1.0 s ✅。`at.up_ms` 取在 `Mic.begin` 之后、第一块填满之前，比 G-A5 hwtest 的"按键 → 第一块" 647 ms 少那约 48 ms，两者一致）。样本数比"按下 + 4 s"的设备墙钟少 1,040（65 ms）。
  - 落地：大小、44 B 头、整文件 SHA-256、设备 `.M` 的数据 SHA 全对；目录 0700、文件 0600；manifest 过契约 v1。拉取 334.7 KB/s（106,540 B 用 0.31 s；10 s 便条的底线是 ≤6 s）。
  - fakewoof 写出 done / summary、`memo.intent=todo`；每日笔记恰好一行 `- [ ] HH:MM #cpa 待办 HIL 便条 … [音频](…/seg-000.wav) ^cpa-<sid>`；卡上 `files_n` 回到 0，该段 `rec.seg.stored`、`rec.seg.deleted` 各一次；没有任何 `queue.added`。
  - 30 天保留干检查：现在不到期；30 天 + 1 分钟后到期；`transcript.json`、`summary.md` 都在；没有 `rec.audio.swept`。
- [x] **rec_meeting**（PASS 20/20，84.2 s）
  - `seg_ms` 30 s，Opt+R → Enter → 按 `1` → 20 s、50 s 各一个 ★ → 70 s 时 Opt+R → Enter：3 段，前两段都正好 480,000 样本，`t0_ms` 0 / 30,000 / 60,000，`gaps` 空，`consent:"ack"`，项目 `cpa`，时长 70,528 ms。
  - Enter 到第一个样本 **368 ms**（≤0.8 s ✅；hwtest 的"Enter → 第一块"是 424 ms，减去约 48 ms 的第一块一致）。总样本数比设备墙钟少 192（12 ms）。
  - 两个 ★ 的会话时间比按键时刻早 56 / 104 ms（第二个在第 1 段里，经 `t0_ms` 换算），在 ±400 ms 内。
  - fakewoof → done / summary、1 条待办；每日笔记一行 `- HH:MM #cpa 会议：[HIL 会议：定价和交付](…/summary.md) 1 分钟 · 待办 1 ^cpa-<sid>`；卡清空，没有 `queue.added`。
  - **app 层有效吞吐 334.2 KB/s**（3 段 2,257,028 B 用 6.6 s，每段 333.5–335.4 KB/s；目标 ≥100 KB/s ✅）；按这个速度 1 小时录音约 **5.6 分钟**（底线 ≤20 分钟 ✅）。和 G-A1 预测的"每块重新开关文件约 302–322 KB/s"一致，说明 app 的拉取瓶颈在读卡，不在链路。
- [x] **rec_resume**（PASS 10/10，81.0 s）
  - 第二段拉到 256 KiB 后 `cpa pause 4`：`.part` 284,280 B；重连后第二段的第一个 `file.get` 偏移 **218,744 = 284,280 − 65,536** ✅；2 次连接，会话只归档一次，每段 `rec.seg.stored` / `rec.seg.deleted` 各一次，SHA 全对，卡清空。
  - 第二段的 142.7 KB/s 包含 4 s 暂停和重连；另外两段 334.4 / 326.1 KB/s。
- [x] **rec_reboot**（PASS 18/18，81.4 s；同一场景另外跑过 4 次，见第 6 条）
  - (a) 第 1 段录了约 5 s 时 `debug.reboot`：新 boot `store=sd`、带 `files`；第 1 段作为 `rcv` 段落地（`recovered:[1]`），头已经是标准头（设备恢复时就回填好了，所以 cpad 的 `repaired` 为空），会话完整（`gaps` 空）、`consent` 保留；**丢失 28 ms**（重启前最后一次读到的 `up_ms` 减去录下的末尾，1 s fsync 节奏以内）。
  - (b) `debug.state` 一显示第 1 段就 `debug.reboot`：新 boot `store=sd`；第 1 段的 `.W` 在开机扫描时被删（头还没 fsync），会话以 **`gaps:[1]`、只有第 0 段** 收尾，manifest 过契约 v1——**尾部缺段的情况确实走到了**。但 daemon 在第 0 段删除后的 45 s 里没有把会话收尾，靠 harness 做一次 `cpa pause 2` 重连、重新列清单才归档（§6.7 第 5 条）。
- [ ] **rec_live_hold**（验收运行 **FAIL 15/16**，78.5 s；判定改了之后见下）
  - 第一段拉到 103,500 B 时按住 G0 4.5 s：设备回了 3 个 `E_BUSY msg:"live"`（排队的请求被冲掉）和 1 个迟到的 `file.data`（音频任务已经取走的那一次读）；E_BUSY 窗口里没有任何非 E_BUSY 的回复；直到 `live:false` 的 pong 之前没有别的 `file.data`；被打断的文件从断点续传、没有从 0 重来；重新列清单后**便条排在会议剩下两段前面**；两个会话 SHA 全对，卡清空。
  - 不过的一项：`host_sent_no_file_requests_while_busy`，窗口里 daemon 发了 1 个 `file.get`（off 285,660），时间是**第一个 E_BUSY 到达 tap 之后 0.1 ms**，设备照样回了 E_BUSY。0.1 ms 不可能是 daemon 读到 E_BUSY 以后的反应（tap 在串口读线程里记录，daemon 要经过事件循环才能读到这一行），是和 E_BUSY 在线上交错的请求；11 §5.4 的主机不变式说的是"收到 E_BUSY 之后"。所以 harness 的判定改成"E_BUSY 到达 tap 20 ms 以后不再发 `file.*`"，并把每个窗口内请求的时刻写进报告（`tools/hil/tests` 有对应用例）。第二次试跑里同样的交错出现过一次（那次还没记下时刻）。
  - **判定修改后的复跑**：03:04–03:09 `rec_live_hold,rec_reboot` 连跑两次，**4/4 PASS**。`rec_live_hold` 两次：`E_BUSY live` 2 个 / 3 个，迟到的 `file.data` 各 1 个，第二次又有 1 个交错请求（0.1 ms，off 280,140，设备回 E_BUSY）；另外验收运行前单跑一次也 PASS（16/16，没有交错）。合计 5 次运行里 3 次窗口里有 1 个 daemon 发出的 `file.*`（记下时刻的 2 次都是 0.1 ms），全部得到 E_BUSY；没有一次是 daemon 读到 E_BUSY 之后才发的。
- [x] **rec_reboot 的重复性**（试跑 2 次、验收运行 1 次、复跑 2 次，共 5 次）
  - (a) 段中重启 5/5 恢复：`rcv` 段落地、会话完整；丢失 21 / 28 / 13 / −7 ms（最后一个是"重启前读到的 `up_ms`"比录音末尾早几 ms，正常）。
  - (b) 换段后立即重启：4 次 `store=sd`，4 次都走到尾部缺段（`gaps:[1]`），**4 次都要重连才收尾**（§6.7 第 5 条）。**1 次（第二次试跑）新 boot 起来是 `store=int`、hello 和 pong 都不带 `files`**：设备把卡当成不可用，录音功能整次开机都没有；那次 harness 还没有"再重启一次"的处理，45 s + 重连后都没等到会话；再 `debug.reboot` 一次就回到 `store=sd`，开机扫描照常恢复（§6.7 第 3 条）。
- [x] **rec_rotation（G-A4）**（PASS 11/11，341.5 s）
  - `seg_ms` 10 s，录 305.5 s：**31 段、30 次换段，30 个整段都正好 160,000 样本**，`t0_ms` 连续、没有缺段。
  - 总样本 4,886,528，设备墙钟 305,509 ms：**少 1,616 样本（101 ms），在 1 块（2,048）以内** ✅。开始时延 367 ms。31 段全部 SHA 正确，拉取 332.0 KB/s（9.77 MB 用 28.8 s，每段 318.7–336.4 KB/s，0 次重取）。
  - 没测到：最坏换段耗时和环形缓冲最高水位（G-A4 要求记录）在 HIL 里拿不到，`debug.state` 没有这些数（§6.7 第 6 条）；device `test_rec_rotation` 报 `ring_high=1`（6 块里最多积压 1 块）。
- [x] **rec_soak（G-A2）**（PASS 15/15，663.8 s）
  - 10 分钟会议（默认 5 分钟段：2 个 9,600,044 B 的段 + 1 个尾段），录音期间 600 次 ping、1,134 份 `sessions.snapshot`，共 **1,854 个主机请求全部有回复：p50 4.3 ms、p95 22.5 ms、最长 38.6 ms**（p95 ≤100 ms ✅，0 个超过 600 ms ✅），**0 次 `E_NOT_READY`** ✅，端口没重新打开过，720 个 pong 全部 `live:true`。
  - "丢帧"：总样本比设备墙钟（601,460 ms）少 1,856（116 ms），在 1 块以内，`t0_ms` 连续 → 没有丢样本 ✅（间接证据，§6.7 第 6 条）。
  - 两个 9.6 MB 段写入、落地、SHA 都对（sd-card §11 的驱动修复在 app 里生效）；拉取 19.24 MB 用 56.3 s，**334.1 KB/s**。
- 试跑中修好的 harness 问题（都在本次提交的 `tools/hil` 里，不涉及固件和 daemon）：
  - `rec_reboot` 原来在新 boot 不带卡时只会干等超时，现在记下新 boot 的 `store` / `files`，不是 SD 就判失败并再重启一次，好把会话检查完。
  - 一次运行里的"新会话"原来只按 `rec.archived` 区分；上一次运行留在卡上、到这次运行中途才以 `gaps` 收尾的会话会被误认为本场景的录音（02:37 的 `rec_live_hold`）。现在按"已归档或已有落地段"的会话集合区分（`rec_common.known`），拉取进度也只从本场景录音结束后的 tap 开始看。
  - `rec_live_hold` 的主机判定加了 20 ms 交错宽限（上面第 5 条）。

### 6.5 v0.2 回归（G-R）

- [x] **`cardputer-adv-test`，插卡：v0.2 的 14 个场景**（03:10:06–03:17:23，**14/14 PASS，退出码 0**；`hello.store=sd`）
  - `sd_empty` 待发 0；`sd_store` 3 条在 SD 上跨 `debug.reboot` 各送达一次；`sd_bulk` 100 条积压，重新握手到最后一条 ack **1.54 s**（≤5 s）。
  - `handshake` 6/6（重连 boot 不变）；`capture` Enter 到笔记 **0.096 s**；`offline_replay`、`dup_ack`（设备发 2 次、笔记 1 条）、`reboot`（重新 Ready 1.28 s）、`board`（3.05 s）、`spawn`、`offline_spawn`（0 次 fake-claude 调用）都通过。
  - `latency`：看板 p95 **3.08 s**（≤5 s），Compose p95 **50 ms**（设备时间戳，≤300 ms），主机看到的 p95 119.9 ms；`cjk` 6 个中文名、`efontCN_12` 0 缺字；`back_keys` 8/8。
- [x] **浸泡 10 分钟，SD 为主**（03:17:33–03:27:33，**PASS 6/6**）：10 条笔记 0 丢失 0 重复；2,048 个主机请求全部有回复，p50 2.9 ms、p95 22.0 ms、最长 57.6 ms，0 次 TX 卡住、0 次 nudge，端口没重开，结束时 outbox 为空（v0.2 §5.10 是 p95 18.6 ms / 最长 37.7 ms）。
- [x] **迁移三阶段（nosd → test → nosd，v0.3 固件）**（03:27:47–03:28:41，**4/4 PASS**，固件都用 `pio run -e <env> -t upload` 换，日志是 `pre_upload: skip: already paused for 53xx s`）
  - 前置 `sd_empty`（test）：`store=sd`，`pending=0`。
  - `migrate_seed`（nosd）：`store=int`，20 条（`mr00`–`mr19`）存进 `cpafs`，一条也没送达。
  - `migrate_check`（test）：首次 hello `store=sd`、`pending=20`、同一台设备；20 条每条恰好一次，id 都以种下时的 boot `1833562-0000K` 开头。
  - `migrate_drained`（nosd）：首次 hello `store=int`、`pending=0`。
- [x] **`cardputer-adv-test-nosd`：`rec_nosd` + v0.1 的 10 个场景**（03:28:41–03:35:01，**11/11 PASS，退出码 0**；`hello.store=int`）
  - `rec_nosd` 6/6：hello 和 `debug.state` 都没有录音字段（没有 `files`、没有 `rec`）；按住 G0 1.5 s 之后没有任何浮层、outbox 不变；Opt+R 没有确认框；daemon 一个 `file.*` 都没发。
  - `capture` 0.126 s；`reboot` 1.55 s；`board` 3.07 s；`latency` 看板 p95 3.095 s、Compose p95 50 ms（主机 127.3 ms）；`cjk` 0 缺字；其余都通过。
- [x] **收尾**：03:36 再跑一次 device `test_sd_store`（结果同 §6.3：只有挂载堆一项不过，`heap_delta=19204`；它的第一项会清掉 `/cpatest`，所以 `test_rec_sd` 留下的文件已经删掉），然后 `pio run -e cardputer-adv-test -t upload`（RAM 66,152 B，比 v0.2 的 63,444 B 多 2,708 B，≤4 KB ✅），`sd_empty,handshake` 2/2 PASS（`store=sd`、`pending=0`、boot `1833562-0000Q`）。**设备留在 `cardputer-adv-test` 上，用户的 cpad 保持暂停**，等 Lead 的复跑；没有刷回正式版。

### 6.6 需要人做的录音检查（11 §14、12"真机闸门"）

每一项做完以后把 `[ ]` 改成 `[x]`，填上日期和结果。凡是要刷固件的，先 `cpa pause <秒>`、`lsof /dev/cu.usbmodem*` 为空再动；**不要按住 G0 上电**（03 §2）。录音期间的 G0 是正常手势，可以按。

- [x] ~~**needs user** · **G-A5 耳听：定提示音音量**（约 2 分钟；阻塞 M3-A 的音量常量）~~
  - 日期：2026-10-21 结论：**不适用**，没有做。需求改为：录音不出声，只给自己用。v0.3.1 起录音全程没有提示音；会议确认框保留；屏幕是唯一提示。没有提示音，也就没有音量要定，`kToneVolume` 不再需要。下面三行是原来的做法，留作记录。
  - 怎么做：按 m3-device-gates.md §4.5：`cpa pause 900`；`cd firmware/hwtest && pio run -e cardputer-adv -t upload`；`uv run --no-project --with pyserial==3.5 python host/hwtest_host.py cue-listen --vols 32,64,128,192,255`（终端先打印"now playing …"，每个音量依次放便条开始音和会议三音）。设备放在口袋里和桌上各听一遍；需要时 `--vols 48,64,96` 再细分。
  - 判定：选**口袋里也能听见、桌上不刺耳**的最低一档；会议三音在安静的会议室里不算失礼。把这个数写进 `firmware/app/lib/hal/audio_io.h` 的 `kToneVolume`（现在是占位值 64）和 m3-device-gates §4.5，然后刷回 app。
  - ~~日期：＿＿ 选定音量：＿＿ 口袋 / 桌面感受：＿＿~~
- [x] **needs user** · **G-A10 电平：HIL `mic_cal` 标定 + `mic_verify` 核对**（约 25 分钟，Mac 外放约 7 分钟；阻塞 v0.3.1 的 `kMicGain` 和 WhisperWoof 的 `CARDPUTER_SILENCE_RMS`）
  - 2026-10-21 起替代原来的 hwtest 步骤（只调 magnification / PGA，结果写进已经删掉的 `kMicMagnification`）。v0.3.1 的增益在 ES8311 里（REG14 PGA、REG17 ADC 音量），`magnification` 是 2；常量是 `firmware/app/lib/core/mic_gain.h` 的 `kMicGain`，标定前是占位值 `{0, 0xBF, 16}`（v0.3.0 的增益），2026-10-21 定为 `{9, 0xC4, 2}`。目标：手持、正常音量说话时样本峰值约 −12 dBFS（−14 到 −10）。
  - 怎么做：完全按 m3-device-gates.md §5.2 的 9 步。
    - ⚠️ 顺序和平时相反：跑之前 cpad 要**恢复**（刷完测试固件后 `cpa pause 1`），并且 `cpa status --json` 显示 `on_device` 0、`pulling` 0。preflight 会自己暂停 cpad。
    - 命令：`uv run --project daemon python tools/hil/run_e2e.py --scenarios mic_cal --report "$TMPDIR/mic-cal.json"`。
    - 改 `kMicGain`（同一次提交里改 `test_the_placeholder_keeps_v030`），重新刷测试固件，`cpa pause 1`，再跑 `--scenarios mic_verify --report "$TMPDIR/mic-verify.json"`。
    - 最后刷回 `cardputer-adv`，`cpa pause 1`，`cpa status` 显示 `link: ready`。
  - 判定：`mic_cal` 全部检查 ok，`decision.ok` 为 true；`mic_verify` 的 `kmicgain_verified` 和卡清空的检查都是 ok。出错时看报告 `error` 末尾：是 `cleanup:` 就可以直接重跑；是 `cleanup FAILED` 就按 §5.2"跑失败了怎么办"做完再 `cpa pause 1`。
  - 中英混说准确率（MER）不在 `mic_cal` 里，要不要做、算不算阻塞由你决定（§5.2 末尾）。
  - 日期：2026-10-21（agent 跑，独占设备；`mic_cal` 12:21–12:27，`mic_verify` 12:34–12:35） `kMicGain`（PGA / REG17 / mag）：**9 / 0xC4 / 2**（+27 dB / +2.5 dB / 不放大） 预计峰值 / RMS / 噪声底（dBFS）：**−11.93 / −35.23 / −73.74** `lead_ms`：**150 不变**（新增益下 5 次 70–90 ms，`recommend_lead` 100） SILENCE_RMS：**0.000843** `mic_verify` 增益 / 峰值：**+11.74 dB（名义 +11.44）/ −11.96 dBFS**
  - 结论：`mic_cal` 8/8 项检查 ok、`decision.ok` true，两个独立复算得到同一档；`mic_verify` 5/5（`kmicgain_verified`、卡清空都 ok）。每档的数字和判定过程见 m3-device-gates.md §5.4，复跑见 §6.9。中英混说准确率（MER）没有做，仍由你决定。
- [ ] **needs user** · **G-A7 续航：录音中（背光调暗）≥8 小时**（过夜）
  - 怎么做：刷正式版 `cardputer-adv`（v0.3），充满电；拔掉 USB（侧面开关 ON，用电池）。Opt+R → Enter 开始会议，放着不动：60 s 后屏幕应调暗（不是熄灭），`● REC` 和计时一直可见。会议到 3 小时上限会自动结束（v0.3.0 有会议结束音；v0.3.1 起不出声，看会议页关闭），**马上再 Opt+R → Enter 开一个**，直到设备在电压 ≤3.45 V 持续 10 s 后自己停下（v0.3.0 是三声低音；v0.3.1 起不出声，看屏幕上的"电量低，不能录音"），或开始时提示电量低。每次开始 / 自动结束时看一眼系统页（Opt+M）的 `电池 NNNNmV` 并记下时间。最后插上 USB，`cpa rec ls` 里把这些会话的时长加起来。
  - 判定：总录音时长 ≥8 小时。记下的电压用来校准 3.45 V 停止 / 3.55 V 警告阈值（`rec_core.h` 的 `kBatStopMv`）。⚠️ 12 号文档要求"每分钟记一次 bat_mv"，但用电池时设备不连 Mac，v0.3 固件也不在卡上记电压日志，只能靠上面的人工读数（§6.7 第 7 条）。
  - 日期：＿＿ 总时长：＿＿ 读数（时间 / mV）：＿＿ 最后停下的原因：＿＿
- [ ] **needs user** · **G-A8 断电恢复：10 次，其中 3 次正好在换段时**
  - 怎么做：插卡的 v0.3 固件（正式版或测试版都行），daemon 先 `cpa pause 3600`（让段留在卡上）。7 次普通断电：Opt+R → Enter 开会，录 20–60 s，然后把侧面开关拨到 OFF 并拔掉 USB，等 5 s，再插上、拨 ON。3 次换段断电：默认 5 分钟一段，看会议页的计时，在 **05:00–05:01**（或 10:00–10:01）之间断电（新段的头要 1 s 后才落盘，这时断电正好是"尾部缺段"的情况）。每次上电后等 2 s（开机扫描），看状态栏 `录N` 是否增加。全部做完后 `cpa pause 1`，等 `录N` 回到 0，`cpa rec ls` / `cpa rec show <sid>` 看每个会话；用 QuickTime 打开每个会话最后一段的 WAV。最后把卡放进 Mac，"磁盘工具 → 急救"。
  - 判定：每次开机都把正在录的那段恢复成能播放的 WAV（`rcv:true`，manifest 里 `recovered:true`）；会话靠 `next` 接上，或按缺段收尾（换段断电的那 3 次应是 `gaps` 含最后一段的下一段）；全部同步、`录N` 归零；急救没有交叉链接，丢失簇数量记下来。注意两点：(1) 如果断电后第一次开机系统页显示"SD 卡无法使用"或状态栏没有 `录N`，记下来，这是 §6.7 第 3 条（软件重启时见过一次）在真实断电下的表现；(2) 尾部缺段的会话要等下一次重新连接（或下一次录音）才会归档（§6.7 第 5 条），必要时 `cpa pause 2` 一次。
  - 日期：＿＿ 10 次结果（恢复 / 缺段 / 不能播放）：＿＿ 急救：＿＿
- [ ] **needs user** · **G-A9 录音中拔卡、卡满**
  - 怎么做：(1) 拔卡：Opt+R → Enter 开会，20 s 后拔出 SD 卡，留意屏幕卡顿和提示（v0.3.1 起不出声）；再插回卡，拨开关重启，`cpa pause 1` 同步。(2) 卡满：用一张**可以清空**的卡，在 Mac 上先写一个大文件，让剩余空间只剩约 30 MB（例如 `mkfile -n <大小> /Volumes/<卡>/fill.bin`，大小按 `df -h` 算），插回设备开机（剩余空间开机时读一次），录一个会议直到停下；再试一次剩余 <8 MB 时开始录音。做完删掉 `fill.bin`。
  - 判定：(1) 拔卡后最多卡一次（≤2 s），有失败提示音（330 Hz × 3；v0.3.1 起不响，改为屏幕提示"SD 卡出错，录音已停止"），录音停止；重新插卡重启后这一段被恢复并同步。(2) 写满时本段正常收尾（回填头、写 `.M`），提示"SD 卡已满"，这一段能同步、能播放；剩余 <8 MB 时直接拒绝开始。
  - 日期：＿＿ 拔卡最长卡顿：＿＿ 卡满结果：＿＿
- [ ] **needs user** · **G-A12 手感：用一天**
  - 怎么做：正式版 v0.3，正常带一天：便条（按住 G0）、会议（Opt+R → Enter，期间 G0 / 空格打 ★）各开始结束 20 次。
  - 判定：提示音听得见又不烦（2026-10-21 起不适用：v0.3.1 录音全程不出声）；会议确认框（"请先告知在场的人"）不嫌麻烦；记下口袋里误触开始录音的次数；屏幕调暗后 `● REC` 看得清。
  - 日期：＿＿ 误触次数：＿＿ 感受：＿＿
- [ ] **needs user** · **G-G5 纪要质量（Mac 端）**
  - 怎么做：WhisperWoof 开发版（M3-W 的 4 个 PR），Intelligence 里选 Qwen3.5 2B。一段 bench 类录音（有明确待办和负责人）加 2–3 段设备录音（带 ★），走 Cardputer 收件箱生成 `summary.md`。
  - 判定（人工打分）：待办召回率；**编造的待办为 0**；每个 ★ 都出现在"重点（你的标记）"里；顺带看 ★ 前 30 s / 后 10 s 的窗口是否合适。
  - 日期：＿＿ 召回：＿＿ 编造：＿＿ ★ 覆盖：＿＿
- [ ] **needs user**（人 + agent）· **G-W2 后台整理时听写不变慢**（阻塞 W2 合并）
  - 怎么做：WhisperWoof 开发版、真实的 llama-server。先测 20 次听写润色的基线延迟；再让收件箱整理一段 60 分钟录音，同时听写 20 次。
  - 判定：润色 p95 比基线多 ≤300 ms；0 次因 3 s 超时悄悄退回原文（`POLISH_TIMEOUT`）；中止后台调用后 llama-server 立刻能接下一个请求（否则后台调用改成 `stream:true` 或加第二个 slot）。
  - 日期：＿＿ 基线 p95 / 整理中 p95：＿＿ 超时：＿＿
- [ ] **needs user** · **G-E2E 端到端（真实 WhisperWoof）**（阻塞发布）
  - 怎么做：正式版 v0.3 刷好；用户自己的 cpad 跑 v0.3 代码（`launchctl kickstart -k gui/$(id -u)/com.cardputer-assistant.cpad`）；WhisperWoof（含 Cardputer 收件箱）在设置里打开收件箱、模型已下载。拔掉 USB，离线开一个 20 分钟会（Opt+R → Enter），打 3 个 ★，在会议页按项目槽位号给它打上项目；再录一条以"待办"开头的便条；然后插上 USB。
  - 判定（11 §10 的时间预算内）：`summary.md` 的"重点"有 3 条、"待办"≥1 条，每条都能点回 `transcript.md`；当天的每日笔记有会议行和一条 `- [ ]` 便条行；设备 `录N` 回到 0；`cpa rec ls` 显示 done；没有产生任何队列项（`cpa status` 的 open queue items 不变）；便条从插上到出现在每日笔记 p95 ≤60 s。
  - 日期：＿＿ 结果：＿＿

### 6.7 发现的问题

- [x] （固件负责人，M3-A，`test/device/test_rec_sd`）· **1. device `test_rec_sd` 在 loopTask 上栈溢出，之后无限重启**
  - 证据：`Guru Meditation Error: Core 1 panic'ed (Unhandled debug exception). Debug exception reason: Stack canary watchpoint triggered (loopTask)`。回溯（`addr2line`）：`test_cut_off_session_is_recovered_at_boot`（`test_main.cpp:132`）→ `RecEngine::start` → `RecWriter::start` / `open_segment`（`rec_writer.cpp:21,27`）→ `VfsRecFilePort::mkdir`（`vfs_rec_file_port.cpp:132`）→ `esp_vfs_mkdir` → `f_mkdir` → `ff_sd_status` → `AcquireSPI`（`third_party/SD/src/sd_diskio.cpp:589`）→ `SPIClass::beginTransaction` → `xQueueSemaphoreTake`。`-fstack-usage`：这个测试函数自己的栈帧 **6,256 B**（两个 `RecEngine` 在栈上），而 `test_rec_sd` 没有像 `test_runtime_budget` 那样 `SET_LOOP_TASK_STACK_SIZE(16 * 1024)`，用的是 Arduino 默认 8 KB 的 loop 栈。设备崩溃后 Unity 每次从头重跑，`pio test` 不会自己超时，只能手动停。
  - 建议：给 `test_rec_sd` 加 `SET_LOOP_TASK_STACK_SIZE(16 * 1024)`，或把两个 `RecEngine` 改成静态对象。产品代码里引擎跑在音频任务上（`test_rec_budget` 量到余量 2,404 B），这不是产品的问题。修好后需要真机重跑这一个套件（后两项 `test_get_and_del`、`test_clean_up` 从没跑到过）。
  - 日期：2026-10-21 结论：**已修复，真机确认**。真正的大头是 `g_index = RecIndex{}` 这个约 4.1 KB 的临时对象（两个 `RecEngine` 各约 850 B、共用一块栈），修复去掉了它，改成断言索引还是开机时的样子，这个函数的栈帧从 6,256 B 降到 2,176 B，不需要 `SET_LOOP_TASK_STACK_SIZE`。另加两道防线：`stack_budget.py` 在 `pio test` 构建时把每个 device 套件自己的栈帧限制在 4,096 B；`device_test_host.py` 看到第一个 `Rebooting...` 就判失败，不再无限重跑。04:23 真机 **PASS 5/5**，后两项第一次跑到（§6.8）。
- [x] （固件负责人 M3-A + Lead，`test/device/test_sd_store` 是 G9 守护的 v0.2 测试）· **2. `test_sd_store` 的"挂载增加的堆 ≤16 KB"不再满足**
  - 证据：两次运行都是 `CPA_SD_MOUNT … heap_delta=19204`（v0.2 是 15,068 B，§5.3）。多出的约 4.1 KB 来自 11 §4.5 有意把 FatFs `max_files` 从 2 提到 3（`sd_card.cpp:49 kMaxFiles = 3`）。12 号文档的 M3-A 真机验收命令里包含这个套件，而它又在 G9 的"一行不改"名单里，所以按现在的规则它永远过不了。
  - 需要决定：按 daemon 那条 G9 例外的做法，把这一个上限改成 20 KB（或"16 KB + 每个额外文件名额 4.6 KB"）并写明；或者回到 2 个文件名额（11 §4.5 的"峰值 2 个"本来就够，第 3 个只是余量）。
  - 日期：2026-10-21 结论：**固件改回 `kMaxFiles = 2`**（第二个选项，不需要 G9 例外，`test_sd_store` 一行没改；**这是固件修复 agent 的选择，等 Lead 确认**）。真机：`test_sd_store` **PASS 7/7**，`CPA_SD_MOUNT us=18432 heap_delta=15068`，和 v0.2 完全一样；整个 app 空闲堆多回来 4,136 B。2 个名额下的录音路径也在真机上跑过：除了这次没重跑的 `rec_rotation`，录音场景全部通过，包括 `rec_live_hold`（拉取中按住 G0 开始便条）和 `rec_soak`（§6.8）。
- [x] （固件负责人，M3-A，`lib/hal/sd_card.cpp` / `third_party/SD`）· **3. 录音中换段时软件重启，下一次开机 SD 卡没挂上（`store=int`），整次开机没有录音功能**
  - 证据：`rec_reboot` (b)（`debug.state.rec.seg` 变成 1 后约 20–50 ms 内 `debug.reboot`）5 次里 1 次：重启后的 hello 是 `"boot":"1833562-00007","up_ms":1745,"rst":"software"`、`store=int`，之后的 pong 都没有 `files`（例如 `{"t":"pong",…,"up_ms":41907,"pending":0,"revs":{…}}`）；daemon 状态 `store: int`；单独再跑 `handshake` 也还是 `store=int`。再 `debug.reboot` 一次（`-00008`）就恢复 `store=sd`，那次会话之后照常被拉走、以缺段收尾。段中重启（a）5/5 没出现过。
  - 推测：`esp_restart()` 不断卡的电，重启时卡可能还在一次多块写（CMD25，写 `.M` 或新段）当中，或者在 Stop Tran 之后的忙期里；`mount()` 只试一次（`sd_card.cpp` 注释"no slower retry"），`sdcard_mount` 失败就退到内部存储。真实世界里对应的是录音中发生 panic、看门狗或掉电复位（不断卡电的那种）：这一整次开机录音被拒绝（"录音需要 SD 卡"），恢复扫描也推迟到下一次开机。
  - 建议：挂载失败时先补一个 Stop Tran 令牌 / CS 拉高多给一些时钟，再整套重试一次（只在第一次失败时，代价是开机多几十 ms）；加一个 device 测试：在 CMD25 写到一半时 `esp_restart()`，下一次开机必须 `SdState::Ok`。HIL `rec_reboot` 现在会把这种情况判为失败（`*_card_mounted_after_reboot`）。
  - 日期：2026-10-21 结论：**已修复，真机确认**。新的纯逻辑 `lib/rec/sd_wake` 在 `hal_sd_mount` 里、驱动第一次 CMD0 之前运行：卡回应 CMD0 就结束（正常开机只多一个 CMD0）；不回应就先把被打断的传输收尾（520 个时钟、Stop Tran + Nbr、CMD12，每步都等忙结束），再发 CMD0。没有加挂载重试（`sdcard_mount` 失败后上游注销 VFS 却没 `f_mount(NULL)`，重试会碰到悬空的 FatFs 卷指针）。新 device 套件 `test_sd_wake` 用原始 SPI 在测试自己的文件里打断一次 CMD25 再挂载。真机证据（§6.8）：
    - 有修复：`test_sd_wake` **3/3**，块之间打断 `state=Ok` 19 ms，块中间打断 `state=Ok` 32 ms（两次运行一样）。
    - **对照（同一个套件，只把 `wake_card()` 注释掉的固件副本）**：块之间打断仍能挂上（驱动自己的初始化够用），**块中间打断后 `state=Absent`、301 ms**，后面清理那一项的挂载也失败，正是这条的 `store=int`；紧接着刷回有修复的构建，第一次挂载就把卡救回来。说明是 `sd_wake` 让块中间打断的卡能挂上。
    - HIL：`rec_reboot` 5 次运行共 10 次 `debug.reboot`（其中 5 次是换段后立即重启），**10/10 新 boot 都是 `store=sd`、带 `files`**。原来是 5 次里 1 次，所以 5 次 0 次本身不足以证明，直接证据是上面的对照。
- [x] （固件负责人 / Lead，`firmware/app` 的版本号）· **4. v0.3 构建报的版本还是 `cpa-fw/0.2.0`**
  - 证据：`release/v0.3.0` HEAD `689f66a` 的 `cardputer-adv-test` 在 hello 里报 `fw=cpa-fw/0.2.0`，每段 `.M` 和 manifest 的 `device.fw` 也都是 `cpa-fw/0.2.0`。发布前要改成 0.3.0，否则 v0.3 录音的 manifest 会标错固件版本。
  - 日期：2026-10-21 结论：**已修复**。`runtime.h` 的 `kFwVersion = "cpa-fw/0.3.0"`（两个 UI 测试的版本断言先改，作为 RED）。真机：测试版 hello `fw=cpa-fw/0.3.0`（HIL 报告 meta 和 `handshake`），正式版 `firmware.bin` 里的串是 `cpa-fw/0.3.0`，用户的 `cpa status` 显示 `fw=cpa-fw/0.3.0`。manifest 里的 `device.fw` 没有单独核对（临时目录随运行删除）。
- [x] （daemon 负责人，M3-B，`daemon/src/cpad/recordings/puller.py` / `service.py`）· **5. 尾部缺段的会话要等下一次重连或录音才收尾**
  - 证据：`rec_reboot` (b) 4/4：会话的第 0 段（`last:false`）拉完、删掉以后 45 s 都没有 `rec.archived`；harness `cpa pause 2` 重连、重新列清单后马上归档（`gaps:[1]`）。原因：`manifest.finalisable` 只在 `Recordings.listed()` 里判断，而 `RecPuller` 只在观察到的 rev 和期望不同时才重新列清单；daemon 自己删掉最后一段后 rev 正好符合期望，不再列清单。11 §6.4 写的是"某次完整列清单显示……"，行为和文字一致，但用户实际看到的是：断电前的最后一个会议在下次插拔或下次录音之前不会生成 manifest，WhisperWoof 不处理，每日笔记也没有那一行。
  - 建议：删掉一个 `last:false` 段之后，如果它的会话没有别的段在清单里，就安排一次重新列清单（或者把"自己删除后 `files.n == 0`"当作一次空的完整清单）。先在 `tests/unit/test_rec_pull.py` / `tests/integration/test_recordings.py` 加用例。
  - 日期：2026-10-21 结论：**已修复，真机确认**。一轮拉取结束时，如果 daemon 自己确认过的删除把某个还没归档的会话在清单里的段全删光了，`RecPuller` 就再列一次清单（纯函数 `pull.relist_to_finish`），由 `Recordings.listed()` 按 11 §6.4 收尾；完整的会话不多发 `file.list`。真机：`rec_reboot` (b) **5/5 尾部缺段（`gaps:[1]`），5/5 `rot_needed_relist=false`**，也就是在同一个连接上 45 s 内归档，不用重连；每次 `rec_reboot` 从 81 s 降到约 36 s。
- [ ] （固件 M3-A + 协议 M3-P，`debug.state.rec`）· **6. HIL 拿不到 mic 丢块、环形缓冲水位、换段耗时，也没量到整个 app 录音时的堆**
  - G-A2 写的是"0 dropped frames from debug.state"，G-A4 要求记录"最坏换段耗时和环形缓冲最高水位"，但 `debug.state.rec` 只有 `live/mode/seg/files_n/files_rev`。`MicRing` 里其实有 `missing_`（回调没能重新挂上缓冲的次数）和 `high_`（最高积压），只是没有报出来。本次 HIL 用设备时钟的样本数对账代替（`rec_soak` 10 分钟少 116 ms、`rec_rotation` 少 101 ms，都在 1 块以内），这能证明没有成块丢失，但不能直接给出计数。
  - 另外 G-A6 的"录音 + 提示音时堆 ≥96 KB / ≥32 KB"只在精简的 `test_rec_budget` 里量过（262.8 / 252.0 KB）；整个 app 只量了空闲（181.5 / 160.0 KB）。按 11 §4.7 的估算录音时再少约 6.7–8 KB，应该还有约 170 KB，但没有实测。
  - 建议（只在测试版）：`debug.state.rec` 加 `ring_high`、`mic_missing`、`rot_ms_max`、`heap_free`、`heap_block`，HIL 直接断言。
  - 日期：2026-10-21 结论：**协议这一半做完，固件那一半还没做，所以这条仍然开着**。schema、向量、protocol README、daemon spec 和固件编解码都加了这 5 个可选、可为 null 的计数（缺省或 null = 这个构建不量）；`heap_free` / `heap_block` 取录音期间的最小值。但固件运行时还没填，测试版的 `debug.state.rec` 里仍然没有这些字段，HIL 也还没断言。本次 `rec_soak`（5 分钟）照旧用设备时钟对账：少 672 样本（42 ms），在 1 块以内。下一步（固件）：音频任务把 `MicRing` 的 `high_` / `missing_`、换段耗时和最低堆填进去；然后 HIL 的 `rec_soak` / `rec_rotation` 断言 `mic_missing == 0` 并记下 `rot_ms_max`、`ring_high` 和整个 app 录音时的堆。
  - 2026-10-22（`fix/stability-followups`）：**固件那一半做完，等真机**。测试版音频任务每次开始录音时清零、每写一块更新这 5 个计数（`lib/rec/rec_health`，native `test_rec_health`），loop 通过 `RecPort::health()` 填进 `debug.state.rec`（本次开机还没录过音时省略）；`rec_soak` / `rec_rotation` 停止录音后读一次，断言 5 个都有、`mic_missing == 0`，把它们记进报告的 `health`。同时加了测试版的 `debug.state.diag`（堆、两个任务的栈余量、loop 计时、幽灵 / 重置，protocol README 第 21 条）。这两个场景在真机上跑过以后再勾这一条。
- [ ] （Lead / 固件）· **7. G-A7 的"每分钟记一次 bat_mv"做不到**：用电池时设备不连 Mac，v0.3 固件也不在卡上记电压日志。§6.6 G-A7 改成人工读系统页；要连续曲线就得加一个测试版的卡上电压日志（或者用一个能同时供电又不充电的 USB 隔离方案）。
- 记录，不算问题：
  - 常量仍是占位值：`kToneVolume = 64`（等 G-A5 耳听；2026-10-21 起不适用，v0.3.1 没有提示音）、`kMicMagnification = 16`（等 G-A10；v0.3.1 起换成 `lib/core/mic_gain.h` 的 `kMicGain`，2026-10-21 由 §6.6 G-A10 的 `mic_cal` 定为 `{9, 0xC4, 2}`）。12 号文档写明 G-A10 出结果之前 M3-A 不算完成。`lead_ms` 已是 G-A5 定的 150（manifest 里看到的就是 150）。
  - `rcv` 段的 `repaired` 一直是 false：设备开机恢复时已经回填了标准头（11 §4.6），cpad 校验时头已经对了，不需要再修。12 号文档"the rcv segment lands with a repaired header"在这个意义上成立（头是对的），只是修的是设备而不是 cpad。
  - 拉取在 app 层稳定在 332–335 KB/s（便条、会议、30 段、9.6 MB 段都一样），和 G-A1"每块重新开关文件约 302–322 KB/s"的预测一致；要再快就按 m3-device-gates §1 的建议在同一个 `fid` 连续读时保持文件打开。
  - §5.9 第 3 条（`handshake` 紧跟 `sd_store`）本次没有触发：验收顺序里 `sd_bulk` 夹在中间。

### 6.8 修复后的最终复跑，刷回正式版（2026-10-21 04:21–04:48）

固件树：`689f66a` 加上 §6.7 第 1–5 条的未提交修复（固件：`test_rec_sd` 去掉 4 KB 的 `RecIndex{}` 临时对象、`kMaxFiles` 回到 2、新的 `lib/rec/sd_wake` 和 device 套件 `test_sd_wake`、`kFwVersion = cpa-fw/0.3.0`；daemon：`RecPuller` 在自己的删除清空一个未归档会话后再列一次清单；协议：`debug.state.rec` 的 5 个可选计数，固件还没填）。用户的 cpad 先 `cpa pause 5400`（04:21:36），`lsof /dev/cu.usbmodem*` 为空；刷机都用 `pio run -e <env> -t upload`（日志 `pre_upload: skip: already paused`），没按任何键，没碰 G0。HIL 用临时 `$CPA_HOME` / notes_dir、fake-claude、fakewoof，录音里只有房间底噪，随临时目录删除；没碰用户的 `~/CardputerNotes` 和 `~/.claude`。

```bash
cpa pause 5400; lsof /dev/cu.usbmodem*
uv run --project daemon pytest tools/hil/tests -q && uv run --project daemon pytest daemon/tests -q
cd firmware/app
pio test -v -e cardputer-adv-test -f device/test_rec_sd -f device/test_sd_wake -f device/test_sd_store \
  -f device/test_runtime_budget -f device/test_rec_budget
# 对照：在临时目录里复制一份固件树，只把 sd_card.cpp 的 wake_card() 调用注释掉，跑 device/test_sd_wake；
# 然后用本仓库的构建再跑一次 test_sd_wake（救回卡、清掉 /cpatest）。副本用完即删
pio run -e cardputer-adv-test -t upload && cd ../..
# rec_soak 缩到 5 分钟：临时包装脚本先 import scenarios.rec_long 并设 SOAK_MIN = 5.0，再调 harness.runner.main（仓库代码没改）
uv run --project daemon python <包装脚本> --scenarios rec_memo,rec_meeting,rec_resume,rec_reboot,rec_live_hold,rec_soak --report "$TMPDIR/hil-rec.json"
for i in 1 2 3 4; do uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_reboot --report "$TMPDIR/hil-reboot-$i.json"; done
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,sd_store,sd_bulk,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk,back_keys --report "$TMPDIR/hil-v02-regress.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,handshake --report "$TMPDIR/hil-final-test.json"
(cd firmware/app && pio run -e cardputer-adv -t upload)          # 正式版
launchctl kickstart -k gui/$(id -u)/com.cardputer-assistant.cpad
cpa pause 1 && cpa status && cpa rec ls
```

- [x] **主机侧**：`pytest tools/hil/tests` 229 passed；`pytest daemon/tests` 836 passed；`protocol/tools/check_vectors.py` OK（25 种消息、15 个流程）。
- [x] **device 套件**（04:22:33–04:24:04，**20/20 PASS**，88 s）
  - `test_rec_sd` **5/5**（10.8 s）：第 3 项不再栈溢出，`test_get_and_del`、`test_clean_up` 第一次跑到（§6.7 第 1 条）。
  - `test_sd_store` **7/7**（28.2 s）：`CPA_SD_MOUNT us=18432 heap_delta=15068`（≤16,384 B ✅，和 v0.2 一样）；200 条立即 ack p50 9.1 / p95 10.0 / 最长 44.2 ms，200 条积压 p50 15.1 / p95 20.1 / 最长 92.5 ms；迁移 50 条每条 46 ms（§6.7 第 2 条）。
  - `test_sd_wake` **3/3**（10.2 s）：`CPA_SD_WAKE partial=0 state=3 ms=19`、`partial=100 state=3 ms=32`（3 = Ok）。
  - `test_rec_budget` **3/3**：`idle_free=279668 idle_block=270324 rec_free=271424 rec_block=262132 xfer_free=279124 audio_stack_left=2396 ring_high=1`（G-A6 精简版各项都过）。
  - `test_runtime_budget` **2/2**（整个 app）：`stack_left=5788 heap_free=189992 heap_min=180448 max_block=167924 audio_stack_left=3744`，`CPA_RX_DONE got=8192 bad=0`。空闲 185.5 KB / 最大块 164.0 KB（≥150 / ≥64 KB ✅），loop 栈余量 5,788 B（≥4 KB ✅）；比 §6.3 多 4,136 B，就是去掉的第 3 个文件名额。
- [x] **`sd_wake` 对照**（04:24–04:25）：同一个 `test_sd_wake`，固件副本里只去掉 `wake_card()` → **1/3**：块之间打断仍然 `state=3`（18 ms），**块中间打断 `state=1`（Absent）、301 ms**，`test_clean_up` 的挂载也失败。紧接着用本仓库的构建重跑 **3/3 PASS**（19 / 32 ms），第一次挂载就把上一轮卡住的卡救了回来。§6.7 第 3 条的结论在这里。
- [x] **录音 HIL**（04:25:50–04:36:13，**6/6 PASS，退出码 0**；`fw=cpa-fw/0.3.0`、`store=sd`、boot `1833562-0000R`）
  - `rec_memo` 18/18：从 G0 按下到第一个样本 588 ms（≤1.0 s ✅）；样本数比"按下 + 4 s"少 1,344；拉取 333.4 KB/s；todo 每日笔记一行，卡清空，没有 `queue.added`，30 天保留干检查 4 项通过。
  - `rec_meeting` 20/20：3 段，两个整段都正好 480,000 样本，`gaps` 空，`consent:"ack"`；★ 误差 −55 / −88 ms；Enter 到第一个样本 368 ms（≤0.8 s ✅）；70,509 ms 里少 1,744 样本；**app 层 330.9 KB/s**（每段 332.7 / 332.2 / 322.5），1 小时约 5.7 分钟。
  - `rec_resume` 10/10：`.part` 282,900 B，续传偏移 **217,364 = 282,900 − 65,536** ✅；2 次连接，只归档一次。
  - `rec_live_hold` 16/16：2 个 `E_BUSY live`、1 个迟到的 `file.data`，窗口里 daemon **一个 `file.*` 都没发**（这次没有交错）；被打断的文件没从 0 重来；便条（`…000A`）排在会议剩下两段（`…0008`、`…0009`）前面。这是在 `max_files = 2` 下跑的。
  - `rec_reboot` 18/18（35.6 s，原来 81.4 s）：(a) `rcv` 段 `[1]` 落地，会话完整，丢失 34 ms；(b) 新 boot `store=sd`，`gaps:[1]`，**`rot_needed_relist=false`**。
  - `rec_soak`（**按 Lead 要求缩到 5 分钟**，默认 5 分钟段）15/15（338.5 s）：929 个请求（300 次 ping、570 份 `sessions.snapshot`）全部有回复，**p50 2.9 ms、p95 22.3 ms、最长 37.4 ms**，0 个超过 600 ms，0 次 `E_NOT_READY`，端口没重开，359 个 pong 都是 `live:true`；300,970 ms 里少 672 样本（42 ms，1 块以内），`t0_ms` 连续；2 段（一个 9.6 MB 整段 + 尾段）共 9,629,784 B，拉取 332.5 KB/s。10 分钟版本见 §6.4。
- [x] **`rec_reboot` 再跑 4 次**（04:36–04:39，**4/4 PASS**）。连同上面一次，共 5 次运行、10 次 `debug.reboot`：**10/10 新 boot `store=sd` 且带 `files`**；(b) **5/5 尾部缺段、5/5 不用重连就归档**；(a) 丢失 34 / −26 / 13 / 35 / 36 ms（上限 1.5 s）。每次运行 `device_not_ready` = 2，也就是每次 `debug.reboot` 一个 `E_NOT_READY`，和 v0.2 的 `reboot`、`sd_store` 一样（每次重启一个）。
- [x] **G-R：v0.2 的 14 个场景，SD 为主**（04:39:24–04:46:41，**14/14 PASS，退出码 0**）：`sd_bulk` 积压排空 1.563 s；`capture` 0.104 s；`reboot` 重新 Ready 1.24 s；`board` 3.07 s；`latency` 看板 p95 3.088 s、Compose p95 51 ms（主机 126.2 ms）；`cjk` `efontCN_12` 7,545 个字形 0 缺字；`back_keys` 8/8；`handshake` `fw=cpa-fw/0.3.0`、`store=sd`。
  - 这次没重跑：10 分钟 SD 浸泡、迁移三阶段、nosd 构建（`rec_nosd` + v0.1 场景）。nosd 构建在 `hal_sd_mount` 里直接返回 Disabled，不会走到 `sd_wake`；它和 §6.5 测过的构建只差版本串和 `debug.state.rec` 的编码（nosd 不发 `rec`）。
- [x] **测试版收尾**（04:46:59）：`sd_empty,handshake` 2/2，`pending=0`、`store=sd`、boot `1833562-00014`；录音场景都以"卡清空"结束。
- [x] **刷回正式版 `cardputer-adv`**（04:47:09，SUCCESS 18.1 s，`pre_upload: skip: already paused for 3865 s`）：RAM 65,872 B（20.1%），app 955,616 B 写入 0x10000（SHA-256 `2505a42b…e5ae2a67`），"Hard resetting via RTS pin" 后正常启动，没进下载模式。正式版 `firmware.bin` 里 `debug.key` 出现 0 次（测试版 1 次），版本串 `cpa-fw/0.3.0`。
- [x] **用户的 daemon 换到新代码并恢复**（04:47:45）：`launchctl kickstart -k gui/$(id -u)/com.cardputer-assistant.cpad`，pid 80674 → 14514（它是本仓库 `daemon/` 的 editable 安装，所以跑的是带 M3-B 修复的工作区代码）。重启后的进程没有继承暂停，马上连上；04:47:51 按要求 `cpa pause 1`，之后 `cpa status`：`link: ready  dev=020000000001 fw=cpa-fw/0.3.0 pending=0 store=sd launcher=no`，`recordings: on_device=0 pulling=0 waiting=0 processing=0 blocked=0 failed=0`；`cpa rec ls` 退出码 0，只有表头（空）。`cpad.log`：`device ready` boot `1833562-00015`，没有错误。这一步也完成了 §5.10 最后一项（重启 LaunchAgent 让 `store=sd` 显示出来）。

### 6.9 v0.3.1：增益标定、核对和录音回归，刷回正式版（2026-10-21 12:18–12:41）

固件树：`release/v0.3.1` 的 HEAD `8381d7f`（静音录音 `aa76501` 合进了 main）加上 v0.3.1 增益的未提交改动（`lib/core/mic_gain.h`、`MicRing` 每次开始写入并读回 REG14 / REG17、`debug.mic`、`mic_cal` / `mic_verify` 场景）。设备和卡同 §6.0。录音都只在 HIL 的临时目录里。`--keep` 留下的临时目录只用来量开头 512 ms 和底噪，只记数字，量完立即删除。没碰用户的 `~/CardputerNotes`。

```bash
cd firmware/app
pio test -v -e cardputer-adv-test -f device/test_mic_gain          # 3 次
pio test -v -e cardputer-adv-test -f device/test_rec_budget
pio run -e cardputer-adv-test -t upload && cd ../.. && cpa pause 1  # preflight 要 cpad 恢复、卡上 0 段
uv run --project daemon python tools/hil/run_e2e.py --scenarios mic_cal --report "$TMPDIR/mic-cal.json"
# kMicGain 改成 {9, 0xC4, 2}；test_the_placeholder_keeps_v030 换成 test_the_calibrated_gain（先看到它失败）
(cd firmware/app && pio test -e native && pio run -e cardputer-adv-test -t upload) && cpa pause 1
uv run --project daemon python tools/hil/run_e2e.py --scenarios mic_verify --keep --report "$TMPDIR/mic-verify.json"
cpa pause 1800; lsof /dev/cu.usbmodem*
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,sd_store,handshake,capture,rec_memo,rec_meeting,rec_resume --keep --report "$TMPDIR/hil-regress.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_bulk,handshake --report "$TMPDIR/hil-handshake.json"
(cd firmware/app && pio run -e cardputer-adv -t upload)            # 正式版
cpa pause 1 && cpa status
```

- [x] **主机侧**：固件 native 416/416（gcovr 94%）；`tools/hil/tests` 335 passed；daemon 848 passed（96.8%）；`protocol/tools` 246、`protocol/recording` 87 passed；`check_vectors.py` OK（26 种消息、15 个流程）。
- [ ] **device 套件**（12:18–12:21）：`test_mic_gain` 3 次里 2 次 PASS（§6.3，⏳ 第 1 次开头全 0）。`test_rec_budget` **3/3**：`CPA_REC_BUDGET idle_free=279604 idle_block=270324 rec_free=273224 rec_block=262132 xfer_free=279168 audio_stack_left=2364 ring_high=1`；录音时比 §6.8 多剩 1,800 B（没有提示音了）。`test_sd_absent` 要拔卡，没跑。
- [x] **`mic_cal`**（12:21:43–12:26:55，**8/8 PASS**）
  - preflight：`cpa status` 卡上 0 段、没在拉取；输出是 MacBook Pro Speakers、音量 69、没静音（只读，没改）；声音 Samantha / Tingting。B1 探测一次就落在窗口里（音 A −44.57 dBFS，参考音不用缩放），参考音 14.1 s。
  - 11 档全部有效，决定 `{9, 0xC4, 2}`，预计峰值 −11.93、RMS −35.23、噪声底 −73.74 dBFS，`silence_rms` 0.000843；N33、N36 都没有削波；5 次 lead 90 / 80 / 70 / 80 / 90 ms → `recommend_lead` 100 → `kLeadMs` 保持 150。每档的数字和判定见 m3-device-gates §5.4。
  - 卡清空，16 段每段 `rec.seg.stored`、`rec.seg.deleted` 各一次；场景收尾时重启，去掉了 `debug.mic`。
- [x] **改常量，`mic_verify`**（测试固件 12:34 刷入，`pre_upload: skip: already paused for 3265 s`；12:34:16–12:35:17，**5/5 PASS**）
  - `kmicgain_verified`：按 `mic_gain.h` 读出 `{9, 196, 2}`；B1 探测音 A −44.52 dBFS；重启后不发 `debug.mic`，按固件自己的增益录：增益 **+11.74 dB**（名义 +11.44），人声 +11.83 dB；推算峰值 **−11.96 dBFS**、RMS −35.26、噪声底（高通）−74.26 dBFS；0 个削波；步长 1；这段的 lead 100 ms。
  - 开头 512 ms（§6.3 第 1 次失败的那种情况）：B1 是刷机后的第一段，第一个样本不是 0，8,192 个样本里 1,599 个 0（×8 时正常）；`kMicGain` 那段是重启后的第一段，221 个 0，前 150 ms 冲击峰值 4,554，150 ms 到 3 s 最大 44、RMS 10.9（−69.6 dBFS，不滤波）。
- [x] **录音回归，新增益的测试固件**（12:35:44–12:38:48，**6/7**；12:39 复跑 `sd_bulk,handshake` **2/2**）
  - `sd_empty` 2/2；`sd_store` 8/8（3 条跨 `debug.reboot` 各送达一次，boot `…0001C` → `…0001D`）；`capture` 5/5，0.103 s；`sd_bulk` 8/8，100 条积压排空 1.562 s。
  - `handshake`：第一次 4/6，`same_boot_after_reopen`、`up_ms_continuous` 不过，"重开前"的 hello 取成了 `sd_store` 重启前的那条（`…0001C`，up_ms 30,628），场景前后重启计数都是 1。这就是 §5.9 第 3 条：这次 `handshake` 紧跟在 `sd_store` 后面，中间没有 `sd_bulk`。按 G-R 的顺序（`sd_bulk` 在前）复跑 **6/6**，同一个 boot，`drift_ms` −1。
  - `rec_memo` 18/18：G0 按下到第一个样本 **506 ms**（§6.8 是 588 ms，少的是提示音和 `Speaker.end`）；3,456 ms，样本数比"按下 + 4 s"少 608；`lead_ms` 150；拉取 316.6 KB/s；todo 每日笔记一行，卡清空。
  - `rec_meeting` 20/20：Enter 到第一个样本 **8 ms**（§6.8 是 368 ms，三音提示没有了）；3 段，两个整段都正好 480,000 样本，`gaps` 空；★ 误差 −47 / −95 ms；70,916 ms 里少 2,112 样本（132 ms，容差 4,096；§6.8 是少 1,744）；**335.9 KB/s**，1 小时约 5.6 分钟。
  - `rec_resume` 10/10：`.part` 284,280 B，续传偏移 **218,744 = 284,280 − 65,536** ✅；2 次连接，只归档一次。
  - 新增益下的电平（7 段，都是没人说话的房间）：开头 150 ms 之后峰值 48–72（−57 到 −53 dBFS），RMS 11.0–14.7（−69.5 到 −67.0 dBFS）；样本最大公约数都是 1；`Mic.begin` 冲击只在第 0 段，前 150 ms 峰值 4,603–4,653（约 −17 dBFS）。每段前 512 ms 都不是全 0（0 的个数 151–355 / 8,192）。
- [x] **刷回正式版 `cardputer-adv`**（12:40，SUCCESS 13.9 s，`pre_upload: skip: already paused for 1495 s`）：RAM 65,920 B（20.1%），app 945,312 B 写入 0x10000，"Hash of data verified"，"Hard resetting via RTS pin" 后正常启动。正式版 `firmware.bin` 里 `debug.(key|state|reboot|rec|mic)` 出现 0 次（测试版里有 `debug.mic`）。
- [x] **用户的 cpad 恢复**（12:41:06）：`cpa pause 1` 之后 `cpa status`：`link: ready  dev=020000000001 fw=cpa-fw/0.3.1 pending=0 store=sd launcher=no`，`recordings: on_device=0 pulling=0`、`processing=0 blocked=0 failed=0`（waiting 是用户自己在等 WhisperWoof 的录音，数目和开始前一样）。

### 6.10 v0.3.2：开机后的第一条录音（#31），刷回正式版（2026-10-21 17:20–17:32）

固件树：`release/v0.3.2`（开机预热 codec、等到有声音才开始、测试固件的 `debug.codec`、`rec_cold_start` 场景、设备测试 `test_mic_cold`）。设备和卡同 §6.0。`rec_cold_start` 用 Mac 内置扬声器放 1 kHz 的音（−20 dBFS，系统音量只读、不改），和 `mic_cal` 一样单独跑、先过 preflight（内置扬声器没静音、`cpa status` 卡上 0 段、不在拉取），然后暂停真正的 cpad 1800 s。录音只在 HIL 的临时目录里，报告只有数字。设备上的 codec 冷状态用 `debug.codec` 的软复位（REG00 = 0x1F）得到：插着 USB 等多久都不会自己变冷。v0.3.2 起第 0 段 `.M` 的 `at.up_ms` 是写下的第一个样本的设备时间（codec 热时就是 `Mic.begin` 之后那一刻），§6.2 的对账算法不变；`device/test_rec_rotation` 的墙钟也改为从 `Started` 带的 `up_ms` 算（`Started` 现在随第一块声音才到）。

```bash
cpa pause 3600; lsof /dev/cu.usbmodem*                               # 串口要空
cd firmware/app
pio test -v -e cardputer-adv-test -f device/test_mic_cold            # 3 次；不用卡、不用放音
pio test -v -e cardputer-adv-test -f device/test_mic_gain -f device/test_rec_budget
pio run -e cardputer-adv-test -t upload && cd ../.. && cpa pause 1    # preflight 要 cpad 恢复、卡上 0 段
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_cold_start --keep --report "$TMPDIR/rec-cold-1.json"
# 第 2-4 次：每次之前 cpa pause 1 并等到 link ready、卡上 0 段；音文件 -1 dBFS（见下面 rec_cold_start 一项）
cpa pause 1800; lsof /dev/cu.usbmodem*
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,sd_store,sd_bulk,handshake,capture,rec_memo,rec_meeting,rec_resume --report "$TMPDIR/hil-regress.json"
cpa pause 1                                                          # preflight
uv run --project daemon python tools/hil/run_e2e.py --scenarios mic_verify --report "$TMPDIR/mic-verify.json"
(cd firmware/app && pio run -e cardputer-adv -t upload)              # 正式版
cpa pause 1 && cpa status
```

- [x] **主机侧**（2026-10-21 17:33 起，真机测试之后，在 `release/v0.3.2` 的工作树里按 `ci.yml` 把 repo、firmware、daemon、launcher 四个 job 的每一步都跑了一遍，**全部通过**）：固件 native 438/438（gcovr 97%，先删掉了旧的 `.gcda`）；`tools/hil/tests` 360 passed；daemon 854 passed（96.8%）；launcher 227 passed；`protocol/tools` 248、`tools/repo` 25、`protocol/recording` 87 passed；`check_vectors.py` OK（27 种消息、15 个流程）；`check_recording.py` OK；构建闸门脚本 53 passed；正式版 946,448 B，预算和分区表检查通过，没有 `debug.*`；`make_image` OK；大小、lizard、ruff 都干净（`daemon/src/cpad/app.py` 402 行的警告原来就有）。`cardputer-adv-test` 和 10 个设备测试都能编译。
- [x] **`test_mic_cold`**（17:20–17:21，**3 次都 PASS**，每次 4/4，约 15.5 s）
  - 测的是正式的 `MicRing` 路径：`cold()`（REG00 = 0x1F）后按 `kMicGain` 开始，前 12 块（1.5 s）里从第一块起被压住的块（`rec::block_muted`）至少 5 块；紧接着再开始，0 块；再 `cold()`、`warm()`（48 kHz 约 0.47 s）后开始，0 块。每次输出一行 `CPA_COLD …`。
  - 判定：全部 PASS。冷的那次不到 5 块说明软复位没把 codec 弄冷，`rec_cold_start` 的 `cold_was_cold` 也会不过。
  - 日期：2026-10-21 结果：3/3 `CPA_COLD`：cold **7 块（896 ms）**、warm **0 块**、after_warm_up **0 块**，warm_up_ms **491**。三次的数字完全一样。
- [x] **`test_mic_gain`、`test_rec_budget`**（17:21:35–17:22:05，都 PASS）
  - `test_mic_gain` 3/3：`CPA_MIC pga=10 vol=0xC5 mag=2 reg14=0x1A reg17=0xC5 gcd=1 gain_failures=0`；停了再开 M5 的默认值：`reg14=0x10 reg17=0xBF gcd=8 gain_failures=0`。
  - `test_rec_budget` 3/3：`CPA_REC_BUDGET idle_free=273296 idle_block=262132 rec_free=273032 rec_block=262132 xfer_free=278940 audio_stack_left=2344 ring_high=1`。
  - "空闲"比 §6.9 少 6.3 KB（279,604 → 273,296），但那是量的时机变了：`IndexReady` 在开机扫描后发出，开机预热接在扫描后面，测试一收到 `IndexReady` 就量，量到的是预热时（麦克风开着）的堆，所以和录音时（273,032）差不多。预热结束后的 `xfer_free` 278,940 和 §6.9 的 279,168 一样，预热没留下占用。音频任务栈余量 2,344 B（§6.9 2,364 B）。
- [x] **`rec_cold_start`**（17:22:49–17:26:20，测试固件 17:22 刷入，app 955,456 B；**4 次：12/15、15/15、15/15、15/15**）
  - 三条便条，每条按住 G0 4.5 s，扬声器先放音 1 s：`cold`（`debug.codec{warm:false}`）、`warm`（紧接着，什么都不发）、`power_on`（`debug.codec{warm:true}`：软复位再预热，`Start` 可能要等预热做完）。
  - 每条的检查：开头被压住（|x| ≤ 2，前面若有偏移校正的衰减尾巴也算在内）的部分不超过 20 ms；前 250 ms 里听得到 1 kHz 的音（≥ −50 dBFS）；开始到 `● REC` 不到 2.5 s（设备时钟：第 0 段的 `at.up_ms` + 一块 128 ms −（G0 按下 + 500 ms），是上限）。另外 `cold_was_cold`（开始到 `● REC` 至少 600 ms：codec 真的冷过）、`power_on_warm_up_worked`（不到 800 ms：预热起作用了）；落地检查、卡清空。
  - **音量**：preflight 读到输出是 MacBook Pro Speakers、没静音，但音量是 **25**（#31 复现和 §6.9 时是 69；只读，没改）。第 1 次按场景原样跑（音文件 −20 dBFS），音到设备约 −55 dBFS，三条的 `*_tone_from_the_start` 都不过（−55.16 / −55.11 / −55.06），其余 12 项都过。第 2–4 次用一个临时的包装脚本，在进程里把 `scenarios.rec_cold.TONE_DBFS` 改成 −1.0，再调 `harness.runner.main`（仓库里的文件没改，系统音量也没改）。音到设备约 −44 dBFS，3 次都是 15/15。
  - 每次都加了 `--keep`。量完下表的数字（用 `harness.cold_start` 和 `harness.mic_cal` 的函数）就删掉临时目录，录音没有留下来。
  - 日期：2026-10-21

    | 次 | 开始到 `● REC`（cold / warm / power_on，ms） | 开头被压住（ms） | 音，前 250 ms（dBFS） | 前 150 ms 峰值 / 500 ms 以后峰值（cold / warm / power_on） |
    |---|---|---|---|---|
    | 1（−20 dBFS） | 1,133 / 150 / 134 | 0 / 0 / 0 | −55.16 / −55.11 / −55.06 | 94/105 · 5,195/123 · 3,687/120 |
    | 2（−1 dBFS） | 1,131 / 134 / 134 | 0 / 0 / 0 | −43.86 / −44.01 / −44.01 | 264/313 · 4,935/354 · 4,571/248 |
    | 3（−1 dBFS） | 1,128 / 141 / 147 | 0 / 0 / 0 | −43.92 / −44.10 / −44.06 | 239/249 · 5,176/277 · 5,242/305 |
    | 4（−1 dBFS） | 1,143 / 134 / 134 | 0 / 0 / 0 | −43.91 / −44.05 / −44.08 | 237/242 · 4,808/368 · 3,387/282 |

  - 12 条的第一个样本都是有声音的。前 250 ms 的音和 1–2.5 s 的稳定段最多差 0.2 dB，所以第 1 次那 3 项不过只是因为音太轻，开头并没有被压住。冷的那条每次都比另外两条短约 1 s（2.97 s 对 3.97 s）：被压住的那约 1 s 丢掉了，没有写进文件。冷的那几条开头都没有偏移校正的衰减尾巴（前几个样本是 −4 到 5）。
  - 开头冲击：`warm` 和 `power_on` 的第一块就是 `Mic.begin` 的冲击，前 20 ms RMS 1,064–1,520，前 150 ms 峰值 3,387–5,242（约 −20 到 −16 dBFS），和 §6.9 的 4,603–4,653 同一量级。`cold` 没有冲击，前 150 ms 和稳定段一样（就是那个音）。用 `gate_cue.lead_ms` 的算法量：cold 都是 0 ms，warm 60 / 10 / 10 / 10 ms，power_on 50 / 10 / 10 / 10 ms。放着音时底噪就是那个音，所以这些 lead 偏小，`lead_ms` 150 的依据仍是 §6.9 的无声测量。
  - 前 512 ms 里正好是 0 的样本有 2–52 个（8,192 个里），都不在开头。
- [x] **录音回归**（测试固件，17:26:33–17:30:25，**8/8 PASS，退出码 0**）：`sd_empty` 2/2、`sd_store` 8/8、`sd_bulk` 8/8、`handshake` 6/6、`capture` 5/5、`rec_memo` 18/18、`rec_meeting` 20/20、`rec_resume` 10/10。
  - `sd_store`：3 条跨 `debug.reboot` 各送达一次，boot `…0001P` → `…0001Q`，`device_not_ready` 1（重启那一次，预期内）。`sd_bulk`：100 条积压排空 1.565 s。`handshake`：`fw=cpa-fw/0.3.2`、`store=sd`，同一个 boot，`drift_ms` 0。`capture`：0.093 s。
  - `rec_memo`：G0 按下到第一个样本 **525 ms**（v0.3.1 是 506 ms）；3,456 ms，样本数比"按下 + 4 s"少 304；`lead_ms` 150；拉取 316.1 KB/s；todo 每日笔记一行，卡清空。
  - `rec_meeting`：Enter 到第一个样本 **8 ms**；3 段，两个整段都正好 480,000 样本，`gaps` 空；★ 误差 −150 / −71 ms；70,880 ms 里少 1,536 样本（96 ms）；拉取 336.9 KB/s，1 小时约 5.6 分钟。
  - `rec_resume`：`.part` 288,420 B，续传偏移 **222,884 = 288,420 − 65,536** ✅；2 次连接，只归档一次。
- [ ] **`mic_verify`**（17:30:51–17:31:23，**没跑成**，0 项检查）
  - 第一步 B1 探测就停了：`B1 probe too_quiet`，音 A **−62.07 dBFS**（§6.9 音量 69 时是 −44.52），参考声音放大到上限也到不了 −48。收尾正常：设备重启（没有 `debug.mic` 残留），卡清空。
  - 这不是固件的结果：v0.3.2 没改 `kMicGain`，`test_mic_gain` 也通过了。要重跑，得先把 Mac 的音量调回 69（用户自己调；agent 只读不改），然后 `cpa pause 1`，再跑上面那条命令。
- [x] **刷回正式版 `cardputer-adv`**（17:31:37–17:31:49，SUCCESS 11.8 s，`pre_upload: skip: already paused for 1754 s`）：RAM 65,920 B（20.1%），app 946,448 B 写入 0x10000，"Hash of data verified"，"Hard resetting via RTS pin" 后正常启动。`firmware.bin` 的 SHA-256 是 `fd3835bd…4a98e1a2`。正式版 `firmware.bin` 里 `debug.(key|state|reboot|rec|mic|codec)` 出现 **0 次**（测试版里这 6 个各 1 次），版本串 `cpa-fw/0.3.2`。
- [x] **用户的 cpad 恢复**（17:31:57）：`cpa pause 1` 之后 `cpa status`：`link: ready  dev=020000000001 fw=cpa-fw/0.3.2 pending=0 store=sd launcher=no`，`recordings: on_device=0 pulling=0`、`processing=0 blocked=0 failed=0`。waiting 和开始前一样。
- [ ] **needs user** · **真断电后的第一条便条**：开关拨到 OFF、拔掉 USB，等 10 s，再插上开机；看到 `● REC` 马上说话，录一条 5 s 的便条。判定：转写出来的第一个字就是开口说的第一个字（或者音频开头没有 1 秒的 0）。日期：＿＿ 结果：＿＿

## 7. v0.4.0：主页、Tab 菜单、锁屏

> V40-H（[13 号文档](13-assistant-v2-plan.zh-CN.md) §17）的交付物。代码和主机侧检查已做完（§7.3）。**真机部分全部待 Lead 跑**：设备由 Lead 独占，V40-H 没有开串口、没有刷机、没有跑 HIL。下面的结果栏都留空，**没有伪造任何结果**。人工项（§7.5）要你本人。
> 固件在 `release/v0.4.0` 工作区（V40-F0、M、L、P、F、H 的改动，还没提交）。`kFwVersion` 当时仍是 `cpa-fw/0.3.1`；合入 v0.3.2 和 v0.4.1 的 Mac 侧之后已改成 `cpa-fw/0.4.0`。
> **真机部分已跑**（2026-10-21 18:38–18:53，agent 独占设备，固件树是 `release/v0.4.0` 的 `7bf847d`，见 §7.2）。nosd 回归和 `test_sd_store`、`test_rec_sd`、`test_rec_rotation` 这次没跑。

### 7.0 harness 跟着改了什么

- **根页面**：`src/module_list.cpp` 注册了主页（排第一个），根页面从项目看板变成主页（`debug.state.mod=home`，13 §5.5）。项目看板改由 Opt+P 打开。
- **`Device.home()`**：先读 `debug.state.ui`。锁着就按 Opt+L、Enter 解锁；菜单开着或有浮层就按 Fn+`；Compose 按 `、Fn+`，等过"放弃？"的 400 ms 防误触（docs/20 U18）再按 Enter，草稿放弃、不保存；最后按 Opt+H，等到 `mod=home`、`overlay=none`、菜单关、没锁。多数场景开头都调它。
- **`back_keys`**：从等你页、系统页按 ` 回到主页（原来回到看板）。v0.1–v0.3.1 的场景里只改了这一处期望。
- **新场景 `nav`、`menu`、`lock`**：只在点名时运行，`all` 仍是 v0.1 的 10 个。`menu`、`lock` 各录一段（一场会议、一条便条），超时 600 s，开头先等卡上清空（和 `rec_*` 一样）。nosd 固件上跳过录音部分，报告里写 `card: false`。
- **随机按键只在 `lock` 里用**，并且不含 Opt+L、Enter、G0（13 §19），免得设备自己解锁或开始录音。种子写在报告的 `seed` 里，可以复现。
- 纯逻辑在 `tools/hil/harness/ui.py`，单元测试 `tools/hil/tests/test_ui.py` 拿主页格子的字母（`modules/home/home_logic.h`）和全局层的 Opt 键（`lib/input/keymap.cpp`）跟 `nav` 的计划逐个核对。

| 场景 | 做法和判定 |
|---|---|
| `nav` | 每一步都从主页开始。主页按 P / I / M（项目、等你、系统）、Enter（第一个格子 P）、`/` 再 Enter（I）、`1`（第 1 个项目页）、Opt+P / I / M，各自到对应页面，再按 ` 回到主页；N / Q / S 打开 Compose，Fn+` 关掉；L 锁屏，Opt+L、Enter 解锁；R 插卡时打开会议确认框（等过 400 ms 保护后按 ` 取消，不录音），nosd 时被拒绝、留在主页；先 Opt+P、Opt+I 再按 Opt+H 回到主页，这时再按 ` 仍在主页（栈已清空） |
| `menu` | 主页、看板、等你、系统、Compose 上按 Tab：`ui.menu` 变 true，`mod` / `overlay` 不变（Compose 上 `overlay` 仍是 compose）；再按 Tab 关掉。按 Enter 执行一行：主页"本页"第 1 行（P）到看板；看板上"去哪"第 1 行（Opt+H）回主页；"去哪"上按 i 到等你；"控制"第 1 行（Opt+L）锁屏。插卡时再开一场会议：菜单开着时按空格、1、0、Opt+R，会议页都收不到（菜单仍开着，没有结束确认框，仍在录）；关掉菜单再打开，Enter 执行"本页"第 1 行（★）；再 Tab、G0（菜单关掉并加 ★）。结束后取回的 manifest 里正好 2 个 ★，没有项目 |
| `lock` | 主页按 Opt+L 锁屏，200 个随机键以后 `mod`、`overlay`、菜单、锁、`outbox.pending` / `bad`、`rec.live` / `mode` / `files_n` 都没变，`key_ms` 变了（键确实到了设备），Mac 上没有新笔记、没有 `item.received`。插卡时：黑屏上按住 G0 3 s，便条一次都没进入录音；等卡片熄灭后先点 G0 再按住 3 s，录下一条便条，之后仍然锁着，取回后正好 1 条新便条。解锁：Opt+L 后马上 Enter 解锁、回到主页；再锁上，Opt+L 后等 3.5 s 再 Enter 仍锁着（报告里 `gap_ms` 是设备上两次按键的间隔，要 ≥3000）；再 Opt+L、Enter 解锁 |

### 7.1 怎么跑（待 Lead，独占设备）

```bash
cpa pause 5400; lsof /dev/cu.usbmodem*          # 串口要空
cd firmware/app
pio test -v -e cardputer-adv-test -f 'device/test_runtime_budget' -f 'device/test_rec_budget' -f 'device/test_sd_store' \
  -f 'device/test_rec_sd' -f 'device/test_rec_rotation' -f 'device/test_mic_gain' -f 'device/test_mic_cold'   # 插卡
pio run -e cardputer-adv-test -t upload && cd ../..
cpa pause 1                                      # rec_cold_start 的 preflight 要 cpad 恢复、卡上 0 段（§6.10）
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_cold_start --report "$TMPDIR/hil-v040-cold.json"
cpa pause 5400; lsof /dev/cu.usbmodem*
uv run --project daemon python tools/hil/run_e2e.py --scenarios nav,menu,lock --report "$TMPDIR/hil-v040-ui.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,sd_store,sd_bulk,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk,back_keys --report "$TMPDIR/hil-v040-regress.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_memo,rec_meeting,rec_resume,rec_live_hold,rec_reboot --report "$TMPDIR/hil-v040-rec.json"
(cd firmware/app && pio run -e cardputer-adv-test-nosd -t upload)
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_nosd,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk,back_keys,nav,menu,lock --report "$TMPDIR/hil-v040-nosd.json"
(cd firmware/app && pio run -e cardputer-adv -t upload)          # 正式版（kFwVersion 已是 cpa-fw/0.4.0）
cpa pause 1 && cpa status                                          # link: ready，fw=cpa-fw/0.4.0
```

- `--scenarios` 里的顺序不要紧，`run_e2e.py` 按 `harness.select.CANONICAL` 排：存储场景在前，`sd_bulk` 在 `handshake` 前面（§6.9 那次 `handshake` 紧跟 `sd_store` 的问题不会出现）。
- `rec_rotation`、`rec_soak` 很长，和 v0.3 一样可以另找时间跑。
- v0.4.0 合入了 v0.3.2 的冷启动修复（#31），所以 `test_mic_cold` 和 `rec_cold_start` 也要在 v0.4.0 的固件上跑一遍。`rec_cold_start` 和 `mic_cal` 一样单独跑、用 Mac 扬声器放音；Mac 音量低时音太小，见 §6.10。模拟器跑不了它（`firmware/sim/README.md`）。

### 7.2 设备测试和自动场景（2026-10-21 18:38–18:53，agent 独占设备）

固件树：`release/v0.4.0` 的 `7bf847d`（工作树干净）。设备和卡同 §6.0，一直插着 USB。开始前 `cpa status --json`：卡上 0 段、不在拉取；Mac 输出是 MacBook Pro Speakers、没静音、音量 **25**（只读，没改）。录音都只在 HIL 的临时目录里，没碰用户的 `~/CardputerNotes`；用户的 cpad 只在 `rec_cold_start` 的 preflight 时连过测试固件（卡上 0 段，什么都没拉）。报告和日志在 agent 的 scratchpad 里。

```bash
cpa pause 3600; lsof /dev/cu.usbmodem*                              # 串口空
cd firmware/app
pio test -v -e cardputer-adv-test -f 'device/test_runtime_budget' -f 'device/test_rec_budget' -f 'device/test_mic_gain' -f 'device/test_mic_cold'
pio run -e cardputer-adv-test -t upload && cd ../.. && cpa pause 1   # 等到 link ready、卡上 0 段
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_cold_start --report "$SP/hil-v040-cold-1.json"
# 第 2、3 次：之前都 cpa pause 1 并等到 ready、卡上 0 段；临时包装脚本把 scenarios.rec_cold.TONE_DBFS 改成 -1.0 再调 harness.runner.main（§6.10 的做法）
cpa pause 3600; lsof /dev/cu.usbmodem*
uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,capture,board,cjk,back_keys,nav,menu,lock,rec_memo,rec_meeting,rec_resume --report "$SP/hil-v040-main.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_store --report "$SP/hil-v040-sdstore.json"   # 单独跑：它会重启设备
(cd firmware/app && pio run -e cardputer-adv -t upload)              # 正式版
cpa pause 1 && cpa status
```

- [ ] **`test_runtime_budget`（整个 app，加上主页模块）**：**仓库里的测试 FAIL**（1/2）；固件没问题，是测试的工作负载过时了。
  - 新画的页面，全部用真实按键走到：主页、菜单三页（本页 / 去哪 / 控制）、锁屏后的黑屏和锁屏卡片、会议确认框（v0.3.1 原文，取消、不录音）；锁屏时的 ● 行（会议、会议加卡片、便条）用一份"正在录"的状态副本直接画（这个测试里不录音）。
  - 判定：PASS 2/2；`CPA_BUDGET` 里 `missing_glyphs=0`（现在是断言）、`stack_left` ≥ 4096、`heap_free` ≥ 153,600、`max_block` ≥ 65,536（13 §10.3）。和 §6.3 的 `stack_left=5788 heap_free=185856 max_block=163828` 比，记下变化。
  - 主机上先核对过字形：这些页面所有字符串字面量里的每个字符都在 `efontCN_12` 里（用 `tools/hil/harness/glyphs.py` 读 libdeps 里的 M5GFX 字形表，0 个缺字）。设备上的 `missing_glyphs` 才是验收数字。
  - 日期：2026-10-21 18:38–18:45
  - **仓库里的版本**：`test_rx_burst_8k_is_not_lost` PASS（8,192 B，0 错）；`test_firmware_stack_and_heap_budgets` 在第 202 行 FAIL：按 Opt+R 后 `rec.phase` 不是 Confirm，`CPA_BUDGET` 没打出来。
  - **原因**（在 scratchpad 的固件副本里加打印，真机上查到）：`workload()` 在根页面按 Opt+S 开 session。v0.4.0 的根页面是主页，`contextSlot()` 是 0，所以 Enter 提交时是 `NeedProject`，Compose 一直开着。后面的确认框、`agent.spawn`、存成排队、Opt+I / M / 1 这几屏都被 Compose 吃掉了。`v040_screens()` 开始时 `overlay=compose`，Tab 菜单和 Opt+L 锁屏照样能用，Opt+R 却进了 Compose，所以没打开会议确认框。固件的行为和设计一致，没有发现固件的问题。
  - **副本里的结果**：在 `v040_screens()` 开头先按 Fn+`、Enter 关掉那个 Compose，只加这两键，其他不动。这样 **PASS 2/2**：`CPA_BUDGET stack_left=5788 heap_free=182944 heap_min=176532 max_block=163828 sent=34 pending=0 bad=1 store_errors=0 missing_glyphs=0 rec=1 audio_stack_left=3696`。
  - 和 §6.3 比：栈余量和最大块不变，空闲堆少 2,912 B（185,856 → 182,944）。注意：开 session 的那条路径这次没走到。v0.3 的根页面是看板总览，它的 `ui_.slot` 也是 0，所以按代码推断，§6.3 那次也没走到。
  - **仓库里的测试要另外修**：在 Opt+S 之前先进一个项目页（让开 session 有项目），或者在 `v040_screens()` 开头关掉浮层。修好以后重跑一遍。副本已删，只留了 diff。
- [x] **其余 device 套件**（`test_rec_budget`、`test_mic_gain`、`test_mic_cold`，回归；`test_sd_store`、`test_rec_sd`、`test_rec_rotation` 这次没跑）
  - 判定：和 §6.3 / §6.8 / §6.9 / §6.10 一样通过（`test_sd_store` 的挂载堆 ≤16 KB 是 §6.7 第 2 条已知问题，照旧记录；`test_mic_cold` 的 `CPA_COLD` 冷的那次至少 5 块，warm 和 after_warm_up 0 块）。
  - 日期：2026-10-21 18:38–18:40 结果：三个都 PASS（共 11 项）。
    - `test_rec_budget` 3/3：`CPA_REC_BUDGET idle_free=273168 idle_block=262132 rec_free=272900 rec_block=262132 xfer_free=278808 audio_stack_left=2372 ring_high=1`，和 §6.10 只差一百多字节。
    - `test_mic_gain` 3/3：`reg14=0x1A reg17=0xC5 gcd=1 gain_failures=0`；换回 M5 默认值后 `reg14=0x10 reg17=0xBF gcd=8`。
    - `test_mic_cold` 4/4：`CPA_COLD` cold **7 块（896 ms）**、warm 0 块、after_warm_up 0 块、warm_up_ms **491**，和 §6.10 完全一样。
- [x] **HIL `nav`、`menu`、`lock`**（插卡，`cardputer-adv-test`，`hil-v040-main.json`）
  - 判定：三个场景全部 ok；`menu.card`、`lock.card` 是 true；`lock` 的 `opt_l_enter_within_3s_unlocks` 的 `gap_ms` <3000，`enter_after_3s_stays_locked` 的 `gap_ms` ≥3000。
  - 日期：2026-10-21 18:48–18:50 结果：`nav` **30/30**（15 步，`card: true`）、`menu` **24/24**、`lock` **12/12**（seed **4191819892**）。
    - `menu`：`card: true`，会议 `…000A` 的 manifest 里正好 2 个 ★、没有项目。
    - `lock`：`card: true`。200 个随机键什么都没改。黑屏上按住 G0 没录。点一下再按住，录下正好 1 条便条（`…000B`），之后仍锁着。`gap_ms`：Enter 在 3 s 内 **124**（解锁），3.5 s 后 **3,616**（仍锁着），再解锁 **133**。
- [x] **HIL `rec_cold_start`**（插卡，`cardputer-adv-test`，`hil-v040-cold-{1,2,3}.json`）：音文件提到 −1 dBFS 以后 **15/15**（2 次）。
  - 判定：和 §6.10 一样 15/15：三条便条（cold / warm / power_on）开头被压住的部分不超过 20 ms，前 250 ms 听得到音，开始到 `● REC` 不到 2.5 s；`cold_was_cold`、`power_on_warm_up_worked` 都过。
  - 日期：2026-10-21 18:45–18:48 Mac 音量：**25**（MacBook Pro Speakers，没静音，只读、没改）
  - 第 1 次按场景原样跑（音文件 −20 dBFS）：**12/15**，只有三条的 `*_tone_from_the_start` 不过，都是电平问题：−54.46 / −54.68 / −54.73 dBFS，门限 −50。其余 12 项都过，开头被压住的都是 0 ms。和 §6.10 第 1 次一样。
  - 第 2、3 次用 §6.10 的包装脚本，音文件 −1 dBFS（仓库文件和系统音量都没改）：**15/15、15/15**。

    | 次 | 开始到 `● REC`（cold / warm / power_on，ms） | 开头被压住（ms） | 音，前 250 ms（dBFS） | 长度（s） |
    |---|---|---|---|---|
    | 1（−20 dBFS） | 1,128 / 137 / 134 | 0 / 0 / 0 | −54.46 / −54.68 / −54.73 | 2.97 / 3.97 / 3.97 |
    | 2（−1 dBFS） | 1,128 / 142 / 153 | 0 / 0 / 0 | −43.35 / −43.51 / −43.50 | 2.97 / 3.97 / 3.97 |
    | 3（−1 dBFS） | 1,128 / 145 / 156 | 0 / 0 / 0 | −43.35 / −43.61 / −43.46 | 2.97 / 3.97 / 3.97 |

  - 每次落地检查、卡清空、每段 `stored` / `deleted` 各一次都过。冷的那条每次都短约 1 s：被压住的部分丢掉了，没写进文件（§6.10 也是这样）。
- [x] **v0.1–v0.3.2 回归，插卡**（`hil-v040-main.json`、`hil-v040-sdstore.json`）：**每个场景都 ok**（`nav`、`menu`、`lock` 在同一次里，见上）。
  - 判定：每个场景都 ok。根页面变了，只有 `back_keys` 的期望跟着改了（回到主页）。
  - 日期：2026-10-21 18:48–18:52
  - 自动场景：`handshake` 6/6、`capture` 5/5、`board` 3/3、`cjk` 7/7、`back_keys` 8/8；录音 `rec_memo` 18/18、`rec_meeting` 20/20、`rec_resume` 10/10；`sd_store` 8/8（单独跑）。
  - `handshake`：`fw=cpa-fw/0.4.0`、`store=sd`，同一个 boot，`drift_ms` 1。`capture`：0.103 s。`board`：1.15 s。`back_keys`：从等你页、系统页按 ` 都回到 `home/none`。
  - `sd_store`：3 条跨 `debug.reboot` 各送达一次，boot `…0001T` → `…0001V`，`device_not_ready` 1（重启那一次，预期内）。
  - `rec_memo`：G0 按下到第一个样本 **507 ms**；3,456 ms，样本数比"按下 + 4 s"少 592；`lead_ms` 150；todo 每日笔记一行，卡清空。
  - `rec_meeting`：Enter 到第一个样本 **8 ms**；3 段，两个整段都正好 480,000 样本，`gaps` 空；★ 误差 −149 / −70 ms；70,880 ms 里少 1,536 样本（96 ms）。
  - `rec_resume`：`.part` 285,660 B，续传偏移 **220,124 = 285,660 − 65,536** ✅；2 次连接，只归档一次。
  - **拉取变慢了，原因没查**：`rec_meeting` 256.0 KB/s（1 小时约 7.3 分钟），§6.10 是 336.9 KB/s；`rec_memo` 212.5 KB/s，§6.10 是 316.1 KB/s。门限（≥100 KB/s，1 小时 ≤20 分钟）都过。这次 HIL 用的是本仓库合入 v0.4.1 以后的 cpad。
- [ ] **nosd 回归**（`cardputer-adv-test-nosd`，`hil-v040-nosd.json`）：**这次没跑**。
  - 判定：每个场景都 ok；`nav`、`menu`、`lock` 报 `card: false`（录音部分跳过，R 被拒绝）。
  - 日期：＿＿ 结果：＿＿
- [x] **刷回正式版，cpad 恢复**
  - 判定：`cpa status` 显示 `link: ready`、`fw=cpa-fw/0.4.0`；正式版 `firmware.bin` 里 `debug.(key|state|reboot|rec|mic|codec)` 出现 0 次；开机停在主页。
  - 日期：2026-10-21 18:52
  - 刷机：SUCCESS 11.2 s，`pre_upload: skip: already paused for 3326 s`。RAM 65,968 B（20.1%），app 969,824 B 写入 0x10000，"Hash of data verified"，"Hard resetting via RTS pin" 后正常启动。
  - 正式版镜像：`firmware.bin` 的 SHA-256 是 `847d67ac…4be5c021`，`debug.(key|state|reboot|rec|mic|codec)` 都是 **0 次**（测试版里各 1 次），版本串 `cpa-fw/0.4.0`。
  - cpad 恢复（18:52:49）：`cpa pause 1` 之后 `cpa status`：`link: ready  dev=020000000001 fw=cpa-fw/0.4.0 pending=0 store=sd launcher=no`，`recordings: on_device=0 pulling=0`，其余的录音和条目数和开始前一样。
  - "开机停在主页"没有核对：正式版没有 `debug.state`，agent 也看不到屏幕。

### 7.3 主机侧（2026-10-21，V40-H，没接设备）

- [x] **固件**（`firmware/app`）：`pio test -e native` 55 个套件 525/525 通过；gcovr（先删掉旧的 `.gcda`）97%（≥80%）；`pio run -e cardputer-adv` SUCCESS，0 个警告，栈预算 0 处违规；`check_budget.py` 通过，静态 RAM 65,968 B，app 961,504 B；`check_partitions.py` 通过；正式版 `firmware.bin` 里 `debug.*` 名字 0 次，链进了 `HomeModule`。`check_sizes.py --warn 400 --fail 800` 280 个文件 0 警告；`lizard -L 50` 通过；ci.yml 的 include 规则通过，`lib/help` 也没有 Arduino / M5 / esp 头文件。
- [x] **HIL 用的两个测试固件只编译不刷**：`pio run -e cardputer-adv-test`（静态 RAM 66,248 B）和 `-e cardputer-adv-test-nosd`（65,544 B）都 SUCCESS，栈预算 0 处违规。
- [x] **device 套件只编译不跑**：`pio test -e cardputer-adv-test -f 'device/*' --without-uploading --without-testing`，9 个套件都编译、链接通过；每个套件自己的栈帧都在 4,096 B 以内（`test_runtime_budget` 最大的一帧 784 B）。
- [x] **脚本和 harness**：`pytest scripts test/core/support` 60 passed（新增 v0.4.0 的栈链和 `help` / `home` 目录的测试）；`pytest tools/hil/tests` 367 passed；`ruff check` / `ruff format --check`（0.17.1）通过。
- [x] **合入 v0.3.2 和 v0.4.1 的 Mac 侧以后**（2026-10-21，`kFwVersion` 已是 `cpa-fw/0.4.0`）：`pio run -e cardputer-adv` 静态 RAM 65,968 B（不变），app **969,824 B**（比上面多 8,320 B）；正式版 `firmware.bin` 里 `debug.(key|state|reboot|rec|mic|codec)` 0 次。device 套件加上 v0.3.2 的 `test_mic_cold` 一共 10 个，只编译不跑，都编译、链接通过。

### 7.4 预算（13 §10.3）

| 项 | v0.3.1 | v0.4.0（本工作区） | 上限 | 状态 |
|---|---|---|---|---|
| 静态 RAM | 65,920 B | 65,968 B（+48 B） | 增加 ≤4 KB，总共 ≤96 KB | ✅ 主机 |
| app 镜像 | 945,312 B | 969,824 B（+24.5 KB；合入 v0.3.2 和 v0.4.1 以前是 961,504 B） | ≤2.5 MB（1.75 MiB 以上提示） | ✅ 主机 |
| loop 栈，静态链（`stack_budget.py`） | — | 新链：菜单按键 4,432 B，画菜单 4,832 B，画主页 2,448 B，画锁屏 1,824 B，锁屏时按键 1,440 B；全部链最深仍是 `outbox tick` 9,904 B | ≤12,288 B（16 KB 栈留 4 KB） | ✅ 主机 |
| `AppState` | 3,488 B | 3,488 B（`UiFlags` 放进了原来的填充字节；主机 `sizeof`，设备上一样） | ≤4,096 B | ✅ 主机 |
| loop 栈实测、空闲堆、最大块 | 5,788 B / 185,856 B / 163,828 B（§6.3） | 5,788 B / 182,944 B / 163,828 B（`test_runtime_budget`，副本里加了两键才跑通，§7.2） | ≥4,096 B / ≥153,600 B / ≥65,536 B | ✅ 真机（仓库里的测试待修） |

### 7.5 需要人做的检查（👤）

每一项做完以后把 `[ ]` 改成 `[x]`，填上日期、结果和你的名字。

- [ ] **needs user** · **V40-H-1 揣着锁屏的设备过一天**（13 §17、§19）
  - 怎么做：Lead 刷好正式版 v0.4.0，你自己的 cpad 照常运行。早上按 Opt+L 锁屏，放进口袋正常带一天。要记东西时照常用：先点一下 G0 再按住 G0 录便条；要解锁就按 Opt+L，再在 3 秒内按 Enter。晚上插上 Mac，看今天的每日笔记、`cpa rec ls`、`cpa status`。
  - 判定：**0 次你没想要的动作**（没想打开的页面、没想写的笔记、没想排的队、没想开的 session）；**0 段你没想录的便条或会议**。顺带记下：口袋里大概亮屏几次、有意录了几条便条、解锁失败几次。
  - 日期：＿＿ 误操作：＿＿ 误录：＿＿ 亮屏 / 便条 / 解锁失败：＿＿ 签名：＿＿
- [ ] **needs user** · **V40-H-2 通读会议确认框文案**（约 2 分钟，13 §7.4）
  - 怎么做：按 Opt+R，读屏幕上的确认框，然后按 ` 取消（不会开始录音）。v0.4.0 保留 v0.3.1 的原文：
    ```
    开始会议录音？
    请先告知在场的人
    Enter 开始    ` 取消
    ```
    （`firmware/app/lib/shell/rec_overlay.cpp` 的 `render_meeting_confirm`）。13 §7.4 提议的六行新文案（写明"告知并取得同意由你负责"）**没有采用**，因为你还没同意。
  - 判定：你读过，并决定保留原文，还是改用 §7.4 的文案。要改就另做一个改动，固件和测试一起改。
  - 日期：＿＿ 决定：＿＿ 签名：＿＿

## 8. v0.4.1：便条的 G0 宽限和点两下（gh #51，docs/20 U26）

### 8.1 怎么跑（测试固件，插卡）

```bash
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_memo,rec_memo_grace,rec_memo_handsfree --report "$TMPDIR/hil-g0.json"
```

| 场景 | 做什么、怎么判定 |
|---|---|
| `rec_memo` | 照旧按住 G0 4 s；停止时刻多算 0.6 s 宽限（`MEMO_GRACE_MS`），样本数按"按下 + 4 s + 0.6 s"核对 |
| `rec_memo_grace` | 按住 G0 2.5 s，松开 0.3 s（宽限内），再按住 2 s。判定：只有一个会话、一个 `seg-000.wav`；样本数和"第一个样本到第二次松开 + 0.6 s"相差不超过 4,096（松开的 0.3 s 没断）；卡清空 |
| `rec_memo_handsfree` | 点两下 G0（第二下在第一下松开 30 ms 后），松开 G0 等 5 s，再按一下 G0。判定：5 s 后还在录；一个会话、一个文件，长度 ≥ 4.5 s，样本数到"结束那一下按下"为止；之后点一下 G0 仍然打开记一条 |

注入的按键（`debug.key`）不经过设备的 G0 去抖，和以前不经过 M5Unified 的去抖一样，所以去抖和真按键的配合要人来试（下面 8.3）。

### 8.2 模拟器上的结果（2026-10-22，`tools/sim/run_hil_sim.py`）

- `rec_memo` 18/18、`rec_memo_grace` 10/10、`rec_memo_handsfree` 13/13 通过，和 `handshake`、`nav`、`menu`、`lock`、`capture`、`rec_meeting`、`rec_live_hold` 一起跑。数字是模拟器的，不是真机的。
- 并入 v0.4.1（`feat/ux-decisions`，和 docs/20 D1–D10 合在一起）后重跑：`handshake`、`nav`、`menu`、`lock`、`capture`、`board`、`spawn`、`cjk`、`back_keys`、`rec_memo`、`rec_meeting`、`rec_resume`、`rec_live_hold`、`rec_memo_grace`、`rec_memo_handsfree` 15/15 一次跑完（`rec_memo` 18/18、`rec_memo_grace` 10/10、`rec_memo_handsfree` 13/13）；`latency` 也通过（采集页 p95 400 ms，上限 650 ms）。

### 8.3 需要人做的检查（👤）

- [ ] **needs user** · **U26-1 点两下认不认得出来**（约 10 分钟，docs/20 U26 的去抖一节）
  - 怎么做：刷测试固件。在主页、项目看板、刚记完一条（"已保存"刚出来）三个地方，各点两下 G0 20 次，每次点完看屏幕：是"免提录音"就再按一下结束；是记一条就按 `` ` `` 关掉（v0.4.1 起 Fn+` 在编辑器里打出反引号，docs/20 D5）。
  - 判定：主页和看板 20 次里认出来 ≥ 19 次；刚记完一条时记下认出来几次（这时一轮循环最慢，两下间隔太短会并成一下）。没认出来的那次不应该误录（最多是记一条，或者一段"录音太短，已丢弃"）。
  - 再做 5 次慢的滑手：在主页按住 G0 说话，说到一半手松开约半秒（页面写"松开了 · 0.6 秒内按回继续"），再按回去接着说，最后松开。判定：每次都只有一段（`cpa rec ls` 里一条，长度是整段话），没有被切成两条。G0 的松开要隔 30 ms 两次读到才算（去抖），这个只有真按侧键才试得出来，`debug.key` 注入的按键不经过去抖（§8.1）。
  - 日期：＿＿ 主页 / 看板 / 刚记完：＿＿ / 20、＿＿ / 20、＿＿ / 20 慢滑手切成两条：＿＿ / 5 签名：＿＿
- [ ] **needs user** · **U26-2 按着说一天**（gh #51：便条说到一半断掉）
  - 怎么做：正式版 v0.4.1。一天里照常按住 G0 录便条，再用点两下录几条长一点的。
  - 判定：没有一条是说到一半断开、又接着新开一条的（`cpa rec ls` 里没有前后相隔不到 1 秒的两条）；免提的每一条都在你按 G0 的那一刻结束。
  - 日期：＿＿ 按住 / 免提条数：＿＿ 中途断开：＿＿ 签名：＿＿

### 8.4 真机：v0.4.1 测试固件（2026-10-22 14:50–15:36，agent 独占设备）

固件树是 `feat/ux-decisions` 的 `199b823`（D1–D10、主题、`fix/memo-hold`、`fix/level-meter` 都在里面，工作树干净），`kFwVersion` 是 `cpa-fw/0.4.1`。设备和卡同 §6.0，一直插着 USB。开始前 `cpa status --json`：卡上 0 段、不在拉取、`pending=0`；`cpa pause 7200`，`lsof` 空。录音都只在 HIL 的临时 `$CPA_HOME` 里，没碰 `~/CardputerNotes`；用户的 cpad 一直暂停到刷回正式版（条目数和开始前一样）。scratch 脚本、报告和日志在 agent 的 scratchpad（`dev041/`），不进仓库。

```bash
cpa pause 7200; lsof /dev/cu.usbmodem1101                       # 串口空
cd firmware/app
pio test -v -e cardputer-adv-test -f 'device/test_runtime_budget' -f 'device/test_rec_budget' -f 'device/test_mic_cold' -f 'device/test_rec_rotation'
pio run -e cardputer-adv-test -t upload && cd ../..
uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,nav,menu,lock,capture,board,back_keys,cjk,rec_memo,rec_meeting,rec_resume,rec_rotation,rec_memo_grace,rec_memo_handsfree,rec_live_hold --report "$SP/hil-main.json"
# scratch：ux_gates.py（UX-H2 的按键时序、D3 开始框、插着 USB 不锁）；debug.rec seg_ms 300000（rec_rotation 把段长留在 10 s，到重启为止）；
#          mixed_soak.py --minutes 15（integrate/v041 那次用过的混合长测，harness 换成本分支的）
# 临时探针固件（下面），scratch probe_checks.py：电平、UX-H5、模拟电池
(cd firmware/app && pio run -e cardputer-adv -t upload)              # 正式版
cpa pause 1 && cpa status
```

**临时探针固件**（只为这次，不进仓库）：scratchpad 里 `git archive 199b823` 的副本，6 个文件共加 50 行，全在 `CPA_TEST_HOOKS` 里：`debug.rec` 的 `seg_ms` 取 10001 时，runtime 读事件时丢掉下一个结束事件（Saved / Discarded / Failed，S1 的故障注入）；10002 / 10003 让 `power_tick` 当作没插 / 插着 USB（只改电源策略的这一个参数，链路照旧）；`debug.state.rec` 的健康字段换成电平（`ring_high` 是上一块高通后的 RMS，`mic_missing` 打包块数、q、有没有声音、Mando 的动作，和 `fix/level-meter` 那次的探针一样）；`diag.ghost_n`、`kb_reinit_n` 换成丢掉的次数和电池开关。这些都没有现成的钩子：正式的 `debug.*` 里没有丢事件和装电池的开关，电平也不在协议里。

- [x] **设备测试**：4 个套件 10/10（14:51–14:54）。
  - `test_runtime_budget` 2/2：`CPA_BUDGET stack_left=4844 heap_free=177376 heap_min=171400 max_block=155636 sent=36 pending=0 bad=1 store_errors=0 missing_glyphs=0 rec=1 audio_stack_left=3724 max_render_us=40616 max_tick_us=14081 meeting_us=21820`；8 KB RX 突发 0 错。最长一次重画 40.6 ms（上限 50），integrate/v041 那次是 40.5 ms。
  - `test_rec_budget` 3/3：`idle_free=272912 idle_block=262132 rec_free=272632 rec_block=262132 xfer_free=278556 audio_stack_left=2364 ring_high=1`，和 §7.2 只差两三百字节。
  - `test_rec_rotation` 1/1：95 s、30 s 一段，`segments=4 samples=1523712 wall_ms=95328 expect=1525248`（少 1,536 样本，96 ms，在两块以内），`ring_high=1 stack_left=2324`。
  - `test_mic_cold` 4/4：冷的那次 7 块（896 ms），warm 和 after_warm_up 0 块，`warm_up_ms` 491，和 §7.2 一样。
- [x] **HIL 15/15**（197 项，14:54–15:06，`hil-main.json`，boot `…0002R`，`fw=cpa-fw/0.4.1`、`store=sd`，`device_not_ready` 都是 0）
  - `handshake` 6/6（同一个 boot）、`capture` 5/5（0.103 s）、`board` 3/3（0.81 s）、`cjk` 7/7（0 缺字）、`back_keys` 8/8、`nav` 32/32（17 步，`card: true`）、`menu` 24/24、`lock` 12/12（seed 2004812512，200 个随机键没改任何东西）。
  - `rec_memo` 18/18：G0 按下到第一个样本 507 ms，3,968 ms，`lead_ms` 150，样本数比"按下 + 4 s + 0.6 s"少 2,000；拉取 483 KB/s。
  - `rec_meeting` 20/20：Enter 到第一个样本 10 ms；3 段，★ 误差 −150 / −70 ms；70,784 ms 里少 1,600 样本（100 ms）；拉取 475.6 KB/s（1 小时约 3.9 分钟，§7.2 是 256 KB/s）。
  - `rec_resume` 10/10：`.part` 322,920 B，续传偏移 **257,384 = 322,920 − 65,536**。`rec_live_hold` 16/16（拉取中开始录音，3 次 busy，之后补齐）。
  - `rec_rotation` 13/13：31 段 10 s，305,885 ms，少 1,488 样本（93 ms）；健康计数 `ring_high` 1、`mic_missing` 0、`rot_ms_max` 103 ms，录音中最低空闲堆 192,128 B；31 个文件 9.8 MB，454.7 KB/s。
  - `rec_memo_grace` 10/10：按住 2.5 s、松开 0.3 s、再按住 2 s，一个会话一个文件，4,864 ms，样本差 −464。`rec_memo_handsfree` 13/13：点两下、松开 5 s 还在录、再按一下结束，4,992 ms，样本差 −528，之后点一下仍打开记一条。
- [x] **UX-H2 的脚本版**（`ux-gates.json`，15:06–15:09）：按键用 `debug.key` 整批预先塞进设备（中间用单独按住的 Shift 当延时，不产生按键事件），所以间隔是设备上的毫秒数。Opt+L 都在 Enter 后 250 ms。真人手速还要你在 UX-H2 里试。

  | Enter 在 Opt+R 后 | 开始："Opt+R、Enter、Opt+L" | 结束："Opt+R、Enter" |
  |---|---|---|
  | 250 ms、350 ms（400 ms 防误触以内） | 没开始；Opt+L 取消开始框并锁屏，没留框、没录。再试一次，Enter 后 500 ms 再按一次 Enter：开始并锁上 | 结束框留着，在录；10,176 / 10,213 ms 后（从 Enter 算）回到会议页接着录；会议页上按 ` 和 Enter 都不起作用；再 Opt+R、600 ms、Enter 结束 |
  | 450、600、800 ms | 开始并锁上，框没了 | 一下就结束 |

  5 场会议都送到 Mac，`consent: ack`（快的两场各 15 s、2 段，是 `rec_rotation` 留下的 10 s 段长；慢的三场各约 4 s、1 段）。
- [x] **D3 开始框**（同一次）：不按键，15,156 ms 后自己关（从 Opt+R 算），没录，回到主页；框上按 `a`、按 `1`（等过防误触）：都是取消，留在主页，没打开编辑器、没打开项目页。
- [x] **D2 插着 USB 不自动锁**（同一次）：主页 75 s 不按键，没锁。
- [x] **UX-H5：结束事件丢了**（探针固件，`probe-checks.json`，15:31）：按住 3 s 的便条，丢掉它的结束事件。停止后 8 s 还在收尾（`live`），**10,146 ms** 回到空闲（5 s 再发停止、10 s 放弃，加一轮循环）；那时卡上还有 1 个文件，随后被拉走；丢掉的次数 1。接着按住 2.5 s 的下一条便条正常：停止后 101 ms 就空闲。两条都到了 Mac，卡最后清空。
- [x] **D2 用电池（模拟）**（探针固件，15:31–15:35）：电池开关打开后，最后一下按键（Opt+H）**60,948 ms** 后上锁（50 s 变暗、60 s 熄屏，加 1 s 的轮询）；锁着的黑屏上按住 G0 1.5 s 两次（间隔不到 1 s）都没录；解锁后点两下免提录 80 s，期间每 5 s 看一次，16 次都在录、都没锁（录音时只变暗）；按一下 G0 结束，**60,990 ms** 后又上锁（结束时屏幕像按了一下键一样亮起来）。这是模拟：拔掉 USB 揣兜的那天（UX-H1）还要你来。
- [x] **电平，安静的房间**（探针固件，15:30）：没人说话，点两下免提录 20 s（19,968 ms，156 块），每 40 ms 读一次 `debug.state`（380 次）。
  - 第 1 块按规定算安静；其余 155 块的 q 全是 0（波形上都是 3 px 的点，点的上限是 q 34），"有声音" 0 次，超过 kVoiceOn（q 71）0 块；Mando 363 次读到的都是 Tilt（歪头），**一次也没点头**（Listen 0）。
  - 房间的电平（高通以后，每块）：RMS 7–17，中位 11；**−73.4 到 −65.7 dBFS，中位 −69.5 dBFS**，第 95 百分位 −66.8。离"有声音"的保持线（−56 dBFS）还有 9.7 dB，离开始线（−50 dBFS）15.7 dB。WAV 原始（不高通，去掉开头 250 ms）−66.5 dBFS。
  - 拉回的 WAV 在主机上过固件自己的 `rec::level_of` 和 `theme::level_push`（scratch 的 `level_replay`）：156/156 块 RMS 和 q 都和设备上读到的一样。
  - 这次房间没有 `fix/level-meter` 那次的低频嗡声（那次原始 −52 到 −57 dBFS），所以这次只说明安静时是点、Mando 不点头；嗡声下的情况仍由 `test_rec_level` 的合成用例管。
- [ ] **15 分钟混合长测**（`mixed-soak.json`，15:09–15:25，seed 2608940095，13 轮，948 s）：62 项里 61 项过，**`mic_missing_0_every_recording` 没过**（下面）。
  - 过了的：39 条笔记每条正好一次；26 段录音（13 便条、13 会议）每段归档一次，每段 `stored` / `deleted` 各一次；一个 boot（`…0002R`），0 次重启，`up_ms` 单调；20 次端口关闭（7 次拉取中断开、13 次离线）以外 0 次 TX 卡住，`E_NOT_READY` 0；锁着随机按键 13/13 没作用；会议加 Tab 菜单加锁屏 13/13（每场正好 2 个 ★、没有项目）；离线时打的两条 13/13（hello 时 `pending` 2）；没锁时随机 40 键（不含 Enter、G0、Opt+D）后 13/13 回到主页，一条多余的采集都没有；结束时 outbox 和卡都空。
  - `debug.state.diag` 每 30 s 一次（32 次）：空闲时的空闲堆前三分之一平均 195,552 B、后三分之一 196,173 B（平的），开始 195,948、结束 198,852；最低 176,928 B，最大块最低 163,828 B；loop 栈余量 5,948 B、音频任务 2,152 B，从头到尾不变；loop 最长 108 ms（>50 ms 2 次、>100 ms 1 次、>500 ms 0 次；integrate/v041 那次最长 98 ms、>50 ms 2 次）；ghost 0，键盘重启 0。
  - **没过的那项**：13 段便条里 12 段录完后 `debug.state.rec.mic_missing` 是 1，会议都是 0。长测后在测试固件上另外查了（15:26–15:30，`missprobe*.json`）：计数在停止的那一刻变成 1（那时已经在收尾），和长测前面记不记笔记无关，只看按住多久：按住 3,000 ms 的另外 5 次 5 次都有；按住 2,500、3,032、3,064、3,096 ms 各 1 次、4,000 ms 3 次，加上免提、会议各 1 次，都没有；按住 3,000 到 3,096 ms 的，有没有这 1 次，录下的长度都是 3,072 ms，`ring_high` 一直是 1，所以没丢音。按源码推测是 `AudioTask::end_session` 关麦克风（`Mic.end()`）时 `MicRing::on_release` 那次重新挂缓冲失败也记成了 miss。长测的便条正好都按住 3,000 ms；integrate/v041 那次长测（同样按住 3,000 ms）还没有 0.6 秒宽限，停止落在别的时刻，所以是 0。要修的是计数（停止开始以后不再计数），录音本身不用改；正式固件不带这个计数。
  - 2026-10-22（`feat/memo-legend`）：**改了，等真机**。`MicRing::stop` 在 `Mic.end()` 之前标记"正在停"，采集任务回调里之后挂不上的缓冲既不算 `mic_missing` 也不再欠（`rec::rearm_missed`，native `test_rec_health` 先写的失败测试：停止以后的失败不算，停止以前的照算，按住 3,000 ms 那样停在块边上的一段报 0）。真机上要重跑：测试固件按住 3,000 ms 的探针 10 次以上都是 0，混合长测 `mic_missing_0_every_recording` 过。
- [x] **刷回正式版，cpad 恢复**（15:35）
  - 刷机：`pre_upload: skip: already paused`，RAM 66,784 B，app 1,046,144 B 写入 0x10000，"Hash of data verified"，"Hard resetting via RTS pin" 后正常启动。正式版 `firmware.bin` 的 SHA-256 是 `e919a34e…df674165`，`debug.(key|state|reboot|rec|mic|codec)` 都是 0 次（测试版各 1 次），版本串 `cpa-fw/0.4.1`。
  - `cpa pause 1` 之后 `cpa status`：`link: ready  dev=020000000001 fw=cpa-fw/0.4.1 pending=0 store=sd launcher=no`，`recordings: on_device=0 pulling=0 waiting=0 processing=0 blocked=0 failed=0`。
- **没跑的**：nosd 回归、`sd_store`、`rec_cold_start`（要 Mac 放音）、`latency`；设备测试 `test_mic_gain`、`test_rec_sd`、`test_sd_absent`、`test_sd_store`、`test_sd_wake`、`test_storage`；要人做的 UX-H1、H3、真人手速的 H2，§8.3 的 U26-1、U26-2。

## 9. v0.4.3「设置和英文」（docs/21）：真机

### 9.1 真机（2026-10-27 01:03–01:10，agent 独占设备）

固件树是 `feat/fw-english`（英文评审的修正之后），`kFwVersion` 仍是 `cpa-fw/0.4.1`。设备和卡同 §6.0，一直插着 USB，串口这次是 `/dev/cu.usbmodem101`。开始前 `cpa status --json`：卡上 0 段、不在拉取、`pending=0`，`fw=cpa-fw/0.4.1`、`store=sd`。没按任何键，没碰 G0。

```bash
cpa pause 7200; lsof /dev/cu.usbmodem101                       # 串口空
cd firmware/app
pio test -v -e cardputer-adv-test -f 'device/test_runtime_budget' -f 'device/test_storage'
pio run -e cardputer-adv-test -t upload && cd ../..
uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,capture,nav,menu,lock,lang,rec_memo,rec_meeting,rec_memo_handsfree --report "$SP/hil-main.json"
(cd firmware/app && pio run -e cardputer-adv -t upload)              # 正式版
cpa pause 1 && cpa status
```

- [x] **设备测试**：2 个套件 11/11（01:03–01:04）。
  - `test_runtime_budget` 2/2：`CPA_BUDGET stack_left=5020 heap_free=173636 heap_min=167656 max_block=155636 sent=54 pending=0 bad=1 store_errors=0 missing_glyphs=0 rec=1 audio_stack_left=3644 max_render_us=40944 max_tick_us=14166 meeting_us=23824 memo_us=23141 memo_tick_us=2183`；**英文第一次量**：`CPA_BUDGET_EN max_render_us=38160 max_tick_us=11850 meeting_us=22230 memo_us=21703 memo_tick_us=2121`，比中文还快一点，都在 50 ms 以内；8 KB RX 突发 0 错。空闲堆 173,636 B，比 §8.4 少 3.7 KB（设置页、Mac 名字和更多页面画过了），仍高于 150 KiB。
  - `test_storage` 9/9：新的 `test_settings_lang_round_trip_and_a_bad_byte` 打印 `CPA_NVS_LANG was=missing:0 en=1 bad_is_zh=1`：这台设备的 `cpa/lang` 之前没有（从没切过），存英文读回来是英文，写 7 读出来是中文，最后把键删掉还原成没有。LittleFS 保存 p95：acked 75.4 ms（达到 100 ms 目标），accumulated 147.0 ms（已知，§5.9 第 1 条）。
- [x] **HIL 9/9**（135 项，01:04–01:08，`hil-main.json`，boot `1833562-00033`，`fw=cpa-fw/0.4.1`、`store=sd`）
  - `handshake` 6/6、`capture` 5/5（0.101 s）、`nav` 32/32（17 步，`card: true`）、`menu` 24/24、`lock` 12/12（seed 944915827，200 个随机键没改任何东西），`device_not_ready` 都是 0。
  - **`lang` 5/5（新）**：开始是 `zh`；Opt+M . Enter 之后 `debug.state.ui.lang == "en"`；`debug.reboot` 后新 boot 还是 `en`；Opt+M . Enter 回到 `zh`，重启后还是 `zh`。`device_not_ready` 2 次，都是重启后 cpad 重新握手之前的那一下（模拟器上一样）。跑完夹具发 `debug.lang zh`（开始时的语言），ack。
  - `rec_memo` 18/18：按下到第一个样本 507 ms，3,968 ms，样本差 −2,000；拉取 484.4 KB/s。`rec_meeting` 20/20：Enter 到第一个样本 9 ms，3 段，★ 误差 −32 / −80 ms，70,784 ms 里少 1,696 样本；474.4 KB/s。`rec_memo_handsfree` 13/13：4,992 ms，样本差 −1,056。
- [x] **刷回正式版，cpad 恢复**（01:09）：`pre_upload: skip: already paused`，RAM 66,816 B，"Hash of data verified"。正式版 `firmware.bin` 的 SHA-256 是 `f12622f2…48a1933`，`debug.(key|state|reboot|rec|mic|codec|lang)` 0 次。`cpa pause 1` 之后 `cpa status`：`link: ready  dev=020000000001 fw=cpa-fw/0.4.1 pending=0 store=sd launcher=no`，`recordings: on_device=0 pulling=0 waiting=0 processing=0 blocked=0 failed=0`。设备停在中文。
- **没跑的**：nosd 回归、`back_keys`、`cjk`、`board`、`rec_resume`、`rec_rotation`、`rec_memo_grace`、`rec_live_hold`；在 Linux 上用 GCC 13 编译（本机只有 clang）；👤 docs/21 §9.7（拔电重启后还是英文、英文下录便条和开会、"退格"键帽上印的字）。

### 9.2 模拟器（同一天，`tools/sim/run_hil_sim.py --scenarios handshake,nav,lang`）

3/3 过；`lang` 5/5，和真机一样。

## 10. v0.5.0「English 或中文、Mac 上建项目、一行安装、小彩蛋」：真机

### 10.1 真机（2026-10-27 01:54–02:34，agent 独占设备）

固件树是 `release/v0.5.0` 的 `916e29b`（彩蛋和评审修正都在里面，工作树干净），`kFwVersion` 是 `cpa-fw/0.5.0`。设备和卡同 §6.0，一直插着 USB，串口 `/dev/cu.usbmodem101`。开始前 `cpa status --json`：卡上 0 段、不在拉取、`pending=0`，`fw=cpa-fw/0.4.1`、`store=sd`。没按任何键，没碰 G0。录音都只在 HIL 的临时 `$CPA_HOME` 里；用户的 cpad 一直暂停到刷回正式版（条目数和开始前一样）。scratch 脚本、报告和日志在 agent 的 scratchpad（`hil050/`），不进仓库。

```bash
cpa pause 7200; lsof /dev/cu.usbmodem101                       # 串口空
cd firmware/app
pio test -v -e cardputer-adv-test -f 'device/test_runtime_budget' -f 'device/test_storage'
pio run -e cardputer-adv-test -t upload && cd ../..
uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,nav,menu,lock,lang,capture,rec_memo,rec_meeting,rec_memo_grace,rec_memo_handsfree --report "$SP/hil-main.json"
# scratch：eggs_check.py（彩蛋，下面）；mixed_soak.py --minutes 15（§8.4 的混合长测）
(cd firmware/app && pio run -e cardputer-adv -t upload)              # 正式版
cpa pause 1 && cpa status
```

- [x] **设备测试**：2 个套件 12/12（01:54–01:55）。
  - `test_runtime_budget` 2/2：`CPA_BUDGET stack_left=5004 heap_free=173552 heap_min=167604 max_block=155636 sent=54 pending=0 bad=1 store_errors=0 missing_glyphs=0 rec=1 audio_stack_left=3728 max_render_us=40944 max_tick_us=14124 meeting_us=23933 memo_us=23143 memo_tick_us=2231`；`CPA_BUDGET_EN max_render_us=38300 max_tick_us=11830 meeting_us=22338 memo_us=21722 memo_tick_us=2148`。和 §9.1 只差几十微秒、几十字节：彩蛋没让重画变慢。8 KB RX 突发 0 错。
  - `test_storage` 10/10：`CPA_NVS_LANG was=0 en=1 bad_is_zh=1`（这台设备 §9.1 以后存过中文，所以 was=0）；LittleFS 保存 p95：acked 88.4 ms（达到 100 ms 目标），accumulated 123.0 ms（已知，§5.9 第 1 条）。
- [x] **HIL 10/10**（145 项，01:56–01:59，`hil-main.json`，boot `1833562-00038`，`fw=cpa-fw/0.5.0`、`store=sd`）
  - `handshake` 6/6（0.003 s，重开同一个 boot）、`capture` 5/5（0.102 s）、`nav` 32/32（17 步，`card: true`）、`menu` 24/24、`lock` 12/12（seed 2537295214，200 个随机键没改任何东西）、`lang` 5/5（zh → en → 重启还是 en → zh → 重启还是 zh；`device_not_ready` 2 次，同 §9.1）。其余场景 `device_not_ready` 都是 0。
  - `rec_memo` 18/18：按下到第一个样本 507 ms，3,968 ms，样本差 −2,000；拉取 479.3 KB/s。`rec_meeting` 20/20：Enter 到第一个样本 9 ms，3 段，★ 误差 −149 / −70 ms，70,784 ms 里少 1,536 样本；472.3 KB/s。`rec_memo_grace` 10/10：一个会话一个文件，4,864 ms，样本差 −464。`rec_memo_handsfree` 13/13：4,992 ms，样本差 −1,248，之后点一下仍打开记一条。
- [x] **彩蛋**（scratch `eggs_check.py`，`eggs.json`，17/17，02:00，28 s）：摸的次数只在 RAM 里，不在 `debug.state`，所以查的是看得见的：每次 Opt+W 之后页面、浮层、菜单、锁、outbox、录音（`harness.ui.changed`）都没变，Mac 那边没收到任何东西，设备收到了键（`key_ms` 变大）。
  - 主页先摸 1 下，再摸 109 下（一共 110，第 100 下是那句少见的话），每次之后 Opt+P 到看板、Opt+H 回主页都正常。
  - 锁屏：Opt+L 锁上，点一下 G0 点亮卡片，摸 5 下；卡片暗下去以后再摸 3 下：都还锁着，没录音；Opt+L Enter 解锁。
  - 看板上摸 3 下：什么都没发生（Opt+W 只在主页和亮着的锁屏卡上）。开会时摸 5 下：会议照录（`live`、`meeting`），页面没变，之后正常结束。
  - 切到 English 在主页摸 12 下，主页照常响应，再切回中文。
  - 一个 boot，0 次重启；`debug.state.diag` 前后：空闲堆 198,732 → 198,764 B，loop 最长 30 ms，>50 ms 0 次。
- [x] **15 分钟混合长测**（`mixed-soak.json`，02:17–02:33，seed 3575258321，13 轮，962 s）：**62/62 过**。
  - 39 条笔记每条正好一次；26 段录音（13 便条、13 会议）每段归档一次，每段 `stored` / `deleted` 各一次；一个 boot（`1833562-0003A`，从彩蛋检查到长测结束 36 分钟没重启），`up_ms` 单调；20 次端口关闭（拉取中断开和离线）以外 0 次 TX 卡住，`E_NOT_READY` 0；锁着随机按键、会议加 Tab 菜单加锁屏、离线打两条、没锁时随机 40 键后回主页，13/13 都过；结束时 outbox 和卡都空。
  - **`mic_missing_0_every_recording` 过了**：26 段录音录完后 `mic_missing` 都是 0（§8.4 那次 13 段便条里 12 段是 1；`feat/memo-legend` 的修正在真机上确认）。
  - `debug.state.diag` 每 30 s 一次（32 次）：空闲堆前三分之一平均 193,920 B、后三分之一 194,770 B（平的），开始 198,032、结束 198,732；最低 170,988 B，最大块最低 163,828 B；loop 栈余量 6,700 B、音频任务 2,184 B；loop 最长 106.5 ms（>50 ms 2 次、>100 ms 1 次、>500 ms 0 次；§8.4 是 108 ms、2 次、1 次）；ghost 0，键盘重启 0。
  - 第一次长测（02:00–02:16，seed 1262960464）62 项里 61 项过：`every_recording_archived_once` 数到 27 次归档、26 段录音。多出来的是彩蛋检查里那场会议：检查结束就关了 rig，它的文件还在卡上（长测第一次采样 `files_n=1`，那时长测还没录任何东西），长测的 cpad 把它拉走归档了。是测试顺序的问题，不是固件的；卡清空以后重跑就是上面那次 62/62。那次其余 61 项也都过，`mic_missing` 26/26 是 0。
- [x] **刷回正式版，cpad 恢复**（02:34）：`pre_upload: skip: already paused`，RAM 66,872 B，"Hash of data verified"，"Hard resetting via RTS pin" 后正常启动。正式版 `firmware.bin` 1,074,560 B，SHA-256 `8b87a16f…f05e3833`，`debug.(key|state|reboot|rec|mic|codec|lang)` 0 次，版本串 `cpa-fw/0.5.0`。`cpa pause 1` 之后 `cpa status`：`link: ready  dev=020000000001 fw=cpa-fw/0.5.0 pending=0 store=sd launcher=no`，`recordings: on_device=0 pulling=0 waiting=0 processing=0 blocked=0 failed=0`。设备停在中文。
- **没跑的**：nosd 回归、`back_keys`、`cjk`、`board`、`rec_resume`、`rec_rotation`、`rec_live_hold`、`latency`；彩蛋屏幕上实际的样子（模拟器截图看过，真机屏幕要人看）；按时钟的心情（真机时钟要到 23:30 以后、早上 5–7 点或周末才看得到，主机测试和模拟器覆盖）；"全处理完了"在真机上（要真的有 session 等你再清掉，fake-claude 能做但这次没排）；👤 docs/21 §9.7、v0.4.1 的 UX-H1、H3。

## 11. docs/28 S1a「留到对的 Mac」：今晚的真机计划（2026-10-27，待跑）

分支 `feat/keep-until-right-mac`（留底、再交一次、给这台 Mac、插上时的 5 秒提示、清掉留底，加两轮评审的修正）。白天设备不在手边，下面全是主机上证明过的（docs/28 §12 的清单）；这一节是今晚设备回来以后要跑的，**跑完才合并**。规矩同 §10.1：`cpa pause`、串口空、录音只进 HIL 的临时 `$CPA_HOME`，不碰 `~/CardputerNotes`、用户的 `$CPA_HOME`、`~/.claude`；scratch 脚本、报告和日志放 agent 的 scratchpad，不进仓库；最后刷回正式版、`cpa pause 1`。

开始前：`cpa status --json` 卡上 0 段、不在拉取；记下 `fw`。卡上已有的 `/cpa/rec/*/SENT.L` 要先看一眼（v0.5.0 的卡上没有）。

### 11.1 设备测试（`cardputer-adv-test`，插卡，自动）

```bash
cpa pause 7200; lsof /dev/cu.usbmodem101                       # 串口空
cd firmware/app
pio test -v -e cardputer-adv-test -f 'device/test_rec_ledger_sd' -f 'device/test_rec_sd' \
  -f 'device/test_rec_budget' -f 'device/test_runtime_budget' -f 'device/test_storage'
```

- [ ] **`test_rec_ledger_sd`（新，6 项）**：FatFs 上的账本。三次交出各开一次文件追加（`O_APPEND`），`SENT.L` 正好 3 × 28 B；下次开机扫描留底的不进索引、`kept=3`；手工在尾巴上追加 5 个字节（撕裂的写），下次开机整理成 `SENT.K`（GEN + 3 条，4 × 28 B）、`SENT.L` 没了、留底还是 3；再交一次排回 3 段且都不给 A；B 拿走后"清掉"删掉全部文件，账本只剩 GEN。记下每次开机的 `CPA_LEDGER scan_us=… kept=… waiting=…`。
- [ ] `test_rec_sd`、`test_rec_budget`：v0.3 的录音回归；**音频任务的栈余量**（`audio_stack_left`，§10.1 是 3,728 B）在账本命令之后不能低于 1 KB。`test_rec_budget` 还没有跑账本命令：要么今晚在它里面加一项（再交一次 + 清掉 200 段，读 `uxTaskGetStackHighWaterMark`），要么用 11.3 的 `debug.state.diag` 在两台 Mac 的步骤之后读一次。
- [ ] `test_runtime_budget`：`max_render_us` 中英文都要 < 50 ms；存储页（三行 + 三行事实）和提示栏是新画面，`test_runtime_budget` 只画到设置页、不翻到存储页也不出提示栏，今晚用 11.3 第 7 步之后的 `debug.state.diag`（loop 最长）看，> 50 ms 就记下来。
- [ ] `test_storage`：NVS 新键 `offb` / `offs`（离线标记）不影响 `cpa/lang`；空闲堆与 §10.1 相比少不了 1 KB 以上（`RecIndex` 每条多 4 B）。

### 11.2 HIL 回归（自动）

```bash
pio run -e cardputer-adv-test -t upload && cd ../..
uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,nav,menu,lock,lang,capture,rec_memo,rec_meeting,rec_memo_grace,rec_memo_handsfree --report "$SP/hil-main.json"
```

- [ ] 10/10。`rec_memo` / `rec_meeting` 拉完以后，HIL 的 cpad 收到 ack、归档一次；**设备上那几段现在是留底**（不再删），所以每个场景之后卡上的段会累积：`rec_*` 的检查里"卡上 0 段"要按 `files.n`（待传），不是按文件数。若有场景因此失败，先记下来，是测试的假设，不是固件的错。

### 11.3 两台 Mac（模拟，scratch `two_macs.py`，needs user 只在第 8、9 步）

一台 Mac 上开两个 HIL rig（`create_rig()` 两次 = 两个临时 `$CPA_HOME`，各自一个 `host_id`，就是两台 Mac），一次只让一个 rig 的 cpad 开串口；"拔线" = 停掉这个 rig 的 cpad 等设备掉线（15 s）。按键用 `debug.key`，看状态用 `debug.state`（`rec.files_n`、`ui`），屏幕上的字（"离线 2 条""留N"、存储页）由第 9 步的人看，或拍照。每步之后在**另一个**开机里核对（`debug.reboot`，再握手）：同一段不会同时在待传和留底，Mac 的归档里每个 `rec` 只有一次。

1. [ ] **A 拉走、留底**：rig A 握手，录 2 条便条，A 拉完。`files.n=0`，锁屏卡有"留2"，存储页"再交一次 ·XXXX 的 2 次"（XXXX = A 的 `host_id` CRC-32 前 4 位）。
2. [ ] **离线录、插 B、5 秒里什么都不按**：停 A，录 2 条，起 B。5 秒内 B 的 `file.list` 不列这 2 条（`files.n` 为 0）；5 秒后列出、`files.rev` 变，B 拉走。0 下。
3. [ ] **离线录、插 B、Tab Enter**：停 B，录 2 条，起 B，提示出来后 `debug.key tab`、`enter`。B 永远不列（再握手两次都 0）；停 B 起 A：A 拉走这 2 条。2 下。
4. [ ] **Tab Enter 后马上停 B**（评审 3，用户最常见的"按完就拔"）：同 3，但 Enter 之后 0.1 s 内停 B 的 cpad；再起 B：0 条。
5. [ ] **锁着插上**（评审 1）：Opt+L 锁上，停、录 1 条、起 B，等 30 s：B 的 `files.n` 是 0；Opt+L Enter 解锁，提示出来（5 秒重新算）；不按：5 秒后 B 拉走。再来一次，锁着等 2 分钟：B 拉走（最多扣 2 分钟）。
6. [ ] **Tab 菜单开着插上**（评审 2）：Tab 开菜单，停、录 1 条、起 B；Tab、Enter 是菜单的（提示不变成"不给这台 Mac？"），菜单关了以后提示的 5 秒才走。
7. [ ] **再交一次、给这台 Mac、清掉**：A 拉 2 条以后还连着 A：Opt+M `,` Enter（确认框，光标在"取消"）Enter：什么都没变；Enter `/` Enter：已排回，A 的 `files.n` 是 0、"给这台 Mac 2 段"；`.` Enter：A 的 `files.n` 回到 2，A 拉走。再拉一批，再交一次，停 A 起 B，B 拉走：两台的 manifest 里 `rec` 相同，A 不会第二次归档。最后存储页 `.` `.` Enter `/` Enter：清掉，锁屏卡没有"留N"。每步读一次 `debug.state.diag`（loop 最长、音频任务栈余量）。
8. [ ] **真掉电**（needs user：拨开关、拔线）：电池开关拨到关，只靠 USB 供电（这时拔线就是真的断电，不是软复位）。
   - 8a 交出时：A 拉一批 20 段，拉到一半时拔线；插回、开关不动、起 A。期望：拔线前已 ack 的段是留底；最后那一段要么留底，要么待传、A 再拉一次（去重：归档里还是一次，`test_a_segment_listed_again_is_deleted_unpulled_and_archived_once`）。
   - 8b 清掉时：留底 40 次录音（清掉每条命令 8 次录音），存储页清掉，确认后 0.5 s 内拔线；插回：没有一段是"有 `.W` 没 `.M`"，留底数只少不多，再清一次能清干净，不会有清掉的回到待传。
   - 8c 开机整理时：用 `debug.reboot` 前先让账本"脏"（8a 那样的拔线常常就会），插回后 1 s 内再拔一次（开机扫描正在整理）；再插回：`SENT.L` 和 `SENT.K` 不会同时留下，留底数和 8a 之后一样。
   - 每种至少 5 次，时机打散；每次之后都做"另一个开机里核对"。
9. [ ] **人看**（needs user）：中文、English 各一遍看提示栏（"离线 2 条 → 这台 · Tab 不给" / "2 offline go to this Mac · Tab: no"）、存储页三行（不重叠、中文三行事实之间有空隙）、两个确认框、锁屏卡的"留N"；插上握手后问候语还写"还没连上 Mac"吗（模拟器里会，评审说多半是模拟器没发 sessions，真机看一眼）。

### 11.4 刷回正式版

- [ ] `(cd firmware/app && pio run -e cardputer-adv -t upload)`，`debug.(key|state|reboot|rec|mic|codec|lang)` 0 次；`cpa pause 1`、`cpa status`：卡上的留底是 HIL 留下的测试录音，用存储页"清掉留底"清掉，`files.n=0`。

**今晚之后才合并**：11.1–11.3 全过，或者有失败但说清楚是测试假设（11.2）。8 的真掉电是 docs/28 §8 S1a 那一道"硬件掉电闸"。


## 12. v0.5.2「开机扫描的栈、暗屏少转、默认英文」：真机

### 12.1 真机（2026-11-01 21:58–22:40 PST，agent 独占设备）

设备和卡同 §6.0，一直插着 USB，串口 `/dev/cu.usbmodem101`，真正的 cpad 暂停着（`cpa pause`）。开始前 `cpa status --json`：link ready，`cpa-fw/0.5.0`，卡上 0 段。每个 PR 先在自己的 head 上测，测完再合并；#94 是把 main（#85、#87）合进去以后测的，所以它这几轮跑的就是 v0.5.2 的固件。

**#85（开机扫描的栈、看门狗），head `fe27443`：**
- [x] `device/test_rec_budget` **5/5**（3 分 9 秒）：`CPA_BOOT_SCAN waiting=100 indexed=102 audio_stack_left=1624`（闸 1,536；修之前 1,028）；`CPA_REC_LEDGER segs=200 recs=25 requeued=200 again_us=5730642 clear_cmds=4 clear_us=14184968 audio_stack_left=1624`：200 段再交一次、清掉，**没有任务看门狗中断**，串口一直在（2026-10-29 这个套件在看门狗上中断，设备掉了 USB）。
- [x] HIL（测试版）：`rec_memo` 18/18（便条起录 507 ms）、`rec_meeting` 20/20（462 KB/s）、`rec_resume` 10/10、`rec_reboot` 18/18（段中重启丢 44 ms）。
- [x] 复位折腾 (a)，拉取到一半 `debug.reboot`（临时场景：40 秒会议，一段约 1.3 MB，第一个 `file.get` 之后 0.4–2.6 s 重启）：**6 次真正在拉取中途的重启**，每次卡都挂上了，开机扫描把拉了一半的段重新索引，会议**只归档一次**、完整、设备 SHA 对得上、没有缺口，每段 stored 一次、deleted 一次，`files_n=0`。第 7 次重启时拉取已经结束，不算。

**#87（暗屏少转，docs/29 P1），head `df6895e`，然后 `2bccd9e`：**
- [x] `df6895e` 上的 `device/test_idle_yield`：一个断言失败，`dark_gap_max_us=24002 < 20000`。从不让出的亮屏循环本来就有 `lit_gap_max_us=20761`（一次整屏重画），所以这个绝对闸要的是固件从来没有过的东西。`2bccd9e` 改成检查让出加了多少（`dark ≤ lit + kIdleYieldMs + 2 ms`，docs/29 P1「按键采样最多晚 10 ms」）。
- [x] `2bccd9e` 上跑两次：**都过**，`CPA_IDLE lit_hz=14385 lit_gap_max_us=20773 dark_hz=98 dark_yields=296 dark_gap_max_us=25002 host_hz=15189`。暗屏每秒 98 轮（原来约 14,400）；亮屏和插着 Mac 时从不让出。
- [x] HIL（测试版，插着线，所以不让出）：`rec_memo` 18/18，便条起录 **507 ms**（没变）；`lock` 12/12。
- [ ] 👤 用电池，手动：黑屏以后点一下再按住 G0 照样录，开头不缺；Opt+L、Enter 一次解锁。
- 留给 P2 的发现：暗屏循环每 3 秒里有一轮约 15 ms，每次都一样：黑屏时还在做的活。

**#94（默认英文），和 main 合并后是 `aa0387f`：**
- [x] 先备份 NVS：`read_flash 0x9000 0x5000` 读两次，一样（从设备读的分区表确认 `nvs` 在 0x9000，大小 0x5000）。放在仓库外面。
- [x] 用了很久的设备保持英文：测试版刷在原来的 NVS 上（`cpa/lang` = 1）；HIL `lang` **5/5**（开始是 `en`；Opt+M `.` Enter → `zh`，`debug.reboot` 后还在；切回 `en`，也还在），`handshake` 6/6。
- [x] `device/test_storage` **11/11**：`CPA_NVS_LANG_MIGRATE wrote=1 zh=1:0 again=0 over_en=0 en=1`。
- [x] 擦掉的 NVS：先刷 app 测试版，再 `erase_region 0x9000 0x5000` 加硬复位，这样擦完后第一次开机的是 app（测试套件的 `nvs_boot_ticket()` 会让它变成中文）。第一次开机 `en`，两次 `debug.reboot` 后还是 `en`；开机号 `34X70FS-00001/2/3`（擦掉后换了新的开机号前缀）。
- [x] 新 NVS 上 HIL `--scenarios all`：**10/10**。
- [x] NVS 放回去：备份 `write_flash 0x9000`，哈希校验过；读回来只差下一次开机写的那一条（29 字节，开机计数和它的位图）。开机号 `1833562-00082/3/4`，又是它自己的计数，英文。

### 12.2 跑完以后

- [x] HIL 留下的留底在测试版上用设备自己的键清掉：Opt+M、`,`（存储）、`.` `.`（「清掉留底」）、Enter、`/`、Enter；语言没变。（`debug.state` 没有留底数：👤 锁屏卡上应该不再显示「留N」。）
- [x] 从 tag 的树刷 v0.5.2 正式版（`pio run -e cardputer-adv -t upload`）：app 分区和发布的 `cpa-v0.5.2-app.bin` 一个字节都不差；`cpa pause 1`，`cpa status`：link ready，`cpa-fw/0.5.2`，开机号 `1833562-00085`，等着的 0。这台 Mac 用公开的那一行升级（`… | bash -s -- --yes`）：`cpa` / `cpa-stt` / app 都是 0.5.2，cpad 和 cpa-stt 在跑，没有 "Broken pipe"。

### 12.3 模拟一台新电脑（2026-11-01 23:02–23:26 PST，公开仓库）

用临时目录里的一次性 HOME，从不碰真的那个。会碰到真实登录会话的东西都用桩代替：`launchctl` 和 `open` 只记日志，`pgrep` 说什么都没在跑，`osascript` 只跑安装脚本解析发布信息用的 JavaScript。
- [x] **用户路线**：README 那一行，匿名（没有 gh、没有 token），`--yes --no-service --firmware`：9 秒，退出码 0；`cpa` 和 `cpa-stt` 0.5.2；app 签名有效、没有隔离标记、0.5.2；`~/Downloads` 里的 `cpa-v0.5.2.bin` 和发布的 SHA-256 一致；PATH 只加了一行。`cpa setup check`、`cpa status`（没起 daemon）、`cpa project add` / `ls`、`cpa lang`、`cpa-stt --help`、`cpa mcp --help` 都能用。那个 HOME 起的 `cpa daemon` 能连上设备：link ready，`cpa-fw/0.5.2`。`uninstall.sh --yes --home` 删掉 `cpa`、app 和 PATH 那一行，留着 `CPA_HOME`。
- [x] **开发者路线**：在全新的 HOME 里照 README Quick start 做。uv 安装脚本、`uv tool install platformio==6.2.0`、匿名 `git clone`（main 在 `v0.5.2`）、`uv sync --project daemon`、`cpa --version` 0.5.2。`pio run -e cardputer-adv`（下载了工具链，173 秒）：和发布的 `cpa-v0.5.2-app.bin` 一样大，只差 64 个字节：app 描述里 ELF 的 SHA-256（0xb0–0xcf）、校验字节和镜像末尾的 SHA-256，所以设备上跑的东西完全一样。`pio test -e native` 1,219/1,219；daemon `pytest` 4,811 过（跳过 2 个）；协议工具、向量、repo 工具、文件大小；`swift build`。
- [x] 顺路发现并修好的：`cpa setup check` 和 README 第 4 步还说没同步的录音会永久删掉（#100）；`uninstall.sh --home` 和别的 HOME 里跑的 `cpa service` 会动用户真的 LaunchAgent（#101；`uninstall.sh` 在 main 上已经修好，`cpa` / `cpa-stt` 跟下一版发）。用一个报告服务是从别的 HOME 加载的桩再跑一次：`uninstall.sh` 打出提示，没动它。
