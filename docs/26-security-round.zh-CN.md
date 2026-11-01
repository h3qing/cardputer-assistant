[English](26-security-round.md) · **中文**

# 26 · 安全测试轮和落盘加密：设备上逐个文件封给 Mac，Mac 上整盘加密、文件照样开放

> 状态：**设计稿，已按三份评审改过（§9），2026-10-22 又和 docs/27 交叉核对、补了几处空白（§9 末行），待你拍板（§8.2）** · 日期：2026-10-22 · 角色：安全（落盘数据、密钥、安全测试轮）· 基于 origin/main `995821d`（代码和 `d330465` 一样，只多了 docs/25）· 关联 docs/18 P8-E 和 §8、docs/05 §8、SECURITY.md、ADR 0001、ADR 0004；docs/24（`wt-mac`）、docs/25、docs/27（新 Mac 初始化，PR #67）。
> 需求：把安全测试定成一个里程碑，跑一轮，确保落盘的数据都是文件加密的；同一台设备配几台 Mac，新 Mac 的初始化要顺，Mac 和设备要能互相认出来。
> 依据：SECURITY.md；docs/04 S11、D16；docs/05 §8；docs/07 §6.7；docs/11 §4.4–§4.6；docs/12 G-A6、G-A8；docs/13 v0.5；docs/15 §2、§3.7、§3.9、§5；docs/18 §3.2、§4、§5.2–§5.3、§8、§9、§11、§13、§15；docs/21 §1、§5.6；docs/22 §4.5、§5.6；docs/23 §6；docs/24 §7、D12（`wt-mac`）；docs/25 §2.3；docs/27 §3.5（PR #67）；docs/gates/launcher.md；ADR 0001、0004；`protocol/README.md`；录音契约 v1；固件 `lib/{hal,rec,outbox,storage,shell,protocol}/`、`src/main.cpp`、`platformio.ini`、`partitions/cpa_8MB.csv`；daemon 的 `service/`、`config/`、`logging/`、`events/`、`recordings/`、`agents/claude_code/hook_settings.py`、`cpa_stt/{runner,service}.py`；Arduino 2.0.17 SDK 的头文件和静态库（`nm`）。
> 标记：**[源]** 读过的代码或文档 · **[测]** 本机只读实测 · **[算]** 推算 · **[推]** 推断 · **[设]** 设计假设，可以改 · **[待]** 要用闸门、测试或真机验证 · **👤** 需要你。
> 范围：只写设计。没有接设备、没有开串口、没有改代码、没有改系统设置；`backups/` 没碰；`~/CardputerNotes` 只看了文件名和权限，没读内容。
> 路径缩写：`fw/` = `firmware/app/lib/`；`cpad/` = `daemon/src/cpad/`；`stt/` = `daemon/src/cpa_stt/`；`SDK/` = `~/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32s3/`。

## 0. 一页摘要

1. **你要的**：两件事。一是把安全测试定成一个里程碑（M-SEC），跑一轮，证明数据落盘都是加密的；二是同一台设备配几台 Mac，新 Mac 的初始化要顺。第二件的流程在 docs/27，本文只管其中跟密钥有关的部分（§5.5）。
2. **今天**：设备上没同步的东西全是明文：笔记、排队项、录音、项目名缓存。"同步后删除"只是 `unlink`，删掉的东西还能从卡和 Flash 上捞回来；崩溃时 coredump 分区会留一份任务栈；Launcher 的 `/bkp/` 里可能还有一份 cpafs 的备份 [推]。Mac 上 FileVault 已开，归档目录 0700、文件 0600，只有 cpad 和 cpa-stt 的 launchd 日志是 0644（§2）。
3. **设备上的密钥护不住数据**：没有 Secure Boot；任何 USB 主机不用按键就能读写整片 Flash，还能经内置的 USB-JTAG 直接读运行中的内存。所以放在 NVS 里的对称密钥等于没有。Flash 加密、NVS 加密要烧 eFuse，会弄坏 Launcher，而且 IDF 4.4 的库里根本没编进去（§4.2）。
4. **推荐 SEAL（封给 Mac 的公钥）**：设备每写一个文件都用一把新的随机密钥加密，这把密钥再用每台收件 Mac 的公钥（X25519）各包一份。设备只存公钥，**设备上没有任何能解开自己文件的东西**。收件人是你在设备上点过头的 Mac，分不分档案都一样；Mac 每次握手都要证明自己有私钥，证明了才交封好的、才许删封好的；钉住了钥匙的 Mac 证明之前什么都不给。丢设备、拔卡、被人读一次 Flash、删后的残留，拿到的都是密文。这就是 docs/18 的 P8-E（§4.3）。
5. **挡不住的要说清楚**：插着的主机如果存心改设备（往收件人表里加自己、刷固件、接 JTAG），以后记的东西它照样拿得到。SEAL 保护的是"被动读一次"；对付存心的主机，只能"去不信任的电脑前先同步清零，只充电时用数据阻断器"（§3.1 T-USB、§4.4）。
6. **代价**：设备看不到自己存下的正文，v0.5 的设备回放有三条路要你选（👤 §8.2 第 1 条）；要先过闸门 G-ENC（CPU、堆、静态内存、随机数、断电恢复）；Mac 端要能解密，而 Python 标准库没有 X25519 和 AEAD（§4.3、§4.5）。
7. **Mac 端解密**：一个小的 Swift 命令行工具 `cpa-key`，只用 Apple 自带的 CryptoKit，零第三方代码，和 Mac app 在同一个 SwiftPM 包里造；daemon 仍只依赖 `pyserial`，ADR 0004 第 1 条不改。私钥是 `$CPA_HOME/keys/seal/` 下的 0600 文件，**不放钥匙串**：`cpa-key` 每次重新编译签名都会变，钥匙串会在后台同步时突然弹框要登录密码（§5.2）。Mac 一时解不开（工具坏了、超时），先把密文存进 `$CPA_HOME/sealed/` 再回 ack，设备的待传不会卡住（§4.3）。
8. **Mac 上的归档不逐个加密**：逐个加密会让 Obsidian、grep、WhisperWoof、QuickLook 都读不了（违背 ADR 0004 的"开放文件"）；AI 客户端经 cpa/MCP 读时照样要解密，所以也挡不住它们。默认：FileVault（已开）+ 加密的 Time Machine（👤 你看一眼）+ §4.6 的几处小修。加密磁盘映像只写成可选做法，不做成功能。
9. **所以"所有数据都文件加密"老实说是**：设备上逐个文件封装；Mac 的磁盘和备份整体加密；Mac 上的文件对你自己的 app 照样开放，这是有意的。
10. **马上能做、不用等决定的**（SEC-0）：cpad 和 cpa-stt 的 launchd 日志预先建成 0600，启动时把 `logs/` 下的都改成 0600；cpa-stt 启动时清掉上次崩溃留下的临时 WAV；`cpad.log` 里的正文哈希改成带本机盐的 HMAC（字段改名 `h8`，事件里的 `sha8` 不动）；`hook-backups` 每个目标留第一份和最近 2 份；改正 SECURITY.md 里"删了就没了"之类的说法（§3.3、§4.6）。
11. **安全测试轮 M-SEC**（§6）：自动的（模糊测试加 ASan/UBSan、权限遍历、金丝雀泄漏、gitleaks）；模拟器的（敌意 SD 卡、冒充设备、冒充 Mac）；真机的；Mac 的（`cpa doctor security`）。**跑两轮**：现在的基线轮只跑自动的、模拟器的和真机上的 coredump 擦除（顺便量整个 app 录音时的堆），不花你的时间去证明"今天是明文"这件已知的事；SEAL 做完的加密轮全跑，读 Flash、镜像 SD 卡这些合成一个脚本，只要一次 sudo。
12. **分期**（§7）：SEC-0 到 SEC-3 约 780 行代码、1,850 行测试，不等任何决定。SEC-4 到 SEC-6 约 2,870 / 3,200，排在 docs/18 v0.8 的绑定和 docs/24 的 Mac app 包之后。
13. **23 条决定**都有默认值（§8.1），**要你拍板的有 5 件**（§8.2）。三份评审的每一条怎么处理的，见 §9。

## 1. 你要的，和今天差在哪

### 1.1 需求拆开

- 安全测试是有清单、有通过标准的里程碑（§6 M-SEC）。
- 不是一次性审查：先跑基线一轮，加密后再跑一轮，之后每次发版都跑自动的部分（§6、§7）。
- 每个落盘的位置都有答案：逐个加密、整盘加密，或者写明为什么不加（§2、§4.1）。
- 一台设备、几台 Mac，每台 Mac 有自己的身份和密钥（§5；docs/18）。
- 新 Mac 的初始化（docs/27；本文 §5.5 只管密钥）。
- Mac 认出这是你的设备，设备认出这是你绑定过的 Mac（§3.1 T-ROGUE、§4.3 握手证明、§5.5；docs/18 §4）。

### 1.2 "文件加密"能做到哪一步

- 加密只防"拿到了存储介质、但不是你的人"，不防"以你的身份在跑的程序"：cpad 和 MCP 必须读得到，本机任何程序也就能经它们读到。SECURITY.md:62 本来就不防同一用户下的恶意程序。
- 设备和 Mac 的处境正好相反。设备容易丢，容易被插到别人的电脑上，也没有 Secure Boot；Mac 有 FileVault 和登录密码。所以设备上逐个文件加密，Mac 上整盘加密。
- 加密只防**被动**地读：插着设备的主机如果存心改它（改收件人表、刷固件），以后的记录它就能拿到明文（§3.1 T-USB）。
- 加密不防伪造：拿到 SD 卡的人可以往里塞假的条目和录音，设备会当成自己记的发出去（§3.1 T-CARD、D10）。

## 2. 落盘地图

### 2.1 设备

| 位置 | 内容 | 今天 | 谁读得到 | 依据 |
|---|---|---|---|---|
| NVS 0x9000（20 KiB），命名空间 `cpa` | 开机计数 `epoch`、`boot`。计划中：语言、绑定（`host_id` 哈希 16 B）、档案、`host_label` ≤24 B ×8、`PROFON`；SEAL 的收件人表（§4.3，约 470 B）、`SEALON` | 明文。IDF 4.4 不开 Flash 加密就加密不了 NVS | 插着的任何 USB 主机：不用按键就能 `esptool read_flash`，也能写。Launcher 装的其他固件：NVS 只有一份，Launcher 的 `l_wifi` 也在里面 | [源 fw/hal/nvs_boot.cpp:26-43；docs/gates/launcher.md:50、114；docs/18 §3.2、§11] |
| 内部 LittleFS `cpafs` 0x670000（1.5 MiB），没插卡时用 | `/outbox/_shard/<MonoId>.json`：文件内容就是线上那一行，含正文。`/outbox/bad/`：最近 20 条被拒的。`/cfg/projects.json` | 明文。删除就是 `unlink`；LittleFS 写时复制，旧块还在 | 拿到设备的人 | [源 fw/hal/lfs_store.cpp:108；fw/outbox/outbox_io.h:1-7；fw/shell/runtime_outbox.cpp:15-17；partitions/cpa_8MB.csv] |
| coredump 0x7F0000（64 KiB）；Launcher 下在 0x160000，和别的固件共用 | panic 时写进所有任务的栈：16 KB 的循环栈（正在打的字、解码后的主机消息）、音频栈 | 明文，留到下一次 panic 覆盖为止；cpa 从不读也不擦 | 拿到设备的人 | [源 SDK/qio_qspi/include/sdkconfig.h:424；docs/gates/launcher.md:217 写的"无害"，从落盘角度看不对] |
| SD `/cpa/outbox/…`、`/cpa/outbox/BAD/…` | 条目 JSON：正文 ≤500 B、项目、意图 | 明文 FAT。收到 ack 就 `unlink`；被拒的先复制再删，残留更多 | 拿到卡的人（任何系统都能读）；Launcher 的 SD 管理和 WebUI | [源 fw/storage/sd_paths.h:1-15；fw/hal/sd_card.cpp:202-204；SECURITY.md:41] |
| SD `/cpa/rec/<E7>/*.W?` | 16 kHz 单声道 PCM，44 B 头，5 分钟 9.6 MB | 明文。`file.del` 就是 `unlink`，已拉走的录音能从空闲簇里恢复 | 同上 | [源 docs/11 §4.4；fw/rec/rec_engine.cpp:251；fw/hal/vfs_rec_file_port.cpp:125-127] |
| SD `/cpa/rec/<E7>/*.M?` | ≤512 B JSON：id、段号、模式、字节数、明文的 `sha256`、时间、项目、星标、`consent` | 明文；`sha256` 不带密钥，防不了有人改卡 | 同上 | [源 docs/11:220-236；fw/rec/rec_meta.h:17] |
| SD `/cpa/cfg/projects.json`（v0.8 以后每个档案一份） | 项目 key 和名字 | 明文 | 同上 | [源 fw/shell/runtime_outbox.cpp:15-17；docs/18 §11] |
| SD `/bkp/`（Launcher 的） | Launcher 的备份目录（`backupData.json` 和 `{appNum}-{appName}/`）；在 Launcher 里删掉 cpa 时做的备份，可能带一份 cpafs [推] | 明文 FAT；cpa 不管 | 拿到卡的人 | [源 docs/gates/launcher.md:138] |
| 只在内存里 | 录音索引、outbox 索引、session、队列、草稿、24 KB 麦克风环形缓冲 | 断电就没了；开着时插着的主机能经 USB-JTAG 读，不用复位：固件用的是内置的 USB 串口/JTAG（`ARDUINO_USB_MODE=1`），`DIS_USB_JTAG` 没烧 | 插着线的主机 | [源 firmware/app/platformio.ini:36] |

### 2.2 Mac

