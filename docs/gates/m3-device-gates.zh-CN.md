[English](m3-device-gates.md) · **中文**

# M3 设备闸门实测：G-A0 堆基线、G-A1 HWCDC 吞吐、G-A5 提示音时序与 I2C 并发、G-A10 步骤

> 2026-10-20 在真机（Cardputer ADV，MAC 02:00:00:00:00:01，插着 v0.2 用的那张 512GB FAT32 卡）上实测，M3-H 阶段 0（docs/12）。
> 设备端：`firmware/hwtest/src/gate_*.cpp`（`!xfer` `!xfermk` `!gateclean` `!cue` `!mic`），纯逻辑在 `firmware/hwtest/lib/gate_core`（`pio test -e native`）；主机端：`firmware/hwtest/host/hwtest_host.py <子命令>`（`gate_*.py`，`pytest host/tests`）。
> 原始 JSON：`firmware/hwtest/captures/g-a*`（已 gitignore，见 §7）。标记：✅ 真机实测 · 📚 来自文档或源码 · ⏳ 没验证 · 👤 需要用户。

## 0. 环境与操作记录

| 项 | 值 |
|---|---|
| 工具链 | A：`espressif32@6.10.0`，Arduino core 2.0.17 / ESP-IDF 4.4.7（ADR-0001）；hwtest 另外用 GCC 14（p54 env）做 `-Wall -Wextra -O2` 编译检查，新文件 0 警告 |
| 端口礼仪 | 先 `cpa pause 5400`，`lsof /dev/cu.usbmodem*` 为空才动设备，中途续了 5 次 pause。所有上传都是 `pio run -t upload`，没按 G0，串口只用安全打开序列（03 §2）。设备没有卡在下载模式过 |
| G-A0 固件 | `firmware/app` env `cardputer-adv-test`，`HEAD f0fa2f9`（v0.2.0），工作区 `firmware/app` 无改动 |
| 重构后复验 | 跑完闸门后整理过 `gate_xfer.cpp`（拆出 `gate_usb.cpp`、降复杂度），又刷一次最终版 hwtest 复验：`xfer b64 1380 4 4 sd` 308.5 KB/s、`cue memo lockyield` 开始 147 ms、读回一致，和前面的数据一致；同一次里 `!xfermk 4096` 出现了 §6 的 EIO |
| 结束状态 | 刷回 v0.2 release（`firmware/app` env `cardputer-adv`，同一 HEAD），`cpa pause 1`，`cpa status`：`link: ready … fw=cpa-fw/0.2.0 store=sd launcher=no`。整片刷写去掉了 Launcher（预期内，M3-L 负责恢复） |
| 卡上内容 | 闸门只写 `/sd/cpa_hwtest/{xfer.bin,mic.wav}`，结束时 `!gateclean` 删掉文件和目录；`!sdtree` 复查只剩 v0.2 的 `/cpa/…` 和 Launcher 的 `/config.conf`。G-A0 的测试只用 `/cpabudget`，用完自己删掉。没碰 `/cpa` |
| 隐私 | 录音只有 4 段各 5 s 的安静房间底噪（G-A10 参考），只留数字，WAV 拉回后立即删除（`--discard`），卡上的也已删除；日志里没有任何音频内容 |

## 1. 结论与定下的常量（给 M3-A / M3-B / M3-W）

| 常量 / 决定 | 值 | 依据 |
|---|---|---|
| 环形缓冲等的分配方式 | **开机一次性分配（默认方案）**。基线空闲堆 234,836 B、最大连续块 208,884 B；减去约 38 KB 后约 196 KB / ≥170 KB，远高于 ≥150 KB / ≥64 KB | G-A0 §2 ✅ |
| app 的 HWCDC TX 环 | **4 KB**（不加到 8 KB）。b64 1380 B、4 个在途：4 KB 632.7 KB/s，8 KB 631.4 KB/s，吞吐一样；8 KB 只是把负载下的 ping RTT p95 从 3.9 ms 拉到 7.6 ms | G-A1 §3 ✅ |
| v0.3.x 二进制子模式 | **不做**。按 audio-capture §7.3 第 2 条：b64 有效吞吐 ≥320 KB/s 就只做 b64。链路本身能给 b64 633 KB/s；app 真正的瓶颈是读卡方式（§3.3），二进制在那种情况下只快 1.03 倍 | G-A1 §3 ✅ |
| `file.get` 每块 | **1380 B（协议上限 `file_get_b64_max`），≥2 个在途**（app 设计的 4 个足够）。1 个在途是 449 KB/s，2 个就到链路上限 | G-A1 §3 ✅ |
| b64 吞吐（给 daemon 定预期） | 链路上限 **≈633 KB/s 有效（≈908 KB/s 线上）**，1 小时录音约 3.0 min。按 app 现在的读卡方式（每块 open→lseek→read→close）在单核上是 **302–322 KB/s**（1 小时约 6 min）；传输期间文件保持打开则是 **632 KB/s**。都远高于 app 层 ≥100 KB/s 的目标和"1 小时 ≤20 min"的底线 | G-A1 §3 ✅ |
| 编码实现（新增要求） | **base64 用普通查表实现，CRC-32 用 `esp_rom_crc32_le`**（和 zlib 一致，129,644 帧 0 错）。不要用 `mbedtls_base64_encode`（常数时间实现，每 1380 B 多 ≈1.6 ms）或逐位 CRC（≈0.35 ms）：用了以后同样吞吐下 core 1 忙 88–100%，换掉后 39% | G-A1 §3.2 ✅ |
| 传输读卡（新增建议） | 同一个 `fid` 连续 `file.get` 时保持文件打开，开始录音、写 `.M` 之前关掉（音频任务仍然最多 1 个打开的文件，11 §4.5 的不变式不破）。每块重新 open/close 要多 ≈2.6 ms，吞吐减半 | G-A1 §3.3 ✅ |
| `lead_ms` | **150 ms**。`Mic.begin` 后的 codec 冲击：前 20 ms RMS 125–215（峰值约 300），之后 20–35 慢慢落回底噪；112 次里 p50 60 / p95 90 / 最长 130 ms，另有 1 次 200 ms 是房间里的声音接上了尾巴。150 = p95 90 + 余量，比设计默认 200 少吃掉一点"嘀声后马上说"的第一个字。2026-10-21：这些数都是 `Mic.begin` 之前先有一次扬声器开关（提示音）时测的；v0.3.1 的无声开始由 `mic_cal` 在 `kMicGain` 下补测：5 次 90 / 80 / 70 / 80 / 90 ms，150 保留（§5.4）。冲击出现在 PGA 之后，随数字增益（REG17、`magnification`）变大：v0.3.0 的 ×8 下无声开始是 260 ms。v0.3.2（#31）起从写下的第一个样本算：codec 热时第一块就是冲击，和这些测量一样；codec 冷时没有冲击 | G-A5 §4.3 ✅，无声开始 §5.4 ✅ |
| 提示音音量 | ~~👤 **待用户耳听**（`cue-listen`，§4.5）~~ **不适用**：2026-10-21 起录音不出声、只供个人使用，v0.3.1 起录音全程不出声，没有音量要定。agent 测时序用的是 64 | G-A5 |
| I2C 锁 | **要锁，但必须有让出**（"lockyield"：音频任务等锁时置 want 标志，loop 放锁后见到标志就让出 1 tick）。裸 FreeRTOS 互斥量在 loop 连续读键盘时，音频任务最多等了 3.7 s（跨核不保证交接），便条开始拖到 4.3 s。加了让出后 40 次 0 错键、0 次 codec 配置失败，最长等锁 0.6 ms。不加 app 锁时 25 次也是 0 错（M5GFX 每个 I2C 事务自带端口互斥，📚 `common.inl:2278`） | G-A5 §4.2 ✅ |
| 开始时延 | lockyield 下：**便条 647 ms**（G0 按下 → 第一块缓冲，含 500 ms 长按门限；≤1.0 s ✅），**会议 424 ms**（Enter → 第一块；≤0.8 s ✅）；loop 单步最长 10.5 ms（≤50 ms ✅）。v0.3.1 起开始路径里没有 `Speaker.begin`、提示音和 `Speaker.end`（§4.1 表的前三行）：便条只剩 G0 长按 + `Mic.begin` + 第一块，会议只剩 `Mic.begin` + 第一块；没有重新测。v0.3.2（#31）起 `● REC` 要等第一块没被压住的声音：codec 热时晚约一块（128 ms）出现，冷时最多等 2 s | G-A5 §4.1 ✅ |
| 麦克风增益（v0.3.1 起是 `lib/core/mic_gain.h` 的 `kMicGain{pga, vol, mag}`） | **`{9, 0xC4, 2}`**：PGA +27 dB、ADC 音量 +2.5 dB、不放大，codec 里共 +29.5 dB。手持正常说话预计峰值 −11.9 dBFS（`mic_verify` 在固件自己的增益下推算 −11.96），有声部分 RMS −35.2，噪声底 −73.7 dBFS（2026-10-21 `mic_cal`，§5.4）。v0.3.1 把增益放进 ES8311（REG14 PGA、REG17 ADC 音量，每次 `Mic.begin` 后写入并读回），`magnification` 改成 2（`over_sampling` 1 时不放大）；v0.3.0 的增益是 `{0, 0xBF, 16}`。参考：PGA 是模拟增益，比软件放大略好（+18 dB PGA 底噪只涨 15.4 dB），但 +18 dB 时安静房间的底噪就已经到 0.0030，碰到 WhisperWoof 的 0.003 小块门限（那是 PGA +18 dB 再 ×8；`kMicGain` 下这个房间不滤波的底噪约 0.0004） | G-A10 §5.4 ✅ |
| `CARDPUTER_SILENCE_RMS` | **0.000843**（`mic_cal` 的 `decision.silence_rms`，§5.2 第 6 步：噪声底 −73.7 dBFS 和"1 m 外说话"估计 −49.2 dBFS 的 dB 中点）。默认 0.0005 要调高：`kMicGain` 下这个房间不滤波的底噪约 0.0004（−68 dBFS，比 brief 预计的 0.0006 低），0.0005 只比它高一点。v0.3.0 默认增益下安静房间 100 ms 窗 RMS p50 = 7.4（0.00023） | G-A10 §5.4 ✅ |
| ⚠️ SD 连续写入 EIO（新发现） | **顺序写一个文件时 `write` 会返回 EIO**：请求 ≥8 MB 的 11/11 次失败（位置 0.24–8.4 MB 不等，慢写、勤 fsync 都一样）；2 MB 2/2、4 MB 3/4 正常（第 4 次在 245,760 B 就失败），`!sdbench` 3×4 MiB 正常。像是卡上某个区域或驱动在某些 LBA 上写不进去，和文件大小无关、和簇分配到哪里有关。**5 分钟 9.6 MB 的段几乎必撞；v0.2 outbox 的小文件也可能撞上（⏳ 没测）**。查清之前不要在这张卡上跑 v0.3 录音测试，见 §6 | ✅ 复现命令在 §6 |

