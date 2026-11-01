[English](27-new-mac-setup.md) · **中文**

# 27 · 新 Mac：装一次，插上就认出同一台 Cardputer

> 状态：**设计稿，已按三份评审（用户、安全、可行性）改过，钥匙和钉住的说法已对齐 docs/26 定稿，2026-10-22 又和 docs/26 交叉核对过（附末行），待你拍板**（§9）· 日期：2026-10-22 · 角色：架构（Mac 安装和首次设置）· 基于 origin/main `995821d`（代码和 `d330465` 一样，含 cpa-stt #62，多了 docs/25）· 分支 `docs/new-mac-onboarding` · issue #64 · 关联 docs/18、docs/22、docs/23、docs/24（`wt-mac`）、docs/25（PR #65，已合进 main `995821d`）、docs/26（`wt-sec`，#63，PR #68，`96a03b6`，交叉核对后 `8fc1f4a`）、#57、#60、#62。
> 需求（2026-10-22）：把一轮安全测试定为以后的里程碑，确保所有数据落盘加密；同一台设备方便地用在几台 Mac 上，新 Mac 的首次设置要顺：装好 Mac 端，并让 Mac 认出这台设备。
> 依据：README §快速上手；SECURITY.md；docs/01 §1；docs/04 US-04、S1；docs/05 §5.10、§6.4、§8；docs/07 第 7 条和第 14 行；docs/15 §2.4；docs/18 §1.3、§3–§5、§8、§9、§10.3、§13、§15；docs/21 §4、第 424 行；docs/22 §4.4、§4.10、§4.13；docs/23 §3.1、§4.2、D10；docs/24 §1.2、§2.3、§2.5、§4、§5.2、§7、D8、D10、D12；docs/25 §2.3–§2.4、§5.5、§5.7；docs/26 §3.1、§4.3–§4.5、§5、§6、§7、D1、D4–D7、D10、D13、D17、D21、D23；`protocol/README.md` 第 49、103、154 行；daemon 的 `app.py`、`device/`、`config/`、`service/launchd.py`、`cli/`、`projects/registry.py`、`hub/reduce.py`、`mcp/`、`archive/cloud.py`、`recordings/`、`cpa_stt/`；固件的 `link_session.cpp`、`runtime_outbox.cpp`、`messages.h`、`hal_platform.h`；`tools/hil/harness/rig.py`、`tools/sim/`；`.github/workflows/{ci,release}.yml`；issue #57、PR #60、#62；一份只读调研（本机文件名和元数据、Homebrew 5.0、Gatekeeper、SMAppService、uv 缓存的公开文档）。
> 标记：**[源]** 读过的代码或文档 · **[测]** 本机只读实测 · **[研]** 外部文档 · **[推]** 推断 · **[设]** 设计假设，可以改 · **[待]** 要用测试验证 · **👤** 需要你。
> 范围：只写设计。没有接设备、没有开串口、没有改代码和系统设置；`~/CardputerNotes`、`$CPA_HOME` 只看了文件名。
> 路径缩写：`cpad/` = `daemon/src/cpad/`；`stt/` = `daemon/src/cpa_stt/`；`fw/` = `firmware/app/lib/`；`$CPA_HOME` = `~/Library/Application Support/CardputerAssistant`。

> **修订（2026-10-26，分支 `feat/installer`）**：你 10-26 要求一条 curl 命令就能装好，参照 WhisperWoof 的新电脑安装。D1 改成**从发布资产一行安装、逐个核对摘要**：`scripts/install.sh`（公开后 `curl … | bash`；私有仓库时 `gh api … | bash`）。§5.1 的"不做 curl 一行"作废；它担心的"网址替你跑脚本"，换成"脚本只装发布里、哈希对得上的文件"：每个资产核对 GitHub API 的 `digest` 和发布里的 `SHA256SUMS`，带隔离属性就拒绝；整个脚本包在 `main()` 里、最后一行才调用，下载到一半不会执行。app 是发布里的 zip（Mac 上用 `scripts/release-local.sh` 造、ad-hoc 签名）；curl 下载的文件没有隔离属性，Gatekeeper 不拦，也不需要去掉隔离属性。"拒绝带隔离属性的 checkout"只对从源码装的路（§2.2 的 `mac-install.sh`，没做）适用。`cpa setup check --json`（D3，`protocol/setup/v1`）先做了最小的 7 行；O3（你点了才连）还没做，所以安装脚本装 LaunchAgent 之前先问"在主力 Mac 上同步过了吗"。

> **评审后（2026-10-26，同一分支）**：新 Mac 上没有 Homebrew，所以私有仓库的首选改成只读 fine-grained 令牌 + `curl -K <(printf …)`（令牌不进参数），安装脚本用 `printf … | curl -K -` 把令牌交给 curl（不写文件），只发给本仓库的 `releases/assets/` 接口，令牌只许 `[A-Za-z0-9_]`；`--from DIR` 从 `release-local.sh --release-json` 造的文件夹（U 盘）装。最后一屏只有一张清单 `cpa setup check --todo`（只列没好的、中英文、标必需），加一行 `claude`（第 8 行：后台服务认不认得 Claude Code；`setup/v1` 只新增）。安全修补：`--prefix` 不许带引号、`$`、反引号和反斜杠；`xattr | grep -q` 在 pipefail 下会漏（SIGPIPE），改成先取再比；Ctrl-C 也清临时文件；curl 一律 `--proto =https --tlsv1.2`；`release-local.sh --out` 只清它自己造的文件夹，不打包 git 不跟踪的文件；`uninstall.sh --purge-data` 不删装着笔记目录的 `$CPA_HOME`。**信任边界**：发布没有签名，信任来自仓库的写权限和到 GitHub 的 HTTPS（API 的 `digest` 和 `SHA256SUMS` 来自同一个发布）；单人维护的私有仓库可以接受。uv 拉的 Python 3.12（python-build-standalone）由 uv 用它内置的哈希核对。发布期间 `--clobber` 替换资产的那几秒里安装会因为摘要对不上而失败，不会装上错的东西。

## 0. 一页摘要

1. **你要的**：同一台 Cardputer 用在好几台 Mac 上；新 Mac 装起来省事；Mac 认得你的设备，设备也认得新 Mac；安全测试和"数据都加密"定成以后的里程碑。加密和安全测试的总设计在 docs/26，本文管新 Mac 那一块。
2. **三种用法**（§9.2 第 1 条问你是哪种）：**A** 家里一台 Mac、公司一台（默认）；**B** 一台笔记本来回带；**C** 几台 Mac 都是你自己的、算同一个档案（比如台式加笔记本）。
3. **今天**（§1）：装一台 Mac 要 6–9 条手工命令，四份文档写了四种装法；版本号一直是 0.1.0，升级会装回旧代码（#57）。第二台 Mac 插上设备时**不问你就接管**：设备上还没传的笔记和录音存到它那里、从设备上删掉，回不去。
4. **目标**（§2）：终端里粘贴 5 次（装工具、登录 GitHub、取代码并安装；仓库公开后 3 次）。然后 Mac app 的"首次设置"页按顺序一行一项：后台服务、笔记目录、这台 Mac 的名字、搬设置（用法 C 才用）、设备、家 / 公司。**最少 2 次点击**（选笔记目录、连上设备），设备分档案以后再在设备上按 3–4 下。可选的几行（本地转写、Claude Code、别的 app、去向）收在"可选"下面，不催你，每行都有"不用"。
5. **Mac 认设备**（§3.1）：没连过的板子，Mac **不和它说话**，只读 USB 上的序列号，算出 4 位设备码（比如 3F7A）给你看。你点"连上"才开始同步。按钮上面用大白话写着：设备上还没传的笔记和录音会存到这台 Mac，设备上的会删掉，存过来就回不去。也可以点"只给它充电"，这台 Mac 以后就不碰它。
6. **设备认 Mac**（§3.2）：照 docs/18 v0.8，在设备上选这台 Mac 属于家、公司还是都不是，Mac 自己说的不算。Mac 屏幕上写着确切的按键：「家」按 `.` 一下、Enter 两下；「公司」按 `.` 两下、Enter 两下。选了"以后再说"或者选错了，屏幕上也写着怎么改。
7. **v0.8 之前**（§3.3）：默认把 docs/18 的最小档案提前到下一个固件版本（§9.2 第 2 条）。在那之前靠第 5 条的"你点了才连"，加上 README 里现在就写上的提醒：去另一台 Mac 之前，先在主力 Mac 上插一下，等 ↑ 和 录 变成 0。
8. **跨 Mac**（§4）：笔记和录音留在收到它的那台 Mac；每台 Mac 的身份从不跨；加密私钥不进任何导出，只跟着迁移助理和 Time Machine 走（docs/26 D6）；设置（1–9、去向）可以导出成一个文件，AirDrop 过去再导入。导入**不会放宽权限**：项目一律用最保守的权限（只做计划）进来；会执行命令的去向和你自己写的链接不搬；会把字送出 Mac 的去向，要你在新 Mac 上再点一次。
9. **安装**（§5）：没有 Apple 开发者证书，所以在你自己的 Mac 上从源码造。脚本自己挑最新的发布版本、造好、装上、开好后台服务；升级只要 `mac-install.sh --upgrade` 一条。本机造的东西 Gatekeeper 不拦，也不用关任何安全设置。
10. **换 Mac、换设备**（§6）：用迁移助理搬过去以后，新 Mac 换一个新身份，设备加密以后也换一把新钥匙、旧的留着只解不封（docs/26 D23），设备会再问你一次，"名字"那一行也会请你改；换了设备，"设备"那一行写"换了设备？"，点一下就改过来。
11. **加密和安全测试**（§3.5、§7）：设备上的文件加密照 docs/26 做（每个文件封给 Mac 的公钥，设备上不留能解开它的东西）。Mac 的私钥是 `$CPA_HOME/keys/seal/` 下的 0600 文件，不放钥匙串（docs/26 D6）。新 Mac 这边的规矩照 docs/26 D21：Mac 的公钥只在你在设备上点头时收下并钉住；Mac 每次握手都要证明有私钥，证明了设备才交封好的东西、才认它的 ack 和删除，钉住以后证明之前什么都不给；换钥匙要用旧钥匙证明，证明不了就在设备上问你，光标在"不换"。Mac 上的笔记靠 FileVault 和加密的 Time Machine，首次设置里有一行只读的"磁盘加密"，告诉你开没开。安全测试并进 docs/26 的里程碑 M-SEC：Mac 这部分在 O0–O6 合并后跑（不测加密），加密那部分在 M-SEC 第二轮（加密轮，docs/26 SEC-6）里跑。
12. **分期**（§8）：先做发版卫生（O0），紧接着做"你点了才连"（O3，只动 Mac，不改协议），然后才是首次设置（O2）。O0–O5 约 2,200 行代码、2,700 行测试，不等固件。
13. **21 条决定**都有默认（§9.1）；要你回答的只有 4 件（§9.2），后面附一行回复模板。

## 1. 你要的，和今天差在哪

### 1.1 需求逐条

- 定一个里程碑，跑一轮安全测试。本文：里程碑是 docs/26 §6 的 M-SEC；本文 §7.2 给它加新 Mac 的用例。
- 所有数据落盘加密。本文：总设计在 docs/26（设备逐个文件封装、Mac 整盘加密）；本文管新 Mac 怎么交公钥（§3.5）和"磁盘加密"一行（§2.3）。
- 一台设备，多台 Mac。本文：§3、§4。
- 第一次启动的设置流程更顺。本文：§2。
- 新 Mac 一次装好。本文：§2.2、§5。
- Mac 端装好；Mac 认得这台设备。本文：§2.3、§3.1。

### 1.2 今天装一台 Mac

| # | 步骤 | 每台都要 | 坑 |
|---|---|---|---|
| 1 | 命令行工具（git）、uv、数据线；第一次插上点"允许配件连接" | 是 | 不点允许只充电、没数据 [源 docs/01 §1] |
| 2 | 取代码：仓库是**私有**的，先 `gh auth login` [测] | 是 | README 没写 |
| 3 | 装 `cpa`：README 写 `uv sync --project daemon` [源 README.md:38]，daemon/README 写 `uv tool install ./daemon`，#57 写 `--force --reinstall --no-cache`，#62 写 `--reinstall` | 是 | 版本一直是 `0.1.0` [源 pyproject.toml:3]，uv 按 `pyproject.toml` 认缓存，升级装回旧 wheel；`cpa --version` 和 hello 里的 `cpad/0.1.0` 分不出新旧 |
| 4 | `cpa service install`：LaunchAgent 记下**跑它的那个 Python** [源 cli/main.py:143-162] | 是 | 照 README 用 `uv run --project daemon` 跑，服务就跑在 checkout 的 `.venv` 上（#57 的根）；`launchctl bootstrap` 的返回码没人看，失败了也打印"installed" [源 service/launchd.py:69-80] |
| 5 | `$CPA_HOME` 和 `host_id` 自动生成 [源 config/host.py] | 是 | `host_label` 默认空，`cpa host name` 还没合并；只能手改 config.json |
| 6 | 设备绑定：自动，**第一块应答 `cpa-fw/` 的板子** [源 app.py:260-263；port_finder.py:50-54] | 是 | 不问、不列；只有 `cpa device forget` |
| 7 | `cpa project add KEY PATH --slot N`（2026-10-26 起：`cpa project add -- 名字`，目录可选，也可以在 Mac app 的"项目"页建，docs/25 D24） | 是 | 编号每台 Mac 一套，设备只缓存一份（§1.4 F3） |
| 8 | Claude Code：登录、每个项目目录信任一次；`cpa hooks install --yes` | 是 | hooks 写的是**跑它的那个 `cpa` 的绝对路径** [源 hook_settings.py:57-64]；换装法后指向旧路径 |
| 9 | MCP：`cpa mcp setup <client>` 打印片段，自己贴 [源 mcp/cli.py:107-109] | 每个客户端 | — |
| 10 | cpa-stt：`cpa-stt setup` → `use` → `service install`；要 Homebrew 的 `whisper-cli` 和 1.6 GB 模型 | 是 | **从不下载**模型 [源 stt/models.py:1-8]；`setup` 只做链接、不拷贝，原文件被挪走就 `model_missing` [源 stt/cli_setup.py:19-23]；和 WhisperWoof 只能开一个 [源 docs/22 §4.10] |
| 11 | Mac app：本机 SwiftPM 造、ad-hoc 签名 [源 docs/24 §7] | 是 | 打包脚本还没写（`wt-mac/mac/scripts` 里只有 `export_mando.py`）[测] |
| 12 | 笔记目录：默认 `~/CardputerNotes`，没有命令能改 | 是 | 放 `~/Documents` 下 LaunchAgent 写不进去（TCC）[源 docs/gates/whisperwoof.md:95]；`cpa status` 只对 iCloud Drive 报警 [源 recordings/service.py:48-52]；cpad 和 cpa-stt 都只在启动时读一次笔记目录 [源 app.py:116；stt/cli.py:108-110、327] |

