[English](0001-toolchain.md) · **中文**

# ADR-0001 · 固件工具链

- **状态**：已决定（2026-10-19，G2 真机实测）
- **决定**：**A** = `espressif32@6.10.0`（Arduino 2.0.17 / ESP-IDF 4.4.7 / GCC 8.4），应用代码用 `-std=gnu++17`
- **未采用**：B = pioarduino `54.03.21-2`（Arduino 3.2.1 / ESP-IDF 5.4.2 / GCC 14.2）。四项检查里第 1 项（CDC 往返）没通过
- **测试固件**：`firmware/hwtest/`（env `cardputer-adv` = A，`cardputer-adv-p54` = B，`cardputer-adv-gnu17` = A 的 gnu++17 编译检查）
- **测试脚本**：`firmware/hwtest/host/g2_run.py`；原始报告和 WAV 在 `firmware/hwtest/captures/g2-*`（已 gitignore）

## 1. 背景

| | A（基线） | B（候选） |
|---|---|---|
| 平台 | `espressif32@6.10.0` | `https://github.com/pioarduino/platform-espressif32/releases/download/54.03.21-2/platform-espressif32.zip`（2,625,441 B，发布于 2025-10-07） |
| Arduino / IDF / GCC | 2.0.17 / 4.4.7 / 8.4.0 | 3.2.1 / 5.4.2（`v5.4.2-25-g858a988d6e`）/ 14.2.0 |
| esptool | 4.5.1 | 5.0.2 |
| 优点 | hwtest 已经跑通；PlatformIO 官方维护 | IDF 5.4 还在维护期；出厂固件也是 IDF 5.4 |
| 缺点 | IDF 4.4 已 EOL，没有安全更新 | pioarduino 实际只有一个维护者（供应链风险）；Arduino 2→3 有破坏性 API 变化 |

不考虑 IDF 5.5 系的 core：在 ADV 上录音全是 0（ESP-IDF #18621）。

**G2 判定规则**（06）：B 必须四项全过才用 B，否则用 A。
1. 握手后往返 ≤100ms（p95，保留 20ms TX kick）
2. 扬声器响过之后，麦克风 RMS 不为 0，而且不是一条平线
3. SD 写入 1MB 后改名成功，或者没插 SD 卡
4. M5Cardputer 1.2.0 / M5Unified 0.2.26 / M5GFX 0.2.33 都能编译

另外顺带检查：A 用 `-std=gnu++17` 能不能编译（05 §11 R8）。

## 2. 方法

- 两套工具链刷的是同一份 `src/main.cpp`，没有用任何 `#if` 区分 core。B 不需要改代码就能编译。
- 在 hwtest 里加了几条命令：`!beep`（`Mic.end()` → `Speaker.begin()` → 1kHz 响 300ms → `Speaker.end()` → `Mic.begin()`）、`!sdtest`（SPI 引脚 SCK40/MISO39/MOSI14/CS12，写 1MB 到 `/cpa_test.tmp`，关闭后改名成 `/cpa_test.bin`，重新打开核对大小，再删除）、`!reboot`（`esp_restart()`）。`hello` 里现在也会带上 `arduino` 和 `idf` 版本号。
- `g2_run.py` 的流程：先用安全方式打开串口（只走 03 §2 的序列，不碰 RTS/DTR）。然后发 `!info` 握手，2 秒重试一次，最多 3 次。握手后连续做 N 次 `!info` 往返，再依次跑 `mic, beep, mic, sdtest`，最后再做一次往返。
- **重新开机**：用 `--reboot`，也就是让固件自己执行 `esp_restart()`。过程中不按 G0，也不拨控制线。
- **回复卡住的判定**：发出请求 10 秒内没收到回复，就再写一行 `nudge`。如果回复这时才出来，就记为 `nudged`。另外用 `hello.up_ms` 看设备是什么时候生成这条回复的，借此区分是 TX 被扣住，还是 RX 丢了。
- p95 用最近秩法。10 个样本时，p95 就等于最大值。

复现：
```bash
cd firmware/hwtest
~/.local/bin/pio run -e cardputer-adv -t upload          # B 用 -e cardputer-adv-p54
cd host && uv run --with pyserial python g2_run.py --label A --reboot --rtt 30 --steps mic,beep,mic,sdtest
```

## 3. 测量结果（2026-10-19，同一台 Cardputer ADV，没插 SD 卡）

