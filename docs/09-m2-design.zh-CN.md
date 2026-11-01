[English](09-m2-design.md) · **中文**

# 09 · M2（v0.2.0）设计：Launcher 安装与切换 + microSD 存储

> 状态：定稿（已处理对抗式评审，逐条记录见 §14）· 日期：2026-10-20 · 角色：架构
> 依据：[gates/launcher.zh-CN.md](gates/launcher.zh-CN.md)（读 Launcher 源码得出，没接设备）、[gates/sd-card.zh-CN.md](gates/sd-card.zh-CN.md)（真机实测）、05 §5.9 / §5.13、04 §5.2 / §7.7、ADR-0001，以及 `firmware/app` v0.1 的代码（`FileStore`、`LfsStore`、`outbox_io`、`outbox_scan`、`Runtime`）和补丁版 SD 驱动（`firmware/hwtest/lib-core2/SD/src/sd_diskio.cpp`）。
> 标记：**[测]** 真机实测 · **[源]** 源码或文档 · **[算]** 推算 · **[待]** 待验证（每条都对应一个闸门或测试）。
> 范围：只写 v0.2.0。v0.1 的规格（05）继续有效，本文取代的部分列在 §13。
> **开发用 SD 卡**：这张卡专门给本项目用，卡上原有数据都无所谓，可以清空，也可以按需要重新格式化。所以设备测试和 HIL 可以清掉卡上的 `/cpa/` 和测试目录；人工检查单里"用 `diskutil` 把这张卡重新格式化"也在授权范围内（执行前用 `diskutil list` 把盘号核对两遍，只动这一张卡）。这张卡现在已经是设备格式化的 FAT32（sd-card.md §3），M2 开工前不需要再动它。

## 0. 一页摘要

1. **一个固件二进制**，既能整片刷写，也能交给 bmorcelli Launcher 安装，运行时自己判断是否在 Launcher 下。**不另建 env**。
2. **分区表只改一个标签**：`spiffs` 改成 `cpafs`，偏移和大小都不变。v0.1 用户整片刷写升级后，原来的数据照样能挂上。在 Launcher 下，`spiffs` 是多个固件共用的，我们绝不碰它。
3. **两个 `FileStore` 后端**：`SdStore`（FAT32，只用卡上的 `/cpa/`）和 `LfsStore`（内部 `cpafs`）。`SdStore` 的逻辑是纯代码（`lib/storage`），通过一个很薄的 `FatPort` 接口调用 VFS，所以能在 native 下按 TDD 测；hal 里只剩挂载和 POSIX 调用。开机时由纯函数 `plan_storage()` 选主存储：SD 可用就用 SD，否则用 `cpafs`；两个都没有，就禁止保存并说明原因。本次运行期间不再切换。
4. **SD 为主时，开机把 `cpafs` 里待发的条目迁到 SD**：每条都先在 SD 上提交成功，再删源文件。中途断电安全，下次开机重跑就收敛。bad/ 不迁。
5. **SD 上完全不用 rename**（FAT 的 rename 不是原子的，也不能覆盖已有目标）。条目直接写成最终文件名，`fsync` 加 `close` 成功才显示"已保存"；挪到 bad/ 用"复制再删除"。`FileStore` 新增特性位 `atomic_rename()` 区分两种提交协议；扫描规则的两条新增项只在 `atomic_rename()==false` 时生效，LittleFS 上的行为和 v0.1 一字不差。
6. **SD 上用 8.3 短文件名，按 epoch 平铺**：`/cpa/outbox/<E7>/<B5><S前3>.J<S末1>`。一张卡一个 epoch 只建一次目录，不按开机建目录。实测 8.3 平铺 200 条 p95 29ms [测]。
7. **SD 驱动**：打了补丁的 core `SD` 库挪到 `firmware/third_party/SD`，app 和 hwtest 共用一份。固定 20MHz，**不做降速重试**；挂载失败时读 `sdcard_type()` 区分"没卡"和"卡不能用"；挂载后做一次探针写；`max_files=2`；不调用 `f_getfree`。
8. **运行中 SD 出磁盘级错误（拔卡、卡坏）就锁死**：之后所有存储操作立即失败、不再碰 SPI 总线，直到重启。这样拔卡不会让主循环反复卡几秒。
9. **协议只加两个可选字段**：设备 `hello` 里的 `store`（`sd` / `int` / `none`）和 `launcher`（bool）。`v` 不变。
10. **daemon 只改显示**：`cpa status` 显示上面两个字段；加一条守护测试（主机发出的行只以 `{` 开头或为空行）。"已绑定设备短退避"推迟，等闸门 G-L9 实测需要时再做。
11. **没插 SD 卡时，整片刷写的设备和 v0.1 行为完全一样**：同一个分区、同样的分片、同样的提交协议和扫描规则。v0.1 的 outbox 测试一行不改照样通过，v0.1 的全部 HIL 场景在"禁用 SD"的构建上通过。
12. **v0.3 录音的 SD 布局**在本文预留（§12），这次不实现。
13. **四个工作包**：A（固件存储层）和 C（协议 + daemon）并行 → B（固件集成、界面、Launcher 发布）→ D（HIL 与人工检查单）。

## 1. 目标与非目标

### 1.1 目标（v0.2.0 的完成标准）

| # | 目标 | 怎么验收 |
|---|---|---|
| G1 | 同一个发布镜像既能整片刷写（写到 0x0），也能被 Launcher 从 SD 安装并启动 | G-L1；CI 解码分区表 |
| G2 | 在 Launcher 下只挂载、只格式化自己的 `cpafs`；不读写 `spiffs`，不写 otadata 和分区表，不擦 NVS | 代码评审；G-L3（和 Bruce 来回切换 10 次） |
| G3 | 插着 FAT32 卡时，outbox 和项目缓存都放在 SD 上；保存 p95 ≤100ms（积压 200 条也一样），单次 ≤150ms；每张卡每个 epoch 的第一条（要建 epoch 目录）≤250ms | device `test_sd_store`；HIL `sd_store` / `sd_bulk` |
| G4 | FAT 上断电时，显示过"已保存"的条目 0 丢失（可能重复发送，由 daemon 去重） | native：在每一步注入断电，证明**提交协议**本身正确；人工 10 次断电只是低强度的物理证据（见 R16） |
| G5 | 从"没卡"到"插卡"：`cpafs` 里待发的条目在开机时迁到 SD，0 丢失 | native 迁移断电注入；device；HIL `migrate_*` |
| G6 | 没插 SD 卡时，整片刷写的设备和 v0.1 行为一致 | v0.1 的 outbox 套件一行不改照样通过（用 `git diff` 守住）；v0.1 的 HIL 场景在 nosd 构建上通过 |
| G7 | 用户看得到当前用的是哪个存储、是怎么启动的，以及存储为什么不可用 | ui `test_mod_system`；`cpa status` |
| G8 | 运行中拔卡后，按键和屏幕仍然响应：最多只在发现错误的那一次存储操作上卡一下（0.6–2s [算]），之后不再碰总线 | native `test_sd_store`（锁死后对 `FatPort` 的调用数为 0）；人工检查 |

### 1.2 非目标（v0.2 不做）

- 录音、转写，以及把文件传到 Mac：放到 v0.3，布局见 §12。
- 在设备上格式化 SD。默认在 Mac 上用 `diskutil` 格式化，待你确认（开放问题 3）。
- 运行中拔插 SD 后自动切换存储，或者拔卡后不重启就恢复。v0.2 只在开机时选一次。
- SD 写满的专门提示（"存储已满"）。卡被写满时，看到的是通用的"保存失败（E_STORAGE）"（§3.8）。
- 挂载失败后降速到 4MHz 重试。
- 把 `cpafs` 的 bad/ 迁到 SD。
- SD 上的加密封装和 PIN 锁。PRD 原来写在 v0.2 的安全项推后（开放问题 9）。
- 把已同步的条目归档到 SD（PRD 原 v0.2 的"SD 卡历史"），推后。
- 系统页加"回到 Launcher"。它需要深睡，违反 R8；拨电源开关或按 RST 本来就会回到 Launcher。所以 launcher.md 的 G-L5 不测。
- daemon 对已绑定设备的短退避：只有 G-L9 不通过才做（§7）。
- 深睡、省电、U 盘模式（MSC）、OTA、LauncherHub 在线目录。
- 再建一个 `cardputer-adv-launcher` env。方案 B（在合并镜像里带一份 1.5MB 的 LittleFS 镜像）只有 G-L4 实测不达标时才重新讨论。

## 2. 输入事实