本机 [测]：`cpad 0.1.0` 是 uv 工具，`~/.local/bin/cpa` 指进 uv 工具目录；LaunchAgent 跑 uv 工具的 Python；只有命令行工具、没有 Xcode；签名身份 0 个。release.yml 只发固件（`cpa-<tag>.bin`、`-app.bin`、`SHA256SUMS`），没有 Mac 端的包 [源]。

### 1.3 第二台 Mac 插上同一台设备

1. Mac B 的 cpad 没绑定，第一块 `cpa-fw/` 板子就合格；它发 hello，带 **B 的 `host_id`**，`host_label` 多半为空 [源 app.py:245-248]。设备收到任何主机 hello 都进入 Ready [源 fw/link/link_session.cpp:74-86]，存下 `host_id`、什么都不做 [源 messages.h:80]；"控制"页两台都显示 `cpad/0.1.0`。
2. B 把序列号写进**自己的** config.json。A 的绑定不变：两台 Mac 都"绑着"同一块板。
3. B 对时（最后一台说了算 [源 docs/05 §5.10]），握手后推全套快照，里面有项目列表 [源 hub/reduce.py:76-77]：设备上**唯一一份** 1–9 被换成 B 的 [源 docs/18 §1.1]。B 还没有项目时，1–9 就变空，直到再插回 A。
4. 待传交给 B：设备一进 Ready 就推最多 4 条 `outbox.item` [源 fw/shell/runtime_outbox.cpp:91]；B 追加、fsync、回 `outbox.ack stored`，设备删掉这条 [源 hub/reduce.py:124-136]。录音默认也拉（`rec_pull: true`），存好再 `file.del` [源 recordings/puller.py:151]。
5. 以后设备的录音历史、★、藏起来只按 **B 的** `events.jsonl` 回答。
6. 每台 Mac 内部恰好一次；跨 Mac 是至少一次：在 fsync 和 ack 之间拔线，下一台会再存一份 [推]。

### 1.4 摩擦，按严重度

| # | 严重 | 摩擦 |
|---|---|---|
| F1 | 高 | 先插先得、不可逆；没有 Mac 之间挪数据的路，`cpa export` 也不带 `events.jsonl` 和配置 [源 docs/15 §2.4] |
| F2 | 高 | 一场会在 A 拉了一半、到 B 拉完，两边各半场、各自转写 [源 recordings/manifest.py:4-6] |
| F3 | 高 | 设备只有一份 1–9：离线 `#3`、按住 G0 加数字、会议标签都按最后那台 Mac 的项目算 [源 docs/13 §13 第 7 条；docs/23 §2.1] |
| F4 | 中 | 静默绑定，看不出连的是哪台 Mac |
| F5 | 中 | §1.2 每一行都要在新 Mac 上重做 |
| F6 | 中 | 装法四种、版本号不变、Release 里没有 Mac 端、私有仓库要认证 |
| F7 | 中 | 新 Mac 上 cpa-stt 缺模型和 `whisper-cli`，不会自己下；AirDrop 过来的模型在"下载"里，一清理就断 |
| F8 | 中 | "同步笔记目录来合并"是个坑：两个 cpad 追加同一个日记文件；`.transcriber` 认领在网盘上不是原子的 [推]；明文上传，而 `cpa status` 只认 iCloud Drive |
| F9 | 低 | 迁移助理或恢复备份把 `$CPA_HOME` 原样搬走：同一个 `host_id`、同一个名字；plist 指的 Python 可能在新机器上不存在 [推] |
| F10 | 低 | 绑定前 cpad 把 `host_id` 明文发给任何 303A:1001 串口；绑定后，报出同一个 USB 序列号的板子照样拿得到 [源 docs/18 §4.3、T4] |

## 2. 目标体验

### 2.1 老实数一数

一台新的公司 Mac，用法 A，家里那台已经装好：

| | 最少（只做必需的行） | 全套 |
|---|---|---|
| 终端 | 粘贴 5 次：命令行工具、uv、gh、`gh auth login`、取代码并安装（仓库公开后 3 次） | 再加 `brew install whisper-cpp` |
| 系统弹框和网页 | 命令行工具的安装框；GitHub 网页登录加一次性码；第一次插线的"允许配件连接" | 同左 |
| Mac app 点击 | **2**：笔记目录、连上设备 | 约 12：再加改名字 1、本地转写 2、Claude Code 2、每个 AI 客户端 1、每个去向 2、（用法 C）搬设置 3 |
| 文件选择框 | 0 | 搬设置 1；导入时目录不存在的项目先空着，不弹框 |
| 设备 | 设备分档案以后（v0.8，或 O7 提前的最小档案）3–4 下；之前 0 | 升级前有"未定"的待传，再 2–3 下 |
| 旧 Mac | 0 | （用法 C）设置 → 这台 Mac →"导出设置…"，再 AirDrop：约 3 下 |
| 要你拿主意的 | 笔记放哪；这块设备连不连；设备上选家 / 公司 | 每个可选行要不要 |
| 升级 | — | `~/cardputer-assistant/scripts/mac-install.sh --upgrade` 一条 |

### 2.2 装

README 里放一段，照着一行一行粘贴：

```bash
xcode-select --install     # 1. 命令行工具（git、swift 都在里面）：点"安装"，等它装完
curl -LsSf https://astral.sh/uv/install.sh | sh   # 2. uv（有 Homebrew 也可以 brew install uv）；装完重开一个终端
brew install gh            # 3. gh；没有 Homebrew 就到 cli.github.com 下载 .pkg 双击装
gh auth login              # 4. 选 GitHub.com → HTTPS → 用浏览器登录，输一次性码（仓库公开后不用第 3、4 步）
gh repo clone h3qing/cardputer-assistant ~/cardputer-assistant && ~/cardputer-assistant/scripts/mac-install.sh   # 5.
```

`scripts/mac-install.sh` 按顺序做，每一步先说要做什么，失败就停在那一步并说怎么办：

0. **只在 git checkout 里跑**。checkout 里的文件带 `com.apple.quarantine`（浏览器下载的源码包）就停："请用 git 取代码"。工作区有没提交的改动也停（`--dev` 才放行）。整个脚本包在一个函数里、最后一行 `exit`，切换版本时 bash 不会读到半个新文件。从路径带空格的 checkout 也能跑（比如名为 `Cardputer Assistant` 的目录）。
1. **挑版本**：没给参数时取最新的发布 tag（`git tag -l 'v*' --sort=-v:refname` 的第一个），`git checkout` 到它，再执行**那个 tag 里的**这份脚本。`--upgrade` 先 `git fetch --tags`，其余一样。tag 的 checkout 没有分支，所以不用 `git pull`；手动升级等于 `git fetch --tags && git checkout v0.N.0 && scripts/mac-install.sh`。
2. 查命令行工具（`xcode-select -p`）：能 `git clone` 就已经有了，这一步只是兜底；没有就停，给 `xcode-select --install`。
3. 查 uv：没有就停，给两条路，**不替你 curl**。
4. **造 wheel**：`daemon/pyproject.toml` 的版本和 tag 对得上才继续；`uv build daemon --out-dir <新的临时目录>`，每次一个新目录，不会拿到旧 wheel。构建后端钉死版本，依赖按 `daemon/uv.lock` 导出的约束装 [待：确认 `uv tool install --constraints` 会不会核对哈希]。
5. `uv tool install --force --reinstall <那一个 wheel 文件>`。
6. **后台服务**：`cpa service install`。升级时，cpad 正在拉录音就问你"等它拉完再重启？"；不忙就用 `launchctl kickstart -k` 重启 cpad 和已经装了的 cpa-stt，新代码才真的跑起来。装完用 `launchctl print` 核对真的在跑；在 系统设置 → 通用 → 登录项 里被关掉了，就停下并说去哪里打开。
7. **Mac app**：docs/24 v1 发布以后，有 `swift` 就本机造、ad-hoc 签名、换进 `~/Applications`；在那之前跳过这一步。
   - **加密钥匙**（设备加密 SEAL 上线以后，docs/26 SEC-5）：同一个 SwiftPM 包里的 `cpa-key` 也在这一步造好、装到固定的绝对路径、写进配置（cpad 只认这个路径，并核对属主，docs/26 §4.5）；然后**在前台**跑 `cpa-key init` 和 `cpa-key selftest`。这一步总是做，不是可选的一行（docs/26 §4.5、§5.5 第 1 条）。已经有钥匙的（升级、迁移过来的）不重新 `init`。
   - cpad 第 6 步已经起来了：它看到配置里的 `cpa-key` 路径和 `keys/seal/` 变了，就重跑 `selftest`、重新发一次 `hello`（docs/26 §4.3）；这一步最后再 `launchctl kickstart -k` cpad 一次兜底。首次设置的"连上"在 `selftest` 通过前是灰的（§2.3），所以新 Mac 第一次握手的 `hello` 就带着公钥。升级上来、设备已经连着的，下一次握手带上公钥，设备弹"让「Mac·8C1D」解开设备上的记录？"（docs/26 §4.3"登记"）。
   - 造不出来（没有 `swift`）：照样装 `cpa`，`cpa doctor` 说清楚为什么；这台 Mac 没有公钥，设备不会把它登记成收件人。设备上一台登记过的 Mac 都没有时写"加密：关"；分档案时当前档案里一台有钥匙的 Mac 都没有，照录明文、带"明"字，写"加密：公司 没有钥匙"，从不封给别的档案的 Mac（docs/26 §4.3"封给谁"、D8）。
8. 打印一行：`v0.6.0（commit 1a2b3c4）· cpa 0.6.0 · cpad 0.6.0 · cpa-stt 0.6.0 · app 0.6.0`。commit 和发布说明末尾写的对得上，就是同一份代码。
9. 有 app 就 `open -a` 打开它（有一行没好，它自己会打开首次设置页）；没有 app 就在终端里跑 `cpa setup`。

不要 sudo，不改系统设置，不碰 `~/.claude`（hooks 由首次设置那一行写，你点了才写）。

### 2.3 首次设置页：一行一项，按顺序

左栏顶上的"首次设置（还差 N 步）"**只数必需的行**；必需的都好了，这一项就收进 设置 → 这台 Mac。可选的行收在"可选"下面，每行有一个"不用"（`cpa setup skip ID`），点了就不再提醒。

数据来自 `cpa setup check --json`（§2.6）：打开页面、点"重新检查"时刷新，页面开着时每 3 s 刷一次。首次设置没做完时，app 就算藏在后台，也每 10 s 跑一次 `cpa device ls --json`，发现设备就在菜单栏图标上加一个点；做完就停。这一条修订 docs/24 的"看不见时不轮询"（§2.5）。

| 行 | 检查（只读） | 按钮（你点了才跑） | 背后的命令 | 必需 |
|---|---|---|---|---|
| 后台服务 | LaunchAgent 在不在、真的在跑；指的是不是**这个** `cpa` 的 Python；cpad 的版本 | 没装："装上"；`cpa` 比 cpad 新："换成新版本"；`cpa` 比 cpad 旧：只写一句，**没有按钮** | `cpa service install` | 是（脚本已经装好，一般是 ✓） |
| 笔记目录 | 不在网盘（`archive/cloud.py` 的全部判断）、不在 文稿 / 桌面 / 下载、可写 | "用 ~/CardputerNotes" / "选目录…" | `cpa notes dir PATH`（新） | 是 |
| 这台 Mac | 名字（`host_label`）；从别的 Mac 迁移过来、名字还是旧的 | "改名字…"；迁移过来时预填"Mac·8C1D（新）" | `cpa host name -- 名字` | 有默认名字，可以不动 |
| 搬设置 | 这台 Mac 有没有项目 | "从另一台 Mac 的导出文件导入…" / "不用" | `cpa settings import FILE`（§4.2） | 用法 C 才用 |
| 设备 | 插着的板子（只读 USB 列表，不开串口）；绑没绑；插着的是不是绑定的那块 | "先不连" / "只给它充电" / "是我的，连上"；连上以后："断开"；"换了设备？改绑" | `cpa device bind --cand ID`（`--charge`）；`cpa device forget` | 是 |
| 家 / 公司 | **设备报来 `prof` 才出现**（`cpa status --json` 有 `device.prof`；不看固件版本号，O7 提前时也对）：设备把这台 Mac 分到哪个档案 | 没有按钮，写确切按键（§3.2） | 设备上选 | 设备分档案以后 |
| 磁盘加密 | FileVault 开没开（`fdesetup status`，不要 sudo）。docs/26 的 `cpa doctor security`（SEC-1）合并后改用它，首次设置最后跑一次（docs/26 §5.5 第 5 条）：再加上 Time Machine 加没加密（盘没接上就写"不知道"；没加密时提醒备份里有归档的明文，设备加密以后还有私钥文件，docs/26 D6）、`cpa-key` 在不在；设备加密以后显示这台 Mac 钥匙的指纹（`kid` 的前 8 个十六进制，写成 `9C1B·27E0`），和设备 系统 → 加密 那一行对得上就是同一把（docs/26 §4.3"指纹"） | 没开时只写一句去哪里打开，不替你开 | — | 只提醒 |
| 可选：本地转写 | 装没装 `whisper-cli`；`$CPA_HOME/models` 里有没有模型；模型的链接是不是指进 下载 / 文稿 / 桌面 / 网盘；有没有别的转写程序认领 | "找模型" / "搬进来" / "选模型文件…" / "打开" | `cpa-stt setup --json`（新：只找不链接）、`cpa-stt setup --move FILE`（新）、`cpa-stt use`、`cpa-stt service install` | 可选 |
| 可选：Claude Code | 找没找到 `claude`；hooks 里的 `cpa` 路径是不是这一个 | "看改动" → "写入" | `cpa hooks install --json`（新：只给改动）→ `cpa hooks install --yes` | 可选 |
| 可选：别的 app | 最近 50 次调用来自哪些客户端（`cpa mcp audit --last 50 --json`），不读别的 app 的配置 | "复制连接设置…"（打开集成页） | `cpa mcp setup <client>` | 可选 |
| 可选：去向 | 这台 Mac 上装了哪些 app 能接（docs/25 R1 以后） | "加上…"：app 先弹自己的确认框，再跑命令（docs/25 §5.7） | `cpa dest add --preset P --key K [--profile X] --yes` | 可选 |

