[English](README.md) · **中文**

# cpa-sim：在 Mac 上跑固件

Cardputer 只有一台，所以设备测试只能一个一个跑，新的屏幕也要刷进去才看得到（gh #32）。`cpa-sim` 把固件的纯运行时（不碰硬件的部分）编到主机上：和 `cpa-fw` 一样的 `Runtime`、外壳、模块、编解码、outbox 和录音核心，用设备的字体和颜色画出来。它做两件事：

- `cpa-sim shots` 跑一个脚本，存下截图（见下）。
- `cpa-sim serve` 是 pty 上的一台虚拟设备：真的 cpad 和 `tools/hil` 的 HIL 场景都能对着它跑，不用接 Cardputer（[虚拟设备](#虚拟设备serve)）。

原样从 `firmware/app` 编进来的：

- `lib/*`，除了 `hal` 和 `usb_transport`
- `modules/*`
- `src/module_list.cpp`（设备的模块列表）
- native 测试给各个端口做的假实现：`FakeTransport`、`FakePlatform`、`FakeStore` 和 `FakeRecPort`（它在内存里的一张卡上跑真的 `RecEngine`）。模拟器的 `SimRecPort` 在它上面加了真的 SHA-256，并把固件版本写进 manifest，因为 cpad 拉段时这两样都要查。

`firmware/app` 的源码一行不改。模拟器自己的代码在这个目录里：

| 库 | 做什么 |
|---|---|
| `lib/simfb` | `Frame`（240 x 135 的色板精灵：每个像素存色板里的一项）、`FbCanvas`（相当于 `hal/m5_display` 的 `Canvas`：主题的 `RasterCanvas` 加 efont 文字）、`SimDisplay`，以及从 M5GFX 移植的 efontCN_12 解码器 |
| `lib/simpng` | 8 位索引 PNG 写入器（只用标准库）、总览图，以及 `shots` 和 `serve` 写的文件 |
| `lib/simscript` | 脚本解析器、按键时间线，以及 `live.h`（serve 的 stdin） |
| `lib/simdev` | `SimDevice`（唯一给 `Runtime` 接线、也负责重启它的文件）、`SimRecPort` 和 `play()` |
| `lib/simio` | `PtyTransport`（serve 走 pty 的 USB 链路，带设备的 TX 环和行队列）、WAV 麦克风和 SHA-256 |
| `lib/efont_cn12` | efontCN_12 数组，构建时由 `gen_font.py` 从 M5GFX 里拷出来 |
| `lib/app_modules` | 编译 `../app/src/module_list.cpp` |
| `oracle/` | 可选：在主机上跑 M5GFX 本身，和 `FbCanvas` 画同样的用例（[见下](#m5gfx-oracle)） |

## 命令

要 PlatformIO 6.2，还要装一次 M5GFX（字体从它来）：

```sh
pio pkg install -d firmware/app -e cardputer-adv   # one time; or set CPA_M5GFX_DIR
```

`gen_font.py` 检查它找到的 M5GFX 是不是 `firmware/app/platformio.ini` 钉住的版本（0.2.33），以及其中 `lgfx_efont_cn.c` 的 sha256 是不是为这个版本钉住的那个。有一项不对，构建就停下。这个哈希或 `gen_font.py` 本身变了，字体会重新生成。

`pio pkg install` 还会在 `firmware/app/.pio/libdeps/cardputer-adv` 里留下 M5GFX 0.2.34 和 M5Unified 0.2.27，和 `M5GFX@0.2.33`、`M5Unified@0.2.26` 放在一起：M5Cardputer 对这两个要的都是 `"*"`。这在预料之中，没有害处。设备构建的依赖图仍然选钉住的版本，`gen_font.py` 也取钉住的那个。

```sh
pio test -d firmware/sim                     # the simulator's tests (about 20 s)
pio run -d firmware/sim                      # builds firmware/sim/.pio/build/sim/cpa-sim
cd firmware/sim
.pio/build/sim/cpa-sim shots scripts/v03_tour.cpas --out out/v03_tour
```

`shots` 往 `--out`（默认 `out`，git 忽略它）里写这些：

- 每个 `shot` 步骤一张 `NNN-<name>.png`
- `contact.png`：所有截图排成网格，带说明文字
- `shots.json`：每张截图的名字、说明、设备时间、背光等级、最前面的模块、这一帧里有几个 □（`missing_glyphs`）、`settled`，以及 `pushes`：从上一张截图以来推到面板上的东西（`full` 整帧数、`partial` 局部推屏次数和它们的像素数 `partial_px`；docs/19 §7.5）

其他选项：

- `--scale N`（1 到 8，默认 2）把每个像素重复成 N x N 个。不做平滑，像素保持原样。
- `--columns N`（默认 4）设总览图每行放几张。
- `--lang zh|en`（docs/21）让每个 `boot` 步骤都用这种语言开机，不管脚本里怎么写，所以任何脚本也都能画成英文：`cpa-sim shots scripts/v04_tour.cpas --lang en`。

截图前先等，最多 500 ms：主机发来的行都读完、屏幕重画完、正在滑动的 Tab 抽屉停稳。没等到就拍的照样保存，`shots.json` 里写 `"settled": false`，stderr 上打一条警告。

这个导览跑一遍约 0.3 s。

退出码：

| 退出码 | 含义 |
|---|---|
| 0 | 完成。 |
| 1 | 运行失败，比如 `reply` 没有可回的消息。 |
| 2 | 用法或脚本错误。消息里写着文件和行号。 |

## 脚本（`.cpas`）

脚本一行一步。`#` 开始注释：在行首，或者在空格、制表符后面，而且不在 `"` 字符串里。`type` 的文字里也是这样：`type a #b` 打出的是 `a`；`#` 前面有奇数个 `"` 时，它不算注释开头。要打出 ` #`，就按键：`press space shift+3`。脚本只按键、发协议消息，所以固件以后加的页面不用改模拟器。v0.4.0 的主页、Tab 菜单和锁屏也是这样进去的，比如 `press tab` 或 `press opt+l`。

```
include common/demo_state.cpas        # relative to this file
boot store=sd usb=on bat=4100         # also store=internal | none; a new boot is a new device
boot lang=en                          # the language NVS holds (default zh, docs/21)
host projects.snapshot {"rev":3,...}  # sends {"v":0,"t":"projects.snapshot","id":"hN",...}
reply outbox.ack outbox.item {"status":"stored"}   # "re" = id of the device's last outbox.item
press opt+s enter ; esc ms=80         # each key combo held 80 ms (default 50), then 30 ms up
hold g0 600                           # one long press
type call the vet re: Bingo's shots   # ASCII; Shift is added for shifted characters
down g0                               # held until `up g0` (`up` alone releases everything)
wait 600
mic 3200 2500                         # 3.2 s of a 440 Hz tone at RMS 2500, while recording
mic 3200 speech                       # a voice: syllables rising and falling, short pauses
usb off                               # pull the cable; plugging back in needs a new hello
bat 3350                              # battery in mV (sampled once a second)
shot memo-recording  Memo: recording  # file name, then the caption
frames memo 4 200 mic=speech Memo     # 4 shots 200 ms apart: memo-1 .. memo-4 (the animation)
```

`pull`（docs/28 S1a）是模拟的 daemon 把列给它的都拿走：先 `file.list`，再对每一项带上大小和 sha256 发 `file.del`，一页一页来，和 cpad 存好一段之后做的一样（字节本身不拉）。S1a 的固件把删掉的段留在卡上，而从某台 Mac 那里收回的段，那台 Mac 再也看不到。

`frames <name> <n> <ms> [mic=level|mic=speech] [caption]`（v0.5 S3）等屏幕画好拍第一张，之后每隔 `ms` 拍一张，到点就拍，什么都不等，所以拍得到动的东西（docs/19 §7）：Mando 的姿势（200 ms）、波形、呼吸的 ●、锁屏卡片的 z（600 ms）、Tab 抽屉的滑入（50 ms）。带 `mic=` 时，这几帧里麦克风一直在收，和 `mic` 一样。`scripts/theme_motion.cpas` 拍了每种动画的帧，也拍了背光调暗时的帧，那时什么都不动。

组合键是用 `+` 连起来的键名，比如 `opt+s`、`` fn+` ``、`shift+a`、`enter`、`space`、`bksp`、`tab` 或 `g0`。这些是 `firmware/app/lib/protocol/key_names.cpp` 里 `debug.key` 用的名字。另外还有这些别名：

- `up`、`down`、`left`、`right` 是印着方向箭头的 `;` `.` `,` `/` 四个键。
- `esc` 是 `` fn+` ``。

时间只按 10 ms 一轮的循环往前走：

- `press` 用 `debug.key` 的时序。
- `shot` 先跑循环，直到主机发来的行读完、屏幕重画完（最多 500 ms）。
- `host hello` 之后，模拟的 daemon 像 cpad 一样每 5 s 发一次 `ping`，所以等得再久链路也不断。
- `host debug.reboot` 在 ack 之后 200 ms 重启设备，和真机一样：boot 2，重启原因 `software`，卡和 outbox 都保留。ping 会停；要再发一次 `host hello`。

脚本不以 `boot` 开头时，用默认值开机。

`scripts/v03_tour.cpas` 走一遍 v0.3 的每一屏：

- 看板，两个项目、三个 session
- 项目页，带 session 和排队两个标签
- 采集浮层
- 新 session：编辑、确认、已开始
- 收件箱和系统页
- 一条便条和一场会议：同意、录音页、加星、结束
- 状态栏的各种状态：没有 daemon、在线、同步中、离线、录音中、电量低
- 拒绝：电量低、没有 SD 卡

`scripts/common/demo_state.cpas` 放着演示用的看板，别的脚本也共用它。

`scripts/ux/` 放着交互审查（docs/20 §4.4）的回归脚本，每个都用说明文字写清屏幕上必须是什么：`unknown_slot`（U13：不存在的项目槽位）、`queue_cursor`（U12：快照之后光标还在原来那一条上）、`toast_order`（U20：自己操作的结果先出来）、`memo_cap`（U15：便条到上限的屏幕等 G0 松开）、`memo_cap_battery`（同上，用电池：按住时屏幕一直亮着）、`locked_backlog`（U6 之后的 U20：锁着时结束的结果从不拖慢下一条）和 `unknown_spawn`（U25：Opt+S 之后结果一直没回来、存成排队的那一条，再开之前会说"可能已开过"）。

决定 D1–D10 和 B 包（v0.4.1，docs/20 §5.6）加了 `home`、`home_letters`（D1）、`autolock`（D2）、`lock_card`、`lock_gestures`（U3、D7）、`meeting_boxes`（D3）、`g0_hold_overlays`（D4）、`editor_back`（D5、D6）、`menu_chords`（D9）、`rec_pages`、`status_bar`、`inbox_board` 和 `copy_layout`（B2–B6）。之后的评审又加了 `lock_after_hold`（被吞掉的按住之后不再出现旧结果；口袋里一直按着，屏幕会熄）、`confirm_menu`（Tab 菜单的说法和框上一样）、`lock_need_chip`（没有数据时不写"等你 0"）、`box_hint`（框的提示跟着框走）、`end_box_menu`（框在菜单下面等着）、`meeting_stop_unlocked`（用电池、没锁时录音停了看得见）和 `ready_refused`（会被拒的按住不显示"● 准备"）。gh #51（docs/20 U26，并进了 v0.4.1）加了 `memo_grace`（G0 滑了一下，便条不断：0.6 s 宽限）和 `memo_handsfree`（点两下：免提，再按一下结束；锁着时结束写在点亮的卡片上）。docs/21（设置和英文，v0.4.3）加了 `settings_zh`（Opt+M 打开设置，光标在 WiFi 行；WiFi 页；设备标签；拔线后连接行还记着 Mac 的名字）和 `settings_en`（Opt+M . Enter：马上变成英文，`debug.reboot` 后还是英文，再切回来）。

docs/28 S1a（留底；两台 Mac 靠 hello 里的 `host_id` 区分）加了 `scripts/kept/`：`plug_hint`（离线录的两条便条，插到公司 Mac 上时先扣住 5 s；Tab、Enter 不给它，家里的 Mac 拿走）、`plug_hint_ignored`（不管它，5 s 后公司 Mac 拿走）、`deliver_again`（设置 → 存储 → 再交一次，确认框打开时光标在取消上：公司 Mac 不会再列出它们，家里的 Mac 会）和 `clear_kept`（锁屏卡片上的留N；清掉留底的确认框打开时光标在取消上）。每个也都用 `--lang en` 跑一遍。

## 虚拟设备（`serve`）

`serve` 按真实时间跑设备，每 10 ms 一轮循环，用一个 pty 当它的 USB 口。cpad、HIL 测试工具或终端打开这个 pty，就像打开 Cardputer 的串口一样。

```sh
pio run -d firmware/sim
firmware/sim/.pio/build/sim/cpa-sim serve [--store sd|internal|none] [--usb on|off] [--bat MV] \
    [--mic silence|FILE.wav] [--shots DIR] [--scale N]
```

它的第一行是 `pty /dev/ttysNNN`。遇到 Ctrl-C、SIGTERM、`quit` 或 stdin 结束就停，所以不会比启动它的工具活得久（没有程序驱动它时，要让 stdin 一直开着：`tail -f /dev/null | cpa-sim serve`）。它是 HIL 构建（`CPA_TEST_HOOKS`），所以 `debug.key`、`debug.state` 和 `debug.reboot` 都能用。

- **USB 链路**和 `UsbTransport` 一样：一行要整行放得进 4 KB 的 TX 环才写，否则在 4 行的队列里等，丢弃规则也一样。输入在 8 KB 的 RX 队列里等。和设备的 HWCDC 队列一样，队列满时到的字节直接丢掉，不会反压到主机，`serve` 打印 `rx_dropped <total>`。
  - 只有主机以 raw 模式开着这个口时，字节才发出去。pyserial 打开端口时会设 raw 模式。在那之前，行都在环里等，和设备上主机不读时一样。这也让 macOS 不会把它们回显回来：新打开的 slave 端一开始是开着回显的。
  - pty 没有线：stdin 上的 `usb off`（或 `--usb off`）就是拔线，`usb on` 之前两个方向都不通。
- **卡和 outbox** 都在内存里：`--store sd`（默认，一张空的 64 GB 卡）、`internal` 或 `none`。存储由固件自己的 `plan_storage` 规划，依据是按 `partitions/cpa_8MB.csv` 完整刷机后的情况：
  - `sd`：用卡，同时挂上 cpafs，所以开机迁移会跑（从空的 cpafs 迁）。系统页显示 `存储 SD 59G`。
  - `internal`：cpafs（1.5 MB），没有卡，所以系统页警告 `SD：未检测到卡`。
  - `none`：两样都没有。

  它们跨 `debug.reboot` 保留（这个命令在 ack 之后 200 ms 重启运行时，boot 号加一，重启原因 `software`）。进程退出就没了。
- **麦克风：** 录音时每 128 ms 给一块 4 KB，内容是静音，或者循环播放的 WAV 文件。只接受 16 kHz 单声道 16 位 PCM；别的文件用 `afconvert -f WAVE -d LEI16@16000 -c 1 in.m4a out.wav` 转。

stdin 接受脚本的步骤，一行一个，按顺序执行：`press`、`hold`、`type`、`down`、`up`、`wait`、`usb`、`bat`、`shot`，还有 `quit`。前面的按键按完了，下一步才开始；`shot` 等屏幕重画完。`boot`、`host`、`reply`、`mic` 和 `include` 只能在脚本里用，因为在 `serve` 上主机是 cpad。

stdout 每个事件一行：

| 行 | 什么时候 |
|---|---|
| `pty <path>` | 第一行：设备的端口 |
| `shot <file> <module> <backlight>` | 存下了一张 `shot`，存成 `--shots DIR/NNN-<name>.png`（默认 `out/serve`） |
| `usb on`、`usb off`、`bat <mv>` | 这一步生效了 |
| `reboot <boot>` | 设备在 `debug.reboot` 之后重启了 |
| `rx_dropped <bytes>` | 主机写得太快，8 KB 的 RX 队列满了；到目前为止一共丢了多少 |
| `error <message>` | stdin 的一行被拒绝 |
| `bye` | 最后一行 |

### 虚拟设备上的 cpad

`tools/sim/cpad_sim.py` 在前台跑真的 cpad，只给它这个 pty：

```sh
CPA_HOME=$(mktemp -d) uv run --project daemon python tools/sim/cpad_sim.py --port /dev/ttysNNN
CPA_HOME=<the same dir> uv run --project daemon cpa status   # link: ready  dev=100000000051
```

- `CPA_HOME` 必须是系统临时目录里的一个目录，否则它不跑；笔记目录也必须是临时的。
- 没有 `config.json` 时，它写一份：`notes_dir=$CPA_HOME/notes`，用 fake-claude（`daemon/tests/fixtures/bin/fake-claude`，状态在 `$CPA_HOME/fake-claude/state.json`）。
- cpad 的端口列表只报这个 pty，当成一台序列号为 `SIM-100000000051` 的 Cardputer，所以它不会打开真的板子。你自己的 cpad 从不列出 pty，所以两边碰不到一起。
- pyserial 在 pty 上设不了 RTS / DTR，cpad 的 SafeOpen 会失败。`PtySerial` 让这两个调用什么都不做；其余都是 pyserial 自己的端口。

### 虚拟设备上的 HIL 场景

`tools/sim/run_hil_sim.py` 启动 `serve`，用 `tools/hil` 自己的运行器对着它跑，用的是测试工具的临时 `CPA_HOME`、笔记目录和 fake-claude。`tools/hil` 不改：跑的时候替换掉端口列表、tap 的串口类和端口占用检查。

```sh
uv run --project daemon python tools/sim/run_hil_sim.py --scenarios handshake,capture,board \
    --report "$TMPDIR/hil-sim.json"
uv run --project daemon python tools/sim/run_hil_sim.py --scenarios rec_nosd --sim-args "--store none"
```

下面这些在模拟器上每项检查都通过了（24 个场景；第一行的 14 个在一次运行里跑完，约 6 分钟）：

| 场景 | 说明 |
|---|---|
| `handshake`、`capture`、`offline_replay`、`dup_ack`、`reboot`、`board`、`spawn`、`offline_spawn`、`cjk`、`back_keys`、`rec_memo`、`rec_meeting`、`rec_resume`、`rec_live_hold` | 默认的模拟器 |
| `sd_empty`、`sd_store`、`rec_reboot` | `sd_empty` 要一个刚开的模拟器：先跑它 |
| `rec_nosd` | 单独跑，加 `--sim-args "--store none"` |
| `nav`、`menu`、`lock` | v0.4.0；也加 `--sim-args "--store none"` 跑一遍，走它们的无卡分支 |
| `rec_memo_grace`、`rec_memo_handsfree` | gh #51：便条的 G0 宽限和免提的点两下 |
| `latency` | 能跑，但数字是模拟器的，不是真机的 |

`handshake` 要在任何会重启设备的场景（`reboot`、`sd_store`、`rec_reboot`）之前跑。模拟器上重启时 pty 一直开着，测试工具记不到新的握手，`handshake` 就会拿这次运行的第一个 hello 和后来某次开机的比（`same_boot_after_reopen` 失败）。评审修复之前也是这样。

别的场景 `run_hil_sim.py` 拒绝跑：

- `mic_cal` 和 `mic_verify` 会暂停真的 cpad，用 Mac 的扬声器放音。
- `rec_cold_start`（v0.3.2）也是，它带着 mic_cal 的预检。它还要一个能用 `debug.codec` 弄冷的音频 codec：模拟器的音频任务一开始就是热的，它的麦克风是 `--mic`，不是扬声器。
- `launcher_boot` 和 `migrate_*` 要一个装好或刷进去的镜像。
- `real_claude_smoke` 会开一个真的 Claude Code session。
- `sd_bulk`、`rec_rotation`、`rec_soak` 和 `soak` 还没在模拟器上试过。

### 测试

```sh
pio test -d firmware/sim                         # includes the pty, TX ring, WAV and stdin tests
pio run -d firmware/sim                          # the tests below start .pio/build/sim/cpa-sim
uv run --project daemon pytest tools/sim -q
```

`test/test_fuzz_runtime`（docs/20 §4.5）让固件的 `Runtime` 对着一个照脚本走的 daemon 和一个随机的用户跑，两个种子，各三分钟模拟时间（实际约一秒），检查：任何状态下最多 6 下回到主页、没有卡死、每条采集都到了主机、每个拉过来的段 SHA-256 都对、每个请求都有回答。它还让音频任务丢一次结束事件（docs/20 S1），检查设备 10 s 内能再录；再晚发一个结束事件，落在下一条便条的 Start 后面，它不能结束那条便条（事件带着各自录音的 gen）。更长的矩阵：`CPA_FUZZ_SEED=100 CPA_FUZZ_SEEDS=12 CPA_FUZZ_MINUTES=15 CPA_FUZZ_STORE=sd|internal|none pio test -d firmware/sim -f test_fuzz_runtime`（`fuzz-nightly.yml` 每晚跑它）。

启动 `cpa-sim` 的测试在它没构建时跳过，设了 `CPA_SIM_REQUIRED=1`（CI 会设）时失败。它比 `firmware/sim` 或 `firmware/app` 里任何一个源文件旧时，这些测试也失败，所以不会在过期的二进制上通过：再跑一次 `pio run -d firmware/sim`（它每次都重新链接，所以日期是新的）。用 `CPA_SIM_BIN` 指定的二进制只检查存不存在。

`tools/sim/tests/test_e2e_cpad.py` 是端到端检查，约 4 s，不要 Cardputer：

1. 启动 `serve`，通过 HIL rig 在上面跑真的 cpad。握手到 Ready，绑定模拟器的序列号。
2. 在模拟器键盘上打的一条笔记（G0、文字、Enter，从 stdin 来）到了 cpad：存下了（`item.received`），写进笔记文件，回了 ack，设备的 outbox 清空。
3. fake-claude 的 session 出现在看板上（`debug.state`），看板存成 PNG 并检查。

`CPA_SIM_SHOTS=DIR` 留下那张 PNG。`CPA_SIM_BIN` 让这些工具用另一个构建。

## 和真机有多像

**和真机一样的：**

- 外壳画出的那一帧的像素。每一屏都由固件自己的代码画。
- `FbCanvas` 照着 `M5Display::walk` 做：
  - 把文字切成一段段，每段用 M5GFX 的 `text_width` 量宽。
  - 字库里没有的码位画成 □：小于 U+0020、大于 U+FFFF，或者不在字库里。
  - fg ≠ bg 时填充背景格。
  - 裁剪一样。
- 字形是 efontCN_12 自己的位图，用移植过来的 M5GFX U8g2 解码器解码。[M5GFX oracle](#m5gfx-oracle) 拿它和 M5GFX 本身比，efont 测试里存着 M5GFX 画出来的结果。efontCN_12 是双宽度的：□ ● … ← 宽 6 px。native 测试的 `FakeCanvas` 当它们是 12 px，所以宽度只有在这里才对。
- 颜色就是色板精灵里存的（docs/19 §3.6）：
  - 每个像素是主题 256 色色板（`firmware/app/lib/theme`）里的一项，和设备用 `createPalette` 装进去的是同一张表。
  - 形状（填充、带预先算好的边缘颜色的圆角卡片、状态栏的小狗头、Tab 菜单下压暗的页面、Mando 的精灵、大 ● 和 Nunito 数字、锁屏卡片的暖光）由设备自己的 `theme::RasterCanvas` 画，所以和设备逐字节一样。
  - PNG 的色板是每一项的 RGB565 值，和 `pushSprite` 发出去的一样：Bg #120e0b 显示为 (16, 12, 8)。PNG 是 8 位索引的，用的就是色板自己的索引。
  - 没用到的项和四个哨兵（0x00、0x10、0xE0、0xFF）是品红：漏网的 v0.4 RGB565 常量会显出来。`test_theme_sim` 在整个 v0.4 导览、`scripts/theme_states.cpas`（每个 Mando 状态一张，docs/19 §7.2）和 `scripts/theme_motion.cpas`（动画帧）里查它们，也查没用自己颜色画的边缘，以及录音时的 ●。
- 动效（docs/19 §3.4-§3.6，S3）：两次整帧之间，运行时只推动画 tick 或滑动帧重画过的部分（`Display::present(Rect)`），大 ● 靠改 RecDot 和 RecDotEdge 两项色板来呼吸（`Display::breathe`）。`SimDisplay` 只把推过来的矩形拷到面板上，而且像设备的面板一样，记住像素被推时的颜色：这两项以当时那一步呼吸自己的颜色进面板（`theme::breath_dot` / `breath_edge`），所以一张色板就能显示每一张截图。`test_theme_sim` 在导览和动效帧里一轮一轮地检查：面板上有画过的全部内容（tick 画的没有漏推）；呼吸的那两项从不出现在 ● 的 12 x 12 矩形以外；Dim 时除了时钟什么都不动。`test_anim_cost` 断言录音时每秒推的 SPI 像素数，并打印每轮循环在主机上用的时间（`pio test -d firmware/sim -f test_anim_cost -v`）。

**和真机不一样的：**

- **背光：**
  - 熄屏和调暗的屏幕都存成全亮的像素。等级写在 `shots.json` 和总览图的说明里。
  - 用电池时 60 s 不按键就熄屏，下一个键只负责点亮。
- **音频：**
  - 音频任务是 `FakeRecPort`，同步运行，所以"保存中…"和"● 准备"阶段在一轮循环里就过去了，拍不到。
  - 电平来自合成的音调，不是麦克风和它的增益。
  - 分段的时间不是真实时间。
- **时序：** 每轮循环正好 10 ms，设备上更快。渲染规则一样（脏了就画、最多 20 fps、每秒一次），但都落在 10 ms 的刻度上。
- **没模拟的硬件：**
  - 键盘矩阵扫描（幽灵键过滤照样对注入的按键组合运行）
  - USB CDC 时序。`shots` 没有 TX 环（`tx_room` 不限）；`serve` 有设备那样的 4 KB 环和 4 行队列，主机读 pty 多快就排空多快。
  - SD 挂载和健康状态（卡是内存里一个类似 FAT 的 `FakeStore`，固定剩 64 GB）
  - LittleFS：cpafs 是内存里一个空的存储。开机迁移在它上面跑，但不画"i/n"那一屏。
  - NVS 开机计数：boot 1，每次 `debug.reboot` 加一
  - 重启原因：`poweron`，`debug.reboot` 之后是 `software`
- **设备统计是固定的假值：** 堆、栈，还有设备 id `100000000051`。
- **面板：** PNG 显示的是面板的 RGB565 值。LCD 的 gamma、亮度和视角都不模拟。
- **daemon：** 在 `shots` 里，它那一边就是脚本发的东西，加上 ping。在 `serve` 里是真的 cpad。
- **只有 `serve` 才有的：**
  - 设备重启或 `usb off` 拔线时，pty 还在。真机上端口会消失，cpad 马上收到 I/O 错误；模拟器上，cpad 要从设备的回复（重启后的 `E_NOT_READY`）或 15 s 的链路超时才知道。
  - 时间是 Mac 的：按挂钟时间每 10 ms 一轮。卡住超过 1 s（Mac 睡眠了、调试器停住了）之后，错过的循环直接跳过，不会一口气补跑。
  - 每个 `serve` 都是同一台设备：id `100000000051`，序列号 `SIM-100000000051`，不是从 Launcher 装的。

## M5GFX oracle

`oracle/oracle.sh` 把 M5GFX 本身编到主机上（用它的 SDL 后端，只用到精灵），和 `FbCanvas` 放在一起。它用两边各画一遍 `test/test_efont/oracle_cases.h` 的每个用例，新的精灵和共用的精灵上各画一次，比 240 x 135 个字节和宽度。用例覆盖：状态栏的字符串、四边的裁剪、从屏幕左边外面开始的文字、字库没有的字形（✓ ⌛、一个 U+FFFF 以上的 emoji）、控制字符、坏的 UTF-8、96 字节一段的切分，以及填充。

```sh
brew install sdl2                           # once (Linux: apt install libsdl2-dev)
firmware/sim/oracle/oracle.sh               # 0 case(s) differ, or exit 1
firmware/sim/oracle/oracle.sh --golden      # rewrite test/test_efont/oracle_golden.h
```

`oracle_golden.h` 存着 M5GFX 画出来的结果：每个用例那一帧的哈希、宽度，还有 A g 助 ● □ 的点阵行。efont 测试拿 `FbCanvas` 和它比，所以构建里没有 M5GFX，`pio test` 也能发现解码器的变化。只从 oracle 重新生成它。

efontCN_12 里没有从笔位左边开始、或者墨迹超出步进宽度的字形（有测试查每一个字形）。所以移植代码里处理这些情况的分支改变不了设备上的任何像素，也没有 oracle 用例走到它们。

## 固件改了以后

- **新页面或新按键：** 写一个脚本；模拟器什么都不用改。
- **`Runtime` 多了一个端口或构造参数：** 只改 `lib/simdev/sim_device.cpp`。
- **新的库或模块：** 一般什么都不用做。v0.4.0 的 `home` 和 `help` 不改就找到了。链接失败时，把它加进 `platformio.ini` 的 `lib_deps`。
- **钉 M5GFX 的新版本：** 用上面那行 `pio pkg install` 装新版本，跑 oracle；通过的话，在 `gen_font.py` 里钉住新的 `lgfx_efont_cn.c` sha256，再重新生成 `oracle_golden.h`。
- **模拟器从只在设备上用的文件里照抄的代码**，跟不上那边的改动：
  - `FbCanvas::walk` 照抄 `M5Display::walk`，`TxPipe` 照抄 `UsbTransport::send` 和 `pump`。
  - `kRebootDelayMs`（`hal/hal_platform.h`）和音频任务 4 KB、128 ms 的块（`hal/audio_io.h`）在 `lib/simdev` 里各抄了一份。

  色板不再照抄：docs/19 以后两边都用 `lib/theme`。