| 位置 | 内容 | 今天 | 谁读得到 | 依据 |
|---|---|---|---|---|
| `~/CardputerNotes/YYYY-MM-DD.md` | 打字的笔记、便条、待办 | 0700/0600，FileVault 已开 [测] | 以你身份跑的任何程序（这两个目录不受 TCC 保护，不弹框）；管理员；Spotlight 查询；Time Machine 盘 | docs/15 §2.1 |
| `recordings/<日期>/<HHMM>-<session>/` | `seg-NNN.wav`（归档并转写后 30 天删）、`manifest.json`；转写程序写的 `status.json`、`transcript.*`、`summary.md`、`result.json` | 同上 | 同上 | [源 cpad/recordings/retention.py:1、27]；录音契约 v1 |
| `$CPA_HOME/events.jsonl` | **每条笔记和排队项的正文，永久保留**；`rec.hidden` 只藏不删 | 0600 [测] | 同上；按文件名在 Spotlight 里查得到 [测] | [源 cpad/events/records.py:23-80]；docs/23 §6.1 |
| `config.json`、`projects.json` | claude 路径、笔记目录、设备序列号（就是 MAC 地址）、`spawn_env`（键值不限）；项目 key、名字、**本地路径** | 0600；没有密钥 [测，只看了键名] | 同上 | [源 cpad/config/settings.py:3-6、56-61、86-89] |
| `host_id`（v0.8 加 `host_hw`） | 当口令用的随机数 | 0600，不进日志 | 同上 | docs/18 §4.1 |
| `logs/cpad.log`、`logs/mcp-audit.jsonl` | 脱敏日志（正文只记长度和不加盐的 `sha8`）；MCP 交出去了什么（不含内容） | 0600 | 同上 | [源 cpad/logging/redact.py:1-22]；docs/15 §3.9 |
| **`logs/launchd.{err,out}.log`**（cpad 和 cpa-stt 各一对） | Python 回溯（`str(exc)`）；cpad 的 err 有 84 行、3 段回溯（没看内容）[测] | **0644**：plist 没写 `Umask`，launchd 新建文件时用默认的 022；`secure_home` 只管 5 个点名的文件 | 同上（上层 0700 挡住了别的用户） | [源 cpad/service/launchd.py:57-58；stt/service.py:31-34；cpad/config/settings.py:157-166] |
| `rec-staging/<dev>/<fid>.part` | 拉到一半的明文音频 | 0700/0600 | 同上 | [源 cpad/recordings/land.py:3] |
| `hook-backups/` | `~/.claude/settings.json` 和各项目 `settings.local.json` 的旧副本，可能带 API 密钥类的环境变量；从不清理 | 0700/0600 | 同上 | [源 cpad/agents/claude_code/hook_settings.py:7-8、29] |
| `$TMPDIR/cpa-stt-*/run.wav` | 转写时拼起来的明文音频 | 0700；`finally` 里会删，但 SIGKILL、崩溃、断电时会留下；启动时不清扫 | 你 | [源 stt/runner.py:87-93] |
| `cpa export` 的导出 | 归档的明文副本 | 0700/0600；默认拒绝放进云同步目录 | 看你放在哪 | docs/15 §2.4 |
| 开发产物 | `backups/`（Flash 和 NVS 镜像）；`firmware/hwtest/captures`（43 个 WAV，0644）；暂存区里的 HIL 录音 | 明文，都在 Time Machine 备份里 | 你；0644 的文件别的用户也能读 | `.gitignore` |

### 2.3 cpa 管不到的副本

- **Spotlight 索引**：只有 root 能读，在 FileVault 里。`mdfind -onlyin ~/CardputerNotes` 按名字找得到 23 个 manifest 和 2 份笔记 [测]。
- **Time Machine**：目标是本地盘，`~/CardputerNotes` 和 `$CPA_HOME` 都在备份范围内。盘没挂上，**看不出备份有没有加密** [测]。30 天删音频和 `rec.hidden` 都管不到备份。
- **AI 客户端**：经 MCP 读到的结果，以及这套工作流转写的语音备忘，会留在 Claude Code、Codex、Cursor 各自的目录里（没去看）；而且客户端会把读到的内容发给它的模型提供商，服务管不了这一步 [源 docs/15:57、520]。
- **docs/25 的云端去向**：预设里标了 `leaves_mac` 的去向（比如 Grok）会把你说的话送出这台 Mac [源 docs/25 §2.3]。
- **剪贴板**：docs/25 的 `clip` 适配器会把文字放进剪贴板。Universal Clipboard 会把它同步到你的其他 Apple 设备，剪贴板管理器会存历史 [推]。
- **iCloud"桌面与文稿"是开着的** [测]：拖到桌面或文稿里的导出会离开这台 Mac。笔记目录不在 iCloud 里。
- **WhisperWoof 自己的加密库**（SQLCipher、WWENC1）：没看，WhisperWoof 的代码不在本次范围内。

## 3. 威胁模型（更新 SECURITY.md）

### 3.1 谁能拿到什么

| # | 对手 | 今天拿到 | 做完本文以后 |
|---|---|---|---|
| T-LOST | 设备连卡一起丢了 | 所有没同步的、`/cfg` 项目名、NVS、coredump；**已同步的笔记和录音的残留** | SEAL 以后，没被人改过的设备上只有密文。剩下的是元数据：文件名、大小、数量、档案、`.M` 的全部字段（模式、时间、项目 key、星标、`consent`，以及 docs/25 §4.6 加的 `dest`、`lock`）、项目名缓存和公钥 |
| T-CARD | 只拿到 SD 卡 | 卡上的一切，包括能从空闲簇恢复的旧 WAV 和 Launcher 的 `/bkp/`。**还能往卡里塞假的条目和录音**：设备当成自己记的发给 Mac，进了 `events.jsonl`、今日笔记和 MCP，是一条往 AI 客户端做提示注入的路（更大的一条是会议里真有人说的话，靠 docs/15 §3.7 的"不可信数据"标记） | SEAL 以后只有密文；旧残留要清一次（§4.3）。伪造照样可以（公钥是公开的）；D10 只挡只拿到卡的人（§8.1 D10） |
| T-USB | 不可信的 USB 主机：公司 IT 管的 Mac、朋友的电脑、带数据线的公共充电桩 | 不用按键就能读写整片 Flash、刷固件、经 USB-JTAG 读运行中的内存 [源 docs/18 §8.2；platformio.ini:36] | **只读一次的**主机只拿到密文。**存心改设备的**主机挡不住：它能往 NVS 加自己的公钥、擦掉 NVS 逼设备回到明文、刷固件、读内存里的文件密钥。能做的是发现其中一部分（§4.4），加上"先同步清零、只充电时用数据阻断器" |
| T-ROGUE | 往你的 Mac 上插冒充的设备；或者拿到了 `host_id` 的人 | cpad 绑定序列号之前，会把 `host_id` 明文发给任何 303A:1001 串口（docs/18 T4）[源 docs/18:223]。序列号（就是 MAC 地址）能被任何 S3 固件伪造 [推]，而且出现在仓库里约 62 个文件中（#39），所以按序列号绑定不是认证。冒充的设备能塞笔记、排队项和录音，还能收到快照（项目名、session、≤120 B 的"等你"摘录）；拿到 `host_id` 的人能冒充那台 Mac | SEAL 以后：没证明私钥的 Mac 什么封好的东西都拿不到，也删不掉；钉住了钥匙的 `host_id`，证明之前连没封的也不给（§4.3"钉住"）；还没登记钥匙的 Mac，没封的待传仍然只靠 `host_id` 这个口令。冒充设备照样能塞东西，除非做 D10 |
| T-USER | Mac 上的别的用户 | 0700 挡住了；只有 0755 路径下那些 0644 的开发录音读得到。今天只有一个账户 [测] | SEC-0 以后都挡住 |
| T-APP | 以你身份跑的程序、恶意软件 | 一切 | 不变：不在防护范围内（SECURITY.md:62；docs/15 §5）。它能读私钥文件，也能直接调用 `cpa-key` |
| T-MAC | Mac 被偷 | 关机时 FileVault 挡住；开着或睡眠时靠登录密码 | 不变 |
| T-BACKUP | Time Machine 盘丢了 | 备份没加密的话，是归档的完整明文副本 | SEAL 以后还多了私钥文件（D6）。👤 你打开"加密备份"以后都挡住 |
| T-CLOUD | 离开这台 Mac 的路 | 笔记目录不在 iCloud；导出有防护 [源 cpad/archive/cloud.py]；剪贴板会经 Handoff 同步；MCP 客户端读到的会发给它的模型提供商 [源 docs/15:57、520]；docs/25 标了 `leaves_mac` 的去向 | 剪贴板只给本机（docs/25 §5.3 的 `ClipWriter`，§4.6）。另外两条是有意的：接哪个客户端、用哪个去向由你选（docs/15 §5 👤、docs/25） |

### 3.2 信任边界

v0.1 的边界"物理 USB + 当前登录的 macOS 用户"不变（SECURITY.md:9-11）。加一句：**在 SEAL 打开之前，设备是明文存储介质；打开以后，设备上存的是只有证明过私钥的 Mac 才解得开的密文，设备自己也解不开。前提是在这之前没有主机存心改过这台设备。** "设备上没有任何密钥"（SECURITY.md:26）在 SEAL 下仍然成立，因为设备只存公钥；做了 D10 以后改成"设备上只有一把证明身份的密钥，拿到它只能冒充设备"。

### 3.3 SECURITY.md 要改正和补上的

1. "收到 ack 就删除条目"不算缓解：删除只是 `unlink`，FAT 和 LittleFS 上都能捞回来。docs/18 的 T6、T7 一起改。真正的修复是 SEAL：密钥只在 Mac 上，残留就只是密文。
2. 补上 coredump：panic 时任务栈会落进 coredump 分区。docs/gates/launcher.md:217 的"无害"改成"会留下明文栈，见 docs/26 §4.4"。
3. 补上不可信的 USB 主机和充电桩：不用按键就能读写整片 Flash、刷固件、经 USB-JTAG 读内存；SEAL 只防被动地读。
4. 补上 Mac 上的副本和离开 Mac 的路：Spotlight、Time Machine、AI 客户端的会话记录和模型提供商、docs/25 的云端去向、剪贴板、iCloud 桌面与文稿。
5. 第 28 行"正式配对……放到 v0.3"已经过时，改成"SEAL 的握手证明（Mac 向设备），以及 D10（设备向 Mac）"。
6. 第 41–42、57 行的"加密推后实现"改成指向本文和 P8-E。
7. docs/18 §8.4 的四条 v0.8 说法一起写进去。
8. 做 SEAL 时新增"密钥"一节：设备只存公钥；私钥是 `$CPA_HOME/keys/seal/` 下的 0600 文件，跟着 Time Machine 和迁移助理走；丢了设备不用撤销任何东西；丢了 Mac 就到设备上忘掉它（§5）。

## 4. 每个位置怎么加密

### 4.1 总表

| 位置 | 默认（推荐） | 挡得住 | 挡不住 | 代价 |
|---|---|---|---|---|
| 设备上的 outbox 条目（cpafs、SD） | SEAL | 丢设备、拔卡、被读一次 Flash、删后残留、没证明私钥的 Mac | 存心改设备的主机；开着时经 JTAG 读内存；元数据；伪造（D10） | 设备看不到存下的正文；每行变长 |
| 设备上的录音 `.V` / `.M` | SEAL 只封音频；`.M` 全是明文结构，靠块的 AAD 防篡改；WAV 头由 Mac 补 | 同上 | 同上；`.M` 的字段 | 设备回放要你选（D9 👤）；要过 G-ENC |
| `/cfg` 项目缓存 | 照旧明文（设备自己要读） | — | 项目名 | 写进 SECURITY.md |
| NVS | 只放公钥、哈希、标签；收件人表在 cpafs 有一份镜像 | 没有能偷的秘密 | 标签和绑定的数量；存心改表的主机（§4.4 能发现一部分） | — |
| coredump | 正式固件空闲时，有内容才擦 | 崩溃后留下的栈 | 从崩溃到下一次空闲之间 | 少了调试用的 dump（测试固件可保留） |
| 已删的旧明文 | 旧的同步完提示一次：设备上清内部存储，卡用 `cpa sd scrub` | 旧残留 | SD 卡 FTL 里的备用块 | 你在 Mac 上跑一次命令 |
| Launcher 的 `/bkp/` | `cpa sd scrub` 发现里面有 cpa 的备份时告诉你，在 Launcher 里删 | 卡上的旧备份 | — | 你删一次 |
| Mac 归档 | FileVault（已开） | 关机或被偷的 Mac | 登录中同一用户的程序（不在范围内） | 无 |
| Time Machine | 加密备份（👤 核对） | 备份盘丢失 | — | 你设置一次 |
| `$CPA_HOME` 的日志等 | 全部 0600（修 launchd 日志） | 别的用户 | 同一用户 | 小修 |
| SEAL 私钥 | `$CPA_HOME/keys/seal/`，0600；不进 `cpa export` 和设置导出 | 别的用户、导出 | 同一用户的程序；没加密的 Time Machine 盘 | 无弹框 |
| Mac 上待解密的密文 | `$CPA_HOME/sealed/`，0700/0600 | 本来就是密文 | — | 小 |
| cpa-stt 临时 WAV | 启动时清扫 | 崩溃留下的残留 | — | 小修 |
| `events.jsonl` | 照旧（文字永久保留） | 靠 FileVault | 真正删除（D15） | — |
| 剪贴板（docs/25 `clip`） | 只给本机，标成隐藏和临时类型，2 分钟清 | Universal Clipboard、剪贴板管理器 [推] | 本机程序 | 无：docs/25 §5.3 的 `ClipWriter` 已经这样定（docs/25 D9） |
| 开发产物 | 改成 0600，或者移进 0700 的目录 | 别的用户 | — | 小修 |

### 4.2 排除的路

- **Flash 加密、Secure Boot**：单向操作，要烧 eFuse，开了以后 Launcher 和社区刷机工具都用不了 [源 docs/04:751 D16；SECURITY.md:35]。
- **NVS 加密**：前提是先开 Flash 加密。而且 `nvs_flash_secure_init` 只有声明（`nvs_flash.h:191`），`libnvs_flash.a` 里只导出了 `nvs_flash_init`，`CONFIG_NVS_ENCRYPTION` 也没开 [测：`nm`；docs/21]。
- **HMAC 外设加 eFuse 密钥**：要烧 eFuse；而且刷进去的读取固件照样能调用这个外设，效果和把密钥放 NVS 一样。
- **对称密钥放 NVS（K-NVS）**：读出 NVS 就拿到了密钥，等于没加密。
- **对称密钥只放内存、绑定时推下来（K-RAM）**：离线重启后就没有密钥了，"录音总能录"这一条就破了。

### 4.3 设备：SEAL（P8-E）

spec 写进 `protocol/enc/v1/README.md`，带已知答案向量和检查器。字段名先只写在文档里，spec 定稿时才进 schema。

**收件人：你在设备上点过头的 Mac，和档案分开**（D7）：
- **登记**：一台 Mac 的公钥只在你在设备上确认的那一刻收下（docs/27 §3.5 第 1 条）。
  - 分档案时：就是 docs/18 §5.3 的绑定确认，确认绑定就同时登记，不多按一次。
  - 不分档案时（docs/18 §9 的"不分档案"）：第一次见到这台 Mac 带公钥的 `hello`，设备弹一张单子"让「Mac·8C1D」解开设备上的记录？"，光标在"以后再说"。每个 `host_id` 只问一次；以后可以到 系统 → 加密 里加。
  - **已经绑定、还没登记钥匙的 Mac**（SEAL 固件之前绑的，或者绑定时还没有 `cpa-key`）：分档案时也没有绑定确认可用了，所以和不分档案时一样，它第一次带公钥的 `hello` 弹同一张单子，光标在"以后再说"，每个 `host_id` 只问一次。两种模式下 系统 → 加密 都有"加上「…」"一行，对连着的、没登记的 Mac 按 Enter 就弹这张单子。
