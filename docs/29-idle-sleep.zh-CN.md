[English](29-idle-sleep.md) · **中文**

# 29 · 休眠：放着不用时省电

> 状态：**设计稿，待你拍板**（§11）· 日期：2026-10-28 · 角色：固件（电源）· 基于 origin/main `677863e`，加上将先合并的 `feat/keep-until-right-mac`（#84，`7382e33`）和 `fix/boot-scan-stack`（#85，`fe8c2db`）。关联：docs/03（G0 锁存陷阱）、docs/11 §4（录音、电池阈值）、docs/13 §7（锁屏）、docs/19（锁屏卡、toast）、docs/20 D2、D7、D8（熄屏即锁、自己亮、"● 准备"）、docs/21（设置页）、docs/28（账本、留底、插上提示、开机扫描）。
> 要求（2026-10-28）：设备长时间放着不用时会一直开着、把电池用完；加一个休眠功能来省电。
> 标记：**[源]** 读过的代码或文档 · **[测]** 已有的真机实测 · **[推]** 推断 · **[设]** 设计假设，可以改 · **[待]** 要在真机上验证 · **👤** 需要你。
> 范围：只写设计和计划。没接设备、没改代码。
> 路径缩写：`fw/` = `firmware/app/lib/`；`M5C/` = M5Cardputer 1.2.0 源码（`.pio/libdeps/cardputer-adv/M5Cardputer/src/`）；`M5U/` = M5Unified 0.2.26 源码；DS = ESP32-S3 数据手册 v2.2；TRM = ESP32-S3 技术参考手册 v1.8；IDF = ESP-IDF v4.4.7 文档。

## 0. 一页摘要

1. **今天为什么会把电用完**（§1）：用电池时，60 秒没按键背光就关了，屏幕也锁上了（docs/20 D2）。但芯片照样全速转：主循环里没有任何 `delay()`，CPU 一直 100% 忙 [源 `src/main.cpp:1-4`，`loop()`]。关掉的只有背光。按数据手册估算，整机暗屏时还要几十 mA，1500 mAh 的电池一到两天就空了 [推]。
2. **办法是三层，用户只看得到一个设置**（§3）：
   - **第 0 层：暗屏**（已经有了）。用电池时 60 秒没按键：先调暗，再熄屏并上锁。
   - **第 1 层：浅睡**（新）。屏幕一黑、锁上，在用电池，又没有任何事在进行，就进入 ESP32-S3 的 light sleep。内存全部保留。按 G0 或键盘任意键，**不到 1 毫秒就醒** [推，IDF]。醒来和今天黑屏时一模一样：锁着，"点一下再按住"录便条，开头不会比今天多丢。
   - **第 2 层：深睡**（新）。最后一次有效按键之后 **2 小时**（设置里可以改成 30 分钟 / 2 小时 / 8 小时 / 从不），进入 deep sleep，相当于软关机。按 G0 或任意键会重新开机，约 1.5 秒后亮出锁屏卡。内存里的东西（当前页面、toast）丢掉；卡上的录音、待传条目、账本、设置都还在。
3. **什么时候绝不睡**（§4）：正在录音或开会；插着 Mac（USB 上能看到主机）；账本命令或开机扫描没做完；编辑器里有没保存的字（只挡深睡）；锁屏卡还亮着；有键按着。**只插充电器、没插 Mac** 时和用电池一样睡，睡着也照样充电。
4. **G0 能不能当唤醒键**（§5）：可以。按住 G0 **上电**会进下载模式（docs/03 坑 2），因为上电是"芯片复位"，会重新锁存 GPIO0。从深睡醒来是"内核复位"，不会重新锁存 [源 DS §3 "At Chip Reset, the latches sample…"；TRM 表 7.1-1：0x05 Deep-sleep reset = Core Reset]。docs/03 实测过的 USB 复位（0x15）也是内核复位，同样不会重新读 G0。浅睡根本不复位。**这个结论还没在真机上验证**（P0 第 4 项）。有一个例外要防：电压跌到掉电复位（brown-out）属于芯片复位，会重新锁存。所以低电时要在掉电之前先主动深睡（§7）。
5. **最难的是 USB**（§6）：light sleep 时 USB-Serial-JTAG 停止工作。主机可能报错，醒来后也可能不再重新枚举，只能拔线重插 [源 IDF usb-serial-jtag-console]。办法有三条：
   - 插着主机时不睡；
   - 进浅睡前先在软件里**断开 D+ 上拉**，让 Mac 以为线没插（TRM 寄存器 `USB_SERIAL_JTAG_PAD_PULL_OVERRIDE` / `DP_PULLUP`）；
   - 浅睡时每 60 秒醒一次，接上 300 ms，看有没有主机。

   效果：睡着时插上 Mac，最多 1 分钟自己连上，按任意键立刻连上。
6. **能省多少**（§2，没有电流表，数字都要 P0 用电压斜率实测）：

   | 状态 | 估计电流 | 1500 mAh 能撑多久 |
   |---|---|---|
   | 今天暗屏 | 约 45–70 mA [推] | 约 1 天 |
   | 浅睡 | 约 1–5 mA [推] | 约 2–6 周 |
   | 深睡 | 约 0.05–1 mA [推] | 2 个月以上，接近自放电 |
   | 拨开关关机 | 0.23 µA [源 M5Stack] | — |

   芯片本身：light sleep 240 µA、deep sleep 7–8 µA [源 DS 表 5-9/5-10]。整机多出来的那部分（LDO、SD 卡、LED、分压电阻）只能实测。
7. **G0 按下到第一个样本**（§8）：
   - 今天 507 ms [测 docs/07:882]；
   - 浅睡：多不到 20 ms [推]；
   - 深睡：第一下只负责开机，开机约 1.5 秒 [测：reboot 到 Ready 1.55–1.66 s，docs/07:267,547]，然后"点一下再按住"，从第一下算起约 2.2–2.6 秒。屏幕上的"● 准备"（docs/20 D8）会告诉你什么时候可以说话，所以开头不会悄悄丢。
8. **分期**（§10）：
   - **P0**：测量和探针（只动测试构建）。
   - **P1**：暗屏少转。一行级别的改动，零风险，先拿一半的收益。
   - **P2**：浅睡。
   - **P3**：深睡、设置项、低电保护。
   - **P4**：Mac 端补一句提示。
9. **16 条决定**都有默认值（§11.1）。要你回答的 4 件在 §11.2。影响最大的是第 1 条：深睡默认 2 小时。

## 1. 今天的样子

### 1.1 电源策略（纯函数，`fw/shell/power.*`）

- 用电池时，50 秒没按键先调暗，60 秒熄屏（`kBacklightIdleMs`、`kPreDimMs`）[源 power.h:22-23；power.cpp:32-46]。
- 熄屏的那一刻自动上锁（docs/20 D2）[源 runtime.cpp:196-197 `if (power_.dark && !was_dark) shell_.auto_lock()`]。
- 插着 Mac 时屏幕常亮，也不锁 [源 power.cpp:34-39]。"插着"指 `HWCDC::isPlugged()`，也就是 USB 上一直有 SOF 帧、有主机在 [源 usb_transport.cpp:25]。**只插充电器时 `isPlugged()` 为假，按电池处理。**
- 录音时背光不关，只调暗（● 规则）[源 power.cpp:64-70]。
- 锁着时，lock gate 吞掉的键**不会**刷新 `last_key_ms`（gate 在 `power_key` 之前）[源 runtime.cpp:169-183]。所以兜里乱按不会让设备一直"有人在用"。

### 1.2 黑屏之后芯片在干什么

