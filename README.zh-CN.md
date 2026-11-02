[English](README.md) · **中文**

# Cardputer Pocket Assistant（占位名）

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

## 这是什么

把 M5Stack Cardputer ADV 变成 Mac 的"口袋副屏 + 速记键盘"：

- 一眼看到每个项目里哪个 Claude Code session 在跑、哪个在等你；
- 随手记下想法或反馈，归到对应项目的队列，或者确认后直接给这个项目开一个新 session；
- 离线时照样能记，插回 Mac 就自动同步。离线记下的东西**只会变成笔记或排队项，绝不会自动执行**。

文档以英文为主，大多数页面旁边有中文版（`*.zh-CN.md`，第一行有链接）。设备（设置 → 语言，Opt+M 打开设置）和 Mac app 都能在中文和英文之间切换。

> 状态：v0.5.2 已发布（[发布说明](docs/releases/v0.5.2.zh-CN.md)）：设备默认英文（中文在设置里一键切换），用电池黑屏时会歇着，大批「再交一次」「清掉留底」不再触发看门狗重启；录音留在设备上，直到对的 Mac 来拿（[docs/28](docs/28-multi-mac-sync.zh-CN.md) S1a）。计划见 [docs/13](docs/13-assistant-v2-plan.zh-CN.md)，按键见下面的[按键](#按键)。`cpa`、`cpad`、`cpa-fw` 和仓库名都是占位名，首次公开发布前统一改（[04 §16](docs/04-prd.zh-CN.md) 第 9 条）。

## 三块代码，一份契约

| 目录 | 内容 | 状态 |
|---|---|---|
| [`protocol/`](protocol/) | NDJSON 协议 v0 的机器可读契约：`limits.json`、JSON Schema、测试向量、`check_vectors.py` | WP1 |
| `firmware/app/` | 设备固件 `cpa-fw`（C++17，PlatformIO） | WP2 / WP4 |
| `daemon/` | Mac 端 daemon `cpad` 与命令行 `cpa`（Python 3.12 + pyserial） | WP3 |
| [`firmware/hwtest/`](firmware/hwtest/) | 硬件探测固件，Day-0 闸门用，保持不改 | 已有 |
| [`firmware/third_party/SD/`](firmware/third_party/SD/) | 打了补丁的 Arduino `SD` 驱动（Apache-2.0，见 [NOTICE](NOTICE)），app 和 hwtest 共用 | v0.2 |
| [`tools/repo/`](tools/repo/) | 仓库级检查（文件行数） | WP1 |

固件和 daemon 只通过协议耦合。协议规范以 [docs/05 §6](docs/05-architecture.zh-CN.md) 为唯一真相源，`protocol/` 是它的机器可读版本，两端的契约测试都读同一份向量。

## 安装（一行命令，v0.5.0 起）

在一台 Apple 芯片、macOS 14 以上的 Mac 上，终端里粘贴一行。**再跑一次就是升级**，坏了的安装也会修好。

```bash
curl -fsSL https://raw.githubusercontent.com/h3qing/cardputer-assistant/main/scripts/install.sh | bash
```

**U 盘，不联网**：在主力 Mac 的仓库里（tag 的那个提交上）造好发布、拷进 U 盘，再到新 Mac 上从 U 盘装：

```bash
scripts/release-local.sh --tag v0.5.0 --out /Volumes/USB/v0.5.0 --release-json   # 主力 Mac
bash /Volumes/USB/v0.5.0/install.sh --from /Volumes/USB/v0.5.0                    # 新 Mac
```

`--from` 的文件照样逐个核对 `release.json` 的摘要和 `SHA256SUMS`。uv 和 Python 3.12 仍要联网下载一次（约 70 MB）；新 Mac 上已经有 uv 和 Python 3.12 就完全不用网。

它做的事（每一步先说要做什么，默认英文，`--lang zh` 时是中文）：

- 从最新的发布下载 cpa 的 wheel、钉死的 pyserial、Mac app，**每个文件都和 GitHub 给的 SHA-256 以及发布里的 `SHA256SUMS` 对一遍**，对不上就停，什么都不装；带隔离属性的文件也拒绝。
- `cpa`、`cpa-stt` 装成 uv 工具，链接在 `~/.local/bin`（没有 uv 就下载钉死版本的 uv，同样先核对哈希）；app 装进 `~/Applications`。不用 sudo，不关 Gatekeeper：curl 下载的文件没有隔离属性。
- **装后台服务之前先问你**："在主力 Mac 上同步过设备了吗（↑ 和 录 都是 0）？"今天的 cpad 插上第一块板就接管，设备上没传的笔记和录音会存到这台 Mac。没有终端可问（或者你答 n）就不装，最后告诉你 `cpa service install`。
- 不碰 `~/.claude`、你的 `config.json`（只更新 `spawn_env`、`claude_bin` 两项）、笔记目录、`$CPA_HOME`，不下载模型。
- 下载 app、固件时有进度条；第一次装会先说"要下载 Python 3.12（约 70 MB）"，不会像卡住了一样不出声。装后台服务的问题里，设备正插着就直说"设备现在就插着"。
- 最后打印版本行和**一张**清单 `cpa setup check --todo`：只列还没好的，和安装脚本同一种语言，标出哪些必需（还差几步、是哪几步），每项带要跑的命令（本地转写那项说模型在主力 Mac 的哪里、用 AirDrop 拷；没有 Homebrew 时给 https://brew.sh；没找到 Claude Code 时说装好以后再跑一次 `cpa service install`）。再补几句清单管不到的：登记项目（`cpa project add --folder ~/code/某项目 -- 名字` 或 app 的「项目」页，不登记设备看板上是空的）、app 在菜单栏里、第一次插设备要点"允许配件连接"。

选项放在 `bash -s --` 后面，比如 `… | bash -s -- --dry-run`：`--dry-run`（只说不做）、`--version v0.5.0`、`--yes`（不问）、`--no-service`、`--no-app`、`--no-modify-path`、`--prefix DIR`、`--from DIR`、`--lang zh|en`（用哪种语言输出；默认看 `CPA_LANG`，再看 Mac 的区域设置，都没有就是英文）、`--firmware`（顺便把 `cpa-<tag>.bin` 核对好放进 `~/Downloads`；刷机仍由你在主力 Mac 上做：先同步、`cpa pause 120`，再照下面"安装方式 A"写到 0x0）。环境里留着测试开关（`CPA_INSTALL_*`）时，脚本第一步就用红字逐个列出来。

卸载：把上面命令里的 `install.sh` 换成 `uninstall.sh`。它删 app、两个 LaunchAgent、`cpa` 和 PATH 那一行，问过你再去掉 Claude Code hooks；**留着** `$CPA_HOME`（钥匙、事件、设置）、笔记和模型，并列出它们的位置。`--purge-data` 会请你输入 `delete` 才删 `$CPA_HOME`，笔记目录从不自动删。

有的 Mac 不让装 gh / Homebrew 时，照 [docs/27 §5.2](docs/27-new-mac-setup.zh-CN.md) 手动下载 wheel 和 `SHA256SUMS`，`shasum -a 256 -c` 核对后 `uv tool install`。发版的人用 `scripts/release-local.sh --tag vX.Y.Z`（默认只造不传，`--publish` 才上传），见 [docs/releases](docs/releases/)。

下面的"快速上手"是从源码开发的装法。

## 快速上手

目标：在一台 Mac 的**全新用户账户**里，从克隆仓库到设备上看到看板，不超过 15 分钟。实际计时记在 [docs/07-hil-checklist.md](docs/07-hil-checklist.zh-CN.md) 第 7 项（⏳ 还没在全新账户里实测）。

**需要**：Cardputer ADV 和一根 USB-C 数据线、macOS（Apple Silicon 上开发）、Xcode Command Line Tools（`xcode-select --install`，用来提供 git）。

1. **装工具**（[uv](https://docs.astral.sh/uv/) 和 PlatformIO 6.2.0，见 [docs/03 §4](docs/03-hardware-bringup.zh-CN.md)）：
   ```bash
   curl -LsSf https://astral.sh/uv/install.sh | sh        # 装完重开一个终端
   uv tool install platformio==6.2.0 --python 3.12         # 命令是 ~/.local/bin/pio
   ```
2. **取代码，准备 Mac 端**：
   ```bash
   git clone <仓库地址> cardputer-assistant && cd cardputer-assistant
   uv sync --project daemon                                # Python 3.12 + pyserial，装进 daemon/.venv
   ```
3. **刷固件（不按 G0）**：插上 USB-C，侧面开关拨到 **ON**。
   ```bash
   (cd firmware/app && pio run -e cardputer-adv -t upload)
   ```
   第一次会下载工具链和库，要几分钟。上传全程**不用按任何键**：esptool 通过 USB 的下载标志自动进出下载模式，传完自动重启。屏幕出现主页，状态栏是 `◇ 无响应`（Mac 端还没运行），这是正常的。
   - 以后再刷之前先执行 `uv run --project daemon cpa pause 120`，让 daemon 放开串口（`cpa` 在 PATH 上时，上传前脚本会自动做这一步）。
   - **出厂固件平时不开 USB 串口**（[03 §2 坑 1](docs/03-hardware-bringup.zh-CN.md)），Mac 上看不到 `/dev/cu.usbmodem*` 时自动上传无从进行，第一次需要手动进一次下载模式：线插着、开关在 ON，**按住 G0，按一下 BtnRst 再松开，继续按住 G0 约 1 秒后松开**，然后执行上面的上传命令；传完**只按一下 BtnRst**（不按 G0）启动固件。⏳ 这个按键顺序在 ADV 上还没实测（[docs/01 §1 第 5 步](docs/01-mac-setup.zh-CN.md)）。**不要**在拨开关或插线的同时按住 G0：那样会把芯片锁在下载模式，直到真正断电（见下面的 E5）。
4. **装 Mac 端服务**（LaunchAgent，登录后自动运行 `cpad`）：
   ```bash
   uv run --project daemon cpa service install
   ```
   它会记下登录 shell 的 PATH 等少数几个环境变量，并找到 `claude` 的绝对路径；没找到时会提示，开 session 的功能保持关闭，速记照常可用。
   - **第二台 Mac**：今天的 cpad 不问你，插上的第一块板就绑定，设备上还没传的笔记和录音会存到那台 Mac、从设备上删掉，存过去就回不去。插到另一台 Mac 之前，先在主力 Mac 上插一下，等状态栏的 ↑ 和 录 变成 0；只想充电的 Mac 不要跑 cpad。改进的设计见 [docs/27](docs/27-new-mac-setup.zh-CN.md)。
5. **登记项目**（每个项目占一个数字键 1–9；从设备开出的 session 默认用 `plan` 权限模式）：
   ```bash
   uv run --project daemon cpa project add myproj ~/code/myproj --slot 1 --name 我的项目
   ```
6. **看到看板**：几秒内设备状态栏变成 `● 在线`。主页是启动器，按 P（或者在任何页面按 Opt+P）打开项目看板，看板列出登记的项目和各项目里运行中、等你的 session 数。
   ```bash
   uv run --project daemon cpa status                     # 应显示 link: ready
   ```
   按 G0 打开速记，输入后按 Enter，笔记会出现在 `~/CardputerNotes/YYYY-MM-DD.md`。
   - **退出**：速记里没有内容时按一下退格（Backspace）或 `` ` `` 就退出；有内容时按 `` ` ``，会先问要不要放弃（v0.4.1 起；反引号这个字符用 Fn+` 打）。其他页面按左上角的 `` ` `` 键返回；开 session、回复的确认框上按它回到编辑器，字留着（确认框打开后 0.4 秒内的按键不算）。底部提示栏总会写出当前的退出键。
   - **忘了键**：按 Tab 打开菜单，见下面的[按键](#按键)。
7. **装 Claude Code hooks**（建议，v0.4）：session 要权限、提问、等计划审批、要登录或一轮结束时，看板马上更新，还能显示原因，不用等下一次轮询。不装也能用，只是全靠每 3 秒一次的轮询。
   ```bash
   uv run --project daemon cpa hooks install              # 只显示要加进 ~/.claude/settings.json 的内容，不写文件
   uv run --project daemon cpa hooks install --yes        # 写入；旧文件先备份到数据目录的 hook-backups/
   ```
   - 终端里的 Claude Code session **只在已信任的目录里运行 hooks**。没信任过的目录，在那里跑一次 `claude` 并接受信任提示（这一步也是从设备开 session 的前提，见下面的 `E_SPAWN`）。
   - 已经开着的 session 会自动读到新的 hooks。`cpa status --json` 的 `hooks` 显示收到过几个 session 的事件、最近一次在几秒前。卸载：`cpa hooks uninstall --yes`。

不想要服务了：`uv run --project daemon cpa service uninstall`。数据在 `~/Library/Application Support/CardputerAssistant/`。

### 设备不响应时

- **E5：黑屏，Mac 上握手失败（疑似卡在下载模式）**。原因是复位或上电时正好按着 G0。恢复：侧面开关拨到 **OFF**，**拔掉 USB 线**，等几秒，再插上并拨回 ON。只拨开关不够，因为 USB 一直在供电（[03 §2 坑 2](docs/03-hardware-bringup.zh-CN.md)）。也可以不按 G0、单独按一下 BtnRst。
- **上传卡在 `Changing baud rate` / `No serial data received`**：先 `cpa pause 120` 确认没有程序占着串口（`lsof /dev/cu.usbmodem*`），再不断电执行一次 ROM 直连的复位，然后重新上传（[ADR-0001 §5.3](docs/adr/0001-toolchain.zh-CN.md)）：
  ```bash
  ~/.local/share/uv/tools/platformio/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
    --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 115200 --before default_reset --after hard_reset --no-stub read_mac
  ```
- **状态栏一直是 `◇ 无响应`**：`cpa status` 看 daemon 是否在运行、端口是否被别的程序（串口监视器、M5Burner 等）占用。
- **开 session 失败（`E_SPAWN`）**：看 `cpa status`。`claude:` 行的 `login=` 是 daemon 环境里 `claude auth status` 的结果（`no` 表示没登录，`-` 表示还不知道）；`warning:` 行写出原因和一次性修复：没登录就在终端里 `claude auth login`；项目目录还没被 Claude Code CLI 信任（桌面端的信任记录和 CLI 分开），就进入那个目录跑一次 `claude` 并接受信任提示。cpad 从不替你接受信任或登录。
- **端到端自检**：真机测试固件和测试脚本见 [docs/07-hil-checklist.md](docs/07-hil-checklist.zh-CN.md) 与 [`tools/hil/`](tools/hil/)。

## 按键

v0.4.1 的按键（完整的表见 [docs/04 §8](docs/04-prd.zh-CN.md)；和 v0.4.0 比改了什么，见 [v0.4.1 发布说明](docs/releases/v0.4.1.zh-CN.md)的"按键变化"）。只用单键和两键组合。**忘了键就按 Tab**：菜单里每一行左边写着键，选中按 Enter，就等于按了那个键。

| 按键 | 作用 |
|---|---|
| G0 短按 | 记一条，存进今日笔记（等 0.35 秒看有没有第二下；直接打字也行，第一个字就落进去） |
| 按住 G0 | 说一段（语音便条），松开结束；手滑松开 0.6 秒内按回去还是同一条；按住时按 1–9 归到项目，0 不归属 |
| 点两下 G0 | 免提说一段：第二下一按下就开始录，松开也在录（第二下慢一点、1 秒内也算）；再按一下 G0 结束并保存。录的时候 1–9 / 0 归项目，Tab 菜单，Opt+L 锁屏（录音不停），别的键不起作用 |
| Tab | 菜单 |
| Opt+H | 主页（启动器） |
| Opt+P | 项目看板（v0.3.1 及以前是 Opt+H） |
| Opt+1 到 Opt+9 | 第 N 个项目页 |
| Opt+I | 等你 |
| Opt+M | 系统 |
| Opt+N / Opt+Q / Opt+S | 记笔记 / 排队给 AI / 开 session |
| Opt+R | 会议录音（先弹确认框，按 Enter 才开始） |
| Opt+L | 锁屏 |
| `` ` ``、Backspace 或 Fn+` | 返回（编辑器里 Fn+` 打出反引号） |

- **列表页**：`;` `.` 上下移，`,` `/` 换标签页，Enter 打开或执行，Opt+D 完成或忽略。
- **主页**：直接按格子上的字母（P 项目、I 等你、N 记一条、Q 排队给 AI、S 开 session、M 系统），等于按 Opt+那个字母；开会和锁屏要按 Opt+R、Opt+L（v0.4.1 起，免得打字时碰到就开会、锁屏），选中 R、L 格按 Enter 也行；1–9 打开第 N 个项目页；`;` `.` `,` `/` 移动，Enter 打开选中的格子。主页还显示最急的一条"等你"。
- **会议页**：G0 或空格加 ★，1–9 / 0 归项目，Opt+R 结束（先确认，Enter 结束、`` ` `` 继续录）。会议的开始框和结束框上 Opt+L 是取消并锁屏 / 继续录并锁屏；开始框 15 秒、结束框 10 秒没人按就自己关掉。
- **编辑器、确认框、菜单开着时**按住 G0 也录便条，草稿留着；松开后一样有 0.6 秒宽限。刚点开、还没打字的记一条里按住 G0，记一条关掉、录便条。点两下 G0 免提只在 G0 短按是"记一条"的地方算（菜单开着时第一下先关菜单）。

### Tab 菜单

菜单有三页，用 `,` `/` 换页，`;` `.` 选行，Enter 执行，Tab 或 `` ` `` 关掉：

- **本页**：这一页能按的键；
- **去哪**：主页、项目看板、等你、项目页、系统，以及记一条、开会这些全局键；在这一页直接按字母，等于按 Opt+那个字母；
- **控制**：锁屏，以及只读的声音（关）、电池、SD 卡、Mac 链路。

按 Tab 再按字母，等于按 Opt+那个字母，在"本页"上也一样：Tab、P 两下就到项目看板，不用记任何组合键。在采集页、确认框和会议页上打开菜单时只有"本页"和"控制"，"本页"上的字母不起作用，草稿和正在录的音都不受影响。菜单里按 G0，或者按菜单上写着的组合键（比如 Opt+L），会先关掉菜单，再照常起作用（开会时 G0 是加 ★，免提录音时按一下 G0 结束）。

### 锁屏

- 按 Opt+L 上锁，卡片亮 2 秒写"已锁屏"，然后熄屏，键盘锁住，**插着 Mac 也熄屏**。**用电池时屏幕因为 60 秒没按键熄灭，就自动上锁**（插着 Mac 不自动锁）。锁屏前的页面和草稿都留着，正在开的会继续录。
- **解锁**：先按 Opt+L（亮出锁屏卡片），再在 3 秒内按 Enter。
- 锁住时**先点一下 G0**，亮出锁屏卡片 3 秒；这之后按住 G0，才录一段便条（锁在草稿上也一样，草稿不动），麦克风打开前显示"● 准备"，松开后有 0.6 秒宽限（按回来还是同一段），录完卡片亮 2 秒写结果（"√ 已存 0:12"），再回到黑屏锁定。没先点就按住，只亮卡片写"没有在录"、不录音，免得在口袋里悄悄录下旁人的谈话。开会时 G0 仍然是加 ★。其他键都不起作用，也不点亮屏幕。
- 点两下 G0 免提录音也只在按键点亮的卡片上算（黑屏时、卡片自己亮着时，第一下只亮卡片）；免提录着锁住时，黑屏的角上写"● 免提 0:12"，G0 也只在卡片亮着时结束它：黑屏时先点一下亮卡片（写"免提录音中：按 G0 结束"），再按一下结束，卡片写结果。
- 锁屏卡片只显示时间、日期、电量和几个数量（等你、待传的录音、↑N），不显示项目名、session 名或任何正文。插着 Mac 时，等你的数量增加，卡片亮 3 秒。
- **锁屏不是安全锁**：没有 PIN，谁拿到设备都能按 Opt+L、Enter 解开。它只防口袋里误触、不让旁人瞟到内容。SD 卡和 Mac 上 `~/CardputerNotes` 里的东西都是明文，见 [SECURITY.md](SECURITY.zh-CN.md)。

### 录音和同意

- v0.3.1 起录音全程不出声，屏幕是唯一的提示：录音时 `● REC` 和计时一直看得见，锁屏、菜单打开、弹出提示时也一样。
- **录别人说话之前，请先告知在场的人并取得同意。** 会议录音前的确认框只是提醒你，告知和取得同意由你自己负责。各地对录音的法律要求不同，本项目的文档不构成法律意见。只录自己时用便条（按住 G0）。

## v0.2：microSD 存储与 Launcher

插着 FAT32 的 microSD 卡时，待发条目和项目缓存都放在卡上的 `/cpa/`（卡上别的文件不碰）；没插卡时和 v0.1 一样存在内部 Flash 的 `cpafs` 分区。开机时选一次，运行中不切换。同一个发布镜像既能整片刷写，也能交给 [bmorcelli Launcher](https://github.com/bmorcelli/Launcher) 从 SD 卡安装。系统页（Opt+M）显示启动方式（`启动 直接` / `启动 Launcher`）、当前存储和原因；`cpa status` 的 link 行显示 `store=sd launcher=no` 这样的两项。

发布产物（`scripts/make_image.py` 生成，CI 发布时附上）：

| 文件 | 用途 |
|---|---|
| `cpa-<tag>.bin` | 合并镜像（bootloader、分区表、boot_app0、app）。整片刷写时写到 0x0；也可以放到 SD 卡上给 Launcher 安装，Launcher 里显示为 `cpa-<tag>` |
| `cpa-<tag>-app.bin` | 只有 app，写到 0x10000，不动分区表 |
| `SHA256SUMS` | 校验和（`shasum -a 256 -c SHA256SUMS`） |

自己打镜像（先 `pio run -e <env>`）：`cd firmware/app && python3 scripts/make_image.py --env cardputer-adv --tag v0.2.0-dev --out dist`。

### SD 卡准备

- 只支持 **FAT32（或 FAT16）+ MBR 分区表**。32GB 以上的卡出厂通常是 exFAT，固件用不了，系统页会提示"SD 卡无法使用：请在 Mac 上格式化为 FAT32"。v0.2 不能在设备上格式化。
- 在 Mac 上格式化（**会清空整张卡**）。先用 `diskutil list` 找到读卡器里的那张卡，**把盘号核对两遍**（容量、名称、`external, physical` 都对得上），写错盘号会清空别的盘：
  ```bash
  diskutil list                                        # 第一遍：找到卡，例如 /dev/disk4
  diskutil list /dev/disk4                             # 第二遍：只看这块盘，确认是那张卡
  diskutil eraseDisk FAT32 CPA MBRFormat /dev/disk4
  ```

### 第 0 步：换安装方式之前，先清空内部待发

整片刷写 ↔ Launcher，两个方向都要先做这一步：换分区表以后，内部 `cpafs` 里还没发出去的条目就找不回来了。

- 连上 Mac 同步，等状态栏的 ↑N 变成 0；或者
- 插上 SD 卡开机一次，让开机迁移把 `cpafs` 里的条目搬到卡上，再到系统页确认存储一行有 `已迁移 N`、没有 `迁移失败`。

### 安装方式 A：整片刷写

- 从源码：`cd firmware/app && pio run -e cardputer-adv -t upload`（分段写，保留 NVS）。
- 用发布镜像：把 `cpa-<tag>.bin` 写到 0x0，例如
  `esptool.py --chip esp32s3 --port /dev/cu.usbmodem1101 write_flash 0x0 cpa-v0.2.0.bin`。合并镜像里段之间填的是 0xFF，所以这样写**会清空 NVS**：epoch 和开机计数重新生成，按设计是安全的（新 id 不会和旧的撞）。

### 安装方式 B：bmorcelli Launcher

1. 先做第 0 步。
2. 装 Launcher **正式版 2.9.1**：用网页刷写器 <https://bmorcelli.github.io/Launcher/>（设备选 "Cardputer and Cardputer ADV"），或者下载官方 release 里的 `Launcher-m5stack-cardputer.bin`，先核对大小是 1,422,720 B，再用 `shasum -a 256` 记下校验和，然后用 esptool 写到 0x0。
3. 把 `cpa-<tag>.bin`（必须是合并镜像，Launcher 才会建出 `cpafs`）拷到 SD 卡，建议放在 `/cpa/fw/`。
4. 在 Launcher 里选 SD → 这个文件 → 安装。
5. Launcher 自己的 SD 驱动可能读不了大容量卡。读不了时换一张 32GB 以下的卡来安装，或者用 Launcher 的串口安装（`flash overwrite`，见 [docs/gates/launcher.md §2、§4](docs/gates/launcher.zh-CN.md)）。

**在 Launcher 下升级**：推荐覆盖安装，也就是 Launcher 空间不够时给出的 "Use <标签> partition"，或者串口 `flash overwrite`；也可以删除旧版本后**立刻**装新版本。不要删了旧版本又留着别的版本不管：删除任何一个我们的版本都会连 `cpafs` 一起擦掉，没插卡时剩下的版本就没有存储可用（系统页显示"没有可用存储"）。插着卡时待发条目在卡上，不受影响。

**从 v0.1 升级**：`pio run -t upload` 保留 NVS；把 `cpa-<tag>.bin` 写到 0x0 会清空 NVS、epoch 重新生成（安全）。两种方式都不动 0x670000：v0.2 的 `cpafs` 就是 v0.1 的 `spiffs` 那块区域，数据都还在；插着卡开机会自动迁到卡上。如果只写了 `-app.bin`，分区表还是 v0.1 的，系统页会提示"分区表是 v0.1 的，请整片刷写"，数据没丢，整片刷写后就能恢复。

### 用户须知

- 在 Launcher 下，开机时按住 G0 会进 Launcher 菜单，而不是打开速记。
- Launcher 下冷启动会多一个约 5 秒的启动画面；拨电源开关关机再开机、按 RST 也会先进 Launcher。
- ↑N 不为 0 时不要拔卡。运行中拔卡后，那一次存储操作会卡一下（最多约 2 秒），之后保存都提示"保存失败（E_STORAGE）"，系统页显示"存储错误 N，请重启"；把卡插回去再重启就恢复，卡上的条目开机后照常发送。
- 卡上的待发条目是明文，和 v0.1 存在内部 Flash 里一样，只是卡可以被直接拔走读取（[SECURITY.md](SECURITY.zh-CN.md)）。

## 开发

规则见 [CONTRIBUTING.md](CONTRIBUTING.zh-CN.md)：TDD、文件和函数规模、conventional commits。WP1 部分的检查：

```bash
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/tools -q   # 协议工具的测试
uv run --with 'jsonschema>=4.23,<5' python protocol/tools/check_vectors.py   # 校验全部 schema 和向量
uv run --with pytest pytest tools/repo -q
python3 tools/repo/check_sizes.py --warn 400 --fail 800 protocol tools
uvx lizard -L 50 -w protocol/tools tools/repo
```

CI 在 [`.github/workflows/ci.yml`](.github/workflows/ci.yml)；`firmware/app/` 和 `daemon/` 出现后，对应的作业会自动启用。真机（HIL）测试不进 CI。

## 文档

| 文档 | 内容 |
|---|---|
| [01-mac-setup](docs/01-mac-setup.zh-CN.md) | Mac 开发环境 |
| [02-landscape-report](docs/02-landscape-report.zh-CN.md) | 现有项目调研 |
| [03-hardware-bringup](docs/03-hardware-bringup.zh-CN.md) | 真机联调记录（USB、HWCDC、键盘） |
| [04-prd](docs/04-prd.zh-CN.md) | 做什么、做到什么程度 |
| [05-architecture](docs/05-architecture.zh-CN.md) | 怎么搭；§6 是协议规范 |
| [06-work-packages](docs/06-work-packages.zh-CN.md) | 工作包与闸门 |
| [13-assistant-v2-plan](docs/13-assistant-v2-plan.zh-CN.md) | 助手 v2 计划：主页、Tab 菜单、锁屏（v0.4.0）和之后的版本 |
| [23-mac-inbox](docs/23-mac-inbox.zh-CN.md) | 设计稿：Mac 上的待归档（`cpa filing`、WhisperWoof 的 Cardputer 页）和录音的环境戳（#54）|
| [25-route-to-any-app](docs/25-route-to-any-app.zh-CN.md) | 设计稿：说着话送到任何 app，Grok Bot 先做；去向 = 通用种类 + 每个 app 一份预设（#61）|
| [26-security-round](docs/26-security-round.zh-CN.md) | 设计稿：安全测试里程碑 M-SEC 和落盘加密；设备上逐个文件封给 Mac 的公钥（SEAL），Mac 上整盘加密、文件照样开放（#63）|
| [27-new-mac-setup](docs/27-new-mac-setup.zh-CN.md) | 设计稿：新 Mac 一条命令装好、首次设置页，Mac 认设备要你确认、设备认新 Mac，设置搬家（#64）|
| [28-multi-mac-sync](docs/28-multi-mac-sync.zh-CN.md) | 设计稿：一台设备、几台 Mac；录音留在设备上直到交给对的 Mac、交出后留底几天可再交，分组在设备上存一份镜像 |
| [releases/](docs/releases/) | 每个版本的发布说明 |
| [adr/](docs/adr/) | 架构决策记录 |

## 安全与隐私

v0.1 的信任边界是"物理 USB 连接 + 当前登录的 Mac 用户"，没有网络端口、没有云、没有遥测。已知风险（设备丢失时未同步的条目是明文）见 [SECURITY.md](SECURITY.zh-CN.md)。v0.4.0 的锁屏（Opt+L）只防误触、不让旁人看到内容，不是安全锁，也不加密任何东西。

## 许可证

[MIT](LICENSE)，Copyright (c) 2026 Heqing Huang（v0.5.0 之前是 Apache-2.0）。打了补丁的 `firmware/third_party/SD` 仍是 Apache-2.0（全文在 [LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt)）。第三方组件和借鉴来源见 [NOTICE](NOTICE) 和 [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)。