A 一共 14 个会话，其中 11 个是刚开机的会话。另外还有 1 个会话没算进去：那一次芯片停在 esptool stub 里，固件根本没在运行，见 §5.3。B 用规定的 kick 跑了 14 个会话，其中 7 个是刚开机的会话。

| 检查项 | A：espressif32@6.10.0 | B：pioarduino 54.03.21-2 |
|---|---|---|
| **4. 三个库能否编译** | ✅ 不用改代码 | ✅ 不用改代码 |
| A 用 gnu++17 编译 | ✅ 41 个 C++ 编译单元（含 M5 库）只带 `-std=gnu++17`，从 compiledb 核实 | 不适用（GCC 14 默认 gnu++2b） |
| 静态 RAM / Flash | 93,504 B / 573,337 B | 96,508 B / 664,130 B |
| 开机后空闲堆 / 最大连续块 | 277,488 / 245,748 B | 254,996 / 188,404 B（少约 22KB / 56KB） |
| 握手 | 14/14 第一次就成功，p50 57.7ms，最长 60.1ms | 14/14 第一次就成功，p50 57.6ms，最长 60.1ms |
| 正常往返 p50 / p95 / max | 57.9 / 60.0 / 60.2ms（n=240） | 57.5 / 59.9 / 60.2ms（n=328） |
| **往返阶段回复卡住** | **0 次**（240 次往返，11 个刚开机的会话） | **7 次**：**7/7 个刚开机的会话**，每个会话卡 1 次；其余 7 个会话（150 次往返）0 次 |
| 卡住多久 | — | 每次约 10.1 秒，也就是等到主机下一次写入才出来（设备在 50–60ms 内就生成了回复，所以是 TX 被扣住） |
| 卡住的时间点 | — | 开机后约 3.4–4.1 秒（`up_ms`），也就是握手后 0.2–1.3 秒；第一次跑的那个会话是 8.6 秒 |
| **1. 字面闸门**（握手加 10 次往返，p95 ≤100ms） | ✅ 14/14 通过 | ❌ **5/7 个刚开机的会话不通过**（p95 约 10.1 秒）；另外 2 个是在第 11 和第 13 次往返才卡住 |
| 麦克风底噪（响之前） | rms 6–39，峰峰值 48–1168，不同取值 7–120 个（9/9） | rms 6–41，峰峰值 56–896，不同取值 8–95 个（7/7） |
| **2. 响过之后的麦克风** | ✅ rms 10–28，峰峰值 112–672，不同取值 15–76 个（9/9） | ✅ rms 12–18，峰峰值 384–424，不同取值 43–50 个（7/7） |
| `Speaker.begin()` / 之后的 `Mic.begin()` | 收到的 7 条回复全是 true（另 2 条回复被扣住，但随后的录音正常） | 7/7 返回 true |
| **3. SD** | `no_sd`（没插卡） | `no_sd`（没插卡），按规则算通过 |
| 长操作后回复卡住 | `!beep` 回复有 2/9 次超过 5 秒才出来；`!sdtest` 之后的那次往返有 1/14 次 2 秒内没回复 | `!beep` 回复 0/7 次 |

麦克风在两套工具链上表现一样：样本都是 8 的整数倍，安静时 20–48% 的样本为 0。`!beep` 之后的前 100ms 有一段 codec 重启的冲击（rms 约 48），200ms 以后回到环境噪声水平。

## 4. 决定

**用 A。** B 第 1 项不通过：每次开机后不久都会有一条回复被扣住约 10 秒，规定的 20ms kick 放不出来。B 的第 2、4 项通过；第 3 项是因为没插卡才算通过，SD 写入本身并没有测到。

## 5. 后果

### 5.1 构建配置
- `firmware/app/platformio.ini` 写 `platform = espressif32@6.10.0`，并加上 `build_unflags = -std=gnu++11` 和 `-std=gnu++17`（05 §3 原文，已验证能编译）。R8 不会发生。
- IDF 4.4 已经 EOL，拿不到安全修复。v0.1 不开 WiFi/BLE，这个风险可以接受。v0.3 加无线功能之前要重新评估，目标是 C（Arduino ≥3.3.9，HWCDC 已修，PR #12606）。不过 C 基于 IDF 5.5，要先确认 #18621（录音全 0）已经修好。