- Arduino 的 `loopTask` 不停地调用 `loop()`，`loop()` 里明确不准 `delay()` [源 main.cpp:1-4]。暗屏时 CPU 还是 240 MHz 满负荷空转。
- 显示控制器 ST7789 没有进入睡眠（只关了背光）[源 m5_display.cpp:76-80]。SD 卡一直挂载着。
- 音频任务空闲时，麦克风和扬声器都关着。M5Unified 在 `Mic.end` 时让 ES8311 的模拟部分断电（`0x0D=0xFC`，`0x00=0x00` CSM power down）[源 M5U/M5Unified.inl:1553-1557]。也就是说，**codec 空闲时已经是低功耗状态**。
- 每秒读一次电池电压（`kStatsEveryMs` 1000）[源 runtime.h:50；hal_platform.cpp:16]。
- docs/28 的留底整理（tidy）在没连 Mac 时也会跑，由音频任务执行 [源 runtime_kept.cpp:155-164]。

### 1.3 键盘

- Cardputer ADV 的键盘是 TCA8418 键盘芯片，挂在 I2C（G8/G9）上。它的中断脚 INT 接 **G11** [源 M5C/utility/Keyboard/KeyboardReader/TCA8418.cpp:13]。
- M5Cardputer 的读取器在 G11 上挂了一个 CHANGE 中断。中断只置一个标志位，`update()` 每次只在标志位为真时读一个事件 [源 TCA8418.cpp:43-66]。
- **对休眠有影响的地方**：在睡眠中按下的那一下，INT 早已拉低。醒来后不会再有边沿，标志位就一直不会置位。所以要自己写一个读取器：M5Cardputer 允许 `Keyboard.begin(std::unique_ptr<KeyboardReader>)` 注入 [源 M5C/utility/Keyboard/Keyboard.h:153]（§9.2）。
- G0 不经过 TCA8418，是 GPIO0 直接读 [源 M5U/M5Unified.inl:3573-3576]。

### 1.4 已有的电池规则

| 阈值 | 作用 | 来源 |
|---|---|---|
| ≤3.55 V | 录音时屏幕警告 | rec_core.h:62 `kBatWarnMv` |
| ≤3.45 V 持续 10 s | 录音正常收尾，拒绝开始新录音 | rec_core.h:56 `kBatStopMv` |
| 3.30 V–4.15 V | 状态栏百分比的两端 | menu_model.cpp:15-16 |

不在录音时，电池再低也没有任何动作。

## 2. 硬件事实（调研结果，带出处）

| 问题 | 结论 | 出处 |
|---|---|---|
| 芯片 light sleep 电流 | 240 µA（VDD_SPI、WiFi 关） | DS 表 5-10 |
| 芯片 deep sleep 电流 | 7 µA（只开 RTC 内存）/ 8 µA（加 RTC 外设，ext0、RTC 上拉要用）/ 1 µA（CHIP_PU 拉低） | DS 表 5-10 |
| 芯片空闲（两核 WAITI） | 80 MHz 22–36 mA；240 MHz 33–48 mA。忙循环更高 | DS 表 5-9 |
| 整机 | 一般工作 120.2 mA @4.2 V；关机开关断开 0.23 µA | docs.m5stack.com/en/core/Cardputer-Adv |
| 电池 | 1500 mAh（2026-07-13 起；之前的批次是 1750 mAh） | 同上，change log |
| RTC GPIO | GPIO0–21。**G0 和 G11 都是**；背光 G38 不是 | DS §2.3.2 |
| 引脚 | 键盘 INT G11；I2C G8/G9；背光 G38（LEDC PWM）；LCD RST/DC/MOSI/SCK/CS = G33/34/35/36/37；SD CS/MOSI/CLK/MISO = G12/14/40/39；电池 ADC G10（分压 2:1）；RGB LED G21 | 原理图 v1.0；M5GFX；M5U/Power_Class.inl:585；M5U/M5Unified.inl:247-259 |
| 充电检测 | 充电芯片 TP4057 的 CHRG/STDBY 没接到 GPIO，也没有 VBUS 检测脚 | 原理图文字提取 [待：看图确认] |
| 深睡唤醒源 | ext0（一个 RTC 脚）；ext1（多个 RTC 脚，S3 上有 `ESP_EXT1_WAKEUP_ANY_LOW`）；定时器 | IDF sleep_modes（v4.4.7） |
| 浅睡唤醒源 | `gpio_wakeup_enable()` + `esp_sleep_enable_gpio_wakeup()`，任何 GPIO，电平触发；定时器；ext0/ext1 也能用 | 同上 |
| 自动 light sleep | **用不了**：Arduino 2.0.17 预编译库里 `CONFIG_PM_ENABLE` 没开，也没有 tickless idle。只能手动调 `esp_light_sleep_start()` | arduino-esp32 2.0.17 `tools/sdk/esp32s3/sdkconfig:1147` |
| 睡眠中的 USB | light sleep 和 deep sleep 中 USB-Serial-JTAG 都停。浅睡醒来后主机可能不再枚举，"可能需要拔线重插"。IDF 5 的 `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` 只管自动 light sleep，IDF 4.4 没有这个选项 | IDF v4.4.7 和 latest 的 usb-serial-jtag-console |
| 软件断开 USB | `USB_SERIAL_JTAG_CONF0_REG` 的 `PAD_PULL_OVERRIDE` + `DP_PULLUP`；RTC 域也有一套 `RTC_CNTL_USB_PAD_PULL_OVERRIDE` / `RTC_CNTL_USB_DP_PULLUP` | TRM 寄存器 33.3；TRM §RTC_CNTL |
| 时间 | RTC 定时器"在任何复位和睡眠模式下保持系统时间"，只有上电才清零。时钟源是内部 150 kHz RC，"深睡和浅睡里时间可能漂移" | IDF v4.4.7 system_time；sdkconfig:988 `RTC_CLK_SRC_INT_RC` |
| 唤醒延迟 | 浅睡：几十 µs 到约 1 ms（论坛实测）。深睡：就是重新开机，sdkconfig 里 `DEEP_SLEEP_WAKEUP_DELAY=2000` µs | esp32.com viewtopic p=152556；sdkconfig:993 |
| 一个可能多耗电的配置 | `CONFIG_RTC_CLOCK_BBPLL_POWER_ON_WITH_USB=y`：用到 USB 时 BBPLL 在睡眠中保持开着 | sdkconfig:1092 [待：测它的代价] |
| TCA8418 | 待机 3 µA；按键时拉低 INT | ti.com/product/TCA8418 |
| ST7789 睡眠 | SLPIN（0x10）后约 5–30 µA；SLPOUT（0x11）之后要等 120 ms 才能画 | 二手资料，done.land [待] |
| SD 卡空闲 | 0.1–1 mA，个别卡几 mA；卡槽没有电源开关 | 二手资料 [待] |

**别人怎么做的**：

- **M5Unified**：给 Cardputer / Cardputer ADV 只配了电池 ADC，没有唤醒脚（`_wakeupPin` 是 255），所以它的 `deepSleep()` / `lightSleep()` 只能定时唤醒 [源 M5U/Power_Class.inl:585]。它通用代码里的做法值得照搬：
  - 深睡用 ext0；用 ext1 `ANY_LOW` 时加 `rtc_gpio_pullup_en`；
  - 浅睡用 `gpio_wakeup_enable(LOW_LEVEL)`；
  - **睡前先等唤醒脚松开**（`_releaseWakeupPin`），否则一睡下去马上就醒。