| 事实 | 来源 | 对设计的影响 |
|---|---|---|
| Launcher 只写 app 和数据分区；数据分区按**标签**新建或复用；`spiffs` 由多个固件共用，删除固件时保留；其他标签的分区在删除对应 app 时被擦掉 | [源] launcher.md §1–2 | 标签改成 `cpafs`，不碰 `spiffs`；升级流程见 §4.4 |
| 8MB 机型上，空的数据分区固定建成 0x70000（448KB） | [源] | Launcher 下 `cpafs` 很小，所以 SD 为主；内部存储的分片减到 4 个 |
| 上电、RST、深睡唤醒都进 Launcher；`esp_restart`、看门狗、USB 复位留在本固件；冷启动读到的复位原因永远是 `software` | [源] | 不用深睡；`rst` 只用于展示 |
| 合并镜像只被当作元数据读；显示名取自文件名，最多 20 个字符 | [源] | 发布文件名要改短（§4.4） |
| Launcher 的串口控制台会把收到的每一行当成命令；以 `{` 开头的行只会得到 `ERR` | [源] | 主机发出的行只能以 `{` 开头，或者是空行 |
| core 自带的 SD 驱动初始化不了这张 512GB 卡；ACMD41 参数改成只带 HCS 后，3–6ms 就能初始化 | [测] sd-card.md §2 | 用打补丁的驱动；Launcher 自己可能也读不了这张卡 [待] |
| 补丁驱动里 `sdcard_init` 只分配结构、登记 diskio，**不碰卡**；卡是在 `sdcard_mount` 的 `f_mount(…, 1)` 里才初始化的，"没卡"和"没有 FAT"都返回 `false`。但初始化成功时 `card->type` 会被设成 SD/SDHC/MMC，可以用 `sdcard_type()` 读出来 | [源] `sd_diskio.cpp` | 挂载失败后用 `sdcard_type()` 区分 Absent 和 Unusable（§3.9） |
| 没卡时 `GO_IDLE_STATE` 重试 3 次、每次 `delay(100)`；`sdSelectCard` 最多等 500ms；`sdcard_uninit` 还会再发一次 CMD0 | [源] | 没卡时不卸载、不重试；无卡开机代价取决于 MISO 空闲电平 [待] |
| 拔卡后每次 FatFs 调用都会先走 `disk_status`（CMD13，同样重试）和 `disk_initialize`（CMD0），一次 0.6–2s | [源] + [算] | SD 出磁盘级错误后锁死（§3.8） |
| 工具链 A 的 FatFs 不支持 exFAT | [源] | 只支持 FAT32 / FAT16；exFAT 卡要先在 Mac 上格式化 |
| 五档时钟（4 / 20 / 25→20 / 26.7 / 40MHz）挂载、写读校验都是 5/5；驱动每块都校验 CRC16 | [测] | 固定 20MHz，不做降速重试 |
| 20MHz 下 32KB 块写入约 1.6MB/s；偶尔有 30–65ms 的写停顿 | [测] | SD 写入允许超过 50ms 单步规则（§5） |
| 长文件名平铺 200 条 p95 93ms，而且线性变慢；8.3 平铺 200 条 p95 29.1ms、最长 101.7ms；新建目录要把 32KB 的簇清零，每个约 70–90ms（由分片布局前 10 条平均 89.5ms 推出） | [测] + [算] | 用 8.3 名字，按 epoch 平铺，不按开机建目录 |
| rename 遇到目标已存在时返回 EEXIST；FatFs 的 rename 先登记新目录项、再删旧目录项，中途断电会留下两个名字指向同一条簇链 | [测] + [源] | SD 上不用 rename，也绝不删除 `.tmp` |
| FSINFO 有效时 `f_getfree` 耗时 0ms；无效时估计要扫约 35s | [测] / [算] | v0.2 不调用 `f_getfree` |
| 所有性能数据都来自 FatFs 自己格式化的卡（1 个 FAT，数据区 4MiB 对齐）；`diskutil` 格式化的卡是 2 个 FAT，对齐情况未知 | [测] + [源] | 在 `diskutil` 格式化的卡上重跑设备测试（R17） |
| `esptool merge_bin` 用 0xFF 填满段之间的空隙；把合并镜像写到 0x0 会把 0x9000 的 NVS 清空 | [源] release.yml | 写 0x0 后 epoch 会重新生成，文档不能承诺 NVS 保留（§4.4） |
| 以 32KB/s 写 60s 录音，3 次都 0 丢帧；录音期间约 27% 的回复被扣住 | [测] | 作为 v0.3 的依据；v0.2 不录音 |

## 3. 存储架构

### 3.1 开机流程总览

```
setup()
  g_usb.begin(); M5Cardputer.begin(); g_display.begin()          （v0.1 原样）
  part = hal_part_facts()            -> PartFacts{launcher, cpafs_bytes, legacy_spiffs}
  sd   = hal_sd_mount(g_sd)          -> SdFacts{state, size_mib}
                                        state==Ok 表示已挂载，并且 g_sd.prepare() 成功（建 /cpa/outbox，探针写）
  lfs  = g_lfs.begin()               签名不变：自己按标签找 cpafs，没有就返回 false；按分区大小定分片数
  plan = plan_storage(part, sd, lfs) -> StoragePlan{primary, migrate, note}           纯函数
  boot = BootInfo{nvs_boot_ticket(), dev, rst}                    （v0.1 原样）
  fs   = primary==Sd ? g_sd : primary==Internal ? g_lfs : g_null
  rt   = setup() 里函数内 static 的 Runtime(…, fs, …)，指针交给 loop()
  rt->begin(boot, millis(), StorageBoot{plan, sd, part, plan.migrate ? &g_lfs : nullptr})
        1. 有迁移源：outbox_boot_scan(源) → outbox_migrate(源 → 主)，每条回调一次（kick USB、刷新进度）
        2. outbox_boot_scan(主)
        3. 之后和 v0.1 相同
  SD 为主并且 lfs 挂上了：迁移完执行 g_lfs.end()，省出 LittleFS 占的堆
```

`Runtime` 的构造函数不变，`begin()` 的第三个参数带默认值（没有迁移、主存储为内部），所以 v0.1 的 ui 测试不用改。

### 3.2 `FileStore` 的变化

```cpp
class FileStore {             // lib/outbox/file_store.h
  // ……v0.1 的方法不变……
  // true：rename 是原子的，并且会覆盖目标（LittleFS）。false：FAT，提交协议见 §3.6
  virtual bool atomic_rename() const { return true; }
};
```

- 默认值是 `true`，所以 v0.1 已有的实现和测试都不用改。
- `NullStore`（`lib/storage/null_store.h`，纯代码）：所有操作都失败，`free_bytes()` 返回 0。没有任何可用存储时用它；`Runtime` 在这种情况下根本不去写（§8 的 `NoStorage`）。

### 3.3 两个后端

| | `LfsStore`（内部） | `SdStore`（SD） |
|---|---|---|
| 代码位置 | `lib/hal/lfs_store`（设备专用，同 v0.1） | `lib/storage/sd_store`（纯代码，native 可测）+ `lib/hal/sd_card`（挂载和 `VfsFatPort`） |
| 介质 | 内部 Flash 上的 `cpafs`（LittleFS） | microSD，FAT32 或 FAT16，MBR；走 FSPI：SCK40 / MISO39 / MOSI14 / CS12 |
| 挂载点 | `/littlefs` | `/sd`，只用其中的 `/sd/cpa/` |
| `atomic_rename()` | true | false；`rename()` 直接返回 false，不碰卡 |
| 提交协议 | v0.1 原样：写 `.tmp` → close → rename | 直接写最终文件名 → fsync → close（§3.6） |
| 物理布局 | v0.1 的分片 `/outbox/_<MonoId 末位>/`；分区小于 1MiB 时只用 4 个分片 `_0`–`_3` | 8.3 名字，按 epoch 平铺（§3.5） |
| `free_bytes()` | v0.1 原样：开机时走一遍元数据 | 不扫 FAT：正常时返回 0xFFFFFFFF，锁死后返回 0 |
| 出错 | 和 v0.1 一样逐次失败 | 磁盘级错误后锁死（§3.8） |
| 什么时候格式化 | 只在 `cpafs` 挂不上时格式化它自己（例如 Launcher 新建的空分区） | 从不格式化 |
| 挂载时机 | `cpafs` 存在就挂载；SD 为主时迁移完就卸载 | 每次开机试一次，不重试 |

**`LfsStore::begin()` 的签名不变**（无参数，返回 bool）。内部用 `esp_partition_find_first(DATA, ANY, "cpafs")` 找分区：没有就返回 false，不挂载、不格式化；有就按 `lfs_shards(size)` 定分片数，再 `LittleFS.begin(true, "/littlefs", 4, "cpafs")`。这样 `src/main.cpp` 和 `device/test_runtime_budget` 里现有的 `g_fs.begin()` 在 M2-A 合入后照样能编译（§14 B2）。

**分片数** `lfs_shards(part_bytes)`（纯函数）：分区 ≥1MiB 时用 32 个分片，和 v0.1 的布局完全相同；小于 1MiB 时用 4 个，分片号是 MonoId 末位的 Crockford 值模 4。

[算] 448KB 的分区共 112 个块。扣掉根目录、`/outbox`、bad、`/cfg` 共 8 块，再扣掉保底的 64KB（16 块）：
- 32 个分片时，分片目录要占 64 块，大约只能存 25 条；
- 4 个分片时，分片目录占 8 块，大约能存 80 条。

实际条数由 G-L4 实测。

**`SdStore` 和 `FatPort`**（`lib/storage/fat_port.h`、`lib/storage/sd_store.{h,cpp}`）：