## 2. G-A0：v0.2 堆基线（SD 已挂载）✅

`cd firmware/app && pio test -v -e cardputer-adv-test -f 'device/test_runtime_budget'`，跑了 2 次（相隔约 1 小时，中间刷过 hwtest），两次都 PASS，数字完全一样：

```
CPA_RX_DONE got=8192 bad=0
CPA_BUDGET stack_left=5836 heap_free=234836 heap_min=223660 max_block=208884 sent=22 pending=0 bad=1 store_errors=0 missing_glyphs=0
```

- 这是整套 v0.2 固件（屏幕、三个模块、SD 为主存储、往 bad/ 挪过一条）跑完最深路径后的值。
- 开机一次性分配约 38 KB（11 §4.7）：空闲堆约 196 KB（≥150 ✅）；即使 38 KB 全从最大块里切，最大块还有约 170 KB（≥64 ✅）。再加 4 KB 的第 3 个文件名额也够。**M3-A 用开机分配**，"录音时分配"的编译期开关留着当后备即可。
- loop 栈余量 5,836 B（≥4 KB ✅）。

## 3. G-A1：HWCDC 持续吞吐 ✅

### 3.1 做法

- `!xfer <b64|bin> <len> <depth> <txring KB> <ram|sd|sdkeep> [secs]`：设备发出和 app 的 `file.data` 一模一样的行（11 §5.3，`id`/`re`/`fid`/`off`/`n`/`size`/`crc` 都在，1380 B 时一行最长 1,997 B）。bin 是"头行 + N 个原始字节 + `\n`"（audio-capture §7.3）。
- **按 app 的规矩发**：整帧放得下 TX 环才写（11 §5.4），每 20 ms 做一次 `UsbTransport::tick` 同样的 kick + re-arm；TX 环在运行时换成 4/8/16 KB（先等环排空、屏蔽 IN_EMPTY 再换）。
- `depth`：主机保持 N 个请求在途（每收到一帧回一个 `!g`），模拟请求-回复流水线；`0` = 设备只管推。
- 数据源：`ram` = 内存里的固定花样；`sd` = `/sd/cpa_hwtest/xfer.bin`，**每帧 open→lseek→read→close**（音频任务"最多 1 个打开文件、先关再开"的读法）；`sdkeep` = 同一文件只打开一次顺序读。
- 主机每秒发一次 `!ping`，测负载下的回复延迟；每帧校验 CRC-32；设备每秒报一行 `xs`：帧数、等 TX 环的时间、组帧耗时、读卡最长耗时、两个核的忙碌百分比（idle hook 计数对比 500 ms 空载基准）。
- 和 app 的差别：hwtest 在同一个 loop 里读卡、编码、发送；app 读卡在 core 0 的音频任务、编码发送在 core 1，能流水并行。所以本表**不验证 app 层吞吐**，那由 HIL `rec_meeting` 测（11 §5.8）。

### 3.2 编码器的坑（先修的）

第一次跑（RAM，b64 1380，4 在途，4 KB 环）组帧用的是 hwtest 现成的逐位 CRC（`sdt::crc32`）和 `mbedtls_base64_encode`：

| 组帧实现 | 有效 KB/s | 每帧组帧耗时 | core 1 忙 | 等 TX 环 |
|---|---|---|---|---|
| 逐位 CRC + mbedtls base64 | 634 | 2,050 µs | 100% | 0.8% |
| ROM CRC + mbedtls base64 | 633 | 1,700 µs | 88% | 17% |
| ROM CRC + 查表 base64（gate_core，RFC 4648 向量测过） | 615–633 | 255 µs | 39% | 84% |

结论：mbedtls 的 base64 是常数时间实现，1380 B 要约 1.6 ms；逐位 CRC 约 0.35 ms。app 必须用查表 base64 + `esp_rom_crc32_le`，否则传输时 core 1 被吃满，界面和链路都受影响。也说明 sd-card §7 的"拉取 138 KB/s"、以及由它反推的"线速 185–225 KB/s"（audio-capture §7.2）**不是链路上限**：`!sdget` 用的就是这两个慢实现，还是逐块等回复。