- **M5Cardputer 官方 UserDemo**：ADV 分支里没有睡眠代码。
- **M5Launcher**：所谓"睡眠"只是关背光，醒来那一下被吞掉。
- **Bruce**：Cardputer 的 `powerOff()` 是空函数。

**也就是说，没有现成的 Cardputer ADV 唤醒实现可以照抄。**

## 3. 三层设计

### 3.1 状态图

```
                 有效按键（过了锁屏闸门的键）
        ┌──────────────────────────────────────────────┐
        ▼                                              │
     [亮屏] ──50 s──▶ [调暗] ──10 s──▶ [暗屏 + 锁]      │   第 0 层，今天已有
                                        │               │
                     闸门全过（§4），用电池              │
                                        ▼               │
                                   [浅睡] ◀──┐          │   第 1 层，新
                     每 60 s 自己醒一下：      │          │
                     读电池、看 USB 主机、再睡 ┘          │
                     G0 / 任意键 → 醒，键照常进锁屏闸门 ──┘
                                        │
         最后一次有效按键起满 2 h（设置），闸门全过；
         或电池 ≤3.45 V 持续 10 s
                                        ▼
                                   [深睡]                    第 2 层，新
                     G0 / 任意键 → 重新开机 → 锁着，锁屏卡亮 3 s
                     每 10 min 自己醒一下：不亮屏，只看 USB 主机和电池
```

### 3.2 每层的进入、唤醒、丢什么

| | 进入 | 唤醒 | 醒来多久能用 | 丢什么 |
|---|---|---|---|---|
| 暗屏（已有） | 用电池，60 s 没有有效按键 | 任意键 | 立刻 | 无 |
| 浅睡 | 背光已经是 Off（暗、锁着、卡没亮），用电池，§4 的闸门全过 | G0（GPIO0 低电平）、键盘 INT（G11 低电平）、60 s 定时 | < 1 ms 醒；进 loop < 5 ms [待] | 无。内存、页面、草稿、钟、boot id 都在 |
| 深睡 | 最后一次有效按键起满 `sleep_after`（默认 2 h），或电池到临界（§7），而且闸门全过、编辑器里没有没存的字 | ext1 `ANY_LOW`：G0 + G11；10 min 定时（只做探测，§6.3） | 约 1.5 s 亮出锁屏卡 [推：开机到 Ready 1.55–1.66 s，docs/07] | 内存：当前页面、toast、Tab 菜单位置；boot id 换新（hello 里 `rst:"deepsleep"`）。卡上、NVS、账本、离线标记都在；钟靠 RTC 带过去，标成"约"（§8.2） |

### 3.3 为什么浅睡不加宽限

屏幕一黑、锁上，浅睡就没有任何代价：

- 醒来不到 1 ms；
- 用电池时没有 USB 连接会被打断；
- 黑屏时能做的事（锁屏闸门、G0 手势）醒来后照样做。

加宽限只会多耗电，换不来什么。唯一要等的是**锁屏卡熄灭**：卡亮着说明有人在看，背光也不是 Off。

### 3.4 为什么深睡要等这么久

深睡的代价是两样：醒来要多等约 1.5 秒；内存里的东西会丢。2 小时覆盖了"白天放在桌上、隔一阵用一下"的情况，这时一直是浅睡，按下就有反应。"晚上忘了关、放包里一周"这种情况，就交给深睡。浅睡的实测电流如果足够低（≤1 mA），这个默认值可以放宽到 8 小时（👤 第 1 条）。

### 3.5 深睡醒来以后

- **醒来是锁着的，锁屏卡亮 3 秒**（和 docs/20 D7 的 `lock_engage(now, kLockFlashMs)` 一样）。睡前本来就是锁着的（用电池时暗屏即锁），醒来还是那个状态。兜里被按醒，也什么都录不上。
- **没人用就直接回深睡**：如果这次深睡醒来以后，一直到屏幕变黑都没有过一次有效按键，就不再进浅睡、等 2 小时，而是马上回深睡。这样，兜里一下误按只花约 1.5 s 的开机电量，不会让设备在浅睡里多耗 2 小时。
- 开机后，屏幕上第一个画面是锁屏卡加一行"醒来中…"。M5 初始化和显示初始化一完成就画，挂 SD 卡、开机扫描都在它之后。这样按下后约 0.3–0.5 秒就有反馈 [待]，不会让人以为没反应又多按几下。

## 4. 什么时候不睡（闸门）

浅睡和深睡共用一张闸门表。任何一条不满足，这一轮就不睡，下一轮 loop 再看。

| # | 条件 | 为什么 | 怎么知道 |
|---|---|---|---|
| G1 | 背光是 Off | 屏幕亮着或调暗着，就说明有人在看，或者在录音（● 规则） | `backlight_of(power_) == Off` [源 power.cpp:64] |
| G2 | 录音状态机是 Idle | 录便条、开会、确认框、收尾中都不睡。开会就是录音 | `rec_core_.phase == RecPhase::Idle` |
| G3 | USB 上没有主机 | 插着 Mac 时不睡：屏幕常亮、链接要通。插着 Mac 但 cpad 没跑，也一样不睡 | `tx_.plugged()` 为假，且 `plug_.plugged` 为假 |
| G4 | 账本空闲 | 没有发出去还没回的账本命令，没有清理的下一轮，没有待做的 tidy（docs/28 D17、D18：只有音频任务写账本，UI 等它回复） | `!kept_.busy && !kept_.drop_due && !kept_.tidy_due` |
| G5 | 音频任务空闲 | 开机扫描做完了，`file.*` 队列空，音频任务手上没有命令 | `rec_core_.queued == 0 && !rec_core_.at_task`，加上一个新的 `scan_done` |
| G6 | 插上提示不在 | docs/28 D11 的 5 秒提示和它的 Hold，要等 LedgerDone | `kept_.plug == PlugHint::None` |
| G7 | 没有键按着 | 按着键（G0 或者任意键）就不睡，否则一睡就醒，或者错过松开的边沿 | 这一轮的 `KeySet` 是空的；G0 的去抖已经结束 |
| G8 | 设置的写入落地了 | `settings_due_` 为假（录音结束后补写的那次） | runtime_settings.cpp |
| G9 | 测试钩子空闲 | `CPA_TEST_HOOKS` 构建里，debug 注入队列是空的 | debug_inject |
| G10（只挡深睡） | 编辑器里没有没保存的字 | 深睡会丢内存；草稿不能悄悄丢 | shell 的 compose 是否 dirty |

**USB 只供电、没有 Mac**（充电器、充电宝）：`isPlugged()` 为假，按电池处理，正常睡。睡着时 TP4057 照常充电（硬件充电，不需要 CPU）。代价是：充着电的时候屏幕也会熄、会锁。今天就是这样（docs/20 D2），不变。

## 5. G0 和启动引脚

docs/03 坑 2 [测]：按住 G0 **上电**，芯片会把 GPIO0 锁存为低电平，直到下次断电都进下载模式；Mac 发起的 USB 复位（`rst:0x15`）是内核复位，不会重新锁存。

- **数据手册**：引脚的锁存"在芯片复位（Chip Reset）时采样，一直保持到芯片断电或关闭" [源 DS v2.2 §3，第 1638 行附近]。
- **技术参考手册表 7.1-1**：
  - 芯片复位只有 0x01（上电、掉电检测、超级看门狗）；
  - **0x05 Deep-sleep reset 是 Core Reset**，和 0x15 USB (UART) reset 同一类；
  - 0x0F 掉电（brown-out）可以是芯片复位。