### 5.2 HWCDC：kick 有必要，但在 A 上也不够（交给 WP3/WP4/WP5）
- 两套 core 的 `isCDC_Connected()` 都是只要 `connected` 为 true 就直接返回，所以 kick 在这种状态下什么也不做。
- 在 B 上试过更强的 kick：每 20ms 用 `usb_serial_jtag_ll_ena_intr_mask(IN_EMPTY)` 重新打开中断。结果**没有改善**：4/4 个刚开机的会话还是卡住，`!beep` 回复有 3/4 次被扣。这段实验代码已经删掉。根因在半天的时限内没查清。
- 在 A 上，回复被扣住只出现在设备阻塞了几百毫秒之后（`!beep`、`!sdtest`），而且主机下一次写入就能放出来。由此推出：
  - daemon 每 5 秒一次的 ping 正好把最坏延迟限制在约 5 秒，15 秒掉线判定不受影响。
  - **建议**：daemon 等回复超过约 500ms 时，补写一个空行 `\n`。LineFramer 本来就会丢掉不以 `{` 开头的行，所以这样做不会改变协议语义。这条由 WP3 决定是否采纳，WP5 的延迟场景可以验证效果。
  - WP4 的 usb_transport 仍然保留 20ms kick（05 §5.8）。固件里要避免长时间阻塞 loop，比如把 Flash 写入拆小，这和 R7 一致。

### 5.3 刷写和恢复
- A→B→A 来回刷了 10 次以上，每次 4 个区域（bootloader、分区表、boot_app0、app）都校验通过。上传都不用按键。
- **一次异常（A，esptool 4.5.1）**：在一个回复没出来的会话之后，`pio run -t upload` 连续两次卡在 `Changing baud rate to 460800` → `No serial data received`。这时 ROM 能连上，但 stub 启动后就没有响应，Flash 没有写入任何东西。用 115200 运行 `read_mac` 也一样。**恢复办法是不断电，执行一次 ROM 直连的正常复位**：
  ```bash
  ~/.local/share/uv/tools/platformio/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
    --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 115200 --before default_reset --after hard_reset --no-stub read_mac
  ```
  执行后 app 正常启动，之后上传也恢复正常，此后没有再出现过。推测是 USB-Serial/JTAG 外设的状态在内核复位后还保留着，app 重新初始化一次就清掉了。这个办法应写进 README 的恢复一节（WP5）。
- 两个平台装在同一台机器上，切换时 PlatformIO 会重装一部分工具（esptool、mklittlefs 等）。装了 B 以后，`~/.platformio` 从 2.4GB 涨到约 5.7GB。

## 6. 怎样切换或回退

- **以后改用 B（或 C）**：在 `firmware/app/platformio.ini` 里只改 `platform` 这一行，换成锁定版本的 release zip 地址（05 §3），kick 保留。然后在 hwtest 上重跑 `g2_run.py --reboot --rtt 30`，**至少跑 4 个刚开机的会话**，要求 0 次卡住，第 2–4 项也都通过，再更新本 ADR。
- **从 B 回到 A**：把 `platform` 改回 `espressif32@6.10.0`，再执行 `pio run -t upload`。上传会同时写 bootloader、分区表和 app，今天来回切换都成功了，不需要整片擦除。⏳ NVS/LittleFS 里的数据在 IDF 4.4 和 5.4 之间互不互通，这一点没有测过。
- **清理 B 占的磁盘**（可选，由用户决定）：B 用到的是 `~/.platformio/platforms/espressif32`（pioarduino 54.03.21），以及 `packages/` 下的 `framework-arduinoespressif32@src-*`、`framework-arduinoespressif32-libs`、`toolchain-xtensa-esp-elf`、`tool-esptoolpy@src-*`。A 用的是 `platforms/espressif32@6.10.0`，以及不带 `@src` 的 `framework-arduinoespressif32` 和 `toolchain-xtensa-esp32s3`。

## 7. 待验证

- ⏳ SD 卡：没插卡，写入、改名、核对大小都没有真正跑过。插卡后执行 `g2_run.py --steps sdtest` 即可补测。v0.1 不用 SD（05 §2）。
- ⏳ 扬声器是不是真的发出了声音：只确认了 `Speaker.begin()` 返回 true，没有人耳确认。
- ⏳ HWCDC 回复被扣的根因（两套 core 都有），以及 §5.2 提议的空行唤醒是否有效。