### 3.3 结果（10 s 一档，另有 2 档 60 s 浸泡；主表 32 档 108,006 帧 + 读卡对比 4 档 21,638 帧，**CRC 错 0、分帧错 0，ping 500/500 有回复**）

| 编码 | 每帧 B | 在途 | TX 环 | 数据源 | 秒 | 有效 KB/s | 线上 KB/s | 1 小时录音 | ping RTT p50 / p95 / max (ms) | core1 忙 % | 最长帧间隔 ms | 每帧耗时 µs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| b64 | 1380 | 1 | 4 KB | ram | 10 | **449.2** | 644.8 | 4.17 min | 1.1 / 2.1 / 2.1 | 28 | 3.9 | 185 |
| b64 | 1380 | 2 | 4 KB | ram | 10 | **632.6** | 908.0 | 2.96 min | 2.7 / 3.6 / 3.6 | 39 | 2.3 | 256 |
| b64 | 1380 | 4 | 4 KB | ram | 10 | **632.7** | 908.2 | 2.96 min | 3.2 / 3.9 / 3.9 | 39 | 2.3 | 254 |
| b64 | 1380 | 推送 | 4 KB | ram | 10 | **648.8** | 931.3 | 2.89 min | 2.7 / 4.0 / 4.0 | 40 | 2.3 | 255 |
| b64 | 1380 | 1 | 8 KB | ram | 10 | **449.2** | 644.8 | 4.17 min | 1.0 / 2.1 / 2.1 | 28 | 3.2 | 185 |
| b64 | 1380 | 2 | 8 KB | ram | 10 | **631.4** | 906.3 | 2.97 min | 2.5 / 3.5 / 3.5 | 39 | 2.3 | 256 |
| b64 | 1380 | 4 | 8 KB | ram | 10 | **631.4** | 906.3 | 2.97 min | 7.0 / 7.6 / 7.6 | 39 | 2.4 | 247 |
| b64 | 1380 | 推送 | 8 KB | ram | 10 | **651.6** | 935.3 | 2.88 min | 6.8 / 9.0 / 9.0 | 40 | 2.3 | 248 |
| bin | 1380 | 4 | 4 KB | ram | 10 | **793.2** | 875.8 | 2.36 min | 3.4 / 4.1 / 4.1 | 37 | 1.9 | 140 |
| bin | 4096 | 1 | 8 KB | ram | 10 | **702.4** | 727.4 | 2.67 min | 1.4 / 4.7 / 4.7 | 28 | 6.1 | 241 |
| bin | 4096 | 4 | 8 KB | ram | 10 | **846.2** | 876.1 | 2.22 min | 5.7 / 8.1 / 8.1 | 34 | 4.9 | 299 |
| bin | 4096 | 推送 | 8 KB | ram | 10 | **848.6** | 878.6 | 2.21 min | 6.9 / 8.3 / 8.3 | 34 | 4.9 | 300 |
| bin | 4096 | 1 | 16 KB | ram | 10 | **668.4** | 692.1 | 2.81 min | 2.4 / 4.7 / 4.7 | 27 | 6.2 | 241 |
| bin | 4096 | 4 | 16 KB | ram | 10 | **846.4** | 876.4 | 2.22 min | 15.9 / 18.3 / 18.3 | 33 | 4.9 | 330 |
| bin | 4096 | 推送 | 16 KB | ram | 10 | **835.9** | 865.5 | 2.24 min | 15.3 / 17.2 / 17.2 | 33 | 38.7 | 333 |
| b64 | 1380 | 1 | 4 KB | sd | 10 | **203.5** | 292.1 | 9.22 min | 1.1 / 2.1 / 2.1 | 69 | 11.2 | 3790 |
| b64 | 1380 | 2 | 4 KB | sd | 10 | **321.6** | 461.6 | 5.83 min | 5.8 / 6.8 / 6.8 | 100 | 8.4 | 4100 |
| b64 | 1380 | 4 | 4 KB | sd | 10 | **322.2** | 462.6 | 5.82 min | 4.8 / 5.9 / 5.9 | 100 | 7.6 | 4088 |
| b64 | 1380 | 推送 | 4 KB | sd | 10 | **326.6** | 468.8 | 5.74 min | 6.2 / 7.0 / 7.0 | 100 | 6.2 | 4048 |
| b64 | 1380 | 1 | 8 KB | sd | 10 | **201.9** | 289.9 | 9.29 min | 2.0 / 2.6 / 2.6 | 68 | 20.8 | 3789 |
| b64 | 1380 | 2 | 8 KB | sd | 10 | **318.0** | 456.5 | 5.9 min | 5.5 / 6.3 / 6.3 | 98 | 101.8 | 4080 |
| b64 | 1380 | 4 | 8 KB | sd | 10 | **322.4** | 462.8 | 5.82 min | 5.8 / 6.8 / 6.8 | 100 | 6.4 | 4087 |
| b64 | 1380 | 推送 | 8 KB | sd | 10 | **326.8** | 469.1 | 5.74 min | 5.6 / 6.0 / 6.0 | 100 | 8.9 | 4046 |
| bin | 1380 | 4 | 4 KB | sd | 10 | **332.7** | 367.4 | 5.64 min | 5.3 / 6.4 / 6.4 | 100 | 6.5 | 3961 |
| bin | 4096 | 1 | 8 KB | sd | 10 | **400.1** | 414.5 | 4.69 min | 4.6 / 4.7 / 4.7 | 60 | 10.6 | 4626 |
| bin | 4096 | 4 | 8 KB | sd | 10 | **759.7** | 786.7 | 2.47 min | 7.8 / 10.0 / 10.0 | 100 | 5.6 | 5169 |
| bin | 4096 | 推送 | 8 KB | sd | 10 | **767.5** | 794.8 | 2.44 min | 7.4 / 10.0 / 10.0 | 100 | 5.5 | 5130 |
| bin | 4096 | 1 | 16 KB | sd | 10 | **400.0** | 414.4 | 4.69 min | 4.5 / 4.7 / 4.7 | 60 | 10.4 | 4623 |
| bin | 4096 | 4 | 16 KB | sd | 10 | **757.0** | 784.0 | 2.48 min | 8.2 / 9.9 / 9.9 | 100 | 5.6 | 5188 |
| bin | 4096 | 推送 | 16 KB | sd | 10 | **766.4** | 793.7 | 2.45 min | 9.0 / 10.0 / 10.0 | 100 | 5.6 | 5136 |
| b64 | 1380 | 4 | 4 KB | sd | 60 | **321.1** | 461.3 | 5.84 min | 6.0 / 6.8 / 7.0 | 100 | 16.3 | 4104 |
| bin | 4096 | 4 | 8 KB | sd | 60 | **758.1** | 785.1 | 2.47 min | 7.9 / 10.0 / 10.1 | 100 | 5.6 | 5179 |

上表的 sd 源是 2 MiB 的文件。读卡方式对比（4 MiB 文件，20 s 一档，最后一列是单次读卡最长耗时 µs）：

| 编码 | 每帧 B | 在途 | TX 环 | 读法 | 有效 KB/s | 线上 KB/s | 1 小时录音 | ping RTT p50 / p95 / max | core1 忙 % | 每帧耗时 µs | 读卡最长 µs |
|---|---|---|---|---|---|---|---|---|---|---|---|
| b64 | 1380 | 4 | 4 KB | 每块 open/close | **302.0** | 433.6 | 6.21 min | 6.3 / 7.0 / 7.0 | 100 | 4361 | 5263 |
| bin | 4096 | 4 | 8 KB | 每块 open/close | **709.2** | 734.4 | 2.64 min | 8.5 / 10.3 / 10.5 | 100 | 5517 | 5535 |
| b64 | 1380 | 4 | 4 KB | 保持打开 | **631.8** | 907.0 | 2.97 min | 3.6 / 5.4 / 5.6 | 90 | 1769 | 2369 |
| bin | 4096 | 4 | 8 KB | 保持打开 | **846.1** | 876.2 | 2.22 min | 5.8 / 8.7 / 8.8 | 78 | 3197 | 3406 |

