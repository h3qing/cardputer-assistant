[English](03-hardware-bringup.md) · **中文**

# 硬件联调记录：Mac ↔ Cardputer ADV（2026-10-19）

> 今天第一次把 Cardputer ADV 接到 Mac 上联调，这里记下踩过的坑和验证过的结论。标 ✅ 的是在真机上实测确认的，标 📚 的来自官方文档或源码，标 ⏳ 的还没验证。
> 测试固件在 `firmware/hwtest/`，Mac 端测试程序是 `firmware/hwtest/host/hwtest_host.py`。

## 1. 设备事实

| 项 | 值 | 来源 |
|---|---|---|
| 芯片 | ESP32-S3 (QFN56) rev v0.2，40MHz 晶振 | ✅ esptool |
| Flash / PSRAM | 内置 8MB（XMC，quad，3.3V）/ **无 PSRAM** | ✅ esptool |
| MAC | 02:00:00:00:00:01 | ✅ |
| M5Unified 板型 ID | `24`（board_M5CardputerADV） | ✅ 固件回报 |
| 堆内存（测试固件） | 空闲约 281KB，最大连续块约 254KB（另有 64KB 静态麦克风缓冲） | ✅ |
| 电池电压（插着 USB） | 约 4.15V | ✅ |
| 出厂固件 | `cardputer-adv` V0.9-36-ge824a76，ESP-IDF v5.4.2，编译于 2025-10-21；分区：nvs / otadata / phy / 一个 4MB 的 factory app | ✅ 已备份（见 `backups/README.md`） |

## 2. USB 连接：三个坑

### 坑 1：出厂固件平时不开 USB 数据（✅）
- 现象：屏幕能亮，但 Mac 上看不到任何 USB 设备，换线也没用。
- 判断方法：读 Mac 的 USB-C 端口控制器状态，用命令 `ioreg -r -c AppleHPMInterfaceType10 -l`。
  - `ConnectionActive=Yes`，`TransportsActive=("CC","USB2")`：Mac 已经检测到线，也打开了 USB2 数据通道
  - 但 `ioreg -p IOUSB` 里还是没有设备：说明**问题在设备侧**，不是 Mac 权限或线的问题
- 只有进 ROM 下载模式时才能枚举出来，设备显示为 `303A:1001 USB JTAG/serial debug unit`，端口是 `/dev/cu.usbmodem1101`。

### 坑 2：G0 锁存陷阱（✅ 实测 + 📚 Espressif 在 esptool #970 里确认过）
- **按住 G0 上电**时，芯片会把 GPIO0 的状态锁存为低电平，并且**一直保持到下次断电**。
- 之后 Mac 通过 USB 触发的复位（`rst:0x15 USB_UART_CHIP_RESET`）都只是"内核复位"，不会重新读取启动引脚，所以**每次复位都落回下载模式**（`boot:0x23`）。esptool 刷完后的 "Hard resetting via RTS" 也回不到固件。
- 解决办法，任选一个：
  1. **真正断电**：开关拨到 OFF，**拔掉 USB 线**，等几秒再插上。只拨开关没用，因为 USB 一直在供电。
  2. 不按 G0，单独按一下 BtnRst。
  3. 用 esptool ≥ 4.9 的 `--after watchdog_reset`。
- **教训：日常刷固件不要再按 G0。** 正常情况下 `pio run -t upload` 会通过 USB 的"下载标志"自动进入下载模式，刷完也会自动回到固件。✅ 已实测，不用按任何键。

### 坑 3：在 Mac 上打开或关闭串口可能会复位芯片（📚 TRM + ✅ 实测）
ESP32-S3 USB-Serial/JTAG 的控制线真值表（顺序是 RTS,DTR）：

| RTS,DTR | 动作 |
|---|---|
| 0,0 | 清除下载标志 |
| 0,1 | 设置下载标志 |
| **1,0** | **复位芯片**（复位时下载标志是置位的，就进下载模式，否则从 Flash 启动） |
| 1,1 | 无动作 |

macOS 打开端口时会把两根线都拉高。**安全的打开方式**和 Espressif 自己的 esp-idf-monitor 一样：

```python
s = serial.Serial(); s.port = dev; s.baudrate = 115200
s.dsrdtr = True          # 打开时 pyserial 不去碰 DTR；RTS 默认是高电平，不会产生变化
s.open()
s.rts = False            # (0,1)：只设置下载标志，不会复位
s.dtr = False            # (0,0)：清掉下载标志，进入安全的空闲状态
# 关闭端口时两根线已经都是 0，系统再放低（HUPCL）也不会改变任何状态
```

✅ 实测：用这种方式打开端口后，固件回报的开机时长 `up_ms` 是连续增长的，说明芯片没有被复位。
**以后要从 Mac 端重启设备**：做一条固件命令，在固件里调用 `esp_restart()`，不要去拨弄控制线。