- **"连上"在后台服务和笔记目录都 ✓ 之前是灰的**，旁边写为什么。这样设备上的东西不会先落进默认目录。设备加密以后、这台 Mac 有 `swift` 时，"连上"还要等 `cpa-key selftest` 通过，第一次握手才带着公钥（docs/26 §4.5）。
- 设备加密以后，"设备"一行的说明加一句：只封给别的 Mac 的东西不会交给这台，留在设备上等那台 Mac 来取（docs/26 §5.5 第 1 条）。
- 名字默认是 `Mac·` 加 4 个随机的十六进制字符（比如 `Mac·8C1D`），第一次运行时写进 config.json：不含你的真名，也不是"家""公司"这类档案的词（§3.2、D9）。这一条修订 docs/23 D10 的"默认为空"。
- `cpa-stt setup --json` 和"找模型"会用 Spotlight，有自己的 2 min 超时；`cpa setup check` 本身只看 `$CPA_HOME/models` 和几个已知目录。
- 做不到一键的只写一句话：Claude 登录和目录信任（cpad 从不替你做 [源 README E_SPAWN]）、系统的"允许配件连接"、`brew install whisper-cpp`、把模型文件拷过来。

### 2.4 页面示意

```
首次设置 · 这台 Mac                                 还差 2 步  [重新检查]
✓ 后台服务   在跑，版本 0.6.0
● 笔记目录   ~/CardputerNotes                       [用这个] [选目录…]
✓ 这台 Mac   名字「Mac·8C1D」，设备上会显示这个       [改名字…]
– 搬设置     用法 C 才需要                            [导入…] [不用]
● 设备       插着一块 Cardputer，设备码 3F7A
             连上以后，设备上还没传的笔记和录音会存到这台 Mac，
             设备上的会删掉，存过来就回不去。有多少看设备状态栏的 ↑ 和 录。
             这台 Mac 还没有项目：设备上的 1–9 会先变空，插回原来那台 Mac 再恢复。
             [先不连]  [只给它充电]  [是我的，连上]（先选好笔记目录）
✓ 磁盘加密   FileVault 已打开
可选（不催你）
○ 本地转写   找到 ggml-large-v3-turbo.bin，在「下载」里，清理时会被删  [搬进来] [不用]
○ Claude Code  hooks 没装                              [看改动] [不用]
○ 别的 app   还没有 app 来读过                         [复制连接设置…] [不用]
```

符号都带字：✓ 好了、● 要你点、◌ 等设备、– 不用或还没到时候、○ 可选、✕ 出错。三个设备按钮里没有哪一个是回车键的默认按钮。

"设备"一行的各种情况：

| 情况 | 这一行写 |
|---|---|
| 没发现板子 | "没发现设备。常见原因：数据线只能充电；第一次插线时没点'允许配件连接'；设备停在 Launcher 里；设备没开（侧面开关拨到 ON）" |
| 一块没连过的 | 上面示意的那样 |
| 两块没连过的，设备码不同 | 各列一行："桌上不止一块：拔掉不用的那块，或者认设备码"（O7 起设备在 系统 页显示自己的设备码） |
| 两块设备码一样 | "两块板的设备码一样，分不清：拔掉一块再看"，不给按钮 |
| 连上了 | "已连上 3F7A · 固件 v0.6.0 · 这次存过来 12 条笔记、3 段录音"［断开］ |
| 连上后发现不是 Cardputer 助手的固件 | "这块板没在跑 Cardputer 助手的固件（可能停在 Launcher 里）" |
| 连上后板子报的身份和 USB 序列号对不上 | "✕ 这块板报的身份和 USB 序列号对不上，已断开" |
| 只充电 | "这块设备在这台 Mac 上只充电"［改成同步］ |
| 绑定的不在，插着别的 | "插着的不是绑定的那台：7C21。换了设备？"［改绑］ |

### 2.5 规则

- **一个按钮只跑一条 `cpa` / `cpa-stt` 命令**（docs/24 §1.2）。行里给的是 `op` 枚举和参数，app 用自己的动词表拼 argv、先校验，不照抄行里的任何字符串。
- **重跑无害**；会写的先显示改什么（hooks 显示改动，导入逐条列出）。
- **超时**：普通命令 5 s（docs/24 §5.2）；`cpa service install` 和 `cpa notes dir` 60 s（里面有 `launchctl bootout`，要等 cpad 退出）；"找模型" 2 min。`cpa setup check` 自己保证 5 s 内回来：不用 Spotlight，不起登录 shell。
- **修订 docs/24 D8**：首次设置里、以及 `cpa` 比 cpad 新时，可以一键跑 `cpa service install`。忙不忙**由命令自己判断**：cpad 正在拉录音或开 session 时它拒绝（加 `--force` 才放行），终端里也一样，不只是按钮灰着。不会降级。
- **修订 docs/24 "看不见时不轮询"**：只在首次设置没做完时，后台每 10 s 一次 `cpa device ls --json`；做完就停。
- **这些设置动作只在命令行和 app 里，从不放进 MCP**，`mcp.json` 打开写工具也不放；测试断言 MCP 的工具表里没有它们（§7.2）。以后要是把 `setup check` 给 MCP 看，设备报来的字符串先去掉。
- 不改系统设置，不联网，不开端口；不显示 `host_id`、`dev`、USB 序列号，只显示设备码，设备加密以后再加这台 Mac 钥匙的 8 位指纹（不是秘密，用来和设备上的对）；设备报来的字符串（比如 `fw`）进行里之前过 `display_safe`。
- 界面文字用大白话：不写 cpad、"接法"、"↑12 录3" 这类缩写；设备状态栏的 ↑ 和 录 只在"看设备"时提。
- 终端 `cpa setup`：同一张表逐行走，每行 y / N / 不用，不需要 app。

### 2.6 给 app 的 JSON：`setup/v1`

`cpa setup check --json` 的一行：

```json
{"v": 1, "id": "device", "state": "act", "required": true, "blocked_by": ["notes"],
 "title": "插着一块 Cardputer，设备码 3F7A",
 "detail": "连上以后，设备上还没传的笔记和录音会存到这台 Mac，设备上的会删掉，存过来就回不去。",
 "fix": [{"op": "device.bind", "cand": "c7f3a9e01b"},
         {"op": "device.bind", "cand": "c7f3a9e01b", "charge": true}]}
```

- `id` ∈ `service notes name import device profile disk stt hooks mcp dest`；`state` ∈ `ok act wait warn off skip na err`（`x-knownValues`，以后只增）。
- `fix[].op` 只有表里那些动词（含 `setup.skip`），参数各有格式：`cand` 是 `^c[0-9a-f]{8,16}$`；名字过 `host_label` 的校验；路径只能是 `~/…` 或 app 自己的文件选择框给的，app 再查一遍。
- `cand` 是 cpad 给的**一次性候选号**：绑着那个端口和序列号，拔掉或用过就作废。
- `title`、`detail` 给人看，过 `display_safe`；不含本机身份。
- `protocol/setup/v1/` 放 schema、合法和非法的例子、检查器，和 `filing/v1` 一样；Mac app 的 `Fixtures/` 带 `SOURCE` 拷一份。

## 3. 认出同一台设备，设备认识新 Mac

### 3.1 Mac 认设备（O3：只动 Mac，不改协议）

今天：VID:PID 303A:1001、设备 hello 的 `fw` 以 `cpa-fw/` 开头、USB 序列号等于绑定的那个 [源 port_finder.py]。没绑定时第一块应答的就绑上，`host_id` 已经发出去了。改成：

1. **没绑定就要你确认**：`device.serial` 是空的，cpad 就不跟任何板子握手。**不加开关**：老配置里已经有序列号的照旧；`cpa device forget` 和"改绑"之后也回到要确认。今天的测试台靠自动绑定，所以 `tools/hil/harness/rig.py` 先从 USB 列表读出唯一那块板的序列号写进 config，`tools/sim/cpad_sim.py` 写 `SIM_SERIAL`（`tools/sim/simtools/serial_pty.py:17`）。
2. **不说话就认出来**：没绑定的 303A:1001 板子，cpad 只读 USB 列表里的序列号，**不开串口、不发 hello**。ESP32-S3 的 USB 序列号就是出厂 MAC [源 docs/07:14 写明"USB 序列号同 MAC"；hello 里的 `dev` 是同一个 MAC 去掉冒号的 12 个小写十六进制，见 `hal_platform.h:42`、`hello.schema.json:58`]。
   - **设备码** = `sha256("cpa-dev:" + 规范化的序列号)` 的前 4 个十六进制，大写。规范化 = 去掉冒号、转小写，和 `dev` 一个样子，所以 O7（或 v0.8）起设备在 系统 页用 `dev` 算出来的设备码和 Mac 上的一样。它只用来分辨板子，不是口令，也不是 docs/26 里每台 Mac 钥匙的指纹：那个是 8 个十六进制、中间一个点（`9C1B·27E0`），在 系统 → 加密 里（docs/26 §4.3"指纹"）。
   - 好处：没确认的板子拿不到 `host_id`，也推不出任何一条笔记；设备不会白白"在线" 15 s 再掉成"无响应"（任何主机 hello 都让设备进入 Ready，15 s 没数据才退回 [源 link_session.cpp:74-86、162；protocol_limits.h:62]）；不加协议字段，固件的编解码和流程测试都不受影响 [源 test_codec_vectors、test_link_flows 会重放全部向量]。
   - 代价：连上之前不知道固件版本和待传数。界面就请你看设备状态栏的 ↑ 和 录。
3. **候选**经 `cpa device ls --json` 给出：设备码和一次性候选号 `cand`，没有序列号原文。两块板设备码一样时不给 `cand`。终端里 `cpa device bind 3F7A` 只在恰好有一块板是这个码时才有效。
4. **绑定**：写入序列号，然后正常握手（带 `host_id`），事件里记 `device.bound`。第一次握手时核对两件事：`fw` 以 `cpa-fw/` 开头；`dev` 等于规范化的序列号，并记下来。对不上就放开串口，行里报错。docs/26 SEC-7（D10）以后改成：序列号先只是候选，设备在 `hello` 里证明了自己才算绑上（docs/26 §7）。
5. **只充电**：`device.mode: "charge"`，cpad 认得这块板，但从不打开它的串口；"改成同步"或 `cpa device forget` 撤销。这样只想充电的 Mac 不用再靠记性。
6. **断开**：绑着的那一行有"断开"按钮 = `cpa device forget`，回到要确认。
7. **换了设备**：绑定以后插上别的板子，同样只读序列号，行里写"插着的不是绑定的那台：7C21。换了设备？"，点"改绑"（旧的被替换，记 `device.rebound`）。
8. **设备上的提示**（O7 或 v0.8，只改固件文字，不改协议）：USB 有数据、却一直等不到 hello 时，"无响应"旁边写"Mac 没回应：没装 cpa，或者在 Mac 上等你点「连上」" [待：确认固件分得清只能充电的线和有数据的线]。

**结果**：F4 解决。F10 / T4 **收窄了，没有关掉**：没确认的板子拿不到 `host_id`；但一块报出绑定的那个序列号的板子（USB 序列号是板子自己报的）照样拿得到。设备向 Mac 证明自己（docs/26 D10，SEC-7，排在设备加密之后）也只挡从没碰过这台设备的板子：读过它 NVS 的板子拿得到认证密钥，照样冒充得了；序列号只防插错，不防冒充（docs/26 §5.5 第 4 条）。§3.5 第 2 条"Mac 证明自己"管的是另一头：拿到 `host_id` 的人冒充这台 Mac，封好的东西拿不到、删不掉；这台 Mac 的钥匙已经钉在设备上时，连没封的也拿不到；还没登记钥匙的 Mac，没封的待传仍然只靠 `host_id` 这个口令（docs/26 §3.1 T-ROGUE、§4.3"钉住"、§5.1）。界面上不把绑定说成"已验证"（docs/26 §5.5 第 4 条）。

**不做**：每次连接先发一个不带 `host_id` 的 hello、核对 `dev` 再正式握手。`dev` 也是板子自己报的，存心仿冒的板子照样报对，只挡得住不小心插错的；而每次连接都要多一轮，设备会先推几条再退回。第 4 条的核对已经挡住插错。

### 3.2 设备认 Mac（v0.8，照 docs/18，不另起炉灶）

- 设备按 `host_id` 认 Mac，只存 `sha256("cpa-host:" + host_id)` 的前 16 字节，只接受 ≥16 个字符的 id；**绑定只在设备上选**，Mac 自报的名字只用来显示 [源 docs/18 §3.2、§4.2]。
- 新 Mac 第一次握手时，设备弹出 docs/18 §5.2 的选择单："以后再说 / 家 / 公司 / 都不是"。**光标停在"以后再说"**，用 `;` `.` 上下移动，400 ms 防误触；选了家或公司，还有一个确认框（§5.3）。
- **按几下**（都从"以后再说"算起）：家 = `.` Enter Enter（3 下）；公司 = `.` `.` Enter Enter（4 下）；第二下 Enter 是在确认框上。每台 Mac 只一次。升级前有"未定"的待传时多问一次（docs/18 §5.4，再 2–3 下）。
- **设备加密以后，这个确认框同时登记这台 Mac 的公钥**，不多按一次（docs/26 §4.3"登记"、§5.5 第 1 条）。不分档案时没有这个确认框，设备另弹一张"让「Mac·8C1D」解开设备上的记录？"，光标在"以后再说"，每个 `host_id` 只问一次，以后可以到 系统 → 加密 里加。**已经绑定、还没登记钥匙的 Mac**（SEAL 固件之前绑的，或者那时还没有 `cpa-key`；v0.8 用户都是这样）也弹这一张：它第一次带公钥握手时问一次，以后到 系统 → 加密 →"加上「Mac·8C1D」"（docs/26 §4.3"登记"）。设备上最多登记 4 台 Mac，第 5 台要先忘掉一台（docs/26 §4.3"上限"）。
- Mac 这边"家 / 公司"一行，按设备报来的 `prof`（docs/18 §10.3）显示，cpad 从下一个 `pong` 就知道（≤5 s [源 docs/18 §3.7]），`cpa status --json` 加 `device.prof{act, ok, ask}`：