**读法**：
1. **HWCDC 线速约 880–935 KB/s**，比之前按 sd-card §7 反推的 185–225 KB/s 高 4 倍。b64 1380 B 的有效吞吐上限约 633 KB/s（有效率 69%），bin 4096 约 846 KB/s。
2. **在途数**：1 个在途是 449 KB/s（每块往返约 3 ms），2 个就到上限。app 的 4 个在途足够。
3. **TX 环**：对吞吐没有影响；环越大，负载下排在帧后面的回复等得越久（4 KB：p95 ≤4 ms；8 KB：≤9 ms；16 KB：≤18 ms）。选 4 KB。
4. **读卡才是 app 的瓶颈**：每块 open→lseek→read→close 在 1380 B 时要约 3.8–4.4 ms（文件越大越慢，lseek 要沿簇链走），所以单核上只有 302–322 KB/s；文件保持打开、顺序读 1380 B 只要约 1.5 ms，又回到链路上限。app 的读卡在 core 0、编码发送在 core 1，可以并行；但只要音频任务每块重新开关文件，上限就在 1380 B / 4 ms ≈ 330 KB/s 附近。
5. **二进制**：链路上快 1.34 倍（RAM），但在 app 的读卡方式下 bin 1380 只比 b64 1380 快 1.03 倍。按 §7.3 规则 b64 ≥320 KB/s → **v0.3.x 不做二进制**。
6. **CPU**：链路跑满时 core 1 约 39%（组帧约 12%，其余主要是 HWCDC 中断：每次最多搬 64 B，按 900 KB/s 算约 1.4 万次/秒 [算]）；core 0 为 0。
7. **扣留**：负载期间主机一直在写（`!g`、`!ping`），没有出现 ADR-0001 §5.2 那种约 1 s 的扣留；最长帧间隔 101.8 ms（1 次）。20 ms 的 re-arm 在负载下每次都发现环里有数据（10 s 约 500 次），这只说明它在跑，不能说明它是否必需。

## 4. G-A5：提示音时序、`lead_ms`、I2C 并发 ✅（音量 👤）

> 2026-10-21 起改为：录音不出声、只供个人使用，v0.3.1 起录音全程不出声；会议确认框保留；屏幕是唯一提示。本节的测量留作记录：I2C 锁（§4.2）的结论不变；`lead_ms`（§4.3）只在有提示音的开始路径上测过，v0.3.1 的无声开始没有重新测（`!sdrec` 的 3 次无声开始是 130–150 ms，150 的余量没了），要用 `cue-silent` 重测；音量（§4.5）不适用。

### 4.1 做法与时序

`!cue <ms> <vol> [memo|meet|silent] [off|nolock|lock|lockyield] [rec_ms]`（`silent` 是 2026-10-21 加的，见 §4.3 末尾）：从空闲状态（麦克风、扬声器都关）开始，在一个和 rec_task 一样的任务里（core 0、优先级 1、6 KB 栈）走 11 §4.2 的开始路径：`Speaker.begin` → `setVolume` → 提示音（便条 1320 Hz 80 ms；会议 660/880/1320 Hz 各 100 ms、间隔 30 ms）→ 等 `isPlaying()` 变假 → `Speaker.end` → `Mic.begin` → 第一块 10 ms 缓冲 → 再录到 1 s。每次 begin 之后把 ES8311 的 8 个寄存器读回来，和 M5Unified 的表比对。同时 loop（core 1）记录自己最长的一步；hammer 模式下 loop 不停地读 TCA8418（CFG 必须保持配置值、事件计数必须是 0、键表必须为空：没人按键）。

每种模式 20 次（`off`、会议 `nolock` 各 5 次），音量 64；另有便条 lock 各 1 次音量 128 / 255。各模式的中位数（相差 ≤1 ms，裸 lock 的 `Speaker.end` 例外）：

| 步骤 | 便条 | 会议 |
|---|---|---|
| `Speaker.begin` | 3.4 ms | 3.4 ms |
| 提示音（到 `isPlaying` 变假） | 80.8 ms | 358.8 ms |
| `Speaker.end` | 5.2–6.2 ms | 5.1–5.2 ms |
| `Mic.begin` | 3.8 ms | 3.8 ms |
| `Mic.begin` → 第一块 10 ms 缓冲 | 48 ms | 48 ms |
| **开始 → 第一块**（lockyield / nolock / off） | **147 ms** | **424 ms** |
| 按键 → 第一块 | 500 + 147 = **647 ms**（≤1.0 s ✅） | **424 ms**（≤0.8 s ✅） |

ES8311 读回：112 次全部和表一致（扬声器 `80B518010010BF08`，麦克风 `80BA18010210BF6A`），begin 全部返回 true。注意 M5Unified 的 ADV 回调不检查 I2C 写是否成功、总是返回 true（📚 `M5Unified.inl:1045,1537`），所以"配置失败"只能靠读回判断，app 想检查也得这么做。

### 4.2 I2C 并发

| 模式（每种 20 次） | 错键 | 读错 / I2C 错 | codec 配置失败 | loop 最长一步 | 音频任务最长等锁 | 开始 → 第一块 最长 |
|---|---|---|---|---|---|---|
| 便条 nolock | 0 | 0 / 0 | 0 | 5.2 ms | — | 148 ms |
| 便条 lock（裸互斥量） | 0 | 0 / 0 | 0 | 14.1 ms | **3,662 ms** | **3,804 ms** ❌ |
| 便条 lockyield | 0 | 0 / 0 | 0 | 10.0 ms | 0.6 ms | 147 ms ✅ |
| 会议 lock（裸互斥量） | 356（1 次，见下） | 0 / 0 | 0 | 9.6 ms | **1,180 ms** | **1,599 ms** ❌ |
| 会议 lockyield | 0 | 0 / 0 | 0 | 10.5 ms | 0.6 ms | 424 ms ✅ |

- **裸互斥量会饿死音频任务**：loop 在 core 1 上放锁后立刻又拿锁，core 0 上等锁的任务被唤醒时锁已经又被拿走了（FreeRTOS 互斥量跨核不保证交接）。最长一次等了 3.7 s。真实 app 的 loop 不会这么密地读 I2C（键盘只在中断时才读，📚 `TCA8418.cpp`），但 11 §4.2 要求每次 loop 都在锁里调 `M5Cardputer.update()`，只要 loop 转得快，同样的情况就可能出现。
- **lockyield（推荐给 M3-A）**：音频任务拿锁前置一个原子 `want` 标志，拿到后清掉；loop 放锁后如果 `want` 为真就 `vTaskDelay(1)`。只在开始和结束录音那几次 begin/end 时触发，平时 loop 不会让出。等价的写法是"`want` 为真时 loop 这一圈跳过键盘读取"，那样 loop 完全不用 delay，但这个变体**没有在真机上测** ⏳，用它的话要在 HIL 里补测。
- **不加 app 锁**也是 0 错（25 次连续读键盘）：M5GFX 的 I2C 每个事务都在端口互斥量里（📚 `M5GFX/src/lgfx/v1/platforms/esp32/common.inl:2278`），所以寄存器级不会交错出错。app 锁的意义在于让 begin/end 整段和键盘读取互斥；有让出就没有代价。
- **会议 lock 那 1 次 356 个"错键"**：只出现在一次运行里，持续约 356 圈 loop，同时 CFG 读回全部正确、I2C 0 错；前几次运行的录音里有一个很响的房间声音（RMS 到 839）。最可能是有人碰了键盘，不像总线错误，但**没法完全排除** ⏳。其余 111 次（含 lockyield 40 次、nolock 25 次）都是 0。