```cpp
enum class FatErr : uint8_t { None, NotFound, Exists, Disk, Other };  // Disk = errno 为 EIO / ENODEV / ENXIO
class FatPort {        // 挂载点下的 POSIX 调用；路径是物理路径（"/cpa/..."）；同一时间只开一个写文件
 public:
  virtual ~FatPort() = default;
  virtual FatErr open_write(const char* path) = 0;     // O_CREAT | O_TRUNC
  virtual FatErr write(const char* data, size_t n) = 0;
  virtual FatErr sync_close() = 0;                     // fsync + close
  virtual long read(const char* path, char* buf, size_t cap, FatErr& err) = 0;
  virtual FatErr unlink(const char* path) = 0;
  virtual FatErr mkdir(const char* path) = 0;          // 已存在算成功
  virtual FatErr list(const char* dir, DirVisitor& visit) = 0;
};

class SdStore : public FileStore {                      // 纯代码
 public:
  explicit SdStore(FatPort& port, const char* root = "/cpa");  // 设备测试用别的 root，不碰真实 outbox
  bool prepare();       // mkdir root、root/outbox；探针：写 root/PROBE.BIN 1 字节 → fsync → 删除
  bool atomic_rename() const override { return false; }
  bool failed() const;  // 已锁死（§3.8）
  // FileStore 的其余方法：按 §3.5 映射路径后调用 port
};
```

- hal 只有两样东西碰 SD 库和 VFS：`SdFacts hal_sd_mount(SdStore&)`（§3.9）和 `VfsFatPort`（`::open`、`::fsync` 等，把 errno 换成 `FatErr`）。
- 探针写放在 `prepare()` 里，是因为只靠 `mkdir` 发现不了只读（寿命到头）或已写满的卡：`mkdir` 遇到已存在的目录直接成功。这样的卡如果被选成主存储，每次保存都会失败，而内部存储明明能用（§14 风险 3）。

### 3.4 选择规则：`plan_storage`（纯函数，`lib/storage/storage_plan.h`）

```cpp
struct PartFacts { bool launcher; uint32_t cpafs_bytes; bool legacy_spiffs; };
enum class SdState : uint8_t { Disabled, Absent, Unusable, Ok };
struct SdFacts { SdState state; uint32_t size_mib; };
enum class Store : uint8_t { None, Internal, Sd };
enum class StoreNote : uint8_t { None, NoCard, SdUnusable, NoInternal, LegacyTable, NoStorage };
struct StoragePlan { Store primary; bool migrate; StoreNote note; };
StoragePlan plan_storage(const PartFacts& part, const SdFacts& sd, bool lfs_mounted);
uint8_t lfs_shards(uint32_t part_bytes);
```

`SdState::Unusable` 表示卡初始化成功、但用不了：exFAT、没格式化、文件系统坏、建目录或探针写失败。草稿里的 Unmountable 和 Unwritable 合并成这一个，因为用户要做的事一样：在 Mac 上重新格式化，或者换一张卡（§14 S5）。

下表里的"旧表"指：`!launcher && legacy_spiffs && cpafs_bytes == 0`，也就是整片刷写的设备只刷了 `-app.bin`，分区表还是 v0.1 的。

| SD 状态 | `cpafs` 挂上了 | 主存储 | 迁移 | 原因（`note`） |
|---|---|---|---|---|
| Ok | 是 | Sd | 是 | None |
| Ok | 否 | Sd | 否 | 旧表时 LegacyTable，否则 NoInternal（例如 Launcher 装的是只有 app 的镜像） |
| Absent | 是 | Internal | 否 | NoCard |
| Unusable | 是 | Internal | 否 | SdUnusable |
| Disabled | 是 | Internal | 否 | None（nosd 构建不提示） |
| 不是 Ok | 否 | None | 否 | 旧表时 LegacyTable，否则 NoStorage |

补充说明：
- `launcher` 不影响选择，只影响展示，以及怎样理解 `legacy_spiffs`。在 Launcher 下，`spiffs` 是别的固件的，永远不挂载，也不提示。
- 选择时不看剩余空间。
- `migrate` 为真、但 `cpafs` 里没有待发条目时，迁移什么也不做。
- 旧表时 v0.1 的待发条目还在那个叫 `spiffs` 的分区里，不会被迁移；提示用户整片刷写后就能恢复。

### 3.5 SD 卡布局

```
/                                     Launcher 的文件：/config.conf  /downloads/  /bkp/  /Launcher_error.log（我们不碰）
/cpa/
  PROBE.BIN                           挂载时的探针（写完即删；断电可能残留，下次开机覆盖）
  outbox/<E7>/<B5><S前3>.J<S末1>       待发条目（MonoId = E7-B5-S4）
  outbox/BAD/<E7>/<B5><S前3>.J<S末1>   被 daemon 拒收或解析失败的条目（只留最新 20 条）
  cfg/projects.json                   槽位到项目 key/name 的缓存
  rec/                                v0.3 预留（§12）
  logs/                               预留
  fw/                                 建议用来放给 Launcher 安装的 .bin（固件不读）
```

例子：`K3M9Q2T-00042-0A1C` 存成 `/cpa/outbox/K3M9Q2T/000420A1.JC`。

**路径映射**（纯函数，`lib/storage/sd_paths`）。逻辑路径就是 `outbox_io` 现在用的路径，保持不变；物理路径相对挂载点 `/sd`，`/cpa` 是 `SdStore` 的 root：

| 逻辑路径 | SD 上的物理路径 |
|---|---|
| `/outbox` | `/cpa/outbox` |
| `/outbox/<E>-<B>-<S>.json` | `/cpa/outbox/<E>/<B><S前3>.J<S末1>` |
| `/outbox/<id>.tmp` | 没有映射：`open_write` 直接失败（FAT 上的提交协议从不写 `.tmp`） |
| `/outbox/bad` | `/cpa/outbox/BAD` |
| `/outbox/bad/<E>-<B>-<S>.json` | `/cpa/outbox/BAD/<E>/<B><S前3>.J<S末1>` |
| 其他路径 `/x/y` | `/cpa/x/y`（例如 `/cpa/cfg/projects.json`；单个文件带一个长文件名目录项，无所谓） |

- **为什么能用 8.3 名字**：Crockford base32 的字符（0-9、A-Z，去掉 I、L、O、U）都是合法的 8.3 字符。全大写、符合 8.3 的名字，FatFs 不会生成长文件名目录项。
- **为什么按 epoch 平铺，不按开机分目录**：
  - MonoId 去掉 epoch 后只剩 9 个有效字符（B5 + S4），加一个类型字母 `J`，正好放进 8+3。
  - epoch 只在 NVS 重新初始化时才变，所以一张卡上通常只有一两个 epoch 目录，每个只建一次。
  - 一个目录里的待发条目最多 200 条（bad 单独放）。实测 8.3 平铺 200 条 p95 29.1ms、最长 101.7ms [测]；那次测的流程还带 rename，我们直接写只会更快。
  - 按开机分目录的话，每次开机都要建一个目录（清零一个 32KB 簇，约 70–90ms [算]），还得清理空目录；迁移跨多次开机的条目时，每个开机号都要建一次目录（§14 B4 和风险 1）。
- **目录不会越查越慢**：FAT 删掉文件后目录不缩小，空出来的目录项会被复用，所以目录大小停在历史最大并发条数（≤200 项；一个 32KB 簇放得下 1024 项），查找开销有上限。
- **列目录**：`list("/outbox")` 先列 `/cpa/outbox`：
  - `BAD` 报告成名为 `bad` 的目录；
  - 名字是 7 个 Crockford 字符的目录记下来（最多 8 个 epoch，超出的不列），列完上层再逐个列（同一时间只开一个目录，和 `LfsStore` 的分片做法一样）；
  - epoch 目录里符合"8 个字符 + `.J` + 1 个字符"的文件拼回 `<MonoId>.json` 报告；
  - 其他名字原样报告，启动扫描会把它们计为 other，不去动。
- **自动建目录**：`open_write` 遇到 NotFound 时逐级建 `/cpa/outbox/<E7>`（或 `BAD`、`BAD/<E7>`），再试一次。每张卡每个 epoch 只发生一次，这一条保存允许 ≤250ms。
- 不清理空目录：epoch 目录很少。

### 3.6 FAT 上的断电安全

**提交协议**。实现在 `lib/outbox/store_commit.{h,cpp}`（`commit_file`、`move_file`），是基于 `FileStore` 接口的纯代码；`outbox_persist`、`outbox_apply`、扫描动作和项目缓存都改为调用它们：

| 操作 | `atomic_rename()=true`（LittleFS，v0.1 不变） | `atomic_rename()=false`（FAT） |
|---|---|---|
| 保存条目 | `open(<id>.tmp)` → write → close → `rename(<id>.json)` | `open(<id>.json)` → write → close（含 fsync）；任何一步失败就 `remove(<id>.json)` |
| 收到 ack（stored / duplicate） | `remove(<id>.json)` | 同左 |
| 挪到 bad/ | `rename(<id>.json, bad/<id>.json)` | 读出内容（静态 2KB 缓冲）→ 按"保存条目"写 `bad/<id>.json` → `remove(<id>.json)` |
| 项目缓存 | 写 `projects.tmp` → close → `rename(projects.json)` | 直接覆盖写 `projects.json`。写坏了就当没有缓存，daemon 会重新推送 |

**不变式**：在 `atomic_rename()=false` 的存储上，outbox 代码和项目缓存一次 rename 都不调用。测试里用 FakeStore 的调用计数来断言。

