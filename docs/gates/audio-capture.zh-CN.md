[English](audio-capture.md) · **中文**

# 录音采集与传输：设备端设计输入（v0.3，没接设备）

> 2026-10-20 · 角色：设备端采集与传输。**本文只做设计输入**：没有接设备、没有刷机、没有打开串口；所有"实测"数字都引用已有的闸门报告和抓包文件。
> 依据：[sd-card.md](sd-card.zh-CN.md) §5–§7（60 s 持续录音、文件拉取 138 KB/s、录音时 16/59 个回复被扣）、[09-m2-design.md](../09-m2-design.zh-CN.md) §3、§5、§6、§12，[ADR-0001](../adr/0001-toolchain.zh-CN.md)，05 §5.8、§6，03 §3，04 §5.2、§7.7、§8.2；代码 `firmware/app`（`usb_transport`、`chord_detector`、`keymap`）、`firmware/hwtest/src/sd_rec.cpp`、`daemon/src/cpad/device/{link,credit,framing}.py`；Arduino core 2.0.17 的 `HWCDC.cpp`、M5Unified 0.2.26（`.pio/libdeps`）、SDK 的 `sdkconfig.h` / `ffconf.h`；WhisperWoof `767ae8d`（`gh api` 只读，没有执行）；M5Stack 官方 Cardputer-Adv 页面；Espressif USB FAQ。
> 标记：**[测]** 真机实测（引用已有报告）· **[源]** 源码或官方文档 · **[算]** 推算 · **[待]** 需要设备闸门（§9）。
> 单位：传输速率的 KB = 1024 B（和 sd-card.md 一致）；录音数据量用十进制 MB（1 小时 = 115.2 MB）。
> 范围：设备端的录音交互、格式、预算、链路、传输协议，以及 daemon 的落地点。Mac app 和 WhisperWoof 之间的接口契约不在本文，§8.2 只给输入。

## 0. 一页摘要

1. **手势（建议，待拍板）**：G0 长按 500 ms 时响"开始"提示音并开始录音，**松手后继续录**；再按一下 G0 结束，响"已保存"。"按住说话"作为备选（§2.1）。
2. **录音中**：空格 = 标记，Opt+1–9 = 设项目，Opt+0 = 清除。录音中扬声器不能响（麦克风和扬声器共用 BCK/WS 引脚，只能半双工），标记和警告只用屏幕和 RGB LED。
3. **不做 04 §7.7 的"按下前预录"**：预录要求按下时麦克风已在录，而开始提示音要用同一组 I2S 引脚。用户要的是长按有声音反馈，所以选提示音，改成"听到提示音再说"。从按下到开始录约 0.7–0.9 s [算/待]。
4. **格式**：16 kHz、16 bit、单声道 PCM WAV（44 B 标准头），5 分钟一段，段与段**无缝**。daemon 按会话拼接，WhisperWoof 自己在停顿处重新切，设备不做 VAD。
5. **SD 布局**沿用 09 §12：`/cpa/rec/<E7>/<B5><S前3>.W<S末1>`。**新增**同名 `.M<S末1>` 元数据文件作为"提交"标记，不用 `.part`，不用 rename。断电后在设备上补头、补元数据（§3.4）。
6. **校验**：录音时增量算 PCM 数据的 SHA-256（硬件加速），写进 `.M`；传输每块带 CRC32；Mac 端核对 SHA-256 之后才发删除。
7. **线程**：新增一个音频任务（core 0，优先级 1，低于 mic_task 的 2），独占录音写卡、提示音切换和传输读卡。loop 不再碰录音 I/O。sd-card §7 那次"27% 回复被扣"测的是 hwtest 的弱 kick；app 的 transport 每 20 ms 重新打开 IN_EMPTY，v0.1 HIL 浸泡 0 次卡住。预计这个问题大部分会消失 [待 G-A2]。
8. **内存**：新增约 53 KB（录音环形缓冲 6×4 KB 是大头）。只在 SD 为主时于开机一次性从堆上分配，静态 RAM 预算不动 [算/待 G-A6]。
9. **电池**：按官方 1500 mAh、120 mA 估，录音约 9–12 小时 [算/待 G-A7]。
10. **传输**：**主机拉取**（`file.list` / `file.get` / `file.del`）。每条主机请求恰好一个回复，现有的 credit 窗口和空行 nudge 直接适用；按偏移续传；Mac 确认后设备才删。不用 outbox 的 `kind:"rec"`（取代 09 §6、§12 的预留，理由见 §7.1）。
11. **吞吐**：从 sd-card §7 反推，HWCDC 线速约 185–225 KB/s [算]。base64 放进 2048 B 的行，有效率约 69%；二进制子模式约 96%。1 小时录音：b64 约 12–15 分钟，二进制约 9–11 分钟；如果线速能到 500 KB/s，分别约 5.4 和 3.9 分钟。**先跑 G-A1 再决定要不要做二进制**（§7.3）。
12. **协议全部是新增**：新消息类型，加上 `hello` / `pong` 里可选的 `files` 对象，`v` 仍为 0。旧 daemon 不会来拉；旧设备收到会回 `E_UNKNOWN_TYPE`。
13. **daemon 落地**：先写 `$CPA_HOME/rec-staging/…part`，校验后原子改名到 `~/CardputerNotes/recordings/<日期>/<会话>/NNN.wav`，旁边放 `session.json`（字段对齐 WhisperWoof 会议设计的 manifest），再交给 WhisperWoof（契约另定）。
14. **闸门** G-A1–G-A12（§9）。吞吐、录音与同步并发、提示音时序、内存可以由独占设备的 agent 跑；电池、断电、音质、手感需要人。

## 1. 输入事实