| 设备报来的 | Mac 上这一行写 |
|---|---|
| `ask: true`（选择单开着） | ◌ 等你在设备上选：「家」按 `.` 一下、再按 Enter 两下；「公司」按 `.` 两下、再按 Enter 两下（第二下 Enter 是确认） |
| `ok: []`、`ask: false` | ● 设备还没把这台 Mac 分到档案，这台 Mac 先什么都不收。可能是设备锁着或在录音（解锁、停下以后会弹出来），或者你选了"以后再说"或"都不是"。要重选：拔下再插上；或者在设备上按 Opt+M 打开 系统，按 `/` 换到"档案"，按 `.` 两下移到"这台Mac 未分配"，按 Enter |
| `ok: ["work"]` | ✓ 设备把这台 Mac 分到了 公司。不对？在设备上 Opt+M → `/` →移到这台 Mac 那一行 → Enter →"忘掉"并确认，再拔插一次重选 |
| 设备加密以后：`seal_chal.kid` 不是这台 Mac 的任何一把钥匙 | ◌ 设备在问要不要换这台 Mac 的钥匙，光标在"不换"，这台 Mac 先什么都不收。只有你自己删过 `keys/seal/` 才会这样，那就在设备上选换；不是的话别动，有人拿着这台 Mac 的身份（docs/26 §4.3"钉住"） |
| 设备加密以后：`seal_chal.kid` 是这台 Mac 的钥匙，这台 Mac 却证明不了（`cpa-key` 不在、坏了） | ✕ 设备在等这台 Mac 证明钥匙，证明之前什么都不交：跑 `cpa doctor` 看 `cpa-key` |

- **设备没报 `prof` 时这一行不出现**（旧固件，或者你选了"不分档案"）；上表最后两行不看 `prof`，设备加密以后照样出现。
- **Mac 不能替你选**：名字那一行的建议不用"家""公司"这类档案的词（D9），免得 Mac 报的名字把你往某个档案上引（docs/18 T5）；也不加"Mac 建议哪个档案"的字段。设备上照旧写"（名字是它自己报的）"。
- 名字改了不会马上重新握手：新名字在下次插上或 cpad 重启时随 hello 发出。因为"这台 Mac"排在"设备"前面，第一次绑定就带着名字。

### 3.3 v0.8 之前

| 做法 | 什么时候 | 挡住什么 |
|---|---|---|
| §3.1 的"你点了才连"和"只给它充电"（O3） | 下一个 Mac 端版本，不等固件 | 升级过 cpa 的新 Mac 不会不声不响地拿走一切 |
| 最小档案提前（docs/18 §15 第 9 条：P8-P、F0、F1、F2、D1，界面用最简单的确认框） | 默认提前到下一个固件版本 👤 §9.2 第 2 条 | F1、F3：家里的不交给公司；离线 `#3` 按当前档案 |
| README 现在就写上：去另一台 Mac 前先在主力 Mac 上插一下，等 ↑ 和 录 变 0；还没升级 cpa 的 Mac 插上就会接管；只充电的 Mac 不要跑 cpad | 本 PR | 缓解 F1；`rec_pull: false` 只管录音，打字的照样交 [源 config/settings.py:64] |

F2（一场会被两台 Mac 各拉一半）在用法 A 里不会发生：会议带着档案，只交给那个档案的 Mac。只在用法 C（几台 Mac 同一个档案）才要"一场会只交给拉了第一段的那台，24 h 后放开"这条设备规则；默认不做（D8）。

### 3.4 为什么不加配对密钥，也不两屏对码

- 设备没开 Flash 加密：IDF 4.4 上加密 NVS 要先开 Flash 加密，而 Flash 加密烧 eFuse、Launcher 就用不了 [源 docs/21:424；docs/18 §8.2]。USB 串口还能自动复位进下载模式，插着的 Mac 不用你按键就能**读写**整片 Flash，还能经 USB-JTAG 读运行中的内存（docs/26 §2.1）。所以**设备存的对称密钥挡不住插着的那台 Mac**。
- 公钥不一样：设备只存 Mac 的公钥，让 Mac 证明它有对应的私钥，设备上不需要任何秘密（§3.5；docs/26 §3.2）。
- 但能写 Flash 的 Mac 能改掉设备存的公钥（往收件人表里加自己）、绑定表，甚至固件本身；这一条什么钥匙都挡不住，设备加密只防被动地读（docs/26 §3.1 T-USB、§4.4）。办法是：去不信任的电脑之前先在家同步清零，只充电时用只充电的线或数据阻断器；系统 → 加密 列出每个收件人和指纹，多出来的看得到（docs/26 D17）。
- 两屏对码：USB 只有一个口，你人就在跟前，对码不多挡什么。设备码（§3.1 第 2 条）只用来分辨板子。

### 3.5 给落盘加密的约束（已收进 docs/26 定稿）

设备上的加密怎么做由 docs/26 定：SEAL，每个文件封给 Mac 的 X25519 公钥（docs/26 §4.3、D4）。Mac 的私钥是 `$CPA_HOME/keys/seal/<kid>.key`（0600，目录 0700），**不放钥匙串**：`cpa-key` 是 ad-hoc 签名的，每次重新编译签名都会变，钥匙串会在后台同步时突然弹框要登录密码（docs/26 §5.2、D6）。只有 Swift 小工具 `cpa-key` 读这个文件，私钥不出 `cpa-key`；它不进 `cpa export`，也不进 `settings/v1`，只跟着 Time Machine 和迁移助理走（docs/26 §4.5、§5.2、D5、D6）。本文**不自己定私钥文件**。下面七条是新 Mac 这边必须满足的，docs/26 定稿已经收进 §4.3、§5.3、§5.5 和 SEC-5，这里照定稿的说法写：

1. **公钥只在你在设备上点头的那一刻收下，并钉住**（docs/26 §4.3"登记""钉住"、D21）。分档案时就是 docs/18 §5.3 的绑定确认框，确认绑定就同时登记；不分档案时，以及已经绑定、还没登记钥匙的 Mac，是另一张"让「Mac·8C1D」解开设备上的记录？"（§3.2）。钉住以后，同一个 `host_id` 的 `hello` 不带公钥、带来不同的公钥，或者没证明，设备都按 docs/18 的"未分配"处理：`prof{ok:[]}`、计数报 0、不采用快照、不发在线请求，封好的、没封的一样都不交，直到证明通过或你在设备上选了换。带来不同的公钥时：
   - 同一次握手里用旧钥匙证明了自己（`cpa-key rotate` 的正常轮换，docs/26 §5.3）：设备直接换，提示一句"「Mac·8C1D」换了钥匙"；
   - 证明不了：设备保留旧的，问"「Mac·8C1D」的新钥匙（3F2A·61D0）证明不了原来那把，换吗？"，光标在"不换"。不请你去对旧指纹，指纹能靠多生成几把钥匙凑出来。

   这样拿到 `host_id` 的人不能把以后的记录改封给自己，也不能靠"不带公钥"拿走没封的。
2. **Mac 每次握手都证明自己有私钥**，和 SEAL 同一个版本上线（docs/26 §4.3"握手证明"、D21）：设备对 `hello` 的回复加 `seal_chal{E′, n, kid}`（每次握手新生成的一把临时 X25519 公钥、16 B 随机数，和设备为这台 Mac 钉住的 `kid`）；Mac 的下一条 `ping` 加 `seal_proof[{kid, tag}]`，`tag` = HMAC-SHA256(HKDF(X25519(sk, E′), info = "cpa-seal/v1 confirm"), n‖dev) 的前 16 字节，由 `cpa-key prove` 算，私钥不出 `cpa-key`。证明通过之前，封好的东西一样都不交，这台 Mac 对封好的条目回的 ack、发的 `file.del` 都不认；通过以后，也只交、只认收件人里有它证明过的 `kid` 的那些；设备报给这台 Mac 的 `pending`、`files.n`、`file.list` 也只算这些和没封的，只封给别的 Mac 的不算（docs/26 §4.3"交给谁"），所以这台 Mac 的 ↑ 能降到 0。钉住了钥匙的 Mac 证明之前连没封的也不给（第 1 条）；还没登记钥匙的 Mac，没封的待传照 docs/18 的规矩，仍然只靠 `host_id`。设备不存秘密，和 §3.4 一致。没有这一条，偷到 `host_id` 的人虽然解不开封好的记录，却能回 ack 让设备把它们删掉。它和 docs/26 D10（设备向 Mac 证明自己，SEC-7）方向相反，两个独立。
3. 设备在 `seal_chal.kid` 里报它为这台 Mac 钉住的 `kid`（`sha256(pk)` 的前 8 字节，docs/26 §4.3、§5.5 第 6 条）；不放进 `prof.ok`，因为不分档案时没有 `prof`，`prof.ok` 又是档案名的列表（docs/18 §10.1、§10.3）。不是这台 Mac 的任何一把钥匙，就报警"设备存的不是这台 Mac 的钥匙"（§3.2 的表）。cpad 还记住"这台设备发过封好的东西"和头里见过的 `kid`：以后它发来没封的条目，或者头里第一次出现一个没见过的 `kid`（"设备多了一个收件人 9C1B·27E0，是你刚点过头的 Mac 吗？"），也在 `cpa status` 和 Mac app 里报警（docs/26 §4.4）。
4. **说清楚保护到哪里**：设备加密只防被动地读一次（docs/26 §3.1 T-USB、§4.4）。存心改设备的 Mac 能往收件人表里加自己、擦掉表逼设备回到明文、刷固件，以后记的东西它就拿得到；插着时还能经 USB-JTAG 读内存里的时段钥匙，打开这一个时段（≤1 小时）里写的文件。
5. **工具链**（Arduino 2.0.17 的预编译库，docs/26 §4.3"用哪个库"）：X25519 用已经链接进来的 libsodium；AES-256-GCM 用 mbedtls 的 `esp_aes_gcm`，是硬件 AES-CTR 加软件 GHASH（S3 的 AES 硬件没有 GCM 模式）；ChaCha20-Poly1305 在 libsodium 里有，留作 AEAD 的备选；HKDF 两个库都没有，用 HMAC-SHA256 自己拼。耗时、堆和栈由 docs/26 的闸门 G-ENC 量。
6. **字段名**按 docs/26 §4.3、§5.5 第 6 条：`hello.seal{kid, pk, kids, salt}`、`hello` 回复里的 `seal_chal{E′, n, kid}`、`ping.seal_proof[]`；不用 `host_pk`，`prof` 不加字段。先只写在文档里，`protocol/enc/v1` 的 spec 定稿时才进 schema。
7. **`host_hw` 只防一件事**：老实的 cpad 跑在被拷走的 `$CPA_HOME` 上时，自己换一个身份。它挡不住存心冒充的人（那人可以改 cpad）（docs/26 §5.1）。O4 重新生成 `host_id` 时用 `token_hex(16)`（32 个字符，合固件的 `HostId FixedStr<32>` 和 `host_id_pattern` 的 `{8,32}` [源 messages.h:64；limits.json:71]），已有的 id 不动。设备加密以后，O4 换 `host_id`、`cpa host reset` 的同时都跑 `cpa-key rotate`：换一把新钥匙，旧的留着只解不封、从不自动删，新身份不拿旧钥匙当在用的（docs/26 §5.3、D23）。迁移过来的旧私钥照样解得开封给旧 Mac 的东西，所以旧 Mac 改作他用之前，要在它上面跑 `cpa host reset`，两边才在密码学上分开（docs/26 §5.3）。

Mac 上的笔记目录：按 ADR 0004 开放给任何 app 读，加密靠 FileVault 和加密的 Time Machine（docs/26 D1、D13）；设备加密以后 Time Machine 里还有私钥文件，备份更要加密（docs/26 D6、§8.2 第 4 条）。首次设置的"磁盘加密"一行只读地告诉你开没开。

## 4. 什么跨 Mac，什么从不跨

### 4.1 总表

| 东西 | 跨不跨 | 怎么跨 / 为什么不跨 |
|---|---|---|
| 笔记、录音、转写、`events.jsonl`、待归档状态、★、藏起来 | **不跨** | 留在收到它的那台 Mac。要备份用 `cpa export`；换 Mac 用迁移助理（§6） |
| `host_id`、`host_hw`、设备序列号的绑定 | **从不跨** | 每台 Mac 一个身份；拷过去的由硬件指纹作废（§6） |
| 加密私钥 | **不随设置搬**；只跟着迁移助理和 Time Machine 走 | `$CPA_HOME/keys/seal/` 下的 0600 文件，不放钥匙串，不进 `cpa export` 和 `settings/v1`（docs/26 §5.2、D6）。迁移过来的旧钥匙只解不封，新 Mac 换一把新的，旧的从不自动删（docs/26 §5.3、D23） |
| 设备上的绑定表、档案、待传 | 跟着设备 | 在设备的 NVS 和存储上 |
| 1–9（key、编号、名字） | 可以搬 | `settings/v1`；目录写成 `~/…`。**权限模式不搬，一律 `plan`**；要放宽，在这台 Mac 上 `cpa project rm` 后用 `cpa project add … --mode` 重新登记 |
| 去向 | 可以搬，有限制 | 整条记录照 destinations 的 schema 搬（只搬预设名会丢掉从你自己的预设加的去向 [源 docs/25:126]）；**`cmd` 和你自己写的链接模板不搬**，列出来让你在新 Mac 上自己加；会把字送出 Mac 的，导入时再给你看一次"送出的字会到…的云端"，每条单独点。**每台 Mac 自己的部分在新 Mac 上重来**（docs/25 §2.3、§3.1、§4.5）：一律先当 `act`，`verified:"draft"` 不搬，要在这台 Mac 上 `cpa dest test` 再答一次；处理的 app（bundle id 和 Team ID）在这台 Mac 上重新认，没装就先不加；`profiles` 取这台 Mac 的 `served`；`file.*` 的路径存成 `~/…`；`usual` 只留两头（项目和去向）都导进来了的 |
| 你自己的预设 `$CPA_HOME/presets/*.json` | 不搬 | 导入的去向自带抄好的模板（docs/25 §2.3）；你自己写的链接模板本来就不搬 |
| `rec_pull`、转写引擎的名字、模型文件名 | 可以搬 | 只搬名字；路径在新 Mac 上重新找 |
| `host_label` | 不搬 | 每台 Mac 自己的名字 |
| `spawn_env`、`claude_bin`、`cpa-key` 的路径、hooks、LaunchAgent、MCP 片段 | 不搬 | 本机路径，由安装脚本和首次设置在新 Mac 上重新生成 |
| `$CPA_HOME/mcp.json` 的写工具白名单 | 不搬 | 放开写工具要你在每台 Mac 上自己决定 [源 docs/15 §3.6] |

