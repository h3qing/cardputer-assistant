[English](10-m2-work-packages.md) · **中文**

# 10 · v0.2.0（M2）工作包

> 2026-10-20 由 M2 设计工作流生成，评审后定稿。设计依据见 [09-m2-design.zh-CN.md](09-m2-design.zh-CN.md)，实现 agent 以本文件为任务来源。

> M2-A 和 M2-C 拥有的目录互不重叠，可以并行开发；M2-B 依赖这两个包；M2-D 是真机 HIL，依赖 M2-B 和 M2-C。

## Lead 已定下的默认决定（要求：尽量少问）

- 存储：SD 是主存储，内部的 `cpafs` 作后备；保留 `CPA_STORAGE_NO_SD` 开关作退路。
- 发布文件名：`cpa-<tag>.bin`（合并镜像，在 Launcher 里显示为 cpa-<tag>）和 `cpa-<tag>-app.bin`。
- v0.2 不提供在设备上格式化 SD 的功能。
- Launcher 用用户批准的正式版 **2.9.1**（官方 release 资产，1,422,720 B）；下载后先核对大小，再记录 SHA-256。
- 如果 Launcher 自己的 SD 驱动读不了这张 512GB 卡，就改用 Launcher 的串口安装（`flash overwrite`）；要不要向上游提修复，先问用户。
- 运行中拔卡：v0.2 不自动切换存储，只提示，并且不让主循环被卡住。
- 协议只新增可选字段 `hello.store` 和 `hello.launcher`，`v` 仍为 0。
- SD 上待发的条目是明文，和 v0.1 一样；加密和 PIN 锁推后，写进 SECURITY.md。

## M2-A · 固件存储层：补丁版 SD 驱动、cpafs 分区、lib/storage（SdStore/FatPort/plan/paths）、FAT 提交协议与迁移、hal 挂载与分区检测

- **拥有的路径**：`firmware/third_party/`, `firmware/hwtest/platformio.ini`, `firmware/hwtest/lib-core2/`, `firmware/app/platformio.ini`, `firmware/app/partitions/`, `firmware/app/lib/storage/`, `firmware/app/lib/outbox/`, `firmware/app/lib/hal/`, `firmware/app/test/core/support/fake_store.h`, `firmware/app/test/core/support/fake_fat_port.h`, `firmware/app/test/core/support/stack_gate.py`, `firmware/app/test/core/support/test_stack_gate.py`, `firmware/app/test/core/test_sd_paths/`, `firmware/app/test/core/test_storage_plan/`, `firmware/app/test/core/test_sd_store/`, `firmware/app/test/core/test_store_commit/`, `firmware/app/test/core/test_outbox_fat/`, `firmware/app/test/core/test_outbox_migrate/`, `firmware/app/test/device/test_storage/`, `firmware/app/test/device/test_sd_store/`, `firmware/app/test/device/test_sd_absent/`, `NOTICE`
- **依赖**：无

### 范围

按 docs/09-m2-design.md 实现，要做的事如下。

(1) 搬移补丁版 SD 驱动（前提：Lead 已提交 hwtest 的 SD 工作）。
- 把 firmware/hwtest/lib-core2/SD 原样搬到 firmware/third_party/SD，删除 firmware/hwtest/lib-core2。
- 补丁 1（ACMD41 只带 HCS）和补丁 2（GET_BLOCK_SIZE 4MiB）保留。
- 补丁 3 改为只在定义 CPA_SD_UNCAPPED_CLOCK 时去掉 25MHz 上限；不定义时恢复上游的上限代码。SD/README.md 同步说明。
- hwtest 的 env cardputer-adv（gnu17 继承它）改为 lib_extra_dirs = ../third_party，并加 -DCPA_SD_UNCAPPED_CLOCK；p54 env 不变。
- NOTICE 补上 arduino-esp32 SD 库的 Apache-2.0 条目，写明路径、来源版本和三处补丁。

