[English](01-mac-setup.md) · **中文**

# Cardputer ADV × Mac：连接与初始化指南

> 依据：M5Stack 官方文档（中英文）、Cardputer-Adv / Stamp-S3A 原理图、M5Stack 与 Espressif 官方 GitHub、Apple 支持文档。
> 每一步都附来源链接。标注「推断」的条目没有官方原文，是从原理图或芯片文档推出来的。
> 调研日期：2026-10-19。本机环境：Apple 芯片的 Mac，macOS 26。

---

## 0. 先搞懂它是怎么连电脑的

| 项目 | 事实 | 来源 |
|---|---|---|
| USB 通路 | ESP32-S3FN8 **芯片自带的原生 USB**（USB-Serial/JTAG），D- = GPIO19、D+ = GPIO20，直接连到 USB-C 口，**没有 CH9102/CP210x 桥接芯片** | [Stamp-S3 原理图](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1150/Sch_StampS3_v0.3.3.pdf)、[ADV 产品页](https://docs.m5stack.com/zh_CN/core/Cardputer-Adv) |
| macOS 驱动 | **不需要装任何驱动**，也**别装** M5 下载页上的 CH9102/CP210x/FTDI 驱动 | [Espressif USB-Serial-JTAG](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_serial_jtag.html) |
| 正常时 Mac 上看到的 | `/dev/cu.usbmodemXXXX`；VID:PID = `303A:1001`（ROM 下载模式 / 出厂固件 / Arduino 默认设置） | [openocd esp_usb_jtag.cfg](https://github.com/espressif/openocd-esp32/blob/master/tcl/interface/esp_usb_jtag.cfg) |
| 跑 UiFlow2 时 | `303A:816B`，名字 "Cardputer-ADV(UiFlow2)" | [uiflow-micropython ADV 板级配置](https://github.com/m5stack/uiflow-micropython/blob/master/m5stack/boards/M5STACK_CardputerADV/mpconfigboard.h) |
| 出厂固件的 Keyboard 应用选了 USB 模式时 | 变成一个叫 "CardputerADV" 的 **HID 键盘**，**此时没有串口** | [出厂固件源码](https://github.com/m5stack/M5Cardputer-UserDemo/tree/CardputerADV) |
| 侧面电源开关 | **只控制电池**。插 USB 时不管开关在哪都有电（原理图推断，和官方"关机按 G0 再插线"的流程一致）。**充电必须把开关拨到 ON**（官方原文） | [ADV 原理图](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Sch_M5CardputerAdv_v1.0_2025_06_20_17_19_58.pdf) |
| 芯片 | ESP32-S3FN8：8MB Flash，**没有 PSRAM** | [ADV 产品页](https://docs.m5stack.com/en/core/Cardputer-Adv) |
| 侧边按键 | 同一条边上依次是 BtnG0、OFF/ON 开关、microSD 槽、BtnRst、IR | [官方实物图](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Cardputer-Adv_operation_01.jpg) |

---

## 1. Mac 完全识别不到设备时，按顺序排查

> 当前状态：`/dev/cu.*` 只有蓝牙串口，USB 树里任何设备都没有。

1. **macOS 的"允许配件连接"**（Apple Silicon 笔记本特有，最容易被忽略）
   - 路径：系统设置 → 隐私与安全性 → 配件 → "允许配件连接"，设成"询问新配件"或"解锁时自动允许"
   - 改完先解锁 Mac，再拔插 Cardputer，弹窗时点**允许**
   - Apple 原话：拒绝之后 Mac "won't recognize the accessory"，但**还能充电**。所以屏幕亮、能充电，都**不能**说明数据通了。
   - 来源：[Apple 102282](https://support.apple.com/en-us/102282)
2. **换一根确定能传数据的 USB-C 线**，**直接插 MacBook 本体**的口，先不经过扩展坞、显示器或 hub。左右两侧的口都试试。
   来源：[官方 Arduino 指南](https://docs.m5stack.com/zh_CN/arduino/m5cardputer/program)要求 "USB-C data cable"
3. **用对命令查**（插线前后各查一次，对比多出了什么）：
   ```bash
   ls /dev/cu.*
   ```
   ```bash
   system_profiler SPUSBHostDataType
   ```
   ```bash
   ioreg -p IOUSB -l -w0 | grep -i -E '12346|303a|jtag|espressif|m5stack|cardputer'
   ```
   macOS 26 上要用 `SPUSBHostDataType`，旧的 `SPUSBDataType` 已经没有了。
4. **强制进入 ROM 下载模式**（官方 ADV 流程，这是**决定性测试**）
   1. 侧面开关拨到 **OFF**
   2. **按住 G0**
   3. 插上 USB-C 线
   4. 松开 G0

   屏幕应该是黑的（推断：ROM 不会点亮屏幕）。这时再执行第 3 步的命令：
   - **出现 `303A:1001`**：USB 链路没问题，是现在装的固件把 USB 关掉或占用了（比如深度睡眠、HID 键盘模式、崩溃重启）。重新刷固件就好。
   - **还是什么都没有**：回到第 1、2 步（权限、线、口），再不行就换一台电脑试。
   - 来源：[ADV 产品页](https://docs.m5stack.com/zh_CN/core/Cardputer-Adv)："将 Cardputer-Adv 侧面的开关键置于 OFF 状态，然后在开机前按住 G0 按键，在设备通电后释放"
5. **备选进入方式**（线已经插着时）：按住 G0，按一下 BtnRst 再松开，继续按住 G0 约 1 秒，然后松开。
   这是 Espressif 通用做法，和原理图一致（BTN1 接 EN、BTN2 接 G0），但 M5 的 ADV 文档里没写。
6. **端口出现了却打不开**：同一时间只能有一个程序占用 `/dev/cu.usbmodem*`。先关掉 Arduino 串口监视器、M5Burner、mpremote、你自己的 daemon 等。用 `screen` 的话，要按 `Ctrl-A` 再按 `K` 退出，直接关窗口会让端口一直被锁着。
7. **换了电脑、换了线、进了下载模式都不识别**：按硬件故障处理，联系 support@m5stack.com。

---

## 2. 四条初始化路径（按适合你这个项目的程度排序）

### 路径 A：Arduino IDE 2 + M5Cardputer 库（推荐先走这条，用来验证双向串口通信）

1. **（建议）先备份出厂固件**，在下载模式下执行：
   ```bash
   esptool -p /dev/cu.usbmodemXXXX -b 460800 read-flash 0 ALL cardputer_adv_factory_8MB.bin
   ```
2. Arduino IDE → Settings → Additional boards manager URLs，填入：
   `https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json`
   （国内镜像：`..._index_cn.json`）
   来源：[arduino_board](https://docs.m5stack.com/en/arduino/arduino_board)
3. Boards Manager 搜 "M5Stack"，安装 **3.2.2 或更高**（目前最新 3.3.10）。
   **没有单独的 ADV 板子选项**，ADV 也选 **M5Cardputer**，靠库在运行时识别硬件。
   ⚠️ **要用麦克风的话，先锁定 3.2.x 版本**。3.3.x 基于 IDF 5.5，在 ADV 上录到的全是 0（ESP-IDF #18621 还没修好，CardMind 项目在 3.3.9 上复现过）。只做键盘、屏幕和串口的话，用最新版没问题。详见 [02-landscape-report.md](02-landscape-report.zh-CN.md) 第 8 节。
4. Library Manager 搜 "M5Cardputer"，点 Install All。最低版本要求：
   - M5Cardputer ≥ 1.1.0
   - M5Unified ≥ 0.2.8（建议 ≥ 0.2.9，才有电量读取）
   - M5GFX ≥ 0.2.10

   **库太旧的话，ADV 的键盘会完全没反应**（ADV 用的是 TCA8418 I2C 键盘控制器，原版 Cardputer 是 GPIO 矩阵）。
   来源：[keyboard 页](https://docs.m5stack.com/en/arduino/m5cardputer/keyboard)
5. **Tools 菜单设置**（官方页面没写，以下按 arduino-esp32 的 boards.txt 核实过）：
   | 选项 | 设置 |
   |---|---|
   | Board | M5Stack → **M5Cardputer** |
   | USB Mode | **Hardware CDC and JTAG**（默认值，不要改） |
   | USB CDC On Boot | **Enabled**（默认值） |
   | Flash Size | **8MB**（⚠️ 默认是 4MB，必须手动改） |
   | Partition Scheme | **8M with spiffs (3MB APP/1.5MB SPIFFS)** |
   | PSRAM | Disabled |
6. **第一次上传**：先进下载模式，Tools → Port 选 `/dev/cu.usbmodemXXXX`，打开 File → Examples → M5Cardputer → Basic → display，点 Upload。传完按 BtnRst 运行。
7. **以后上传不用再按键**：只要你的固件一直保持 USB-Serial/JTAG 可用，主机就能自动把芯片复位进下载模式。
   以下几种情况会让端口消失，需要重新用 G0 刷：深度睡眠、切到 USB-OTG/TinyUSB 模式、改了 GPIO19/20 的用途。
   来源：[ESP-IDF USB-Serial-JTAG console](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-serial-jtag-console.html)
8. **双向串口测试固件**（自己拼的，不是官方示例；用到的 API 都核实过）：键盘打一行字回车会发到 Mac，Mac 发过来的行会显示在屏幕上。
   ```cpp
   #include <M5Cardputer.h>
   String typed, rx;
   void setup() {
     auto cfg = M5.config();
     M5Cardputer.begin(cfg, true);          // true = 启用键盘
     Serial.begin(115200);                  // 原生 USB CDC 会忽略波特率
   #if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
     Serial.setTxTimeoutMs(0);              // 没有主机在读时不阻塞
   #endif
     M5Cardputer.Display.setRotation(1);
     M5Cardputer.Display.setTextScroll(true);
     M5Cardputer.Display.println("ready");
   }
   void loop() {
     M5Cardputer.update();
     if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
       auto st = M5Cardputer.Keyboard.keysState();
       for (char c : st.word) typed += c;
       if (st.del && typed.length()) typed.remove(typed.length() - 1);
       if (st.enter) { Serial.println(typed); typed = ""; }
     }
     while (Serial.available()) {
       char c = (char)Serial.read();
       if (c == '\n') { M5Cardputer.Display.println(rx); rx = ""; }
       else if (c != '\r') rx += c;
     }
   }
   ```
   不要写 `while(!Serial)`：HWCDC 只有在主机打开端口时才返回 true，没开串口监视器就会一直卡在这里。
9. **Mac 端读写**：
   ```bash
   screen /dev/cu.usbmodemXXXX 115200
   ```
   退出方法：按 `Ctrl-A`，再按 `K`。

   Python daemon 的骨架：**按 VID:PID 找端口**，不要写死端口名，因为复位后名字可能会变。
   ```python
   import serial, serial.tools.list_ports as lp
   port = next(p.device for p in lp.comports() if (p.vid, p.pid) == (0x303A, 0x1001))
   s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 1
   s.dtr = False; s.rts = False   # 先拉低再 open，避免误触发复位（中等可信度，来自论坛）
   s.open(); s.write(b'{"type":"ping"}\n'); print(s.readline())
   ```
   ⚠️ **本机实测（2026-10-19）**：在 macOS 上，即使 open 之前已经把 DTR/RTS 设为 False，用 pyserial 打开端口时，芯片还是被 USB-JTAG 复位了（`rst:0x15 USB_UART_CHIP_RESET`），并且进入了下载模式（`boot:0x23`）。所以 daemon 怎样打开端口才不会误复位，要等我们自己的固件刷上去以后再专门验证。

### 路径 B：PlatformIO（适合把固件和 Mac daemon 放进同一个 git 仓库）

ADV 产品页上的官方配置：
```ini
[env:m5stack-cardputer]
platform = espressif32@6.7.0
board = esp32-s3-devkitc-1
framework = arduino
upload_speed = 1500000
monitor_speed = 115200
build_flags =
    -DESP32S3
    -DCORE_DEBUG_LEVEL=5
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    M5Cardputer=https://github.com/m5stack/M5Cardputer
```
- 这个配置锁定的是 Arduino core 2.0.16；`esp32-s3-devkitc-1` 是 8MB Flash，和 ADV 一致。
- 日常使用可以把 `CORE_DEBUG_LEVEL` 调低。
- 第一次刷要进下载模式，之后用 `pio run -t upload` 会自动复位。
- 新的 `espressif32 v8.0.0`（今天刚发布）加了一个 `m5stack-cardputer` 板子定义，**但它声明的是 4MB Flash**。如果要用它，得手动覆盖成 8MB。
- 来源：[ADV 产品页](https://docs.m5stack.com/en/core/Cardputer-Adv)

### 路径 C：ESP-IDF（完全掌控，出厂固件就是用它写的）

```bash
git clone -b CardputerADV https://github.com/m5stack/M5Cardputer-UserDemo
cd M5Cardputer-UserDemo && python3 ./fetch_repos.py
idf.py build && idf.py -p /dev/cu.usbmodemXXXX flash monitor
```
- 需要 ESP-IDF v5.4.2。
- 适合要同时管 USB、BLE、WiFi、低功耗，或者要直接复用出厂的 TCA8418 键盘和 ES8311 音频驱动的情况。
- ⚠️ 出厂固件在 `hal.cpp` 里会**明文打印 WiFi 账号密码**到串口日志。自己写固件时别照抄。

### 路径 D：UiFlow2 / MicroPython（不写 C++，快速原型）

1. 安装 [M5Burner](https://docs.m5stack.com/en/uiflow/m5burner/intro)。macOS 版只有 x64 安装包，Apple Silicon 上大概率要先装 Rosetta（未确认）：
   ```bash
   softwareupdate --install-rosetta
   ```
2. 在 M5Burner 里选 CARDPUTER → **"UIFlow2.0 Cardputer-Adv"**（最新 v2.5.3）。⚠️ **不要选"UIFlow2.0"**，那个是给原版 Cardputer 的。
3. 进下载模式 → Burn → 填 COM 端口和 2.4GHz WiFi，Boot Option 选 "Show startup menu and network setup" → Start → 完成后按复位。
4. 连接方式二选一：
   - 在 [uiflow2.m5stack.com](https://uiflow2.m5stack.com/) 输入屏幕上显示的 Access Code
   - 用 USB 的 WebTerminal（基于 Web Serial，要用 Chrome 或 Edge）
5. 也可以直接用 MicroPython REPL（推断可用，官方没写）：
   ```bash
   mpremote connect /dev/cu.usbmodemXXXX repl
   ```
6. 代价：UiFlow2 走的是 TinyUSB（`303A:816B`），**每次重刷都要按 G0**，没有自动复位。

### 路径 E：恢复出厂固件（救砖，也可以用来检查硬件好坏）

M5Burner → CARDPUTER → **"Cardputer-Adv User Demo"**（v0.3）→ 进下载模式 → Burn → 端口选 `/dev/cu.usbmodemXXXX`，波特率 1500000 → Start。
⚠️ **不要选"Cardputer User Demo"**（那个是原版的）。官方教程截图里的对话框标题也写着 "Burn Cardputer User Demo"，是官方截图标错了。
来源：[恢复出厂指南](https://docs.m5stack.com/en/guide/restore_factory/cardputer_adv)

---

## 3. 原版 Cardputer 和 ADV：哪些通用，哪些不能混用

| 通用 | **不能混用** |
|---|---|
| Arduino 快速入门、板子选项 M5Cardputer、下载模式步骤、充电要拨到 ON | 固件包：ADV 只能用带 "-Adv" 的 User Demo 和 UIFlow2.0 |
| | 键盘：ADV 是 TCA8418 I2C，原版是 GPIO 矩阵 |
| | UiFlow2 的 USB PID：ADV 是 816B，原版是 81DA |
| | 库版本：库太旧会让 ADV 的键盘完全失灵 |

---

## 4. 对本项目的含义

- **主链路选 USB-Serial/JTAG（CDC）**：免驱动，按 VID:PID 就能自动找到，刷固件自动复位，最适合"插上电脑就同步"。
- **想把 SD 卡挂成 U 盘（MSC），或者模拟 HID 键盘**：就得切到 TinyUSB/OTG 模式，代价是失去自动复位，每次刷固件都要按 G0。建议做成运行时可以切换的"模式"，不要一开机就默认进这个模式。
- **没有 PSRAM**：可用内存只有约 512KB SRAM。录音缓冲、TLS 和 UI 都要省着用。具体容量计算会写在 deep research 报告里。
- **值得关注的线索**：交叉核对时发现 Anthropic 有一个叫 [cwc-makers](https://claude.com/cwc-makers) 的插件（里面有一个 m5-onboard skill），可以让刷了 UiFlow2 的 Cardputer-Adv 通过 BLE 和 Claude Desktop 的 "Hardware Buddy" 配对。这和你的想法高度相关，会在 deep research 报告里专门核实。