### 4.2 `settings/v1`

**这个文件决定 cpad 会跑什么**（项目目录、去向），所以下面的限制是安全规则，不是可有可无的。

- **导出**：旧 Mac 的 app 里 设置 → 这台 Mac →"导出设置…"：存到临时目录，直接打开 AirDrop；终端里是 `cpa settings export FILE`。先显示"9 个项目名、3 个去向，不含笔记、钥匙和本机身份"。文件 0600。目标在网盘里就拒绝，和 `cpa export` 一样只有终端里加 `--allow-cloud` 才放行 [源 archive/cli.py:46]。文件里记下导出那台 Mac 服务过的档案：`profile.json` 的 `served`（docs/18 §7.1 D1），不取设备此刻报的 `device.prof`，和 docs/18 §7.2 一样不看设备的状态。
- **导入**：`cpa settings import FILE`，逐条列出要加的：
  - 编号冲突就拒绝那一条并说明；已有相同的跳过；重跑结果一样。
  - 目录：展开 `~` 后取 realpath，必须在 `$HOME` 下面，不能是 `$HOME` 本身，不能在点开头的目录里。目录不存在就**先不加**这个项目，列在"还没目录的项目"里，以后点"选目录…"或者再导入一次。导入时不弹文件选择框，不建目录。
  - 权限模式一律 `plan`（文件里写了 `auto`、`acceptEdits`、`dontAsk` 也一样）[源 projects/registry.py:21；docs/05:919 要求 `auto` 必须由项目明确写明]。
  - 去向按 §4.1：`cmd` 不收；`url` 只收模板和随 cpad 发的预设一字不差的；送出 Mac 的每条单独确认。每条都重跑一遍 `cpa dest add` 的检查（模板形状、能接到哪里的拒绝表，docs/25 §2.3），再按 §4.1 把每台 Mac 自己的部分重来：先当 `act`，处理的 app 重新认，`profiles` 取这台 Mac 的 `served`。`file.*` 的路径照上面项目目录的规矩取 realpath、必须在 `$HOME` 下，落在网盘里的按"送出 Mac"再确认。
  - 文件里的 `served` 和这台 Mac 的 `served` 不一样（家的设置导进公司的 Mac）：先警告，终端里要输 `y`，app 里要再点一次"仍然导入"。哪一边的 `served` 是空的（v0.8 之前，或者还没服务过任何档案），这一条不起作用。
- 搬文件你自己选：AirDrop、U 盘。
- 契约 `protocol/settings/v1/`（schema、例子、检查器）。**泄漏检查**：导出里不能出现 `host_id`、`host_hw`、`dev`、USB 序列号（带不带冒号、大写小写都查）、`/Users/<名字>`（`file.*` 去向的路径也写成 `~/…`）、`spawn_env` 的值、`claude_bin`、`cpa-key` 的路径，以及 `$CPA_HOME/keys/` 下的任何东西（SEAL 私钥、日志的盐；docs/26 §5.1、§5.2）。
- 同一个档案的几台 Mac 导入同一份设置，1–9 和去向字母在它们之间意思就一样，用法 C 的 F3 也解决了。

### 4.3 不做的路

| 路 | 为什么不做 |
|---|---|
| 网盘同步笔记目录或 `$CPA_HOME` | F8：两个 cpad 追加同一个文件，认领不是原子的，明文上传；"笔记目录"一行对网盘报警（用 `archive/cloud.py` 的全部判断，不只 iCloud） |
| 两台 Mac 之间同步 `events.jsonl` | 两个写者一个日志，要合并协议；违背"一份日志只有一个 cpad 写" |
| 设备当搬运工（新 Mac 从设备读 1–9 缓存） | 要新加一条设备→主机的消息；还要防别的档案的 Mac 读到；导出文件已经够用 |
| 每台 Mac 互相发现、局域网同步 | 开端口、联网，违背"数据在本机"（docs/15 §3.12） |

## 5. 打包和安装

### 5.1 选项

| 选项 | 你要做的 | 要签名吗 | Gatekeeper | 结论 |
|---|---|---|---|---|
| **A. git checkout + `scripts/mac-install.sh`（脚本自己挑 tag）** | 克隆 + 1 条命令 | 不要 | 本机造的没有隔离属性 | **默认** |
| B. Release 带 wheel，`uv tool install <wheel 文件>`，再 `cpa setup` | 下载、核对、2 条命令 | 不要 | 不涉及 | 受管理的 Mac 的退路（§5.2）；没有 app |
| C. Homebrew 私有 tap 的 formula（`python@3.12` + pyserial） | `brew install h3qing/tap/cardputer-assistant` | 不要 | formula 不加隔离属性 [研] | 仓库公开后加；不用 `brew services`，免得多一个 plist |
| D. Homebrew cask 装 `.app` | 1 条命令 | 要 Developer ID | Homebrew 5.0 弃用了 `--no-quarantine` 和不签名的 cask [研] | 不做 |
| E. 不签名的 `.pkg` | 双击 | 要 Developer ID Installer | 要到 隐私与安全性 里"仍要打开" [研] | 不做：以 root 装，而 LaunchAgent 是每个用户的 |
| F. app 里内嵌 Python 和 cpad，用 SMAppService 注册 | 拖进来 | ad-hoc 时 SMAppService 不可靠 [研] | 拷到别的 Mac 会被隔离 | 不做：违背 docs/24 D8 和 §1.2 |

~~不做"curl 一行装好"：那等于让一个网址替你跑脚本，和"不替你 curl"矛盾。~~ 2026-10-26 修订：做，见文首的修订和 D1。

### 5.2 默认：`scripts/mac-install.sh`

- 步骤见 §2.2。`--dry-run` 只打印步骤；`--dev` 允许不在 tag 上、工作区有改动，版本行写"开发版本"。
- 没有 `swift`（命令行工具太旧）就跳过 app，只装 `cpa`，最后跑 `cpa setup`。
- **受管理的 Mac 不让装命令行工具或 Homebrew 时**（§9.2 第 4 条），不用 git、不用 gh：
  1. 在浏览器里从 Release 下载 `cpad-X-py3-none-any.whl` 和 `SHA256SUMS`，或者从另一台 Mac AirDrop 过来；
  2. `cd ~/Downloads && shasum -a 256 -c SHA256SUMS --ignore-missing`（`shasum` 是系统自带的）；
  3. uv：从 uv 的 GitHub Release 下载独立的二进制包，或者用它官网的安装命令，都不要命令行工具；
  4. `uv tool install ~/Downloads/cpad-X-py3-none-any.whl`，然后 `cpa setup`（终端版的首次设置，没有 app）。

  不在受管理的 Mac 上登录你自己的 GitHub，也不留令牌。

### 5.3 根上要修的（O0）

1. **版本号跟 tag**：发版时 `uv version X.Y.Z --project daemon`，`pyproject.toml` 和 `uv.lock` 一起改；`__version__` 从包元数据读，不再写死；CI 检查 tag 等于版本。新版本号让 uv 的缓存自然失效。cpa-stt 也报版本（`cpa-stt --version` 和 `status --json`；今天它没有 [源 grep `stt/`]）。
2. **Release 带 Mac 端**：release.yml 加 `uv build daemon`，wheel 进 `dist/` 和 `SHA256SUMS`；发布说明末尾自动加 commit。
3. **README 只留一种装法**：快速上手改成 §2.2；删掉 `uv sync --project daemon` 和 `uv run --project daemon cpa service install / hooks install`（它们把服务和 hooks 钉在 checkout 的 `.venv` 上）；daemon/README、#62 的发布说明跟着改。
4. **只改 Mac 端也打完整的 tag**：release.yml 要求 `docs/releases/<tag>.md` 并构建固件 [源 release.yml:31]；固件没变也照样构建一份，发布说明写明"固件没变"（D21）。

### 5.4 没有 Developer ID 时的规矩

- 命令行工具里就有 `swift` 和 `codesign`；Apple Silicon 上 ad-hoc 签名（`codesign --force --sign -`）就够跑。
- `cpa-key` 和 app 同一个包，也是本机造、ad-hoc 签名，每次升级签名都会变。所以它的私钥是文件、不放钥匙串，升级不会弹框要登录密码（docs/26 §5.2、D6）。
- **本机造的 app 没有隔离属性**，Gatekeeper 不检查。所以不在 Mac 之间拷 `.app`：AirDrop、下载、信息都会加隔离属性，被拦以后只能到 隐私与安全性 里点"仍要打开"（Sequoia 去掉了按住 Control 打开）[研]。要用就在新 Mac 上 git clone、本机重造。
- **不写、也不建议**去掉隔离属性或关掉 Gatekeeper 的命令。被拦了的办法只有一个：本机重造。
- 安装脚本拒绝带隔离属性的 checkout（浏览器下载的源码包）：从它本机造出来的东西就绕过了 Gatekeeper 本该做的检查。
- 模型文件：`cpa-stt setup` 链接时算一次 SHA-256，和 docs/22 列的官方值比；对不上就显示"未知来源的模型"，照样能用。
- LaunchAgent 仍是 plist + `launchctl bootstrap`，不用 SMAppService。`bootstrap` 的返回码要看（今天 cpad 和 cpa-stt 的 `install()` 都不看 [源 service/launchd.py:79-80；stt/service.py:49-50]）。
- 公证要付费开发者账号；免费的 Apple Development 身份要 Xcode，这台 Mac 没装 [测]。

## 6. 恢复

| 情况 | 发生什么 | 首次设置显示 | 你做什么 |
|---|---|---|---|
| **换 Mac，用迁移助理** | 笔记、`events.jsonl`、`$CPA_HOME`（包括 `keys/seal/` 里的私钥文件）都过去了；`host_hw` 对不上 → 换新的 `host_id`，日志只写"hardware changed" [源 docs/18 §4.1]；设备加密以后同时 `cpa-key rotate`：新钥匙在用，迁移过来的旧钥匙只解不封（docs/26 §5.3、D23）；O4 在第一次 `hello` 之前就把名字改成"<旧名>（新）"（截到 ≤24 B），设备上不会出现两台同名的 Mac；plist 指的 Python 可能不在了 | "后台服务：指的 Python 不存在"［装上］；"这台 Mac：名字暂时是「Mac·8C1D（新）」"［改名字…］；"家 / 公司：等设备" | 点两下；设备上选档案（3–4 下）。设备加密以后，确认了新 Mac 就能证明旧 `kid`，只封给旧钥匙的待传也交过来（docs/26 §5.5 第 2 条）；设备发现新 Mac 也有旧 Mac 在用的钥匙时问"忘掉旧的？"，光标在"留着"（docs/26 §5.3）。旧 Mac 不再用了就选忘掉，或者有空到 系统 → 档案 忘掉它；旧 Mac 留着改作他用（比如当公司的），先在它上面跑 `cpa host reset`，不然新 Mac 手上的旧钥匙解得开封给它的东西（docs/26 §5.3、D23） |
| 换 Mac，不用迁移助理 | 新装，空的归档 | 全新的表；"搬设置"一行 | 旧 Mac 上"导出设置…"，新 Mac 导入；旧归档留在旧 Mac 或 `cpa export` 存档 |
| 同一台 Mac 重装系统 | 硬件指纹一样，但 `$CPA_HOME` 没了 → 新 `host_id`；设备加密以后私钥也没了，新装生成新钥匙。从 Time Machine 恢复 `$CPA_HOME` 的话，身份和私钥都回来（docs/26 §5.4） | 同上 | 设备上再选一次；忘掉旧的那条。设备加密以后，忘掉前设备提示"有 N 条只有这台 Mac 解得开"：没有 Time Machine 备份，它们就永远解不开了，可以丢掉（docs/26 §5.4、D22） |
| Mac 送人 | — | — | `cpa host reset --wipe-keys`（要输 `y`）：换新 `host_id`；先提示"`sealed/` 里还有 N 条没解开，设备上只封给这台的也会永远解不开"，再删掉 `keys/seal/` 里的全部私钥（docs/26 §5.3、§5.5 第 3 条）。只是改作他用、不送人，用不带 `--wipe-keys` 的 `cpa host reset`：换身份也换钥匙，旧钥匙留着只解不封。设备上忘掉它，需要的话丢掉只有它解得开的（docs/26 §5.4）[源 docs/18 §4.1] |
| **换设备，或设备被擦**（Launcher 擦了 NVS、整片刷写） | Mac 还绑着旧序列号；v0.8 设备丢了绑定表，存储上的 `PROFON` 让它什么都不交，等你重新绑定 [源 docs/18 T9]。设备加密的收件人表和 `SEALON` 写在 NVS 和每个挂上的存储上：只擦了 NVS、内部 cpafs 还在时开机自动恢复；只剩 SD 卡上那份时（Launcher 布局没有 cpafs），每台 Mac 插上、证明了自己的钥匙才恢复它那一条；表一条都找不回来时，设备照录明文、带"明"字，写"加密：收件人表丢了"；整片刷写连卡上的都没了（或者又换了卡），每台 Mac 要在设备上重新点头（docs/26 §4.3"镜像"） | "设备：插着的不是绑定的那台：7C21。换了设备？"［改绑］ | 每台 Mac 点一下改绑；设备上每台 Mac 选一次档案。旧设备上没同步的东西，设备丢了就丢了（T7） |
| **换 SD 卡** | 绑定在 NVS，不受影响 [源 docs/18 §3.2]；旧卡上的待传和录音留在旧卡；新卡挂上时写 `PROFON` | "设备"一行加一句"卡上没有待传" | 想取旧卡的东西就把旧卡插回去、插上对应档案的 Mac |
| 旧卡插进另一台设备 | 卡上的 `PROFON` 打开那台设备的档案模式，带档案标记的等它的绑定 [源 docs/18 原则 8]。SEAL 以后，卡上封好的东西只交给证明了收件人里某把钥匙的 Mac：那些 Mac 没在另一台设备上登记过，就交不出去 | — | 要取就把卡插回原来的设备 |
| 用读卡器在 Mac 上读卡 | 明文（SEAL 之前）；SEAL 以后只有密文和元数据（docs/26 §3.1 T-CARD） | — | 不支持当作同步的路 |
| 升级以后版本对不上 | `cpa`、cpad、hooks、cpa-stt 服务的版本或路径不一致 | "后台服务：在跑的是 0.5.0，装的是 0.6.0"［换成新版本］；装的比在跑的旧时只写一句 | 点一下 |
| 模型文件被挪走 | `model_missing` | "本地转写：模型找不到了"［找模型］ | 点一下；以后用"搬进来"放进 `$CPA_HOME/models` |