- **每个新文件封给谁**：分档案时，封给登记过、绑在当前档案上的 Mac；档案 0（未定）和不分档案时，封给所有登记过的 Mac。交不交仍按 docs/18 §9 的规矩。当前档案里一台登记了钥匙的 Mac 都没有（比如公司那台没有 `swift`、装不了 `cpa-key`，docs/27 §5.2；或者它是第 5 台）：照录明文、带"明"字（D8），系统 → 加密 写"加密：公司 没有钥匙"；**从不封给别的档案的 Mac**，封了按 docs/18 的过滤也永远交不出去。
- **上限**：设备上最多登记 4 台 Mac，所以每个文件最多 4 个收件人。要登记第 5 台，先忘掉一台（"最多 4 台 Mac 能解，先忘掉一台"）。docs/18 的绑定表仍是 8 条，但用 SEAL 时实际能用的是 4 台。
- **存在哪**：NVS 里一张表，最多 8 条 `{hh, kid, pk, 档案位, 状态}`，约 470 B：`hh` 是 docs/18 §3.2 的那个 16 B 哈希（`sha256("cpa-host:" + host_id)` 的前 16 字节），"钉住"和"每个 `host_id` 只问一次"都按它查，不分档案时没有 docs/18 的绑定表也查得到；状态是"在用""换下的旧钥匙""问过、你选了以后再说"（最后这种不带 `pk`，表满时先挤掉它）。4 条在用的，另外留着最多 4 条换下来的旧公钥，直到设备上没有封给它的待传为止（握手证明要用到它）。`kid` = `sha256(pk)` 的前 8 字节，同一个 `kid` 只存一条：新的 `hh` 带来的在用 `kid` 已经登记在别的 `hh` 下（迁移时没换钥匙，或者 `cpa host reset` 之前的老 cpad），不另存一条，按 §5.3 问"忘掉旧的？"。
- **镜像**：同一张表和一个 `SEALON` 标记，写在 NVS 和**每一个挂上的存储**（cpafs 的 `/cfg/seal.json`、SD 卡的 `/cpa/cfg/seal.json`）上，和 docs/18 的 `PROFON` 一样（docs/18 原则 8、T9）。NVS 被整片擦掉（core 2.0.17 遇到 NVS 满了会这样做 [源 docs/gates/launcher.md:162]，Launcher 每次开机还会补 19 个左右的 `uiflow` 键 [源 docs/gates/launcher.md:203]）时，开机自动从 cpafs 的那份恢复。SD 卡上的那份不靠你核对指纹：插着的 Mac 的 `hh` 和其中一条对得上、又证明了那个 `kid`，才恢复那一条。任何一处有 `SEALON`、表却一条都找不回来（Launcher 布局没有 cpafs，又换了卡）：照录明文、照样带"明"字，系统 → 加密 写"加密：收件人表丢了"，不悄悄退回"从没开过"。
- **钉住**：一个 `hh` 在表里有在用的 `kid` 以后，它的 `hello` 不带 `seal`、带来不同的公钥，或者 `seal_proof` 没来、没验过，设备就把这台 Mac 当成 docs/18 的"未分配"：`prof{ok:[]}`、计数报 0、不采用它的快照、不发在线请求，封好的、没封的一样都不交。证明通过、或者你在设备上选了换成新钥匙，才放宽，照 docs/18 §3.7 等下一个 `pong`。所以拿到 `host_id` 的人不能靠"不带公钥"绕过去。带来不同的公钥时：
  - 这台 Mac 在同一次握手里用旧钥匙证明了自己（§5.3 的轮换）：直接换，提示一句"「Mac·8C1D」换了钥匙"；
  - 证明不了：保留旧的，设备上问"「Mac·8C1D」的新钥匙（3F2A·61D0）证明不了原来那把，换吗？"，光标在"不换"。不请你去对旧指纹：4 位、8 位的指纹都能靠多生成几把钥匙凑出来（下面"指纹"）。
- **一眼看得到**：系统 → 加密 第一行总写"加密：开（封给 Mac·8C1D、Mac·41F0）"或"加密：关（还没有 Mac）"，下面每台 Mac 一行，带指纹。Mac app 的安全卡片第一行写同一句。
- **指纹** = `kid` 的前 8 个十六进制，写成 `9C1B·27E0`；Mac 上 `cpa status` 和 Mac app 的安全卡片显示同一个，对得上就是同一把。不用 4 个十六进制：只有 16 位，多生成六万多把钥匙就能凑出任意一个，几秒的事；这样也和 docs/27 的 4 位设备码分得开。

**握手证明：Mac 证明自己有私钥**（D21；docs/27 §3.5 第 2 条）：
- `hello` 带 `seal`，或者这个 `hh` 在表里有在用的 `kid` 时，设备对 `hello` 的回复加 `seal_chal{E′, n, kid}`：每次握手新生成一把临时钥匙 `(e′, E′)` 和 16 B 随机数 `n`；`kid` 是设备为这个 `hh` 钉住的那把（没有就不带）。Mac 拿它和自己的钥匙比，对不上就报警"设备存的不是这台 Mac 的钥匙"（docs/27 §3.5 第 3 条）。设备只在这里报 `kid`，不放进 `prof`：不分档案时没有 `prof`（docs/18 §10.1），`prof.ok` 是档案 key 的列表（docs/18 §10.3）。
- Mac 的下一条 `ping` 加 `seal_proof[{kid, tag}]`：`tag` = HMAC-SHA256(HKDF(X25519(sk, E′), info = "cpa-seal/v1 confirm"), n‖dev) 的前 16 字节。由 `cpa-key prove` 算，私钥不出 `cpa-key`。
- 设备对每个声称的 `kid` 用表里的公钥验一遍。每次握手：在用的那个 `kid` 必验；旧 `kid` 只在设备上还有只封给它的待传时才验。所以通常 2–3 次 X25519，最多 1 + 5 次 [算]。
- **证明通过之前**：封好的东西一样都不交；这台 Mac 对封好的条目回的 ack、发的 `file.del` 都不认。证明通过以后，也只认收件人里有它证明过的 `kid` 的那些。
- 两边都拒绝全 0 的 X25519 结果（RFC 7748 §6.1），否则有人推一个低阶点当公钥，包出来的钥匙就是公开的。
- 不加新的消息类型，只加字段。和 D10（设备向 Mac 证明自己）方向相反，两个独立。

**格式**（D4）：
1. 每个文件（一条 outbox 条目，或一个录音段）用一把新的 32 B 文件密钥 `fk`，从 DRBG 取。打开文件失败重试时，一律重新取一把。
2. **时段钥匙**：设备维持一把临时 X25519 钥匙 `(e, E)`，和对每个收件人算好的 `Sᵢ = X25519(e, PKᵢ)`，放在静态区。收件人表一变、切换档案、忘掉某台 Mac，或者满 1 小时，就立刻换一把，旧的 `e`、`Sᵢ` 清零（docs/18 的"收窄立刻生效"）。
3. 每个文件：`wkᵢ = HKDF-SHA256(Sᵢ, salt = E‖PKᵢ, info = "cpa-seal/v1 wrap"‖fid)`，用 `wkᵢ` 以 AES-256-GCM 把 `fk` 包一份。`wkᵢ` 每个文件、每个收件人都不同，所以这一步固定用全 0 的 nonce。每个文件只做 N 次 HKDF 和 N 次包装，<1 ms；X25519 只在换时段钥匙时做。
4. **头**（明文，从不回写）：魔数、版本、标志（模式提示：便条或会议）、`fid`、`rec`、`seg`、`prof`、`E`、收件人 `[{kid, 包好的 fk}]`。约 52 + 56×N B，N ≤ 4 时最多约 280 B [算]。
5. **正文**按 4 KiB 明文一块，加密后每块 4112 B（带 16 B 标签）。nonce = 块序号（32 位）‖标志（本段最后一块、整个录音最后一段）‖0；AAD = 头的 SHA-256。这样 `fid`、`rec`、`seg`、`prof`、模式、收件人、"最后一块"和"最后一段"都受认证：改 `.M`、调换段的顺序、把别的录音的段拼进来、改 `prof`，Mac 都会发现。
6. **规矩**：同一个 `fk` 下每个块序号只加密一次；只有最后一块可以不满；断电恢复从不重新加密。`fk`、`e`、`Sᵢ`、`e′` 用完就用 `sodium_memzero` 清零；它们放在静态区，不放在任务栈上（IDF 4.4 的 ELF coredump 只抓栈）[推]。
7. **用哪个库**：X25519 用库里已经链接的 libsodium（`crypto_scalarmult_curve25519`，固定 32 B 数组、不用堆）[测：`nm liblibsodium.a`]；AES-GCM 用 mbedtls，它的 `MBEDTLS_GCM_ALT` 走 `esp_aes_gcm`：硬件 AES-CTR 加软件 GHASH，S3 的 AES 硬件没有 GCM 模式 [测：`nm libmbedcrypto.a`；SDK/include/mbedtls/port/include/mbedtls/esp_config.h:127]；HKDF 两个库都没有，用 HMAC-SHA256 拼，约 30 行，跑 RFC 5869 的向量；DRBG 用 mbedtls 的 CTR_DRBG。G-ENC 把 libsodium 和 mbedtls（开了 `CONFIG_MBEDTLS_HARDWARE_MPI`）的 X25519 都量一遍。

**为什么不用 age**（D4）：
- age 一块 64 KiB，要 64 KiB 的缓冲；没有 PSRAM，录音时的底线只要求最大块 ≥32 KiB，放不下。
- 标准的 `age -d` 拒收截断的流，断电恢复的段反正要用我们自己的读法。
- ChaCha20-Poly1305 其实在库里：libsodium 导出了 `crypto_aead_chacha20poly1305_ietf_encrypt` [测]（原稿说"要从源码带进来 +10 KiB"，错了）。它纯软件、不用堆也不用 DMA，留作 AEAD 的备选：G-ENC 发现 `esp_aes` 的 DMA 或中断路径有问题，就在 spec 定稿前换成它。CryptoKit 两种都有。
- 备选 age 的好处：标准格式，原生支持多个收件人，cpa 坏了时能用 `age -d` 救急。

**WAV 头不进密文**：明文 WAV 换段时要 `lseek 0` 回填 44 B 的头 [源 docs/11:242]，在密文里回填等于同一个 nonce 加密两次。所以密文里只放 PCM，头由 Mac 解密后按字节数补上；录音契约 v1 里 `segments[].sha256`（整个明文 WAV）也由 Mac 算，契约不变。今天断电恢复在没有前一段时，是从 WAV 占位头里读模式提示（"MEMO"/"MEET"）[源 fw/rec/wav_header.h:4]；加密段没有这个占位头，模式提示改放在 SEAL 头的标志里。

**录音的 SD 布局**：
- 加密的文件用新的 8.3 类型字母：音频 `.V?`、SD 上的条目 `.E?`。老固件和老 cpad 不认这些名字，不会误读。
- **`.M` 什么都不封**：它照旧是明文结构，`recovered_meta` 要从前一段的 `.M` 抄模式、时间、项目、槽位 [源 fw/rec/rec_recovery.cpp:25-27]，`file.list` 的 `rec_item_of` 也要读这些字段 [源 fw/rec/rec_meta.h:52]；断电时 `fk` 已经没了，设备也封不了新的 `.M`。元数据泄露本来就接受（项目名在 `/cfg` 里也是明文），防篡改靠上面块的 AAD。
- `.M` 只多三个字段：`seal:1`、`hdr`（头的长度）、`rk`（收件人 `kid`，最多 4 个）。今天最坏的 `.M` 约 501 B，离 512 B 只剩约 11 B（可行性评审算的；`test_worst_case_fits_512_bytes` 只断言 ≤512 [源 test/core/test_rec_meta/test_main.cpp:104-119]），加上 `prof` 和 `rk` 就超了，docs/25 §4.6 还要加 `dest`、`lock`：`kMetaMax` 改成 768（四样都加上以后的最坏 `.M` 要在 `test_rec_meta` 里重算 [待]），`MetaText` 改成静态的，不放在 6 KiB 的 `rec_task` 栈上 [源 fw/rec/rec_meta.h:17、46；fw/hal/audio_task.h:43]。
- `.M` 里的 `sha256` 改成对整个 `.V` 文件从第 0 字节算（头从不回写，所以 Mac 能整文件算）；设备的增量硬件 SHA 照用。
- **Mac 的传输校验要改**（原稿说"不用变"，错了）：`verify.py` 写死了 `HEADER = 44`、对 `data[44:]` 算哈希 [源 cpad/recordings/verify.py:7、17、37]，要改成按文件类型取头长；`land.py` 解密后补 WAV 头。
- 固件里凡是假定 `size == 44 + bytes` 的地方都要改：`rec_item_of`（`exact` 和 `dur_ms`）、`del` 的大小检查、`even_data`、`needs_recovery`、`backfill` [源 fw/rec/rec_meta.h:52；wav_header.h:38；rec_recovery.h:48；rec_engine.h:78]。
- **分块不跟环形缓冲对齐**：`gate()` 去掉静音的开头，所以一段录音的第一次写不满一块；跨段那一块会被 `RecWriter::write` 拆开（9,600,000 mod 4096 = 3,072 B）[算]。所以 `RecWriter` 加一个静态的 4096 + 16 B 暂存区，攒满 4 KiB 就加密写出一块 4112 B，换段和结束时把最后不满的一块写出去。**不在环形缓冲里原地加密**：`RecEngine::write` 在 `writer_.write` 之后才对同一块 `pcm` 算电平 [源 fw/rec/rec_engine.cpp:127-129]，原地加密会让电平表读到密文。
- **断电恢复**：头之后按整块（4112 B）截断，没有"最后一块"标志的流标成 `rcv`。不再"头先 fsync"：现有的每秒一次 fsync 已经保证了顺序，而多一次 fsync 最多要 78 ms，最坏换段本来就要 290 ms [源 fw/hal/audio_task.h:2；docs/11:242]。