**启动扫描规则**（加粗的两条是新增的，**只在 `atomic_rename()==false` 时生效**；LittleFS 上的规则和 v0.1 一字不差，所以 v0.1 测试里"长度为 0 的 `.json` 移到 bad/"照旧成立，§14 B3）：
- **`.json` 长度为 0：删除。** 这表示没提交过：在 FAT 上，open 时目录项已经登记，但 fsync 之前就断电了。用户没见过"已保存"。
- `.json` 解析失败，或者文件里的 id 和文件名对不上：挪到 bad/（不变）。
- `.json` 读不出来（I/O 错误）：留到下次开机再处理（不变）。
- **`.tmp` 一律只计数、不删除。** FAT 上我们从不写 `.tmp`（`SdStore` 也根本不会报告 `.tmp`），所以出现了就不是我们写的；而删除一个可能和别的文件共用簇链的目录项，会连带毁掉那个文件。这条是给 FakeStore 的 FAT 模式和以后别的 FAT 后端的防线。
- bad/ 只保留最新 20 条（不变）。

**断电分析**（FAT）：

| 断电发生在 | 开机后看到 | 结果 |
|---|---|---|
| open 之后、fsync 之前 | 没有这个文件，或者是一个长度为 0 的文件 | 删除。用户没见过"已保存"，不算丢失 |
| fsync 更新目录项的过程中（扇区写了一半） | 长度对但内容坏，或者目录项坏了 | 解析失败，挪到 bad/。同一扇区里最多 15 个相邻目录项可能一起受损，见 R16 [待] |
| fsync 之后 | 完整的文件 | 正常待发 |
| 收到 ack 后删除文件时 | 文件还在 | 重发一次，daemon 判为 duplicate |
| 挪到 bad/：副本已写好、原件还没删 | 两份都在 | 原件如果解析失败，就再挪一次（覆盖副本）；如果是被拒收的有效条目，会重发再被拒一次。不会丢 |

**残余风险**：
- 断电可能留下"丢失簇"：FAT 表里标记为已占用，但没有任何文件引用它。每次断电最多泄漏一个 32KB 的簇。设备上没有 fsck，在 Mac 上用"磁盘工具 → 急救"可以回收。
- FAT 表或目录扇区写到一半断电，可能造成交叉链接或相邻目录项损坏。概率很低，取决于卡内 FTL 对扇区写入是否原子 [待]。人工 10 次断电几乎碰不上这个几毫秒的窗口，所以 G4 的物理证据很弱（R16）。

### 3.7 迁移：内部存储 → SD

接口在 `lib/outbox/outbox_migrate.{h,cpp}`：

```cpp
struct MigrateResult { uint16_t moved, failed; };
class MigrateObserver {
 public:
  virtual ~MigrateObserver() = default;
  virtual void on_item(uint16_t done, uint16_t total) = 0;
};
MigrateResult outbox_migrate(FileStore& from, FileStore& to, const OutboxCore& pending, MigrateObserver& obs);
```

**步骤**（在 `Runtime::begin` 里执行，在扫描主存储之前）：
1. 先对源跑一遍 `outbox_boot_scan(from, codec)`（LittleFS，v0.1 规则）：删掉孤立的 `.tmp`，把坏的条目挪到源的 bad/，得到有效待发条目的 id 列表。列表最多 200 条，按 id 升序；超出的留在源里，下次开机再迁。
2. 对每个 id：读出源文件（静态 2KB 缓冲）→ 用 `commit_file` 写到目标（FAT 协议：截断重写，所以会覆盖上次迁移中断留下的残缺副本）→ 删源。任何一步失败都保留源文件，计入 `failed`，然后继续下一条。
3. bad/ 不迁移，留在 `cpafs` 里（它们从不发送，只是证据，§14 S3）。
4. `/cfg/projects.json` 不迁移，daemon 会重新推送。
5. 每迁完一条就回调一次 `obs.on_item`。外壳在回调里 kick USB，并刷新"正在迁移到 SD 卡 i/n"。

**不变式**：源里的条目，只有在目标上提交成功（fsync + close 都成功）之后才会被删除。这和用户保存时显示"已保存"的标准一样。所以无论在哪一刻断电，每一条要么在源里，要么在目标上，要么两边都有；下次开机再跑一遍就会收敛。

**不会因此丢失**：
- 目标已提交、源还没删时断电，下次开机没插卡：条目从内部存储发出并确认后删除；之后插卡，SD 上那份会再发一次，daemon 判为 duplicate。
- 发送只从主存储读取；即使出现重复，daemon 也会按 `(dev, id)` 去重。

**耗时** [算]：每条约 40ms（读内部 Flash 约 5ms，写 SD 约 20–30ms，删内部文件约 10ms）。200 条约 8s，而且只会发生一次。目标是 epoch 平铺目录，不会每个开机号建一次目录。

**测试**：两个 FakeStore 共用一个"电源"对象，在两边任意第 k 步操作之前断电，k 遍历所有步骤；断电后再跑一遍迁移和扫描。要求 0 丢失，最后每条在目标上恰好出现一次，源被清空。设备测试用来自 ≥20 个不同开机号的 50 条来计时。

### 3.8 运行中出故障

- **主存储开机时选定，本次运行期间不切换。** 这样简单、好测；而且 `OutboxCore` 只记 id，不记条目放在哪个存储里。
- **SD 出磁盘级错误就锁死**：`SdStore` 第一次从 `FatPort` 得到 `FatErr::Disk`（errno 为 EIO / ENODEV / ENXIO，也就是 FatFs 的 `FR_DISK_ERR`、`FR_INT_ERR`、`FR_NOT_READY` 一类），就把自己标成 failed。之后每个方法都立即返回失败，不再调用 `FatPort`，直到重启。
  - 原因：拔卡后每次 FatFs 调用都会先走 `disk_status`（CMD13，3 次重试各 `delay(100)`）和 `disk_initialize`（CMD0，同样重试），`sdSelectCard` 还可能等 500ms，一次 0.6–2s [源/算]。`pump_outbox` 每次重发都要读最多 4 条在途条目，不锁死的话主循环会每隔几秒卡住一次，超出单步预算十倍以上。
  - 锁死前的那一次操作仍然要走完重试，所以拔卡后会有一次 0.6–2s 的卡顿 [算]，之后按键和屏幕正常响应（G8）。
  - 锁死后：保存提示"保存失败（E_STORAGE）"；系统页显示"存储错误 N，请重启"。卡上的待发条目留在卡上，不会丢。
  - 重新插卡后，按 RST 或拨电源开关重启即可恢复；在 Launcher 下重启会先经过 5 秒的启动画面。
- **拔卡时卡上还有待发条目**：这些条目留在卡上，下次插卡开机后照常发送。在此期间，状态栏的 ↑N 不包含它们。README 里写明"↑N 不为 0 时不要拔卡"。
- **卡写满**：不是磁盘级错误，不锁死；保存失败，显示通用的"保存失败（E_STORAGE）"。不做专门的"存储已满"提示：`Runtime` 只在开机时读一次 `free_bytes()`，要做就得改成每次保存都查（§14 风险 2）。outbox 自己最多占 200 个 32KB 的簇（约 6.4MB），卡被写满只可能是别的文件占满了。

### 3.9 SD 驱动与挂载

- **驱动**：core 2.0.17 `SD` 库的补丁副本，从 `firmware/hwtest/lib-core2/SD` 挪到 `firmware/third_party/SD`，app 和 hwtest 共用。
  - 补丁 1（ACMD41 参数只带 HCS）保留。
  - 补丁 2（`GET_BLOCK_SIZE` 报 4MiB，让 `f_mkfs` 对齐）保留。app 不调用格式化，这个补丁留给 hwtest 和以后用。
  - 补丁 3（去掉 25MHz 上限）改成只在定义了 `CPA_SD_UNCAPPED_CLOCK` 时生效，只有 hwtest 定义它。
  - NOTICE 里补上 Apache-2.0 的条目，写明路径和三处补丁。
  - 这份代码放在 `firmware/third_party/` 下，不进 check_sizes、lizard 和 clang-tidy 的检查范围：它是第三方代码，除了标了 `CPA patch` 的地方以外原样保留。
  - app 只用 `sd_diskio.h` 里的 `sdcard_*` 函数，不用 `SDFS` 类（`SD.begin()`）。
  - `cardputer-adv-test` env 的 `lib_deps` 是显式列出的，要加上 `storage`；构建日志里要能看到用的是 `third_party/SD`，而不是框架自带的 `SD`。
- **挂载步骤**（`hal_sd_mount(SdStore&)`）：
  1. 定义了 `CPA_STORAGE_NO_SD`：直接报 Disabled，不碰总线。
  2. `SPI.begin(40, 39, 14, 12)`；`sdcard_init(12, &SPI, 20000000)`。这一步只登记驱动，不碰卡。
  3. `sdcard_mount(pdrv, "/sd", 2, false)`。卡在这里才初始化（`f_mount` 里的 `disk_initialize`，初始化阶段驱动固定用 400kHz）。`format_if_empty` 固定为 false，永远不格式化。
  4. 挂载失败：读 `sdcard_type(pdrv)`。
     - `CARD_NONE` / `CARD_UNKNOWN`：Absent。
     - `CARD_SD` / `CARD_SDHC` / `CARD_MMC`：Unusable（卡初始化成功，但没有可用的 FAT，例如 exFAT）。
     - 两种情况都**不调用 `sdcard_uninit`**（它会再发一次 CMD0，没卡时又要 300ms 以上），也**不重试**；驱动留着登记、不再使用（`sdcard_mount` 失败时已经注销了 VFS 路径）。
  5. 挂载成功：`size_mib = sdcard_num_sectors(pdrv) / 2048`；`store.prepare()` 失败就报 Unusable，否则 Ok。