(2) 分区表和构建配置。
- 新增 partitions/cpa_8MB.csv：default_8MB.csv 把 spiffs 标签改成 cpafs，偏移和大小不变（09 §4.2）。
- platformio.ini：board_build.partitions 指向它；env cardputer-adv 的 lib_extra_dirs 加上 ../third_party（native 不加）；cardputer-adv-test 的 lib_deps 加 storage；新增 env cardputer-adv-test-nosd（继承 cardputer-adv-test，加 -DCPA_STORAGE_NO_SD=1）。

(3) lib/storage（纯代码，不包含 Arduino、M5、esp_、SD、LittleFS 的头文件）。
- storage_plan.{h,cpp}：PartFacts、SdState{Disabled,Absent,Unusable,Ok}、SdFacts{state,size_mib}、Store、StoreNote{None,NoCard,SdUnusable,NoInternal,LegacyTable,NoStorage}、StoragePlan、plan_storage()、lfs_shards()，严格按 09 §3.4 的表。
- sd_paths.{h,cpp}：逻辑路径和物理路径双向映射（09 §3.5）：/outbox/<E>-<B>-<S>.json ↔ <root>/outbox/<E>/<B><S前3>.J<S末1>；bad 同理放在 BAD/<E>/；.tmp 无映射；其他路径前面加 root。
- fat_port.h：FatErr{None,NotFound,Exists,Disk,Other} 与 FatPort 接口（09 §3.3）。
- sd_store.{h,cpp}：SdStore : FileStore，构造参数 FatPort& 和 root（默认 /cpa）。prepare() 建 root、root/outbox 并做探针写（PROBE.BIN 写 1 字节、sync_close、unlink）；open_write 遇到 NotFound 逐级建父目录再试一次；list("/outbox") 报告 bad 目录、逐个列 epoch 目录（最多 8 个，同一时间只开一个目录）并把文件名拼回 <MonoId>.json，其他名字原样报告；rename() 直接返回 false 且不调用 port；atomic_rename() 为 false；第一次得到 FatErr::Disk 就锁死，之后所有方法立即失败、不再调用 port；free_bytes() 正常时 0xFFFFFFFF、锁死后 0；failed() 报告锁死。
- null_store.h：所有操作都失败，free_bytes() 为 0。

(4) lib/outbox。
- file_store.h 新增 virtual bool atomic_rename() const，默认 true。
- 新增 store_commit.{h,cpp}：commit_file 和 move_file（09 §3.6 的表）；FAT 语义下从不调用 rename；move_file 的读缓冲用静态 2KB。
- outbox_io 的 persist、apply 和扫描的 MoveToBad 改为调用它们；保留 outbox_persist / outbox_apply 这两个函数名（scripts/stack_budget.py 的链引用它们）。
- 启动扫描新增两条规则，只在 !fs.atomic_rename() 时生效：长度为 0 的 .json 删除；.tmp 只计数、不删除。LittleFS 上的行为与 v0.1 一字不差。
- 新增 outbox_migrate.{h,cpp}：MigrateResult{moved,failed}、MigrateObserver；对每个 id：读源（静态 2KB）→ commit_file 写目标 → 删源；任何失败保留源并继续；bad/ 不迁；满足 09 §3.7 的不变式。
- 所有函数帧不超过 1536B；大缓冲一律静态存储，并在注释里说明原因（同 outbox_io 的先例）。

(5) 测试支持。
- FakeStore 增加 FAT 模式（set_fat(true)）：atomic_rename() 返回 false；rename 遇到已存在的目标就失败；统计 rename 调用次数。默认模式行为不变，v0.1 测试不受影响。
- 增加一个共享电源对象，让两个 FakeStore 在全局第 k 步同时断电。
- 新增 fake_fat_port.h：内存里的物理文件和目录；可以在第 k 次调用时注入 Disk / NotFound 等错误；按方法统计调用次数。
- stack_gate.py 的 LIBS 加上 storage，并加上纯库里的新链（commit_file、move_file、outbox_migrate、SdStore 的读写）。