### 4.3 `lead_ms`

`Mic.begin` 后每 10 ms 一格的 RMS（int16），典型一次：`126 144 18 23 26 26 25 21 17 14 16 13 8 11 9 7 …`（峰值 232、296，之后 ≤64）。即前 20 ms 是 codec 冲击，之后约 20–35 的尾巴，60–130 ms 回到底噪（安静时 7–10）。

`lead_ms` 的算法（`gate_cue.lead_ms`）：取 500–1000 ms 各格的中位数为底噪，门限 = max(3 × 底噪, 底噪 + 8)，从第 0 格起第一段连续 5 格（50 ms）都不超过门限的地方就是冲击结束；之后房间里再有声音不算。112 次：**p50 60 / p95 90 / 最长 130 ms**；另有 1 次 200 ms，是冲击尾巴和房间里的一个响声（RMS 839）接在了一起。

**建议 `lead_ms = 150`**（盖住所有纯 codec 的情况）。不选设计默认的 200：录音的第 0 个样本在提示音结束后约 60 ms（`Speaker.end` + `Mic.begin` + 第一块），人听到嘀声再开口大约还要 200–300 ms，`lead_ms` 太大会让 WhisperWoof 跳过第一个字。

> **2026-10-21：v0.3.1 的无声开始没有重新测** ⏳。上面 112 次每次都是 `Speaker.begin`（ES8311 0x0D=0x01，模拟级上电）→ 提示音 → `Speaker.end` → `Mic.begin`，模拟级在 `Mic.begin` 之前已经开着（便条一个 80 ms 的音，会议三个音约 360 ms）；按模式分也看得出来：便条 p50 70 / p95 90 ms，开得更久的会议 p50 50 / p95 80 ms。v0.3.1 从 `Mic.end` 之后的空闲状态（0x0D=0xFC，模拟级关）直接 `Mic.begin`，模拟级和录音同时上电。现有的旁证是 hwtest `!sdrec`（`Mic.end` 后马上 `Mic.begin`，不碰扬声器）的 3 个 WAV（`captures/sd-rec*-rec-*.wav`），按同一个 `gate_cue.lead_ms` 算出来是 **130 / 150 / 140 ms**：p95 90 + 余量的 150 在无声开始下已经没有余量。v0.3.1 两次录音之间模拟级关着的时间比这更长，只会更差，不会更好。
>
> 重测：`$H cue-silent`（`!cue … silent lockyield`，不碰扬声器，20 次，安静房间，→ `captures/g-a5-silent-*.json`，打印 `lead_ms` 的 p50 / p95 / 最长）。p95 加余量还在 150 以内才保留 150，否则改成设计默认的 200（`lib/rec/rec_meta.h` 的 `kLeadMs`）。`!cue` 每次之前只关 300 ms，可以再录几段真机便条（开头 1 s 不说话），用同一个算法核对。
>
> **2026-10-21 补测 ✅**（`mic_cal` 的 lead 阶段，§5.4）：在 `kMicGain {9, 0xC4, 2}` 下开 5 个 2.5 s 的无声会议，同一个 `gate_cue.lead_ms` 算法得到 **90 / 80 / 70 / 80 / 90 ms**，`recommend_lead` 100 ≤ 150，`kLeadMs` 保持 150；`mic_verify` 那段（重启后的第一段）是 100 ms。`cue-silent` 没有跑。
> 冲击比上面记的大得多：`kMicGain` 下前 150 ms 峰值约 4,500（−17 dBFS），150 ms 之后最大不超过 81。它不随 PGA 变（同样 0 dB 数字增益：U0 3,604、S10 3,500、N27 3,283），随 REG17 变（S6 多 +12 dB 数字增益：13,468）；在 v0.3.0 的 ×8 下峰值 28,200 / 28,680（×8 之前约 3,500），lead 260 ms。所以冲击出现在 PGA 之后。上面"峰值约 300"是先放提示音的路径。以后调高 REG17 或 `magnification`，要重测 `lead_ms`。
>
> **2026-10-21（v0.3.2，#31）**：上面的测量都是 codec 已经"热"的开始。codec 断电再上电后（或软复位 REG00 = 0x1F 后），它会先把输出压成 0 约 2^14 个时钟帧（16 kHz 下 933–995 ms），之后才有声音，而且**没有冲击**（峰值 642，稳态 628）。v0.3.2 开机时先预热 codec，开始录音时丢掉被压住的块、从第一个有声音的样本写起，所以 `lead_ms` 从写下的第一个样本算：codec 热时第一块就是冲击，150 照旧够用；冷时没有冲击，150 只是多跳过一点。`kLeadMs` 不变。

### 4.4 判定

| 条件 | lockyield | 裸 lock |
|---|---|---|
| loop 单步 ≤50 ms | 10.5 ms ✅ | 14.1 ms ✅ |
| 20 次里 0 错键、0 次 codec 配置失败 | ✅ | 会议 1 次错键（见上） |
| 等锁不饿死（≤50 ms） | 0.6 ms ✅ | 3,662 ms ❌ |
| 便条开始 ≤1.0 s | 647 ms ✅ | 4,304 ms ❌ |
| 会议开始 ≤0.8 s | 424 ms ✅ | 1,599 ms ❌ |
| **结论** | **通过** | 不通过 |

### 4.5 👤 剩下的：用耳朵定音量（约 2 分钟）

> **不适用**（2026-10-21）：v0.3.1 起录音全程不出声，没有提示音可听，不用做。下面的步骤留作记录。

1. `cpa pause 900`；`cd firmware/hwtest && ~/.local/bin/pio run -e cardputer-adv -t upload`。
2. `uv run --no-project --with pyserial==3.5 python host/hwtest_host.py cue-listen --vols 32,64,128,192,255`：每个音量依次放便条开始音和会议开始音，终端先打印"now playing …"，间隔 2 s。
3. 设备放在口袋里和桌上各听一遍，选**口袋里也能听见、桌上不刺耳**的最低一档（可以用 `--vols 48,64,96` 再细分）。顺便确认会议的三音在安静的会议室里不算失礼。
4. 把选定的音量写进本节，作为 M3-A 的编译期常量；然后按 §7 刷回 v0.2 并 `cpa pause 1`。

## 5. G-A10：电平 ✅ 与中英混说准确率 👤（步骤）

### 5.1 agent 已测的参考：安静房间底噪（各 5 s，只留数字）

| magnification / PGA | 100 ms 窗 RMS p10 / p50 / p90 | 整段 RMS（float，含开头冲击） | 峰值 |
|---|---|---|---|
| 16 / 0 dB（M5Unified 默认） | 6.7 / 7.4 / 8.5 | 0.00040（−68.0 dBFS） | 488（开头冲击） |
| 32 / 0 dB | 13.0 / 14.7 / 17.8 | 0.00081 | 1,040 |
| 16 / +18 dB（PGA 6） | 34.2 / 43.8 / 59.5 | 0.00304 | 3,504 |
| 16 / +30 dB（PGA 10） | 135.6 / 163.6 / 262.5 | 0.00919 | 11,072 |