**outbox 条目**：
- 加密的条目换一个文件名：cpafs 上 `<MonoId>.sea`，SD 上 `.E?`。老固件开机扫描时不认的名字只计数、不动 [源 fw/outbox/outbox_scan.h:11]；如果还叫 `.json`，老固件的解码要求 `body`/`text` [源 fw/protocol/decode_device.cpp:11、82]，会把它挪进 `bad/`，而 `bad/` 只留 20 条，就丢了。经 Launcher 降级是很平常的事。
- 行的格式：`{…, "prof":…, "rk":[kid…], "enc":"<base64>"}`。`enc` 里封的是一个二进制、带长度前缀的小块，装着 `body` 的**全部**字段（`text`、`project`、`intent`，docs/18 和 docs/25 加的 `dest`、`how`、`live`，以后加的也一样），不是转义过的 JSON 文本，所以 500 个 `"` 不会变成 1000 B。AAD = 头的哈希‖`id`‖`kind`‖`prof`。
- `id`、`kind`、`prof`、`rk` 留在明文里：设备开机扫描照样能建索引，还能建每项的收件人位图（每项 1 B，8 位对应收件人表的 8 条；outbox 200 项加录音索引 256 项，约 0.5 KB，开机一次分配）。docs/18 P8-F2 要的每项过滤就是这个。
- **没封的条目不带 `enc`**，和今天一字不差。不会出现 `enc:false`，`enc` 只有一种类型。
- 协议早就给 `enc` 留了名字 [源 protocol/README.md:201]。
- **行长**：4 个收件人时头约 276 B，`enc` 约 1.14 KB [算]；正文从转义的 JSON 挪进了二进制，最坏一行要在 2048 B 上限下用 `test_worst_lines` 重算，把封好的 `outbox.item`（4 个收件人）加进去 [待]。
- `file.items[]` 尽量不加字段：密文和明文靠文件类型区分。今天 4 项一页是 2044 B，只剩 4 B [源 protocol/tools/test_worst_lines.py:100]；真要加字段，`file_fit_page` 会把一页降到 3 项，测试跟着改。schema 和 `test_worst_lines` 里 `size` 的上限 44 + 600000×32 也要加上头和每块 16 B 的标签，约多 75 KB [算]。
- 今天的固件只在发送和开机扫描时读回存好的行，还没有回放（可行性评审对着 `d330465` 核对过）。

**交给谁**：在 docs/18 的档案过滤之上再加一条。设 proven(H) 是 Mac H 这次握手证明过的 `kid`，visible(H) = `prof ∈ ok(H)`（不分档案时这一条总成立），并且条目没封、或者 `rk` 里有 proven(H) 的 `kid`：
- docs/18 §3.5 第 1–3 条和它的测试预言一律按 visible(H) 算：`file.list` 只列它；别的段 `file.get` 回 `E_UNKNOWN_ITEM`、`file.del` 回 `ack` 但不删；`pending`、`files.n`、`files.rev` 只数它。所以只封给别的 Mac 的东西，这台 Mac 看到的 ↑ 能降到 0，它也看不出别的 Mac 那边有没有动静。
- 设备对 `hello` 的回复还不知道 proven(H)，计数里不含封好的；证明通过以后由下一个 `pong` 放宽（docs/18 §3.7）。
- 钉住了钥匙的 Mac 证明之前什么都不给（上面"钉住"）。

**Mac 收到以后：先存密文，再解密**（D20）：
- cpad 启动时、以及配置里的 `cpa-key` 路径或 `keys/seal/` 变了时，跑 `cpa-key selftest`，只把自检能打开的 `kid` 放进 `hello` 的 `seal.kids`；结果和上一次不一样就重新发一次 `hello`。所以安装脚本在 cpad 已经起来以后才造好 `cpa-key`（docs/27 §2.2 第 7 步），下一次握手照样带着公钥。
- 收到密文：头里的收件人至少有一个是自检过的 `kid` → 校验 `sha256` → 写进 `$CPA_HOME/sealed/<dev>/<fid>`（0700/0600）→ fsync → 回 ack 或发 `file.del`。设备这边的条目就此完成。头里一个自检过的 `kid` 都没有（正常的设备不会交这样的条目）：不存、不 ack、不回 `rejected`，照 docs/18 §7.1 D2 重新发一次 `hello`，连着 3 次就照 D4 停下。
- 解密另外跑：一个工作循环把 `sealed/` 里的交给 `cpa-key unseal --batch`，逐块验标签、核对 `.M` 和头里受认证的字段，成功了就照常落地（`land.py`、今日笔记、事件），再删掉 `sealed/` 里那一份。
- **解不开时**（工具不在、坏了、超时）：密文留在 `sealed/`，`cpa status` 和 Mac app 显示"等解密 N"，过一会儿重试。设备的 outbox 照常往前走，没封的条目不受影响。
- 原稿写的是"解不开就不回 ack、不发 `file.del`"。那样不行：outbox 按 ID 顺序发、最多 4 条在途、每 30 s 重发 [源 fw/outbox/outbox_core.h:1-7]，4 条解不开的就把后面所有的都堵住（包括明文的）；待传到 200 条就拒收新的，录音在卡上越积越多，录音索引只有 256 项 [源 docs/18:568]，"录音总能录"就破了。
- **老实说**：一台 Mac 的私钥要是真丢了（盘坏了、没有 Time Machine），只封给它的东西就永远解不开，不管它还在设备上，还是已经进了那台 Mac 的 `sealed/`（§5.4）。

**随机数**：
- nonce 不靠随机数，但 `fk`、`e`、`e′`、`n` 要靠。`esp_random()` 在不开射频时是伪随机的 [源 docs/18 §8.2]。
- **种子只取一次**：在 `setup()` 的最开头、`M5Cardputer.begin()` 启动电池 ADC 之前 [源 firmware/app/src/main.cpp:76]，`bootloader_random_enable()` → 读一段 → `bootloader_random_disable()`。头文件要求在 RF、ADC 初始化之前用完 [源 SDK/include/bootloader_support/include/bootloader_random.h:28-40]；之后再也不调。
- 再混进去：`esp_random()`；每段录音开头几块环形缓冲的 SHA-256（麦克风噪声的低位）；cpad 每次 `hello` 给的 `seal.salt`。主机给的盐只对没见过它的人有用，发盐的那台主机自己知道它。
- `fk` 的 HKDF 里混了 fid，只能保证万一 DRBG 输出重复，文件密钥也不同；**不能**让它变得猜不出来。猜不出来靠种子：G-ENC 在冷启动、离线、不开射频的情况下，用 SP 800-90B 的最小熵估计器量 [待]。

**CPU、堆、静态内存、在哪个任务里算**（G-ENC 实测之前的估算 [算]）：
- 每 4 KiB 一块的 AES-GCM 约 0.3–1 ms（硬件 AES-CTR 加软件 GHASH），一块的周期是 128 ms，核 0 占用 <1–3%。每个文件的包装 <1 ms。
- X25519 一次约 20–80 ms，只在换时段钥匙和握手证明时做，**都在循环任务里，从不在 `rec_task` 里**：`rec_task` 只有 6 KiB 栈，G-A6 要求留 ≥1.5 KiB 余量 [源 fw/hal/audio_task.h:43；docs/12]。
- **刚绑定或刚切档案就开录**：循环在开录的准备阶段（codec 还静音着，至少 70–150 ms）算新的时段钥匙，环形缓冲能扛 768 ms [源 docs/11:242]。这个情况进 G-ENC。
- **静态内存**：暂存区 4112 B + `.M` 文本 768 B + 时段钥匙、握手钥匙和收件人表（表约 470 B）约 1 KB，共约 6 KB。今天 66.7 / 98.3 kB，放得下；但 docs/21 如果把 WiFi 链接进来（约 92.1 kB），就只剩约 6 kB，两样不能都加 [源 docs/21:91、336]。G-ENC 量，docs/21 的 WiFi 决定时要算上它 [待]。
- **堆**：libsodium 的 X25519 不用堆；GCM 的上下文约 400 B。底线是空闲 ≥150 KiB、最大块 ≥64 KiB，录音时 ≥96 / ≥32 KiB [源 docs/12:455]；今天空闲 178.7 KiB、最大块 160 KiB [源 docs/21:88]。**整个 app 在录音时的堆还没在真机上量过**（docs/07 §6.7 第 6 条仍开着），SEC-3 先量。镜像约 +20–40 KiB。
- **栈**：预编译的 mbedtls 和 libsodium 里的栈帧，`stack_gate` 和 `stack_budget` 看不见。G-ENC 要记下 `rec_task` 和循环的 `uxTaskGetStackHighWaterMark`。

**录音时不擦写 Flash**：擦一个 4 KiB 扇区时两个核的 Flash 缓存都停，一般约 45 ms、最长约 400 ms，而 I2S DMA 只能扛约 32 ms [源 fw/hal/audio_task.h:2]；docs/21 已经有"录音中不写 NVS"。所以收件人表的写入、"上次崩溃"计数、coredump 擦除、清理内部存储，都等不录音、不传文件的时候再做；之前先放在内存里。

**没有钥匙时**（D8）：
- 只有设备上一个登记过的 Mac 都没有（新设备、你还没对任何一台点过头），或者分档案时当前档案里一台登记了钥匙的 Mac 都没有时，才照录明文；格式和今天一样，不带 `enc`。档案 0 和不分档案时都封给全部登记过的 Mac；NVS 丢了从镜像恢复（上面"镜像"）。
- 系统 → 加密 第一行写"加密：关（还没有 Mac）"或"加密：公司 没有钥匙"。
- 开过 SEAL 以后（`SEALON` 记在 NVS 和每个挂上的存储上，见"镜像"），某一条又落回明文：这条记下时给一个提示"这条没加密"，锁屏卡片多一个"明"字，系统 → 档案 显示"明文待传 N"。
- 理由：录音总能录，比加密更要紧。备选是拒绝录。

**以前的明文怎么办**（D12）：
- **没有开关**：有登记过的钥匙就封。旧的明文条目照常同步，不用你先清零。
- 旧的都同步完以后，设备提示一次"旧的明文残留还在卡上和内部存储里"，给两个办法：
  - 系统 → 加密 →"清理内部存储"：只在 outbox 为空、不录音、不传文件时能选；擦掉整个 `cpafs` 再格式化，约 10–20 s [待]。
  - `cpa sd scrub`：卡插在 Mac 的读卡器上，在 `/cpa` 下写满全 0 的文件、fsync、再删掉，把空闲簇里的旧明文盖掉。它不碰 Launcher 的文件，也不格式化。卡的 FTL 里的备用块，写零也碰不到 [推]。
- **不要格式化卡**（原稿建议过"整卡写零再格式化"）：macOS 给大于 32 GB 的卡默认格式化成 exFAT，IDF 4.4 挂不上，设备就没卡可用、录不了音；格式化还会删掉 Launcher 的 `/config.conf`、`/downloads/`、`/bkp/` [源 docs/gates/launcher.md:15、138]。
- `cpa sd scrub` 发现 `/bkp/` 里有 cpa 的备份时，告诉你它在哪、在 Launcher 里怎么删；它自己不删 Launcher 的东西。

### 4.4 设备：其他位置

- **`/cfg` 项目缓存**：设备自己要显示 1–9 的名字，加密了它自己就读不了，所以**照旧明文**，写进 SECURITY.md。里面是 Mac 推下来的、你起的项目名，没有正文。
- **NVS**：SEAL 只多存了公钥，公钥是公开的。`host_label` 默认是 `Mac·` 加 4 个随机十六进制（docs/27 D9，修订 docs/23 D10 的"默认为空"），不含真名，也不用"家""公司"这类档案的词。将来如果做 docs/21 W2 的 WiFi 密码，除了 §5.6 的不变式，还要写明"拿到设备就拿到密码"。
- **coredump**（D11）：
  - 正式固件在循环空闲、不录音、不传文件时，先 `esp_core_dump_image_check()`，有内容才往下走 [源 SDK/include/espcoredump/include/esp_core_dump.h:114]。
  - 读 `esp_core_dump_get_summary()` 的 `app_elf_sha256`：和正在跑的固件一样，就把"上次崩溃"计数加一（系统页看得到）；不一样，说明是 Launcher 下别的固件（比如 Bruce）留的，不计数 [源 esp_core_dump.h:41、158]。
  - 然后用 `esp_partition_erase_range` 按 4 KiB 一块分开擦，16 块约 0.7 s [待]；不用一次擦完的 `esp_core_dump_image_erase` [源 esp_core_dump.h:134]。中途开始录音就停下，下次空闲再接着擦。
  - 测试固件看编译开关 `CPA_COREDUMP_KEEP`：开着就保留，留着调试。它和 `CPA_TEST_HOOKS` 分开，所以 S2-d 能用测试钩子触发 panic、再验证擦除。
  - 分区按类型去找，不缓存偏移（Launcher 会搬动分区 [源 docs/gates/launcher.md:48]）。
  - 不选"把 `cpa_8MB.csv` 最后一行 coredump 删掉"：它是最后一行，删了什么都不用挪，可 Launcher 下照样要擦，一条路比两条简单。
- **删后残留**：SEAL 就是修复，靠的是"密钥擦掉了，密文就没用了"。不做"删除前覆盖写"：SD 卡的 FTL 会把写入重定向到别的块，覆盖不一定落在原处，而且每段要多写 9.6 MB [推]。
- **不可信的 USB 主机**（D17）：
  - **被动和主动要分开说**：固件用的是内置的 USB 串口/JTAG（`ARDUINO_USB_MODE=1`）[源 platformio.ini:36]。不用按键，任何 USB 主机，以及从 Launcher 启动的任何固件（NVS 是共用的），都能：读整片 Flash；写 NVS（往收件人表加自己的公钥、擦掉表逼设备回明文）；刷任何固件；不复位就经 USB-JTAG 用 OpenOCD 接上运行中的芯片，读内存里的 `fk`、`Sᵢ`、草稿和麦克风环形缓冲。所以"SEAL 以后只有密文"只对"被动读一次"成立。
  - **能发现的一部分**：
    - 系统 → 加密 列出每个收件人和指纹，你看得到多出来的；
    - cpad 记住"这台设备发过封好的东西"和它在头里见过的 `kid`：以后它发来没封的条目（D8 落回明文的也算，照样提示），`seal_chal.kid` 不是这台 Mac 的钥匙，或者头里第一次出现一个没见过的 `kid`（"设备多了一个收件人 9C1B·27E0，是你刚点过头的 Mac 吗？到 系统 → 加密 看"），就在 `cpa status` 和 Mac app 里报警（docs/27 §3.5 第 3 条）。
    - 这些挡不住换了固件的设备，也挡不住同时改了表和镜像的人。
  - **写进 SECURITY.md 和 README**："去不信任的电脑之前，先在家同步清零；只充电时用只充电的线或数据阻断器。"
  - **研究项停放，不进 M-SEC**：锁屏时关掉 USB 复位（`RTC_CNTL_USB_CONF_REG` 的 `RTC_CNTL_USB_RESET_DISABLE`，第 17 位，IDF 4.4 的头文件里没写语义）和 USB-JTAG 桥（`USB_SERIAL_JTAG_USB_JTAG_BRIDGE_EN`，第 16 位，没核实）。光挡复位没用，JTAG 不用复位就能读内存；两位都管用也挡不住按住 G0 上电。
- **内存**：不处理。设备开着时被人接上 JTAG，能拿到当前时段的钥匙，也就能打开这个时段（≤1 小时）里写的文件。每个文件换一把临时钥匙也挡不住这种人：他同时在读麦克风的环形缓冲。

### 4.5 Mac：解密工具 `cpa-key`