- **结论**：
  - 浅睡：不复位，G0 只是一个普通 GPIO 唤醒源。
  - 深睡：醒来是内核复位，不重新锁存 GPIO0。**按住 G0 醒来不会进下载模式**，和 docs/03 已经实测过的 USB 复位同一类。
  - **还没在真机验证**。网上找到过一篇 ESP32（不是 S3）"深睡醒来进了下载模式"的帖子，没有原因（community.mongoose-os.com/t/2334）。P0 第 4 项专门测：按住 G0 唤醒 20 次，`esp_reset_reason()` 都是 `ESP_RST_DEEPSLEEP`，固件都起来了。
- **要防的例外**：掉电复位（brown-out）可能是芯片复位，会重新锁存。电池耗到掉电时如果 G0 正好按着，就会卡在下载模式，直到拨开关断电。§7 的临界深睡就是要在掉电之前先把设备停下。
- 实现细节：ext1 唤醒以后，GPIO0 和 GPIO11 还是 RTC IO。要先 `rtc_gpio_deinit()`，再交给 M5Unified 和键盘读取器当数字脚用 [源 IDF sleep_modes]。

## 6. USB：睡着时插上 Mac 会怎样

### 6.1 问题

light sleep 时 USB-Serial-JTAG 没有 APB 时钟。插着主机时，主机会报"设备不响应"；醒来后 D+ 一直上拉着，主机可能不再枚举，"可能需要拔线重插" [源 IDF latest usb-serial-jtag-console]。我们本来就不在有主机时睡（G3），**但用户可能在设备睡着时插上 Mac**。这时不能出现"插了没反应、按键醒了也连不上"的卡死状态。

### 6.2 浅睡：先断开，定时接上看一眼

1. **进浅睡前**：用 `PAD_PULL_OVERRIDE` 关掉 D+ 上拉 [源 TRM 寄存器 33.3]。从 Mac 那边看，就是线没插。这时插上 Mac，Mac 什么都看不到，也就没有失败的枚举。
2. **每次醒来**（按键或定时）：先把上拉恢复，Mac 看到的是一次干净的"刚插上"。
3. **定时醒（每 60 s）**：
   - 读一次电池（约 2 ms）；
   - 把上拉接上，最多等 300 ms 看有没有 SOF（`HWCDC::isPlugged()`）；
   - 有主机：留在醒着的状态，进入插着 Mac 的规则（屏幕亮、链接起来、docs/28 的插上提示）；
   - 没有主机：再断开、再睡。
4. **代价**：每分钟醒 0.3 s，按约 45 mA 算，平均约 0.23 mA [推]。如果 P0 测出 macOS 300 ms 内发不出 SOF，就把窗口放宽到 500 ms，或者把间隔改成 120 s。
5. **你看到的**：睡着时插上 Mac，最多 1 分钟自己连上；按任意键立刻连上（醒来马上接上上拉）。

### 6.3 深睡：定时探测开机

- 深睡时数字域断电，D+ 上拉也没了，Mac 看不到设备。
- 每 10 min 定时醒一次，这是一次**探测开机**：`setup()` 一开头看复位原因是 `DEEPSLEEP`、唤醒原因是定时器，就只做三件事：
  - 起 USB；
  - 最多等 500 ms 看 SOF；
  - 用 `analogReadMilliVolts(10) × 2` 读电池。

  没有主机，就记一条电源日志（§9.4），马上回深睡。
- 探测开机**不初始化屏幕，不挂 SD 卡**。背光脚 G38 在睡前用 `gpio_hold_en` + `gpio_deep_sleep_hold_en` 拉低锁住，所以探测时不会闪屏。
- 有主机就按正常开机走：锁着，卡亮。
- **代价**：约 0.8 s × 40 mA / 600 s，平均约 0.05 mA [推]。

### 6.4 Mac 那边（cpad）

- 浅睡：设备从来不在连着 Mac 时睡，cpad 看不到区别。定时探测时，如果插着的是 Mac，Mac 看到的就是一次正常插入。
- 深睡：醒来是新开机，hello 里 `rst:"deepsleep"` [源 hal_platform.cpp:73]，`boot` 换新。cpad 已经把新 boot 当作正常重启处理，`rst` 只按 `^[a-z0-9_]{1,16}$` 校验 [源 daemon specs.py:135]。**cpad 不用改。**
- `debug.sleep` 测试钩子（§9.4）会故意在连着 Mac 时睡。这时 HIL 脚本要能接受串口消失再出现：cpad 本来就会处理拔插，HIL 侧要加重开端口的等待。

## 7. 低电

| 电压（用电池、不在录） | 动作 |
|---|---|
| ≤3.55 V（`kBatWarnMv`） | 深睡的等待时间缩到 **10 分钟**，不管设置是多少，"从不"也一样。锁屏卡上的电池字变成 Warn 色（已有） |
| ≤3.45 V（`kBatStopMv`）持续 10 s | **临界深睡**：<br>1. 屏幕亮 3 s，显示"电量低 · 已休眠 · 请充电"；<br>2. 编辑器里有没保存的字，就先存进 NVS（§7.1）；<br>3. 等闸门 G2、G4、G5 通过，最多 10 s；<br>4. 卸载 SD 卡；<br>5. 深睡 |
| 临界深睡后被按醒，或者定时探测 | 只读电池，不挂 SD 卡：<br>- 仍 ≤3.45 V、没有主机：亮 3 s"电量低 · 请充电"，再睡；<br>- 有主机（在充电）：正常开机 |

- 录音中不变：录音自己在 3.45 V 持续 10 s 时收尾（docs/11 §4.3）。收尾回到 Idle 以后，上表才开始起作用。
- **SD 卡不会坏**：
  - 进深睡之前，闸门保证音频任务空闲、账本命令都回了；
  - docs/28 D17 的写入顺序保证每一条账本记录在 ack 之前已经 fsync；
  - outbox 的写入是 loop 里同步完成的；
  - 最后再 `f_unmount`，CS 拉高锁住。

  这比"电压跌到掉电、写到一半断电"安全得多：账本最多丢最后一条，而这一条本来就是 fsync 前的那一条，后果只是"多交一次"（docs/28 §3.4）。
- 电压读数：休眠中醒来时读的是**空载电压**，比带载时高几十 mV [推]。临界判定沿用"持续 10 s"的规则，读数在醒着、带载时取。探测开机读到的空载电压只用来决定"要不要正常开机"，所以阈值要加 50 mV 的余量 [设，P0 校准]。

### 7.1 草稿

- **深睡等草稿**（G10）：编辑器里有没保存的字，就只浅睡，不深睡。浅睡不丢任何东西。
- **临界深睡不能等**：把草稿（编辑器的目标 + 文字，最长就是编辑器的上限）写进 NVS 键 `draft`。下次开机解锁后，编辑器里还是这段字，提示栏多一行"上次的草稿"。NVS 是日志式写入，掉电也安全。写入是在 3.45 V 时、还没到掉电电压的时候做的。

## 8. 时间

### 8.1 G0 按下到第一个样本

| 从哪里按 | 第一下做什么 | 到第一个样本 | 开头会不会悄悄丢 |
|---|---|---|---|
| 亮屏 / 调暗 | 按住就录 | 507 ms [测 docs/07:882,993] | 不会：一按下就显示"● 准备"（D8） |
| 暗屏 + 锁（今天用电池时） | 点一下点亮并武装，再按住 | 第二下起 507 ms | 同上 |
| **浅睡** | 和暗屏 + 锁一样 | 第二下起约 507–527 ms [推：醒来 < 1 ms + loop 恢复 + USB 上拉恢复（不阻塞）]。预算：比今天多 ≤ 20 ms | 同上 |
| **深睡** | 第一下只开机：约 1.5 s 亮出锁屏卡 | 卡亮以后再点一下、按住，从第二下起 507 ms；从第一下算起约 2.2–2.6 s [推] | 不会：卡没亮、"● 准备"没出来之前，说的话本来就没在录，屏幕会告诉你 |