- 软件放大 ×2，底噪正好 ×2；PGA +18 dB，底噪 ×5.9（+15.4 dB）；+30 dB 时 ×22（+26.9 dB）。所以 PGA 比 magnification 的信噪比好约 3 dB，**要加增益优先加 PGA**。
- PGA +18 dB 时，没人说话的房间整段 RMS 已经是 0.0030，等于 WhisperWoof 的小块门限：静音段会被当成有声送去转写。这正是 `CARDPUTER_SILENCE_RMS` 要单独定的原因。

### 5.2 需要用户做的：HIL `mic_cal` 标定和 `mic_verify` 核对（v0.3.1 起，约 25 分钟）

> 2026-10-21 起替代原来的 hwtest 步骤。原来的做法只调 magnification 和 PGA，不碰 REG17，结果写进 `audio_io.h` 的 `kMicMagnification`，这个常量 v0.3.1 已经删掉。hwtest 的 `mic-capture` 只留作后备。依据是 mic gain brief §5–§7。

**做什么**：Mac 内置扬声器放参考声音（1 kHz 的 −20 / −32 dBFS 两段音，`say` 念的一句英文和一句中文），Cardputer 放在旁边录。脚本先用 `debug.mic` 依次换 11 档增益（mic gain brief §5 的 B1…B2），每档录一个约 27 s 的会议，由 HIL 自己的临时 cpad 拉进临时目录测量，然后按 brief §7 选出要发布的增益。最后在选定的增益下录 5 段 2.5 s 的静音会议，测开头冲击（`lead_ms`），再重启设备，去掉 `debug.mic`。录音只在临时目录里，报告里只有数字，跑完连同临时目录一起删除，**不会进 `~/CardputerNotes`，也不会进 WhisperWoof**。

**准备**
- 安静的房间。Cardputer 插着卡，平放在 MacBook 内置扬声器旁边，跑的时候不要动它。
- Mac 默认输出是内置扬声器、没有静音。音量不用改：脚本只读，不改；不对就拒绝运行。会外放约 7 分钟的声音。
- 你自己的 cpad 在运行，而且**没有暂停**（原因见第 3 步）。

**步骤**（命令都在仓库根目录执行）
1. `cpa status --json`：`recordings.on_device` 必须是 0，`pulling` 必须是 0。不是就等 cpad 拉完。卡上还有东西的话，HIL 的临时 cpad 会把它拉进临时目录然后从卡上删掉。
2. 刷测试固件：`cd firmware/app && ~/.local/bin/pio run -e cardputer-adv-test -t upload && cd ../..`。`pre_upload` 会自己 `cpa pause 120`。
3. `cpa pause 1`，等 `cpa status` 显示 `link: ready`，并且 `on_device` 仍是 0。**跑之前 cpad 必须是恢复状态**：preflight 要靠它确认卡是空的，暂停中的 cpad 看不到卡，preflight 会拒绝运行。所以"动设备前先 `cpa pause`"的习惯在这里要反过来。
4. 标定：`uv run --project daemon python tools/hil/run_e2e.py --scenarios mic_cal --report "$TMPDIR/mic-cal.json"`，约 10–15 分钟（timeout 1200 s）。
   - preflight 先查输出设备和 `cpa status`，然后自己执行 `cpa pause 1800`，等串口空出来。
   - 不满足条件时退出码是 2，并打印原因。
5. 看结果：
   - `$TMPDIR/mic-cal.mic_cal.md`：每档的表格和结论；
   - `$TMPDIR/mic-cal.mic_cal.data.json`：全部数字；
   - `$TMPDIR/mic-cal.mic_cal.json`：各项检查。
   - 通过的条件：所有检查都是 ok，包括 `sweep_valid`、`decision`、`headroom_n33_n36_clip_free`、`lead_measured`，以及 `card_empty` 等卡清空的检查。`decision.ok` 是 false 时不改常量，把 `decision.problems` 记下来。
6. 定常量（一次提交）：
   - 把 `decision` 的 `pga`、`vol_hex`、`mag` 写进 `firmware/app/lib/core/mic_gain.h` 的 `kMicGain`。
   - 同一次提交里改 `test/core/test_mic_gain` 的 `test_the_placeholder_keeps_v030`：它按设计钉住占位值 `{0, 0xBF, 16}`，常量一改它就会失败。然后跑 `~/.local/bin/pio test -e native -f core/test_mic_gain`。
   - `lead.keep_150` 是 false 时，把 `lib/rec/rec_meta.h` 的 `kLeadMs` 改成 `lead.recommended`。
   - 把这些数填进 `docs/releases/v0.3.1.md` 的 TBD。
   - WhisperWoof 的 `CARDPUTER_SILENCE_RMS` 用 `decision.silence_rms`。
7. 核对（brief §6.7）：
   - 先按第 2、3 步重新刷一次测试固件，`cpa pause 1`，再等到 `link: ready` 并且 `on_device` 是 0。
   - 然后跑：`uv run --project daemon python tools/hil/run_e2e.py --scenarios mic_verify --report "$TMPDIR/mic-verify.json"`，约 3 分钟。
   - 它先从 `mic_gain.h` 读出 `kMicGain`。然后用 `debug.mic` 录 B1，用来摆好参考音量；重启，去掉 `debug.mic`；最后在固件自己的 `kMicGain` 下录一段。
   - 通过的条件：`kmicgain_verified` 和卡清空的检查都是 ok。`kmicgain_verified` 要求：
     - 相对 B1 的增益在名义值 ±1.5 dB 以内；
     - 用音和用人声测出的增益相差不超过 1 dB；
     - 没有削波；
     - `mag` 2 时样本步长是 1（16 位都用上了）；
     - 推算的手持峰值在 −14 到 −10 dBFS 之间。
   - 数字在 `$TMPDIR/mic-verify.mic_verify.json` 的 `verify` 里。
8. 刷回正式版：`cd firmware/app && ~/.local/bin/pio run -e cardputer-adv -t upload && cd ../..`，然后 `cpa pause 1`，等 `cpa status` 显示 `link: ready` 和 `fw=cpa-fw/0.3.1`。不管成功还是失败，`mic_cal` / `mic_verify` 跑完时设备都还在测试固件上，你的 cpad 也最多还暂停 30 分钟，runner 最后一行会提醒这件事。
9. 建议再做一件事：用正式版、用平时的音量手持录一条便条，确认峰值在 −12 dBFS 左右（brief §8：说话音量只测过一次）。

**跑失败了怎么办**
- 任何失败都包括在内：`afplay` 出错、某一步超时、整个场景超时、设备报错。场景在报错之前会先重启设备（去掉 `debug.mic`），再让 HIL 的临时 cpad 把卡上的东西拉进临时目录并删掉，最多等 120 s（`DRAIN_S`）。
  - 报告里这个场景的 `error` 末尾是 `[cleanup: rebooted … card emptied …]`：卡已经空了，`debug.mic` 也已经去掉。修好原因后可以直接从第 3 步重跑；不重跑就做第 8 步。
- `error` 里写着 `cleanup FAILED`：卡上可能还留着一段标定录音，设备在重启之前也可能还在用某一档扫描增益。**先不要 `cpa pause 1`**，否则你的 cpad 会把这段录音拉进 `~/CardputerNotes/recordings`，WhisperWoof 也会去转写它。按顺序做：
  1. 马上 `cpa pause 3600`。preflight 的暂停从开始跑算起 30 分钟就到期。
  2. 把设备断电再上电：侧面开关拨到 OFF，拔掉 USB，等 5 s，再插上、拨回 ON。重启会去掉 `debug.mic`；如果正在录，那一段在开机时会被恢复出来。
  3. 跑 `uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_memo --report "$TMPDIR/drain.json"`。`rec_memo` 的第一步会一直等到 HIL 的临时 cpad 把卡拉空，拉下来的东西只在它的临时目录里。
  4. 确认 `$TMPDIR/drain.rec_memo.json` 里的 `card_empty` 是 ok（卡已经空了），然后再做第 8 步。