- **是什么**：一个小的 Swift 命令行工具，和 Mac app 在同一个 SwiftPM 包里（`mac/`，docs/24 §7），但是单独的可执行目标。只用 Apple 的 CryptoKit（Curve25519、HKDF、AES.GCM、ChaChaPoly、HMAC），**不链接任何第三方代码**。daemon 仍然只依赖 `pyserial`，ADR 0004 第 1 条不改 [源 docs/adr/0004-local-data-access-mcp.md:57]。
- **子命令**：
  - `init`：生成私钥，写进 `$CPA_HOME/keys/seal/<kid>.key`（0600，目录 0700），打印公钥和 kid；
  - `pub`；
  - `selftest`：用它手上的每把私钥打开一份已知答案的样本，cpad 启动时跑；
  - `prove`：对设备的挑战算握手证明；
  - `unseal --batch`：cpad 起一个子进程，经 stdin/stdout 一行一个请求，省得每条笔记都起一次进程；cpad 给它设超时，超时就杀掉重起；
  - `rotate`：生成一把新的在用钥匙，旧的一律留着只解不封（§5.3），没有不留的选项；
  - `wipe`：删掉 `keys/seal/` 里的全部私钥，只给 `cpa host reset --wipe-keys` 用（§5.5 第 3 条）。
  - **不做 `export` / `import`**（D7）。

  一律用参数数组、不经 shell；密钥从不出现在 argv、stdout 和日志里。
- **cpad 怎么找它**：只认配置里的绝对路径，并检查属主是你、别人不能写（和 WhisperWoof 检查 `cpa` 的做法一样，docs/23 §6.1）。
- **一定装**：安装脚本（docs/27 §2.2 第 7 步）总是装上 `cpa-key`，在前台跑 `init` 和 `selftest`，不是"装了才顺手做"；首次设置的"连上"在 `selftest` 通过前是灰的（有 `swift` 时），所以第一次握手就带着公钥。造不出来（比如没有 Swift 工具链），`cpa doctor` 说清楚，设备上显示"加密：关"。
- **解不开时**：见 §4.3"先存密文，再解密"。
- **它自己的解析器也要防**：头和容器的解析会吃到敌意设备或敌意卡给的字节，进 §6.6 的变异测试。
- **备选**：改 ADR 0004，允许 `cryptography`（wheel 很大，带原生代码）；外部的 `age` 程序（没装，要从外面装）；纯 Python 实现（慢，容易写错）。

### 4.6 Mac：归档和小修

- **默认**：FileVault（已开）+ 加密的 Time Machine。👤 请在 系统设置 → 通用 → 时间机器 里选中备份盘，看有没有写"已加密"。SEAL 以后私钥文件也在备份里，这是又一个要加密备份的理由。文件照样开放。
- **不做逐个文件加密**（D13）：它会挡住 Obsidian、grep、WhisperWoof、QuickLook 和 Spotlight。可 cpa 和 MCP 要读就得先解密，同一用户的程序经它们照样读得到；还得让 cpad 拿得到钥匙，等于把钥匙交给了任何能跑 `cpa` 的程序。
- **可选，只写做法、不做成功能**：把笔记目录放进一个加密的 APFS 稀疏磁盘映像，登录时挂上。代价：
  - cpad 和 cpa-stt 都要处理"笔记目录还没挂上"；
  - Obsidian 和 WhisperWoof 的路径要改到 `/Volumes/…`；
  - 挂上以后，对本机程序照样是开放的。

  只有在你要把备份放到不加密的地方时，它才有意义。
- **Spotlight**：照旧索引，你搜得到笔记；索引只有 root 能读，而且在 FileVault 里。要排除，得改系统设置，由你决定。
- **小修**（SEC-0，不用等决定）：
  1. **launchd 日志**：cpad 和 cpa-stt 的 `install()` 在 bootstrap 之前，先把 `launchd.{out,err}.log` 建成 0600（launchd 只在自己新建文件时定权限）；`secure_home` 启动时把 `logs/*` 都改成 0600，修好已有的那几份 [源 cpad/service/launchd.py:50-60；stt/service.py:31-34]。`secure_home` 也管 `rec-staging/`、`stt/`、`hook-backups/`、`keys/`、`sealed/`，不再只管 5 个点名的文件 [源 cpad/config/settings.py:157-166]。
     **不在 plist 里加 `Umask`**（原稿这么写过）：子进程会继承 umask。cpad 在项目目录里跑 `claude`，docs/25 的适配器会跑别的 app，它们新建的文件会变成 0600、目录 0700 [推]，Docker 挂载、开发服务器可能就坏了。
  2. cpa-stt 启动时清掉 `$TMPDIR/cpa-stt-*`，只清属主是你、超过 1 小时、而且没有进程在用的 [源 stt/runner.py:87-93]。
  3. **日志里的正文哈希**：`cpad.log` 里的字段改名 `h8`，值是 HMAC-SHA256(本机盐, 正文) 的前 8 位十六进制；盐本机随机生成（0600，不进导出）。这样短笔记猜不出来，两台 Mac 的日志也对不上。只改 `redact.py` [源 cpad/logging/redact.py:19-22]，跟着改 `test_control_service_logging.py`。**事件里的 `sha8` 不动**：docs/22 §5.6 和 docs/23 里它是事件字段，`inbox_start` 要拿它比对 [源 docs/22:611；docs/23:224]；事件本来就存着正文，多一个不加盐的哈希不多漏什么。
  4. `hook-backups/`：每个目标（`~/.claude/settings.json`、各项目的 `settings.local.json`，同在一个目录里）留**第一份**（装 cpa 之前的原件）和最近 2 份。原稿的"只留最近 5 份"会把原件删掉。
  5. 开发录音 `firmware/hwtest/captures` 改成 0600，或者移进 0700 的目录。`backups/` 不动，只提醒你它在 Time Machine 备份里。
  6. 剪贴板：docs/25 §5.3 的 `ClipWriter`（docs/25 D9）已经定了，本文不另做：cpad 经 ctypes 调 `NSPasteboard`（不用 Swift），只给本机、标成隐藏和临时类型，2 分钟后清；ctypes 走不通退回 `pbcopy` 时要你按一下确认。本文只在 M-SEC 里核对（S3-d）。
- **`spawn_env` 里像密钥的键**（放到 SEC-1，和 `cpa doctor` 一起）：`cpa doctor` 对名字像 `*KEY`、`*TOKEN`、`*SECRET`、`*PASSWORD` 的键报警告；只有 cpa 自己写这个设置时才拒收。**读配置时不报错**：要是放进 `_spawn_env` [源 cpad/config/settings.py:86-89]，手改过的旧配置会让 cpad 起不来。launchd 那边本来就只抄 `PATH`、`HOME`、`LANG`、`SHELL`、`USER`、`LC_*` [源 cpad/service/launchd.py:20、44]。

## 5. 密钥管理

### 5.1 谁有什么

| 东西 | 在哪 | 是不是秘密 | 丢了会怎样 |
|---|---|---|---|
| SEAL 私钥（每台 Mac 一把在用的，加上留着只解不封的旧钥匙） | `$CPA_HOME/keys/seal/<kid>.key`（0600，目录 0700）；不进 `cpa export` 和设置导出；在 Time Machine 里 | 是 | 只封给那台 Mac 的东西解不开（§5.4） |
| SEAL 公钥、kid | `$CPA_HOME/keys/seal/pub.json`；设备 NVS 和 cpafs 镜像；SD 上一份副本（Mac 证明了才恢复） | 否 | 下次 `hello` 重新推，要你在设备上确认 |
| `host_id` | `$CPA_HOME/host_id`（0600）；设备只存哈希 | 是（当口令用） | 重新绑定 |
| 文件密钥 `fk` | 设备内存里，文件一关就清零；Mac 解密时也只在内存里 | 是 | — |
| 时段钥匙 `e` 和 `Sᵢ`、握手钥匙 `e′` | 设备静态区，换时段或握手完就清零 | 是 | — |
| 待解密的密文 | `$CPA_HOME/sealed/`（0700/0600） | 密文 | 私钥在就能解 |
| 日志 HMAC 的盐 | `$CPA_HOME/keys/log_salt`（0600） | 不太要紧 | 旧日志的 `h8` 对不上，无所谓 |
| 设备认证密钥（做 D10 才有） | 设备 NVS 和 Mac 上 | 是，但拿到它只能冒充设备 | `cpa device forget` 撤销 |

- **`host_id` 还是口令，要老实说**：它明文发给第一次握手的任何板子 [源 docs/18:223]，拷走 `$CPA_HOME` 的人也拿得到。拿到它就能冒充那台 Mac 的绑定（原稿说"拷走 `$CPA_HOME` 的人拿不到绑定"，对存心的人不成立）。`host_hw` 只让**老实的** cpad 在拷来的目录上自己换身份，挡不住改过的客户端（docs/27 §3.5 第 7 条）。
- **SEAL 以后**：冒充者没有私钥，过不了握手证明，封好的东西拿不到、删不掉；钉住了钥匙的 `host_id`，不证明就连没封的也拿不到（§4.3"钉住"）；还没登记钥匙的 Mac，没封的待传仍然只靠这个口令。私钥文件也在 `$CPA_HOME` 里，所以整个目录被拷走（比如没加密的 Time Machine 盘）就两样都丢了，这和归档本身被拷走是同一件事（§5.2）。

### 5.2 私钥放哪（D6）

- **默认：0600 的文件**，`$CPA_HOME/keys/seal/`。
- **为什么不放钥匙串**（原稿的默认）：
  - `cpa-key` 是 ad-hoc 签名的（docs/24 D12），每次重新编译签名都会变，钥匙串的访问控制就要再问一次。
  - 这个框要的是登录密码，而且是 launchd 后台进程在同步到一半时弹出来的。这个项目编译得很勤，不是"一年几次"。这等于训练你往突然冒出来的框里打登录密码。
  - "用本机自签名证书签就不弹了"没验证过，钥匙串可能照样认编译的哈希 [待]。
  - 钥匙串换来的好处也比原稿说的小：登录钥匙串文件本身也在 Time Machine 里，只靠登录密码保护，备份没加密时能离线猜。
- **文件的代价**：Time Machine 没加密时，私钥以明文在备份里；拷走 `$CPA_HOME` 的人拿得到它。两样和归档本身的暴露是同一回事，所以办法也是同一个：加密 Time Machine（👤 §8.2 第 4 条），`cpa doctor security` 在没加密时警告。
- **不出这台 Mac**：不进 `cpa export`，也不进设置导出（docs/27 的泄漏检查加上 `keys/`）；只跟着 Time Machine 和迁移助理走。
- **不经命令行传密钥**：只有 `cpa-key` 读这个文件；不用 `security -w` 之类会让密钥出现在进程列表里的做法。
- **备选：钥匙串**。要做就三条一起做：后台访问不弹框（拿不到就失败，条目留在 `sealed/` 里等）；加一个前台的 `cpa-key authorize`（或者 Mac app 里一个按钮），在你人在 Mac 前时弹框；`cpa-key` 单独编译，源码不变就不重编。

### 5.3 轮换

- `cpa-key rotate` 生成新钥匙，旧钥匙留着只解不封，从不自动删。下次 `hello` 推新的公钥，同一次握手里 Mac 用新旧两把都证明一遍，设备就替换这台 Mac 那一条并提示一句；证明不了旧的，就在设备上问、光标在"不换"（§4.3"钉住"）。旧文件照样解得开。
- **换了硬件自动换**（D23）：`host_hw` 对不上（迁移助理、恢复到另一台 Mac）时，cpad 换新的 `host_id`，`cpa-key rotate` 也换一把新钥匙，旧的留着只解不封。这样新 Mac 不会拿旧 Mac 的钥匙当在用的。
- **`cpa host reset` 也换**：换新 `host_id` 的同时跑 `cpa-key rotate`，否则新身份带来的在用 `kid` 已经在设备的表里。
- **老实说分到哪一步**：迁移以后新 Mac 仍然有旧 Mac 的私钥（只解不封是规矩，不是密码学），能证明旧 `kid`。旧 Mac 的 `host_hw` 对得上，自己不会换钥匙；它要是拿去当公司的、没跑 `cpa host reset`，封给它的公司文件新 Mac 也解得开，只剩 docs/18 的档案过滤把两边分开。所以旧 Mac 改作他用之前，先在它上面跑 `cpa host reset`（§5.5 第 3 条），家和公司在密码学上才分开。
- 新 Mac 证明的旧 `kid` 正好是另一台已绑定 Mac 在用的钥匙时，设备提示"「Mac·8C1D（新）」也有「Mac·8C1D」的钥匙"，问"忘掉旧的？"，光标在"留着"。
- **什么时候手动换**：怀疑私钥泄露时才换，不定期换。设备上没有秘密，定期换得不到什么。
- 设备上没有要轮换的东西。

### 5.4 丢了以后

- **丢了设备**：
  - 设备上没有秘密，Mac 上什么都不用撤销；做了 D10 的话，用 `cpa device forget` 撤销它的认证密钥。
  - 损失的是上次同步以后还没传的东西。
  - SEAL 开着时，捡到的人只拿到密文和元数据；没开时拿到的是明文。
- **丢了 Mac 或 Mac 坏了**（D22）：
  - 归档靠 FileVault。
  - 到设备的 系统 → 档案 里**忘掉**这台 Mac，以后新记的东西就不再封给它、也不交给它。
  - 忘掉之前，设备提示"有 N 条只有这台 Mac 解得开"，光标停在"先留着"。
  - 忘掉以后，再给一行"丢掉 N 条解不开的"，光标在"留着"，选了还要确认。不丢的话，这些条目不再算进待传数，所以不会永远占着 200 条的名额，也不会永远挡住 docs/18 的"关闭分档案"（它要求待传都是 0 [源 docs/18:332]）。
- **只封给那台 Mac 的东西怎么办**：
  - 从 Time Machine 恢复，或者用迁移助理搬到新 Mac：私钥文件会跟过来，新 Mac 换了新钥匙但留着旧的，能证明旧 `kid`，设备就交出这些文件（§4.3"交给谁"）。
  - **没有备份，就永远解不开了**。原稿说"钥匙丢了……都不会丢数据"，说过头了。
  - 同一个档案绑了两台 Mac 时，每个文件同时封给这两台，坏了一台，另一台照样解得开。
  - 不做带口令的导出（D7）：它要你记一个口令、管一份文件，而 Time Machine 已经带着私钥；两台 Mac 都绑的话也不怕坏一台。

### 5.5 新 Mac（接 docs/27）