## 3. HWCDC 发送数据会被"扣住"（Arduino core 2.0.17，✅ 实测 + 📚 源码）
- 现象：Mac 刚打开端口后，设备发出的第一条回复会卡在设备里，要等 Mac 下次发数据才一起冲出来。最长卡过 11 分钟。
- 原因：`HWCDC` 是根据 USB 的 SOF 信号判断"主机还在不在"的（`isPlugged()`）。主机刚连上时它会误判为已断开，于是在 `IN_EMPTY` 中断里把发送通道停掉，数据就留在 ring buffer 里没发出去。
- 处理办法：
  - **固件**：主循环里每 20ms 读一次 `(bool)Serial`，这会触发 `isCDC_Connected()`，重新打开 `IN_EMPTY` 中断，把扣住的数据送出去。
  - **Mac 端**：连上后先发 `!info` 握手，2 秒内没收到回复就重发，最多 3 次。
  - 实测效果：握手之后，往返延迟稳定在约 60ms，键盘事件也是实时到达。
- ⚠️ G2 补充实测（见 docs/adr/0001-toolchain.md）：kick 是必要条件，但**不是充分条件**。在 core 2.0.17 上，长时间阻塞的操作（比如播放提示音、写 SD）之后，偶尔还是会有回复卡住，要等 Mac 再写一次才会出来。所以 daemon 在一条回复超过约 500ms 还没到时，会发一个空行去"推"一下（固件会把空行当噪声丢掉）。固件的主循环也要避免长时间阻塞。
- 长期方案（⏳）：评估 Arduino core 3.3.9 及以上（HWCDC 的修复从 3.3.9 才有，PR #12606；3.2.1 仍然需要 kick），或直接用 ESP-IDF 的 `usb_serial_jtag` 驱动。

## 4. 工具链（✅）
- **PlatformIO**：6.2.0，用 `uv tool install platformio --python 3.12` 安装，命令在 `~/.local/bin/pio`。
- **平台**：`espressif32@6.10.0`，对应 Arduino core 2.0.17、ESP-IDF 4.4.7，自带 esptool 4.5.1。
  - 为什么锁这个版本：基于 IDF 5.5 的 core 在 ADV 上录音全是 0（ESP-IDF #18621）。⏳ 麦克风在这套工具链上的实测还没做。
- **库**：
  - M5Cardputer 用 git tag `1.2.0`。它的 library.json 里版本号仍写着 1.1.1，属正常现象。
  - M5Unified `0.2.26`，M5GFX `0.2.33`，在 core 2.0.17 上都能编译通过。
- **注意**：IDF 4.4 里没有 `ESP_RST_USB` 这个值。通过 USB 触发的复位，在固件里读到的复位原因是 `0`（unknown）。
- **测试固件资源占用**：RAM 27%（89KB，其中 64KB 是麦克风缓冲），Flash 521KB。

## 5. esptool 使用经验（✅）
- 读 Flash 时一定要加 `-b 2000000`：默认速度只有约 11KB/s，加了以后约 100KB/s，8MB 只要 84 秒。
- esptool 中途被杀掉的话，芯片里的 stub 会停在半截。下次连接时用 `--before default_reset`。
- `--before no_reset` 只适用于芯片已经在下载模式的情况。

## 6. 键盘（✅ 部分）
- `M5Cardputer.Keyboard.keyList()` 能实时给出所有按住的键。测试固件会同时上报键在 4×14 布局里的坐标，以及在 TCA8418 矩阵里的行列号。
- `Keyboard.isChange()` **只比较按住的键数量**，所以要自己比较完整的按键集合。
- ⏳ **三键组合会不会出幽灵键还没实测**，`Ctrl+Shift+S` 等组合待测。

## 7. Mac 端排查命令速查
```bash
ioreg -p IOUSB -w0                                     # 有没有枚举出 USB 设备
ioreg -r -c AppleHPMInterfaceType10 -l | grep -E 'ConnectionActive|TransportsActive|PortDescription'
system_profiler SPUSBHostDataType                      # macOS 26 用这个，旧的 SPUSBDataType 已经没有了
/usr/bin/log show --last 10m ...                       # 注意：在 zsh 里 log 是内置命令，要写全路径
```

## 8. 对架构的影响
1. **Mac daemon 独占串口**：必须用"安全打开"方式，连上后握手并带重试，用 `up_ms` 或开机 ID 判断设备有没有重启过。
2. **固件保持 USB-Serial/JTAG（HWCDC）作为默认模式**：这样刷固件能自动复位、不用按键。连着 USB 时不进深度睡眠。U 盘模式（MSC）和 HID 只做成需要手动切换的模式。
3. **重启设备用固件命令**，不用拨控制线；固件里要保留 TX kick。
4. **音频**：工具链锁在 IDF 4.4 或 5.4；麦克风能不能用要先实测再决定。