(6) hal。
- sd_card.{h,cpp}：SdFacts hal_sd_mount(SdStore&)，严格按 09 §3.9 的挂载步骤：定义 CPA_STORAGE_NO_SD 时报 Disabled、不碰总线；SPI.begin(40,39,14,12)；sdcard_init(12,&SPI,20MHz)；sdcard_mount(pdrv,"/sd",2,false)；失败时读 sdcard_type()：NONE/UNKNOWN 为 Absent，SD/SDHC/MMC 为 Unusable；两种都不调用 sdcard_uninit、不重试；成功时算 size_mib，再调用 store.prepare()，失败报 Unusable。VfsFatPort：POSIX 调用加 /sd 前缀，errno 换成 FatErr（EIO/ENODEV/ENXIO→Disk，ENOENT→NotFound，EEXIST→Exists，其他→Other）。
- lfs_store：标签改成 cpafs；begin() 签名不变（无参数）：找不到 cpafs 就返回 false，不挂载、不格式化；找到就按 lfs_shards(size) 定分片数，挂不上时只格式化 cpafs 本身；分片数为 4 时用 MonoId 末位的 Crockford 值模 4 得到 _0–_3，为 32 时保持 v0.1 的映射；永远不碰 spiffs。
- part_facts.{h,cpp}：按 09 §4.3，只收集事实，不做判断。

(7) 设备测试：test_sd_store（root 用 /cpatest，测完删掉）、test_sd_absent（拔卡人工执行）、test_storage 更新，内容见 09 §9.2。

不做：src/main.cpp、Runtime、shell、界面、协议、daemon、scripts/、CI 和发布、README、在设备上格式化 SD、运行中切换存储、v0.3 录音；不改 v0.1 的 test/core/test_outbox_boot_scan 和 test/core/test_outbox_core。

### 验收（先写测试）

- (RED 先行) cd firmware/app && pio test -e native -f 'core/test_sd_paths' -f 'core/test_storage_plan' -f 'core/test_sd_store' -f 'core/test_store_commit' -f 'core/test_outbox_fat' -f 'core/test_outbox_migrate'   # FAT 模式每一步注入断电 0 丢失；FAT 上 rename 调用 0 次；长度为 0 的 .json 只在 FAT 上删除；SdStore 遇到 Disk 错误后对 FatPort 的调用数为 0；迁移在两边任意一步断电后重跑都收敛、源被清空、源的 bad/ 不动
- cd firmware/app && pio test -e native && git diff --quiet HEAD -- test/core/test_outbox_boot_scan test/core/test_outbox_core   # v0.1 的全部 core/ui 套件通过，v0.1 的 outbox 测试一行不改（G6）
- cd firmware/app && uvx gcovr -r . --filter 'lib/|modules/' --exclude 'lib/(hal|usb_transport)/' --fail-under-line 80
- cd firmware/app && ! grep -rEn '#include\s*[<"](Arduino([^J]|$)|M5|esp_|SD\.h|sd_diskio|FS\.h|LittleFS)' lib/storage lib/outbox
- cd firmware/app && pio run -e cardputer-adv && python3 scripts/check_budget.py .pio/build/cardputer-adv   # 链接后的栈闸门（stack_gate 的 LIBS 已含 storage）也要通过
- cd firmware/app && pio run -e cardputer-adv-test && pio run -e cardputer-adv-test-nosd && pio run -e cardputer-adv-test -v 2>&1 | grep -q 'third_party/SD'   # 链接的是补丁版 SD，不是框架自带的
- cd firmware/app && python3 ~/.platformio/packages/framework-arduinoespressif32/tools/gen_esp32part.py .pio/build/cardputer-adv/partitions.bin > $TMPDIR/parts.csv && grep -E '^cpafs,data,spiffs,0x670000,' $TMPDIR/parts.csv && ! grep -E '^spiffs,' $TMPDIR/parts.csv
- cd firmware/hwtest && ~/.local/bin/pio run -e cardputer-adv -e cardputer-adv-gnu17 -e cardputer-adv-p54 && test ! -d lib-core2
- python3 tools/repo/check_sizes.py --warn 400 --fail 800 firmware/app/lib firmware/app/test && uvx lizard -l cpp -L 50 -w firmware/app/lib/storage firmware/app/lib/outbox firmware/app/lib/hal
- grep -q 'Apache' NOTICE && grep -q 'third_party/SD' NOTICE
- (接真机并插卡，先执行 cpa pause 900，确认 lsof /dev/cu.usbmodem* 为空) cd firmware/app && pio test -e cardputer-adv-test -f 'device/test_sd_store' -f 'device/test_storage'   # 20MHz 挂载加 prepare ≤200ms；200 条保存 p95 ≤100ms、最长 ≤150ms（积压与立即 ack 两种），新 epoch 目录的第一条 ≤250ms；挂载增加的堆 ≤16KB；LFS→SD 迁移 50 条（≥20 个开机号）0 丢失并计时；cpafs 1.5MB 用 32 个分片；part facts 报 launcher=false
- (人工拔出 SD 卡后执行，cpa pause 600) cd firmware/app && pio test -e cardputer-adv-test -f 'device/test_sd_absent'   # hal_sd_mount 返回 Absent，耗时 ≤500ms；超了先记录 MISO 空闲电平，再试 GPIO39 内部上拉