- 中英混说准确率（MER，原 G-A10 的后一半）不在 `mic_cal` 里。要不要做、算不算阻塞，由你决定（brief §8 建议改成不阻塞的人工项）。要做的话，用正式版手持录一条便条，朗读 §5.3 的段落，用 WhisperWoof 转写，再用 `firmware/hwtest/host/hwtest_host.py mer` 和 Mac 同时录的那一份比。

### 5.3 朗读段落（约 60 s，中英混说，本项目自拟）

> 今天下午我们和 design team 开了一个 sync meeting，主要讨论 recording pipeline 的进度。第一个问题是 transfer speed：在 USB 上用 base64，一个小时的录音大概几分钟就能传完，所以先不做 binary mode。第二个问题是 battery life，大家希望录音的时候屏幕只是调暗，不要完全关掉，这样随时能看到 REC 的标志。Action items 有三个：小王负责更新 WhisperWoof 的 pull request，Alex 在下周一之前把 SD card 的 bug 查清楚，我来写一份 summary 发给大家。最后提醒一下，下次 meeting 改到星期四上午，地点还是 Zoom，请大家提前看一下 design doc。

### 5.4 结果：2026-10-21 的 `mic_cal` 和 `mic_verify` ✅

agent 独占设备跑（测试固件 `cpa-fw/0.3.1`，插卡），按 §5.2 的步骤。Mac 输出是 MacBook Pro Speakers、音量 69、没静音（只读，没改）。B1 探测一次就落在窗口里（音 A −44.57 dBFS，参考音不用缩放），参考音 14.1 s。录音只在 HIL 的临时目录里，报告只有数字。复跑和回归的记录见 07 号文档 §6.9。

**`mic_cal`**（12:21:43–12:26:55，8/8 项检查 ok）。ΔG 都相对 B1；预计峰值 = −23.7 + ΔG，预计 RMS = −47.0 + ΔG（两段参考录音，brief §0）；SNR = 预计 RMS − 噪声底。

| 档 | PGA | REG17 | mag | 名义 ΔG | 音 A（dBFS） | ΔG | 人声 ΔG | 噪声底（高通，dBFS） | 削波 | 步长 | 开头冲击峰值 | lead（ms） | 预计峰值 | 预计 RMS | SNR（dB） |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| B1 | 0 | 0xBF | 16 | 0.00 | −44.57 | +0.01 | +0.01 | −76.5 | 0 | 8 | 28,200 | 260 | −23.69 | −46.99 | 29.56 |
| U0 | 0 | 0xBF | 2 | −18.06 | −62.49 | −17.91 | −18.04 | −94.7 | 0 | 1 | 3,604 | 190 | −41.61 | −64.91 | 29.82 |
| S6 | 6 | 0xD7 | 2 | +11.94 | −32.10 | +12.48 | +12.42 | −73.1 | 0 | 1 | 13,468 | 240 | −11.22 | −34.52 | 38.61 |
| S7 | 7 | 0xD1 | 2 | +11.94 | −31.88 | +12.70 | +12.55 | −73.2 | 0 | 1 | 9,580 | 200 | −11.00 | −34.30 | 38.92 |
| S8 | 8 | 0xCB | 2 | +11.94 | −32.01 | +12.58 | +12.48 | −73.4 | 0 | 1 | 6,786 | 100 | −11.12 | −34.42 | 39.02 |
| S9 | 9 | 0xC5 | 2 | +11.94 | −32.30 | +12.28 | +12.26 | −72.1 | 0 | 1 | 4,920 | 80 | −11.42 | −34.72 | 37.42 |
| S10 | 10 | 0xBF | 2 | +11.94 | −32.31 | +12.27 | +12.27 | −73.2 | 0 | 1 | 3,500 | 90 | −11.43 | −34.73 | 38.51 |
| N27 | 9 | 0xBF | 2 | +8.94 | −35.33 | +9.25 | +9.26 | −73.1 | 0 | 1 | 3,283 | 100 | −14.45 | −37.75 | 35.31 |
| N33 | 10 | 0xC5 | 2 | +14.94 | −29.24 | +15.34 | +15.35 | −59.9 | 0 | 1 | 4,903 | 60 | −8.36 | −31.66 | 28.26 |
| N36 | 10 | 0xCB | 2 | +17.94 | −26.34 | +18.24 | +18.28 | −64.5 | 0 | 1 | 6,757 | 90 | −5.46 | −28.76 | 35.75 |
| B2 | 0 | 0xBF | 16 | 0.00 | −44.59 | −0.01 | −0.01 | −76.5 | 0 | 8 | 28,680 | 260 | −23.71 | −47.01 | 29.47 |

- **有效性**（brief §7.1，全过）：B1 和 B2 差 0.02 dB（≤1）；U0 −17.91 dB，应为 −18.06 ± 0.5；每档比名义值高 +0.30 到 +0.76 dB（≤1.5）；音和人声的 ΔG 最多差 0.15 dB（≤1）；N33、N36 也没有一个样本削波。
- **分法**（§7.3）：+30 dB 的 5 种分法 SNR 37.4–39.0 dB，最好的是 S8（39.02）。S10 的 38.51 在 1 dB 以内，所以取 S10（PGA 10，REG17 不加）。分法之间只差约 1.6 dB，证实"总增益一样时，怎么分对音质几乎没有影响"。
- **微调**（§7.4）：S10 预计峰值 −11.43，round(2 × (−12 + 11.43)) = −1，要往下调，所以降一级 PGA；PGA 9 时预计 −14.43，round(2 × 2.43) = +5，REG17 = 0xBF + 5 = **0xC4**，预计峰值 **−11.93 dBFS**（窗口 −14 到 −10）。实测 PGA 9 → 10 是 3.02 dB（N27 对 S10），S9 和 S10 只差 0.01 dB，推算可靠。不微调的 `{10, 0xBF, 2}`（−11.43）也在窗口里，两档 SNR 一样。
- **结果**：`kMicGain {9, 0xC4, 2}`，预计峰值 −11.93、有声 RMS −35.23、噪声底 −73.74 dBFS（S10 的噪声底减 0.5 dB），`CARDPUTER_SILENCE_RMS` 0.000843。两个独立的复算（不用仓库代码）得到同一档。
- **lead 阶段**（§7.5）：在 `{9, 0xC4, 2}` 下 5 个 2.5 s 的无声会议，lead 90 / 80 / 70 / 80 / 90 ms → `recommend_lead` 100 → `kLeadMs` 保持 150（§4.3）。这 5 段的噪声底（高通）−73.8 到 −74.2 dBFS，不滤波 −67.3 到 −68.7 dBFS（12–14，约 0.0004）。
- 记录：N33 的开头底噪偏高（高通 p50 33，−59.9 dBFS），N36 也有一点（19.5），是那时房间里的声音，不是增益造成的；它们不参与决定。这个房间的底噪比 brief 预计的（约 −64 dBFS、0.0006）低约 10 dB。

**`mic_verify`**（改常量、重新刷测试固件后，12:34:16–12:35:17，5/5 项检查 ok）：B1 探测音 A −44.52 dBFS；重启后不发 `debug.mic`，按固件自己的 `kMicGain` 录一段：ΔG **+11.74 dB**（名义 +11.44），人声 +11.83 dB；推算峰值 **−11.96 dBFS**，有声 RMS −35.26，噪声底（高通）−74.26 dBFS；0 个削波；步长 1；lead 100 ms。

