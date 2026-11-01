[English](launcher.md) · **中文**

# Launcher 兼容性调研（v0.2 闸门：用 bmorcelli Launcher 安装和切换 + microSD 存储）

> 2026-10-20，只读源码和文档，**没有接设备**。目标：v0.2 起让本固件能被 [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) 安装、切换，并把主存储放到 microSD。
> 标记：**[源]** 读源码或官方文档得出 · **[算]** 按源码规则推算出的数字 · **[待]** 需要真机验证（第 8 节列成闸门）。
> 读的版本：Launcher `main` = `e8cc4df`（2026-10-16，含 PR #448），最新正式版 `2.9.1`（2026-09-04）。Launcher 的 bootloader 来自 [bmorcelli/myLibBuilder](https://github.com/bmorcelli/myLibBuilder) 的 `launcher_keyboot` 分支（`cf56f6a`），2.9.1 用的是 `launcher` 分支（`386ffd3`）。下文链接里的 `L/` 指 `https://github.com/bmorcelli/Launcher/blob/e8cc4df/`。

## 0. 结论速览

| 问题 | 结论 |
|---|---|
| 1 分区表 | Launcher 自己只占 `nvs 0x9000/0x5000`、`otadata 0xE000/0x2000`、`app0`（**test** 子类型）`0x10000/0x150000`、`coredump 0x160000/0x10000`。其余 **0x170000–0x800000（6,881,280 B，约 6.56 MiB）全部是动态空间**：每装一个固件，就新建一个 `ota_N` 分区，大小 = 镜像大小向上对齐到 64KB。数据分区（SPIFFS/LittleFS/FAT）照固件自带分区表里的**标签**新建；空的数据分区在 8MB 机型上默认只给 **0x70000（448KB）** |
| 2 安装什么文件 | app-only `.bin` 和合并镜像都收。合并镜像**只当元数据读**：从文件 0x8000 处的分区表找第一个 app 条目和数据条目，只写 app 和数据分区的内容；**bootloader、分区表本身、nvs/otadata/phy 都不写**。写完用 `esp_image_verify` 校验。不需要额外的头或 manifest，显示名和分区标签取自**文件名** |
| 3 怎样回到 Launcher | Launcher 用**自己改过的二级 bootloader**：上电复位（电源开关、RST 键、芯片级掉电复位）和深睡唤醒会进 Launcher；`esp_restart()`、panic、看门狗、USB 触发的复位都直接回到当前选中的固件。ADV 上没有"按住某键进 Launcher"（键盘走 I2C，bootloader 读不到；G0 被排除）。固件**什么都不用做**，只要别碰 otadata、分区表，也别擦整个 NVS |
| 4 装 Launcher / 回到全量刷写 | 网页刷写器或 esptool，把 `Launcher-m5stack-cardputer.bin` 写到 **0x0**。回到我们自己的全量刷写：照常 `pio run -t upload`，或者把我们的 `-full.bin` 写到 0x0。这样会覆盖 bootloader、分区表和 Launcher 本体，NVS 原样留下 |
| 5 SD | 引脚和我们一样：CS12 / SCK40 / MISO39 / MOSI14。Launcher 平时用 Arduino `SD` 的默认 **4 MHz**；进入 U 盘模式时升到 40 MHz（校验失败就退到 20 MHz，再退回 4 MHz）。卡要求 FAT32 + MBR。Launcher 在卡上用 `/config.conf`、`/downloads/`、`/bkp/`、`/Launcher_error.log`；我们的东西放 `/cpa/` 就不会冲突。**我们的工具链（IDF 4.4）不支持 exFAT**，只能用 FAT32 |
| 6 方案 | **不另建固件 env**：一个二进制，运行时检测。把 LittleFS 分区从 `spiffs` 改名为专属标签 `cpafs`（偏移不变）；发布的 `-full.bin` 既能整片刷写，也能交给 Launcher 安装。主存储放 SD，`cpafs` 只作后备。最大的坑：`spiffs` 这个标签在 Launcher 里是**多个固件共享**的（Bruce 也用），而我们现在的 `LfsStore` 遇到挂不上的分区会直接格式化 |

## 1. 分区表（8MB ADV）

**Launcher 自己的分区表** [源] `L/support_files/custom_8Mb.csv#L3-L6`，由 `L/boards/m5stack-cardputer/platformio.ini#L14` 选用。同一个 env 同时覆盖 Cardputer 和 ADV：运行时检测 TCA8418，见 `L/boards/m5stack-cardputer/interface.cpp#L98-L120`。

```
nvs,      data, nvs,      0x9000,   0x5000
otadata,  data, ota,      0xE000,   0x2000
app0,     app,  test,     0x10000,  0x150000   ← Launcher 本体（ESP32-S3 上用 test 子类型）
coredump, data, coredump, 0x160000, 0x10000
（0x170000–0x800000 空闲，按需动态分配）
```

- **装进来的固件放在哪**：`launcherPartitionCreateOtaApp` 取下一个空闲的 `ota_N` 子类型，在空闲区找一段，大小 = `alignUp(镜像大小, 0x10000)`（`L/src/partition_table_model.cpp#L666-L697`）。所以**分区正好装下镜像，没有给 OTA 自升级留余量**。
- **最大能装多大**：理论上限 = 剩余空闲空间。一个都没装时是 6,881,280 B [算]。安装布局按以下顺序尝试，前一步成功就停：直接在空闲区新建 → 先建数据分区再建 app → 删掉孤立的数据分区。都不行时，再让用户在"复用某个分区 / 重新分区 / 删掉某个固件和它的数据"之间选（`L/src/partition_install_layout.cpp#L73-L137`，`#L139-L411`）。
- **约束**：生成的表里必须保留 FACTORY 或 TEST 分区和 otadata，app 按 64KB 对齐（`L/src/partition_table_model.cpp#L274-L352`）。`nvs 0x9000/0x5000` 和 `otadata 0xE000/0x2000` 写死成"必需的系统分区"（`L/src/idf/idf_update.cpp#L25-L28`）。
- **余量提醒**：beta 版 Launcher 的 app 部分 1,363,536 B，已经占了 0x150000 的 99%，以后可能换成 `custom_8Mb2.csv`（app0 0x170000）。到那时空闲区会少 128KB [算]。