新 Mac 的初始化流程写在 docs/27。跟密钥有关的约定是下面六条：
1. **全新的 Mac**：
   - 初始化时生成 `host_id` 和 `host_hw`，**总是**装 `cpa-key`，在前台跑 `init` 和 `selftest`（docs/27 §2.2 第 7 步）。这一步在 cpad 第一次和设备握手之前做完，那次 `hello` 才带 `hello.seal`。
   - 在 Mac 上点"连上"（docs/27 §3.1，O3）以后第一次握手，设备弹出选择单：这台设备从没分过档案时是 docs/18 §5.1，分过以后是 §5.2。你选"家"或"公司"并确认，就同时登记了这台 Mac 的钥匙。选"不分档案"或本来就不分时，弹的是"让这台 Mac 解开设备上的记录？"，光标在"以后再说"（§4.3）。
   - 从此新记的东西，会封给这台 Mac（和同一档案的其他 Mac）。
   - 之前只封给旧 Mac 的东西，留在设备上等旧 Mac 来取；系统 → 档案 里会写着"等「Mac·8C1D」↑3"。
2. **用迁移助理从旧 Mac 搬过来**：`host_hw` 对不上，`host_id` 换新，SEAL 也换一把新钥匙，旧的留着只解不封（§5.3）。设备会再问一次绑定（docs/18 §4.1）；确认以后，新 Mac 能证明旧 `kid`，旧的待传也能交。
3. **不再用的 Mac**：到设备上忘掉它，需要的话丢掉解不开的（§5.4）；Mac 上用 `cpa host reset` 换掉身份，同时 `cpa-key rotate`（§5.3）。要送人就用 `cpa host reset --wipe-keys`：先提示"`sealed/` 里还有 N 条没解开；设备上只封给这台的，删了钥匙就永远解不开"，要输 `y`，然后 `cpa-key wipe` 删掉 `keys/seal/` 里的全部私钥。
4. **认出设备和 Mac**：Mac 认设备，今天靠 cpad 第一次握手后绑定的 USB 序列号；序列号能伪造（T-ROGUE），所以它只防插错，不防冒充；D10 也只挡从没碰过这台设备的板子（读过它 NVS 的板子照样冒充得了），SEC-7 以后序列号只从验过的 `hello` 记（docs/27 O3 第 4 步跟着改）。设备认 Mac，靠 `host_id` 这个口令；SEAL 以后再加握手证明，这一步是真的认出来。docs/27 的界面不要把序列号说成"已验证"。
5. **初始化本身**：不开新端口；只在取代码和钉死的依赖时联网（安装脚本的 `git fetch --tags`、`uv build`、`uv tool install`），首次设置页本身从不联网。最后一步跑一次 `cpa doctor security`，告诉你 FileVault、Time Machine 加密的情况，以及笔记目录在不在 iCloud 里。
6. **和 docs/27（PR #67）对齐的**（docs/27 `58ed685` 已经改好，下面是约定本身）：
   - 私钥按本文 D6 放在 `$CPA_HOME/keys/seal/` 的 0600 文件里，不是钥匙串；不进导出；换硬件时不删旧钥匙。
   - O4 重新生成 `host_id`、`cpa host reset` 时，同时跑 `cpa-key rotate`（D23、§5.3）。
   - 字段名按 §4.3：`hello.seal{kid, pk, kids, salt}`、`hello` 回复里的 `seal_chal{E′, n, kid}`（`kid` 是设备为这台 Mac 钉住的那把）、`ping.seal_proof[]`。不放进 `prof.ok`。
   - 工具链那一条：ChaCha20-Poly1305 在 libsodium 里有；GCM 是硬件 AES-CTR 加软件 GHASH。

## 6. 安全测试轮 M-SEC（里程碑清单）

跑两轮，结果写进 `docs/gates/sec.md`。自动的部分之后每次发版都跑。
- **基线轮**（SEC-3，现在）：§6.1 全部、§6.2 的 S1-a、S1-b（S1-c 要 docs/18 P8-S 的三台假 Mac，v0.8 以后才有，放进加密轮），真机上只跑 S2-d（coredump 擦除，测的是新写的 D11 代码），顺便量整个 app 录音和传文件时的堆（S2-g，G-ENC 要用）。今天设备上是明文这件事已经知道了，不值得你在场、触发 panic、再用 sudo 镜像整张卡去证明它。
- **加密轮**（SEC-6，SEAL 之后）：全部，要全部通过。S2-a 到 S2-c 合成一个脚本跑一次，只要一次 sudo。
- **新 Mac 部分**（docs/27 §7.2）：第 1 部分只动 Mac、不测加密，在 docs/27 O0–O6 合并后单独跑一次，结果也记进 `sec.md`，之后跟着自动部分每次发版跑；第 2 部分（含迁移换钥匙、`cpa host reset`）并进加密轮的清单。

### 6.1 自动（CI，不用设备）

- [ ] **S0-a 模糊测试**：libFuzzer + ASan/UBSan。用一个普通的 `clang++` 脚本在 ubuntu-24.04 上编，不做成 PlatformIO 环境：Apple clang 没有 libFuzzer 运行时，Unity 的 `main` 也和 libFuzzer 的冲突。挂进现有的 `fuzz-nightly.yml`。目标是所有读卡上文件和主机消息的解析器：`decode_file`、`rec_meta`、`outbox_scan`、`sd_item_name` / `rec_parse_name`、`/cfg` 缓存、`b64`、`line_framer`；SEAL 以后加上 SEAL 头的解析和加密段的断电恢复扫描（收件人上限 4、块数上限 `bytes/4112`）。种子取自 `protocol/vectors`，每晚每个目标跑 10 分钟，语料提交进仓库。通过：0 崩溃，0 条 sanitizer 报告。
- [ ] **S0-b**：现有的 native 测试套件加上 `-fsanitize=address,undefined` 再跑一遍。
- [ ] **S0-c**：daemon 读设备文件和第三方文件的地方，用 Hypothesis（已经是开发依赖）生成恶意输入：`recordings/{contract,land,verify}.py`、`results.py`（别的转写程序写的 status 和 result）、`stt/parse.py`（whisper 的 stdout）、filing 和 box 的 JSON。
- [ ] **S0-d `test_private_modes`**：跑完整个流程以后，遍历 cpa 写过的每个路径，断言目录 0700、文件 0600、没有符号链接、属主是你；cpad 和 cpa-stt 的 launchd 日志都是 0600；两个 plist 里都没有 `Umask`。
- [ ] **S0-e 金丝雀泄漏测试**：用金丝雀字符串把整个流程跑一遍（包括 cpa-stt 中途被 SIGKILL），断言在 `logs/`、`mcp-audit`、`stt/state.json`、launchd 日志、不该带正文的事件里，以及启动清扫之后的 `$TMPDIR` 里，都找不到它；SEAL 以后加上 `sealed/` 里只有密文。
- [ ] **S0-f**：gitleaks，加上 #39 里的标识规则（MAC 占位符、session ID），再加一条：仓库里不许有 Flash 和 SD 的 dump（`*.bin`，`.gitignore` 也加上）。
- [ ] **S0-g**：正式固件里没有 `debug.*`（已经有了：ci.yml:105、release.yml:37），再加两条：没有测试密钥，没有 `CPA_LOG_CONTENT`、`CPA_COREDUMP_KEEP`。

### 6.2 模拟器

- [ ] **S1-a 敌意 SD 卡语料**（`store=sd`）：超长或不合 8.3 的名字、坏 JSON、id 和文件名对不上、超过 256 段、`.M` 自称的字节数比文件大。通过：不崩、不卡住、没提交的东西不交、坏的进隔离区。
- [ ] **S1-b 冒充设备** `fake_device`：陌生的序列号、伪造的已绑定序列号、伪造的 `cpa-fw/`。记下 cpad 在绑定前后分别发出了什么，作为 D10 的基线。
- [ ] **S1-c**（v0.8 以后，加密轮）：docs/18 的 G-PROF-1（三台假 Mac）金丝雀测试照样通过。
- [ ] **S1-d 冒充 Mac**（SEAL 以后）：拿着对的 `host_id` 但没有私钥的假 Mac，封好的东西一样都拿不到，它的 ack 和 `file.del` 不起作用；同一个 `host_id` 换了公钥，设备弹确认、光标在"不换"；**同一个 `host_id`、`hello` 不带 `seal`**，按"未分配"处理，连没封的待传、快照、在线请求都没有；推一个低阶点当公钥，被拒；SD 上塞一份收件人表副本，没有证明就不恢复。
- [ ] **S1-e 卡住的 `cpa-key`**（SEAL 以后）：一个永远不回话的假 `cpa-key`，不能挡住设备的 outbox，也不能挡住没封的条目；`sealed/` 里留着密文，"等解密 N"对得上。

### 6.3 真机 👤（一次一台，你在场，用测试设备）

**dump 的卫生**（和 `backups/` 一样对待）：Flash 的 dump 里有 Launcher 的 `l_wifi` 和 NimBLE 配对，SD 镜像里可能有别的固件的数据。
- dump 写到仓库以外、0700 的目录里；要不要排除在 Time Machine 之外由你定（👤 §8.2 第 4 条）；
- 找金丝雀的脚本只打印偏移和所在区域，不打印内容；
- 用完就删；
- 仓库里禁止 dump（S0-f）。

**金丝雀**：笔记用一个固定的字符串；音频用测试钩子喂一段固定的 PCM 图样（真麦克风录的音调没法按字节找）。

- [ ] **S2-a**（加密轮）：植入金丝雀，同步、删掉，用测试钩子触发一次 panic（编译时关掉 `CPA_COREDUMP_KEEP`），然后 `esptool read_flash 0 0x800000`，在 NVS、cpafs、coredump 和 app 分区里找。一个都不能有。
- [ ] **S2-b**（加密轮）：用读卡器把 SD 卡只读镜像下来（要一次 sudo），在镜像里找，包括空闲簇和 Launcher 的 `/bkp/`。
- [ ] **S2-c**（加密轮）：用 Launcher 的 SD 管理和 WebUI 浏览 `/cpa`（docs/18 T8），只看得到密文。
- [ ] **S2-d coredump**（两轮都跑）：用测试钩子 panic（关掉 `CPA_COREDUMP_KEEP`）→ 重启 → 空闲时擦掉，"上次崩溃"加一；一个 `app_elf_sha256` 不对的 dump 照擦、不计数；录音中从不擦。
- [ ] **S2-e G-ENC**：边录音边封装，测这些：每 4 KiB 块的 AEAD 耗时（AES-GCM 和 ChaCha20-Poly1305 都测）；X25519 耗时（libsodium 和 mbedtls 都测）；握手证明 1 + 5 次的总耗时；最低空闲堆和最大块；`rec_task` 和循环的栈高水位；静态内存；`ring_high`、`mic_missing == 0`；最坏换段时间 ≤ 环形缓冲的余量；**刚绑定或刚切档案就开录**；冷启动离线时种子的最小熵（SP 800-90B）；断电后按整块截断的恢复（在加密段上重跑 G-A8）。
- [ ] **S2-g 堆基线**（基线轮）：整个 app 在录音和传录音时的最低空闲堆和最大块（docs/07 §6.7 第 6 条），G-ENC 拿它当对照。

原稿的 S2-f（锁屏时挡 USB 复位）去掉，研究项停放（§4.4）。

### 6.4 Mac

- [ ] **S3-a `cpa doctor security`**（只读）：检查 FileVault；Time Machine 的目标和加密（盘没挂上就说"不知道"，没加密就警告：备份里有归档的明文和私钥）；`tmutil isexcluded`；笔记目录和导出在不在 iCloud 里；文件权限，包括 `keys/` 和 `sealed/`；"等解密 N"；cpa-stt 的临时残留；`cpa-key` 的属主和签名；`spawn_env` 里像密钥的键。有 `--json` 输出，Mac app 把它显示成一张卡片。
- [ ] **S3-b** 👤：你看一眼 Time Machine 是不是加密的。
- [ ] **S3-c**（SEAL 以后）：扫一遍 `ps`，密钥不出现在任何进程的 argv 里；私钥文件 0600；`cpa export` 和设置导出里没有 `keys/`。
- [ ] **S3-d**（docs/25 R1 以后）：docs/25 §5.3 `ClipWriter` 的测试在这里重跑一遍：只给本机、带隐藏和临时类型、2 分钟后清；退回 `pbcopy` 时要确认。

### 6.5 Mac app（docs/24，等它合进 main）

- [ ] **S4-a**：源码规则测试（`RulesTests`）加几条：app 不读 `$CPA_HOME/keys`，不调用 `security`；安全卡片只来自 `cpa doctor security --json`，第一行是"加密：开（封给 …）"。
- [ ] **S4-b**：`ProcessTests` 里已有的环境变量白名单、超时、4 MiB 上限都保持。

### 6.6 加密本身（SEAL 以后）

- [ ] 已知答案向量放在 `protocol/enc/v1/`，设备的 native C++ 和 Mac 的 `cpa-key` 都要跑；HKDF 用 RFC 5869 的向量，X25519 用 RFC 7748 的，包括低阶点要得到全 0、被拒。
- [ ] 性质测试：同一个 `fk` 下每个块序号只加密一次；只有最后一块不满；断电恢复从不重新加密；打开失败重试换新 `fk`；换了收件人解不开；改动任何一个字节都解不开；截断时只丢最后那块不完整的；调换段的顺序、把别的录音的段拼进来、改 `prof`、改 `.M` 的字段，都被发现。
- [ ] 公司的钥匙打不开家里的文件（扩展 G-PROF-1）。
- [ ] 握手证明：错的私钥什么都拿不到；证明过的 Mac 只能 ack 和删收件人里有它的条目。
- [ ] 先存后解：只有密文落盘、哈希对上了才 ack；用假的 `cpa-key` 模拟失败、超时、被杀，`sealed/` 留着，outbox 照常走。
- [ ] `cpa-key` 的头和容器解析：随机和变异的输入（Swift 测试），不崩、不越界。
- [ ] 加密段的断电恢复扫描（用 `rec_recovery` 的断电注入）。
- [ ] 降级：老固件开机扫描遇到 `.sea` 和 `.E?`、`.V?`，原样留着，不进 `bad/`。

## 7. 分期和工作包

一律先写失败的测试（RED → GREEN → REFACTOR）。