| # | 事实 | 来源 | 对设计的影响 |
|---|---|---|---|
| F1 | 60 s 持续录音，4×4 KB 环形缓冲：丢帧 0，单次写最长 45 ms，fsync 最长 78 ms，环形缓冲最多只用到 1/4 | [测] sd-card §7.2 | 稳态 16 KB 够用；段切换另算（§3.3） |
| F2 | hwtest 的写卡在 loop 里，USB 只做 `(bool)Serial` kick：59 个 ping 中 16 个被扣约 1 s，p95 1037.9 ms | [测] sd-card §7.2；`firmware/hwtest/captures/sd-rec3-20261020-114610.json` | 不能照搬 hwtest 的结构 |
| F3 | app 的 `UsbTransport::tick` 每 20 ms 检查一次，TX 环非空就 flush FIFO 并重新打开 IN_EMPTY（`rearm_tx`）；v0.1 HIL 浸泡 2028 次请求 0 次卡住，p95 22 ms | [源] `firmware/app/lib/usb_transport/usb_transport.cpp:19,56`；[测] 07 §2 第 11 项 | 只要 loop 每 ≤20 ms 转一圈，被扣的回复最多等一圈 |
| F4 | 拉文件：每块 12 KB，base64 加 CRC32，逐块请求，1,916,972 B 用 13.6 s（138 KB/s），0 重传 | [测] sd-card §7.2；`sd_rec.cpp` 的 `cmdGet` | 1 小时录音要 13.6 分钟 |
| F5 | HWCDC 发送：每次 IN_EMPTY 中断最多从环形缓冲取 64 B 填进 FIFO。`write()` 空间不够时先写一部分，再带 `delay(1)` 循环等剩下的；`tx_timeout_ms=0` 时拿不到锁就返回 0 | [源] core 2.0.17 `HWCDC.cpp:117-139`、`:410-468` | 线速受中断和 64 B 包限制；整行或整帧放得下才写，并且只有一个写者 |
| F6 | HWCDC 接收：在中断里逐字节 `xQueueSendFromISR` | [源] `HWCDC.cpp:151-160` | 主机到设备方向只发小请求 |
| F7 | Espressif FAQ：ESP 的 USB 受 FIFO 大小限制，目前最高约 500 KB/s（说的是 OTG 摄像头场景）。S3 的 USB-Serial-JTAG 和 OTG 同一时刻只能用一个，用 OTG 就没有自动下载 | [源] Espressif USB FAQ | 线速上限按约 500 KB/s 估；U 盘模式（OTG）不作主路径 |
| F8 | ADV 的麦克风是 I2S0（BCK 41、WS 43、DIN 46），扬声器是 I2S1（BCK 41、WS 43、DOUT 42），共用时钟引脚 | [源] `M5Unified.inl:2758-2765`、`:3105-3115`（0.2.26） | 半双工：录音中不能出声；提示音只能在开始前、结束后响 |
| F9 | `Mic.begin()` 之后前 100–200 ms 有 codec 重启冲击；开始录音后第一个缓冲延迟 33–35 ms | [测] ADR-0001 §3；sd-card §7.2 | 记 `lead_ms`，由 Mac 裁掉 |
| F10 | M5Unified 的麦克风默认：16 kHz，`over_sampling` 1，`magnification` 16，DMA 8×128 帧，mic_task 优先级 2、不绑核、栈 2048+512 B。扬声器 DMA 8×256 帧。（2026-10-21 更正：原来写的 `over_sampling` 2 是 `Mic_Class.hpp` 里结构体的默认值，但 `M5.begin()` 给所有内置麦克风改成了 1。增益是 `magnification / (over_sampling × 2)`，乘在 16 位样本之后，所以 v0.3.0 是 16/2 = ×8，约 +18 dB；用户的两段录音每个样本都是 8 的倍数，印证了这一点。v0.3.1 起 `MicRing::start` 把 `over_sampling` 钉在 1） | [源] `Mic_Class.hpp:58-93`、`Mic_Class.inl:619-620`、`Mic_Class.inl:967`、`M5Unified.inl:2625`、`Speaker_Class.hpp:66-73`（0.2.26）；[测] 2026-10-21 的两段录音 | 写卡任务优先级必须低于 2；麦克风增益应该放在 codec 里（PGA、ADC 音量），见 docs/releases/v0.3.1.md |
| F11 | 安静房间 RMS 约 6；Mac 外放 1 kHz 测试音 RMS 43–74、峰值 144（满量程 32767） | [测] sd-card §7.2 | 电平偏低；人说话的电平还没测（G-A10） |
| F12 | SD 20 MHz：读 1619–1714 KB/s，写 1363–1634 KB/s；8.3 平铺目录保存 p95 29 ms、最长 102 ms | [测] sd-card §5、§6 | SD 不是传输瓶颈；段切换约 300 ms 以内 |
| F13 | FatFs：`FF_FS_REENTRANT 1`（卷锁，超时 10 s）；每个打开的文件自带一个 4 KB 扇区缓冲；长文件名缓冲在栈上。mbedtls 硬件 SHA 已打开；ROM 里有 `esp_rom_crc32_le` | [源] `ffconf.h:216,248,287`；`sdkconfig.h:434-439,568`（`qio_qspi`）；`esp_rom_crc.h:70` | 两个任务可以同时用卷；第 3 个文件名额 +4 KB；CRC 不占代码 |
| F14 | G0 按住满 500 ms 立即发 `G0Long`；长按之后松手**不发**任何事件 | [源] `chord_detector.cpp:61-79`；`keymap.cpp:44`、`shell.cpp:111` | "按住说话"要新增松手事件；"长按开始、再按停止"不用改输入层 |
| F15 | 静态 RAM 63,444 B，预算 96 KB | [测] 本地 `scripts/check_budget.py .pio/build/cardputer-adv`（2026-10-20 14:47 的 build） | 新缓冲如果放静态，会顶到预算 |
| F16 | 官方：电池 1500 mAh（2026-07-13 从 1750 mAh 改）；4.2 V 下工作电流 120.2 mA；有 RGB LED（电源由 GPIO38 控制，M5Unified 表里数据脚是 GPIO21）；功放 NS4150B，codec ES8311 | [源] docs.m5stack.com Cardputer-Adv；`M5Unified.inl:259` | 电池估算；LED 做录音指示 |
| F17 | daemon 只在"主机请求有没回复的 credit"时，每 500 ms 补写一个空行 | [源] `daemon/src/cpad/device/link.py:281` `_maybe_nudge` | 传输用主机请求，被扣的回复自动有人推 |
| F18 | WhisperWoof 会议设计：16 kHz，2–5 分钟片段在停顿处切，每个会议一份 manifest（片段序号、墙钟开始、样本数、状态）。中英混合测试：SenseVoice MER 15.6%，whisper-large-v3-turbo 25.8%。文件导入接受 `.wav`，单个 ≤500 MB | [源] WhisperWoof@767ae8d `docs/design/local-meeting-transcription.md` §3、§3.1、§4.1；`src/whisperwoof/bridge/file-import.js:17-18` | 设备不用按停顿切段；交接单位是"片段 + manifest" |

## 2. 录音交互

### 2.1 开始与结束：两种手势

- **A：长按开始，松手继续录（锁定）；再按 G0 结束。**
- **B：按住说话，松手结束；按住时按 Enter 锁定。**

| | A（建议） | B |
|---|---|---|
| 1 分钟以上（会议、长段思考） | 不用一直按着 | 要先锁定，否则手指累 |
| 10 秒以内的备忘 | 两个动作（长按，再按一下） | 一个动作 |
| 口袋里误触 | 误开后会一直录，要靠提示音、LED、电量停止和静音自动停（开放问题）兜底 | 松手就停 |
| 输入层改动 | 不用改：`G0Long` 开始，`G0Tap` 或 `G0Long` 结束 | 要在 chord_detector 里新增 `G0Up`（长按后的松手事件） |
| 和 G0 短按"文字速记"的关系 | 不冲突 | 不冲突 |

**建议 A**：产品目标是 Granola 式的长录音，用户又说打字累、希望少动手；短备忘多按一下的代价很小。B 以后可以做成设置项。

### 2.2 录音时的按键

录音是**后台状态**：开始后显示"录音页"；在录音页按 `` ` `` 回到原来的页面，录音继续，状态栏显示红点和时长。

| 按键 | 录音页 | 其他页面 |
|---|---|---|
| G0（短按或长按） | 结束并保存 | 同左。录音期间 G0 的含义从"提交 / 速记"让给"停止"，Compose 里提交改用 Enter |
| 空格 | 标记：每段最多 16 个，超出的只计数不记录；屏幕和 LED 各闪一下 | 正常输入 |
| Opt+1–9 / Opt+0 | 设项目 / 清除；录音页上显示项目 key | 保持原有含义（打开项目页） |
| `` ` `` | 回到上一页，录音继续 | — |
| Fn+`` ` `` | 丢弃这段录音（Confirm，400 ms 防误触）——开放问题 | — |

- **默认项目**：开始录音时所在的项目页，和 Compose 的默认目标是同一条规则（`UiContext`）。
- **屏幕灭着时**（用电池，60 s 无操作）：任意键只点亮屏幕；G0 例外，直接结束录音（和 v0.1 的"G0 例外"一致）。

### 2.3 提示音与 I2S 切换

| 事件 | 声音 | 时机 |
|---|---|---|
| 长按满 500 ms | 上行两音：880 Hz → 1320 Hz，各 60 ms | 此时麦克风还没开 |
| 保存成功 | 下行两音：1320 Hz → 880 Hz | `.M` 提交之后，约在停止后 0.1–0.3 s [算] |
| 拒绝或失败 | 330 Hz 三声，各 80 ms | 没有卡、卡满、电量低、存储错误、未同步段数到上限 |
| 标记、电量警告 | 不出声 | 录音中扬声器不能用（F8） |

**开始路径**：`G0Long` → loop 投递 `Start` → 音频任务依次 `Speaker.begin()` → 播两音 → 等 `isPlaying()` 变假 → `Speaker.end()` → `Mic.begin()` → 第一块缓冲。
- 从按下到开始录约 0.7–0.9 s [算]：500 ms 长按门限 + 约 150 ms 提示音 + 切换耗时 [待 G-A5] + 第一块 35 ms [测]。
- 开头 100–200 ms 是 codec 冲击（F9）。设备上不裁，在第 0 段的 `.M` 里记 `lead_ms`，由 Mac 裁掉。

**结束路径**：`Mic.end()` → 收尾（§3.3）→ `Speaker.begin()` → 下行两音 → `Speaker.end()`。

**其他规则**：
- 空闲时麦克风和扬声器都关（`end()`），省电，功放也不会有底噪。
- 这些调用（装 I2S 驱动、写 ES8311 寄存器）都放在音频任务里，loop 不阻塞。
- [待] ES8311 和键盘的 TCA8418 都在 In_I2C（GPIO 8/9）上，M5Unified 的 I2C 类里没看到锁：音频任务配置 codec 时，loop 可能正在读键盘。G-A5 要验证；不安全就加一把共享锁，或者把 begin/end 挪回 loop（代价是 loop 被阻塞一次，时长 [待]）。
- 音量默认中等，由用户耳听确定 [待]。"会议静音"（只用 LED 和屏幕）列为开放问题。（2026-10-21 改为：录音全程不出声、只供个人使用，v0.3.1 起录音全程不出声；会议确认框保留；屏幕是唯一提示。音量不用再定。）

### 2.4 屏幕与 LED

```
● 录音 12:31                #cpa  ★2
▁▂▅▇▅▂▁▁▂▃                         电平，每 128 ms 一格
第 3 段 · 待同步 5 段 · 电 78%
G0 结束  空格 标记  ⌥1-9 项目  ` 返回
```