## 7. 安全和隐私

### 7.1 规则

| 规则 | 怎么做到 |
|---|---|
| 没确认的板子什么都拿不到 | 不说话就认（§3.1）：不开串口、不发 `host_id`、不收任何一条 |
| 冒充绑定的那块板（报同一个序列号） | **收窄了，没堵上**：它照样拿得到 `host_id`。设备向 Mac 证明自己（docs/26 D10，SEC-7）也只挡从没碰过这台设备的板子，SEC-7 以后序列号验过才算绑上（§3.1 第 4 条）；界面不说"已验证"（docs/26 §5.5 第 4 条） |
| 设备只认它自己选过的 Mac | docs/18 §3.2、§4；Mac 自报的档案没有这个字段 |
| 公钥钉在设备上确认的那一刻 | §3.5 第 1、2 条；docs/26 §4.3、D21：换钥匙要用旧钥匙证明，否则设备上问、光标在"不换"；每次握手证明有私钥，证明了才交封好的、才认 ack 和删；钉住以后不带公钥或没证明，按"未分配"什么都不给 |
| 导入不放宽权限 | §4.2：一律 `plan`、目录在 `$HOME` 下、不搬 `cmd` 和自写链接、送出 Mac 的再确认、档案不同要再确认 |
| 导出不带秘密 | `settings/v1` 的白名单和泄漏检查（§4.2），泄漏检查也查 `keys/`；私钥按 docs/26 D6 是 `$CPA_HOME/keys/seal/` 下的 0600 文件，不放钥匙串，不进任何导出 |
| 设置动作不进 MCP | §2.5；测试断言 |
| 写只发生在你点了以后 | 首次设置一按钮一命令，先显示改什么；忙不忙由命令自己判断 |
| 不弄松系统 | 不改系统设置、不关 Gatekeeper、不去隔离属性、不要 sudo、不要辅助功能权限 |
| 不显示本机身份 | 只显示设备码，设备加密以后再加钥匙的 8 位指纹（公开的，用来和设备上的对）；`host_id`、`dev`、USB 序列号不进 `setup/v1`、日志和 MCP 输出 |
| 文件权限 | `$CPA_HOME` 0700，里面 0600（`keys/seal/` 目录 0700、私钥 0600，docs/26 §5.1）；导出文件 0600 |
| 身份跟着硬件 | `host_hw` 提前到 O4；只防老实的 cpad 用拷来的身份（§3.5 第 7 条）；设备加密以后换身份（`host_hw` 对不上、`cpa host reset`）时也换钥匙，旧的只解不封（docs/26 D23） |
| 笔记目录不进网盘 | `cpa notes dir` 拒绝网盘路径，只有终端里加 `--allow-cloud` 才放行（app 从不加） |
| 剩下的风险 | 存心读设备的 Mac 能读到所有还没同步的明文（docs/18 T6），要等 SEAL；SEAL 也只防被动地读，能写 Flash 的 Mac 能改收件人表和固件，以后记的照样拿得到，插着时还能经 USB-JTAG 读内存（docs/26 §3.1 T-USB）；还没登记钥匙的 Mac，没封的待传仍然只靠 `host_id`；迁移过来的旧私钥解得开封给旧 Mac 的东西，旧 Mac 改作他用前要跑 `cpa host reset`（docs/26 §5.3）；还没升级 cpa 的 Mac 插上照样接管；设备上的 debug 固件不算数（docs/18 §8.1） |

### 7.2 安全测试：并进 docs/26 的 M-SEC

每个工作包先在包里写自己的安全测试（RED）；M-SEC 是跨包的对抗性测试，按 docs/26 §6 跑两轮：基线轮（SEC-3，现在）和加密轮（SEC-6，SEAL 之后），结果写进 `docs/gates/sec.md`，自动的部分之后每次发版都跑。本文给 M-SEC 加两部分。

**第 1 部分：只动 Mac，O0–O6 合并后跑，必须全过。它不测加密。**

它不在 docs/26 基线轮（SEC-3）的清单里：基线轮现在就跑，那时 O0–O6 还没做完。结果照样写进 `docs/gates/sec.md`。

1. **陌生板子**：模拟器扮一块没绑定的板子，断言 cpad 从没打开它的串口，线上从没出现 `host_id`、`host_label`；60 s 后设备的 `pending`、`files.n` 不变。
2. **仿冒**：一块序列号和绑定的一样、`dev` 不一样的板子 → 断开并报错；一块序列号和 `dev` 都一样的 → **今天会通过**，写下来作为剩余风险。
3. **老配置**：没有序列号的 config.json → 一律要确认。
4. **克隆**：把 `$CPA_HOME` 拷到另一个临时 HOME、换一个假的 IOPlatformUUID → 换身份、第一次 `hello` 之前名字就改成"<旧名>（新）"、"名字"一行要你改。USB 只有一个口，两台**先后**连。
5. **导入提权**：导入的文件里写 `auto`、带 `cmd` 去向、自己写的 `url` 模板、`$HOME` 外或点开头目录的路径、`..`、跳出 `$HOME` 的符号链接、别的档案 → 全被拒、降为 `plan` 或要再确认。
6. **泄漏**：每种设置组合导出，泄漏检查全找不到（包括 `$CPA_HOME/keys/` 下的东西，docs/26 §5.2）；非法文件（超长、控制字符、未知字段的值）全拒。
7. **MCP**：工具表里没有任何设置动作（`mcp/catalog.py`），打开写工具也一样。
8. **权限和安装**：`$CPA_HOME`、导出文件、LaunchAgent plist 的权限；安装脚本不调 sudo、不写 `~/.claude`，只在取代码和钉死的依赖时联网（`--upgrade` 的 `git fetch --tags`、`uv build`、`uv tool install`），首次设置页和 `cpa setup check` 从不联网；带隔离属性的 checkout 被拒。
9. **app**：本机造的 app 没有 `com.apple.quarantine`；`codesign --verify --strict` 通过；docs/24 §8.1 的规则扫描（没有 `URLSession`、`cpad.sock`、`/bin/sh`）。

**第 2 部分：v0.8 和 SEAL 之后，在 M-SEC 第二轮（加密轮，docs/26 SEC-6）里跑。**

1. 设备只认它选过的 Mac：v0.8 模拟器上的三台假 Mac（docs/18 §12.4）。
2. 公钥被换：同一个 `host_id`、不同的公钥、证明不了旧钥匙 → 按"未分配"什么都拿不到（没封的、快照、在线请求也没有），设备弹确认、光标在"不换"，Mac 上"家 / 公司"一行写"设备在问要不要换钥匙"；同一次握手里用旧钥匙证明了的轮换 → 直接换，提示一句（docs/26 §4.3"钉住"、§6.2 S1-d）。
3. 没有私钥的 Mac：只有 `host_id` → 证明过不了，封好的一条都拿不到，它对封好的条目回的 ack 和 `file.del` 不起作用（docs/26 §6.2 S1-d）。**同一个 `host_id`、`hello` 不带 `seal`**：钥匙已经钉住的，按"未分配"什么都不给；还没登记钥匙的，没封的待传照 docs/18 的规矩。只封给别的 Mac 的东西不算进它的 `pending`、`files.n`。
4. 设备和 SD 卡上没有明文正文（docs/26 §6.3 S2-a、S2-b）。
5. **迁移**（docs/26 D23）：把 `$CPA_HOME` 拷到另一个临时 HOME、换一个假的 IOPlatformUUID → 换身份的同时跑了 `cpa-key rotate`，两个 HOME 没有同一把在用的钥匙，旧钥匙只解不封；新的那个第一次 `hello` 就报"<旧名>（新）"；确认绑定以后能证明旧 `kid`、取走只封给旧钥匙的待传；设备问"忘掉旧的？"，光标在"留着"（docs/26 §5.3）。
6. **送人和改作他用**（docs/26 §5.3、§5.5 第 3 条）：`cpa host reset` 换身份也换钥匙，新身份的在用 `kid` 不在设备的表里；`--wipe-keys` 以后 `keys/seal/` 里一把私钥都不剩，提示过 N 条解不开、要输 `y`。
7. **已绑定、没登记钥匙的 Mac**（docs/26 §4.3"登记"）：分档案时绑定过、那时没有 `cpa-key` 的 Mac，装上 `cpa-key` 后第一次握手，设备弹"让…解开设备上的记录？"、光标在"以后再说"；选了以后再说，下次不再问，系统 → 加密 里"加上"照样能加。

## 8. 分期、工作包、测试

一律先写失败的测试（RED → GREEN → REFACTOR）。O0–O5 不要设备，用现有的假串口和模拟器。

### 8.1 工作包

| 包 | 内容 | 动哪里 | 代码 / 测试（行）[推] | 依赖 |
|---|---|---|---|---|
| **O0 发版卫生** | `uv version` 跟 tag、CI 检查；Release 带 wheel 和 commit；README 和 daemon/README 只留一种装法；cpa-stt 报版本 | `daemon/pyproject.toml`、`daemon/uv.lock`、`cpad/__init__.py`、`stt/`、`.github/workflows/{ci,release}.yml`、README | 60 / 80 | 无 |
| **O3 你点了才连**（紧接 O0） | 没序列号就要确认（不加开关）；只读 USB 认板子、设备码、一次性候选号；`cpa device ls / bind / forget --json`、`--charge`；绑定后核对 `fw` 和 `dev`；换了设备；`rig.py`、`cpad_sim` 先写序列号。**不改协议和向量** | `device/port_finder.py`、`app.py` 的 `_bind`、`control/server.py`、`cli/commands.py`、`config/settings.py`、`tools/hil/harness/rig.py`、`tools/sim/cpad_sim.py` | 380 / 500 | O0 |
| **O2 首次设置** | 十一行检查（纯函数，输入是探到的事实）；`setup/v1`；终端逐行；`cpa setup skip`；`cpa notes dir`（档案已有东西就拒、拒网盘、改了就重启两个服务）；`cpa hooks install --json`；`cpa-stt setup --json`、`--move`、模型位置警告和哈希；`service install` 的忙碌检查、核对 bootstrap 和登录项（cpad 和 cpa-stt 两份）；`cpa status --json` 加 `notes_dir`、`service{installed, running, program_ok}`、`device.code`、`device.prof`；"磁盘加密"一行 | 新 `cpad/setup/`、`cpad/cli/setup.py`、`protocol/setup/v1/`；改 `cli/main.py`、`cli/hooks.py`、`service/launchd.py`、`stt/cli_setup.py`、`stt/service.py` | 900 / 1,100 | O0；O3 的 `device ls --json`；`cpa host name`（docs/23 P1-B，没合并就在这里先做） |
| **O1 安装脚本** | §2.2 的步骤；挑 tag 再执行 tag 里的自己；`--upgrade`、`--dry-run`、`--dev`；隔离属性和工作区检查；新的 out-dir 和约束；重启已装的服务；带空格路径的测试 | 新 `scripts/mac-install.sh`、`scripts/tests/` | 260 / 240 | O0；O2 里 `service install` 的修复 |
| **O4 身份跟硬件** | docs/18 §4.1 的 `host_hw` 和 `cpa host reset` 提前；新 id 用 `token_hex(16)`；默认名字 `Mac·XXXX`；换了硬件时第一次 `hello` 之前把名字改成"<旧名>（新）"（≤24 B），名字一行要你改；SEAL 以后换 id（`host_hw` 对不上、`cpa host reset`）时同时换钥匙，`--wipe-keys`（O8，docs/26 §5.3、D23） | `config/host.py`、`config/settings.py`、`cli/` | 140 / 200 | 无 |
| **O5 设置搬家** | `cpa settings export / import`、`settings/v1`；§4.2 的全部安全规则；泄漏检查；档案按 `served` 记和比；去向部分等 docs/25 R1：重跑 `dest add` 的检查，`act`、处理的 app、`profiles` 在新 Mac 上重来 | 新 `cpad/settings_io/`、`protocol/settings/v1/` | 460 / 580 | 去向那块等 `feat/destinations` |
| **O6 Mac app 首次设置** | docs/24 加一屏"首次设置"和 设置 → 这台 Mac（含"导出设置…"和 AirDrop）；按 `setup/v1` 画；`op` → argv 的自己的表；设备行的各种状态；"不用"；首次设置没做完时的后台设备查询和菜单栏的点；截图状态 | `mac/Sources/{CardputerCore,CardputerUI}`、`mac/Fixtures/` | 750 / 550 | O2、O3；docs/24 v1 |
| **O7 设备端** | 最小档案提前（docs/18 §15 第 9 条）；系统页"设备码"；"Mac 没回应"的提示 | 见 docs/18 §13 | 跟 docs/18 | §9.2 第 2 条 |
| **O8 接上设备加密** | §3.5 的七条已经收进 docs/26（§4.3、§5.3、§5.5、SEC-5），不用再写。SEAL 上线时本文这边要接的：安装脚本第 7 步造 `cpa-key`、前台跑 `init` 和 `selftest`、最后 kickstart cpad（§2.2）；"连上"等 `selftest` 通过（§2.3）；O4 换 `host_id`、`cpa host reset` 时跑 `cpa-key rotate`，`--wipe-keys` 跑 `cpa-key wipe`（§3.5 第 7 条、§6）；"设备"一行加一句只封给别的 Mac 的留在设备上（§2.3）；"家 / 公司"一行加 `seal_chal.kid` 的两种情况（§3.2）；"磁盘加密"一行改用 `cpa doctor security`、显示钥匙指纹 | `scripts/mac-install.sh`、`config/host.py`、`cpad/setup/` | 跟 docs/26 SEC-5 | docs/26 SEC-5（SEAL）；"磁盘加密"那条只等 SEC-1 |
| **M-SEC 新 Mac 部分** | §7.2 | `docs/gates/sec.md` | — / 350 | 第 1 部分：O0–O6；第 2 部分：v0.8 + SEAL + O8 |

**合计** [推]：O0–O5 约 2,200 行代码、2,700 行测试；O6 +750 / +550；M-SEC 新 Mac 部分约 350 行测试。

```
README 提醒 ─ 本 PR
O0 ─► O3（你点了才连，先上）─► O2 ─► O6 ─► M-SEC 新 Mac 第 1 部分
O0 ─► O2 里 service install 的修复 ─► O1
O4（独立）   O5（去向那块等 docs/25 R1）
§9.2 第 2 条答"提前" ─► O7（docs/18 P8-P、F0、F1、F2、D1）
docs/18 v0.8 ─► docs/26 SEC-5（SEAL）─► O8 ─► M-SEC 新 Mac 第 2 部分（docs/26 SEC-6 加密轮）
```