- **codec 在睡眠里保持原样**，浅睡、深睡都不额外断电。
- 空闲时 M5Unified 已经让 ES8311 的模拟部分断电了（§1.2）。这样一来，浅睡醒来不需要重新"预热"（`warm()`，0.47 s，issue #31）[源 audio_io.h:13-18]。
- 深睡醒来是新开机，开机本来就跑一次 warm（在音频任务开机扫描之后）。它在锁屏卡亮出来之前多半已经做完了 [待]。

### 8.2 时钟

- **浅睡**：醒来后，IDF 用 RTC 定时器量出睡了多久，补到 `esp_timer` 上，所以 `millis()` 是连续的 [源 IDF system_time]。boot id 不变，`up_ms` 连续，cpad 看来就是同一次开机。
  - 漂移来自 150 kHz 内部 RC 的校准误差。数量没有公开数字，P0 第 6 项实测：睡 30 min，再连 Mac，比较 `up_ms` 和主机时钟。
- **深睡**：`millis()` 从 0 开始，但 RTC 定时器继续走。睡前把 `ClockRef`（docs/28 D16：上次见到的墙上时间 + 当时的 `up_ms`）和 `esp_rtc_get_time_us()` 一起写进 RTC 内存（`RTC_NOINIT_ATTR`，带魔数和 CRC）。醒来时：墙上时间 = 存的时间 + RTC 走过的时间。
  - **这个钟标成"约"**：状态栏、录音时间照常用它；docs/28 留底的"7 天删除"不用它，等下次 `time.set` 再说。这是 D16"钟不可信就不按时间删"的延伸。
- **拨开关断电**：时间丢失，和今天一样。板子上没有 RTC 芯片。
- `millis()` 是 32 位的，49.7 天回绕。设置成"从不"、一直浅睡的话，这是真的会碰到的。代码里的时间比较都是 `now - x` 无符号相减，回绕是安全的 [源 power.cpp:40 等]，但要加一条回绕测试（§9.3）。

## 9. 实现要点

### 9.1 分层（沿用 05 §5：纯逻辑 + 端口 + HAL）

| 层 | 文件 | 做什么 |
|---|---|---|
| 纯策略 | `fw/shell/sleep.h/.cpp`（新，约 150 行） | `SleepInputs`（闸门各项、电压、`sleep_after`、上次有效按键、`woke_deep_no_key`）→ `SleepPlan {None, Light{max_ms}, Deep{why}}`；低电和临界的计时（10 s 持续）也在这里 |
| 运行时 | `fw/shell/runtime_sleep.cpp`（新，约 120 行）；`runtime.h` 加几行 | 收集闸门，给出 `sleep_plan(now)`；`woke(cause, now)` 处理醒来（恢复 USB 上拉、标记有效键）；`park_for_deep(now)` 让音频任务收尾、卸 SD、写 RTC 记录 |
| 端口 | `fw/shell/platform.h` | 加 `virtual WakeCause light_sleep(const LightSpec&)`、`virtual void deep_sleep(const DeepSpec&)`、`virtual void usb_attach(bool)`，默认都是空操作。`FakePlatform` 和模拟器各实现一份 |
| HAL | `fw/hal/sleep_hal.cpp/.h`（新，约 200 行） | `esp_sleep_*`；`gpio_wakeup_enable(0/11, LOW)`，醒来后还原；ext1 `ANY_LOW` + `rtc_gpio_pullup_en`；`PAD_PULL_OVERRIDE`；G38、G12、G37 的 hold；I2C 锁跨睡眠持有；睡前等 G11 变高（照搬 M5Unified `_releaseWakeupPin`），超时就不睡 |
| HAL | `fw/hal/kb_reader.cpp/.h`（新，约 100 行） | 自己的 TCA8418 读取器，经 `Keyboard.begin(std::unique_ptr<KeyboardReader>)` 注入。比原版多两件事：<br>- `drain()`：睡前把 FIFO 读空、清 INT；<br>- `kick()`：醒来后 INT 还低就直接置标志。<br>一次 `update()` 把 FIFO 读到空，不再一次一个 |
| HAL | `fw/hal/rtc_keep.cpp/.h`（新，约 80 行） | RTC 内存记录：`ClockRef`、深睡原因、`woke_deep_no_key`、电源日志环（64 条 × 8 B） |
| 音频任务 | `fw/hal/audio_task.*` | 加 `idle()`（扫描完、没有命令）和 `park()` 命令（关文件、`f_unmount`、回复）。只有音频任务碰 SD（docs/28 D18） |
| 入口 | `src/main.cpp` | `loop()` 末尾问 `sleep_plan()`，浅睡就调 `light_sleep()` 再 `woke()`；`setup()` 最前面是深睡探测开机的分支（§6.3） |
| 设置 | `fw/core/settings.h`；`fw/hal/nvs_settings.*`；`modules/system/settings_logic.*`、`system_render.cpp`、`system_texts.h`；`fw/module_api` 的 `ModuleContext` | `SleepAfter {M30, H2, H8, Never}`，NVS 键 `sleep`（u8，读不到就用默认 H2）；设置页第三行；`ModuleContext` 加 `sleepAfter()` / `setSleepAfter()`，带默认实现，第三方应用不受影响 |

### 9.2 浅睡的顺序（`sleep_hal.cpp`）

1. 拿 I2C 锁（音频任务空闲，但不能让它在中途碰 codec）。
2. `kb.drain()`；G11 是高电平、G0 是高电平，否则放弃这一轮。
3. 背光 PWM 本来就是 0；G38 `gpio_hold_en`。
4. USB 上拉断开。
5. `gpio_wakeup_enable(GPIO_NUM_0, LOW)`、`gpio_wakeup_enable(GPIO_NUM_11, LOW)`、`esp_sleep_enable_gpio_wakeup()`、`esp_sleep_enable_timer_wakeup(60 s)`。
6. `esp_light_sleep_start()`。
7. 醒来：
   - `gpio_wakeup_disable` 两个脚，还原 G11 的 CHANGE 中断类型；
   - G38 `gpio_hold_dis`；
   - 读 `esp_sleep_get_wakeup_cause()`；
   - 是键盘唤醒，就 `kb.kick()`；
   - 放 I2C 锁。
8. 定时唤醒：读电池，上拉接上，等 SOF 最多 300 ms（这段时间每 10 ms 看一次键），没有就回第 2 步。

- **防止醒了又醒的死循环**：连续 5 次是 GPIO 唤醒、却读不到任何键，就 10 分钟内不再浅睡，在电源日志里记一条 `spurious`。
- **LCD 要不要 SLPIN**：先不发（D14）。P0 测"浅睡时 LCD SLPIN 和不发"的差，≥1 mA 再加。加的话，醒来时 SLPOUT 不阻塞：发命令，记时间，120 ms 内不画、不开背光。

### 9.3 测试（每期先写，RED → GREEN）

**主机（`test/core`，纯逻辑）**

- `test_sleep_policy`：
  - G1–G10 每条单独挡住，表驱动；
  - 浅睡从背光 Off 那一刻开始；
  - 深睡从上次有效按键起算，被吞的键不算（兜里乱按不续命）；
  - 2 h / 30 min / 8 h / 从不；
  - ≤3.55 V 缩到 10 min（从不也缩）；
  - 3.45 V 持续 9.9 s 不动、10 s 临界；
  - 深睡醒来没人用就回深睡；
  - `now` 从 `0xFFFF0000` 起跨过回绕。