**数据分区**：只在固件的合并镜像里自带分区表时才会建，见 `L/src/sd_functions.cpp#L851-L917`。

| 镜像里的条目 | Launcher 建出来的分区大小（8MB：`DEFAULT_SPIFFS_SIZE = 0x70000`，`THRESHOLD = 0x500000`） | 依据 |
|---|---|---|
| SPIFFS/LittleFS，**内容为空**（文件在该偏移处已经结束，或者前 16 字节全是 FF 或全是 00），声明大小 ≤5MB | **0x70000**（不论标签是什么） | `#L858-L862`，`partition_table_model.cpp#L188-L198`，board ini `#L29` |
| 非空，标签 ≠ `spiffs`，声明 > 0x70000 | 按声明大小建，并拷贝内容 | `#L863-L864` |
| 非空，标签 = `spiffs`，声明 > 5MB | 用掉剩余的最大空闲段 | `#L865-L866` |
| FAT，标签 `sys`/`system` | 按内容整块拷贝；其他 FAT 标签默认 0x70000 | `partition_install_layout.cpp#L36-L46` |
| 拷进来的 LittleFS 镜像比原来小 | 改写超级块里的 block_count | `L/src/littlefs_patch.cpp#L155` |

**数据分区归谁**（这是本次最关键的发现）[源]：
- 按**标签**查找并复用：已有同名、同子类型、而且够大的分区，就直接拿来用（`partition_table_model.cpp#L778-L792`）。所以**标签叫 `spiffs` 的分区是所有固件共用的**。Bruce 就用 `LittleFS.begin(false, "/littlefs", n)`，默认标签正是 `spiffs`（[Bruce `src/core/sd_functions.cpp#L33-L36`](https://github.com/BruceDevices/firmware/blob/a59213f/src/core/sd_functions.cpp#L33-L36)）。
- 删除一个固件时，Launcher 会**擦掉并删除和它关联的数据分区**（先问要不要备份），**唯独 `spiffs` 例外**（`L/src/app_registry.cpp#L404-L519`，见 `#L427` 和 `#L471-L480`）。
- 删除或整理时，Launcher 会**搬动**其他分区的位置，同时把里面的数据一起搬过去（`launcherPartitionCompact` / `MigrateMovedData`）。所以固件**绝不能缓存 flash 偏移**，每次开机都要按标签去找。

**已装的其他固件能不能用这些数据分区**：能，前提是知道标签。NVS 只有一个，所有固件共用。

## 2. Launcher 安装什么文件

入口是 `updateFromSD`（`L/src/sd_functions.cpp#L801-L958`）。WebUI 上传的文件也先存到 SD，再走这个入口（`L/src/webInterface.cpp#L2229`）。在线安装的逻辑相同（`L/src/onlineLauncher.cpp#L500-L600`）。

1. **文件 0x8000 处不是 `AA 50 01`** → 当作 app-only 镜像。从文件开头按段头量出镜像长度（`#L593-L621`，含追加的 SHA-256），写进新建的 `ota_N`。**不会建任何数据分区**。
2. **0x8000 处是分区表** → 当作合并镜像（"flash image"布局：bootloader 在 0x0）。逐条读：
   - 第一个 app 条目，子类型是 factory、ota_0 或 test（`#L829-L849`），它的**文件偏移**就是 app 的起点；
   - 数据条目按第 1 节的规则处理；
   - nvs、otadata、phy、coredump 条目和 bootloader 都**忽略**；
   - 后面的 ota_1 等条目也忽略（只取第一个 app）。
   - **结论**：Launcher 不接受、也不会写我们的分区表；表只用来告诉它"app 在哪、要哪些数据分区、标签是什么"。
3. **写入后的校验**：头一个字节必须是 0xE9，并且整份镜像要通过 IDF 5.5 的 `esp_image_verify`（芯片 ID、段校验、SHA-256、芯片版本范围）（`L/src/idf/idf_update.cpp#L72-L80`，`#L310-L330`）。PlatformIO 默认产出的 `firmware.bin` 就满足，**不需要任何额外的头或元数据**。
4. **名字**：
   - 分区标签 = 文件名里前 6 个小写字母或数字，不够 6 个补 `0`，重名时末位换成数字（`partition_table_model.cpp#L728-L764`）；
   - 显示名 = 去掉扩展名的文件名，最多 20 个字符，存在 NVS 的 `l_apps` 里（`L/src/app_registry.cpp#L326-L345`）。
   - 例：现在的 `cpa-fw-v0.1.0-cardputer-adv-full.bin` 会变成标签 `cpafwv`、显示名 `cpa-fw-v0.1.0-cardpu`。
5. **升级语义**：只要空间够，再装一次就**新建一个分区**，旧版本还留在列表里（`partition_install_layout.cpp#L89-L101`）。只有空间不够时，才会出现"Use <标签> partition"这类覆盖选项。另一个办法是串口命令 `flash overwrite <name> <size>`，它复用原来的槽位（`L/src/serial_console.cpp#L36-L41`，工具在 `L/tools/serial_flasher.py`）。
6. **在线目录**（可选，以后再说）：LauncherHub 的 manifest 支持 `install.sources.{firmware,bootloader,partitions,data}`，Launcher 下载后在 SD 上合并成一个文件，再走上面的流程（`L/src/onlineLauncher.cpp#L1197-L1215`）。

## 3. 怎样回到 Launcher

**机制** [源]：Launcher 的合并镜像里带一个改过 `bootloader_start.c` 的二级 bootloader。它在 IDF 正常选好分区之后多做一层判断：

| 复位原因（ESP32-S3 代码） | 2.9.1（`launcher` 分支 `#L105-L117`） | main/beta（`launcher_keyboot` 分支 `#L103-L165`） |
|---|---|---|
| 上电 `0x01`（电源开关、RST 键拉 CHIP_PU；**芯片级掉电复位和 super WDT 也是 0x01**） | 进 Launcher | 进 Launcher |
| 深睡唤醒 `0x05` | 进 Launcher | 进 Launcher；如果 NVS 里 `launcher/DDLB=1`，直接回到固件 |
| 软件复位 `0x03`/`0x0C`、看门狗、panic、USB-UART/JTAG 复位 `0x15`/`0x16` | 直接进 otadata 选中的固件 | 同左 |
| 开机时按住 `LauncherOnKey` 指定的 GPIO | — | 强制进 Launcher。不过只有定义了 `LAUNCHER_GPIO_KEYS` 的板子才会显示这个菜单，Cardputer 没有定义；G0 被排除 |

复位代码见 IDF [`soc/esp32s3/include/soc/reset_reasons.h#L38-L61`](https://github.com/espressif/esp-idf/blob/v5.5.4/components/soc/esp32s3/include/soc/reset_reasons.h#L38-L61)。bootloader 从 NVS 读设置用的是 `nvs_bootloader_read`，读的是 `nvs` 分区里的 `launcher` 命名空间，键为 `DDLB`/`LauncherOnKey`/`LauncherKeyLvl`。

**进了 Launcher 之后**（`L/src/main.cpp#L363-L451`）：
- 显示启动画面，main 上是 1 + `bootTimer` 秒，默认 5 秒，可以调成 1–5 秒；2.9.1 固定 5 秒。
- 画面上按 **Enter 或 G0** → 进 Launcher 菜单；
- 按数字 1–9、0 → 直接启动第 N 个已装的固件；
- 按其他键 → 启动已装的固件（装了不止一个时弹出选择列表）；
- 什么都不按 → `ESP.restart()`（`L/boards/m5stack-cardputer/interface.cpp#L393-L404`）进入当前选中的固件。
- 设置里打开"Boot to Launcher"时，会一直停在 Launcher。

**对本项目的含义**：
- 每次冷启动要多等约 5 秒，而且 Launcher 用的是 `ESP.restart()`，所以我们固件读到的复位原因永远是 `software`，读不到 `poweron`。`hello.rst` 本来就只用于展示，重启检测靠的是 boot id，不受影响。
- daemon 的 `debug.reboot` 和 `esp_restart()` 都**留在我们的固件里**，这是好事。
- 开机时按 G0（我们的速记键）会进 Launcher 菜单。用户手册里要写明。
- **深睡等于回到 Launcher**。v0.2/v0.3 的省电只能用浅睡（PRD §5.2 写的也是浅睡），或者接受唤醒后多一次 Launcher 画面。
- 固件**不需要配合做任何事**。只要遵守：不写 otadata，不调 `esp_ota_set_boot_partition`，不改分区表，不调 `nvs_flash_erase`。M5Apps 的做法相同（除软件复位外一律回 factory，见 [M5Apps `bootloader_components/main/bootloader_start.c#L49-L55`](https://github.com/d4rkmen/M5Apps/blob/5f9f4aa/bootloader_components/main/bootloader_start.c#L49-L55)）。Bruce 也没做任何配合：它的"关机"就是 `esp_deep_sleep_start()`，下次唤醒自然回到 Launcher（[`power_commands.cpp#L5-L9`](https://github.com/BruceDevices/firmware/blob/a59213f/src/core/serial_commands/power_commands.cpp#L5-L9)）。
- 可选：系统页加一项"回到 Launcher"，实现为 1 秒定时器深睡；只在检测到 Launcher 时显示（6.3）。[待] 在 2.9.1 和 beta 上各测一次。

## 4. 安装 Launcher，以及怎样回到我们自己的全量刷写

**安装 Launcher**：
- 网页刷写器 <https://bmorcelli.github.io/Launcher/>，设备选"Cardputer and Cardputer ADV"（id `m5stack-cardputer`）。它用 ESP Web Tools 10.1.1 把合并镜像写到 **offset 0**，用的是 `eraseAll:false`；"Erase before flashing"复选框默认不勾（`WebPage` 分支 `src/webflasher.ts#L1030-L1033`，`#L589-L615`）。
- 正式版是 2.9.1（`Launcher-m5stack-cardputer.bin`，1,422,720 B）。beta（`releases/download/beta/…`，1,429,072 B，2026-10-15 构建自 `e8cc4df`）才有 PR #448 的 macOS U 盘修复和 DDLB。
- 命令行（本机工具链，不用按 G0：USB-JTAG 会自动进下载模式，见 03 §2）：
  ```bash
  ~/.local/share/uv/tools/platformio/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
    --chip esp32s3 --port /dev/cu.usbmodem1101 -b 460800 write_flash 0x0 Launcher-m5stack-cardputer.bin
  ```
  刷完后 esptool 做的硬复位是 USB 复位（0x15），不是上电复位。这时表里还没有 ota 分区，IDF 会回落到 test 分区，所以仍然会进 Launcher [源]（[IDF `bootloader_utility.c#L622-L623`](https://github.com/espressif/esp-idf/blob/v5.5.4/components/bootloader_support/src/bootloader_utility.c#L622-L623)）。
- 切换前：确认待同步数为 0（v0.1 的 outbox 在 LittleFS，换表后就找不到了）；可选先整片备份：`read_flash 0 0x800000`，加 `-b 2000000`。

**回到全量刷写**：`pio run -e cardputer-adv -t upload`，或者把我们的 `-full.bin` 写到 0x0。这会写 bootloader（0x0）、分区表（0x8000）、boot_app0（0xE000）和 app0（0x10000），Launcher 的 bootloader、分区表和本体都被覆盖。
- **NVS（0x9000）原样保留**，两边布局一样。里面会留下 Launcher 的 `launcher`、`l_apps`、`l_wifi`（加密的 WiFi 密码）、`uiflow` 和 NimBLE bond 等命名空间。想清干净就先执行 `erase_region 0x9000 0x5000`。这会让我们的 epoch 重新生成，按 05 §5.9 的设计是安全的。
- 数据区里可能是旧的 app 字节。我们的 `LfsStore::begin()` 挂不上会格式化，前提是标签已经是我们自己的（见 6.2）。
- 从全量刷写再回 Launcher：重新网页刷写，再从 SD 安装我们的固件。

**开发循环**：
- `pio run -t upload` 会把 Launcher 冲掉。日常开发继续用全量刷写；只在 Launcher 闸门和 HIL 里测 Launcher 模式。
- 要在 Launcher 下快速换固件，可以用 Launcher 的串口 `flash overwrite`。进 Launcher 只能靠按 RST 键（上电复位），因为 USB 复位会直接进固件。`serial_flasher.py` 的第 1 步在 S3 上做不到这一点 [算]。

## 5. SD 卡

- **引脚和速度** [源]：
  - `SDCARD_CS=12 SCK=40 MISO=39 MOSI=14`（`L/boards/m5stack-cardputer/platformio.ini#L54-L57`），和 hwtest 的 `kSd*` 一致。屏幕在另一条 SPI 上（MOSI35/SCLK36），不共用。
  - 挂载用 `SD.begin(cs, sdcardSPI)`，默认 4 MHz（`L/src/sd_functions.cpp#L33-L81`）。
  - U 盘模式下先试 40 MHz，再试 20 MHz：在卡上取 16 个扇区，读出的 CRC 必须和 4 MHz 下读的一致，不一致就退回 4 MHz（`L/src/massStorage.cpp#L330-L400`）。
  - 实测（作者和 PR 作者在 ADV 上测的）：4 MHz 时 U 盘读约 277 KB/s；PR #448 之后读约 630 KB/s，macOS 挂载用了 13.2 秒。
- **ADV 的特殊处理** [源]：
  - Launcher 在初始化 SD 前把 GPIO 3/4/5/6/13/15 设成输出高电平（`interface.cpp#L78-L95`），注释说"GPIO5 HIGH for SD card compatibility"。这几个是 ADV 扩展口的 GPIO（Bruce 的 ini 把它们列为 "ADV 3…15"）。
  - 推测是为了让扩展帽上同一 SPI 总线的片选保持不选中。裸机不需要。[待] 插上扩展帽时验证。
- **卡格式**：
  - Launcher 建议 SDHC、≤32GB、FAT32、MBR，不要 GPT（`L/README.md#L22-L27`）；2.8.0 起它也支持 exFAT。
  - 但**我们的工具链 A 的 FatFs 设置是 `FF_FS_EXFAT 0`**（`~/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32s3/include/fatfs/src/ffconf.h:255`），所以**只能用 FAT32**，卡大于 32GB 也要格成 FAT32。
- **Launcher 在卡上占用的路径** [源]：
  - `/config.conf` 和 `/config.conf.old`；
  - `/downloads/`（下载目录 `dwn_path`，可改；内含 `downloaded.json`）；
  - `/bkp/`（`backupData.json` 加 `{appNum}-{appName}/` 备份目录）；
  - `/Launcher_error.log`（U 盘模式期间的日志，退出 U 盘模式后才写到卡上）。
  - 文件浏览器默认只显示 `.bin`。
- **我们的布局建议**：
  - 全部放在 `/cpa/` 下：`/cpa/outbox/`、`/cpa/rec/`（v0.3 录音）、`/cpa/archive/`、`/cpa/fw/`（放给 Launcher 安装的 `.bin`）；
  - 不碰 Launcher 的路径。两个固件不会同时运行，不存在并发问题。
- **这张卡的格式化（已授权：卡专用于本项目，可以清空）**：在 Mac 上先用 `diskutil list` 认准卡的盘号，再执行 `diskutil eraseDisk FAT32 CPA MBRFormat /dev/diskN`。盘号一定要核对两遍，填错会清掉别的盘。v0.3 的"插上就传到 Mac"有两条路：
  - **主路径**：我们的固件自己通过 CDC 串口传。[待] 测 HWCDC 的持续吞吐。
  - **后备**：手动进 Launcher → USB（SD->USB）走 U 盘模式。这条路要求 Launcher ≥ `09ffc59`（PR #448，2026-10-15 合入 main），目前只有 beta 有。issue #424 仍是 open 状态。
- 板上 SD 的实测见并行进行的 `docs/gates/sd-card.md`。

## 6. `cardputer-adv-launcher`：要求和建议方案

### 6.1 建议：一个二进制，运行时检测；不另建固件 env

一个固件、一份分区表，同时适用于整片刷写和 Launcher 安装（Bruce 的 Cardputer 版就是这样做的：一个合并镜像两用，见 [`build.py`](https://github.com/BruceDevices/firmware/blob/a59213f/build.py)，[`custom_8Mb.csv`](https://github.com/BruceDevices/firmware/blob/a59213f/custom_8Mb.csv)）。只有当 `cpafs` 的 448KB 实测不够用时，才考虑方案 B（6.6）。

### 6.2 硬性要求

| # | 要求 | 原因 |
|---|---|---|
| R1 | LittleFS 分区改名为专属标签 **`cpafs`**（≤15 字符），子类型仍写 `spiffs`（0x82）：IDF 4.4 的 `gen_esp32part.py` 不认识 `littlefs`（0x83），Launcher 两个都认。整片刷写的分区表就是 `default_8MB.csv` 只改这一个标签，**偏移和大小都不变**（0x670000/0x180000），所以 v0.1 的数据升级后还能直接挂上 | `spiffs` 是多个固件共用的（第 1 节）。现在 `lfs_store.cpp:19` 的 `kLabel = "spiffs"` 加上 `begin(true)` 会挂上 Bruce 的文件系统，甚至把它格式化 |
| R2 | 只挂载、只格式化 `cpafs`。分区不存在，就禁用内部存储（`E_STORAGE`），**绝不回退去用 `spiffs`** | 同上 |
| R3 | 不要缓存任何 flash 偏移，开机时用 `esp_partition_find_first` 按标签查 | Launcher 会搬动分区 |
| R4 | 不写 otadata 和分区表，不调 `nvs_flash_erase`，NVS 只用 `cpa` 命名空间，而且用得越少越好 | 第 3 节。另外，core 2.0.17 遇到 NVS 满了（`NO_FREE_PAGES`）会**擦掉整个 NVS 分区**（`esp32-hal-misc.c:249-262`），连 Launcher 的设置也一起清掉 |
| R5 | 发给 Launcher 的必须是**合并镜像**（`-full.bin`，里面带分区表），这样 Launcher 才会建 `cpafs`。只给 app-only 也能运行，但没有内部存储，只能用 SD | 第 2 节 |
| R6 | 存储以 SD 为主，`cpafs` 作后备；后备容量要按实际分区大小来算 | 第 6.4 节的容量问题 |
| R7 | SD 只用 FAT32；挂载时先试 20–25 MHz，读一段数据比对校验，不一致就退回 4 MHz（照 Launcher 的做法） | 第 5 节 |
| R8 | 不用深睡；重启一律用 `esp_restart()` | 第 3 节 |

### 6.3 运行时检测（只在 `hal/` 里调用 IDF 4.4 API；判断逻辑写成纯函数，放进 core，在 native 环境下用 TDD 开发）

```cpp
// hal：收集事实，不做判断
PartFacts hal_part_facts() {
  const esp_partition_t* run  = esp_ota_get_running_partition();
  const esp_partition_t* test = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                         ESP_PARTITION_SUBTYPE_APP_TEST, nullptr);
  const esp_partition_t* fs   = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                         ESP_PARTITION_SUBTYPE_ANY, "cpafs");
  return PartFacts{test && run && run->address != test->address,  // 在 Launcher 下运行
                   fs ? fs->size : 0u,                              // 0 表示没有内部存储
                   run ? run->size : 0u};
}
// core（纯函数，不可变）：StoragePlan plan_storage(const PartFacts&, const SdFacts&);
//   → primary(Sd|Lfs|None)、fallback、lfs_item_cap、show_back_to_launcher
```

依据：
- Launcher 的表里必须有 FACTORY 或 TEST 分区（`partition_table_model.cpp#L342-L345`）；在 ESP32-S3 上它用的是 TEST（`L/src/partitioner.cpp#L1042-L1046`）。我们自己的表里没有 test 分区，所以"有 test 分区，并且当前运行的不是它"就足以判断"在 Launcher 下"。
- IDF 4.4 里这些符号都有（`esp_partition.h:82,96`，`esp_ota_ops.h:214`）。
- 系统页显示：启动方式（launcher 或 direct）、本 app 分区的标签和大小、`cpafs` 的大小、SD 状态。**不改协议**。

### 6.4 改了以后哪些东西会受影响（逐项）

1. **`LfsStore` 的标签**：见 R1、R2。必须改。
2. **`cpafs` 变小，分片目录把空间吃掉** [算]：
   - Launcher 下空分区只有 0x70000，也就是 112 个 4KB 块。
   - `LfsStore` 的 32 个分片目录，每个是一对元数据块，合计 **64 块，256KB**；再加上根目录、`/outbox`、`/outbox/bad`、`/cfg` 共 8 块。
   - 剩下约 40 块。再扣掉"剩余至少 64KB"的门槛（16 块），**大条目大约只能存 20–40 条**，比 05 §5.9 的 200 条少得多。
   - 对策：SD 作主存储（R6）；后备的上限按 `cpafs` 实际大小计算，从 `plan_storage` 输出。[待] 实测。
3. **app-only 镜像没有 `cpafs`**：见 R5。
4. **NVS 和开机计数器**：照常工作。
   - Launcher 的表里 NVS 固定在 0x9000/0x5000，和我们一样。NVS 页格式版本两边都是 0xfe（IDF [v4.4.7 `nvs_page.hpp#L50`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/nvs_flash/src/nvs_page.hpp#L50)，[v5.5.4 `nvs_constants.h#L33`](https://github.com/espressif/esp-idf/blob/v5.5.4/components/nvs_flash/private_include/nvs_constants.h#L33)），所以不会触发 `NEW_VERSION_FOUND` 擦除。
   - 计数器只在我们的固件开机时加 1。装了新旧两个版本时，它们共用 `cpa` 命名空间，ID 仍然唯一。
   - Launcher 只会擦它自己的命名空间（`L/src/settings.cpp#L105-L106`），但它每次开机都会补齐约 19 个 `uiflow` 键（`#L383-L413`），会占用一些 NVS 空间。
   - 用网页刷写器重装 Launcher 并勾了"Erase before flashing"，epoch 会变。按设计这是安全的。
5. **复位原因**：冷启动永远是 `software`（第 3 节）。只影响展示。
6. **开发时上传**：`-t upload` 会冲掉 Launcher（第 4 节）。
7. **升级**：
   - 装新版本会新建分区，旧版本还在。
   - 删除旧版本时，Launcher 会**连 `cpafs` 一起擦掉**：`cpafs` 被关联到每个装过它的标签上，删任何一个都会把它删掉。
   - 用户流程：先把待同步清零 → 在 Launcher 里删掉旧版本（选"Remove Without Backup"）→ 安装新版本。另一个办法是用 `flash overwrite`。
   - 恢复备份可能把已经确认过的条目带回来。daemon 按 `(dev, id)` 去重，会把它们当作 duplicate 删掉，不会出问题。
8. **同一个 USB 口上的 Launcher 控制台**：
   - Launcher 每 500ms 打一行 `Press the button to enter the Launcher!`，收到任何一行都会当命令解析，不回显。
   - 我们的 JSON 行以 `{` 开头，只会得到 `ERR unknown command…`（`L/src/serial_console.cpp#L940-L1006`）。daemon 把不以 `{` 开头的行当噪声（`daemon/src/cpad/protocol/codec.py:109`）。
   - 风险在于：daemon **绝不能**发出以 `reboot`、`partition`、`flash`、`wifi` 等单词开头的行。现在的协议满足这一点，以后改协议时要守住。
9. **工具链**：Launcher 的 bootloader 是 IDF 5.5.4，我们的 app 是 IDF 4.4.7。Bruce 等基于 Arduino 2.x 的固件历来都能这样启动，但我们的镜像还没在这种组合下验证过。[待]
10. **coredump**：core 2.0.17 打开了 `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`。panic 时会写进 Launcher 的 coredump 分区（0x160000），无害。
11. **体积预算** [算]：
    - 我们现在的 app 是 841,808 B，占一个 0xD0000 的分区；加上 `cpafs` 0x70000，共 1,310,720 B。
    - 和 Bruce 1.16.1 一起装：Bruce 的 app 4,101,424 B，占 0x3F0000，再加它的 `spiffs` 0x70000。这时留给我们 app 的最多是 **1,835,008 B（约 1.75 MiB）**。
    - 建议：CI 里 2.5MB 的硬上限保持不变；另加一条"超过 1.75MB 就警告"的软预算，并写进 05 §5.13。

### 6.5 工作项（按 TDD，先写失败的测试）

| 项 | 内容 | 测试 |
|---|---|---|
| L1 | core `storage_plan`：纯函数 `plan_storage(PartFacts, SdFacts)`，算出主存储、后备存储和 LittleFS 条目上限 | native：在 Launcher 下有没有 `cpafs`、有没有 SD、不同分区大小的组合，以及最小的 0x70000 |
| L2 | hal：实现 `hal_part_facts()`；`LfsStore` 改用 `cpafs`，分区不存在就返回 false，不格式化 | 设备测试：在全量刷写和 Launcher 两种状态下，都只挂载 `cpafs` |
| L3 | 构建：新增 `firmware/app/partitions/cpa_8MB.csv`（就是 `default_8MB.csv` 把 `spiffs` 改成 `cpafs`）；`board_build.partitions` 指向它；release 的 `-full.bin` 命名改成方便 Launcher 显示的名字，比如 `CPAssistant-<ver>.bin`，并注明"整片刷写写到 0x0，也可以放进 SD 用 Launcher 安装" | `check_budget.py` 加软预算；CI 用 `gen_esp32part.py` 校验 CSV |
| L4 | SdStore：在 SD 的 `/cpa/` 下实现 FileStore。FAT 的改名不是原子的，靠 05 §5.9 的启动扫描规则加每条记录的校验和来兜底 | native：用 FakeStore 注入"非原子改名"；设备：断电测试 |
| L5 | 系统页：显示启动方式、分区标签和 SD 状态；可选"回到 Launcher"（需先过 G-L5） | ui 测试 |
| L6 | 文档：README 写安装和升级流程（先清零待同步 → 删旧版 → 装新版），注明开机按 G0 会进 Launcher | — |

### 6.6 方案 B（只在 448KB 实测不够时考虑）

在合并镜像里带一份用 mklittlefs 生成的 `cpafs` 镜像（`pio run -t buildfs`）。条件是：标签不叫 `spiffs`，内容非空，声明大小大于 0x70000。满足时 Launcher 会按声明大小建分区并拷入镜像，缩小时还会修正 block_count（第 1 节）。

代价：要么把 `cpafs` 挪到紧接 app 之后（文件约 4MB，并且整片刷写的布局会变），要么接受一个约 8MB 的文件。另外，安装时要多拷贝 1.5MB。

## 7. 其他固件怎样打包给 Launcher

- **Bruce**（[`a59213f`](https://github.com/BruceDevices/firmware/tree/a59213f)）：
  - 发布一个合并镜像 `Bruce-m5stack-cardputer.bin`（4,166,960 B），由 `build.py` 把 bootloader、分区表和 app 合在一起，从 0x0 开始；整片刷写和 Launcher 安装都用它。
  - 表：nvs 0x6000、factory 0x4E0000、spiffs 0x300000。装进 Launcher 后：app 在 ota_N，`spiffs` 空着、按 0x70000 建，而且**是共享的**；NVS 用的是 Launcher 那个 0x5000 的。
  - 只有 4MB 的板子另有 `LAUNCHER_*` env（精简功能，减小体积）；8MB 的板子不需要。
- **M5Apps**（d4rkmen）：本身就是一个"Launcher 类"的固件，用自己的 factory 分区，加一个自定义 bootloader（除软件复位以外都回 factory）。它和 bmorcelli Launcher 是二选一的关系，不会叠在一起用。
- **UIFlow2**（M5Burner 上的合并镜像）：
  - 依赖 FAT 分区 `sys`/`vfs`；Launcher 会把 `sys` 整块拷过去，`vfs` 用默认大小（`partition_install_layout.cpp#L36-L46`）。
  - 它镜像里自带的 NVS 默认值装不进去，所以 Launcher 每次开机替它补齐 `uiflow` 命名空间（`settings.cpp#L383-L413`）。
  - 这从侧面说明：**固件自带的 NVS 镜像在 Launcher 下不会被写入，所有 NVS 键都必须由固件在运行时自己建**。我们本来就是这样做的。

## 8. 待真机验证的闸门（需要设备，加进 07-hil-checklist）

| 闸门 | 做法 | 通过标准 |
|---|---|---|
| G-L1 | 网页刷写 Launcher（beta），从 SD 安装改名后的 `-full.bin` | 能启动；`hello` 正常；系统页显示 launcher、`cpafs` = 0x70000 |
| G-L2 | 按 RST 键、拨电源开关、执行 `debug.reboot`、跑 `pio` 的 USB 复位 | 前两种进 Launcher 启动画面；后两种直接回到我们的固件 |
| G-L3 | 装 Bruce 1.16.1，在两个固件之间来回切换 10 次 | 我们的 outbox 和 epoch、boot 计数都在；Bruce 的 `spiffs` 没被动过 |
| G-L4 | `cpafs` 容量：断开 daemon，一直存到被拒 | 记下条数，回填 6.4 第 2 项 |
| G-L5 | 系统页"回到 Launcher"（定时器深睡），在 2.9.1 和 beta 上各测一次 | 进 Launcher；DDLB=1 时回到我们的固件 |
| G-L6 | 照 6.4 第 7 项的流程升级 | 不丢已确认的数据；daemon 收到的重复条目都判为 duplicate |
| G-L7 | SD 20 和 25 MHz 挂载并比对校验（裸机，以及插扩展帽、GPIO5 拉高两种情况） | 记下实际用的时钟和吞吐 |
| G-L8 | Launcher 的 U 盘模式（beta）在 macOS 26 上挂载这张卡 | 能挂上，用时 ≤30 秒 |

## 9. 证据索引

- Launcher 源码（`e8cc4df`）：<https://github.com/bmorcelli/Launcher/tree/e8cc4dfb35b63aa3dc56eceef58da19e348dda5c>。2.9.1 的 `platformio.ini#L114-L115`（platform 55.03.39 + `launcher_esp32-arduino-libs-20260807`）。
- keyboot bootloader：<https://github.com/bmorcelli/myLibBuilder/blob/cf56f6a55cd512a908fa455d80b1ee4b6c122e9d/patches/esp-idf/components/bootloader/subproject/main/bootloader_start.c>。2.9.1 用的版本：<https://github.com/bmorcelli/myLibBuilder/blob/386ffd319bdaab3e48cb24ae48c209af86d6a5db/patches/esp-idf/components/bootloader/subproject/main/bootloader_start.c>。
- Wiki（`4fb10da`）：[Explaining-the-project](https://github.com/bmorcelli/Launcher/wiki/Explaining-the-project)（固件格式、动态分区、空数据分区 0x70000）；[Serial-interface](https://github.com/bmorcelli/Launcher/wiki/Serial-interface)。
- 发布：<https://github.com/bmorcelli/Launcher/releases/tag/2.9.1>，以及 beta：`releases/download/beta/Launcher-m5stack-cardputer.bin`。网页刷写器：`WebPage` 分支（`2b119bb`）的 `src/flasher.json` 和 `src/webflasher.ts`。
- issue [#424](https://github.com/bmorcelli/Launcher/issues/424)：ADV 在 macOS 上挂不上 U 盘，仍是 open；PR [#448](https://github.com/bmorcelli/Launcher/pull/448)：2026-10-15 合入 `massStorage.cpp`，改用 CMD18 多块读，日志在 U 盘模式期间不写卡。
- 本仓库：`firmware/app/lib/hal/lfs_store.cpp:19,53`；`firmware/app/platformio.ini`（`default_8MB.csv`）；`.github/workflows/release.yml:46-51`；`daemon/src/cpad/protocol/codec.py:109`；`firmware/hwtest/src/main.cpp` 里的 `kSd*` 常量（HEAD 第 198 行；工作区里另一个 agent 正在改这个文件，目前在第 205 行）。

## 10. Tooling：`tools/launcher/`（M3-L，2026-10-21）

把 2026-10-20 G-L1 手工做的步骤（esptool 刷 Launcher → 按 RST、Enter 进菜单 → 串口 `flash firmware`）做成一个命令行工具。**本工具还没有在真机上跑过**，下面的行为都来自单元测试（假串口）和第 4 节、G-L1 的记录；第一次真机运行由 lead 来做。

**命令**（在仓库根目录执行，`uv run` 按脚本头里的 PEP 723 元数据自动装 `pyserial==3.5`）：

| 命令 | 做什么 |
|---|---|
| `uv run tools/launcher/cpa_launcher.py fetch` | 从官方 release（`releases/download/2.9.1/Launcher-m5stack-cardputer.bin`）下载到缓存 `~/.cache/cardputer-assistant/launcher/2.9.1/`，核对大小 1,422,720 B 和 SHA-256 `2adff1ad…a024`。先下到临时文件，两项都对才改名；缓存里的文件每次使用前都重新核对，不对就重新下载 |
| `… flash-launcher` | 再核对一次，然后 `pio pkg exec --package tool-esptoolpy -- esptool.py --chip esp32s3 --port <port> --baud 460800 write_flash 0x0 <image>`（不用按 G0）。刷完等 Launcher 自己起来（新表里没有 ota 分区，USB 复位后直接进 Launcher），用 `version`、`partitions` 确认。**会覆盖我们的整片刷写**（第 4 节） |
| `… wait` | 每 0.6 s 发一次 `version`，直到用户按 RST、再在启动画面按 Enter，菜单打开为止（默认等 1500 s）。判定"菜单已打开"：`version` 有回复，并且距上一次启动画面横幅（每 500 ms 一行）和打开端口都已超过 2 s。单独用时建议加 `--stay-paused`，否则结束后 cpad 会去连 Launcher 的口（握手失败后会自己放开） |
| `… cmd version` / `partitions` / `whoami` / `help` | 只发这四个只读命令。`wifi`、`settings get` 等会打印密码的命令、以及会改设备的命令一律拒绝 |
| `… install factory` | 装 `backups/factory-app-cardputer-adv-v0.9.bin`（先核对 backups/README.md 里的 SHA-256），Launcher 里叫 `factory-v0.9` |
| `… install <path>/cpa-v0.2.0-app.bin` | 装我们的 app-only 镜像，名字默认去掉 `-app.bin`（`cpa-v0.2.0`）；`--name` 可改，例如 rc 包 `--name cpa-v0.2.0`。合并镜像（0x8000 处有分区表）会被拒绝，提示改用 `-app.bin` |

全局选项：`--port`（默认第一个 303A:1001 口）、`--cache-dir`、`--stay-paused`。退出码：0 完成；1 失败（设备回 ERR、超时、传到一半端口没了、esptool 失败）；2 准备阶段的问题（镜像不对、下载失败、找不到 pio 或端口、端口被别的进程占着）；3 字节都发完了但没能确认；4 等不到 Launcher 控制台。

**安全措施**：
- 只用安全打开（03 §2 坑 3：`dsrdtr=True` → open → `rts=False` → `dtr=False`），绝不拨控制线复位芯片；
- 打开前用 `lsof -t <port>` 查占用，有别的进程就等它放开（最多 10 s），不抢；
- 动端口前先 `cpa pause N`（N = 等待时间 + 按 4 KB/s 估算的传输时间 + 300 s，最少 600 s；已有更长的暂停就不缩短，和 `pre_upload.py` 一样），结束时 `cpa pause 1`，Ctrl-C 也会恢复；`--stay-paused` 时不恢复。没装 `cpa` 就跳过，只靠 lsof 检查；
- 安装前先读一次 `partitions`，读不全就不发任何字节（说明不在菜单里）；
- 输出只有进度和设备事实（版本、分区标签、大小），没有笔记内容或密码。

**安装协议**（`flash firmware <name> <size>`，2.9.1 的二进制里能找到同样的字符串：`READY %u`、`ACK %u/%u`、`OK flashed, rebooting`、`Launcher 2.9.1`、启动画面横幅）：
- 等 `READY <size>`（30 s，大小必须一致）→ 每次只发一个 2 KB 块，设备确认的字节数追上已发的字节数才发下一块（设备把一块分几次读、回几行 ACK 也没问题）→ 每块 15 s 内必须有进展 → 全部确认后等 `OK`（60 s）。每过 10% 打印一行进度。
- 横幅和日志行一律忽略，也不会推迟超时。ACK 倒退、超过已发字节、总数不对，都按协议错误处理。
- **结果分五种**：`ok`（Launcher 打出 OK：已校验、已写表、已设为启动项）；`unconfirmed`（每个字节都确认了，但重启前 OK 没传出来，或端口直接断了）；`error`（设备回 ERR，原文照印）；`timeout`、`lost`（确认完以前就没回应或端口没了）。失败时 Launcher 不会改启动项。
- `unconfirmed` 时（或显式 `--verify`）再确认一次：
  - 名字是 `cpa-vX.Y.Z…` 并且有 `cpa` 时走 **daemon**：先 `cpa pause 1`，再轮询 `cpa status --json`，直到 `link ready`、`launcher=yes`、`fw=cpa-fw/X.Y.Z`（最多 90 s）。同版本重装用这个方法分辨不出；
  - 其他情况（例如出厂 app 不开 USB）走 **partitions**：请用户再按一次 RST、Enter，读新表，和安装前的表比：必须正好多出一个 ota app 分区，大小不小于镜像，而且标着 `<-- BOOT`。标签规则和 Launcher 一样：名字里前 6 个小写字母或数字，重名时取前 5 个加一位数字（`factory-v0.9` 已有 `factor` 时新的叫 `facto1`）。

**测试**（不需要设备）：

```bash
uv run --no-project --python 3.12 --with pytest --with pytest-cov --with pyserial==3.5 \
  pytest tools/launcher -q --cov=tools/launcher/cpalauncher --cov=cpa_launcher --cov-fail-under=80
(cd tools/launcher && uvx ruff==0.17.1 check . && uvx ruff==0.17.1 format --check .)
uvx lizard==1.24.1 -l python -L 50 -C 10 -w tools/launcher
```

`tests/fakes.py` 的 `FakeLauncher` 按 `serial_console.cpp` 模拟控制台（启动画面横幅、READY/ACK/OK、分区表打印格式、重启后端口断开），用虚拟时钟，所以超时测试不用真的等。

**已知限制**：
- 串口安装只写 app，**不建 `cpafs`**（G-L1 已记录）；要 `cpafs` 就走 SD 安装合并镜像；
- 没在真机跑过；release URL 是 GitHub 的标准地址，本工具还没有真正下载过它（测试用的是本地预置的缓存和假网络）；
- 端口忙时的等待依赖 `lsof`，系统里没有 `lsof` 时这一步当作"没人占用"。