## M2-C · 协议与 daemon：hello.store / hello.launcher、cpa status 显示、主机行守护测试

- **拥有的路径**：`protocol/`, `firmware/app/lib/protocol/`, `firmware/app/test/core/test_codec_vectors/`, `firmware/app/test/core/test_codec_unit/`, `daemon/`
- **依赖**：无

### 范围

按 docs/09-m2-design.md §6、§7 实现，要做的事如下。

(1) protocol/。
- hello.schema.json 的 device 部分加两个可选属性：store（枚举 sd、int、none）和 launcher（boolean）。只新增，v 仍为 0（05 §6.9）。不加 additionalProperties: false。
- vectors/v0/messages/hello.json 加用例：带这两个字段的合法；v0.1 那样不带的合法；store:"fat" 非法；launcher:"yes" 非法。
- protocol/README 的契约补充里写明这两个字段，并写明主机发出的行只能以 '{' 开头或为空行（为了 Launcher 的串口控制台）。
- check_vectors 的覆盖面检查要通过。

(2) 固件 lib/protocol。
- DeviceHello 加 Opt<StoreKind> store 和 Opt<bool> launcher，默认都不设置，所以其他代码不改也能照常编译。
- encode 只在字段设置了时才输出（不设置时和 v0.1 逐字节相同）；decode_device 解析它们，并按 schema 校验。
- 契约向量测试两端一致。

(3) daemon。
- protocol/specs.py 的 hello device 部分加两个可选字段；messages.DeviceHello 加 store 和 launcher，默认 None。
- app.status 的 device 里加上 store 和 launcher；cli 的状态输出打印 store=… launcher=…，没有时显示 '-'。
- 加一个守护测试：所有主机主动发出的消息序列化后都以 '{' 开头，nudge 是空行，不会出现以字母开头的行。
- 运行时依赖仍然只有 pyserial。

(4) 条件项，默认不做：只有 M2-D 报告 G-L9 不通过时，由 Lead 另行派发——已绑定序列号的端口在 no_reply 时只挂起 3 秒（新的 Timing 字段），foreign、version、open_failed、internal_error 和未绑定端口仍为 30 秒，并加 FakeDevice 先输出 Launcher 风格噪声再变成 cpa 设备的集成测试。

不做：新增消息类型、outbox 格式变化、debug.state 变化、Launcher 串口命令、录音相关。

### 验收（先写测试）

- (RED 先行) uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/tools -q && uv run --with 'jsonschema>=4.23,<5' python protocol/tools/check_vectors.py
- cd firmware/app && pio test -e native -f 'core/test_codec_vectors' -f 'core/test_codec_unit'   # 新的 hello 向量两端解码一致；不设 store/launcher 时编码结果和 v0.1 逐字节相同
- (RED 先行) cd daemon && uv run pytest -q tests/unit/test_hello_store_launcher.py tests/unit/test_status_store.py tests/unit/test_host_lines_never_start_with_word.py   # v0.1 设备（没有这两个字段）在 status 里显示 '-'；所有主机主动发出的行都以 '{' 开头或为空行
- cd daemon && uv run pytest --cov=cpad --cov-fail-under=80 && uv run ruff check . && uv run ruff format --check .
- cd daemon && uv tree --no-dev --depth 1 | grep -vE '^(cpad|└──|├──) ?(pyserial)?' | grep -c . | grep -qx 0
- python3 tools/repo/check_sizes.py --warn 400 --fail 800 daemon/src daemon/tests protocol firmware/app/lib/protocol && uvx lizard -l python -L 50 -w daemon/src && uvx lizard -l cpp -L 50 -w firmware/app/lib/protocol