- `test_settings`：`SleepAfter` 的默认值、相等、`save_when`；NVS 字节读不懂就用默认值。

**界面（`test/ui`，`FakePlatform` 记录 `light_sleep` 调用）**

- `test_runtime_sleep`：
  - 录音、开会、确认框、插着主机、账本命令在途、tidy 待做、插上提示、键按着、锁屏卡亮着，各自不睡；
  - 浅睡醒来的那个键只送进锁屏闸门一次；
  - Opt+L 在浅睡里按下（Opt 是唤醒键），醒来后 L 也到，能点亮卡（防 FIFO 被清掉，§1.3）；
  - G0 点一下再按住，便条照常开始，`FakeRecPort` 记的开始时刻和不睡时相同；
  - 编辑器有草稿只浅睡、不深睡；
  - 临界时草稿进 NVS，下次开机回来。
- `test_settings_sleep_row`：
  - 光标 `;` `.` 能停在第三行，Enter 轮换四个值；
  - 中英文行宽、toast 宽度都在边界里（`test_text_width`，`i18n/width.py`）；
  - 离开再进来光标回到 WiFi 行（docs/21）。
- 已有的 `test_lock_power`、`test_lock_ux`、`test_plug_hint`、`test_runtime_kept` 全部照过。

**模拟器（`firmware/sim`）**

- `SimPlatform::light_sleep()`：虚拟时钟直接快进到下一个脚本按键或 60 s 轮询，返回唤醒原因。`deep_sleep()`：走 `SimDevice` 已有的重启路径，带上 RTC 记录。
- 新脚本 `scripts/ux/sleep.cpas`，截图这些画面：
  - 设置页三行（中、英）；
  - 电量低卡；
  - 深睡醒来的"醒来中…"卡；
  - 深睡醒来后锁屏卡；
  - 草稿恢复。
- `cpa-sim serve` + `tools/hil` 新场景 `sleep_sim`：用 `debug.sleep` 让虚拟设备浅睡、深睡，检查 boot id、`up_ms`、`rst`。

**真机自动（`test/device`，测试构建）**

- `test_sleep_hal`：
  - 定时浅睡 2 s，醒来原因是 TIMER，`esp_timer` 前进约 2 s；
  - 醒来后 I2C 能读 TCA8418，SD 能读一个文件；
  - 连续 100 次没有 spurious；
  - `kb_reader` 的 drain / kick 对 FIFO 里预先放的事件（靠 TCA8418 的寄存器可以注入吗？[待]，不行就人工按）。

**HIL（串行、要设备，`tools/hil`）**

- `sleep_usb`：`debug.sleep {"tier":"light","ms":5000}` → 端口消失 → 5 s 后定时醒，上拉接上 → 2 s 内端口回来，cpad 重新 Ready，boot 不变，`up_ms` 连续且多了约 5 s。重复 20 次，0 次卡住。
- `sleep_deep`：`debug.sleep {"tier":"deep","ms":10000}` → 新 boot、`rst:"deepsleep"`，3 s 内 Ready。20 次。
- `sleep_gates`：录音中、开会中、有 `file.*` 请求在途、有账本命令时，`debug.sleep` 返回拒绝和原因，不睡。

**人工闸门（要你按，H-S1–H-S7，记入 docs/07）**

| 闸门 | 怎么做 | 通过 |
|---|---|---|
| H-S1 G0 深睡唤醒 | 深睡后按住 G0 唤醒，20 次，其中 10 次按住 3 s 以上 | 20/20 正常开机，`rst=deepsleep`，没有一次进下载模式 |
| H-S2 浅睡唤醒按键 | 浅睡后分别用 G0、字母键、Opt+L 唤醒 | 和今天暗屏锁着时完全一样；Opt+L 一次就亮卡 |
| H-S3 浅睡便条 | 浅睡后点 G0、再按住说话，10 次 | `memo_lead_ms`（按下到第一个样本）≤ 今天 + 20 ms |
| H-S4 睡着插 Mac | 浅睡、深睡时各插 5 次 | 浅睡 ≤ 60 s、深睡 ≤ 10 min 自己连上；按任意键 ≤ 3 s 连上；0 次要拔线 |
| H-S5 只插充电器 | 插充电宝，放 30 min | 正常睡；拔掉再插 Mac 能连 |
| H-S6 低电 | 放电到 3.5 V 附近，放着 | 10 min 进深睡；到 3.45 V 出电量低卡；SD 卡开机扫描无坏块 |
| H-S7 兜里 | 锁着放兜里走 30 min | 没有误录、没有误存；深睡被按醒后 60 s 内回深睡 |

### 9.4 不用电流表也能量电流

- **电源日志**：RTC 内存里一个 64 条的环。每条记 RTC 秒数、电压（mV）、层级（暗屏 / 浅 / 深 / 探测）、唤醒原因。浅睡每 60 s、深睡每 10 min 写一条。深睡、软复位都不丢，拨开关断电才丢。
  - 读法：插上 Mac 以后，测试构建用 `debug.state.power` 读；正式构建在设置的"设备"页加一行"耗电 −8 mV/小时（上次休眠 6 小时）"。
- **测法**（P0、P2、P3 各做一次）：
  1. 充满，拔线，静置 30 min（让电压回落）；
  2. 让设备停在要测的那一层，放 8 小时（过夜）；
  3. 插上读日志；
  4. 取 3.7–3.9 V 平台段的 mV/小时。
- 平台段里，电压斜率和电流近似成正比。先测一次**今天的暗屏**（`debug.sleep {"off":true}` 关掉休眠）作为基准。这个基准再用一次满电到自动停的放电时间，换算成 mA：
  - 基准电流 ≈ 1500 mAh ÷ 小时数；
  - 其他层的电流 = 基准 × 两者斜率之比。
- 深睡的斜率可能小到 8 小时里看不出来（ADC 分辨率和噪声大约是 10–20 mV）。那就放 3 天，或者只记"≤ 某个上限"。
- **唤醒延迟**：测试构建在醒来时记三个时间：`esp_timer`（醒来那一刻）、第一次进 loop、第一个键事件。另外记 `memo_lead_ms`。都放在 `debug.state.power.last_wake` 里。

## 10. 分期