- **不做 4MHz 降速重试**：五档时钟（含 20MHz）都 5/5 通过 [测]，驱动每块都校验 CRC16（§14 S1）。这取代 launcher.md R7 的"比对校验后退回 4MHz"。
- **没卡时的开机代价**：`GO_IDLE_STATE` 重试 3 次、每次 `delay(100)`，约 300ms；如果 MISO 空闲时读成 0x00，`sdWait` 和 `sdSelectCard` 还要各等 500ms，可能超过 2s [源/算]。预算按 ≤500ms 定，由 device `test_sd_absent` 实测；超了就在 `SPI.begin` 之后把 GPIO39 设成内部上拉再测 [待]。
- **文件名额**：`max_files = 2`，一个写、一个读。本 SDK 的 `FF_MAX_SS` 是 4096，每个名额约占 4KB 堆。v0.3 录音时要增加到 3 个。
- **剩余空间**：v0.2 不调用 `f_getfree`。容量由 200 条的上限约束；v0.3 要显示还能录多久时，再加一个耗时有上限的 FSINFO 读取。
- **卡的格式要求**：FAT32（或 FAT16），分区表用 MBR。
  - exFAT 卡（32GB 以上的卡出厂都是 exFAT）是 Unusable，系统页提示"SD 卡无法使用：请在 Mac 上格式化为 FAT32"。
  - README 给出格式化命令：`diskutil eraseDisk FAT32 CPA MBRFormat /dev/diskN`。执行前先用 `diskutil list` 把盘号核对两遍。
- **扩展口 GPIO**：v0.2 不碰 GPIO 3/4/5/6/13/15。Launcher 为了兼容扩展帽，会把这几个脚拉高；插扩展帽的情况放到 G-L7 里测，v0.2 不支持插着扩展帽用 SD。
- **测试构建 `cardputer-adv-test-nosd`**：定义 `CPA_STORAGE_NO_SD=1`。这时 SD 报 Disabled，主存储是内部。用途是在插着卡的 HIL 设备上回归 v0.1 的行为，以及给迁移场景种数据。如果断电闸门显示 SD 上丢了已保存的条目，发布版也可以用这个开关退回"内部为主"（R16）。

## 4. Launcher：构建方式与分区检测

### 4.1 决定：一个二进制，不另建 env

理由：
- Launcher 读的是合并镜像里的分区表，据此建出 `cpafs`。
- 我们的 app 放在 `ota_N` 里运行，和放在 `app0` 里没有区别。
- 两种情况下的差异（是否在 Launcher 下、分区大小、复位原因）都能在运行时检测出来。
- Bruce 的 8MB 版本也是这样做的。

**开发流程**：`pio run -t upload` 会把 Launcher 冲掉。日常开发照旧用整片刷写，Launcher 模式只在 G-L 闸门和 HIL 里测。

### 4.2 分区表 `firmware/app/partitions/cpa_8MB.csv`

```
nvs,      data, nvs,      0x9000,   0x5000,
otadata,  data, ota,      0xe000,   0x2000,
app0,     app,  ota_0,    0x10000,  0x330000,
app1,     app,  ota_1,    0x340000, 0x330000,
cpafs,    data, spiffs,   0x670000, 0x180000,
coredump, data, coredump, 0x7F0000, 0x10000,
```

- 和 `default_8MB.csv` 相比只改了一个标签。
- 子类型仍然写 `spiffs`（0x82）：IDF 4.4 的 `gen_esp32part.py` 不认识 `littlefs`，Launcher 两个都认。
- CI 解码构建出来的 `partitions.bin`，断言三件事：
  - 有 `cpafs`，位置 0x670000、大小 0x180000；
  - 没有任何标签叫 `spiffs` 的分区；
  - 没有子类型为 test 的 app 分区（否则 Launcher 检测会误判）。

### 4.3 运行时检测（`lib/hal/part_facts`）

```cpp
PartFacts hal_part_facts();   // 只收集事实；判断逻辑在 plan_storage 里
//  launcher      = 存在 APP/TEST 分区，并且当前运行的不是它（我们自己的分区表里没有 test 分区）
//  cpafs_bytes   = esp_partition_find_first(DATA, ANY, "cpafs") 的大小；没有就是 0
//  legacy_spiffs = 存在标签为 "spiffs" 的数据分区（只用来给提示，永远不挂载）
```

每次开机都按标签重新查找，不缓存任何 flash 偏移，因为 Launcher 会搬动分区。

### 4.4 发布产物、安装与升级

| 文件 | 用途 |
|---|---|
| `cpa-<tag>.bin` | 合并镜像，包含 bootloader、分区表、boot_app0 和 app。整片刷写时写到 0x0；也可以直接放进 SD 给 Launcher 安装，Launcher 里显示为 `cpa-<tag>`（不超过 20 个字符） |
| `cpa-<tag>-app.bin` | 只有 app，写到 0x10000，用于不改分区表的重刷。Launcher 也能装，但这样不会建 `cpafs`，只能用 SD |
| `SHA256SUMS` | 校验和 |

合并镜像的逻辑从 `release.yml` 挪到 `scripts/make_image.py`，HIL 也用它打测试版的合并镜像。

**NVS 说明**：`merge_bin` 会用 0xFF 填满段之间的空隙，所以把 `cpa-<tag>.bin` 写到 0x0 会清空 0x9000 的 NVS，epoch 和开机计数会重新生成。按 05 §5.9 的设计这是安全的（新 epoch 的 id 不会和旧的撞），但文档和 HIL 都**不能承诺**写 0x0 之后 epoch 或开机计数还在。`pio run -t upload` 是分段写的，不碰 NVS。这一条修正 launcher.md §4 里"NVS 原样保留"的说法：那句话只对 `pio run -t upload` 成立。

**第 0 步：切换前先清空内部的待发条目**（两个方向都要做；写进 README、G-L6 和 07 检查单）：
- 让状态栏的 ↑N 变成 0（连上 Mac 同步完）；或者插上 SD 卡开机一次，让迁移把 `cpafs` 清空，再到系统页确认内部待发为 0。
- 原因：整片刷写的设备上，`cpafs` 在 0x670000；网页刷写 Launcher 会换掉分区表，这块区域要么没人认，要么之后被 Launcher 动态放置的 `ota_N` 覆盖。反过来，从 Launcher 回到整片刷写，Launcher 下那块 448KB 的 `cpafs` 也会同样被丢下。

**安装 Launcher 版**（写进 README）：
0. 按上面的第 0 步清空内部待发。
1. 用网页刷写器或 esptool 装 Launcher（建议用 beta 版，见开放问题 4）。
2. 把 `cpa-<tag>.bin` 放到 SD 卡上，建议放在 `/cpa/fw/`。
3. 在 Launcher 里选 SD → 选这个文件 → 安装。

**在 Launcher 下升级**：
- 推荐用覆盖安装（Launcher 空间不够时给出的"Use <标签> partition"选项，或串口 `flash overwrite`），或者删除旧版本后立刻重新安装新版本。**不要**删掉旧版本又留着别的版本不管：删除任何一个我们的版本，都会连 `cpafs` 一起擦掉，剩下的版本在没插卡时就没有存储可用（NoStorage）。
- 插着 SD 卡时，待发条目都在卡上，不受影响；没插卡时先做第 0 步。

**从 v0.1 升级（整片刷写）**：
- 执行 `pio run -t upload`（保留 NVS），或者把 `cpa-<tag>.bin` 写到 0x0（清空 NVS，epoch 重新生成）。两种方式都不碰 0x670000，新的 `cpafs` 和 v0.1 的 `spiffs` 是同一块区域，数据都还在；插着 SD 卡时开机会自动迁移。
- 如果只写了 `-app.bin`，分区表不会更新。这时系统页提示"分区表是 v0.1 的，请整片刷写"，数据没有丢，整片刷写后就能恢复。

**用户须知**：
- 开机时按住 G0 会进入 Launcher 菜单，而不是打开我们的速记。
- 冷启动会多一个约 5 秒的 Launcher 启动画面。
- 拨电源开关关机再开机，会先进 Launcher。

### 4.5 固件必须遵守的规矩（摘自 launcher.md 的 R1–R8）

- 只挂载、只格式化 `cpafs`。分区不存在就禁用内部存储，绝不回退去用 `spiffs`。
- 每次开机按标签查找分区，不缓存偏移。
- 不写 otadata 和分区表，不调用 `esp_ota_set_boot_partition`，不调用 `nvs_flash_erase`；v0.2 不新增任何 NVS 键。
- 不用深睡；需要重启时一律用 `esp_restart()`。
- 发给 Launcher 的必须是合并镜像，这样它才会建出 `cpafs`。
- R7（SD 降速重试）被 §3.9 取代。

## 5. 预算（更新 05 §5.13）