## M2-B · 固件集成、界面与 Launcher 发布：开机选存储与迁移、NoStorage、系统页、预算与分区脚本、发布产物、README

- **拥有的路径**：`firmware/app/src/`, `firmware/app/lib/shell/`, `firmware/app/lib/module_api/`, `firmware/app/modules/system/`, `firmware/app/test/ui/`, `firmware/app/test/device/test_runtime_budget/`, `firmware/app/scripts/`, `firmware/app/.clang-tidy`, `.github/workflows/`, `README.md`
- **依赖**：M2-A, M2-C

### 范围

按 docs/09-m2-design.md 实现，要做的事如下。

(1) src/main.cpp 的开机顺序，按 09 §3.1。
- 依次调用 hal_part_facts、hal_sd_mount(g_sd)、g_lfs.begin()、plan_storage。
- 按计划选出 FileStore（SdStore、LfsStore 或 NullStore）；Runtime 在 setup() 里用函数内 static 构造（不用堆），指针交给 loop()。
- SD 为主并且迁移完成后执行 g_lfs.end()。
- 迁移观察者每条 kick 一次 USB，迁移超过 0.5 秒时显示"正在迁移到 SD 卡 i/n"。

(2) lib/shell 的 Runtime。
- begin(boot, now, const StorageBoot& = {})：有迁移源时先 outbox_boot_scan(源)、再 outbox_migrate、再扫描主存储；结果写进 SysInfo。默认参数保证 v0.1 的 ui 测试不用改。
- SaveResult 新增 NoStorage，在判断 Full 之前判断（主存储为 None 时不碰存储）。
- save_projects_cache 改用 commit_file，FAT 上不调用 rename。
- fill_reply_body 填 hello 的 store 和 launcher。
- 保存、outbox_apply 或项目缓存写入失败时，存储错误计数加 1。

(3) lib/module_api 的 SysInfo 加字段：store、sd 状态、sd 容量、launcher、cpafs 大小、note、migrated、migrate_failed、store_errors。

(4) modules/system 的系统页：按 09 §8 加上启动方式和存储两行、每种 StoreNote 的文字、迁移和存储错误计数；行数上限相应调整。

(5) scripts（都先在 test_scripts.py 里写测试）。
- check_budget.py：app 超过 1,835,008B 时打印 WARN，但退出码仍为 0；超过 2.5MB 仍然失败。
- 新增 make_image.py：把 esptool merge_bin 从 release.yml 挪过来，按 env 和 tag 生成 cpa-<tag>.bin（合并镜像）和 cpa-<tag>-app.bin。
- 新增 check_partitions.py：解码 partitions.bin，断言 cpafs 在 0x670000、大小 0x180000，没有 spiffs 标签，没有 test 子类型的 app。
- stack_budget.py：APP_DIRS 覆盖新的 hal 文件；经过 SdStore 的链另加 2048B 的 FatFs 余量；新增两条链：收到 ack → apply_outbox → outbox_apply → move_file → SdStore，以及 setup → Runtime::begin → outbox_migrate → commit_file → SdStore。

(6) .github/workflows。
- ci.yml 的纯代码 grep 加上 lib/storage，并增加 check_partitions 这一步。
- release.yml 改用 make_image.py，产物为 cpa-<tag>.bin、cpa-<tag>-app.bin 和 SHA256SUMS。