| 期 | 内容 | 动哪里 | 代码 / 测试（行）[算] | 依赖 | 等你吗 |
|---|---|---|---|---|---|
| **SEC-0 Mac 小修和文档改正** | §4.6 小修的第 1–5 条；§3.3 对 SECURITY.md 的改正；launcher.md:217 | `cpad/service/launchd.py`、`stt/service.py`、`cpad/config/settings.py`、`stt/runner.py`、`cpad/logging/redact.py`、`hook_settings.py`、`test_control_service_logging.py`；文档 | 110 / 240 | 无 | 不用 |
| **SEC-1 自动测试和体检** | §6.1 全部；`cpa doctor security`（含 `spawn_env` 警告） | 模糊测试的 `clang++` 脚本和目标、`fuzz-nightly.yml`、`ci.yml`；新 `cpad/cli/doctor.py` | 400 / 1,000 | SEC-0 | 不用 |
| **SEC-1b Mac app 的安全卡片** | §6.5 | `mac/` | 50 / 100 | docs/24 合进 main | 不用 |
| **SEC-2 设备卫生和模拟器** | coredump 擦除（D11）；§6.2 的 S1-a、S1-b | 固件的开机和空闲流程；模拟器的模糊测试语料；`daemon/tests/support/fake_device.py` | 180 / 450 | 无 | 按 D11 默认做 |
| **SEC-3 基线轮** 👤 | §6.1、§6.2 的结果；真机 S2-d 和 S2-g | `tools/hil` 的脚本；`docs/gates/sec.md` | 40 / 60 | SEC-1、SEC-2 | 要你在场一次，不长 |
| **SEC-4 闸门 G-ENC** | §6.3 S2-e 的探针固件和测量 | 测试环境 | 250 / 150 | SEC-3 的堆基线 | 👤 第 1 条 |
| **SEC-5 SEAL（= docs/18 P8-E）** | `protocol/enc/v1`（spec、向量、检查器、握手证明）；固件 `fw/seal/`（纯函数核心 + HAL）：登记和收件人表、镜像、时段钥匙、握手证明、`RecWriter` 暂存区、`.V`/`.E`/`.sea`、`.M` 加字段、所有 `44 + bytes` 的地方、收件人位图、明文兜底、系统 → 加密、忘掉和丢掉、清理内部存储；`cpa-key`（Swift）；cpad：`seal` 字段、`sealed/` 和解密循环、`verify.py` 和 `land.py`、`cpa-key` 的看管和超时、报警、`cpa status`、`cpa sd scrub` | 协议、固件、`mac/`、`cpad/recordings/`、`cpad/protocol/` | 协议 200 加向量；固件 1,300 / 1,500；Swift 350 / 450；cpad 650 / 900 | G-ENC 通过；docs/18 P8-F1、P8-F2（绑定和 `prof`）、P8-F3（确认框、忘掉和丢掉的行、系统 → 档案；docs/27 O7 的最小确认框够用时就是它）；docs/24（`mac/` 包） | 👤 第 1–3 条 |
| **SEC-6 加密轮** 👤 | §6 全部重跑，要全部通过；S2-a 到 S2-c 的一个脚本 | `tools/hil` | 120 / 200 | SEC-5 | 要你在场 |
| **SEC-7（可选）认证密钥** | D10：`hello` 里的 HMAC 挑战（docs/05 §8 的 L2）；SD 上的条目和 `.M` 带 MAC，验不过的进隔离区；只从验过的 `hello` 记序列号（改 docs/27 O3 第 4 步：绑定时从 USB 列表写的序列号，验过才算数） | 协议、固件、cpad | 400 / 500 | SEC-5 | D10 |

**合计** [算]：SEC-0 到 SEC-3 约 780 行代码、1,850 行测试，不等任何决定（SEC-2 的 coredump 按默认做；SEC-1b 等 docs/24）。SEC-4 到 SEC-6 约 2,870 / 3,200（G-ENC 探针计入代码）。固件 1,300 / 1,500 比原稿的 900 / 1,100 多三到四成：改动面包括 rec 的大小算法、第二种文件类型、编解码、NVS 延迟写、收件人位图和界面。M-SEC 第一轮可以跟下一次发版一起走；第二轮排在 v0.8（档案）之后，作为 v0.9 的主题 [设]。

```
SEC-0 ─► SEC-1（之后每次发版都跑）─┐
SEC-2 ─────────────────────────────┴► SEC-3 基线轮 ══ M-SEC 第一轮
docs/24 合进 main ─► SEC-1b
docs/27 O0–O6 ─► 新 Mac 第 1 部分（单独跑，记进 sec.md）
docs/18 P8-F1、F2、F3（或 docs/27 O7）─┐
docs/24 合进 main ─────────────────────┤
SEC-3 ─► SEC-4 G-ENC ──────────────────┴► SEC-5 SEAL ─► docs/27 O8 ─► SEC-6 加密轮（含新 Mac 第 2 部分）══ M-SEC 第二轮 ─► SEC-7（可选）
```

## 8. 决定和要你拍板的事

### 8.1 决定

不回复就按默认做。

| # | 决定 | 默认（推荐） | 备选 | 为什么 | 见 |
|---|---|---|---|---|---|
| D1 | "所有数据都文件加密"指什么 | 设备上逐个文件封装；Mac 上整盘加密，备份也加密；Mac 上的文件对你自己的 app 开放 | Mac 上也逐个加密 | 逐个加密挡不住经 cpa/MCP 读的程序，却挡住了你自己的 app | §1.2、§4.6 |
| D2 | 设备的加密方案 | SEAL：每个文件封给你在设备上点过头的 Mac 的公钥，设备不存秘密 | 对称密钥放 NVS；对称密钥只放内存 | 没有 Secure Boot，NVS 里的密钥等于没有；只放内存的话，离线重启后就录不了 | §4.2、§4.3 |
| D3 | 什么时候做 | 小修、自动测试、基线轮现在做；SEAL 排在 docs/18 v0.8 的绑定和 docs/24 的 Mac app 包之后，先过 G-ENC | 并进 v0.8 | 分档案时收件人要先有绑定；`cpa-key` 要放进 docs/24 的包；G-ENC 还没测过 | §7 👤 |
| D4 | 格式 | X25519（libsodium）+ HKDF-SHA256 + AES-256-GCM，4 KiB 一块，块的 AAD 是头的哈希；时段钥匙 ≤1 小时，收件人或档案一变就换；每个文件最多 4 个收件人（`cpa-seal/v1`，spec 和向量公开） | age v1；AEAD 换成 libsodium 的 ChaCha20-Poly1305（G-ENC 定） | age 一块 64 KiB 要 64 KiB 缓冲，没有 PSRAM 放不下，标准工具也读不了截断的流；每个文件只做 HKDF 和包装，不在 Enter 或开录时等 X25519 | §4.3 |
| D5 | Mac 端怎么解密 | Swift 的 `cpa-key`（CryptoKit，和 Mac app 同一个包、单独的目标）；daemon 仍只依赖 pyserial | ADR 0004 允许 `cryptography`；外部的 `age` | 零第三方代码；ADR 0004 第 1 条不改 | §4.5 👤 |
| D6 | 私钥放哪 | `$CPA_HOME/keys/seal/`，0600；不进导出；跟着 Time Machine 和迁移助理走 | 登录钥匙串（后台不弹框、解不开就排队，前台 `cpa-key authorize`） | ad-hoc 签名每次编译都变，钥匙串会在后台同步时突然要登录密码；没加密的 Time Machine 里两者差不多 | §5.2 |
| D7 | 收件人 | 你在设备上点过头的 Mac：分档案时按档案，档案 0 和不分档案时给全部；设备上最多 4 台；不加恢复钥匙，不做导出 | 只在绑定以后才有公钥（原稿）；默认加一把恢复钥匙；`cpa-key export` | 原稿在"不分档案"时永远是明文；Time Machine 和迁移助理会带上私钥文件；恢复钥匙和导出都要你记口令、管文件 | §4.3、§5.4 |
| D8 | 没有钥匙时 | 只在设备上一台登记过的 Mac 都没有、或当前档案里没有登记了钥匙的 Mac 时照录明文，格式和今天一样，从不封给别的档案的 Mac；系统页总写"加密：开/关"或"<档案> 没有钥匙"；开过 SEAL 后（`SEALON` 在每个存储上）又落回明文，提示加锁屏"明"字，收件人表丢了也一样 | 拒绝录 | 录音总能录，比加密更要紧 | §4.3 👤 |
| D9 | 设备看不到自己的正文，回放怎么办 | 👤 三选一，默认 (a)：加密的录音不能在设备上回放 | (b) 设备自己也当一个收件人：能回放，挡得住只拿到卡的人和 Launcher 浏览 SD，挡不住丢设备；(c) 只有加星的或最近几条留明文 | docs/13 v0.5 答应过"离开 Mac 也能听录音"，docs/18 §15 第 2 条只把丢回放列成代价、没定；(b) 以后能加，格式不用改 | §4.3、§8.2 👤 |
| D10 | 认证密钥（防伪造） | 做，排在 SEAL 之后（SEC-7）；写明它只挡只拿到卡的人和从没碰过设备的冒充板 | 和 SEAL 一起做；不做 | NVS 里的钥匙，设备插过的每台主机都读得到，每台 Mac 一把也一样；提示注入的大头是会议里真说的话，靠 docs/15 §3.7 | §3.1、§7 |
| D11 | coredump | 正式固件在空闲、不录不传时，有内容才看：本固件的计数，别的固件的不计；按 4 KiB 一块擦；测试固件看 `CPA_COREDUMP_KEEP` | 都保留；把分区表里的 coredump 删掉 | 崩溃时的栈里有正在打的字；Launcher 下分区是共用的；擦 Flash 会让录音丢音 | §4.4 |
| D12 | 删后的残留 | 靠 SEAL；没有开关，有钥匙就封；旧明文照常同步，之后提示一次：设备上清内部存储，卡用 `cpa sd scrub` | 删除前先覆盖写；格式化 SD 卡 | FTL 让覆盖不可靠；macOS 给大卡格式化成 exFAT，IDF 4.4 读不了，还会删掉 Launcher 的文件 | §4.3 |
| D13 | Mac 上的归档 | FileVault + 加密的 Time Machine；加密磁盘映像只写做法 | 逐个文件加密；把磁盘映像做成功能 | ADR 0004 的开放文件；挂载的生命周期会让 cpad 和 cpa-stt 不稳 | §4.6 |
| D14 | Spotlight | 照旧索引 | 排除 | 你要搜得到；索引在 FileVault 里；要改就得动系统设置 | §4.6 |
| D15 | 忘掉 / 粉碎 | 现在不做，"文字永久保留"不变；写明 `rec.hidden` 只藏不删 | 做 `cpa forget`（重写 `events.jsonl`） | 只追加的日志是归档的根；要改得你先决定 | §2.2 👤 |
| D16 | 日志里的正文哈希 | `cpad.log` 里改成带本机盐的 HMAC，字段改名 `h8`；事件里的 `sha8` 不动 | 照旧；`sha8` 原名改成 HMAC | 不加盐的话短笔记能被猜出来；`sha8` 在 docs/22、docs/23 里是事件字段，`inbox_start` 要拿它比对 | §4.6 |
| D17 | 不可信的 USB 主机 | 写明 SEAL 只防被动读；做几条便宜的检测；锁屏挡 USB 复位和 JTAG 只停放成研究项，不进 M-SEC | 现在就研究 | JTAG 不用复位就能读内存，光挡复位没用；两位都没核实 | §4.4 |
| D18 | 剪贴板 | 照 docs/25 §5.3、D9（它说了算）：`ClipWriter` 经 ctypes 只给本机、标成隐藏和临时类型、2 分钟清；本文只在 M-SEC 核对（S3-d） | — | Universal Clipboard 和剪贴板管理器都是副本 | §4.6 |
| D19 | 测试轮怎么跑 | 基线轮只跑自动的、模拟器的（S1-c 除外）、真机 coredump 擦除和堆基线；加密轮全跑，读 Flash 和镜像 SD 卡合成一个脚本、一次 sudo；docs/27 §7.2 的第 1 部分在 O0–O6 后单独跑，第 2 部分并进加密轮；结果写进 `docs/gates/sec.md` | 两轮都全跑；只在加密后跑一轮 | 今天是明文是已知的，不值得你花时间去证明；加密轮才有对照 | §6 |
| D20 | Mac 一时解不开 | 密文先存进 `$CPA_HOME/sealed/`（0600），落盘了、哈希对了、收件人里有自检过的钥匙，就回 ack 或发 `file.del`；解密另外跑 | 解不开就不回 ack（原稿）；加一个 `held` 回执 | 不回 ack 会让 outbox 4 条在途全堵住、卡被录音塞满；`held` 要改协议和设备状态 | §4.3 |
| D21 | 设备怎么信 Mac 的公钥 | 只在你在设备上点头时收下，按 `hh` 钉住；每次握手 Mac 证明有私钥，证明了才交封好的、才认它对封好的条目的 ack 和删；钉住了钥匙的 Mac 证明之前（不带 `seal` 也一样）按"未分配"处理，什么都不给；换钥匙要用旧钥匙证明，否则设备上确认、光标在"不换" | 认 `host_id` 就收公钥、直接替换（原稿） | `host_id` 明文发给第一次握手的任何板子，拿到它就能把以后的记录改封给自己，还能删掉它读不了的 | §4.3 |
| D22 | 忘掉一台 Mac 以后 | 再给一行"丢掉 N 条解不开的"（光标在"留着"，要确认）；不丢的也不再算待传 | 一直留着、一直算 | 不然永远占着 200 条的名额和卡的空间，还会永远挡住 docs/18 的"关闭分档案" | §5.4 |
| D23 | 换了硬件（迁移助理）、`cpa host reset` | `host_id` 换新，SEAL 也换一把新钥匙（`cpa-key rotate`），旧的留着只解不封；送人用 `cpa host reset --wipe-keys` 删掉私钥 | 私钥原样跟过去用 | 新 Mac 不拿旧 Mac 的钥匙当在用的。但迁移过去的旧私钥仍能解开封给旧 Mac 的东西：旧 Mac 改作公司用之前要跑 `cpa host reset`，家和公司在密码学上才分开 | §5.3 |

### 8.2 要你拍板

1. **设备加密和回放**（D2、D3、D9）：**默认**在 v0.8（档案）和 Mac app 之后做 SEAL，先过 G-ENC。设备看不到自己存下的正文，所以 v0.5 答应的"离开 Mac 也能听录音"要三选一：
   - (a) **默认**：加密的录音不能在设备上回放；
   - (b) 设备自己也当一个收件人：能回放，挡得住只拿到卡的人和 Launcher 浏览 SD 卡，但丢了设备，录音就是明文；
   - (c) 只有加星的或最近几条留明文，其余加密。

   (a) 以后想改成 (b)，格式不用变。在 SEAL 做好之前，去公司或别人的电脑前，先在家同步清零。
2. **没有钥匙时**（D8）：**默认**照录明文，带标记；只在你还没对任何一台 Mac 点过头时才会这样。备选：不让录。
3. **Mac 端解密**（D5）：**默认**用 Swift 的 `cpa-key`，和 Mac app 一起造，ADR 0004 不改。备选：改 ADR，允许 `cryptography`。
4. **Time Machine**：请你看一眼备份盘是不是"已加密"。SEAL 以后解密用的私钥文件也在备份里（D6）；没加密的话，捡到备份盘的人拿到的是归档的明文副本加上这把钥匙。开发产物目录（`backups/`、`firmware/hwtest/captures`）和真机测试的 dump 要不要排除在备份之外，也由你定。
5. **"忘掉"**（D15）：要不要有一个办法，真正删掉某条笔记的正文？今天的规矩是"文字永久保留"，`events.jsonl` 只追加。**默认**：不做。