### 8.2 测试

- **认板子**（假的 USB 列表和假串口）：没绑定时 cpad 从不打开端口（假串口的打开次数为 0）；设备码由规范化的序列号算出，和由 `dev` 算出的一样；`cand` 拔掉作废、用过作废；两块同码不给 `cand`；`device ls --json` 里没有序列号原文；绑定后第一次 hello 核对 `fw` 和 `dev`；只充电的板子永不打开；有序列号的老配置一字不变；没序列号的老配置要确认。
- **`setup check`**：每个事实组合（服务没装 / 指错 Python / 版本旧 / 版本新 / 拉录音中 / 被登录项关掉；笔记目录在 iCloud、CloudStorage、Dropbox、`~/Documents`；`whisper-cli` 缺、模型缺、模型链接指进"下载"、WhisperWoof 认领；hooks 指旧路径；设备没报 `prof` 时没有"家 / 公司"，O7 的固件报了就有）→ 固定的行；JSON 过 `check_setup.py`；`invalid/` 例子全拒；假 Runner 断言不调 `mdfind`、不起登录 shell。
- **`service install`**：`bootstrap` 失败 → 非零退出和原因码；忙时拒绝，`--force` 放行；不降级；cpa-stt 那份一样。
- **`notes dir`**：档案已有东西就拒；网盘拒；改了以后重启两个服务（假 `launchctl`）。
- **cpa-stt**：模型链接指进 下载 / 文稿 / 桌面 / 网盘 时警告；`--move` 在同一个卷上是改名；`setup --json` 不链接、不读 stdin；哈希对不上显示"未知来源"。
- **`host_hw`**：升级上来保留 id；对不上换新 id（32 个字符）且日志不含新旧值；名字一行变成要你改；`host reset` 要 `y`；第一次 `hello` 带的名字已经是"<旧名>（新）"；SEAL 以后对不上、`host reset` 时调用 `cpa-key rotate`（假 `cpa-key` 断言参数），旧钥匙文件不删；`host reset --wipe-keys` 先报 N 条、要 `y`，再调 `cpa-key wipe`（docs/26 §5.3、D23）。
- **设置**：导出导入往返；泄漏检查（含 `keys/`、`cpa-key` 的路径、`file.*` 的绝对路径）；§7.2 第 1 部分第 5 条的提权全拒；编号冲突、目录不存在（先不加）、未知字段、`v:2`；导入两次结果一样；档案按两边的 `served` 比，任一边空就不比；导入的去向一律 `act`、`profiles` 是这台 Mac 的、处理的 app 没装就不加。
- **安装脚本**：shellcheck；临时 HOME 下 `--dry-run` 的步骤；没有 `uv` / `swift` 时停在对的那一步；从带空格的路径跑；带隔离属性的 checkout 被拒；`--upgrade` 切 tag 后执行的是新脚本；装出的 `cpa --version` 等于 tag。
- **Mac app**（docs/24 §8 的做法）：`setup/v1` 例子能解析；`op` → argv 表；`cpa` 比 cpad 旧时没有按钮；设备行每种状态和首次设置每种状态的截图；首次设置做完后后台查询停止。
- **端到端"第二台 Mac"**：两个临时 `CPA_HOME` + 模拟器设备（docs/18 §12.4 的三台假 Mac 测法）：A 绑定并拿走；B 插上，确认前从没打开串口；确认后拿到；v0.8 固件下 B 只拿到它档案的。
- **真机 👤**：在新的 macOS 用户里计时 docs/07 第 7 条（≤15 分钟），加一条新的"第二台 Mac 从克隆到必需的行全绿 ≤10 分钟，不含拷模型"。

## 9. 决定和要你拍板的事

### 9.1 决定

不回复就按默认做。

| # | 决定 | 默认（推荐） | 备选 | 为什么 | 见 |
|---|---|---|---|---|---|
| D1 | 安装渠道 | **2026-10-26 修订**：一行安装 `scripts/install.sh`，从发布资产装（wheel、钉死的 pyserial、app zip），每个文件核对 API `digest` 和 `SHA256SUMS`，再跑一次就是升级；私有仓库用 gh 或 `GITHUB_TOKEN`。原来是：git checkout + `scripts/mac-install.sh`，脚本自己挑最新 tag，`--upgrade` 升级；公开后加 Homebrew formula | Release wheel；cask；`.pkg`；app 内嵌 cpad | 没有 Developer ID；本机造的不被隔离；私有仓库哪条路都要认证 | §5.1 |
| D2 | 版本号 | 跟 tag（`uv version`），CI 检查；脚本从新的临时目录装那一个 wheel；cpa-stt 也报版本 | uv 的 `cache-keys` | 0.1.0 不变就装回旧代码，`--version` 也没用 | §5.3 |
| D3 | 首次设置的接口 | 公开命令 `cpa setup check --json`（`setup/v1`），5 s 内回来；Mac app 和终端都用它 | app 自己读 `$CPA_HOME` 检查 | docs/24 §1.2；任何 app 都能做同样的页 | §2.6 |
| D4 | 谁装后台服务 | 安装脚本装；app 在首次设置里和 `cpa` 比 cpad 新时可以一键装；命令自己查忙不忙、核对真的在跑、不降级 | 只给复制命令 | 新装时没有服务在跑；忙碌检查放在命令里，终端也安全 | §2.2、§2.5 |
| D5 | Mac 认设备 | 不说话、只读 USB 序列号；你点了才连；没序列号一律要确认，不加开关；还有"只给它充电"和"断开" | 发一个不带 `host_id` 的 hello 去探测；今天的静默先到先得 | 探测也是握手，设备会推笔记、白白"在线" 15 s，还要改协议 | §3.1 |
| D6 | 设备码 | `sha256("cpa-dev:" + 规范化的序列号)` 前 4 位，大写；Mac 先显示，设备系统页随 O7（或 v0.8）；和 docs/26 的 8 位钥匙指纹（`9C1B·27E0`）是两样东西 | 不做；显示序列号末 4 位 | 分得清板子，不泄露出厂 MAC；不是口令 | §3.1 |
| D7 | 设备认 Mac | 照 docs/18 v0.8：设备上选，存 `host_id` 哈希；Mac 写出确切按键和改法；USB 上不加配对密钥、不对码 | Mac 上选档案；两屏对码 | Mac 自己说的不算 | §3.2、§3.4 |
| D8 | v0.8 之前的多台 Mac | 最小档案提前到下一个固件版本（docs/18 §15 第 9 条）；同时 D5 和 README 提醒；"会议跟着第一台 Mac"不做 | 等 v0.8 全套 | 语音说了要多台 Mac；用法 A 下会议本来就不会被拆 | §3.3 👤 |
| D9 | 这台 Mac 的名字 | 默认 `Mac·` 加 4 位随机十六进制（修订 docs/23 D10 的"默认为空"，docs/26 §4.4 跟着改）；建议你改成认得出的名字，但不建议"家""公司"这类档案的词 | 默认取 macOS 电脑名；按钮直接给"家""公司" | 电脑名常带真名；档案的词会把你往某个档案上引（docs/18 T5），还会和"家 / 公司"那一行混 | §2.3、§3.2 |
| D10 | `host_hw` | 提前到 O4，只动 Mac；新 id 用 32 个字符；只防老实的 cpad 用拷来的身份；对不上就换新的 `host_id`、第一次 `hello` 之前名字改成"<旧名>（新）"，SEAL 以后同时 `cpa-key rotate`，旧钥匙只解不封（docs/26 §5.3、D23） | 跟 v0.8 | 越早写指纹，迁移助理搬出的克隆越少；新身份不拿旧钥匙当在用的；设备上不会出现两台同名的 Mac | §3.5、§6 |
| D11 | 什么跨 Mac | 数据不跨；身份从不跨；SEAL 私钥不进任何导出，只跟着迁移助理和 Time Machine 走，过去以后只解不封（docs/26 D6、D23）；设置（1–9、去向、`rec_pull`、引擎名）可以搬 | 网盘同步笔记目录；同步 `events.jsonl` | 一个日志一个写者；不上云 | §4.1 |
| D12 | 设置怎么搬 | `settings/v1`：项目一律 `plan`、目录必须在 `$HOME` 下、不存在先不加；去向搬整条但不搬 `cmd` 和自写链接、送出 Mac 的再确认，每台 Mac 自己的部分（`act` / `draft`、处理的 app、`profiles`）在新 Mac 上重来；档案按两边的 `served` 比，不同要再确认；旧 Mac 的 app 有"导出设置…"直接 AirDrop | 设备当搬运工；iCloud；只搬预设名 | 这个文件决定 cpad 跑什么；只搬预设名会丢东西 | §4.2 |
| D13 | 换 Mac | 迁移助理；身份由硬件指纹换掉，SEAL 以后钥匙也换一把新的、旧的只解不封；设备再问一次绑定，确认后新 Mac 能取走只封给旧钥匙的待传（docs/26 §5.3、§5.5 第 2 条、D23）；名字先自动改成"<旧名>（新）"、名字一行请你改；首次设置修服务；旧 Mac 改作他用前跑 `cpa host reset`，送人用 `--wipe-keys` | 新装 + 导入设置 | 归档和历史一起过去，最省事 | §6 |
| D14 | 换设备 / 设备被擦 | "设备"一行变"换了设备？"，点一下改绑；v0.8 设备丢绑定后什么都不交 | 让你跑 `cpa device forget` | 今天只看到"设备没连"，不知道为什么 | §6 |
| D15 | 本地转写在新 Mac | 一行：找模型 / 搬进来 / 选文件 / 打开；模型要放在 `$CPA_HOME/models` 或别的稳的地方，指进 下载 / 文稿 / 桌面 / 网盘 就警告；哈希不对标"未知来源"；从不下载 | `cpa-stt` 自己下载 | docs/22 定的"不下载"不改；"下载"一清理模型就断 | §2.3 |
| D16 | hooks 和 MCP | hooks 先看改动再写；MCP 只复制连接设置，状态来自 `cpa mcp audit --last 50 --json`，不读别的 app 的配置；设置动作从不进 MCP | 跑 `claude mcp add` 一类的命令 | 改别人的配置要你看过；AI 客户端不能替你绑设备、导设置 | §2.3、§2.5 |
| D17 | 加密 | 按 docs/26（D6、D21、D23）：私钥是 `$CPA_HOME/keys/seal/` 下的 0600 文件，不放钥匙串、不导出；安装脚本总是装 `cpa-key`、前台跑 `init` 和 `selftest`，"连上"等它通过；公钥在你在设备上点头时登记并钉住；换钥匙要用旧钥匙证明，否则设备上问、光标在"不换"；SEAL 同版每次握手证明有私钥，证明了才交封好的、才认 ack 和删，钉住以后不带公钥或没证明就按"未分配"什么都不给；设备在 `seal_chal.kid` 里报它为这台 Mac 钉住的 `kid`（不放进 `prof.ok`）；Mac 上靠 FileVault 和加密的 Time Machine，首次设置只读地查 | 本文自己定私钥文件；钥匙串（docs/26 D6 的备选） | 不和 docs/26 打架；堵住偷 `host_id` 改封、删封好的记录 | §3.5 |
| D18 | 安全测试 | 不另起里程碑，并进 docs/26 的 M-SEC：第 1 部分（Mac，O0–O6 后，必须全过，不测加密），第 2 部分（v0.8 + SEAL 后，在加密轮 SEC-6 里）；每个包先写自己的安全测试 | 本文自己的 O-SEC | docs/26 已经定了 M-SEC 的两轮（基线轮、加密轮）；两份文档各跑各的会重复 | §7.2 |
| D19 | 可选行 | 只数必需的；可选的收起来，每行"不用"（`cpa setup skip`）；导入时不弹目录选择框 | 每一行都催 | 不用的东西不该一直挂着 | §2.3 |
| D20 | 插上设备有没有提示 | 首次设置没做完时，后台每 10 s 查一次设备、菜单栏加一个点，做完就停；v0.8 设备在"无响应"旁写原因 | 一直不轮询；一直轮询 | 新 Mac 上插了设备却两边都没反应，最让人卡住 | §2.3、§3.1 |
| D21 | 只改 Mac 端的发版 | 照样打完整的 tag，固件没变也构建一份 | 改 release.yml，允许只发 Mac 端 | 少一种发版流程；固件构建本来就是自动的 | §5.3 |

### 9.2 要你拍板

1. **你的用法**：**A** 家里一台、公司一台（默认）；**B** 一台笔记本来回带；**C** 几台都是自己的（比如台式加笔记本），同一个档案。👤 A：首次设置有"家 / 公司"一行，设置一般不用搬。B：本文大多用不上，回到 docs/18 P8-B1。C：几台都选同一个档案，导入同一份设置，再考虑"会议跟着第一台"（D8）。
2. **最小档案提前到下一个固件版本吗？**（D8，docs/18 §15 第 9 条；第 1 条答 B 就不用答）👤 默认**提前**：你已经在用或就要用第二台 Mac，在那之前第二台插上就会拿走家里的东西。
3. **仓库会公开吗？** 👤 默认**保持私有**：新 Mac 先装 gh、`gh auth login`，然后 §2.2 的一条命令。公开的话不用 gh，再加 Homebrew formula。
4. **受管理的 Mac 能装命令行工具、Homebrew 和 cpad 吗？** 👤 默认**能**。不能装命令行工具或 Homebrew：走 §5.2 的 wheel 退路，没有 Mac app，首次设置在终端里走。完全不能装 cpad：那个档案的东西一直留在设备上（docs/18 §14）。

"数据都加密指哪一边"已经在 docs/26 §8.2 问了，这里不再问。

**回复模板**（按 1–4 的顺序，不答的按默认）：`回：A／提前／私有／能`

## 附：评审意见怎么处理的

三份评审（用户、安全、可行性）的意见都接了，下面只列改法和有分歧时怎么定的。