| 期 | 做什么 | 动哪 | 测试先写 | 预算 | 风险 |
|---|---|---|---|---|---|
| **P0 测量和探针** | 电源日志 + `debug.state.power` + `debug.sleep`（只在测试构建）。在真机上测：<br>① 今天暗屏的基准斜率，加一次满放；<br>② 浅睡电流：LCD SLPIN 发与不发，SD 挂与不挂；<br>③ 深睡电流：BMI270、RGB LED 的状态；<br>④ G0 深睡唤醒 20 次（H-S1）；<br>⑤ D+ 上拉断开 / 接上后 macOS 能不能干净重枚举，SOF 多久出来；<br>⑥ 浅睡 30 min 的时钟漂移；<br>⑦ `BBPLL_POWER_ON_WITH_USB` 的代价 | `fw/hal/rtc_keep.*`、`sleep_hal.*`（探针版）、`runtime_debug.cpp`、`tools/hil` | `test_sleep_hal`（device）；`sleep_usb`、`sleep_deep`（HIL） | 只在测试构建；正式构建 0 字节 | ⑤ 不行，整个浅睡方案要改（§12 R1） |
| **P1 暗屏少转** | 用电池、背光 Off 时，`loop()` 每轮末尾 `vTaskDelay(10 ms)`，让 idle 任务进 WAITI。不改频率，不碰 USB | `src/main.cpp`；`runtime` 给出 `idle_slow()`（就是 G1 + G2） | `test_runtime_sleep` 的前半（什么时候慢转）；HIL `rec_memo` 回归 | 按键采样多 ≤ 10 ms；`rec_memo` 的 507 ms 不变（±10）；芯片电流从约 40–60 mA 降到约 25–35 mA [推 DS 表 5-9] | 低。录音时不慢转（G2） |
| **P2 浅睡** | §9.2 全部；自己的键盘读取器；USB 断开 / 探测；电源日志进正式构建；设备页一行 | §9.1 表里除深睡、设置以外的部分 | §9.3 的 core、ui、sim、device、`sleep_usb`；H-S2–H-S5 | flash ≤ 6 KB，RTC 内存 ≤ 600 B，RAM ≤ 128 B；loop 里每轮的判断 ≤ 20 µs；醒来到 loop ≤ 5 ms；浅睡平均 ≤ 3 mA（含轮询）[目标，P0 定] | 中：USB、键盘中断、虚假唤醒 |
| **P3 深睡 + 设置 + 低电** | ext1 唤醒；探测开机；`park()`；RTC 带钟；锁着醒来、没人用就回深睡；设置第三行（中英）；≤3.55 V 缩短、3.45 V 临界深睡；草稿进 NVS | §9.1 的深睡、设置、`audio_task` `park()` | `test_sleep_policy` 全部；`test_settings_sleep_row`；`sleep_deep`；H-S1、H-S6、H-S7 | 深睡平均 ≤ 0.5 mA（含 10 min 探测）[目标]；按下到"醒来中…"卡 ≤ 0.5 s、到锁屏卡 ≤ 2 s [待]；设置页仍在 111 px 内容区里 | 中：开机路径、SD 卸载、低电读数 |
| **P4 Mac 补一句** | README 和 Mac app 的"没看到设备？"里加一句"设备可能在休眠：按任意键" | `README.md`、Mac app 文案 | 无 | — | 低 |

**P1 可以马上做**：不依赖 P0，收益大约是今天暗屏电流的一半，风险几乎为零。P2、P3 的参数（轮询间隔、窗口、深睡默认值）等 P0 的数字出来再定。

### 10.1 设置页（P3）

设置页加第三个可改的行。分隔线从 y=55 移到 y=69，事实行从 y=72 起。两行事实到 y=96，仍在内容区（y≤122）里 [算，docs/21 §4.2 的布局]。