(7) README（09 §4.4）。
- 两条安装路径：整片刷写；Launcher（beta 版本、文件放在 /cpa/fw/）。两个方向都先做第 0 步：清空内部待发（↑N 为 0，或插卡开机一次让迁移清空 cpafs，并在系统页确认）。
- SD 准备：FAT32 + MBR，diskutil eraseDisk 命令，先用 diskutil list 把盘号核对两遍。
- Launcher 下升级：推荐覆盖安装（Use <标签> partition 或 flash overwrite），或删除后立刻重装；不要删了旧版本又留着别的版本不管。
- 从 v0.1 升级：pio run -t upload 保留 NVS；把 cpa-<tag>.bin 写到 0x0 会清空 NVS、epoch 重新生成（安全）；只写 -app.bin 时系统页会提示旧分区表。
- 开机按 G0 会进 Launcher 菜单；冷启动多约 5 秒；↑N 不为 0 时不要拔卡；运行中拔卡后要重启。

(8) device/test_runtime_budget：改为挂载 SD、SD 为主存储；工作负载里包含一次被拒收的 ack（在 SD 上走 move_file）。

可以先开工的部分：(5)、(6)、(7)，以及 (4) 的纯逻辑。这些按 09 §3.4、§6 定下的接口就能写，不必等 A、C 完成；接线部分 (1)–(3)、(8) 要等 A、C 合入。

不做：设备上格式化 SD、回到 Launcher 的菜单项、深睡、运行中切换存储、"存储已满"提示、录音。

### 验收（先写测试）

- (RED 先行) cd firmware/app && pio test -e native -f 'ui/test_mod_system' -f 'ui/test_runtime_storage'   # 每种 StoreNote 的系统页文字；主存储为 None 时保存返回 NoStorage（先于 Full，且对存储零调用）；有迁移源时先迁移、后扫描；hello 带 store 和 launcher；FAT 上的项目缓存 rename 调用 0 次；写入失败时 store_errors 加 1
- cd firmware/app && pio test -e native && uvx gcovr -r . --filter 'lib/|modules/' --exclude 'lib/(hal|usb_transport)/' --fail-under-line 80
- (RED 先行) cd firmware/app && uv run --no-project --with pytest pytest scripts test/core/support -q   # 软预算：1,835,009B 时打印 WARN、退出码 0；2.5MB+1 时失败；make_image 的参数和文件名；check_partitions 遇到 spiffs 标签或 test 分区时失败；stack_budget 的 SD 链带 2048B FatFs 余量
- cd firmware/app && pio run -e cardputer-adv && python3 scripts/check_budget.py .pio/build/cardputer-adv && python3 scripts/check_partitions.py .pio/build/cardputer-adv/partitions.bin
- cd firmware/app && python3 scripts/make_image.py --env cardputer-adv --tag v0.2.0-dev --out $TMPDIR/dist && test -f $TMPDIR/dist/cpa-v0.2.0-dev.bin && test -f $TMPDIR/dist/cpa-v0.2.0-dev-app.bin
- cd firmware/app && pio run -e cardputer-adv-test && pio run -e cardputer-adv && ! grep -a -q 'debug.key' .pio/build/cardputer-adv/firmware.bin
- cd firmware/app && pio check -e cardputer-adv --skip-packages --fail-on-defect high
- cd firmware/app && ! grep -rEn '#include\s*[<"](Arduino([^J]|$)|M5|esp_)' lib/shell lib/module_api modules
- python3 tools/repo/check_sizes.py --warn 400 --fail 800 firmware/app/lib firmware/app/test firmware/app/modules firmware/app/src && uvx lizard -l cpp -L 50 -w firmware/app/lib/shell firmware/app/modules firmware/app/src
- grep -q 'diskutil list' README.md && grep -q 'flash overwrite' README.md
- (接真机并插卡，先执行 cpa pause 600) cd firmware/app && pio test -e cardputer-adv-test -f 'device/test_runtime_budget'   # SD 已挂载且为主存储、含一次 SD 上的拒收 ack：空闲堆 ≥150KB、最大连续块 ≥64KB、栈余量 ≥4KB

## M2-D · HIL：SD、迁移与 Launcher 场景；07 人工检查单（G-L1–G-L9、SD 项）