- 电平条用音频任务对每块缓冲顺手算出的 RMS（几乎不花时间），让用户确认麦克风真的在工作。
- RGB LED：录音时红色低亮度慢闪，标记时闪一下白色 [待 G-A12 看观感]。屏幕灭了也看得见。
- 状态栏：录音时显示 `●12:31`；有待同步的录音时显示 `录5`。

### 2.5 拒绝与自动停止

| 情况 | 动作 |
|---|---|
| 没插 SD 卡，或主存储不是 SD | 拒绝：失败提示音 + "没有 SD 卡，不能录音"（04 §7.7） |
| 开始时剩余空间不足 8 MB（约 4 分钟） | 拒绝。不足 256 MB 时照常开始，但提示"剩余约 N 小时"。剩余空间来自开机时读一次 FSINFO，读不到就显示"未知"（09 §12） |
| 写入返回 ENOSPC | 正常收尾本段（补头、写 `.M`），失败提示音，"SD 卡已满" |
| SD 锁死（EIO，09 §3.8） | 立即停止；本段留在卡上没有提交，下次开机恢复（§3.4）；失败提示音 |
| 电压 ≤3.45 V 持续 10 s [待 G-A7] | 正常收尾 + 失败提示音。≤3.55 V 时只在屏幕上警告 |
| 未同步段数到 256（5 分钟一段约 21 小时） | 拒绝新录音："请先连 Mac 同步" |
| 重启 | 录音结束，本段按断电处理 |

不设最长时长。静音自动停（防口袋误触）列为开放问题。

## 3. 录音格式与 SD 布局

### 3.1 格式与分段

- 16 kHz、16 bit、单声道 PCM，44 B 标准 WAV 头。数据率 32,000 B/s；5 分钟一段是 9,600,000 B 数据加 44 B 头；1 小时 115.2 MB。
- **段长 5 分钟**（04 §7.7）。段与段之间无缝：同一个环形缓冲接着往新文件写，不丢样本。
- daemon 按会话把各段拼成连续音频；WhisperWoof 在停顿处自己切。它的测试表明，固定切点对流式模型没有影响；离线模型它本来就会再切成 ≤20 s 的小块 [源 F18]。所以设备不做 VAD，也不在停顿处切。
- 段长只影响文件数，以及"边录边同步"时转写的新鲜度，**不影响断电损失**：每秒 fsync 一次，断电最多丢约 1 s。段长做成编译期配置，1–10 分钟。
- **为什么留 WAV 头，不用裸 PCM**：卡插到电脑上（或走 Launcher 的 U 盘模式，§7.7）可以直接播放。代价是头要在结束时回填，SHA-256 只能覆盖数据部分（§3.5）。

### 3.2 布局与命名（在 v0.2 布局之下）

```
/cpa/rec/<E7>/<B5><S前3>.W<S末1>   一段音频（WAV）
/cpa/rec/<E7>/<B5><S前3>.M<S末1>   这一段的元数据（JSON，≤512 B）；存在并且能解析 = 已提交
```

- 每段一个 MonoId；**会话 id = 第一段的 MonoId**。和 outbox 一样按 epoch 平铺、用 8.3 名字（09 §3.5）。
- 未同步上限 256 段，所以一个 epoch 目录最多约 512 项（16 KB 目录项，半个簇），建文件时扫目录仍在几十毫秒以内 [算]。
- MonoId 的序号 S 现在由 loop（outbox）和音频任务（录音）共同取号，要改成原子计数。

`.M` 的内容（例子：会话的第 2 段）：

```json
{"v":1,"id":"K3M9Q2T-00042-0A1D","rec":"K3M9Q2T-00042-0A1C","seg":1,"last":false,"next":"K3M9Q2T-00042-0A1E","bytes":9600000,"sha256":"<64 位十六进制>","at":{"up_ms":735000,"wall_ms":1792440345000},"project":"cpa","marks":[12800,201344],"fw":"cpa-fw/0.3.0"}
```

| 字段 | 含义 |
|---|---|
| `bytes` | PCM 数据字节数（不含 44 B 头），是偶数 |
| `sha256` | 数据部分（文件偏移 44 到末尾）的 SHA-256。恢复出来的段没有这个字段 |
| `next` | 下一段的 id。换段时先取号、再写本段的 `.M`；断电时靠它把没提交的下一段接回会话 |
| `marks` | 段内的毫秒偏移，最多 16 个 |
| `lead_ms` | 只出现在第 0 段：开头 codec 冲击的长度，由 Mac 裁掉 [待 G-A5 定值] |
| `at` | 本段第一个样本时的 `up_ms`；墙钟已知时带 `wall_ms`，否则为 null |
| `rcv` | true 表示这一段是开机时恢复出来的 |

**墙钟**：设备没有 RTC。离线开机后 `wall_ms` 为 null；只要同一次开机后来连上过 Mac，daemon 就能用 boot id 加 `up_ms` 反推（§8.1）。

### 3.3 写入流程（音频任务）

1. **开段**：取 id → `open(.W, O_CREAT|O_TRUNC)` → 写 44 B 占位头（长度字段为 0）→ 初始化 SHA。
2. **写入**：环形缓冲满一块（4 KB，128 ms）就写一块，同时更新 SHA；每 1 s `fsync` 一次。这就是 sd-card §7 的做法，0 丢帧 [测]。
3. **换段**（满 5 分钟）：取下一段的 id → 回填本段头（`lseek 0` 加写 44 B）→ `fsync` + `close` → 写本段的 `.M`（09 §3.6 的 FAT 提交协议：直接写最终文件名 → `fsync` → `close`）→ 开下一段。
   - 整个过程同一时间只开一个录音文件，环形缓冲在这期间继续收样本。
   - 最坏耗时 ≈ fsync 78 ms + 写停顿 65 ms + 写 `.M` 102 ms + 建新文件 45 ms ≈ 290 ms [算]。6 块缓冲能扛 640 ms，4 块只有 384 ms，余量太薄，所以用 6 块。
4. **结束**：同第 3 步，但 `last:true`，不开下一段；然后响"已保存"。

不用 `.part` 加 rename：FAT 的 rename 不是原子的（sd-card §6.3），09 §12 已经定了。**`.M` 就是提交标记。**

### 3.4 断电恢复（取代 04 §7.7 的 `.part`）

开机后由音频任务在后台扫描 `/cpa/rec/*/`，只列目录项的名字和大小，不读文件内容：