| 项 | v0.1 | v0.2 | 怎么检查 |
|---|---|---|---|
| 静态 RAM | ≤96KB（实际 59KB） | 上限不变；新增的静态缓冲不超过 4KB（`move_file` 2KB、迁移 2KB） | `check_budget.py` |
| 空闲堆 / 最大连续块 | ≥150KB / ≥64KB | 指标不变，但要**在 SD 已挂载的状态下**测；挂载 SD 增加的堆不超过 16KB | device `test_runtime_budget`、`test_sd_store` |
| loop 栈余量 | ≥4KB | 不变。经过 `SdStore` 的调用链另加 2KB 的 FatFs 余量 [算]（`FF_USE_LFN=2` 把约 512B 的长文件名缓冲放在栈上，再加 VFS 和 SPI 的帧）；新增链：收到 ack → `apply_outbox` → `move_file` → `SdStore`，以及 `setup` → `Runtime::begin` → `outbox_migrate` → `commit_file` → `SdStore` | `stack_budget.py`、`stack_gate.py`、device `test_runtime_budget`（含一次 SD 上的拒收 ack） |
| app 镜像 | ≤2.5MB | 硬上限仍是 2.5MB；**超过 1.75MiB（1,835,008B）时警告**，这是和 Bruce 1.16.1 一起装进 Launcher 时的余量 | `check_budget.py` |
| 保存 | p95 ≤100ms（LittleFS 积压 200 条时 108–158ms，是已知问题） | SD：p95 ≤100ms，积压 200 条时也要满足；单次最长 ≤150ms；每张卡每个 epoch 的第一条（要建 epoch 目录）≤250ms | device `test_sd_store` |
| 单步阻塞 | ≤50ms | SD 写入例外：卡内部整理时会停顿 30–65ms，单次保存不超过 150ms 就算合格。拔卡后发现错误的那一次操作会卡 0.6–2s [算]，之后锁死，不再碰总线 | device；人工 |
| 开机准备存储 | — | 插卡：初始化、挂载、`prepare()`（含探针写）≤200ms；没插卡 ≤500ms [待]；扫描时每条待发约 5ms [测]；迁移每条约 40ms，只发生一次 | device `test_sd_store`、`test_sd_absent`（计时） |
| outbox 容量 | 200 条，并且剩余空间 ≥64KB | SD：200 条（受内存里 id 数组的大小限制）；`cpafs` 1.5MB：200 条；`cpafs` 448KB：约 80 条 [算] | G-L4 |

## 6. 协议变化（只新增字段，`v` 仍为 0，符合 05 §6.9）

设备回复的 `hello` 加两个可选字段：

| 字段 | 取值 | 含义 |
|---|---|---|
| `store` | `"sd"` / `"int"` / `"none"` | 本次开机选定的主存储 |
| `launcher` | bool | 是否在 Launcher 下运行 |

- **为什么需要**：
  - HIL 要能确认一个场景真的走了 SD。否则 SD 用不了时，固件会悄悄退到内部存储，场景照样通过。
  - 保存失败时，用户能在 `cpa status` 里看到原因（例如 `none`）。
- **兼容性**：`hello.schema.json` 的 device 部分没有 `additionalProperties: false`，v0.1 的 daemon 会忽略这两个字段；v0.2 的 daemon 遇到 v0.1 设备（没有这两个字段）显示 `-`。
- **不做的**：不新增消息类型，outbox 的格式不变，`debug.state` 不变。
- **契约**：
  - `hello.schema.json` 的 device 部分加两个可选属性。
  - 测试向量：带这两个字段的合法；不带的也合法（v0.1 设备）；`store:"fat"` 非法；`launcher:"yes"` 非法。
  - 固件不设置这两个字段时，编码结果和 v0.1 逐字节相同。
- **Launcher 控制台的守护规则**：主机发出的每一行，要么以 `{` 开头，要么是空行（nudge）。daemon 里加测试守住这一点；`protocol/README` 写明。
- **v0.3 预留（这次不实现）**：
  - outbox 新增 `kind:"rec"`，用来登记一段录音（rec id、字节数、sha256、时长）。它不带执行语义，所以符合"outbox 不装执行类条目"的规定。
  - 录音传输用的消息（`voice.*` 的名字已经占住，见 05 §6.10）。

## 7. daemon 变化

1. 解析并保存 `hello.store` 和 `hello.launcher`。
   - `cpa status` 打印 `store=sd launcher=yes`；JSON 输出里放在 `device` 下面。
   - 旧固件没有这两个字段时，显示 `-`。
2. 加守护测试：所有主机主动发出的消息序列化后都以 `{` 开头，nudge 是空行，不会出现以字母开头的行。
3. **已绑定设备的短退避：推迟，由 G-L9 决定做不做。**
   - 担心的情形：用户开机时 Mac 正连着、daemon 在跑。Launcher 启动画面的 5 秒里，daemon 发出的 hello 只会收到 `ERR …`（判为 unrelated）；3 次 hello 共 6 秒都没有有效回复，就是 `no_reply`，端口被挂起 30 秒。我们的固件 5 秒后就起来了，却要再等最多 30 秒才能连上。
   - 现在的代码里，挂起是按序列号记的（`port_finder.Backoff` 以 serial 为键），端口重新枚举也不会解除 [源]。不过如果 Launcher 的 `ESP.restart()` 让端口消失，链路会以别的原因结束（不在挂起列表里），马上就能重连；如果 Launcher 用的不是 303A:1001，PortFinder 根本不会碰它。到底是哪种情况，要实测 [待]。
   - G-L9：daemon 在跑、设备插在 Mac 上，在 Launcher 下开机 10 次，每次我们的固件都要在 12 秒内 link ready。不通过，就由 M2-C 做一个小改动：已绑定序列号的端口如果是 `no_reply`，只挂起 3 秒；`foreign`、`version`、`open_failed` 仍是 30 秒。
4. 其余不变：事件、笔记、按 `(dev, id)` 去重、credit、nudge。

## 8. 界面变化

- **系统页加两行**。总行数上限相应调大，必要时把 daemon 那一行合并进别的行。
  - 第一行是启动方式：`启动 Launcher` 或 `启动 直接`。
  - 第二行是存储：`存储 SD 476G`、`存储 内部 1.5M` 或 `存储 无`。后面跟上原因（`StoreNote`）：
    - NoCard：`SD：未检测到卡`
    - SdUnusable：`SD 卡无法使用：请在 Mac 上格式化为 FAT32`
    - NoInternal：`无内部存储，只用 SD`
    - LegacyTable：`分区表是 v0.1 的，请整片刷写`
    - NoStorage：`没有可用存储：请插入 FAT32 SD 卡`
  - 迁移过就显示 `已迁移 N`；有失败的显示 `迁移失败 N`；存储错误计数不为 0 时显示 `存储错误 N，请重启`。
- **Compose 提交时的提示**：
  - 主存储是 None：提示"没有可用存储：请插入 FAT32 SD 卡"。这需要新增 `SaveResult::NoStorage`，并且**在判断 Full 之前**判断（NullStore 的 `free_bytes()` 是 0，否则会先报 Full）。
  - 写入失败（包括 SD 锁死、卡写满）：提示"保存失败（E_STORAGE）"，存储错误计数加 1。
- **开机迁移**：超过 0.5 秒时，屏幕显示"正在迁移到 SD 卡 i/n"。
- **状态栏**：不变。

## 9. 测试策略

### 9.1 native 测试（进 CI，按 TDD 先写）

| 套件 | 测什么 |
|---|---|
| `core/test_sd_paths` | 逻辑路径和物理路径的双向映射；非法 id、超长路径、小写名字和长文件名、BAD 目录；生成的名字都是合法的 8.3 名字；`.tmp` 没有映射 |
| `core/test_storage_plan` | §3.4 表格的每一行；launcher、legacy_spiffs、cpafs_bytes 的各种组合；`lfs_shards` 的边界（1MiB−1 和 1MiB） |
| `core/test_sd_store` | 用 FakeFatPort：路径映射；list 拼回 MonoId、epoch 目录上限、other 原样报告；父目录自动创建；`prepare()` 的探针写和失败情况；`rename()` 返回 false 且不调用 port；root 前缀；**任何一次 `Disk` 错误之后，对 port 的调用数为 0**，`free_bytes()` 变成 0 |
| `core/test_store_commit` | 两种语义下的 `commit_file` / `move_file`；FAT 模式下 rename 调用次数为 0；失败时的清理 |
| `core/test_outbox_fat` | 用 FakeStore 的 FAT 模式，在保存、ack、挪到 bad/、写项目缓存的每一步注入断电：0 丢失，显示过"已保存"的条目一定还在；长度为 0 的 `.json` 被删除；`.tmp` 只计数、不删除 |
| `core/test_outbox_migrate` | 两个 FakeStore 共用一个电源，在每一步断电后重跑：结果收敛、0 丢失、源被清空；还要覆盖目标上已有同 id 的残缺副本、源超过 200 条、源的 bad/ 原样不动 |
| v0.1 的全部套件 | 不改，必须继续通过（目标 G6）；`test_outbox_boot_scan`、`test_outbox_core` 用 `git diff` 守住"一行不改" |
| `ui/test_mod_system`、`ui/test_runtime_storage` | 系统页每种 `StoreNote` 的文字；NoStorage 先于 Full；`Runtime::begin` 有迁移源时先迁移、再扫描；`hello` 里带上 store 和 launcher；FAT 上的项目缓存不调用 rename；写入失败时存储错误计数加 1 |
| 协议向量和 daemon 的单元测试 | 见 §6、§7 |