```
[头] ↑0                         周三 09:41 94%
(设置) (设备) (存储)
▸WiFi              无 · 只用 USB 线
 语言 Language     中文
 休眠              2 小时
 ─────────────────────────────────────
 连接   USB 线 · MacBook            在线
 同步   3 分钟前 · 待传 ↑0 录2

[;.]选 [Enter]改 [`]返回 [Tab]菜单
```

- 按键：Opt+M、`.`、`.`、Enter。每按一次 Enter 换一个值：30 分钟 → 2 小时 → 8 小时 → 从不 → 30 分钟。从默认改一次要 4 下。
- 反馈是**无声的**：
  - 行里的值当场变；
  - 提示栏位置出一个 toast，说清楚意思。
- NVS 写入走 docs/21 §6.8 的 `save_when`（录音中改的，录完再写）。
- `Txt` 对（都要过 `i18n/width.py`）：

| 键 | 中文 | English |
|---|---|---|
| `kSleepRow` | 休眠 | Sleep |
| `kSleep30m` | 30 分钟 | 30 min |
| `kSleep2h` | 2 小时 | 2 h |
| `kSleep8h` | 8 小时 | 8 h |
| `kSleepNever` | 从不 | Never |
| `kSleepToast` | {N}不用就深睡 · 任意键醒 | Deep sleep after {N} idle · any key wakes |
| `kSleepToastNever` | 不深睡 · 黑屏时仍浅睡 | No deep sleep · light sleep when dark |
| `kWaking` | 醒来中… | Waking… |
| `kBatSleep` | 电量低 · 已休眠 · 请充电 | Battery low · sleeping · please charge |
| `kBatCharge` | 电量低 · 请充电 | Battery low · please charge |
| `kDraftBack` | 上次的草稿 | Draft restored |
| `kPowerRow`（设备页） | 耗电 | Drain |

## 11. 决定和要你拍板的事

### 11.1 决定

| # | 题目 | 默认 | 备选 | 为什么 | 见 |
|---|---|---|---|---|---|
| D1 | 分几层 | 暗屏（已有）→ 浅睡 → 深睡 | 只有深睡；IDF 自动 light sleep | 浅睡醒来不到 1 ms、什么都不丢，覆盖日常；深睡管"忘了关一周"。自动 light sleep 要重编 Arduino 库 | §3 |
| D2 | 浅睡何时进 | 背光 Off 的那一刻，闸门全过，不加宽限 | 暗屏后再等 N 分钟 | 黑屏时浅睡没有代价；锁屏卡亮着就不算 Off | §3.3 |
| D3 | 浅睡唤醒源 | G0、键盘 INT（G11）、60 s 定时 | 只有 G0 | 和今天黑屏时所有键都有效一致；定时器用来看电池和 USB | §3.2 |
| D4 | 醒来的那一下 | 浅睡：照常送进锁屏闸门（和今天黑屏一样）。深睡：只开机，亮锁屏卡 3 s | 深睡的那一下也算数 | 浅睡要和今天一模一样；深睡开机前不知道按的是什么 | §3.5、§8.1 |
| D5 | 什么时候不睡 | §4 的 G1–G10 | — | 录音、开会、连着 Mac、账本、传输都不能被打断 | §4 |
| D6 | USB 只供电 | 按电池处理，照常睡、照常充电 | 插着就不睡 | 板子分不出充电器和"没跑 cpad 的 Mac"以外的情况；睡着照样充电 | §4 |
| D7 | 浅睡时的 USB | 睡前断开 D+ 上拉，醒来接上；每 60 s 探测 300 ms | 不断开；只靠按键 | 不断开会出现"插了 Mac 连不上、要拔线"的卡死 | §6.2 |
| D8 | 深睡默认 | 最后一次有效按键起 2 小时；设置可选 30 分钟 / 2 小时 / 8 小时 / 从不 | 30 分钟；8 小时；没有深睡 | 一天里隔一阵用一下不会碰到深睡；放一夜就进深睡 | §3.4 👤 1 |
| D9 | 深睡丢什么 | 内存（页面、toast）；有没保存的草稿就不深睡；钟靠 RTC 带过去，标"约" | 草稿存起来再深睡 | 草稿不能悄悄丢；带过去的钟不能用来删留底 | §3.2、§7.1、§8.2 |
| D10 | 深睡唤醒 | ext1 `ANY_LOW`：G0 + G11；每 10 min 探测开机，不亮屏 | 只有按键 | 睡着插上 Mac，最多 10 分钟自己连上，代价约 0.05 mA | §6.3 👤 3 |
| D11 | 深睡醒来的样子 | 锁着，先亮"醒来中…"，再亮锁屏卡；没人用就直接回深睡 | 直接到主页 | 睡前就是锁着的；兜里按醒不能变成没锁 | §3.5 👤 2 |
| D12 | 低电 | ≤3.55 V 深睡等 10 分钟；≤3.45 V 持续 10 s 临界深睡；醒来仍低就亮卡再睡 | 只警告 | 在掉电复位之前停下来：既护住 SD 卡，也躲开 G0 重新锁存 | §7、§5 |
| D13 | 设置项 | 设置页第三行"休眠"，Enter 轮换；无声，值当场变 + toast | 放在设备页；不给改 | 只有深睡时间值得让你改；浅睡对你是透明的 | §10.1 |
| D14 | 浅睡时的 LCD | 先不 SLPIN；P0 测出 ≥1 mA 再加，SLPOUT 不阻塞 | 一定 SLPIN | 少一个"醒来 120 ms 不能画"的变量 | §9.2 |
| D15 | 第一步 | P1 暗屏少转（`vTaskDelay`），先拿一半收益 | 直接做浅睡 | 零风险，独立可测 | §10 |
| D16 | 怎么量 | RTC 电源日志 + 电压斜率 + 一次满放标定；设备页一行"耗电" | 买电流表 | 你没有电流表；斜率能比出各层的倍数 | §9.4 |

### 11.2 要你拍板

1. **深睡默认几小时**（D8）：默认 2 小时。你如果常常隔半天才用一次，又不在乎醒来多等 1.5 秒，可以选 30 分钟。你如果希望一整天按下去都是立刻有反应，就选 8 小时（P0 测出浅睡 ≤1 mA 时，我会建议改成 8 小时）。
2. **深睡醒来是锁着的**（D11）：默认锁着，卡亮 3 s，和睡前一样。代价：深睡醒来想打字，要 Opt+L、Enter 解锁，多 2 下。备选：直接到主页，兜里被按醒时会有约 60 秒没锁。
3. **睡着时插上 Mac 要多久自己连上**（D7、D10）：默认浅睡 1 分钟、深睡 10 分钟，代价各约 0.23 mA、0.05 mA。备选：只靠按键唤醒，最省电，但插上以后屏幕不亮、Mac 也看不到设备，你得记得按一下。
4. **临界电量时的草稿**（§7.1）：默认存进 NVS，下次开机放回编辑器。备选：不存，屏幕上提示"草稿没保存"。

## 12. 风险

| # | 风险 | 后果 | 怎么办 |
|---|---|---|---|
| R1 | D+ 上拉断开 / 接上在 macOS 上不能触发干净的重新枚举（IDF 4.4 HWCDC 有自己的状态） | 睡着时插上 Mac 要拔线 | P0 ⑤ 先验证。不行就改成：浅睡前 `HWCDC::end()`，醒来再 `begin()`；还不行，就只在"上次没连过 Mac 超过 N 分钟"时浅睡，并在设备页说明 |
| R2 | `BBPLL_POWER_ON_WITH_USB=y` 让浅睡时 PLL 不关 | 浅睡电流高几 mA | P0 ⑦ 测。若是，睡前停 USB 外设时钟试试；预编译库改不了这个选项 |
| R3 | 整机漏电大（LDO、RGB LED G21、SD 卡、BMI270、G10 分压） | 深睡收益比估计的小 | P0 ③ 测；可控的（BMI270 suspend、SD CS 高、LED 数据脚拉低锁住）都做；不可控的写进文档 |
| R4 | G0 深睡唤醒在真机上进了下载模式（和手册不符） | 按 G0 唤醒变砖，要拨开关 | P0 ④ 先测 20 次。真出问题，深睡唤醒只用 G11（键盘），G0 只做浅睡唤醒 |
| R5 | 键盘中断标志在睡眠中丢失；一次一个事件读不完 | 醒来第一键、Opt+L 丢掉 | 自己的读取器 `drain()` / `kick()`；`test_runtime_sleep` 的 Opt+L 用例 |
| R6 | 虚假唤醒循环（INT 一直低、G0 抖动） | 睡不着，比不睡还费电 | 睡前确认两个脚都是高电平；连续 5 次空唤醒就停 10 分钟并记日志 |
| R7 | RC 慢时钟漂移 | 浅睡多日后 `up_ms` 和墙上时间偏几分钟 | P0 ⑥ 测；下次 `time.set` 校正；"约"的钟不用来删留底 |
| R8 | 选"从不"时连续浅睡超过 49.7 天，`millis()` 回绕 | 某处没用无符号相减的比较出错 | 回绕测试；低电规则会在那之前进深睡 |
| R9 | 睡眠里任务看门狗、I2C 锁、M5Unified 的内部状态 | 醒来卡住或重启 | I2C 锁跨睡眠持有；`test_sleep_hal` 连续 100 次；看门狗在 light sleep 里随时钟停 [待] |
| R10 | 浅睡时 LEDC 背光脚的电平不确定 | 屏幕微亮、多耗电 | G38 `gpio_hold_en` 拉低 |
| R11 | 空载电压比带载高 | 探测开机误以为电量够 | 阈值加 50 mV 余量，P0 校准 |
| R12 | 浅睡从 FakePlatform 上测不出硬件问题 | 主机测试全绿，真机出错 | 每期都有 device 和 HIL 闸门；人工 H-S1–H-S7 |

## 附：证据索引

- **固件**：
  - `fw/shell/power.h:1-23`、`power.cpp:14-74`；
  - `fw/shell/runtime.cpp:37-64`（开机）、`169-217`（按键、计时、背光）；
  - `fw/shell/runtime_kept.cpp:155-164`（`pump_ledger`）；
  - `fw/shell/lock.h:1-24`；
  - `fw/usb_transport/usb_transport.cpp:25`；
  - `fw/hal/keyboard.cpp`；`fw/hal/hal_platform.cpp:14-24,65-77`；`fw/hal/m5_display.cpp:11,76-80`；`fw/hal/audio_io.h:1-20`；
  - `fw/rec/rec_core.h:56,62`；`fw/help/menu_model.cpp:15-16`；`fw/core/settings.h`；
  - `modules/system/settings_logic.h:1-40`；`src/main.cpp`。
- **库**：
  - `M5C/utility/Keyboard/KeyboardReader/TCA8418.cpp:13,28-66`；`M5C/utility/Keyboard/Keyboard.h:152-153`、`Keyboard.cpp:15-37`；
  - `M5U/M5Unified.inl:247-259`（RGB LED G21）、`1045-1067`（扬声器）、`1537-1558`（麦克风开关 ES8311）、`3573-3576`（G0）；
  - `M5U/utility/Power_Class.inl:585`。
- **文档**：
  - docs/03 坑 2；
  - docs/07:267,547（重启到 Ready）、882,993（便条 507 ms）、570,630（G-A7 续航还没测、没有电压日志）；
  - docs/11 §4.3；docs/13 §7；docs/20 D2、D7、D8；docs/21 §4.2、§6.8；docs/28 D11、D16–D18。
- **外部**：
  - ESP32-S3 数据手册 v2.2 §3、表 5-9、表 5-10；
  - ESP32-S3 TRM v1.8 表 7.1-1、寄存器 33.3（`USB_SERIAL_JTAG_CONF0_REG`）、RTC_CNTL USB pad 寄存器；
  - IDF v4.4.7：sleep_modes、system_time、usb-serial-jtag-console（以及 latest 版的同一页）；
  - arduino-esp32 2.0.17 `tools/sdk/esp32s3/sdkconfig`（988、993、1081-1092、1147 行）；
  - docs.m5stack.com/en/core/Cardputer-Adv 和原理图 Sch_M5CardputerAdv_v1.0；
  - github.com/m5stack/M5Unified `Power_Class.inl`；
  - esp-adf `es8311.c` 的 suspend 序列（本设计不需要，M5Unified 空闲时已经断了模拟部分）；
  - ti.com/product/TCA8418；
  - esp32.com viewtopic p=152556（浅睡唤醒延迟）；
  - community.mongoose-os.com/t/2334（反例，没有原因）；
  - M5Launcher、Bruce 的 Cardputer `interface.cpp`。