| `.W` | `.M` | 动作 |
|---|---|---|
| 有，大小 = 44 + `bytes` | 能解析 | 已提交，加入待同步索引 |
| 没有 | 有 | 删除操作做到一半的残留（删除顺序是先 `.W` 后 `.M`）：删掉 `.M` |
| 有 | 没有、长度为 0 或解析失败 | **恢复**：`.W` 不到 44 B 就删掉；否则 data =（大小 − 44）向下取偶数 → 回填头 → fsync → 从前一段 `.M` 的 `next` 找回 `rec` 和 `seg`（找不到就自成一个会话，seg 为 0）→ 写新的 `.M`（`rcv:true`，没有 `sha256`，`last:true`） |
| 有，大小 ≠ 44 + `bytes` | 能解析 | 不改文件；按 `.W` 的实际大小上报，去掉 `sha256`，标 `rcv:true`（理论上不会出现） |

- 恢复只做一次 44 B 的小写和一个 `.M`，不读整段，不拖慢开机。扫描期间来的录音请求排队等待，约 1–2 s [算]。
- **损失**：最后一次 fsync 之后的音频（≤1 s）。已经分配、但还没记进目录项的簇会变成丢失簇，每次断电约 1–2 个 32 KB 的簇 [算]，可以用 Mac 的"磁盘工具 → 急救"回收（09 §3.6）。
- **回填头时断电**：文件的第一个扇区可能写坏，最多影响头和开头约 15 ms 的音频；下次开机会再回填一次 [算]。
- 这套规则写成纯函数 `plan_rec_recovery(entries) -> actions`（`lib/rec`），在 native 下把每种组合都测到。

### 3.5 校验

- **录音时增量算 SHA-256**（mbedtls，硬件 SHA 已打开 [源 F13]），32 KB/s 的数据量，开销可以忽略 [算]。它只覆盖数据部分，因为头是最后才回填的。头完全由数据长度决定，Mac 端按 `bytes` 重建，再逐字节比较。
- **传输时每块带 CRC32**（ROM 里的 `esp_rom_crc32_le`）：只保护这一块的内容。SHA-256 负责端到端，包括 SD 上的存储，以及偏移拼接有没有出错。
- **恢复出来的段没有 SHA-256**：靠 SD 驱动每块的 CRC16（09 §3.9）加传输的 CRC32；daemon 把它标成 `rcv`。

## 4. 任务、内存与栈

### 4.1 线程模型

```
loop（core 1，优先级 1）   按键、界面、链路、outbox、USB 发送（唯一写 Serial 的地方）
   │ 命令队列：Start{project} / Stop / Mark / Tag / Discard / FileReq{list | get | del}
   ▼
音频任务 rec_task（core 0，优先级 1，栈 6 KB）
   提示音、Mic / Speaker 切换、环形缓冲落盘、SHA、换段、开机恢复扫描、传输读卡
   ▲ 环形缓冲（由 mic_task 的 release 回调补缓冲，和 sd-card §7 一样）
mic_task（M5Unified，优先级 2，不绑核）
```

- **为什么要单独的任务**：sd-card §7 里写卡在 loop 中，loop 会被卡 45–78 ms，回复被扣，单步也超过 50 ms 的规则。挪出去以后，loop 只在 outbox 保存时才碰 SD。
- **优先级必须低于 mic_task**：Arduino 的 SD 驱动用 CPU 轮询 SPI。如果音频任务优先级更高，又和 mic_task 落在同一个核上，78 ms 的 fsync 期间 mic_task 跑不了，I2S DMA（8×128 帧，约 32 ms）就会溢出丢样本 [算]。hwtest 里写卡发生在优先级 1 的 loop 中，正好低于 mic_task，所以 0 丢帧。
- **FatFs 卷锁**（F13）让两个任务可以同时用卡。代价是 loop 的 outbox 保存可能排在一次 fsync 后面，多等不超过 80 ms，仍在 09 §5 的 150 ms 例外之内 [算]。
- **音频任务内部的顺序**：先把满的缓冲全部写完 → 到点就 fsync → 再处理一个传输读请求（最多 4 KB，约 3 ms）。录音永远优先。
- **只有 loop 写 Serial**：HWCDC 的 `write()` 在缓冲不够时会先写一部分再循环等待，拿不到锁时直接返回 0（F5）。"先查空间再写"在两个写者之间不是原子的，可能卡住 loop，或者写失败。
- **SD 锁死标志**（09 §3.8）改成两个任务共享的原子量：任何一边遇到 EIO，两边都停。
- **纯逻辑放在 `lib/rec`**（native 可测）：`RecCore` 状态机（空闲 → 提示音 → 录音 → 收尾）、换段判断、`.M` 编解码、WAV 头、恢复规划、`file.get` 的参数校验。任务本身只是一层外壳。

### 4.2 内存（新增部分）

| 项 | 字节 | 依据 |
|---|---|---|
| 录音环形缓冲 6×4 KB | 24,576 | §3.3：换段最坏 290 ms；稳态 4 块就够 [测] |
| 传输：读缓冲 2×1380 B + base64 行缓冲 2 KB（用二进制时改为 2×4.2 KB） | 4,808（8,512） | §7 |
| HWCDC 发送环从 4 KB 加到 8 KB | +4,096 | §7.3 |
| 第 3 个 FatFs 文件名额（每个文件自带 4 KB 扇区缓冲） | ≈4,600 | [源] `CONFIG_FATFS_PER_FILE_CACHE` |
| 麦克风 DMA 和 mic_task 栈 | ≈6,700 | [源] F10：8×128 帧约 4 KB，栈 2,560 B |
| 音频任务栈 | 6,144 | 长文件名缓冲 512 B 在栈上，加 VFS、SPI、mbedtls 的帧 [算]；余量看 G-A6 |
| 待同步索引 256×12 B、SHA 上下文、队列 | ≈3,600 | |
| **合计** | **≈53 KB**（用二进制时约 57 KB） | |

- 扬声器（DMA 8×256 帧约 8 KB，加任务栈）只在响提示音时存在，和麦克风互斥，不叠加。
- 如果把环形缓冲、传输缓冲和索引（约 32 KB）都放成静态变量，静态 RAM 会从 63 KB 涨到约 96 KB，正好顶到预算。**建议**：只在 SD 为主存储时，开机一次性从堆上分配、永不释放（不会产生碎片）；没插卡的设备不付这笔内存。
- **预算建议**（[待 G-A6]）：
  - 空闲时保持 09 §5 的指标：空闲堆 ≥150 KB，最大连续块 ≥64 KB；
  - 同时在录音、同步、响提示音时：空闲堆 ≥96 KB，最大连续块 ≥32 KB；
  - 音频任务栈余量 ≥1.5 KB。

### 4.3 栈

- **loop 栈不变**（16 KB，余量 ≥4 KB）。loop 里新增的只有解析 `file.*` 请求、拼 `file.data` 行（用静态行缓冲），没有大的栈帧。`stack_budget.py` 要加上这两条调用链。
- **音频任务 6 KB**：长文件名缓冲 512 B 在栈上（`CONFIG_FATFS_LFN_STACK` [源]）。`.M` 用 `snprintf` 生成，不用 `JsonDocument`；只有开机恢复扫描解析 `.M` 时才用一个小的静态 `JsonDocument`。系统页显示它的栈高水位。

## 5. 录音时保持链路响应（"27% 的回复被扣"）

**原因分析**：
- 那次测量用的是 hwtest：写卡在 loop 里（平均 3.5 ms，最长 45 ms，fsync 最长 78 ms），USB 只有 `(bool)Serial` 这个 kick。被扣的回复都要等主机下一次写入（每秒一个 ping）才出来，所以约 1 s。
- app 的 transport 多做了一步（F3）：TX 环非空时 flush FIFO 并重新打开 IN_EMPTY。`usb_transport.h` 的注释记录了"中断有时带着数据把 IN_EMPTY 关掉，主机发空行也放不出来"的现象。有了这一步，v0.1 HIL 浸泡 0 次卡住。但它只在 loop 运转时生效。
- 推断 [算]：回复被扣多久 ≈ loop 两次 tick 之间的间隔。loop 不再写卡以后，最坏只剩渲染（全屏 push，约十几毫秒 [待]）和 outbox 保存（p95 29 ms）。