- **拥有的路径**：`tools/hil/`, `docs/07-hil-checklist.md`
- **依赖**：M2-B, M2-C

### 范围

按 docs/09-m2-design.md §9.3、§9.4 实现，要做的事如下。

(1) tools/hil/scenarios/storage.py。
- sd_empty：断言 hello.store=='sd'，连上后等到 pending==0（迁移场景的前置条件）。
- sd_store：断言 hello.store=='sd'；离线记 3 条 → debug.reboot → 每条恰好送达一次。
- sd_bulk：离线记 100 条，恢复后 5 秒内全部送达，没有重复。
- migrate_seed、migrate_check、migrate_drained：三个阶段之间靠 --state FILE 传递种下的 id。
  - migrate_seed：在 nosd 固件上，确认 store=='int'，种下 N 条，保持离线、不送达。
  - migrate_check：在 test 固件上，确认 store=='sd'，种下的每个 id 恰好送达一次，且没有别的 id。
  - migrate_drained：换回 nosd 固件后，确认 pending==0。

(2) tools/hil/scenarios/launcher.py。
- launcher_boot：配合 --expect-launcher 使用，断言 hello.launcher==true、rst=='software'。

(3) harness 和 runner。
- 新增 --state 和 --expect-launcher 两个参数。
- 新增读取 hello 新字段的辅助函数，并把场景注册进 SCENARIOS。
- 辅助逻辑（读写 state 文件、期望值判断、场景选择）先写单元测试。

(4) docs/07-hil-checklist.md。
- 加上 G-L1–G-L4、G-L6–G-L8（照 launcher.md §8；G-L6 按 09 §4.4 的第 0 步和覆盖安装）；G-L5 标为"v0.2 不适用"。
- 新增 G-L9：Mac 连着、daemon 在跑，在 Launcher 下开机 10 次，每次 12 秒内 link ready，记下是否出现 30 秒挂起。
- SD 人工项：10 次断电（按低强度证据记录）；运行中拔卡（最多卡一次 ≤2s，之后按键正常，重启恢复）；不插卡开机跑 device test_sd_absent；exFAT 卡的提示；用 diskutil 把开发卡重新格式化后重跑 device test_sd_store（用户已授权清空这张卡；盘号核对两遍）；2–3 张其他卡；Mac 急救检查丢失簇。
- Launcher 下的手动安装步骤，以及用 make_image.py 打测试版合并镜像的命令。
- 每一项都要有日期和结果栏。

(5) 用 nosd 构建回归 v0.1 的全部场景。

(6) 把 G-L9 的结果报告给 Lead；不通过时，由 Lead 派发 M2-C 的条件项（已绑定设备 3 秒退避）。

不做：在 harness 里自动刷固件、自动拔插卡、把 HIL 放进 CI、改 daemon 或固件代码（发现的问题按归属交回 A、B、C 修）。

### 验收（先写测试）

- (RED 先行) uv run --project daemon pytest tools/hil/tests -q
- python3 tools/repo/check_sizes.py --warn 400 --fail 800 tools/hil && uvx lizard -l python -L 50 -w tools/hil
- (接真机并插卡，cpa pause 3600，已刷 cardputer-adv-test) uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,sd_store,sd_bulk,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk --report $TMPDIR/hil-sd.json   # 退出码 0；hello.store=='sd'
- (已刷 cardputer-adv-test-nosd，卡仍插着) uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk --report $TMPDIR/hil-nosd.json   # v0.1 行为不变，hello.store=='int'
- (迁移，依次执行) cd firmware/app && pio run -e cardputer-adv-test -t upload && cd ../.. && uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty --report $TMPDIR/m0.json && (cd firmware/app && pio run -e cardputer-adv-test-nosd -t upload) && uv run --project daemon python tools/hil/run_e2e.py --scenarios migrate_seed --state $TMPDIR/mig.json --report $TMPDIR/m1.json && (cd firmware/app && pio run -e cardputer-adv-test -t upload) && uv run --project daemon python tools/hil/run_e2e.py --scenarios migrate_check --state $TMPDIR/mig.json --report $TMPDIR/m2.json && (cd firmware/app && pio run -e cardputer-adv-test-nosd -t upload) && uv run --project daemon python tools/hil/run_e2e.py --scenarios migrate_drained --state $TMPDIR/mig.json --report $TMPDIR/m3.json
- (人工装好 Launcher，并从 SD 安装 make_image.py 打出的测试版合并镜像) uv run --project daemon python tools/hil/run_e2e.py --scenarios launcher_boot,handshake,capture,reboot --expect-launcher --report $TMPDIR/hil-launcher.json
- grep -q 'G-L9' docs/07-hil-checklist.md && grep -q 'diskutil' docs/07-hil-checklist.md && grep -q 'test_sd_absent' docs/07-hil-checklist.md