**还没做**：§5.2 第 9 步（用正式版、平时的音量手持录一条便条确认峰值），和中英混说准确率（MER）。

> **2026-10-21（v0.3.2，#31）**：`mic_cal` 和 `mic_verify` 的各段都是 codec 已经"热"时录的：设备一直插着 USB，ESP32 重启和刷机都不会让 codec 变冷（#31 的复现），07 号文档 §6.9 也记着这些段的开头都不是全 0。所以上面的增益、底噪和 lead 都不受开机后那 1 秒 0 的影响，不用重做。开机后的那段 0 在 v0.3.2 由开机预热和"等到有声音才开始"处理（11 号文档 §4.2），HIL `rec_cold_start` 用测试固件的 `debug.codec` 复现。以后再跑 `mic_cal`，第一段之前不用特意预热：开机预热已经做了。


## 6. ⚠️ 新发现：SD 卡上单个文件写到约 8 MB 就 EIO（M3-A 的 9.6 MB 段会撞上）✅

`!xfermk <KB> [pace_ms] [sync_KB]` 每次 `write` 16 KB 花样数据到 `/sd/cpa_hwtest/xfer.bin`（20 MHz，打过补丁的 SD 库，v0.2 那张卡）：

| 请求大小 | 写法 | 结果 |
|---|---|---|
| 2,048 KB | 全速 | 2/2 正常（1.3 s） |
| 4,096 KB | 全速 | 3/4 正常（2.6 s）；最后一次（代码重构后复验，每 1 MB fsync）在 **245,760 B** 就 EIO |
| 9,375 KB（= 9,600,000 B，一个 5 分钟段） | 全速，`O_TRUNC` 覆盖旧文件 | 4/4 **EIO**，分别写到 999,424 / 2,080,768 / 8,372,224 / 8,372,224 B |
| 8,192 KB | 全速，`O_TRUNC` | 2/2 **EIO**，8,372,224 / 8,355,840 B |
| 9,375 KB | 每 16 KB 后停 50 ms（约 280 KB/s，30 s） | **EIO**，8,372,224 B |
| 9,375 / 8,176 KB | 先删文件再写 | **EIO**，6,914,048 / 8,355,840 B |
| 9,375 KB | 每 1 MB fsync / 每 32 KB fsync | **EIO**，8,339,456 / 8,372,224 B |
| `!sdbench`（3 个 4 MiB 文件，512 B / 4 KB / 32 KB 块，写完读回比对） | 紧接在一个 4 MiB 文件之后 | 全部写对、读对 |

- `errno = 5`（EIO），也就是 FatFs 报了 `FR_DISK_ERR` 或 `FR_INT_ERR`。跟写入速度、fsync 节奏无关；失败位置随簇分配变化（最早两次的 1 MB、2 MB 是旧版 `!xfermk`，那时还不报 errno）。用 `O_TRUNC` 覆盖时 FatFs 会从刚释放的簇重新分配（📚 `remove_chain` 把 `last_clst` 拨回），所以 4 次都停在同一个 8,372,224 B；先删文件再写、或文件起点变了，失败位置就跟着变。
- 推测：卡上某个区域写不进去，或者打过补丁的驱动在某些 LBA / 某种多块写上出错。但 `!sdbench` 紧接着写过的那一段又是好的，所以也可能和写入方式（16 KB 一次的 CMD25）有关。⏳ 没有定位，需要：在 `third_party/SD` 的 `sd_diskio` 里记下失败的 LBA、块数和卡的回应（不在本包范围）；在 Mac 上对这张卡跑"磁盘工具 → 急救"；换一张卡复测。
- 卡的状态：结束后 `!sdinfo` 报 15,350,775 个空闲簇（只用了 9 个：v0.2 的文件和目录），FSINFO 记账没有漏簇；但这个数来自 FSINFO，不是逐表扫描，FAT 本身要用急救才能确认。卡上只剩 v0.2 的内容（§0）。
- **对 M3-A / M3-H 的影响**：5 分钟一段（9.6 MB）几乎一定会在这张卡上 EIO，按 11 §4.3 走"SD 锁死"，录音被截断；改短段长**不能**规避（4 MB 也失败过）。HIL `rec_meeting`（30 s 段）和 `rec_rotation`（1 分钟段）不一定碰得到。v0.2 的 outbox 每条只写一个小文件，会不会撞上没测 ⏳；v0.2 目前在这张卡上工作正常（`cpa status`：`store=sd`）。
- 复现：hwtest 刷好后 `printf '!xfermk 9375\n'` 发给设备（或 `host/hwtest_host.py xfer-sd --file-kb 9375`），看 `{"t":"xfermk",…,"wrote":…,"errno":5}`；之后 `gate-clean`。

## 7. 复现

```bash
cpa pause 5400                                   # lsof /dev/cu.usbmodem* 应为空
# G-A0（v0.2 测试固件，插卡）
cd firmware/app && ~/.local/bin/pio test -v -e cardputer-adv-test -f 'device/test_runtime_budget' | grep CPA_BUDGET
# hwtest 闸门固件
cd ../hwtest && ~/.local/bin/pio test -e native && ~/.local/bin/pio run && ~/.local/bin/pio run -e cardputer-adv -t upload
H="uv run --no-project --with pyserial==3.5 python host/hwtest_host.py"
$H xfer-sweep                 # G-A1 主表，约 8 分钟 -> captures/g-a1-*.json
$H xfer-sd --file-kb 4096     # G-A1 读卡方式 -> captures/g-a1-sd-*.json
$H cue-sweep                  # G-A5，112 次，约 6 分钟，会响 112 次提示音 -> captures/g-a5-*.json
$H cue-listen                 # G-A5 音量（人）；v0.3.1 起不适用，不用跑
$H cue-silent                 # G-A5 lead_ms，v0.3.1 的无声开始，20 次 -> captures/g-a5-silent-*.json（§4.3）
$H mic-capture --mag 16 --pga 0 --secs 60 --label 30cm   # G-A10 的旧做法，v0.3.1 起只作后备；现在用 HIL mic_cal（§5.2）
$H mer --ref ref.txt --hyp dev30.txt mac30.txt           # G-A10 MER，不用设备
$H gate-clean                 # 只删闸门写的文件
# 主机侧单元测试
uv run --no-project --with pytest --with pyserial==3.5 python -m pytest host/tests -q
# 收尾：刷回 v0.2 release
cd ../app && ~/.local/bin/pio run -e cardputer-adv -t upload && cpa pause 1 && cpa status
```

原始数据（`firmware/hwtest/captures/`，gitignore，要入库请复制到 `docs/gates/captures/`）：

| 文件 | 内容 |
|---|---|
| `g-a0-test_runtime_budget-20261020.log` | G-A0 的 `pio test -v` 输出 |
| `g-a1-20261020-232719.json` | G-A1 主表（32 档，每档有 `xs` 每秒数据、ping RTT 全部样本） |
| `g-a1-sd-20261020-233705.json` | 读卡方式对比（4 MiB 文件）；`g-a1-sd-20261020-233027.json` 是同样的对比，但文件只写出了 0.95 MB（EIO，§6） |
| `g-a1-enc-*-20261020.json` | §3.2 三种组帧实现 |
| `g-a5-20261020-234339.json` | G-A5 全部 112 次（每次带 100 格 RMS / 峰值）、分组汇总、两种锁的判定 |
| `g-a10-noise-m*-p*-20261020.json` | §5.1 底噪（只有数字） |