**措施**：
1. 录音 I/O 全部放进音频任务（§4.1）。
2. 保留每 20 ms 的 rearm。
3. 传输期间屏幕刷新降到每秒 5 帧以内，把 loop 的时间让给 USB。
4. daemon 已有 500 ms 的空行 nudge。传输请求会占用 credit，所以被扣的传输回复也在覆盖范围内（F17）。
5. `file.data` 只在 TX 环放得下整行（或整帧）时才生成，不进那个 4 行的 LineQueue，回复类消息仍然不会被丢弃。

**验收（G-A2）**：录音 10 分钟，主机每秒 ping 加正常的快照推送：回复 p95 ≤100 ms，0 个超过 600 ms，0 丢帧。

另外要留意 07 §3 的"偶发掉出 Ready"（SOF tick hook 误判拔线）在录音时会不会更频繁：G-A2 同时统计 `E_NOT_READY`。

## 6. 电池

- **官方数据**：电池 1500 mAh（早期批次 1750 mAh），4.2 V 下工作电流 120.2 mA [源 F16]。官方没说明这个数字是否亮着屏。
- **录音多出来的耗电**：codec ADC 和 I2S、SD 写入（占空比约 3% [测 §7.2]）、每秒一次 fsync，估计 +5–15 mA [算]。灭屏（用电池时 60 s 后关背光）能省回一部分 [待]。
- **估算**：可用容量按 85–90% 计（电压到 3.45 V 就停，§2.5）。

| 情况 | 电流 [算] | 1500 mAh | 1750 mAh |
|---|---|---|---|
| 亮屏录音 | ≈135 mA | ≈9.4 h | ≈11 h |
| 灭屏录音 | ≈110 mA | ≈12 h | ≈14 h |

  也就是每录 1 小时耗电约 7–9%。
- 连着 Mac 时边录边充，不受影响。
- **可选的省电手段**（v0.3 不默认开，要 G-A7 验证）：录音时 CPU 降到 80 MHz（可能影响 USB 吞吐，只在用电池时开）。录音时不能浅睡，因为 I2S DMA 在跑。
- **G-A7**：充满电 → 灭屏录音直到自动停 → 每分钟把 `bat_mv` 记到 SD 的日志里。得到实际小时数和放电曲线，也用来确定 3.45 V 这个停止阈值。

## 7. 传输到 Mac

### 7.1 总体：主机拉取

daemon 列清单 → 按偏移分块取 → 校验 → 落地 → 发删除 → 设备删除。

- 每个主机请求恰好一个回复（05 §6.3 第 2 条），所以 credit 窗口、`re` 匹配、空行 nudge 都直接适用。
- 设备不需要传输状态机；掉线后按偏移续传。
- **发现新文件**：设备在 `hello` 和 `pong` 里带可选的 `files` 对象；`rev` 一变，daemon 就重新列清单。

**为什么不用 09 §6、§12 预留的 outbox `kind:"rec"`**：
1. v0.2 的 daemon 只认 `note` 和 `queue`（05 §6.5），会把 `rec` 判成 rejected。设备随后把它挪进只保留 20 条的 bad/，这段录音的登记就丢了。要避免，就得让 `OutboxCore` 按能力跳过某种 kind，等于改 v0.1 的核心逻辑。
2. outbox 的容量（200 条）和内存里的 id 数组是和笔记共用的，10 小时录音就要占 120 条。
3. 文件的生命周期（传完才删）和 outbox 条目的生命周期（ack 就删）本来就是两件事，daemon 无论如何都得单独跟踪文件。
4. 05 §6.5 的"outbox 只有 note 和 queue"这条硬性规定不用改。

所以录音的"提交"放在 SD 上的 `.M` 里，发现靠主机列清单。

### 7.2 吞吐估算

**线速 W**（设备到主机的原始字节率）还没单独测过：
- 从 sd-card §7 的拉取反推：157 块，每块 86.6 ms，线上约 16.5 KB。全部算作线上时间是 186 KB/s；扣掉每块约 15 ms 的读卡、CRC 和请求开销是 224 KB/s [算]。主机脚本的轮询粒度也可能拖慢了它，所以这只是粗估。
- 上限参考：Espressif FAQ 说 ESP 的 USB 受 FIFO 限制，最高约 500 KB/s（OTG 场景，不是 Serial/JTAG）[源 F7]；USB 全速理论值约 1.2 MB/s。HWCDC 每次中断只送 64 B（F5）。
- 所以按 185–500 KB/s 来规划。

SD 读 1.6–1.7 MB/s，CPU 做 base64 和 CRC 也都不是瓶颈 [测/算]。主机发来的请求每块约 90 B，占用的总线时间很少。

**两种编码的有效率**（算上 JSON 信封，例子见 §7.4）：
- base64 放进 ≤2048 B 的行：每行最多 1380 B 数据，一行约 2000 B，有效率约 69%。
- 二进制子模式：头行约 158 B + 4096 B 数据 + 1 B，有效率约 96%。

| 线速 W | b64 有效吞吐 | 1 小时录音 | 二进制有效吞吐 | 1 小时录音 |
|---|---|---|---|---|
| 185 KB/s（反推下限） | 128 KB/s | 14.6 min | 178 KB/s | 10.5 min |
| 225 KB/s（反推） | 156 KB/s | 12.0 min | 217 KB/s | 8.7 min |
| 350 KB/s | 242 KB/s | 7.7 min | 337 KB/s | 5.6 min |
| 500 KB/s（参考上限） | 346 KB/s | 5.4 min | 481 KB/s | 3.9 min |
| 实测：12 KB 块 base64，逐块等回复 | 138 KB/s | 13.6 min | — | — |

- **目标（建议）**：1 小时录音 6 分钟内同步完（有效吞吐 ≥320 KB/s）；底线是 10 分钟（≥190 KB/s）。按反推的线速，b64 达不到底线，二进制勉强达到。所以二进制大概率要做，但先测。
- **如果线速真的只有约 200 KB/s**，还剩这些办法（都不在 v0.3 默认里）：
  - 设备端 IMA ADPCM：4:1，几乎不占 CPU，对转写准确率的影响要 G-A10 量；
  - 简化版 FLAC：无损，约 1.6–2 倍 [算，未测]，实现量大；
  - Launcher 的 U 盘模式（§7.7）；
  - 晚上连着电脑慢慢同步，本来也够用。

### 7.3 base64 行内 vs 二进制子模式

| | base64 行内 | 二进制子模式 |
|---|---|---|
| 有效率 | ≈69% | ≈96% |
| daemon 分帧 | 不用改（每行 ≤2048 B） | `framing.split` 要认出 `file.data` 头行，然后收 `bin` 个原始字节和 1 个 `\n` |
| 设备发送 | 走现有的整行发送，只要求"放得下整行才生成" | 新增整帧发送：头行、数据、`\n` 拼成一块连续缓冲，一次 `write`；TX 环要 8 KB |
| 出错恢复 | 一行坏了就丢掉这一行，重取这一块 | 帧后不是 `\n`，或者 CRC 不对：丢弃字节，直到出现一行能解析的 `{"v":0,…`；同一连接 1 分钟内出现 3 次以上，就退回 b64 |
| 契约测试 | 普通 JSON 向量 | 还要字节级向量（已有 `line_b64` 的先例） |
| Launcher 控制台规则 | 不受影响（规则只管主机到设备方向） | 同左 |
| 旧版本 | 不受影响 | 只有主机明确要 `enc:"bin"`、设备报告 `files.bin:true` 时才用 |

**决定规则（建议）**：
1. 先在 hwtest 里把两种编码都做成命令，跑 G-A1。hwtest 只是测量固件，做起来便宜。
2. b64 有效吞吐 ≥320 KB/s：app 只做 b64。
3. 否则，如果二进制 ≥1.3 倍 b64：app 两种都做，二进制为默认，b64 作退路。
4. 两种都低于 190 KB/s：再考虑 ADPCM。

协议里两种编码从一开始就定义好（`enc` 字段），所以不管走哪条路都只是新增，不会再动 `v`。