## 开放问题（设计时提出，按上面的默认值推进）

- 存储的主次：设计默认把 SD 作为 outbox 的主存储，理由有三：Launcher 下 cpafs 只有 448KB；SD 用 8.3 布局时保存 p95 约 29ms；还顺带修掉 v0.1 积压 200 条时的慢。代价是 FAT 断电只能在协议层面证明 0 丢失，人工 10 次断电是低强度证据。如果断电闸门出现任何丢失，发布版可以用现成的 CPA_STORAGE_NO_SD 开关退回"内部为主"。sd-card.md 原来建议 v0.2 的 outbox 留在 LittleFS。要不要按默认（SD 为主）走？
- 发布文件名：默认改成 cpa-<tag>.bin（合并镜像，Launcher 里显示为 cpa-v0.2.0）和 cpa-<tag>-app.bin，不再用 v0.1 的 cpa-fw-…-full.bin 命名。可以吗？
- 在设备上格式化 SD：默认 v0.2 不做。这张开发卡已经是 FAT32，你也授权了可以随时清空、重新格式化；新买的 32GB 以上的卡出厂是 exFAT，要先在 Mac 上执行一次 diskutil eraseDisk FAT32 CPA MBRFormat /dev/diskN。需不需要 v0.2 就在设备上提供格式化？那需要两次确认，主循环会阻塞约 1 分钟（512GB 卡实测 44 秒），完成后自动重启。
- Launcher 版本：HIL 和 README 锁定 beta（2026-10-15，构建自 e8cc4df，带 macOS U 盘修复和 DDLB），还是正式版 2.9.1？beta 的下载地址会被新构建覆盖，所以要自己存一份固定的副本。
- Launcher 自己的 SD 驱动很可能也读不了这张 512GB 卡（同样的 ACMD41 问题）。如果 G-L1 失败，选哪条路：换一张 32GB 以下的卡来安装，用 Launcher 的串口 flasher，还是给 bmorcelli/Launcher 和 arduino-esp32 上游提修复？向上游公开提交需要你同意。
- 运行中拔卡：v0.2 不自动切换存储。拔卡后发现错误的那一次操作会卡 0.6–2 秒，之后不再碰卡；重启之前保存都会失败（会有提示），卡上的待发条目等下次插卡开机后再发送。可以接受吗？
- 需要你动手的事，什么时候能安排：(a) 提供 2–3 张别的卡，至少一张是 32GB 以下、出厂就是 FAT32 的；(b) 人工断电 10 次（拨开关并拔线）；(c) 拔卡开机一次，测无卡开机耗时；运行中拔卡一次，看按键是否仍响应；(d) 把开发卡放进 Mac 读卡器，用 diskutil 重新格式化后重跑设备测试（清空已获你授权）。你手边有读卡器吗？(e) 为 G-L 闸门装 Launcher（网页刷写器，或由 agent 用 esptool 写）。
- 协议是否加 hello.store 和 hello.launcher 两个可选字段（只新增，v 不变，v0.1 daemon 会忽略它们）？默认加，用来让 HIL 确认场景确实走了 SD，以及让 cpa status 显示保存失败的原因。
- PRD 原来写在 v0.2 的"SD 卡历史归档 + Mac 公钥加密封装"和"PIN 锁"推后。在此之前，SD 上待发的条目是明文，和 v0.1 内部 Flash 里的一样，只是卡可以被直接拔走读取。可以接受吗？