| 来源 | 意见 | 怎么处理 |
|---|---|---|
| 用户 1 | "按两下 Enter"是错的；没写"以后再说"和"选错了"怎么办 | 写出确切按键；"家 / 公司"一行按 `prof` 分三种情况，各写改法（§3.2） |
| 用户 2、可行性 8 | 行的顺序让人先绑设备：1–9 被清空、没名字、数据落进默认目录 | 新顺序：服务 → 笔记目录 → 名字 → 搬设置 → 设备 → 家 / 公司；"连上"在服务和笔记目录 ✓ 前是灰的；名字有不暴露你的默认值；没项目时写明 1–9 会变空；`cpa notes dir` 在档案有东西后拒绝 |
| 用户 3、可行性 3 | 一条命令不老实；`git pull` 在 tag 上会失败；重跑不等于升级 | 脚本自己挑 tag 并执行 tag 里的自己；`--upgrade`；脚本装服务、重启已装的服务；README 一段带前置条件；§2.1 改成"最少 / 全套"两栏老实数。两份评审的升级做法合在一起：用户评审要的 `--upgrade` 做在脚本里，可行性评审的 `git fetch && git checkout` 写成手动的等价做法 |
| 用户 4 | 点错没法撤；警告用设备缩写 | 大白话；三个按钮都不是默认按钮；"只给它充电"；绑着的行有"断开" |
| 用户 5、安全 11 | 两行都叫"家 / 公司"；名字按钮用档案的词 | **分歧**：用户评审建议"家里的 Mac / 公司的 Mac"，安全评审建议不用档案的词。按安全的定：名字默认 `Mac·XXXX`，不建议档案的词；"家 / 公司"一行同时写出两种按法，不需要 Mac 猜你要哪个，选错的情况由第三种状态处理 |
| 用户 6 | 插上设备两边都没反应 | 首次设置没做完时后台查设备、菜单栏加点；"没发现"列出常见原因；v0.8 设备写"Mac 没回应"的原因。原来要的"收到 `probe:true` 时提示"不再适用，因为不再探测 |
| 用户 7 | 可选行一直催；导入弹很多目录框 | 只数必需的；可选收起、每行"不用"；目录不存在的先不加，不弹框 |
| 用户 8 | 旧 Mac 上导出要打命令 | 设置 → 这台 Mac →"导出设置…"，存临时目录直接 AirDrop |
| 用户 9 | AirDrop 的模型在"下载"里，链接会断 | "搬进来"（`--move`）；指进 下载 / 文稿 / 桌面 / 网盘 警告；测试 |
| 用户 10 | 迁移后两台 Mac 同名 | O4 发现换了硬件时名字一行要你改，预填"（新）" |
| 用户 11 | 受管理的 Mac 的退路走不通 | 写成浏览器下载或 AirDrop wheel → `shasum` 核对 → `uv tool install` → `cpa setup`；不在受管理的 Mac 上登录 GitHub |
| 用户 12、13 | 用语不清；拍板太重 | 用法 A / B / C 在 §0 定义并按顺序；§0 和界面不用开发者的词；问题减到 4 个，附回复模板；第 2 条写明依赖第 1 条 |
| 用户小点 | v0.8 前设备显示不了设备码；旧固件隐藏"家 / 公司"；带空格路径 | 请你看状态栏的 ↑ 和 录；旧固件不显示那一行；脚本测试从带空格的路径跑 |
| 安全 1 | 导入能提权、装命令 | §4.2 全部采纳：`plan`、`$HOME` 下的 realpath、不搬 `cmd` 和自写链接、送出 Mac 的再确认、导出拒网盘、记档案、泄漏检查加全格式序列号等、M-SEC 加提权用例 |
| 安全 2、可行性 10、11 | `host_id` 仍是口令；`host_pk` 可被换；"关掉 T4"说过头 | 改成"收窄"；公钥在设备上点头时钉住，换钥匙要用旧钥匙证明或在设备上确认（docs/26 D21）；SEAL 同版每次握手证明有私钥；设备在 `seal_chal.kid` 里报它为这台 Mac 钉住的 `kid`；新 id 32 个字符；`host_hw` 写明只防老实的 cpad；工具链约束交给 docs/26 |
| 安全 3、可行性 6 | 老配置照样静默绑定；新装默认 `confirm` 弄坏测试台、也判断不出"新装" | **分歧**：安全评审建议保留显式的 `auto`，可行性评审建议去掉开关。按更简单也更安全的定：去掉开关，没序列号一律要确认；`rig.py`、`cpad_sim` 先写序列号；没升级的 Mac 仍会静默绑定，写进剩余风险，也是 D8 的理由 |
| 安全 4、可行性 1、2 | 探测本身是握手：设备会推笔记、白白"在线" 15 s；`probe` 字段会让固件 CI 变红；"录3"的数不对 | **分歧**：安全评审给了两条路，可行性评审建议 O3 先发一个不带 `host_id` 的普通 hello。按安全评审的第一条定：**不说话，只读 USB 序列号**。这样不推笔记、不假在线、不改协议和向量，v0.8 也不需要 `probe` 字段。代价是连上前不知道固件和待传数 |
| 安全 5 | MCP 不能碰设置动作 | §2.5 写明，§7.2 加测试 |
| 安全 6 | "文件加密"在 Mac 上没落实；私钥会进 Time Machine | **和 docs/26 定稿对齐**：私钥按 docs/26 D6 是 `$CPA_HOME/keys/seal/` 下的 0600 文件，不放钥匙串；本文不再定 `host_sk` 文件；换硬件时换一把新钥匙，旧的不删（docs/26 D23：要靠它解开迁移前的待传）；加"磁盘加密"只读一行；`notes dir` 拒网盘 |
| 安全 7 | 按设备码绑定能被抢 | 用一次性候选号，拔掉作废；同码不给；绑定后核对 `dev` 和序列号。**不做**每次连接先探测再握手：`dev` 也是自报的，挡不住存心的，代价是每次多一轮（§3.1 末） |
| 安全 8、可行性 9 | O-SEC 太窄，又在 O0–O6 后跑不完 | 并进 docs/26 M-SEC，分两部分；第 1 部分明说不测加密；"两台同时连"改成先后连；每个包先写自己的安全测试 |
| 安全 9 | curl 一行、`xattr` 命令、tag 没核对、依赖没钉死、模型来源 | 删 curl 一行和 `xattr` 那句；脚本拒绝带隔离属性或不在 git 里的 checkout；打印 commit，发布说明自动带 commit；构建后端钉死、按 `uv.lock` 的约束装（哈希核对标 [待]）；模型哈希不对标"未知来源" |
| 安全 10 | app 一键装服务可能降级 | 只在 `cpa` 比 cpad 新时给按钮 |
| 可行性 4 | app 5 s 超时会杀掉 `service install`；bootstrap 失败也说成功 | 60 s 超时；核对 `bootstrap` 返回码和 `launchctl print`，报"被登录项关掉"；忙碌检查放进命令；cpa-stt 那份一起修 |
| 可行性 5 | 安全修复排在界面后面 | 改成 O0 → O3 → O2；README 提醒本 PR 就加 |
| 可行性 7 | 好几条命令不存在 | `hooks install --json`、`cpa-stt setup --json`、`notes dir`（拒绝 + 重启）进 O2；MCP 用现有的 `--last 50`；`setup check` 不用 Spotlight、不起登录 shell |
| 可行性 12 | 只搬预设名会丢东西 | 搬整条记录，按安全 1 的规则过滤 |
| 可行性 13 | 小点 | `xcode-select` 写成兜底；app 那步等 docs/24 v1；"插拔时刷新"写成页面开着时 3 s 一次；用 `uv version`；只改 Mac 端也打完整 tag（D21） |
| 可行性（行数） | 合计没算新加的 | 重算：O0–O5 约 2,200 / 2,700，O6 750 / 550 |
| docs/26 定稿（PR #68，`96a03b6`） | 本文照 docs/26 的早期草稿写了"私钥在钥匙串"；§3.5 第 1 条是冲着旧的 docs/26 §5.3 写的；D10、D13、D17 没写换钥匙 | 全文对齐：私钥是 0600 文件、不放钥匙串（D6）；钉住、握手证明、换钥匙照 D21；换硬件换钥匙照 D23；字段名、工具链、M-SEC 两轮照 docs/26 §4.3、§5.5 第 6 条、§6。docs/26 没写的几处标了 [待]（下一行已收掉） |
| 交叉核对（2026-10-22） | docs/26 和本文并行写，对着 docs/18、docs/25 的 main（`995821d`）又核了一遍：几处 [待] 是 docs/26 的空白，还有几处两边或和 docs/18、docs/25 前后不一 | docs/26 补上空白（`8fc1f4a`，它 §9 末行），本文跟着改：钉住了钥匙的 Mac 不带公钥或没证明就按"未分配"什么都不给（§3.5 第 1 条、§3.2 表的两行）；`kid` 改在 `seal_chal{E′, n, kid}` 里报（第 3、6 条）；已绑定、没登记钥匙的 Mac 也问一次（§3.2）；`cpa-key` 在 cpad 起来以后才造好，cpad 重跑自检、脚本最后 kickstart，"连上"等自检通过（§2.2、§2.3）；当前档案没有钥匙时明文带"明"（§2.2）；`rotate` 不带 `--keep-old`，`cpa host reset` 也换钥匙、送人用 `--wipe-keys`，旧 Mac 改作他用要跑 reset（§3.5 第 7 条、§6）；迁移后第一次 `hello` 前就改名"<旧名>（新）"（§6、O4）；收件人表在每个存储上都有一份（§6）；钥匙指纹 8 位、Mac 上也显示，和设备码分开（§2.3、§2.5、§3.1）；导入去向重跑检查、每台 Mac 自己的部分重来，档案按 `served` 比（§4.1、§4.2）；"家 / 公司"一行看设备报没报 `prof`，不看 v0.8（§2.3、§3.2）；`host_label` 默认值写明修订 docs/23 D10（D9）；D10 只挡从没碰过设备的板子（§3.1、§7.1）；联网的说法改准（§7.2）；旧卡进另一台设备（§6）；M-SEC 第 2 部分加送人和已绑定补登记两条 |

## 附：证据索引

- daemon（origin/main `d330465`）：`app.py:116`（`NotesWriter` 只建一次）、`245-248`（host hello）、`250-273`（`on_ready`、`_bind`）、`384-392`（配置重载只换对象）；`hub/reduce.py:76-77`（Ready 时推全套快照）、`124-136`；`device/port_finder.py:3、43、50-54`；`device/link.py:205-242`（主机先发 hello）；`config/host.py:31-41`（`token_hex(8)`）；`config/settings.py:56-65、101-119`；`projects/registry.py:21`（权限模式）；`cli/main.py:69、92、143-162、188-204`；`cli/commands.py:95`；`cli/hooks.py:135-147`（只有 `--cpa`、`--settings`、`--yes`）；`mcp/cli.py:107-113`（`audit --last`）；`mcp/catalog.py`（工具表）；`service/launchd.py:69-80`；`archive/cloud.py:1-24`；`archive/cli.py:46`（`--allow-cloud`）；`agents/claude_code/hook_settings.py:57-64`；`recordings/service.py:44-52`；`recordings/puller.py:151`；`recordings/manifest.py:4-6`；`stt/cli_setup.py:19-23、38`；`stt/cli.py:108-110、327`；`stt/service.py:42-50`；`stt/models.py:1-8`；`daemon/pyproject.toml:3、7、10-12`；`daemon/uv.lock`；`cpad/__init__.py:3`。
- 固件：`fw/protocol/messages.h:64、80、89`（`HostId`、`host_id`、`files.n` 是"已提交的段"）；`fw/protocol/protocol_limits.h:62`（`kLinkTimeoutMs`）；`fw/link/link_session.cpp:74-86、162`；`fw/shell/runtime_outbox.cpp:91`；`fw/hal/hal_platform.h:42`（`dev` 是 MAC 的 12 个小写十六进制）；`test/core/test_codec_vectors/test_main.cpp:1-4`、`test/core/test_link_flows/test_main.cpp:157-160`；`~/.platformio/.../esp32s3/qio_qspi/include/sdkconfig.h:565、603、622` [测，只读]。
- 协议：`protocol/README.md:49`（只新增、忽略未知字段）、第 103 行（Attached 时只回 `hello`）、第 154 行（重新握手退回待发）；`protocol/limits.json:71`；`protocol/schema/v0/hello.schema.json:58`；`protocol/vectors/v0/flows/`（没有 `link/` 子目录）。
- 测试工具：`tools/hil/harness/rig.py:157-170`；`tools/sim/cpad_sim.py:1-12`；`tools/sim/simtools/serial_pty.py:17`；`tools/sim/tests/test_e2e_cpad.py:46`。
- CI：`.github/workflows/release.yml:2、31、54`（只发固件，要求发布说明）；`ci.yml:147-180`（测已构建的 wheel）。
- 文档：README.md:30-70、200-213；SECURITY.md:28-57；docs/01 §1 第 1 条；docs/05:919；docs/07:14、138-143；docs/15:145；docs/18 §1.3、§3.2、§3.5、§3.7、§4、§5.1–§5.6、§8.2–§8.4、§9、§10.3、§13、§15；docs/21:92、182-186、424；docs/gates/whisperwoof.md:95；docs/gates/launcher.md 第 13–16 行；docs/23 §3.1、D10；docs/24 第 137、143 行和 §1.2、§2.3、§2.5、§4、§5.2、§7、§8.1、D8、D10、D12（`wt-mac`）；docs/25 第 16、22、97、124、126、170 行和 §2.3、§3.1、§4.5、§5.3、§5.7、D7、D12、D22（main `995821d`）；docs/26 §3.1、§4.3–§4.5、§5.1–§5.5、§6、§7、§8.1（`wt-sec`，PR #68 定稿 `96a03b6`，交叉核对后 `8fc1f4a`）；docs/18 §7.1 D1、§7.2、§10.1（main `995821d`）。
- GitHub：issue #57 和评论；PR #60；PR #62（已合并为 `d330465`）；issue #63（安全轮）、#64（本文）；PR #68（docs/26）；PR #65（docs/25，已合并，main `995821d`）。
- 本机 [测]（只读）：`uv tool list`；`~/.local/bin/cpa` 的链接；cpad LaunchAgent 的 `ProgramArguments` 和 `Environment`（plutil）；`$CPA_HOME` 和 `~/CardputerNotes` 只看文件名；`xcode-select -p`；签名身份数；`gh repo view` 的可见性。
- 外部 [研]：Homebrew 5.0.0 发布说明（`--no-quarantine` 和不签名 cask 的弃用）；Workbrew 对 Homebrew 5.0 的解读；Macworld、OSNews 对 Sequoia 打开未签名 app 的说明；Gatekeeper（Wikipedia）；Apple 开发者论坛 thread 779379、799910（SMAppService 和 ad-hoc 签名）；uv 缓存文档（`cache-keys`）。