### 7.4 协议新增（`v` 仍为 0）

**设备 `hello` 和 `pong` 的可选字段 `files`**：

| 字段 | 取值 | 含义 |
|---|---|---|
| `files.n` | 0–256 | 已提交、还没删除的段数 |
| `files.rev` | 0–4294967295 | 每次提交、删除、恢复都加 1（到头回绕） |
| `files.live` | bool | 正在录音 |
| `files.bin` | bool | 支持 `enc:"bin"` |

不带 `files`，表示设备不支持录音传输（v0.2 及以前）。

**消息**：

| `t` | 方向 | 模式 | 字段 |
|---|---|---|---|
| `file.list` | H→D | 主机发起 | `after?`（MonoId，从它之后开始，不含它本身）、`max?`（1–4，默认 4） |
| `file.items` | D→H | 回复 | `items[≤4]`、`more`（bool） |
| `file.get` | H→D | 主机发起 | `fid`（MonoId）、`off`（0 到 2³²−1）、`len`（b64 时 1–1380，bin 时 1–4096）、`enc?`（`b64` 为默认，或 `bin`） |
| `file.data` | D→H | 回复 | `fid`、`off`、`n`（0 到 `len`）、`size`（当前可读的文件总长，含头）、`final`（bool，这一段已提交）、`crc`（这 n 个字节的 CRC-32/IEEE）；再加 `b64`（enc 为 b64 且 n>0 时）或 `bin`（enc 为 bin 时，值等于 n，这一行后面紧跟 n 个原始字节和一个 `\n`） |
| `file.del` | H→D | 主机发起 | `fid`、`size`、`sha256?` |
| `ack` / `error` | D→H | 回复 | 删除成功或本来就不在：`ack`；否则 `error` |

`items` 里每一项的字段：`id`、`kind`（v0.3 只有 `rec`）、`size`（含头）、`final`、`sha256?`、`sha_from`（固定 44）、`rec`（会话 id）、`seg`（0–9999）、`last`、`dur_ms`、`at{up_ms, wall_ms|null}`、`project?`、`marks?[≤16]`、`lead_ms?`、`rcv?`。单项最长约 431 B，所以一次回复最多 4 项；设备按 2048 B 截断，并置 `more:true`。

**规则**：
- 正在录的段也会出现在清单里：`final:false`，`size` 是上次 fsync 时的长度，可以边录边取。`final` 变成 true 之后，daemon 必须重新取 [0, 44)，因为头被回填过。
- `file.get` 的 `off ≥ size` 时回 `n:0`，不算错误，daemon 借此知道已经取完。
- `file.del` 只在 `size`（以及设备记录的 `sha256`，如果有）对得上时才删；对不上回 `E_BAD_FIELD`（`field` 为 `size` 或 `sha256`）。正在录的段回 `E_BUSY`。文件不在则回 `ack`：这样做是幂等的，丢了 ack 重发也安全。删除顺序是先 `.W` 后 `.M`（§3.4）。
- **在途上限**：设备最多排队 4 个 `file.*` 请求（`file_inflight_max`），再来的立即回 `E_BUSY`；daemon 自己也只保持 ≤4 个在途。
- **错误码全部沿用已有的**：`E_UNKNOWN_ITEM`（`file.get` 的 fid 不在）、`E_BAD_FIELD`、`E_BUSY`、`E_STORAGE`（读卡失败或 SD 锁死）、`E_NOT_READY`、`E_UNKNOWN_TYPE`（旧设备）。不新增错误码。

**`protocol/limits.json` 新增**：

```json
"file_list_max": 4,
"file_get_b64_max": 1380,
"file_get_bin_max": 4096,
"file_inflight_max": 4,
"file_pending_max": 256,
"file_marks_max": 16,
"file_seg_max": 9999,
"sha256_pattern": "^[0-9a-f]{64}$"
```

1380 的来由：`id` 和 `re` 都取最长的 24 个字符、所有数字都取最大值时，`file.data` 行是 2039 B（含 `\n`），不超过 2048 [算]。

**示例**：

```jsonl
{"v":0,"t":"pong","id":"d40","re":"h40","up_ms":735000,"pending":0,"revs":{"projects":42,"sessions":17,"queue":9},"files":{"n":2,"rev":17,"live":false,"bin":false}}
{"v":0,"t":"file.list","id":"h41"}
{"v":0,"t":"file.items","id":"d41","re":"h41","items":[{"id":"K3M9Q2T-00042-0A1C","kind":"rec","size":9600044,"final":true,"sha256":"<64 位十六进制>","sha_from":44,"rec":"K3M9Q2T-00042-0A1C","seg":0,"last":false,"dur_ms":300000,"at":{"up_ms":435000,"wall_ms":null},"project":"cpa","lead_ms":150}],"more":false}
{"v":0,"t":"file.get","id":"h42","fid":"K3M9Q2T-00042-0A1C","off":0,"len":1380}
{"v":0,"t":"file.data","id":"d42","re":"h42","fid":"K3M9Q2T-00042-0A1C","off":0,"n":1380,"size":9600044,"final":true,"crc":2914141237,"b64":"UklGRiz…"}
{"v":0,"t":"file.get","id":"h43","fid":"K3M9Q2T-00042-0A1C","off":9598620,"len":4096,"enc":"bin"}
{"v":0,"t":"file.data","id":"d43","re":"h43","fid":"K3M9Q2T-00042-0A1C","off":9598620,"n":1424,"size":9600044,"final":true,"crc":1056532021,"bin":1424}
<1424 个原始字节><\n>
{"v":0,"t":"file.del","id":"h44","fid":"K3M9Q2T-00042-0A1C","size":9600044,"sha256":"<64 位十六进制>"}
{"v":0,"t":"ack","id":"d44","re":"h44"}
```

**命名**：05 §6.10 预留的 `voice.begin` / `voice.chunk` / `voice.end` 是"设备推送"的形状，和这里的主机拉取不一样。建议不复用，名字继续留着；改用通用的 `file.*` 加 `kind`，以后归档别的文件不用再加消息类型。由 Lead 决定。

**兼容性**：
- v0.3 设备配 v0.2 daemon：daemon 不发 `file.*`，也会忽略 `files`（`protocol/schema/v0` 里没有任何 `additionalProperties:false`，05 §6.2 规定不认识的字段一律忽略 [源]）。录音留在卡上，设备显示待同步段数。
- v0.2 设备配 v0.3 daemon：没有 `files` 就不拉；就算误发了，也只会收到 `E_UNKNOWN_TYPE`。
- 主机发出的行都以 `{` 开头，符合 Launcher 控制台的规则。二进制数据只出现在设备到主机的方向。

**契约**：每种新消息先写 schema 和测试向量（RED）。
- 消息向量：合法用例；`len` 超限；`enc:"zip"`；`fid` 不是 MonoId；b64 解码后长度 ≠ n；`file.items` 超过 4 项；二进制帧（字节级向量）。
- flows：续传；`file.del` 的 ack 丢失；`final` 翻转后重取头。

### 7.5 续传、校验与删除

**设备侧**：无状态。只缓存一个打开的读句柄：同一个 fid、偏移连续时不重新打开、也不重新 lseek；空闲 2 s 或换了 fid 就关掉。FatFs 没开 fast-seek 时 lseek 要沿簇链走，5 分钟一段最多 293 个簇、FAT 表最多 3 个扇区，开销可以忽略 [算]。