**FakeStore 的 FAT 模式**（只模拟需要的部分，§14 S2）：
- `atomic_rename()` 返回 false；
- rename 的目标已存在时失败（同 EEXIST）；
- 统计 rename 调用次数，测试断言为 0；
- open 时就创建一个可见的、长度为 0 的条目（现有 FakeStore 已经是这样），断电后它留着。

不模拟"rename 拆成两步留下共用簇链的两个名字"：按不变式，FAT 上的代码根本不调用 rename，断言调用次数为 0 就够了。

**FakeFatPort**（`test/core/support/fake_fat_port.h`）：内存里的物理路径和目录；可以在第 k 次调用时注入 `Disk`、`NotFound` 等错误；统计每种调用的次数。

### 9.2 真机设备测试（`pio test -e cardputer-adv-test -f 'device/*'`）

- **`test_sd_store`**（插卡；用 root `/cpatest`，测完删掉，不碰真实 outbox）：
  - `hal_sd_mount` 加 `prepare()` 的耗时 ≤200ms，时钟 20MHz；
  - 直接写入 → 读回 → 重新挂载 → 再读；
  - list 能还原出 MonoId；
  - 200 条保存的 p95 和最长值，分"每条立即 ack"和"全部积压"两种；新 epoch 目录的第一条单独计（≤250ms）；
  - 挂载前后的堆差不超过 16KB；
  - LFS → SD 迁移 50 条（来自 ≥20 个不同的开机号），计时，0 丢失。
- **`test_sd_absent`**（拔卡，人工执行）：`hal_sd_mount` 返回 Absent，耗时 ≤500ms。
- **`test_storage`（更新）**：
  - 分区标签是 `cpafs`；整片刷写时大小 1.5MB，用 32 个分片；
  - part facts：直接启动时 launcher 为 false，legacy_spiffs 为 false。
- **`test_runtime_budget`（更新，M2-B）**：在 SD 已挂载、SD 为主存储的状态下测堆、最大连续块和栈余量；工作负载里包含一次被拒收的 ack（在 SD 上走 `move_file`）。

### 9.3 HIL（`tools/hil`，由独占设备的 agent 运行）

| 场景 | 用的固件 | 检查什么 |
|---|---|---|
| `sd_empty` | `cardputer-adv-test`，插卡 | `hello.store=="sd"`；连上后 pending 降到 0。作为迁移场景的前置条件 |
| `sd_store` | 同上 | `hello.store=="sd"`；离线记 3 条 → `debug.reboot` → 每条恰好送达一次 |
| `sd_bulk` | 同上 | 离线记 100 条，恢复连接后 5 秒内全部送达，没有重复 |
| v0.1 的全部场景 | 上面这个，以及 `cardputer-adv-test-nosd` | 全部通过；nosd 下 `hello.store=="int"` |
| `migrate_seed` → `migrate_check` → `migrate_drained` | 先在 test 上跑 `sd_empty`；然后依次用 nosd → test → nosd，两次之间用 `pio run -t upload` 换固件（保留 NVS 和 `cpafs`） | 先在内部存储里种下 N 条；换成 test 固件后 `store=="sd"`，种下的每条恰好送达一次、没有别的条目；再换回 nosd 时 `pending==0` |
| `launcher_boot` | 由 Launcher 安装的测试版合并镜像 | `hello.launcher==true`、`rst=="software"`；然后再跑 handshake、capture、reboot |

### 9.4 人工检查单（加进 07）

- launcher.md §8 的 G-L1–G-L4、G-L6–G-L8。G-L5 不测（v0.2 不做"回到 Launcher"）。G-L6 的升级流程按 §4.4（第 0 步、覆盖安装）。
- **G-L9（新增）**：Mac 连着、daemon 在跑，在 Launcher 下开机 10 次。每次我们的固件都要在 12 秒内 link ready；记下有没有出现 30 秒的挂起。
- SD 相关：
  - 10 次断电：看到"已保存"后立刻把开关拨到 OFF 并拔线，要求 0 丢失。结果按低强度证据记录（R16）。
  - 运行中拔卡：最多卡一次（≤2s），之后按键正常；系统页显示存储错误；重新插卡并重启后恢复正常。
  - 不插卡开机：跑 device `test_sd_absent`，记下耗时。
  - 插一张 exFAT 卡：系统页给出"无法使用，请格式化"的提示。
  - **`diskutil` 格式化的卡**：把这张开发卡放进 Mac 的读卡器，用 `diskutil eraseDisk FAT32 CPA MBRFormat /dev/diskN` 重新格式化（已获授权；盘号核对两遍），再跑 device `test_sd_store`，记下 p95、建目录耗时和写停顿（2 个 FAT、对齐未知，R17）。
  - 再测 2–3 张卡，其中至少一张是 32GB 以下、出厂就是 FAT32 的。
  - 用 Mac 的"磁盘工具 → 急救"检查卡上有没有丢失簇。

## 10. 工作包与依赖

```
M2-A 固件存储层 ─────────┐
                         ├──► M2-B 固件集成、界面、Launcher 发布 ──► M2-D HIL 与检查单
M2-C 协议与 daemon ──────┘                                          ▲
        └───────────────────────────────────────────────────────────┘
```

- A 和 C 可以并行。
- B 依赖 A 和 C。不过 B 里的脚本、CI、README 和系统页的纯逻辑，可以按 §3.4 和 §6 定下的接口先开工。
- D 依赖 B 和 C。
- 各包负责的路径互不重叠（见工作包列表）。A 不改 `src/main.cpp`：`LfsStore::begin()` 签名不变，所以 A 合入后现有的接线照样能编译。
- 规则同 08：先写测试（RED），再实现（GREEN），最后重构（REFACTOR）。改动只留在工作区，不提交，由 Lead 统一提交。
- 前提：Lead 先提交 hwtest 的 SD 工作（`lib-core2/SD`、`sd_*.cpp`、两份闸门报告），A 才能搬驱动。

## 11. 风险

| # | 风险 | 影响 | 缓解办法 / 对应闸门 |
|---|---|---|---|
| R1 | Launcher 的 bootloader（IDF 5.5）启动不了我们的 IDF 4.4 app | Launcher 这个目标失败 | 先跑 G-L1；实在不行就只发整片镜像 |
| R2 | Launcher 自带的 SD 驱动读不了这张卡（同样的 ACMD41 问题） | 没法从 SD 安装 | G-L1；换一张卡、用串口 flasher，或者向上游提修复（开放问题 5） |
| R3 | FAT 断电后留下丢失簇或交叉链接 | 空间泄漏；极少数情况下文件损坏 | 不用 rename、不删 `.tmp`；用 Mac 急救回收；人工 10 次断电 |
| R4 | 只测过一张卡 | 别的卡可能初始化失败 | 只带 HCS 符合 SPI 模式规范 [源]；再测 2–3 张卡 |
| R5 | FSINFO 无效时 `f_getfree` 要扫约 35 秒 | 开机卡死 | v0.2 不调用它 |
| R6 | 运行中拔卡 | 一次 0.6–2s 的卡顿；之后保存都失败直到重启；卡上的待发条目不计入 ↑N | SdStore 锁死（§3.8）；提示；README 写明 |
| R7 | Launcher 下没插 SD 时，`cpafs` 只有 448KB | 约 80 条就满 [算] | 用 4 个分片；G-L4 实测；不够再讨论方案 B |
| R8 | 在 Launcher 里删除任何一个我们的版本，都会擦掉 `cpafs` | 没插 SD 时会丢失待发条目，剩下的版本没有内部存储 | README：第 0 步、优先覆盖安装；SD 为主时不受影响 |
| R9 | core 2.0.17 遇到 NVS 满了会擦掉整个 NVS | Launcher 的设置也被清掉 | v0.2 不新增 NVS 键 |
| R10 | SD 写入停顿 30–65ms，超过 50ms 的单步规则；HWCDC 回复被扣住 | 链路偶尔有延迟 | 单次保存 ≤150ms 视为合格；daemon 的 nudge 已经有了；HIL `sd_bulk` 验证 100 条 ≤5 秒 |
| R11 | 开机迁移 200 条约需 8 秒，期间 setup() 不回复 | daemon 的 3 次 hello 可能都超时，端口挂起 30 秒 | 只发生一次；屏幕显示进度；可以接受 |
| R12 | 扩展帽和 SD 共用同一条 SPI 总线（GPIO5 等没有拉高） | SD 不稳定 | G-L7；v0.2 不支持插着扩展帽时用 SD |
| R13 | 第三方代码（Apache-2.0，约 870 行）要自己维护 | — | 补丁处都标 `CPA patch`；写进 NOTICE；考虑向上游提交（开放问题 5） |
| R14 | Launcher 的串口控制台把主机发的行当成命令；它的控制台任务在启动画面之前就开了 | 误触发 Launcher 命令；开机时握手被拖慢 | 主机发出的行只以 `{` 开头或者是空行，daemon 测试守住；G-L9 |
| R15 | 开机按 G0 会进 Launcher 菜单；冷启动多 5 秒 | 用户困惑 | 写进 README 和 PRD |
| R16 | 目录扇区写到一半，或者卡内 FTL 的配对页损坏，可能连带毁掉同一扇区里最多 15 个已保存条目的目录项 | G4 在物理层面没有被真正证明：人工 10 次断电几乎碰不上这个几毫秒的窗口 | G4 只声称协议层面 0 丢失；断电结果按低强度证据记录；一旦断电闸门出现任何丢失，发布版用 `CPA_STORAGE_NO_SD` 退回内部为主 |
| R17 | 所有性能数据都来自 FatFs 格式化的卡（1 个 FAT，4MiB 对齐）；`diskutil` 格式化的是 2 个 FAT（每次改 FAT 都写两遍），对齐未知 | G3 的 p95、建目录耗时可能变差 | 在 `diskutil` 格式化的卡上重跑 `test_sd_store`（§9.4，已获授权） |
| R18 | 没卡时的开机耗时取决于 MISO 空闲电平 | 可能超过 500ms，最坏 2s 以上 | `test_sd_absent` 实测；超了就给 GPIO39 开内部上拉 |
| R19 | 把合并镜像写到 0x0 会清空 NVS | epoch 和开机计数重新生成 | 按设计无害；文档和 HIL 不承诺保留（§4.4） |
| R20 | FatFs 的长文件名缓冲和 VFS、SPI 的帧都在 loop 栈上，`stack_budget.py` 现在的 1KB 框架余量不够 | 栈溢出 | 经过 SdStore 的链另加 2KB 余量；新增两条链；`test_runtime_budget` 在 SD 上跑一次拒收 ack |

