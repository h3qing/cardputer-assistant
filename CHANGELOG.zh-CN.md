[English](CHANGELOG.md) · **中文**

# Changelog

格式参考 [Keep a Changelog](https://keepachangelog.com/)，版本号遵循 SemVer。每个版本的详细说明在 `docs/releases/<版本>.md`，打 tag 时 release 工作流会把它作为 GitHub Release 的正文。

## [Unreleased]

## [v0.5.2] - 2026-11-01
### Added
- 暗屏少转（docs/29 P1；需求（2026-10-28）：长时间没人碰，设备一直开着，把电池用完）：用电池、屏幕已经黑了并锁上、锁屏卡也灭了，而且没在录音、没插 Mac、音频任务和账本都空闲、没有键按着（最后一次按键起 1 秒内也不算）时，主循环每一轮后让出 CPU 10 ms，不再 240 MHz 空转。按键最多晚 10 ms；亮屏、调暗、录音、插着 Mac 时完全照旧。纯函数 `lib/core/idle_yield.*`，主机测试含 `millis()` 回绕；真机上 `device/test_idle_yield`：暗屏每秒 98 轮（亮屏约 14,400 轮），让出最多给最长间隔加 4.2 ms（它的闸改成检查让出加了多少，即 docs/29 P1 的「按键采样最多晚 10 ms」，不再用连亮屏自己都达不到的绝对 20 ms）。
- 固件版本报 `cpa-fw/0.5.2`；`cpa --version`、`cpa-stt --version` 和 Mac app 都是 0.5.2。
### Changed
- 设备默认英文（英文优先、支持中文，接着 #88–#92）：什么都没存的设备（新的，或者 NVS 擦过的）用英文开机，模拟器的 `boot` 也是（按中文写的脚本写 `boot lang=zh`）。跑过旧版 cpa-fw、从没切过语言的设备继续用中文：用这个固件第一次开机时，在写开机计数之前，把 `cpa/lang` 存成 0，只写一次。已经选过的语言一律不动，Opt+M、`.`、Enter 照样能切换。按中文界面写的固件测试在 `setUp()` 里固定中文。
- 固件和模拟器的代码注释改成英文，模拟器脚本的注释和截图说明也是。只有讲中文本身的注释（字形、宽度、"语言 Language" 那一行、CJK 测试数据）还留着中文。
- 安装说明（README、发布说明、`install.sh` 和 `uninstall.sh` 的开头注释和 `--help`）只给公开的 `curl … | bash` 一行；文档里遗留的个人化措辞清掉了（#97）。
### Fixed
- 开机扫描的栈（#85）：卡上有约 100 条录音等着时，开机扫描后音频任务 6 KB 的栈只剩 1,028 B。现在每个 `.M` 直接解码进调用方的 `RecMeta`，恢复计划原地构造：真机上剩 1,624 B（闸 1,536 B）。堆、静态 RAM、栈大小都没变。
- 大批「再交一次」或「清掉留底」时的任务看门狗重启（#85；v0.5.1 的已知问题）：音频任务做文件活时每 100 ms 睡一个 tick，core 0 的空闲任务不再被饿着。真机上 200 段留底 5.7 s 排回、14.2 s 清完，没有重启。
- 安装脚本：curl 安装时不再冒出 "printf: write error: Broken pipe"（#98）；安装测试在没有 `~/Library/LaunchAgents` 的 HOME 里也能跑。
- daemon 测试：MCP stdio 测试在 macOS 把管道留得很小时不再死锁（#96）。

## [v0.5.1] - 2026-10-29
### Added
- 录音交错 Mac 不再丢（docs/28 S1a，#84；需求（2026-10-26）：给另一台 Mac 的录音，不能因为先连了这台 Mac 就没了）：
  - **留底**：Mac 拉走并删除一段录音时，设备不再删文件，而是在 `/cpa/rec/<E7>/SENT.L` 追加一条带收件 Mac 哈希（`hello.host_id` 的 CRC-32）的记录，fsync 之后才回 ack；留底谁都看不见（不进 `file.list`、`files.n`），不占 256 段的待传。按整次录音留 7 天（时钟来自 `time.set`，往前跳超过一天的先不用，D16）、超过扫描表能放下的段数或卡上空闲不到 1 GB 时从最旧的删；太长的录音、卡满时照旧直接删。
  - **再交一次**：设置 → 存储（Opt+M 再按 `,`）→"再交一次 ·3F2A 的 2 次"Enter，确认框光标在"取消"，`/` Enter：最近交给那台 Mac 的一批重新待传，**那台 Mac 以后永远看不到它们**，别的 Mac 来拿。至少留 12 个待传位置，不会挡住录音。
  - **给这台 Mac**：设置 → 存储第二行，撤回"再交一次"（或提示里的"不给"）：连着的这台 Mac 又能拿到被扣下的那些（只有一台 Mac 时不会卡死）。
  - **插上时的 5 秒提示**：有离线录的录音时，插上 Mac 握手后提示栏写"离线 2 条 → 这台 · Tab 不给"，这 5 秒里只扣住这几条；不按就照常给（0 下），Tab 再 Enter 就不给这台（2 下）。离线 = 上次拔线以后录的（拔线时把下一个 id 存进 NVS）。锁着（从口袋里拿出来插上）或开着 Tab 菜单时提示看不见，5 秒就不走、键也不归它，这几条一直扣着，最多 2 分钟；按了 Enter 马上拔线也照样不给那台。
  - **清掉留底**：设置 → 存储 →"清掉留底"，确认框光标在"取消"。锁屏卡片多一个"留N"，设置页的同步行多"留N"。
  - 账本只由音频任务写；每条 28 B、魔数加 CRC，坏的一条按魔数跳过，脏了或太长就整理到另一个名字（`SENT.L` ↔ `SENT.K`），从不原地改。开机扫描先读账本，留底的段不读 `.M`；名字多过扫描表时拒绝开始录音（"卡上段太多"，D15）。
  - 主机上的掉电测试（每个字节、每条记录边界截断，每条记录 CRC 错，交出、再交一次、清掉、开机整理时每一次文件调用断电，加上随机命令和断电的模糊测试）；模拟器多了 `pull` 步骤和 `scripts/kept/` 四个脚本（中英文）。真机上（docs/07 §11）：设备测试、10 个 HIL 场景、两台 Mac 的模拟、36 次芯片复位当作掉电（拉取中、刚清掉以后、开机扫描时）和 62 分钟两台 Mac 的长测都过了；真的断电（关掉电池、拔线）还没做。
  - 评审后修的：账本整理后"再交一次"不再多拿同一台 Mac 更早的录音（整理时 GEN 记下最近一批从哪开始）；删不掉文件的"清掉"在整理后不会回到待传；插着线、卡上不到 1 GB 时在两次拉取之间清留底（拉取不再腾地方）；按段数清到上限减一整张待传表（1008 段），新交出的不会被直接删；钟往回跳一天以上也先不用；第 5 台 Mac 不会让开机扫描排除错的那台；提示的回执按次数（不按段数）；没交给音频任务时写"没改成，已照常给这台 Mac"而不是"SD 卡出错"；30 秒没回音的账本命令放开屏幕；中文存储页的几行事实不再挤在一起；英文同步行写 `kept N`。
  - 真机上修的（docs/28 §12）：B 刚停、A 紧接着握手（链路没掉）时，hello 的 `files.n` 还按上一台 Mac 算，A 要等 60 秒才拉；现在先按发 hello 的 Mac 算再回，换了 Mac 时 `files.rev` 也跟着变，A 马上就拉。
- 英文为主，也支持中文（#88–#92）：
  - 文档（#91）：英文在原来的路径，中文在旁边的 `*.zh-CN.md`，两个文件的第一行互相链接（CONTRIBUTING 的文档语言规则）。README、CONTRIBUTING、docs/01–29、ADR、闸门、发布说明、协议、daemon 和 Mac app 的 README 都是这样；这一版 CHANGELOG、SECURITY、docs/07、docs/28 和 `firmware/sim/README.md` 也是。
  - `cpa`、cpad 和 cpa-stt（#92）：每次输出时决定语言：`CPA_LANG`，然后 `config.json` 的 `lang`（`cpa lang zh|en|auto`），然后 `LC_ALL`、`LC_MESSAGES`、`LANG`；`zh` 开头的是中文，别的（或者什么都没设）是英文。原来只有中文、或者中英并排的 `cpa filing triage`、`cpa dest test --send` 和 `cpa setup check`（`--todo` 只出一种语言）都跟着它。cpad 和 cpa-stt 写下的东西（每日笔记的行、会议的默认标题、看板的语音前缀、内置去向的名字、交给别的 app 的头、MCP 的截断标记、cpa-stt 的"没听到"、导出的 README）用写的那一刻的语言；写过的行不改写，读的一方只认格式不认字。`cpa service install` 把登录 shell 的 `CPA_LANG`、`LC_ALL`、`LC_MESSAGES`、`LANG` 带进 LaunchAgent。
  - 安装脚本（#89）：`install.sh` 和 `uninstall.sh` 每一行只出一种语言，默认英文：先看 `--lang zh|en`，再看 `CPA_LANG`、`LC_ALL`、`LC_MESSAGES`、`LANG`；`--help` 也跟着；最后一屏的 `cpa setup check --todo` 用同一种语言。核对摘要、隔离属性和 token 的做法都没变。
  - Mac app（#90）：截图先画英文，再画中文（见 Changed），演示数据是英文的，留一个中文项目和一条中文便条，屏幕上照样有汉字；核心测试默认用英文跑，查中文的套件钉 `zh`。
  - HIL 工具（#88）：注释改成英文；读不到开始时的语言时，跑完把设备设回英文。
- 设计稿 docs/29（#86）：设备放着不用时省电：电池供电、熄屏时浅睡，2 小时后深睡，电量低时关机。还没实现。
- 固件版本报 `cpa-fw/0.5.1`；`cpa --version`、`cpa-stt --version` 和 Mac app 都是 0.5.1。
### Changed
- Mac app 的截图（`swift run CardputerShots`）：英文在 `out/shots/`，中文在 `out/shots/zh/`（原来中文在 `out/shots/`、英文在 `out/shots/en/`；#90）。
### Fixed
- 安装后台服务时的竞争（#83）：`cpa service install` 先等旧的 cpad 退出（最多 10 秒），再确认新的真在跑、pid 和旧的不同；不行就再试一次，还不行就退出 1 并说清楚原因。`install.sh` 只信这个结果，几秒后再查一次 `cpa status`。

## [v0.5.0] - 2026-10-27
### Added
- 整个设备界面都能切到 English（docs/21 ST-4、ST-5、ST-6；需求（2026-10-26）：双语，整个界面能切到英文）：主页、看板、等你（英文叫 Inbox）、编辑器、确认框、录音页和 1–9 图例（`0 none`）、锁屏卡（"Wed 10/21"、`Waiting 1`、`R2`）、状态栏、Tab 菜单（Here / Go / Control）、所有 toast、提示栏、Mando 的牌子（Listen、Saved、Napping…），每一条字都是编译期检查过的中英一对；中文界面逐像素没变（314 张截图对比）。英文提示栏的键写在方括号里（`[Enter]open`），解析出来和中文一样是键帽；放不下时退回一行字也不带括号。英文主页格子：Board（旁边是正在跑的个数）、Inbox、Meet、Session。Mando 的牌子靠近屏幕边时往里收，不再出界（会议页的"保存中…"）。`tools/i18n/check_texts.py`（CI）拦住 `lib/`、`modules/` 里不在 `constexpr Txt` 里的中文，`texts.py` 出一份全部 428 对的宽度表；`test_hint_bars` 两种语言画每一条提示栏；模拟器 `cpa-sim shots --lang en` 能用英文跑任何脚本，模糊测试多跑一轮英文：英文下画出汉字（"语言""中文"除外）或任何一帧把字画出屏幕都算失败，跑完 Opt+M . Enter 两个方向都能切回来。
- 设置 / Settings（docs/21 §4，ST-3）：Opt+M 改叫"设置"，先打开设置页，光标每次停在 WiFi 行。WiFi 一行写"无 · 只用 USB 线"，Enter 进说明页（这个固件不带 WiFi；数据只走 USB 线；想用 WiFi 关开电源进 Launcher 切到出厂固件，它会把 WiFi 密码明文打到串口）；"语言 Language"一行两种语言写法一样，Opt+M、`.`、Enter 三下在中文和 English 之间切换；"连接"写 USB 线连着哪台 Mac（握手里的 `host_label`）、在线/离线，"同步"写上次多久以前、待传 ↑N 和录N。今天的诊断在右边一页（`/`），8 行，最后一行永远是警告行（键盘的幽灵、重置计数并进警告行）。整个设置都是中英两套；英文的时间写 "9 s / 40 min / 2 h / 2 d"。
- Mac app 的两个小彩蛋（本机、安静、不挡路）：**待归档清零**——最后一条归完时，菜单栏 Mando 的状态点变成闪光 3 秒（一天最多一次，启动时本来就是 0 不算），空的待归档里 Mando 跳三下（开了"减少动态效果"就不跳）、"都归好了 ✓ / All filed ✓"，下面是今天的回顾：便条数、分钟、去了几个项目、★，从 `cpa rec ls --json` 在本机算（纯函数 `TodayRecap`，带日期；过了零点重读，不把昨天的当今天）。**Vlog 时间码**——录音详情里"复制为 Vlog 时间码"：有 ★ 时是 `00:00:00` 加上带 ★ 的那几句（`00:01:23 ★ 文字`，YouTube 章节能认），没有 ★ 时每句一行；"导出 SRT"只写到你在保存面板里选的地方（同一秒的几句合成一条、空句不出、每条至少 1 秒，最后一条到 manifest 的 `duration_ms`）。中英文都有。
- 一行命令装到新 Mac（docs/27 修订 D1，需求（10-26）：一个 curl 就装上）：`scripts/install.sh` 从 GitHub 发布装 `cpa`（uv 工具，wheel 和钉死的 pyserial 都在发布里，不走 PyPI）、Mac app（`~/Applications`）和 cpad 的 LaunchAgent（先问你设备在主力 Mac 上同步过没有）。每个文件都核对 GitHub 的 SHA-256 和发布里的 `SHA256SUMS`，带隔离属性的拒绝；私有仓库用 gh 或 `GITHUB_TOKEN`；`--dry-run`、`--yes`、`--version`、`--prefix`、`--home`、`--from`；再跑一次就是升级。`scripts/uninstall.sh` 卸载并留着数据（`--purge-data` 要输入 `delete`）。`scripts/release-local.sh` 在 Mac 上造齐一个发布的资产，默认不上传。沙箱测试 `scripts/tests/test_install.sh`（18 种情况，临时 HOME、假的 launchctl / open / ioreg）。评审后：没有 Homebrew 的新 Mac 用只读令牌 + `curl -K <(printf …)`，脚本自己用 `printf … | curl -K -` 交令牌（不进参数、不写文件，只发给本仓库的发布接口，只许 `[A-Za-z0-9_]`）；`--from DIR` 从 U 盘装（`release-local.sh --release-json` 造的文件夹）；app、固件下载有进度条，第一次装先说要下载 Python 3.12（约 70 MB）；设备正插着时，装后台服务的问题里直说；最后一屏是一张中英文清单 `cpa setup check --todo`，再补登记项目、app 在菜单栏、`--firmware` 时怎么刷；取不到发布信息时打印真正的错误。安全：`--prefix` 不许带引号、`$`、反引号和反斜杠（会变成 `~/.zprofile` 里的代码）；`xattr | grep -q` 在 pipefail 下会漏掉隔离属性，改成先取再比；Ctrl-C 也清临时文件；curl 一律 `--proto =https --tlsv1.2`；环境里的测试开关用红字列出；`release-local.sh --out` 只清它自己造的文件夹、不打包 git 不跟踪的文件；`uninstall.sh --purge-data` 不删装着笔记目录或在 HOME 之上的 `$CPA_HOME`，`.zprofile` 只剩我们那一行时也能去掉。
- `cpa setup check [--json | --quiet | --todo]`（docs/27 D3、§2.6）：这台 Mac 的设置行（后台服务、笔记目录、名字、设备、磁盘加密、本地转写、Claude Code hooks、Claude Code），只读、5 秒内回来、不开串口；`--todo` 只列没好的，中英文，"还差 N 步"写出是哪几步；后台服务已经在跑时，设备一行不再说"先插回主力 Mac"；没有 Homebrew 时本地转写一行给 https://brew.sh；`--json` 是新契约 `protocol/setup/v1/`（schema、向量、`check_setup.py`；行里从不出现 `host_id`、序列号、`dev`，设备只用 4 位设备码）。
- `cpa-stt --version`。
- 项目是有名字的分组（docs/25 D24，2026-10-26）：`cpa project add [--key K] [--slot N] [--folder 目录] -- 名字` 不再要代码目录（名字可以是中文，key 自动从名字来，中文名是 `p<号>`；号默认第一个空的），`rename`、`move`（和那个号上的项目对调）、`folder 目录|--none`、`rm`（录音保留 key），都带 `--json`，打印 filing/v1 的 `projects` 文档（新增 `linked`、`folder`：目录名，从不给路径）或错误。只有连了目录的项目才有 hook、按 cwd 认项目和设备上开 session。老写法 `cpa project add KEY 路径 --slot N` 照旧（再带 `--key`、`--folder` 会被拒绝）。删掉的项目的 key 不会分给新项目（录音、排队项、常用去向里还带着的 key 都避开；手写撞上要 `--reuse`），`rm` 同时忘掉它的常用去向。Mac app 新的"项目"页：1–9 和设备一样，新建、改名（或双击名字）、点编号选新号或拖 ≡ 换号、连目录、删除（点两次）；英文界面跟随 Mac 或在设置里选。
- Mac app 有中文和 English（docs/24 §2.7）：跟随系统偏好语言，设置里可以选；每句界面文字是 `tr("中文")`，英文表一份，测试钉着没有漏翻的；截图两种语言都画（v0.5.1 起英文在 `out/shots/`、中文在 `out/shots/zh/`）。
- 从 Mac 把便条送进 Grok Bot 和任何 app（docs/25 R1，#61）：去向 = 通用种类 + 每个 app 一份预设（纯数据，代码里没有任何 app 的名字）。`cpa dest add --preset grok-bot` 加上 `url.grok`（字母 X），`cpa dest test url.grok --send` 送一条"测试 NNNN"并问你"出现了吗、自动发了吗"，答"出现了、没发"以后它才算只填草稿；`cpa filing send ID url.grok`、`cpa dest send REF` 把便条正文（只用便条正文，前面一个同一行的头 `[Cardputer 语音·可能有错字或别人的话·#项目]`）经预填链接放进 Grok 的输入框，**你在 Grok 里按回车才发出**。链接由本机直接交给加的时候认准的那个 app（bundle id + 签名 Team ID），正文不进任何进程的参数；太长或同一个 Bot 30 分钟内已经有一条时走剪贴板（只在本机、不进剪贴板历史、2 分钟后清）。`cpa dest pending` 是"等你送"：暂存的和项目常用去向（`cpa dest usual 项目 url.grok`）预选的建议；`cpa filing triage` 先过它们（↩ 送、`g` 再按字母换去向）。会议只进本机；可能自己发出去的去向只认设备上的确认。不开放任何远程 MCP。`--json` 的输出是新契约 `protocol/dest-cli/v1/`。设备上按 `X` 送是 R2。评审后：送到一半出错或被打断（包括 app 的 30 秒超时）记 `E_INTERRUPTED` 留在等你送，不再卡住；同一条再送同一个去向要 `--again`（`E_ALREADY`，Mac app 里再按一次）；认 app 只认苹果签发、带 Team ID 的签名（苹果自己的除外），ad hoc 签名的冒名 app 认不上；`deny.json` 拦的 app 对 `--app` 也拦；测试那一条答完就不再占着 Bot 的草稿。Mac app（R1-E）：待归档上面是"等你送"（⌘↩ 送、⌥D 不送了），便条详情有"送到…"（链接去向在前，⌥ 加字母一下送），集成 → 去向能加上（先确认）、送一条测试并就地问"出现了吗？自动发出去了吗？"、设项目的常用去向；全经 `cpa dest … --json`；测试按去向种类问，剪贴板那句留到你点"知道了"并能"再复制"，按数字归档后直接选中常用去向那一行（数字、⌘↩ 两下）。
- Cardputer Assistant for Mac（docs/24，`mac/`）：自己的原生 Mac app，一个窗口（待归档 · 录音 · 设备 · 集成 · 设置）加一个菜单栏图标（Mando 的头、状态点、待归档数），`mac/scripts/build-app.sh` 造、`install-app.sh` 装到 `~/Applications`。**它只是公开接口的一个普通客户端**：`cpa … --json`、笔记目录的开放文件（只读）、`cpa mcp`、`cpa export`、`cpa-stt`；不连 cpad 的 socket，不读 `$CPA_HOME`，不联网，代码里不出现任何 app 的名字，唯一会改东西的是你点的那条命令——它能做的，任何 app 照同样的命令都能做。待归档：一行筛选，1–9 一下归档（名字写在按钮上），交给 AI（排进待办 / 开工）、看过了、藏起来、播放，⌘Z 撤销（编辑菜单写着撤销哪一步），别的电脑的项目要再按 Enter，文字里正好提到一个项目时建议它、Enter 批准；没装 `cpa filing` 时是只读的"没项目的录音"。集成页：先是"去向"（docs/25 §5.7，等 `cpa dest`），再是给开发者的 MCP（标签和片段来自 `cpa mcp setup`，加"其他 MCP 客户端"）、命令行 + JSON（带绝对路径、每条写能不能用）、文件、转写程序、导出，和"复制接入说明"（一段 Markdown，贴给任何 app 的编程助手）。设置：本地转写开关（`cpa-stt use/off/setup/service install`）、这台 Mac 的名字（`cpa host name --json`）、cpa 和笔记目录在哪。SwiftUI + SwiftPM，只要命令行工具；测试是 `swift run CardputerCoreTests`（filing/v1 契约副本 `8baed54` 的全部向量）；`swift run CardputerShots` 把每一屏画成 PNG（屏幕外的真窗口、整页、菜单栏），`--live` 用这台 Mac 的真 cpa 只读地画；CI 的 `mac` 任务在 macOS runner 上跑这些。缺的公开接口记在 docs/24 §1.3（G1–G10）。
- 录音的环境戳（docs/23 §4.2，#54）：`manifest.json` 和 `rec.archived` 新增可选的 `env{host, link}`（录音契约 v1 只新增，加了向量）。`host` 是你用 `cpa host name -- 家` 给这台 Mac 起的名字（默认不设，从不用 macOS 的电脑名；设备控制页上显示的也是它），`link` 只在 cpad 能证明录音开始时设备连着它才是 `here`，否则 `unknown`。`cpa filing` 的行、导出的 `data/filing.jsonl` 和 MCP 的 `list_unfiled`、`get_filing_item`、`list_recordings`、`get_recording` 都带上它（只有 `host`、`link`；`link` 不是枚举词时读成 `null`，照未知显示）。不做网络指纹。
- Mac 待归档的折叠（docs/23 P1-A，#54）：一个纯函数从 `events.jsonl` 算出每段录音的"有效项目"（最后一次改的项目或交给 AI 的排队项；没有时便条取 manifest、会议取最后一段）和归档状态（新、已归档、已交给 AI、看过了、藏起来，别的电脑的项目单独一组）。`cpa rec ls/show`、设备的录音历史、每日笔记、MCP 的录音/待办/采集、`cpa export` 都用它，说法一致。
- 结果写进今日笔记之后才归档的录音，cpad 追加一条自带上下文的 `rec.filed` 事件和一行 `- HH:MM #项目 ↪ 归档：标题 ^cpa-<id>-f<n>-<项目>`（`n` 是这段录音的第几次，所以归到 blog 又撤回 cpa 也会再写一行，笔记最后一行总是现在的项目；只有 cpad 写笔记，启动时重放）；结果之前归档的，结果那一行直接用新项目。每日笔记里便条自己的那一行待办（`- [ ] … ^cpa-<id>`），MCP 的 `list_todos` 也按有效项目给。
- `cpa export` 多一个 `data/filing.jsonl`（每段录音一行，藏起来的也在；设备只给 `$CPA_HOME/id_key` 算的短 id）。cpad 启动时创建 `id_key`（0600）。
- 控制回复带整数 `api`；`cpa status` 发现正在跑的 cpad 比 `cpa` 老时提示 `cpa service install` 重启。
- `cpa filing`（docs/23 P1-B，#54）：`cpa filing` 列出这台 Mac 没归档的录音（新的在前，有文字、设备短 id、时间），能按日期、设备、录音时是否连着这台、便条/会议、项目、状态、转写状态、文字筛，翻页；`show` 看全文、逐句转写、待办、音频文件名；`file ID KEY|--none`、`done`、`hide`、`reopen` 各写一条记录（在事件锁里按最新的日志再决定一次），已经是这个状态就不写，每次都打印撤销命令；别的电脑的项目要加 `--other`。所有文字过 `display_safe()`，最多 280 字。`--json` 的输出是新契约 `protocol/filing/v1/`（schema、向量、`check_filing.py`，CI 里跑），`cpa project ls --json` 也在里面（不含本机路径）。
- `cpa filing hand ID [KEY] [--spawn]`（docs/23 P1-C，#54）：把一段便条交给 AI，只排一条队（`origin:"inbox"`），正文由 `cpa` 从便条正文或转写自己取、清洗、截到 500 B，从不进命令行参数；会议不能交；`reopen` 撤回。开 session 仍然只在设备看板上按 Enter，这种排队项不管项目配的什么模式一律 plan，看板摘要以"语音·"开头。正在跑的 cpad 比 `cpa` 老时拒绝（`E_UNAVAILABLE`，先重启 cpad），控制回复的 `api` 升到 2。写命令的 JSON 结果多一个 `queue`。
- MCP 只读工具 `list_unfiled`、`get_filing_item`（docs/23 P1-E，#54）：AI 客户端能看 Mac 上的待归档，和 `cpa filing ls`、`show` 是同一个函数、同样的筛选、同样的行（契约 `protocol/filing/v1/` 的 `ls`、`show`，加 `trust`）；藏起来的从不返回，别的电脑的项目要 `orphans:true`，行里有电脑名和是否连着这台（`env.host`、`env.link`），永远没有网络、设备 id、路径和固件版本。没有写工具：归档仍然用 `cpa filing`。顺手：`list_recordings`、`get_recording` 多一个 `env{host, link}`；`list_queue` 不再列从藏起来的录音交给 AI 的排队项。
- `cpa filing triage`（docs/23 P1-F，#54）：没装 WhisperWoof 时在终端里一条一条过待归档（新的在前，别的电脑的项目放在最后），一个键一个动作：`1`–`9` 归到这台 Mac 的第 N 个项目并跳到下一条，`0` 去掉项目，`x` 看过了，`q`、`s` 交给 AI（再按 1–9；`s` 之后到设备看板上按 Enter 开，plan），`p` 或空格用 `/usr/bin/afplay` 播第一段（参数数组，给的是受限打开的文件描述符，不是路径），`u` 连着撤销这次的每一步，`n` 跳过，`?` 帮助，Esc 退出。每一步打一行结果；别的电脑的项目先问 y/n；会议和还没字的便条按 `q`、`s` 只说为什么、什么都不动；开始前敲下的键丢掉。写的记录和 `cpa filing file|done|hand|reopen` 一样。不开网页、不开端口；不在终端里（脚本、agent）时礼貌地拒绝，什么都不读不写。
- 待归档这一轮评审后的改动（#54）：有打开的排队项时 `cpa filing file` 也拒绝（`E_ALREADY`，排队项保留它的项目；先 `reopen` 撤回），所以连着撤销几步总能回到最开始；排队项在设备上开着的时候撤回、开完以后仍算已交给 AI；撤销能恢复成这台 Mac 已经删掉的项目；转写里半个表情符号（单独的代理码点）不再让 `ls`、`show`、`hand` 和导出整个出错；`ls` 问正在跑的 cpad 只等 0.5 s（`hand` 等 2 s），cpad 卡住时列表照样出来；`status.json` 和别的文件一样不跟符号链接；cpad 自己写的排队记录也进它的折叠。
- `cpa filing` 给人用的便利（不在契约里）：只给 `--project KEY` 时列这个项目的全部状态；`--from`、`--to` 也认 `today`、`yesterday`；出错时说人话（例如 `hand` 没给项目时提示 `cpa filing hand ID KEY`）。`--json` 时 argparse 拒绝的参数（这个 `cpa` 还没有的选项或命令）也打印一份 v1 错误文档（`E_BAD_ARG`，`field:"argv"`），读的一方能分出"`cpa` 太旧"和崩溃。
- `cpa host name --json`：读、设、清这台 Mac 的名字，输出契约里新的 `host` 文档（WhisperWoof 的"家 / 公司"横幅用它）；名字不能带 C1、U+2028/9 和双向控制符（行里会被去掉，存的和显示的就不一样了）。契约 `protocol/filing/v1` 这一轮只新增和收紧（还没发布）：`host` 文档；`env` 的 `host`、`link` 必有；`file` 的撤销必有 `args.project`；文字不许单独的代理码点；新增按码点计长、`env`、`asked` 的向量；`check_filing.py` 改成和 WhisperWoof 校验器一样的关键字允许列表。
- `cpa filing triage` 更顺手：连按两下只归屏幕上那一条（下一条出来之前敲的键丢掉）；开着中文输入法时全角的 `２`、`ｘ`、`？` 当成对应的键，别的中文字提示切到英文；`u` 说清楚回到了哪个状态；别的电脑的项目按 `0` 问"不要 acme，当成没项目？"；转写失败或超时的便条提示"p 听一遍再归"；`?` 之后把这一条再显示一遍；播放失败会说。
- `cpa queue reopen <qid>`：从录音交给 AI 的项（`cpa filing hand`）重开以后，`cpa filing` 里也回到"已交给 AI"。
- 录音页的 1–9 图例（gh #53，docs/20 U27，需求（2026-10-22）：说话时在屏幕上看到 1 到 9 是哪个项目）：便条页（按住说、松开后的宽限、免提）和会议页一直写着 `[1] 项目键 … [9] 项目键 [0] 不归属`，和键盘上数字那一排同一个顺序；现在选的那个亮着（原来"存到"胶囊的样子），按数字的那一刻就换过去，别的都不挪（便条页给没有项目的数字在 `[0]` 后面留了一格，按了写 `[9] ?`；会议页标题写 `#9`）。项目多时先多占一行、让键保持完整（便条页：两个项目一行，三个长键两行，5–6 个三行、波形变成 2/3 高），三行也放不下才把所有键截到同一个宽度，截过的后面画两个小点，选中的被截了标题行写 `存到 #完整的键`；会议页先用计时下面那一行，放不下再用时间轴下面 `0:00 现在` 那一行，9 个长键时 0 只画键帽 `[0]`（标题写着去向）。没亮的数字用更亮的 Ink。只在整屏重画时画，动画不碰它；SPI 不变或更少，一次整屏多约 2 ms（按主机实测折算，docs/19 §7.5）。纯函数 `lib/shell/rec_legend.*`；模拟脚本 `ux/memo_legend.cpas`；`test_runtime_budget` 多打 `memo_us`、`memo_tick_us`。
- 设备上 Mando 的几个隐藏小彩蛋（docs/04 末尾"隐藏按键"；安静、不挡路，录音时从不起作用，不写卡、协议不变）：`Opt+W` 在主页和亮着的锁屏时钟卡上摸摸 Mando（他跳一下、冒个心、说一句，旁边记"摸 ×N"，1.5 秒；每第 100 下有一句特别的；只在内存里记）；锁屏卡和主页问候跟着本地时间换心情（23:30 以后"夜深了"多一个 z，5–7 点"早起鸟"醒着，周末"周末啦"；不知道时间时和以前一样）；在主页上等你的 session 从有变成没有时，他跳一下、卡片写"搞定啦 / All clear"，只一次。纯函数 `lib/eggs/mando_eggs.*`；模拟脚本 `ux/eggs_zh.cpas`、`ux/eggs_en.cpas`。评审后：搞定啦不盖掉刚录太短时他头上的"?"；中文牌子不再重复问候里的"都处理完了"；说"摸摸 / Pat pat"时计数只写"×N"；主页上放不下时不写计数（和锁屏卡一样）；时区测试加了 UTC−7 和 UTC+5:30。
- 固件版本报 `cpa-fw/0.5.0`。
### Changed
- 许可证改成 MIT（Copyright (c) 2026 Heqing Huang，和 WhisperWoof 一样）；打了补丁的 `firmware/third_party/SD` 仍是 Apache-2.0，全文在 `LICENSES/Apache-2.0.txt`。
- 版本号从包元数据读（`importlib.metadata`），`uv version` 跟着 tag 改；Mac 端现在是 0.5.0，升级不再装回缓存里的旧 0.1.0（#57）。发布工作流检查版本等于 tag，并发 wheel、pyserial wheel、两个脚本和一份 `SHA256SUMS`。
- `cpa service install` 只改 `config.json` 的 `spawn_env`、`claude_bin` 两项，别的键原样留着（不再补默认值、5 不再变成 5.0）；`config.json` 坏了就停，什么都不写。
- 方向（docs/25 D23，2026-10-26）：设备说完直接送到 agent（R2）暂缓；语音先进 Mac app，你再手工交给不同的 agent。语音片段从不直接开 session（docs/22 §5.4 补记）。
- 便条页去掉"存到 #项目键"那一行：亮着的图例项就是去向。会议页标题照旧写去向。
- 便条页三种提示栏去掉 `0 不归属`（图例就在上面写着；两处上下叠着像画错了），免提的写回完整的"选项目"：`G0结束 1-9选项目 Opt+L锁屏`。
### Fixed
- 英文界面评审后的修正（docs/21）：没有字再碰到屏幕右边（收件箱和看板的提示栏 `[Opt+D]hide`，原来的 dismiss 最后一个字母压在边上；会议波形下的 "now" 离边 2 px；确认框里的 session 一行和引用的文字都留出最后一列）。提示更好懂：会议 `[G0]/[Space]star`（不再有光秃秃的 G0 键帽），编辑器 `[Opt+N/Q/S]type`，主页 `[G0]hold, talk after REC`。"Hello, no Mac yet"（逗号后小写）；锁屏卡 "√ Saved 0:15"、"× Too short"，和旁边的 "Not recording" 一样大写开头；"No project 5 (only 2)"。中文界面下 Tab 菜单"去哪"里 Opt+M 那一行写"设置 Settings"，只看得懂英文的人也找得到切换的地方（主页格子还是"设置"）。
- 模拟器把墨迹落在屏幕最后一列的字也算出界（原来只看字宽，差 1 px 的漏掉了）；新的 `test_ux_edges` 用两种语言跑全部 ux 脚本和两个导览，任何一张截图有字碰边就失败。
- 真机上量了英文：`test_runtime_budget` 用英文再画一遍同样的页面和动画（`CPA_BUDGET_EN`，最长一次重画 38.2 ms，中文 40.9 ms，都在 50 ms 以内）；`test_storage` 在真机 NVS 上存英文、读回来，`cpa/lang` 写个坏值（7）读出来是中文，再放回原样。HIL 加 `lang` 场景（按键 Opt+M . Enter 切过去、重启还在、切回来），而且每次 HIL 跑完都用 `debug.lang` 把开始时的语言放回去，失败了也放，不会把你的设备留在英文。
- 测试版 `debug.state.rec` 的 `mic_missing` 假阳性（v0.4.1 已知问题；真机上按住正好 3,000 ms 的便条 18 次里 17 次多 1）：停止落在一块的边上时，`Mic.end()` 拒绝采集任务回调里的重新挂缓冲，被记成了一次 miss。现在 `MicRing::stop` 在 `Mic.end()` 之前标记"正在停"，之后挂不上的既不算 miss 也不再欠（纯函数 `rec::rearm_missed`，`test_rec_health`）；停止以前的 miss 照旧算。录音本身没变，正式固件没有这个计数。真机上还要再跑一次按住 3,000 ms 的探针和长测。

## [v0.4.1] - 2026-10-22
docs/20 交互审查的 A 包和 B 包（D1–D10 你 2026-10-22 都按默认值定了）、Mando 主题（docs/19）、v0.4.0 真机长测找到的问题，和 2026-10-22 的三条需求：便条说着说着断掉、想免提录音、波形不跟着声音动（gh #51、电平修复）；Mac 上还有同一个 tag 带上的 `cpa-stt`（#52）、`cpa rec ls` 显示到秒（#55）和从 wheel 安装的 cpad（#57）。说明见 docs/releases/v0.4.1.md，各项状态见 docs/20 §2 的"状态"一列和 §5.6。原计划的 v0.4.1「录音历史与待办」的设备界面改成 v0.4.2（D10）。
### Added
- 锁卡（docs/20 B1）：锁着时按住 G0 被吞掉，写大字"没有在录 / 松开，点一下再按住"，Mando 歪头不打盹，留到卡片熄灭或者再按一个键；锁着时录音结束写结果（"√ 已存 0:12"、"× 太短，未存"、自动停的原因）；牌子写等你 N（没有数据不画；Mac 断开前留下的，卡片顶上写"已锁屏 · 离线"）、录N、↑N 或"√ 已同步"；电量低时变色。
- 录音页（B2）：便条 4:30 起写"还剩 0:30"；会议页保留"电 N%"；30% 以下开始框写"电量约够 N 分钟"。G0 一按下（这次会录的时候）就显示"● 准备"（D8）。
- 主页（B4）：离线时问候语、最急一条和 Mando 都说离线；最急一条先列等你的、再列最近空闲的，空闲一天以上的收起来；不能录音时 R 格变灰、G0 提示写原因。看板上结果未知的开 session 标"开?"（B5）。
- `cpa-stt`（docs/22 §4，VC-1，#52）：在这台 Mac 上转写录音的独立程序，录音契约 v1 的又一个转写程序，和 WhisperWoof 同一个位置；cpad 里没有转写代码。**默认关**：`cpa-stt setup` 找到已有的 ggml 模型（`~/.cache/openwhispr/whisper-models` 和 Spotlight，问过你才链接，不复制、不下载），`cpa-stt use` 用 whisper.cpp 示例配置（large-v3-turbo、自动判断语言、贪心、有模型时加 VAD、项目名当提示词）或你的配置开，`cpa-stt run --once` / `watch` 跑，`cpa-stt service install` 才装 LaunchAgent。命令一律包在 `sandbox-exec` 里（不能联网，只能写自己的临时目录和 Metal 缓存），不经 shell；静音门限、VAD 和幻觉句（含繁体）三道拦截；写 `status.json` → `transcript.*` → `result.json`（最后、原子），有 `result.json` 的目录不碰；长会议的输出上限随时长增加，超了算失败，不留截断的转写。一次只有一个转写程序：认领文件用 `link()` 独占创建，空闲时也续，转写中认领丢了就杀掉命令、不再写；别人 5 分钟内写过的目录不碰，更旧的是遗留文件。环境坏了只写 `blocked`，修好后 `cpa-stt retry`（在跑的 `watch` 也收得到）或一分钟内自动接着转；一个目录读写不了不影响别的目录。录音契约 v1 只新增：认领文件 `recordings/.transcriber` 和它的规则、`status.json` 的 `by` 和 `reason: engine`、`manifest.json` 的 `cap`；`cpa status` 多一行"transcriber"。
- Mando 伴侣主题（docs/19，#45）：WhisperWoof 的暖棕配色（画布仍是 8 位，换成索引色板）、圆角卡片和键帽、主页九宫格和项目页、等你、锁卡、录音页、Tab 菜单和状态栏全部重画；Mando 有八种样子（在呢、在听、嗅一嗅、等你、存好了跳一下、没听到、打盹、离线），计时和时钟用 Nunito 大数字。只在全亮时动：200 ms 节拍、局部刷新、呼吸的录音 ●、滚动的波形、带 ★ 的会议时间线、Tab 菜单从右边滑入；录音时屏幕的 SPI 时间从约 104 降到 21–30 ms/s。`lib/theme` 是给模块和第三方应用的公开画法（卡片、键帽、胶囊、牌子、条纹、标题、气泡、`hint_fits`）；图和字体的版权见 NOTICE。
- 测试版固件的 `debug.state.diag`（#21）：空闲堆和最低值、最大块、主循环和音频任务的栈余量、循环耗时计数、键盘计数；录过音后 `debug.state.rec` 带健康计数。协议 v0 只新增（protocol/README 第 21 条），正式固件不带。
- `cpa queue reopen <qid>`：设备上 Opt+D 点错了行，在 Mac 上撤销（U12）。`events.jsonl` 新增 `queue.reopened`，带着原项的字段。
- 协议 v0 只新增：`queue.snapshot` 每项可选字段 `uncertain`（protocol/README 第 22 条）。从这样的项开 session 时，确认框在标题下另起一行写"可能已开过，先看 Sessions"（U25）。
- 菜单"控制"页加状态栏说明：↑N、!N、录N、※kN 各一句整话，每个计数只用一个名字，状态栏上画着的才写（↑N 一直有，其余不为 0 时）；说明和声音、电池等事实行的值对齐成一列（U21）。
- 按了不起作用的键给一句提示，同一句 3 秒内不重复（U17）：编辑器开着时按 Opt+H/I/P/M、会议页上按位置键、菜单里按说明行（"先按 Tab 关菜单，再按 Opt+1-9"）、列表里选中的那条已经不在了（"按 ; . 选一条"）。Fn+`;` `.` `,` `/` 在列表、主页和菜单上当方向键用。
- 录音时电压到 3.55 V 提示一次"电量低：录音可能很快自动停止"（docs/11 §4.3）。
- 运行时模糊测试进 CI（`firmware/sim/test/test_fuzz_runtime`，每次约 3 秒），`fuzz-nightly.yml` 每晚跑完整矩阵（docs/20 §4.5）。
- 双击 G0 免提录音、再按一下结束（gh #51，docs/20 U26）：第二下（第一下松开后 0.35 秒内；慢一点到 1 秒也算，空的记一条会关上）一按下就开始录，松开也继续；页面写"免提录音 · 按 G0 结束"（最后 30 秒写"免提 · G0 结束"，旁边是"还剩 0:30"）；再按一下 G0 结束并保存（点两下结束也行，第二下不会打开记一条）。录的时候 1–9 / 0 归项目，Tab 菜单（"G0 结束并保存"一行只是说明，Enter 不结束；菜单开着时按 G0 也结束，按 Opt+L 菜单关掉、锁屏、录音不停），Opt+L 锁屏、录音不停，别的键不起作用；5 分钟上限照旧；用电池时屏幕只变暗、不熄、不自动上锁，录完才照常熄屏上锁。点两下只在 G0 点一下是"记一条"的地方算：主页和各页、Tab 菜单开着时（第一下关菜单）；编辑器、确认框、会议开始框里不算。锁屏上只在按键点亮的卡片上算（黑屏时、卡片自己亮着时第一下只亮卡片）；免提录着锁住时，黑屏按 G0 只亮卡片（写"免提录音中：按 G0 结束"，不会写"没有在录"），卡片亮着再按一下结束，卡片写结果（"√ 已存 0:12"），黑屏的角上写"● 免提 0:12"。点一下再按住（第二下按到 0.5 秒）仍是按住说；空的记一条里按住 G0 是关掉它、按住说（有字的编辑器里按住照 D4 另录一段，草稿留着）。Tab 菜单"去哪"有"双击G0 免提说一段"，主页提示栏写"G0 按住看到●REC再说/双击免提"，锁卡写"先点 G0 再按住说 · 点两下免提"。
### Changed
- 按键（docs/20 D1–D9，你 2026-10-22 同意了默认值；对照表见 docs/releases/v0.4.1.md 的"按键变化"）：
  - 主页上 R、L 要按 Opt（Opt+R 开会、Opt+L 锁屏），按不是格子的字母每进一次主页提示一次（D1）。
  - 用电池时屏幕一熄就上锁，插着 Mac 不锁；锁着时只有先点一下 G0 才能按住录便条（D2、U3）。
  - 会议的开始框和结束框：Opt+L 取消并锁屏 / 继续录并锁屏；开始框 15 秒、结束框 10 秒没人按就走（从最后一次按键算，Tab 菜单开着时不走）；开始框上打字是取消，不算录音；太快的 Enter 提示"再按一次 Enter"；会议页上 ` 和 Enter 只提示"开会中：Opt+R 结束"（D3）。会议确认框的文案没改。
  - 编辑器、确认框、菜单开着时按住 G0 录便条，草稿留着（D4）。
  - 编辑器里 ` 退出（有字先问"放弃？"），Fn+` 打出反引号（D5）；开 session、回复的确认框上 ` 回到编辑器、字留着（D6）。
  - 锁屏、锁着时录音结束、插着 Mac 时等你数增加，锁卡亮 2 / 2 / 3 秒（D7）。
  - 菜单开着时按菜单上写着的组合键，菜单关掉再执行（D9）。
- 确认框的卡片、提示栏和 Tab 菜单用同一个动词："Enter 开始 · ` 返回修改"、"Enter 存为排队"、"Enter 关闭"（B6）。
- 没锁时录音自己停了（电量低、到上限、卡满），屏幕像按了一下键一样亮起来，结果看得见，再照常变暗、熄屏、上锁（D7）。锁卡被按住的 G0 点亮时最多多亮 10 秒。
- 固件版本报 `cpa-fw/0.4.1`。
- 点一下 G0 的记一条要等 0.35 秒（看是不是点两下）才打开；这期间按别的键，记一条马上打开、这个键落进去（gh #51）。HIL 的采集页延迟上限从 300 ms 放宽到 650 ms。
- 提示条（U20）：自己操作的结果马上显示，顶掉排着的"X 等你"（状态栏的 !N 还在）；错误显示 4.5 秒；锁着或黑屏时提示条不过期，亮屏后只留最近一条录音结果和错误，之后的操作结果排在它们前面。
- 录音结束都说清怎么结束、录了多长；自动停止和拒绝开始分开写："会议已停：电量低 · 已存 42:10"、"已到 5 分钟上限 · 已存 5:00"、"会议已停：SD 卡满 · 已存 …"（U6）。最近一次录音的结果留到亮屏看过一次（给锁卡用，锁卡的画法随主题）。
- 便条到 5 分钟上限时 G0 还按着，屏幕停在"已到 5 分钟，已保存 / 松开 G0 / 要录更久：松开 G0，再按 Opt+R 开会"，直到松开；用电池时也不熄（U15）。
- 确认框和它的提示条（U7）：一行写得下，不再提"v0.4"；在等你的 session 不再说"正在运行"；存成排队的写"只存到 Mac 队列（不会发给 session）"。
- 文案统一（U22）：记一条 · 排队给 AI · 开 session · 回复；年龄用中文、四舍五入（"2 小时"，列表行右边写紧凑的"45分钟"，看板"更新于 2 天前"）；终端 / 后台代替 tty / bg；写"Opt+1-9"。
- 用电池时熄屏前先变暗 10 秒，变暗时按键照常生效（U24）。
- "放弃草稿？"上的 Enter 有 400 ms 防误触，这时 Tab 菜单第一行是"返回编辑"（U18）。
- 没有项目的槽位（U13）：排队或开 session 到不存在的项目，提示"Opt+数字：1 acmeshop · 2 cardputer"（放不下时"… 共 9 个"），草稿留着，不再报 E_STORAGE；数字键按空槽位原地提示"没有第 5 个项目（共 2 个）"；看板"其他"一行按 Enter 提示"其他项目只在 Mac 上看"。
- SD 卡运行中出错（U16）：提示"SD 卡出错：插好后需重启"；有内部存储时这期间的采集先存进内部 cpafs，下次开机迁到卡上；"录音需要 SD 卡：插卡后重启"。
- cpad：下发 session 名时去掉它自己项目的 `key/` 前缀（设备显示时会加）；中文口述的任务用开头 8 个字起名，不再是 `task-N`（U14）。
### Fixed
- 便条不再说着说着断掉（gh #51，docs/20 U26）：G0 的松开要两次相隔至少 30 ms 的采样都读到才算（M5Unified 的去抖在循环慢于 10 ms 时不起作用，一次接触不良就当成了松开）；松开后还有 0.6 秒宽限，页面写"松开了 · 0.6 秒内按回继续"（最后 30 秒写"0.6 秒内按回继续"），按回来还是同一段，没按回才结束并保存（宽限算进 1 秒的最短长度）。宽限在哪儿都一样：编辑器、确认框、Tab 菜单上按住录的（D4），锁卡上录的；按回的那一下只属于这段录音，不会提交草稿、不会打开记一条。一轮循环很长时先读按键再看宽限，按回不会晚一步。
- 波形跟着声音动、不被房间嗡声带着动：电平改按 dB 画（-60 dBFS 是一个点、-24 dBFS 满格），说话的块占 5–9 格，屋里的底噪是点；只在电平这条路上先过一个 4 阶 130 Hz 高通（两节双二阶），35–68 Hz 的嗡声压低 22–46 dB，WAV 不变；Mando 的"在听/点头"跟着同一个电平（-50 dBFS 起，-56 dBFS 以上保持）。用真实便条的电平做了回归测试。
- 从 Tab 菜单开始的免提便条到 5 分钟上限时，不再停在"松开 G0"的屏幕上（gh #51 审查）。
- 会议第 0 段以后才设置或清除的项目标签，在 Mac 上以最新的为准（manifest、归档记录、历史页），最后存下的那段带着最新的标签（#46，docs/11 §3.2）。
- cpad 按 8 位 sid 去重，按显示顺序留第一条：一个重复的 sid 不会再让整个 `sessions.snapshot` 被设备拒收；看板上的数字和 Sessions 页的行数一致。
- 测试：`test_runtime_budget` 的快照字符串正好顶到协议上限（24/32/80 字节），预算按真的中文数据量算；HIL 的 `Device.home()` 关掉非空的采集页时不再在"放弃？"上来回切换，按 Enter 之前等过 400 ms 防误触。
- 录音收尾事件丢了，设备卡在收尾，所有键都没用，只能重启（S1）：5 秒后再发一次停止，10 秒后回到空闲并提示"录音收尾超时：已写下的段还在卡上"。每段录音带一个代号，迟到的结束事件只认自己那一段，不会结束下一段。
- D1–D10 做完后的审查找到的 9 处（docs/20 §5.6）：锁卡在被吞的按住松开后翻回旧的"√ 已存"、Mando 说"存好了"；确认框和会议开始框的菜单写"Enter 确认"；锁卡在没有数据时写"等你 0"；"先处理确认框"在框没了以后还在；HIL `latency` 用 Fn+` 关不掉空编辑器（`keys.py` 打反引号改按 Fn+`）；结束框在 Tab 菜单下面走完，之后的 Enter 落在会议页上什么都不说；被吞的按住让锁卡一直全亮；没锁的会议在电池上自己停了，结果从来看不到；会被拒的时候（卡满、电量低等）也显示"● 准备"。
- 列表刷新后 Enter / Opt+D 落到别的条目（U12）：收件箱和看板按 sid / qid 记住选中的那一条；那一条不在了就什么都不选中，直到移动光标；Opt+D 的提示写出是哪一条（"已完成：…"、"已忽略：…"）。
- 不用 editable 方式安装的 cpad（`uv tool install ./daemon`）每条命令都报 `FileNotFoundError: .../lib/protocol/limits.json`（#57）：`limits.json` 现在随包进 wheel，装好的 cpad 不再需要仓库；CI 会构建 wheel，装进仓库外的新 venv 里跑一遍。
- `cpa rec ls` 和 `cpa rec show` 的时长：10 分钟以内显示到秒（`51s`、`4m59s`），短便条不再都显示成 `1m`；更长的仍按分钟（`42m`）。

## [v0.4.0] - 2026-10-21
### Added
- 主页启动器（Opt+H）：九宫格，格子上的字母就是快捷键（P 项目、I 等你、R 开会、N 记一条、Q 排队给 AI、S 开 session、M 系统、L 锁屏、Tab 菜单），1–9 打开第 N 个项目页；格子上显示跑N、!N，下面一行是最急的一条"等你"。主页是导航栈的根页面。
- Tab 常驻菜单，任何页面都能按（便条进行中和锁屏时除外）。三页：本页（这一页的键）、去哪、控制（锁屏，以及只读的声音、电池、SD 卡、Mac 链路）。选中一行按 Enter，等于按那一行的键；Tab 再按字母等于按 Opt+那个字母（Tab、P 两下到项目看板）。主页、项目看板、等你、系统页、采集页和会议页的提示栏最右边写"Tab菜单"（确认框的提示不变）。在采集页、确认框、会议页上只有"本页"和"控制"，"本页"上的字母不起作用；采集页的"放弃已输入的内容？"按确认框处理。
- Opt+L 锁屏：马上熄屏，插着 Mac 也熄屏，屏幕已经熄灭时按也直接锁上；解锁是 Opt+L 再在 3 秒内按 Enter。锁住时点一下 G0 亮出锁屏卡片（只显示数量），卡片亮着时按住 G0 才录便条（锁在草稿上也一样），屏幕黑着时按住 G0 只亮屏；开会时 G0 仍是加 ★，录音不停。锁屏不是安全锁（SECURITY.md）。
- 测试固件的 `debug.state` 新增可选字段 `ui{menu, locked}`（协议 v0 只新增，`v` 仍为 0）。
- v0.4.1 的协议和 Mac 侧（设备界面还没有）：协议 v0 只新增主机 `hello` 的 `caps.recs`/`caps.todo`、`host_id`、`host_label`，笔记和 `file.items` 的 `dest`，`rec.list`/`rec.items`/`rec.act`；cpad 回答录音历史和 ★/项目/隐藏（只追加事件，不碰 `manifest.json`），`dest` 为 `todo` 的笔记和便条写成 `- [ ]`，`cpa rec ls --hidden`、`cpa rec unhide`；固件只加了编解码，运行时不发这些消息。
### Changed
- 项目看板从 Opt+H 挪到 Opt+P；Opt+H 改成主页。Opt+M 仍是系统。
- 从项目看板总览打开的项目页，按返回先回到总览（以前直接回到根页面，主页成了根页面以后会跳过总览）。
- 采集页的提示改成"退格 退出  Opt N/Q/S 类型  Tab菜单"和"Enter 保存  Fn+` 退出  Tab菜单"，会议页的提示改成"G0/空格★ 1-9项目 Opt+R结束 Tab菜单"；改目标项目的 Opt+0-9 写在菜单的"本页"里。
- 录音时 ● 永远看得见：锁屏、菜单打开、弹出提示时都画出 ● 和计时，背光最暗只到调暗。
- 锁住时不弹提示（"等你"提示会带出项目和 session 名）。
- 按 Tab 现在产生 `Nav::Menu` 事件（以前没有事件）。
- 会议确认框的文案和 v0.3.1 一样，没有改。
- 固件版本报 `cpa-fw/0.4.0`。

## [v0.3.2] - 2026-10-21
### Fixed
- 开机后的第一次录音开头约 1 秒全是 0（#31）：ES8311 上电后会把输出压成 0 约 2^14 个时钟帧。音频任务做完开机扫描后用 48 kHz 预热 codec 一次（约 0.5 s，不出声、不写文件）；开始录音后，等到第一块没被压住的声音才显示 `● REC`、开始写文件，前面被压住的部分丢掉，最多等 2 s。录音的时间（`at.up_ms`、★、`lead_ms`）都从写下的第一个样本算。
### Added
- Claude Code hooks（#33）：`cpa hooks print|install|uninstall`（加 `--yes` 才写 `~/.claude/settings.json`，旧文件备份到 `$CPA_HOME/hook-backups/`）和 `cpa hook`（Claude Code 对每个事件调用，只把白名单字段经 `cpad.sock` 发给 daemon）。看板在 hook 触发时就更新，并带上等你的原因（权限、计划、提问、登录）；轮询仍然是最终依据。`cpa status --json` 新增 `hooks`。终端 session 只在已信任的目录里运行 hooks。
- 开 session 失败的原因（#25）：`agent.result` 和 `agent.spawn_failed` 事件新增可选字段 `why`（`untrusted` | `login`，协议 v0 只新增）；`cpa status` 的 claude 行显示 `login=`，`warning:` 行给出一次性修复命令。cpad 从不替用户接受信任或登录。
- 本地数据访问（#34）：`cpa export`（导出成文件夹或 zip，附 SHA256SUMS，拒绝云同步目录）和 `cpa mcp serve`（stdio 的本地 MCP 服务，只读为默认，写入要逐项打开，每次调用都记审计）；`cpa mcp setup` 打印 Claude Code、Codex、Cursor 的配置。
- 主机模拟器（#32）：`firmware/sim` 在 Mac 上跑固件，用真实字体出截图，或开一个 pty 虚拟设备给 cpad 和 HIL 用。
- 测试固件的 `debug.codec{warm}`（协议 v0 只新增）：把 codec 软复位到刚上电的状态，可选再跑一遍开机预热；HIL 场景 `rec_cold_start` 和设备测试 `test_mic_cold`。
### Changed
- "等你"的判定（#5）：后台 session 的 `state` 是 `blocked` 而且没在跑，也立即算等你；解析 Claude Code 2.1.306 的 `waitingFor`（permission / question / dialog）。
- 固件版本报 `cpa-fw/0.3.2`。

## [v0.3.1] - 2026-10-21
### Changed
- 录音全程不出声（用户决定：录音不出声，只给自己用）：便条开始和已保存、会议开始和已保存、太短或取消、拒绝或失败都不再响；会议开始前的确认框（"请先告知在场的人"）照旧；屏幕是唯一提示（录音页的 `● REC` 和电平条、状态栏的 `●12:31`、原有的拒绝说明）。
- 便条准备时屏幕上的提示从"听到嘀声后说话"改成"看到 ● REC 再说话"。
- 录音电平调高：增益放进 ES8311（PGA 和 ADC 音量，每次开麦克风后写入并读回），`magnification` 改为不放大，样本保留完整 16 位精度；手持正常说话的峰值目标 −12 dBFS，便条和会议共用（定下的值见 docs/releases/v0.3.1.md）。测试固件新增 `debug.mic`（协议 v0 只新增）。
- 固件版本报 `cpa-fw/0.3.1`。

## [v0.3.0] - 2026-10-21
### Added
- 随身 Granola：按住 G0 录语音备忘；Opt+R → Enter（先确认已告知在场的人）开始会议录音，开始和结束的提示音不能关，会议中可以加 ★ 标记；录音存到 SD 卡，按段无缝切分，断电后能恢复。
- `file.*` 传输协议（带 CRC、可断点续传、Mac 确认后设备才删除），实测约 330 KB/s；录音契约 v1。
- cpad 拉取录音并做 SHA-256 校验、写清单、等 WhisperWoof 产出结果、在当天笔记里加一行；音频保留 30 天；新增 `cpa rec ls/show`。
- Launcher 安装工具（tools/launcher）。
### Fixed
- SD 驱动：多块写入结束后没有等卡忙完（大文件写到约 8 MB 报 EIO）；复位打断写入后卡挂不上（sd_wake）；断电截断的录音会话在同一次连接里归档。

## [v0.2.0] - 2026-10-20
### Added
- microSD 存储：待同步条目存到 SD 卡，用不依赖改名的提交协议；没插卡时退回内部分区 `cpafs`；插卡开机时恰好一次地迁移旧条目。
- 兼容 bmorcelli Launcher：分区改用专属标签 `cpafs`，启动时检测是否经 Launcher 启动；发布产物改为 `cpa-<tag>.bin` 和 `cpa-<tag>-app.bin`。
- 协议 `hello.store`、`hello.launcher`（可选字段，`v` 仍为 0）；`cpa status` 显示存储位置和启动方式。
- 单键返回（#15）。
### Fixed
- 控制 socket 路径过长时的报错说明（#4）；补丁版 SD 驱动（ACMD41）；CI 上的 GCC 告警和测试竞态；上传脚本缩短 daemon 暂停时间的问题。

## [v0.1.0] - 2026-10-20
### Added
- USB 闭环 MVP：G0 速记 → 设备 outbox → Mac 每日笔记；Claude Code 项目看板；"等你"收件箱；项目队列；开新 session（默认 plan 模式）。
- 协议 v0 契约（`protocol/`）：19 种消息、8 条流程、测试向量。
- Mac daemon `cpad` 和命令行 `cpa`，以 LaunchAgent 方式常驻。
- 真机 HIL 测试工具（`tools/hil/`）和人工检查单。
- CI：协议契约、固件 native 测试和编译、daemon 测试、文件和函数规模检查；CD：打 tag 时编译固件并发布 GitHub Release。