**daemon 侧**（每台设备一个拉取器，同一时间只拉一个文件）：
1. 进入 Ready 后，以及 `files.rev` 变化时，翻页列出清单。
2. 对事件日志里还没有 `rec.stored` 的段，按 id 从小到大拉（也就是会话顺序），暂存到 `$CPA_HOME/rec-staging/<dev>/<fid>.part`（权限 0600）。续传时先把 `.part` 截掉最后 64 KB，再从那里接着取，防止崩溃时尾部只写了一半。
3. 每块核对 CRC32 和偏移是否连续；坏块就重取同一个偏移；同一块连续坏 3 次，本次连接里就放弃这个文件。
4. 取完后检查三项：`size` = 44 + `bytes`；头和按 `bytes` 重建的标准头逐字节相同；`sha256(文件[44:])` 等于清单里的值（`rcv` 段跳过这一项）。
5. 落地（§8.1）→ 追加 `rec.stored` 事件并 fsync → 发 `file.del` → 收到 ack 后追加 `rec.deleted`。
6. 设备上还在、但日志里已经有 `rec.stored` 并且 SHA 相同的段：直接发 `file.del`。这一条覆盖 ack 丢失和 daemon 重启两种情况。

- **什么时候拉**：只要链路 Ready 就拉，录音时也拉（要不要限速由 G-A3 决定）。`cpa pause`（刷机前）会中断传输，之后按偏移续传。
- **删除安全**：设备只在 `size` 和 `sha256` 对上时才删；daemon 只在 fsync 之后才发删除。任何一步断电，最坏是同一段再传一次，daemon 按 `(dev, fid)` 加 SHA 去重。

### 7.6 边录边取（可选，协议已经允许）

连着 Mac 开会时，daemon 可以每 10–30 s 把正在录的段取到上次 fsync 的位置，WhisperWoof 就能按片段陆续出字。v0.3 可以只做"一段结束后再取"（延迟 ≤5 分钟加传输时间），这个功能以后再打开。

### 7.7 后备：Launcher 的 U 盘模式

- 积压很多时（比如出差回来有几十小时），可以手动进 Launcher → SD→USB。这要求 Launcher ≥ `09ffc59`，目前只有 beta 版有（launcher.md §5）。
- daemon 加一个 `cpa import-card /Volumes/<卷名>`：按 `.M` 同样校验、落地，再删掉卡上的文件。
- 代价：要离开我们的固件；macOS 会往卡里写 `.Spotlight-V100`、`._*` 之类的文件，设备扫描时会把它们当成 other 忽略 [待]。
- ESP32-S3 自己做 U 盘要用 OTG 外设，它和 Serial/JTAG 同一时刻只能用一个，而且用了就没有自动下载（F7），所以不作为主路径。

## 8. daemon 端

### 8.1 落地位置

```
$CPA_HOME/rec-staging/<dev>/<fid>.part                 传输中（0600）
~/CardputerNotes/recordings/<YYYY-MM-DD>/<HHMM>-<会话 id 小写>/
    000.wav  001.wav …                                  每段一个文件，原样保留（含 44 B 头）
    session.json                                        会话清单
```

- **落地步骤**：`.part` fsync → `rename` 到目标（同一个卷上是原子的；`$CPA_HOME` 和 `~/CardputerNotes` 默认都在主目录所在的卷）→ fsync 目录。如果 `notes_dir` 配到了别的卷，就先复制到目标目录里的临时名，再 rename [待]。
- **`session.json`** 的字段对齐 WhisperWoof 会议设计里的 manifest（片段序号、墙钟开始、样本数、状态）：
  `{v, source:"cardputer", dev, rec, project, state: recording|complete|recovered, sample_rate:16000, channels:1, lead_ms, clips:[{index, file, id, samples, wall_start_ms, wall_est, sha256, rcv}], marks:[{clip, ms}]}`。
  每落地一段就整体重写一次（先写临时名再 rename）。
- **日期和时间**：
  - 优先用 `at.wall_ms`；
  - 没有时，如果同一个 boot 后来连上过 daemon，就用那时的"墙钟减 `up_ms`"偏移反推，并标 `wall_est:true`；
  - 都没有，就用落地时间，前面加 `~`（同 05 §7.8 笔记的规则）。
- **每日笔记**加一行（具体格式由 daemon 负责人定），例如：`- 14:32 录音 12:31 #cpa → recordings/2026-10-20/1432-k3m9q2t-00042-0a1c/ ^cpa-k3m9q2t-00042-0a1c`。
- **体积**：1 小时 115 MB。要不要定期清理、这个目录会不会被 iCloud 同步，列为开放问题。

### 8.2 交给 WhisperWoof（只给输入，契约另定）

**已知事实** [源 WhisperWoof@767ae8d]：
- 文件导入接受 `.wav`（以及 mp3、m4a 等），单个文件 ≤500 MB，16 kHz 单声道约等于 4.3 小时。
- 会议设计：16 kHz，2–5 分钟一个片段，每个会议一份 manifest，崩溃后按 manifest 重新排队。片段在停顿处切，离线模型再切成 ≤20 s 的小块。
- 它的中英混合会议测试中，SenseVoice（MER 15.6%）比 whisper-large-v3-turbo（25.8%）好约 10 个百分点；FireRedASR2 AED 是 13.3%，但很慢（RTF 0.455）。所以中英混合建议默认用 SenseVoice，而用户原本以为是 Whisper。
- 已有的"伴侣设备"先例是 Telegram 伴侣：外部进程写 `telegram-inbox.json`，桌面端每 10 s 轮询一次并导入。它只传文字（`plugins/telegram-companion/index.js`、`src/whisperwoof/bridge/telegram-sync.js:16-17`）。
- 加密：WhisperWoof 保险库用 WWENC1 格式（X25519 + HKDF + AES-256-GCM，≤64 KiB 分块）。只要公钥就能写，所以保险库锁着时也能录（`vault/wwenc-pure.js:1-14`）。

**建议**：交接单位是"会话目录 + `session.json`"，不拼成一个大 WAV。这样省一份磁盘，也碰不到 500 MB 的上限。具体走监视目录、本地接口还是插件，由 WhisperWoof 集成负责人定。

**开放**：如果用户开了 WhisperWoof 的加密，`~/CardputerNotes` 里放明文 WAV 就和它的设计相反。可选的做法：
- daemon 落地时直接封成 WWENC1；
- 更进一步，设备直接用保险库公钥加密封装（ESP32-S3 有 AES 和 SHA 硬件，mbedtls 支持 X25519），这样 SD 卡和 Mac 上都不会有明文。

v0.3 不做加密，列为开放问题。

## 9. 设备闸门

| 编号 | 测什么 | 怎么测 | 通过标准 | 谁来做 |
|---|---|---|---|---|
| G-A1 | HWCDC 持续吞吐 | hwtest 加命令 `!xfer <b64\|bin> <len> <depth> <txring>`，数据分别来自 RAM 和 SD（`rec.wav`）；主机脚本同时每秒 ping 一次 | 出一张表：有效 KB/s、CPU 占用、ping 延迟；按 §7.3 的规则选编码 | 独占设备的 agent，约 30 分钟 |
| G-A2 | 录音时的链路（app 结构） | 测试版 app：音频任务录 10 分钟，daemon 正常心跳并推快照 | 0 丢帧；回复 p95 ≤100 ms，0 个超过 600 ms；`E_NOT_READY` 为 0 | agent |
| G-A3 | 录音和同步同时进行 | 录 30 分钟（段长 1 分钟），同时不停地拉取前面的段 | 0 丢帧；环形缓冲最多占用 ≤4/6；吞吐 ≥G-A1 的 70%；每段 SHA 都对 | agent |
| G-A4 | 换段无缝 | 段长 1 分钟，录 30 段 | 各段样本数之和 = 墙钟 × 16000 − 第一块的延迟，误差 ≤1 块；记下换段最长耗时和环形缓冲最多占用 | agent |
| G-A5 | 提示音时序 | 分别量 `Speaker.begin`、提示音、`Speaker.end`、`Mic.begin` 的耗时，loop 最长阻塞，`Mic.begin` 之后 0–500 ms 的 RMS（据此定 `lead_ms`）；I2C 并发：开始录音时连续按键盘 | loop 单步 ≤50 ms；20 次里键盘没有错键、codec 配置全部成功；定出 `lead_ms` | agent，加人耳听 2 分钟（音量、听不听得见；v0.3.1 起不出声，耳听不适用） |
| G-A6 | 内存与栈 | 空闲、录音、录音 + 同步 + 提示音三种状态下，记空闲堆、最大连续块、音频任务和 loop 的栈高水位 | 满足 §4.2 的预算 | agent |
| G-A7 | 电池 | 充满电，灭屏录音直到自动停，每分钟记一次 `bat_mv` | 得到实际小时数，定出停止阈值；≥8 h 算通过（建议值） | 人（要拔线，约 10 小时，可以过夜） |
| G-A8 | 断电恢复 | 录音中拨开关并拔线 10 次，其中 3 次正好卡在换段的时候（段长 1 分钟） | 每次开机都能恢复成可播放的 WAV，`rcv:true`，会话能接上（`next`）；用 Mac 急救记下丢失簇 | 人 |
| G-A9 | 拔卡与卡满 | 录音中拔卡；用一张小卡或预先塞满的卡一直录到满 | 拔卡：最多卡一次（≤2 s），响失败提示音；重插卡重启后那一段能同步。卡满：本段正常收尾 | 人 |
| G-A10 | 音质与转写 | 设备和 Mac 麦克风同时录一段中英混合朗读（30 cm 和 1 m 两个距离），量峰值和 RMS（dBFS），用 WhisperWoof 的 SenseVoice 和 whisper-turbo 转写 | 设备录音的 MER 比 Mac 麦克风差 ≤3 个百分点（建议值）；不削波；据此确定 `magnification` | 人（要说话） |
| G-A11 | 二进制分帧的鲁棒性（只在选了二进制时做） | 测试版注入错误：CRC 不对、帧太短、帧后不是 `\n` | daemon 1 s 内恢复，落地文件 0 错误 | agent |
| G-A12 | 手感 | 用一天：开始和结束各 20 次，加标记、看 LED 和屏幕 | 用户主观确认，记下误触次数 | 人 |