## 12. v0.3 预留：录音（只写设计，这次不实现）

- **布局**：`/cpa/rec/<E7>/<B5><S前3>.W<S末1>`，和 outbox 一样按 epoch 平铺；每一段录音一个 MonoId，每 5 分钟分一段。
- **不用"先写 `.part` 再改名"**（FAT 上 rename 不安全），改为：
  1. 直接写最终文件名，WAV 文件头先写占位，每秒 fsync 一次；
  2. 一段录完后，回填文件头，再 fsync；
  3. 然后写一条 outbox `rec` 条目来登记这段录音。这条登记就相当于"提交"。
- **断电留下的半段录音**：文件头里的长度是 0，实际大小以目录项为准（也就是最后一次 fsync 时的大小）。开机扫描发现没有登记过的录音段，就补登记；Mac 端按实际大小修复文件头。
- **写入方式**：用麦克风的 release callback 补缓冲，4 个 4KB 的缓冲组成环形，写满一块就写一块，每秒 fsync 一次（sd-card.md §7 实测 0 丢帧）。`max_files` 提高到 3。
- **剩余空间**：加一个耗时有上限的 FSINFO 读取，用来显示还能录多久。
- **传到 Mac**：base64 只有 138KB/s，1 小时录音要传 14 分钟，太慢。v0.3 改用二进制分帧加滑动窗口，或者 U 盘模式；动手之前先测 HWCDC 的持续吞吐。
- **录音时约 27% 的回复被扣住**：把写卡挪到单独的任务里，或者依靠 daemon 的 nudge。
- **SD 锁死**（§3.8）对录音同样适用：录音中拔卡就结束这一段。
- **工具链**：录音继续用工具链 A（ADR-0001；IDF 5.5 有 #18621 录音全 0 的问题）。

## 13. 对已有文档的影响（由 Lead 改）

- **05 §5.9**："Outbox（只用 LittleFS）"这一说法，以及 §5.13 的预算表，由本文取代。在 05 里加一条指向本文的注释。
- **04 §5.2**：原来写在 v0.2 的内容按新路线图挪到 v0.3（离线录音）和 v0.4（hooks、审批、回复）。另外补两条：开机时按 G0 会进 Launcher 菜单；在 Launcher 下不用深睡。
- **launcher.md**：R7（SD 降速重试）被 §3.9 取代；§4 的"NVS 原样保留"只对 `pio run -t upload` 成立（§4.4）；G-L5 不测。
- **sd-card.md §8**："v0.2 的 outbox 继续放在 LittleFS"和"按开机计数分目录"这两条建议，由本文的"SD 为主、按 epoch 平铺"取代（开放问题 1）。
- **07**：由 M2-D 加上 G-L1–G-L4、G-L6–G-L9 和 SD 的人工检查项。
- **ADR**：如果你接受"SD 为主"（开放问题 1），写成 ADR-0003。

## 14. 评审处理记录

### 14.1 阻塞问题（全部采纳）

| # | 问题 | 处理 | 位置 |
|---|---|---|---|
| B1 | 分开调用 `sdcard_init` 和 `sdcard_mount` 区分不了 Absent 和 Unmountable：`sdcard_init` 不碰卡；"只在初始化成功时降速重试"无法判断；没卡时卸载加重试会超出 500ms | 挂载失败后读 `sdcard_type()`：NONE/UNKNOWN 是 Absent，SD/SDHC/MMC 是 Unusable；两种都不卸载、不重试（降速重试本身也砍掉了，见 S1）；加 device `test_sd_absent` 计时 | §3.9 |
| B2 | `LfsStore::begin(shards)` 改了签名，而调用方在 M2-B 名下，M2-A 自己的构建验收会编译失败 | `begin()` 签名不变，内部自己找 `cpafs`、算分片数 | §3.3 |
| B3 | "长度为 0 的 `.json` 删除"写成两种存储通用，和 v0.1 测试（移到 bad/）冲突 | 两条新规则只在 `atomic_rename()==false` 时生效；v0.1 测试一行不改，用 `git diff` 守住 | §3.6 |
| B4 | 每次开机第一条保存要建 `<E>`、`<B>` 两个目录，各 70–90ms，超出 150ms | 进一步简化：改成按 epoch 平铺，不再按开机建目录；每张卡每个 epoch 的第一条单独 ≤250ms。没有采用 `prepare_boot`：它还需要空目录清理，而且迁移跨多次开机的条目时每个开机号仍要建目录 | §3.5 |
| B5 | 运行中拔卡会让主循环每次卡 0.6–2s，而不只是保存失败 | SdStore 遇到磁盘级错误就锁死，之后不碰总线；锁死逻辑在纯代码里，用 FakeFatPort 断言"之后调用数为 0"；人工检查按键仍响应 | §3.8 |
| B6 | 文档里的安装流程漏了"先清空待发"，整片刷写 ↔ Launcher 切换会丢内部的待发条目 | 两个方向都加第 0 步；写进 README、G-L6 和 07 | §4.4 |

### 14.2 范围裁剪（全部采纳）

| # | 裁剪 | 说明 |
|---|---|---|
| S1 | 去掉 4MHz 降速重试 | 五档时钟都 5/5；驱动每块校验 CRC16；无卡开机也更快 |
| S2 | FakeStore 的 FAT 模式不模拟两步 rename | 断言 rename 调用为 0；只模拟"目标已存在时失败"和"open 时就有长度为 0 的条目" |
| S3 | 不迁移 `cpafs` 的 bad/ | 它们从不发送，只是证据；少一个分支和一组断电测试 |
| S4 | daemon 的已绑定设备短退避推迟 | 由 G-L9 实测决定；补充一点：现有挂起按序列号记，端口重新枚举不会解除挂起，但如果端口真的消失，链路结束原因不在挂起列表里，也能马上重连 |
| S5 | Unwritable 并入 Unmountable | 合并成 `SdState::Unusable` 和一条提示 |

### 14.3 评审指出的风险

| 风险 | 处理 |
|---|---|
| 1. 迁移跨多次开机时要为每个开机号建目录，可能要 20–25 秒 | 按 epoch 平铺后不存在；设备测试仍用来自 ≥20 个开机号的条目计时 |
| 2. "ENOSPC → 存储已满"做不到（`Runtime` 只在开机读一次剩余空间）；NullStore 会先报 Full | 删掉"存储已满"；`NoStorage` 在 Full 之前判断（M2-B） |
| 3. 只靠 `mkdir` 发现不了只读或写满的卡 | `prepare()` 里加探针写 |
| 4. 目录扇区写一半可能连带毁掉相邻条目，10 次断电证明不了 G4 | R16：G4 改为协议层面的声明；断电结果按低强度证据记录；出现丢失就用 nosd 开关退回内部为主 |
| 5. 性能数据只来自 FatFs 格式化的卡 | R17：在 `diskutil` 格式化的卡上重跑（已获授权） |
| 6. 栈：FatFs 的长文件名缓冲在 loop 栈上 | R20：加 2KB 余量和两条新链；`test_runtime_budget` 跑一次 SD 上的拒收 ack（M2-B） |
| 7. 合并镜像写到 0x0 会清空 NVS | R19；§4.4 修正说法 |
| 8. Launcher 的控制台在启动画面期间就开着，没有闸门覆盖"Mac 连着时开机" | 新增 G-L9 |
| 9. 在 Launcher 里删除旧版本会擦掉共用的 `cpafs` | README 推荐覆盖安装（§4.4） |
| 10. HIL `migrate_check` 的"恰好 N 条"要求 SD 上原本没有待发 | 先跑 `sd_empty`；`migrate_check` 只认种下的那些 id |
| 11. Launcher 自己可能读不了这张卡（R2） | 仍然开放（开放问题 5） |
| 12. `cardputer-adv-test` 的库是显式列出的；要确认链接的是补丁版 `SD` | M2-A 加上 `storage`，并在构建日志里检查 `third_party/SD` |