## 9. 三份评审怎么处理的

三份评审（工程、攻击者、可行性）都对着 `d330465` 的代码核对过；下面的源码引用本文又核对了一遍。"分歧"一栏写的是评审之间说法不同时怎么定的。

| 评审 | 说的 | 怎么改的 |
|---|---|---|
| 工程 1、可行性 3 | 解不开就不回 ack，outbox 会被堵死，卡被塞满 | 先存密文再解密（D20、§4.3）。**分歧**：可行性提议加一个 `held` 回执；定成本机存密文，因为它不改协议、不改设备状态，`held` 能解决的它都解决了 |
| 工程 2、3，攻击者 11，可行性 11 | 私钥放钥匙串会在后台弹框；和 docs/27 冲突；迁移助理以后两台 Mac 同一把钥匙 | 私钥改成 0600 文件（D6、§5.2）；换硬件自动换钥匙、旧的只解（D23）；一定在初始化时生成；一个字段名、不做导出（D7）。**分歧**：可行性提议保留钥匙串、后台不弹框、前台授权；定成文件，因为每次重编都要你回 Mac 前点一次，不合"方便和稳定优先"，而钥匙串在没加密的 Time Machine 里也只靠登录密码；钥匙串的做法写成备选。docs/27（PR #67）原来写成钥匙串，已在 `58ed685` 改回文件（§5.5 第 6 条） |
| 工程 4，攻击者 3 | 不分档案、档案 0、NVS 丢了时永远是明文 | 登记和档案分开；档案 0 和不分档案时封给全部；表在 cpafs 有镜像；只在一台都没登记时才明文（D7、D8）。**分歧**：工程说"明"字只在落回明文时显示，攻击者说开过 SEAL 后每次落回都要提示；两条合成一条：只在开过 SEAL 以后落回明文时提示，系统页总写开还是关 |
| 工程 5，可行性 14 | D9 丢掉了 v0.5 的回放，理由不对 | D9 改成 👤 三选一（§8.2 第 1 条）。**分歧**：可行性提到"每个档案一个 SEAL 开关"；没采用，因为工程 9 要求不设开关，(b)、(c) 已经覆盖了想要回放的情况 |
| 工程 6，攻击者 6，可行性 6 | 预先算好的头用不了（HKDF 绑了 fid），用光了 Enter 和开录会卡；复用会重复 nonce；X25519 不能在 `rec_task` 里 | 改成时段钥匙：每个文件只做 HKDF 和包装（D4、§4.3）；收件人或档案一变立刻换；`fk` 每次打开都新取；X25519 只在循环里；"刚绑定就开录"进 G-ENC。**分歧**：攻击者和可行性的做法是"每个文件一把临时钥匙、池子按收件人分"；定成时段钥匙，因为每个文件一把挡不住的那种人（接着 JTAG 读内存）同时在读麦克风缓冲，而时段钥匙最简单、最省电 |
| 工程 7 | 老固件会把加密条目挪进 `bad/` 删掉；`enc` 有两种类型 | 加密条目换文件名；没封的不带 `enc`（§4.3） |
| 工程 8，攻击者 4 | 忘掉以后，只封给它的条目永远卡着；"不会丢数据"说过头 | 加"丢掉 N 条解不开的"，不再算待传（D22）；老实写明钥匙真丢了就解不开（§5.4） |
| 工程 9 | 格式化卡危险；没有 SEAL 开关；漏了 Launcher 的 `/bkp/` | 不设开关；`cpa sd scrub` 代替格式化；`/bkp/` 进地图和 S2-b（D12、§2.1、§4.3） |
| 工程 10，攻击者 13，可行性 13 | plist 里的 `Umask` 会传给子进程；对已有文件没用；cpa-stt 的 plist 也一样 | 不加 `Umask`；两个 `install()` 都预先建 0600 的日志，启动时 chmod（§4.6 第 1 条） |
| 工程 11，攻击者 13，可行性 13 | `spawn_env` 读配置时报错会让 cpad 起不来；"留最近 5 份"会删掉原件；`sha8` 撞名 | 改成 `cpa doctor` 警告、放到 SEC-1；留第一份加最近 2 份；字段改名 `h8`、事件不动（§4.6、D16）。可行性说"没有别的代码读 `sha8`"也对，改名只是避开 docs/22、docs/23 里的事件字段 |
| 工程 12 | 基线轮让你花时间证明已知的事 | 基线轮只跑自动、模拟器、S2-d 和堆基线；S2-a 到 S2-c 只在加密轮跑一次（D19、§6） |
| 工程 13 | Launcher 下 coredump 分区是共用的 | 按 `app_elf_sha256` 只数自己的；录音和传文件时不擦（D11） |
| 工程 14，可行性 7 | 收件人多了一行会超 2048 B；转义后的正文翻倍 | 最多 4 台；封二进制的小块；`test_worst_lines` 加封好的条目（§4.3）。**分歧**：工程说每个档案 3 台，可行性说每个文件 4 台；定成设备上最多 4 台，这样不管分不分档案，每个文件都 ≤4 |
| 工程 15 | `json_read` 这个模糊测试目标不存在；加一行"加密：开（封给 …）" | 删掉；系统 → 加密 和 Mac app 的卡片都加这一行 |
| 攻击者 1 | 拿到 `host_id` 就能换掉设备存的公钥，任何 Mac 都能删它读不了的文件 | 公钥钉住、每次握手证明私钥、证明了才交和删（D21、§4.3），和 docs/27 §3.5 第 1、2 条一致；改正"拷走 `$CPA_HOME` 拿不到绑定"（§5.1）。**分歧**：攻击者另提"`file.del` 带一个只有解密过才知道的原像"；没采用，因为先存密文再解密（D20）时，Mac 在解密之前就要回 ack，证明有私钥已经够了 |
| 攻击者 2，可行性 10 | 没有考虑存心改设备的 USB 主机；USB-JTAG 不用复位就能读内存；挡复位的研究没意义 | 全文改成"只防被动读"；加几条便宜的检测（§4.4、T-USB）。**分歧**：可行性说删掉研究项，攻击者说扩大到 USB-JTAG；定成停放，两位都写上，不进 M-SEC（D17） |
| 攻击者 5，可行性 1 | `.M` 的明文字段没有认证；封 `.M` 的字段会破坏断电恢复、`file.list` 和 512 B 上限 | `.M` 什么都不封，`kMetaMax` 改 768；块的 AAD 是头的哈希，最后一块的标志里带"最后一段"，`.M` 被改 Mac 能发现（§4.3）。两份评审的办法合在一起用 |
| 攻击者 7，可行性 5 | 种子只在冷启动离线时最弱；主机的盐对发盐的主机没用；"不会相同"不等于"猜不出"；全 0 的共享密钥 | 种子在 `setup()` 最开头取一次；混进麦克风噪声；SP 800-90B 估计；两边拒绝全 0（§4.3） |
| 攻击者 8 | D10 的作用说大了 | D10 和 T-CARD 改写（§3.1、D10） |
| 攻击者 9，可行性 12 | 真机的 dump 和 `backups/` 一样敏感；音调没法按字节找 | dump 的卫生四条；音频用固定的 PCM 图样（§6.3） |
| 攻击者 10 | 迁移以后两台 Mac 一把私钥 | 换硬件自动换钥匙（D23）；设备发现同一把旧钥匙时问要不要忘掉旧的（§5.3） |
| 攻击者 11 | 钥匙串的几条说法没验证；导出要口令派生 | 默认不用钥匙串（D6）；不做导出，口令派生的问题就没了（D7） |
| 攻击者 12 | 模糊测试漏了两个解析器 | 加上 SEAL 头、加密段恢复扫描和 `cpa-key` 的解析（§6.1、§6.6） |
| 攻击者 14 | T-CLOUD 漏了 MCP 客户端的模型提供商和 docs/25 的云端去向 | 补进 §2.3、T-CLOUD、§3.3 |
| 可行性 2 | 4 KiB 一块只对第一块成立；不能在环形缓冲里原地加密；"头先 fsync"太贵 | `RecWriter` 加暂存区；从暂存区加密；去掉"头先 fsync"；`sha256` 从第 0 字节算（§4.3） |
| 可行性 4 | 录音时擦写 Flash 会丢音 | 收件人表、计数、coredump、清理都等空闲；coredump 有内容才按块擦（§4.3、§4.4）。**分歧**：可行性另提"删掉分区表里的 coredump"；没采用，因为 Launcher 下照样要擦，一条路更简单 |
| 可行性 5 | GCM 不在硬件里；libsodium 已经链接、有 ChaCha；age 真正的问题是 64 KiB 缓冲；`bootloader_random` 只能在 ADC 之前用 | 全部按实情改（§4.3、D4）；ChaCha20-Poly1305 留作 G-ENC 的备选 |
| 可行性 8 | SEC-1 和 SEC-5 依赖还没合的 docs/24；G-ENC 缺整机堆基线；S2-a 自相矛盾；模糊测试的环境 | 拆出 SEC-1b；SEC-3 量堆基线；加 `CPA_COREDUMP_KEEP`；用 `clang++` 脚本和现有的 `fuzz-nightly.yml`（§6、§7） |
| 可行性 9 | 代码量估少了 | 固件改成 1,300 / 1,500，cpad 650 / 900（§7） |
| 和 docs/27 交叉核对（2026-10-22） | 两份文档并行写，对着 docs/18、docs/25 的 main（`995821d`）核对后，本文有几处没写或前后不一 | 只补空白、改矛盾：收件人表按 `hh` 存、约 470 B（§4.3"存在哪"）；钉住了钥匙的 Mac 不带 `seal` 或没证明时按"未分配"什么都不给；设备在 `seal_chal{E′, n, kid}` 里报钉住的 `kid`，不放进 `prof.ok`；visible(H) 套用 docs/18 §3.5 第 1–3 条（"交给谁"）；已绑定、没登记钥匙的 Mac 也弹"让…解开"，两种模式都有"加上"；当前档案里没有钥匙就明文带"明"，不封给别的档案；`SEALON` 和表写到每个存储上，SD 上的那份证明了才恢复；指纹改成 `kid` 前 8 位、Mac 上也显示；`body` 全部字段都封，`.M` 加上 docs/25 的 `dest`、`lock`；cpad 在 `cpa-key` 变了时重跑自检；`kid` 一个都不认识时照 D20 不存不 ack；`rotate` 没有 `--keep-old`，`cpa host reset` 也换钥匙，送人用 `--wipe-keys`，D23 写明分开要靠旧 Mac 跑 reset；剪贴板归 docs/25；基线轮不跑 S1-c；SEC-5 加 P8-F3；§6 和 §7 加上 docs/27 的新 Mac 部分；`host_label` 默认 `Mac·XXXX`、例子里的名字跟着改；联网的说法改准 |

## 附：证据索引

- 固件（`d330465`，main 上没变）：`fw/hal/nvs_boot.cpp:26-43`；`fw/hal/lfs_store.cpp:108`；`fw/hal/sd_card.cpp:202-204`；`fw/hal/vfs_rec_file_port.cpp:125-127`；`fw/hal/audio_task.h:2、43`；`fw/rec/rec_engine.cpp:127-129、251`；`fw/rec/rec_engine.h:78`；`fw/rec/rec_meta.h:17、46、52`；`fw/rec/rec_recovery.cpp:25-27`；`fw/rec/rec_recovery.h:48`；`fw/rec/wav_header.h:4、38`；`fw/storage/sd_paths.h:1-15`；`fw/outbox/outbox_io.h:1-7`；`fw/outbox/outbox_core.h:1-7`；`fw/outbox/outbox_scan.h:8-11`；`fw/protocol/decode_device.cpp:11、82`；`fw/shell/runtime_outbox.cpp:15-17`；`src/main.cpp:30、76`；`platformio.ini:36`；`partitions/cpa_8MB.csv`；`test/core/test_rec_meta/test_main.cpp:104`。
- daemon：`cpad/service/launchd.py:20、44、50-60`；`stt/service.py:31-34`；`cpad/config/settings.py:3-6、56-61、86-89、157-166`；`cpad/logging/redact.py:1-22`；`cpad/events/records.py:23-80`；`cpad/recordings/land.py:1-4`；`cpad/recordings/verify.py:7、17、37`；`cpad/recordings/retention.py:1、25-28`；`cpad/agents/claude_code/hook_settings.py:5-8、29`；`stt/runner.py:87-93`；`pyproject.toml`（`["pyserial==3.5"]`；开发依赖有 `hypothesis`）。
- 协议：`protocol/README.md:201、224`；`protocol/tools/test_worst_lines.py:100`。
- SDK（`SDK/`）：`qio_qspi/include/sdkconfig.h:424、565、567、568、603、622`；`include/mbedtls/port/include/mbedtls/esp_config.h:127`（`MBEDTLS_GCM_ALT`）；`include/nvs_flash/include/nvs_flash.h:191`；`include/bootloader_support/include/bootloader_random.h:28-40`；`include/espcoredump/include/esp_core_dump.h:41、114、134、158`；`nm libnvs_flash.a`、`nm libmbedcrypto.a`（`esp_aes_gcm_crypt_and_tag`、`gcm_mult`、`mbedtls_ctr_drbg_seed`，没有 HKDF、没有 ChaCha）、`nm liblibsodium.a`（`crypto_scalarmult_curve25519`、`crypto_aead_chacha20poly1305_ietf_encrypt`、`crypto_auth_hmacsha256`、`sodium_memzero`）[测]；`soc/rtc_cntl_reg.h` 的 `RTC_CNTL_USB_CONF_REG`、`RTC_CNTL_USB_RESET_DISABLE`。
- 文档：SECURITY.md:9-11、26、28、35、41-42、57、62；docs/04:632（S11）、751（D16）；docs/11:220-236、242；docs/12:455；docs/13:38、425、695；docs/15:57、520；docs/18 §3.2、§4.1–§4.3、§5.2–§5.3、§8、§9、§11、§13、§15，以及 :105、223、332、568、704；docs/21:88-91、336；docs/22:611；docs/23 §6.1、:224、D10；docs/24 §7、D12（`wt-mac`）；docs/25 §2.3；docs/27 §3.5（PR #67）；docs/gates/launcher.md:15、48、50、114、138、162、203、217；ADR 0004:57；录音契约 v1 `segments[].sha256`；CI `ci.yml:105`、`release.yml:37`、`fuzz-nightly.yml`。
- 本机 [测]（2026-10-22，只读）：`fdesetup status`；`$CPA_HOME` 和 `~/CardputerNotes` 的权限和文件名；`config.json` 的键名；`tmutil destinationinfo`、`tmutil isexcluded`；`mdfind -onlyin`（只看文件名）；iCloud 目录是否存在；`firmware/hwtest/captures` 的数量和权限。