**HIL 场景**（`tools/hil`，可以自动跑）：
- `rec_sync`：测试版钩子（只在测试固件里的 `debug.rec{start|stop}`）录一段 70 s 的音频，段长 30 s → 3 段全部落地，SHA 都对，卡上删光。
- `rec_resume`：拉到一半执行 `cpa pause`，恢复后按偏移续完。
- `rec_del_lost_ack`：丢掉 `file.del` 的 ack，重发后结果仍然正确。
- `rec_old_daemon`：v0.2 daemon 不拉，也不报错。

## 10. 测试（先写，RED）

- **固件 native**（`lib/rec`）：
  - `RecCore` 状态机：每个状态下的每个按键；
  - 换段时机；WAV 头；`.M` 编解码（marks 超长、坏 JSON）；
  - `plan_rec_recovery`：§3.4 的每一行，以及各种组合；
  - `file.get` 的校验：越界、`enc`、在途上限；
  - b64 行在最坏情况下 ≤2048 B；
  - MonoId 原子取号。
- **FakeFatPort 断电注入**：在换段、收尾、删除的每一步断电，然后跑恢复。要求：已提交的段 0 丢失；会话能接上；绝不删除有 `.M` 的 `.W`。
- **daemon**：拉取器（续传截尾、坏块重取、`final` 翻转后重取头）；校验；落地的原子性（每一步注入崩溃）；`file.del` 幂等；二进制分帧（如果做）；`session.json` 生成；墙钟反推。
- **契约**：§7.4 列的向量和 flows。
- **设备测试**（`test/device`）：`test_rec_budget`（G-A6 的自动部分）、`test_rec_rotation`（G-A4 的短版本）。

## 11. 风险

| # | 风险 | 缓解 |
|---|---|---|
| A1 | HWCDC 线速只有约 200 KB/s | 二进制子模式；ADPCM；U 盘后备；过夜同步 |
| A2 | 录音中不能出声 | 标记和警告用屏幕加 LED。要彻底解决得自写全双工 I2S 驱动（一个 I2S 口同时收发），而且那样提示音也会被录进去 |
| A3 | 开始延迟 0.7–0.9 s，提示音之前说的话会丢 | 教用户"听到提示音再说"；屏幕在按下后立即显示"准备…" |
| A4 | 锁定式录音在口袋里被误开 | 500 ms 长按门限 + 提示音 + LED；静音自动停（开放问题）；低电量自动停 |
| A5 | 键盘和 ES8311 的 I2C 并发访问 | G-A5 验证；共享锁，或者把 begin/end 放回 loop |
| A6 | 麦克风电平偏低，影响转写 | G-A10 确定增益 |
| A7 | FAT 断电后留下丢失簇，或者目录扇区写坏 | 每秒 fsync；恢复规则；Mac 急救；同 09 R3、R16 |
| A8 | 只测过一张卡 | 同 09 R4 |
| A9 | SD 卡和 Mac 上的录音都是明文 | 开放问题；WWENC1 封装作为后续选项 |
| A10 | outbox 保存排在录音的 fsync 后面，偶尔超过 150 ms | G-A2 统计；必要时在 outbox 保存期间推迟 fsync |
| A11 | 离线重启后没有墙钟 | daemon 反推；反推不了就标 `~` |
| A12 | 未同步上限 256 段 | 到上限就拒绝新录音并提示；数字可以调 |

## 12. 需要拍板的问题

1. 手势用 A（长按开始并锁定，再按结束）还是 B（按住说话）？建议 A。
2. 提示音的音量，以及要不要"会议静音"模式（只用 LED 和屏幕）。2026-10-21 改为：录音全程不出声、只供个人使用；v0.3.1 起录音全程不出声（便条和会议都是），只用屏幕；会议确认框保留。
3. 段长用 5 分钟（建议）还是 2 分钟（和 WhisperWoof 的片段一样长）。
4. 是否用 `.M` 加主机拉取，取代 09 §6、§12 的 outbox `kind:"rec"`？建议取代。
5. 消息名用 `file.*`（建议），还是 05 §6.10 预留的 `voice.*`。
6. 先跑 G-A1 再决定 b64 还是二进制（建议），还是直接做二进制。
7. 设备上要不要"丢弃这段录音"（Fn+`` ` `` 加确认）。
8. 要不要静音自动停，阈值多少（例如连续 15 分钟低于底噪 +6 dB）。
9. 未同步上限 256 段是否合适。
10. Mac 上的录音放明文 WAV 是否可以，保留多久，`~/CardputerNotes` 会不会被云同步。

## 13. 对已有文档的影响（由 Lead 修改）

- **04 §7.7**：去掉"按下前预录"；`.wav.part` 加改名，改为直接写最终文件名并用 `.M` 提交；"录完后改名，算出 SHA-256，登记进 outbox"，改为录音时增量算 SHA-256、由主机拉取。**§8.2**：G0 长按改成开始录音。
- **05 §6.10**：决定 `voice.*` 预留的去留；新增一节写 `file.*`；`protocol/limits.json` 加上 §7.4 的各项。
- **09 §6 的"v0.3 预留"和 §12**：outbox `kind:"rec"` 被本文 §7.1 取代；`max_files` 维持 3（录音写 1 个、传输读 1 个、outbox 1 个）；SD 锁死标志改成跨任务共享。**09 §5 预算**：加上录音状态下的堆指标和音频任务栈指标（§4.2）。
- **sd-card.md §8** 的"传到 Mac"一行：改为指向本文 §7。
- **新 ADR**：录音传输采用主机拉取加 `file.*`。

## 附：外部来源

- M5Stack 官方 Cardputer-Adv 页面（电池、电流、功放和 codec、RGB LED、SD 引脚）：https://docs.m5stack.com/en/core/Cardputer-Adv
- Espressif USB FAQ（FIFO 限制约 500 KB/s；S3 的 Serial/JTAG 和 OTG 同一时刻只能用一个）：https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/usb.html
- WhisperWoof（MIT），提交 `767ae8d2315507dd0dca31839e4552e0e4baa175`：`docs/design/local-meeting-transcription.md`、`src/whisperwoof/bridge/file-import.js`、`src/whisperwoof/bridge/user-audio-file.js`、`src/whisperwoof/bridge/telegram-sync.js`、`plugins/telegram-companion/index.js`、`src/whisperwoof/bridge/vault/wwenc-pure.js`
