[English](21-settings.md) · **中文**

# 21 · 设置：WiFi 和语言（中文 / English）

> 角色：设置、多语言和 WiFi 设计（Lead 汇总）。日期 2026-10-22，同日按评审修订（18 条意见的处理见文末）。对象是 `integrate/v041`（`e4c2c68`：v0.4.0 + Mando 主题 docs/19 + 小屏交互 docs/20 + 浸泡修复）。读了代码、文档和字体数据；用脚本盘点界面文字、量宽；编了 4 个只编译、不刷机的 WiFi 探针固件。**没有改产品代码，没有接设备，没有开串口。**
> 需求（2026-10-22）：加一个系统设置页，能看到 WiFi 和连接相关的选项，并且能把整个系统切换成英文。
> 标记沿用 docs/13：**[源]** 读过的代码或文档 · **[测]** 实测（编译、脚本） · **[算]** 推算，或用脚本量出来的 · **[推]** 推断 · **[设]** 设计假设，可以改 · **[待]** 要模拟器或真机验证 · **👤** 需要你。
> 宽度按 efontCN_12 的真实字模量：ASCII 6 px，汉字和全角标点 12 px，`● … ★ ↑ ← → ◎ ◇ ○ · ×` 这 11 个符号 6 px。**`▸ ✓ ⌫ —` 字库里没有**，下面示意图里的 `▸` 只表示"选中行"。
> 内存一律写 **KiB**（1024 B），和 `test_runtime_budget` 的 `150u * 1024u` 一致；镜像写 MB（10⁶ B）。
> **证据随本文提交**，在 `docs/gates/settings-probes/`（怎么重跑见文末证据索引）：
> - `i18n/`：`inventory.py`（盘点）、`efont.py`（量宽，模拟器 `firmware/sim/lib/simfb/efont.cpp` 的 Python 移植）、`en_draft.py`（357 条英文草稿）、`measure.py`、`width.py`、`hints.py`、`strings.txt`（本文新字串）；
> - `wifi/`：4 个 WiFi 探针（`platformio.ini` 里写了重跑命令和结果）和 `mapsum.py`。工具链和产品一样（espressif32@6.10.0、Arduino 2.0.17、IDF 4.4.7、同样的 USB CDC 选项）。

## 0. 一页摘要

**一句话**：加一个"设置"。**语言**：整台设备换成英文，按三下、马上生效、断电后还记得。**WiFi**：设置里**看得到连接**（今天的"连接"就是 USB 线和哪台 Mac），并说清楚 WiFi 的情况；这一版不把无线编进固件，因为只要编进去，哪怕一直关着，开机可用堆就少约 31 KiB，跌破 150 KiB 的稳定性底线。

**你会看到什么**（Opt+M）：

```
[头] ↑0                         周三 09:41 94%
(设置) (设备)
▸WiFi              无 · 只用 USB 线
 语言 Language     中文
 ─────────────────────────────────────
 连接   USB 线 · 工作 MBP           在线
 同步   3 分钟前 · 待传 ↑0 录2

[;.]选 [Enter]改 [`]返回 [Tab]菜单
```

- Opt+M 改叫"设置"，先打开**设置**页；今天的诊断信息在右边一页（按 `/`）。不加新键。
- 光标每次都停在 **WiFi 行**，那一行按 Enter 只是看说明，误按也没事。
- **换语言**：Opt+M、`.`、Enter，三下。马上生效，不用重启，断电后还记得。"语言 Language"这一行两种语言下写法一样，切错了照样找得回来。
- **WiFi** 一行写"无 · 只用 USB 线"。按进去说清楚：这个固件不带 WiFi；数据只走 USB 线到你的 Mac；想用 WiFi，关开电源进 Launcher，切到出厂固件；还提醒一句"出厂固件会把 WiFi 密码明文打到串口"。设备不替你跳进 Launcher（§4.4）。
- **连接**、**同步**两行：USB 线连着哪台 Mac、在不在线、上次同步是什么时候、还有几条没传。

**WiFi 为什么这一版不做** [测][算]：

- 用同一套工具链编探针：只要把 WiFi 链接进来（哪怕从不打开），开机可用堆就少 31,704 B（只扫描）到 34,336 B（能连网）。v0.4.0 实测空闲堆 182,944 B（178.7 KiB），离 150 KiB 的底线只剩 28.7 KiB，减完是 **147.7 / 145.1 KiB，跌破底线**。
- 真打开时还要再占多少，在 S3 和 IDF 4.4 上**没人量过**；照别的平台猜是 35–50 KiB [推]。要先在真机上量（§5.7 G-WIFI 第 4 条）。
- 工具链 IDF 4.4 已经 EOL，没有安全修复；ADR 0001 定过加无线之前先换工具链。

**以后怎么做**（§5）：

- **W1 只听不连：不排期。** 设备自己扫描附近网络，唯一的用处是"没插线时提醒你换档案"（docs/18）。先看 Mac 端的信号够不够用（设备 0 B）；真要做，就是 docs/18 的 P8-W，先过 G-WIFI，而且只在没连着 Mac、你看着 WiFi 页的时候扫。
- **W2 连网：不排期。** 这里只定死几条不变式（§5.6）：密码在 Mac 上输、走 USB 线、不进日志；录音和传录音时 WiFi 一律关着；设备不自己重启。

**语言怎么做**（§6）：

- 固件里有 373 个中文字面量，35 个文件，按文件去重 357 条，全局去重约 300 条，没有字符串表 [算]。
- 每条界面文字写成一对 `constexpr Txt{"中文", "English"}`，放在用它的代码旁边。缺一种语言、英文里混进汉字、`%` 占位符对不上、超出它那一类的宽度上限，都**编译不过**。
- 英文平均比中文宽（中位数 ×1.20）。用一份尽量短的英文草稿量下来，有 23 条超宽，全都能改短放下（§6.9）。顺手发现两处中文本身就超出的地方。
- Mac 端：协议几乎不带界面文字，只有 cpad 拼的"会议 42m"要改成让设备自己写；Mac 上笔记里的标签默认继续用中文。

**代价** [算]：flash 约 +13 KB，静态 RAM 约 +27 B（Mac 名字 25 B，语言和"待写"2 B），堆 0，NVS 1 个键。工作量约 9–11 个工作日（§8、§10）。

**👤 要你定的 6 件事**（§13）都有默认值，不回复就按默认做。影响最大的是：Opt+M 改叫"设置"、先开设置页（第 1 条）；英文词表（第 3 条）；WiFi 你想用来做什么（第 5 条）。

## 1. 你的要求落在哪

| 需求 | 落在哪 | 这一轮 |
|---|---|---|
| 加一个系统设置页 | Opt+M 改叫"设置"，第一页是设置（§4） | 做 |
| 能看到 WiFi 和连接相关的选项 | 设置页的 WiFi 行和 WiFi 说明页（W0）；"连接""同步"两行（§4.2、§4.4） | 做：看得到连接和 WiFi 的情况，告诉你怎么用出厂固件的 WiFi。扫描（W1）、连网（W2）不排期，等 👤 第 5 条 |
| 能把整个系统切换成英文 | 字符串表 + 语言设置（§6） | 做：设备上所有界面文字。`cpa` 命令行本来就是英文；Mac 上笔记的标签见 👤 第 4 条 |

## 2. 起点

### 2.1 今天的系统页

- Opt+M 打开 `system` 模块：只读，最多 9 行，每行 12 px，正好占满 111 px 的内容区（y=12 到 123）[源 system_logic.h:19 `kSystemLines = 9`；canvas.h:113-116]。
- 内容是 8 行固定的（固件、dev、启动、存储、outbox、堆、栈余、电池）+ 录音行 + 警告行；页面满了时录音行让给警告行 [源 system_logic.cpp:132-153]。`add()` 超过 9 行就**悄悄丢掉**。
- 第 0 行的标签位置画标题"系统"，值是固件版本。值列起点 `kValueX = 46`，是按最宽的标签 "outbox" 定的 [源 system_module.cpp:17、57-62]。
- `onKey` 一律返回 false，`` ` `` 回到上一页 [源 system_module.cpp:38]。
- docs/18 §5.6 计划在 v0.8 给它加"档案"标签，用 `,` `/` 换；列表层本来就有这两个键 [源 keymap.cpp]。
- 主页格子是"M 系统"，Tab 菜单"去哪"页那一行也叫"系统" [源 home_logic.h:31、action_help.cpp:23]。

### 2.2 界面文字

- 固件（`lib`、`modules`、`src`，去掉注释、还原 `\x` 转义、跳过生成的 `lib/theme/gen/`）：**373 个中文字面量，35 个文件，按文件去重 357 条，全局去重 299 条**，UTF-8 约 5.3 KB [算 `i18n/inventory.py`]。
- 约 120 个屏幕上的 ASCII 字面量（键名、单位、格式、命令）两种语言共用，不进字符串表。
- 固件以外：测试 46 个文件 641 行、模拟器脚本 123 行、HIL 工具 52 行、cpad 27 行带中文；`cpa` 命令行没有中文。细节见 §6.1。

### 2.3 今天的 WiFi、连接和预算

- 固件里没有任何 WiFi 或 BLE 代码，只走 USB [源 docs/18 §6]。
- 工具链 Arduino 2.0.17 / IDF 4.4.7，已经 EOL（Espressif AR2024-008）。ADR 0001：加无线之前重新评估，目标是方案 C（Arduino ≥3.3.9，基于 IDF 5.5），前提是 #18621（录音全 0）已修好 [源 adr/0001-toolchain.md:77]。
- 预算 [源 docs/releases/v0.4.0.md:83-88；docs/19 §10；本轮任务给的 integrate/v041 数字]：
  - 空闲堆：v0.4.0 实测 182,944 B（178.7 KiB），最大块 163,828 B（160.0 KiB）；主题后估少约 0.8 KiB；
  - 底线（G-A6，单位 KiB [源 test_runtime_budget/test_main.cpp:377]）：空闲 ≥150 KiB / 最大块 ≥64 KiB，录音时 ≥96 / ≥32 KiB，传录音时 ≥128 KiB [源 docs/12:455]；
  - **整个 app 在录音和传录音时的堆还没在真机上量过**：只量过精简的 `test_rec_budget` 和整个 app 的空闲；测试版的计数已经加好，等真机（docs/07 §6.7 第 6 条，仍开着）[源 docs/07:624-628]；
  - 静态 RAM 约 66.7 kB / 98,304 B；app 镜像约 1.03 / 2.5 MB，1.75 MiB 是警告线（Launcher 下要和 Bruce 放得下）[源 scripts/check_budget.py]。
- 连接：Mac 每次 USB 连上都发 `time.set` 对时 [源 docs/05 §5.10]；时钟只是内存里的一个偏移 `state().time.offset_ms`，重启就没了，要等下一次 `time.set` [源 runtime_rec.cpp:140]。`hello` 里有 `host_label`（Mac 的名字，最长 24 字节）[源 messages.h:65]，但设备解码后**没存**，只存了 `host_app` [源 runtime_link.cpp:51]；"控制"页显示"Mac 在线 · cpad/x" [源 menu_model.cpp `mac_text`]。
- Launcher [源 docs/gates/launcher.md]：
  - 上电复位（电源开关、RST 键）一定进 Launcher；深睡唤醒也进，但 beta 版里 `launcher/DDLB=1` 时直接回到固件（第 78 行）；`esp_restart()`、panic、USB 复位直接回到当前固件（第 3 节）。
  - R8 规定"不用深睡；重启一律用 `esp_restart()`"；系统页的"回到 Launcher"只是一个可选项，要先过 G-L5（第 98、166、231、260 行）。
  - 出厂固件有 WiFi，可以从 Launcher 切过去（#12）；它会把 WiFi 密码明文打到串口 [源 docs/01:178]。
  - NVS 只有一个，Launcher 下所有固件共用（Launcher 自己的 `l_wifi`、Bruce 等）；core 2.0.17 开机时遇到 NVS 没有空页（`NO_FREE_PAGES`）会**擦掉整个 NVS**（R4，第 162 行）。
- 只在本地，不上云（ADR 0004）。05 §8 定了"始终不做：默认开启 flash 加密或 Secure Boot"。

## 3. 原则

1. **方便和稳定优先**（docs/20）：换语言三下、马上生效；设置不能给录音、存储、USB 链路带来任何风险。
2. **永远找得回来**：语言那一行在两种语言下都写"语言 Language"；光标默认不停在会改东西的行上；打开设置页后最多三下就能换回来。
3. **WiFi 永远让路**：录音、传录音时 WiFi 一律不扫、不连；WiFi 只在空闲时开。按下录音，先开麦克风，再关 WiFi；不会因为 WiFi 拒绝录音。
4. **能在 Mac 上做的，不在设备上打字**（docs/17 "选，不打字"）：密码在 Mac 上输，设备上只做选择。
5. **秘密只走一条路**：密码只在 `cpa` → cpad → USB → 设备 NVS 这一条路上出现一次，不进日志、不回传、不进笔记、不进 outbox。
6. **界面文字和数据分开**：只翻译设备自己的文字；从 Mac 来的项目名、session 名、录音标题、Mac 的名字、网络名原样显示。
7. **漏翻、错位、超宽在编译时抓住**，不等到真机上看见 □ 或"…"。
8. **不加新键**：Opt+M、`;` `.`、`,` `/`、Enter、`` ` ``、Tab 都是现成的。
9. **设备不自己重启，也不替你进 Launcher**：自动重启会丢掉锁状态和时钟（§5.4）；进 Launcher 只用电源开关（launcher.md R8）。

## 4. 设置应用

### 4.1 信息结构

```
Opt+M  设置
├─ (设置)                                ← 先打开这一页（👤 第 1 条）
│   ├─ WiFi            无 · 只用 USB 线    ← 光标每次停在这里；Enter：WiFi 说明页（W0）
│   ├─ 语言 Language   中文 / English     Enter：直接切换
│   ├─ 连接            USB 线 · Mac 名 · 在线    只看
│   └─ 同步            3 分钟前 · 待传 ↑0 录2     只看
├─ (设备)                                ← 今天的诊断信息，按 /
└─ (档案)                                ← v0.8，docs/18 §5.6
```

- 模块 id 仍是 `system`：HIL 按 id 找页面（`debug.state.mod`），不改 [源 tools/hil]。
- 名字统一成"设置"：主页格子"M 设置"、Tab 菜单"去哪"页那一行"设置"、第一个标签"设置"。docs/20 U22 "一个动作一个名字"。
- 只放能在设备上改、而且改了有用的东西。别的设置不往这里塞（§12）。

### 4.2 设置页

示意图的零件都是 docs/19 已有的：

| 记号 | 是什么 | 画法 |
|---|---|---|
| `(设置)` | 标签胶囊 | 和项目页的 `Sessions(N)` `队列(N)` 一样：当前页 Accent，另一页 Neutral，高 12，间距 4 [源 board_module.cpp:274-281] |
| `▸` 行 | 选中行 | 14 px 的 Select 胶囊，左右缩进 8（`kListX`），和列表行一样 |
| 标签 / 值 | 行 | 标签 Ink3，值 Ink；事实行没有选中态 |
| `───` | 分隔线 | 1 px `Line`，和系统页一样 |
| `[Enter]` | 键帽 | 提示栏的键帽画法（docs/19 §4.2） |

中文：

```
[头] ↑0                         周三 09:41 94%
(设置) (设备)
▸WiFi              无 · 只用 USB 线
 语言 Language     中文
 ─────────────────────────────────────
 连接   USB 线 · 工作 MBP           在线
 同步   3 分钟前 · 待传 ↑0 录2

[;.]选 [Enter]改 [`]返回 [Tab]菜单
```

英文：

```
[头] ↑0                          Wed 09:41 94%
(Settings) (Device)
▸WiFi              None · USB only
 语言 Language     English
 ─────────────────────────────────────
 Link   USB · 工作 MBP              online
 Sync   3 min ago · to sync ↑0 R2

[;.]pick [Enter]change [`]back [Tab]menu
```

| 元素 | 位置 | 说明 |
|---|---|---|
| 标签胶囊 | **y=12**，x=4 起 | 三个标签页（设置、设备、以后的档案）都用这个 y。没有另外的标题：当前那个胶囊就是标题 |
| 可改的行 | y = 26 + 14k，标签 x=8，值 x=100 | "语言 Language" 78 px；值最宽"无 · 只用 USB 线"96 px，到 x=196 [算 `i18n/width.py`] |
| 分隔线 | y=55 | |
| 事实行 | y = 58 + 12k，标签 x=8，值 x=44，右边界 x=236 | 见下面"连接"和"同步" |
| 提示栏 | y=123 | 中文 196 px、英文 232 px，都在第一档间距里 [算 `i18n/hints.py`] |

- **连接**一行分三段：
  - 左边固定的"USB 线 · "（54 px）/"USB · "（36 px）；
  - 右边钉住状态词"在线 / 离线"（24 px）/"online / offline"（36 / 42 px），右对齐到 x=236；
  - 中间是 Mac 的名字，用 `draw_fit` 放进剩下的地方：两种语言都是 108 px（9 个汉字或 18 个 ASCII），放不下就截成"…"。`host_label` 最长 24 字节（144 px），所以一定要截 [算]。
  - 名字取这次开机最后一次握手的 `host_label`；没有就写 `host_app`（`cpad/0.4.1`）；都没有写 `--`。拔了线照样显示上一台的名字，状态词变"离线"。
  - "上次 3 分钟前"不放这一行，下面的"同步"行已经写了。
- **同步**一行：上次收到 Mac 消息到现在多久，加上和状态栏同一套计数（↑N 待传、录N 待同步的录音）。最宽英文"59 min ago · to sync ↑99 R99"168 px，到 x=212 [算]。
- 设备要存 Mac 的名字：`SysInfo` 加一个 `HostLabel host_label`（25 B，静态），`runtime_link.cpp` 握手时连 `host_app` 一起存。
- 光标只停在**可改的行**上（WiFi、语言），事实行跳过。
- **每次打开设置页，光标都回到 WiFi 行**：WiFi 行按 Enter 只是看说明，什么都不改。这是 docs/18 §5.9 "光标预先停在最安全的那一行"，和 docs/20 U5、U18 修的是同一类误按。切换语言以后光标留在语言行（提示里的"再按 Enter"才对得上），离开再进来又回到 WiFi 行。
- 以后的可改项加在语言下面、分隔线上面。

### 4.3 语言

- **在语言行上按 Enter，直接在中文和 English 之间切换**（👤 第 2 条）。只有两种语言，弹选择单要多按两下。
- 切完马上整屏重画，然后用**两种文字**弹一条提示，告诉你怎么换回去：
  - 切到英文："English · Enter again: 中文"（162 px）；
  - 切回中文："中文 · 再按 Enter 换回 English"（180 px）[算]。
  - 这两条都是 `Txt` 的格式串 `"%s · Enter again: %s"` / `"%s · 再按 Enter 换回 %s"`，语言名从 `kLangNames` 填进去，所以英文那一半里没有汉字（§6.3 第 2 条）。
- 语言名永远用它自己的文字写："中文"、"English"。`kLangNames` 是一对普通的 `const char*`，不是 `Txt`。
- "语言 Language"这一行的标签是 `Txt{"语言", "Language"}` 的**两半一起画**（`both()`），不是一条混着写的字。
- 存进 NVS（§6.8）。录音中先不写 NVS，等录音结束再写；语言照样马上换 [设]。
- NVS 写失败：弹"没存上：重启后会回到中文" / "Not saved: 中文 after a restart"（144 / 186 px，语言名同样从 `kLangNames` 填）。这次开机里照样用新语言。

### 4.4 WiFi 页（W0，这一版）

只有说明，没有可以按的东西：

```
[头] ↑0                         周三 09:41 94%
WiFi                                     (无)
现在   这个固件不带 WiFi
数据   只用 USB 线连你的 Mac，不上网
想用   出厂固件有 WiFi：
       关开电源进 Launcher 切过去
注意   它会把 WiFi 密码明文打到串口
以后   还没排期
[`]返回 [Tab]菜单
```

```
WiFi                                    (None)
Now    No WiFi in this firmware
Data   USB to your Mac, never online
Want   Factory firmware has WiFi:
       power-cycle into the Launcher
Note   Prints WiFi passwords to serial
Later  not planned yet
[`]back [Tab]menu
```

- 标题 `WiFi` 假粗体，右边一个 Neutral 胶囊写现在的状态（"无" / "None"）。值列：中文 x=36，英文 x=42（最宽的英文标签 "Later" 30 px）；最宽一行英文"Prints WiFi passwords to serial"186 px，到 x=228 [算]。
- 不是经由 Launcher 启动的（`SysInfo::launcher` 为假，整片刷写的设备）："想用"的第二行换成"先装 Launcher（见 README）" / "Needs the Launcher (README)"（156 / 162 px）。
- "注意"那一行是给你的提醒：出厂固件的 `hal.cpp` 会把 WiFi 账号密码打到串口日志里 [源 docs/01:178]。README 的"想用 WiFi 工具"一节写得更细。
- **为什么不做"去 Launcher…"一键跳过去**（评审第 2 条）：
  - launcher.md R8 规定不用深睡、重启一律 `esp_restart()`；系统页的"回到 Launcher"要先过 G-L5。
  - beta 版的 `launcher/DDLB=1` 会让深睡唤醒直接回到我们的固件，看上去像无缘无故重启了一次。
  - 只有出厂固件已经装进 Launcher 才有用。
  - 电源开关拨一下就是一个动作，比菜单里按 5 下还省事。
  - 它把你送去一个会明文打出 WiFi 密码的固件，这件事说明页上必须写出来，一键跳转反而把它藏起来了。
- outbox 和录音都在 SD 卡（或 cpafs）上，用完出厂固件再切回来，照常同步，不丢 [源 docs/09 §3]。

### 4.5 设备标签

内容就是今天的诊断信息。标签胶囊占了最上面 12 px，下面只剩 **8 行**（y=24 起，每行 12 px，分隔线前后各 1 px：3 × 12 + 3 + 5 × 12 = 99 px，到 y=123 正好）。今天是 8 行固定 + 录音行 + 警告行，所以要并掉一些，**警告行一定要有位置**：

| 行 | 中文 | 英文 | 值宽（中 / 英） |
|---|---|---|---|
| 1 | 固件 cpa-fw/0.4.1 rst poweron | FW … | 144 / 144 |
| 2 | dev a1b2c3d4e5f6 boot ABCDEFG-HJKMN | dev … | 186 / 186 |
| 3 | 启动 Launcher daemon cpad/0.4.1 | Boot Launcher daemon … | 156 / 156 |
| — | 分隔线 | | |
| 4 | 存储 SD 476G 剩 412G | Store SD 476G free 412G | 90 / 102 |
| 5 | outbox 待发 0 在途 0 坏 0 | outbox pending 0 out 0 bad 0 | ≤150 / ≤168 |
| 6 | 堆 180K 最小 170K 块 160K | Heap 180K min 170K blk 160K | 132 / 132 |
| 7 | 电池 4012mV 栈余 3.2K | Bat 4012mV stack 3.2K | 96 / 102 |
| 8 | 警告行（有才画） | | 一整句，`draw_fit` |

- **录音行并进存储行**："录N 待同步"本来就在设置页的"同步"行和状态栏上，存储行只留卡的大小和剩余空间。
- **这次开机迁移过**（已迁移 / 迁移失败不为 0）时，存储行的"剩 412G"换成迁移结果："SD 476G 已迁移 3 迁移失败 1"（162 px）/ "SD 476G moved 3 failed 1"（144 px）；内部存储最坏"内部 1.5M 已迁移 12 迁移失败 12"186 px / "Flash 1.5M moved 12 failed 12"174 px [算]。迁移失败照旧让这一行变 Warn 色。
- **栈余并进电池行**；"幽灵 N 重置 N"（键盘的两个计数）不再单独占一行：不为 0 时，作为最后一段加进警告行（"键盘 幽灵 3 重置 1"108 px）。它们今天本来就是让那一行变 Warn 色的东西。
- 第 0 行"系统 <固件版本>"改回普通的"固件 <固件版本>"：当前的标签胶囊就是标题。
- **英文标签都不超过 36 px**（FW、dev、Boot、Store、outbox、Heap、Bat），值列仍是 x=46，两种语言一样。第 2 行的值有 186 px，x=46 时正好到 232；如果英文标签用 "Firmware"（48 px），值列要挪到 x=58，这一行就出界了。
- 值列从 x=46 到 236，有 190 px；上表每一行都放得下 [算 `i18n/width.py`]。
- `test_mod_system` 加一条：所有警告来源都打开、录音可用、迁移计数不为 0 时，设备页不超过 8 行，**最后一行是警告行**（§9.2）。

### 4.6 按键

| 键 | 设置页 | 设备页 | WiFi 页 |
|---|---|---|---|
| `;` `.`（或 Fn+; Fn+.） | 在 WiFi、语言两行之间上下选 | — | — |
| Enter | WiFi 行：进 WiFi 页；语言行：切换 | — | — |
| `,` `/`（或 Fn+, Fn+/） | 换标签 | 换标签 | — |
| `` ` `` / 退格 | 返回上一页 | 返回上一页 | 回到设置页 |
| Tab | 菜单 | 菜单 | 菜单 |

- 系统模块开始处理按键（今天 `onKey` 一律返回 false）。
- 提示栏写上返回键（docs/20 B5 给看板总览补的也是它）：设置页"[;.]选 [Enter]改 [`]返回 [Tab]菜单"，设备页"[,/]换页 [`]返回 [Tab]菜单"（151 px），WiFi 页"[`]返回 [Tab]菜单"（100 px）。`,` `/` 不写进设置页的提示栏：标签胶囊就在屏幕上，Tab 菜单里也有。
- `help()` 按页返回 Tab 菜单"本页"的行：上移、下移、改这一行、换标签、返回、主页。Enter 那一行重放 Enter，所以菜单里也能切语言。
- 改语言要经过外壳：`ModuleContext` 加一个有默认实现的 `setLanguage(Lang)`（默认返回 false）；模块不直接碰 NVS [设]。

**按几下**：

| 要做的 | 按键 | 次数 |
|---|---|---|
| 看连接、WiFi 状态、当前语言 | Opt+M | 1 |
| 看 WiFi 说明 | Opt+M、Enter | 2 |
| 换语言（不管现在是哪种） | Opt+M、`.`、Enter | 3 |
| 看诊断（今天 1 下） | Opt+M、`/` | 2 |

## 5. WiFi

### 5.1 在这个产品里做什么、不做什么

| 用途 | 结论 | 理由 |
|---|---|---|
| 看连接状况 | **这一版做（W0），不用无线** | 你的"连接"今天就是 USB 线和 Mac。设置页直接显示是哪台 Mac、在不在线、上次同步、待传几条 |
| 用出厂固件里的 WiFi 工具 | **这一版写清楚怎么用**：关开电源进 Launcher | 今天就能用；设备不替你跳（§4.4） |
| 认地方（家 / 公司），给 docs/18 的档案提建议 | **不排期。先评估 Mac 端的信号**，再决定要不要 W1 | 见下面"认地方，先看 Mac" |
| 看附近有哪些网络 | 跟 W1 一起 | 单独没什么用 |
| 对时（NTP） | 不做 | 每次插 USB，Mac 都发 `time.set`；`wall_ms` 允许为空；NTP 要连公网，违背"只在本地"。真要做，只用 DHCP 给的局域网 NTP |
| 不插线也同步到 Mac | 不做，除非你说要（👤 第 5 条） | 违背"USB 优先、Mac 是中心"；要配对和加密（05 §8 的 Noise）；录音和传录音时 WiFi 又必须关着（原则 3）；几周的工作量 |
| 上网、云同步、OTA 升级 | 永远不做 | ADR 0004；升级走 USB 或 Launcher |

**认地方，先看 Mac**（评审第 13 条）：

- docs/18 的形态 A（家、公司各一台 Mac）：插哪台 Mac 已经决定了档案，用不着 WiFi。
- 形态 B（一台笔记本带来带去）：Mac 自己知道连着哪个网络。默认网关的 MAC 地址不要任何权限；CoreWLAN 能拿到 SSID，但 macOS 15 起要定位权限 [源 Jamf 社区、Apple 开发者论坛]。docs/18 §7.3 本来就把"Mac 的 WiFi"列成 P8-B1 的信号来源。**设备上 0 B。**
- 设备自己扫描只剩一个用处：**没插线的时候**，提醒你"看起来在家，要切到 家 吗？"（docs/18 T2"忘了切换"）。这个提醒值不值 31 KiB 内存、一次工具链迁移，要你在 👤 第 5 条里说。

另外：设备只有 2.4 GHz；学校、公司常见的 eduroam 这类企业网（WPA2-Enterprise）和要网页登录的网络，W2 都不支持。W1 只听，这些网络照样能"认出来"。

### 5.2 三级

| 级 | 内容 | 什么时候 | 要密码 |
|---|---|---|---|
| **W0** | WiFi 行、WiFi 说明页 | 这一版（阶段 A） | 否 |
| **W1** | 附近网络：按需被动扫描，用完彻底关掉。就是 docs/18 的 P8-W | **不排期**：先做 Mac 端评估（W-M）；你同意以后先过 G-WIFI（§5.7）。不和 v0.8 绑在一起（docs/18 §6、§15 第 8 条：v0.8 不做 WiFi） | 否 |
| **W2** | 连网 | **不排期**：只有 👤 第 5 条选了一个要连网的用途，才另写设计，并且满足 §5.6 的不变式 | 是 |

### 5.3 内存：编进去就少约 31 KiB

4 个探针（`docs/gates/settings-probes/wifi`），只编译，统计口径和 `scripts/check_budget.py` 一致；开机可用堆按 `_heap_start` 的位移算（S3 的 IRAM 和 DRAM 共用一块 SRAM，IRAM 和静态区变大，堆的起点就后移）[测 xtensa nm]：

| 探针（和空白 Arduino 比） | 镜像 | 静态 RAM | IRAM | 开机可用堆 |
|---|---|---|---|---|
| 只扫描：直接调 IDF，被动扫描，不建 netif | +358,064 B | +22,772 B | +8,924 B | **−31,704 B（−31.0 KiB）** |
| STA：扫描 + 连接 + SNTP，直接调 IDF | +386,944 B | +25,408 B | +8,924 B | **−34,336 B（−33.5 KiB）** |
| 同样功能，用 Arduino `WiFi.h` | +426,800 B | +25,920 B | +8,924 B | −34,848 B（−34.0 KiB） |

- 涨在哪 [测 `mapsum.py`]：只扫描时 net80211 的 bss 9.7 KB、phy 约 8.2 KB（IRAM 5.1 + data 3.1）、pp 3.5 KB、lwIP 1.7 KB、wpa_supplicant 1.4 KB；STA 时 lwIP 涨到 4.4 KB。
- **编译期就定了，压不下来**：Arduino 2.0.17 用预编译的 SDK，`WIFI_IRAM_OPT` 本来就关着 [源 sdkconfig]。要再压只能自己编 SDK，也就是换工具链。

放到今天的固件上 [算]：

| 项 | 今天 | 链接了 WiFi、一直关着（W1 / STA） | 上限 |
|---|---|---|---|
| 静态 RAM | 约 66,700 B | 约 89,500 / 92,100 B | 98,304 B |
| 空闲堆 | 178.7 KiB（182,944 B） | **147.7 / 145.1 KiB**（主题后再少约 0.8） | **≥150 KiB：跌破** |
| 最大块 | 160.0 KiB（163,828 B） | 最多 129.0 / 126.5 KiB | ≥64 KiB |
| 镜像 | 约 1.03 MB | 约 1.39 / 1.42 MB | 1.75 MiB 警告线以下 |

**打开以后还要再占多少：没量过** [推]：

- 能找到的数字都不是这块芯片、这个工具链：Rust esp-radio 的 STA 47–57 KB；arduino-esp32 #8033 在 ESP32（不是 S3）上 `WiFi.mode(STA)` 少了 45.6 KB；Espressif 的 S3 无 PSRAM 配置表，"Default"和"Minimum"两档差约 90 KB。
- 按最小配置猜，开着时再占 35–50 KiB：W1 扫描时空闲堆约 98–113 KiB；W2 连着时还有常驻的 lwIP（下面），约 83–104 KiB，**可能低于录音的 96 KiB**。这就是原则 3 的来由。
- 这些都是猜的。G-WIFI 第 4 条用真机探针量：链接未开、扫描中、关掉后三种状态，初始化—扫描—关掉 50 轮。量出来以前，本文不定任何新的底线。
- 扫描结果本身很小：一条 `wifi_ap_record_t` 80 B [测 sizeof]，`esp_wifi_scan_get_ap_records` 时驱动一次释放 [源]。

**关掉以后能还回来多少** [源][推]：

- `esp_wifi_stop()` + `esp_wifi_deinit()` 释放驱动的缓冲和 WiFi 任务 [源 esp_wifi.h:269-278]。
- IDF 4.4 不支持 `esp_netif_deinit`：lwIP 的 tcpip 任务、默认事件循环和 netif 一旦建了就一直在，估 6–12 KiB [源 esp_netif.h:64-73；推]。W1 不建 netif，所以只有 W2 才有这一块。
- 维护者在 #8033 的说法：WiFi"卸不掉"，办法是重启 [源]。论坛有人报每开关一轮漏 32–48 B [源 仅标题]。
- **碎片才是主要风险**：WiFi 开着时，别的分配可能落进它的空洞里，关掉后最大块回不来。

**底线怎么办**（G-WIFI 第 3 条，先量再定）：

1. **省回来**：换工具链（IDF 5.x 能自己配 WiFi 和 lwIP 的选项）后重量；或者在别处省出约 34 KiB。
2. **改底线**：先要有整个 app 在录音和传录音时的实测堆（docs/07 §6.7 第 6 条做完、G-A6 补齐），才能讨论"链接了 WiFi 的固件"能不能用别的空闲底线。现在不给数字。
3. **扫描时的会话底线**：开之前查空闲堆和最大块，不够就不开。数值等第 4 条量完再定。

### 5.4 W1 什么时候开、什么时候关（以后真做时）

```
       在 WiFi 页按"扫描"（链路不是 Ready、不在录音和传录音、屏幕亮、堆够）
  关（未初始化）──────────────────────────────────────► 扫描中（≤5 s）
    ▲    ▲                                                 │
    │    └── 扫完 / 离开 WiFi 页 / 熄屏 / 锁屏 ─────────────┤
    │                                                      │
    └── 按住 G0、Opt+R：先开始录，同时关；链路变成 Ready：关 ┘
            关掉后量堆：没回来 → 这次开机不再扫描（不重启）
```

1. **默认关**，`esp_wifi_init` 都不调。关着的代价只有 §5.3 那 31 KiB 静态的。
2. **只在这些条件都满足时开**：你看着 WiFi 页、按了扫描；屏幕亮着；**链路不是 Ready**（没连着 cpad）；不在录音、不在传录音；电量不低于 17%（录音自动停的那条线）；空闲堆和最大块够（§5.3 第 3 条）。
   - "链路不是 Ready"一条就排除了扫描中途来 `file.*` 拉录音、扫描和 USB 收发抢 PHY、传录音时堆被碎片化这三种情况；而且 W1 本来就是给没插线的时候用的。插着 Mac 时页面写"没插 Mac 时才扫描" / "Unplug to scan"（102 / 84 px）。
3. **一次不超过 5 秒**，扫完立刻 `esp_wifi_stop` + `esp_wifi_deinit`。被动扫描每信道 120 ms，13 个信道约 1.6 s；360 ms 一档约 4.7 s（信标间隔 102.4 ms，偶尔漏）[源 esp_wifi.h:413-418；算]。
4. **录音优先，而且不让开口变慢**：扫描中按住 G0 或 Opt+R，**先开始录**，同时停扫描、关 WiFi。环形缓冲开机就分配好（`CPA_REC_ALLOC_AT_START=0`），能顶 768 ms；关 WiFi 目标 ≤200 ms，在这之内 [源 audio_task.h:38、audio_io.h:32-36；待 G-WIFI 第 7 条]。不会因为 WiFi 拒绝录音，也不让便条的开头比今天丢得更多（docs/20 U8、D8）。
5. **链路变成 Ready**（插上 Mac、cpad 连上）：立刻关。
6. **关掉以后量一次**：空闲堆和最大块回到开之前 ±1 KiB 以内才算干净。**没回来也不重启**：这次开机不再扫描，WiFi 行写"关了，重启后再用" / "Off until restart"（96 / 102 px），等下一次自然开机（你关机、没电）。`debug.state.wifi` 记一笔。
   - 为什么不自动重启：锁状态开机默认是"没锁"[源 lock.h:27、app_state.h:115]，揣在兜里自动锁上的设备会亮着屏、没锁着醒来，等于撤掉 docs/20 的 U2/D2；时钟只是内存里的偏移，重启后到下一次插 Mac 之前，记的笔记和录音都没有 `wall_ms`，而 W1 恰恰是给没插线的时候用的。
7. **W1 不建 netif、不起 lwIP**：起了就卸不掉。
8. **不用 Arduino `WiFi.h`** [源 WiFiGeneric.cpp:658-707、763]：它总会多建一个 AP netif 和 4 KB 栈的 `arduino_events` 任务；默认 `persistent=true`，会把 SSID 和密码明文写进**共用 NVS** 的 `nvs.net80211`；默认动态 RX 32 个，网络突发时最多约 51 KB。直接调 IDF，`wifi_init_config_t` 用最小配置：静态 RX 4、动态 RX 8、动态 TX 8，关掉 AMPDU（静态 RX 少于 6 时必须关 [源 IDF 4.4.7 S3 WiFi 指南]）和 CSI，`nvs_enable=0`，再 `esp_wifi_set_storage(WIFI_STORAGE_RAM)`。

**和别的东西一起用** [源][待]：

- CPU：WiFi 任务固定在核 0、优先级 23；我们的 `rec_task` 也在核 0，优先级 1；环形缓冲 6×2048 样本只能顶 768 ms [源 sdkconfig、audio_task.h:43-45、audio_io.h:32-36]。所以第 2、4 条：录音时不扫，按下录音先录再关。
- 底噪：WiFi 发射突发会拉动电源，模拟麦克风前级可能录进去 [源 atomic14、WLED]。被动扫描不发射，而且录音时不扫；只有 W2 才要量（G-WIFI 第 9 条）。
- SD：卡在单独一条 SPI，不和屏幕共总线 [源 sd_card.cpp:43-53]，只抢内部 RAM 和 CPU。
- USB：第一次初始化会往 NVS 写 PHY 校准（会停 flash cache）。`CONFIG_ESP_PHY_ENABLE_USB=y` 让 USB-Serial-JTAG 在 PHY 初始化时保持可用 [源 sdkconfig；RIOT 在 C3 上的修复]。因为只在链路不 Ready 时扫，闸门只需确认"扫描以后插上 Mac，链路照常建立"和"插着线但 cpad 没连时扫描，USB 不掉"。
- 耗电：M5 标的正常 120.2 mA、开 WiFi 132.3 mA，电池 1500 mAh [源 M5 产品页]。扫一次 2–5 秒，可以忽略。

### 5.5 W1 的页面（以后真做时）

```
[头]                            周三 09:41 94%
WiFi · 附近                      (只听，不连)
▸重新扫描
 HomeNet              强
 eduroam              中
 Cafe-Guest           弱
 3 个网络 · 12 秒前
[Enter]扫描 [`]返回 [Tab]菜单
```

- 扫描中第一行写"扫描中… 约 5 秒"（Accent），Mando（46 px）用 docs/19 的 Sniff 动作，牌子"嗅一嗅…"。只在背光 On 时动。
- 信号写成"强 / 中 / 弱"（good / fair / weak），不画格子。
- 列表只在这一页的内存里，离开就丢；不写存储，不发给 Mac。
- W1 自己不在行上做什么事；P8-W 以后在这里加"把它记成 家 的地方"。
- 宽度：最宽一行中文 186 px、英文"Nearby · listen only" 120 px [算]。

### 5.6 W2：不排期，先定不变式

docs/18 的评审已经定过"WiFi 写得太细，压缩成一段"（docs/18:790）。W2 这里只写**以后无论怎么设计都不能违反的规则**，真要做时另写设计、单独评审：

1. **先有用途**（👤 第 5 条），排在 docs/18 v0.8 档案之后；G-WIFI 第 4、7、9 条要在 **STA 连着、lwIP 常驻**的状态下重测通过，才能排期。
2. **录音、传录音时不连、不扫**，WiFi 只在空闲时开。连着时按 G0 或 Opt+R：先开始录，再断开、关 WiFi。
3. **设备不自己重启**。lwIP 起了卸不掉，用完要回收内存只能重启：重启一律由你确认，而且要插着电、屏幕亮着；重启前把锁状态和时钟偏移写进 RTC_NOINIT 内存，醒来照旧锁着、时间照旧。"试连一次"也是你看着、确认过的动作，结束时同样确认重启。
4. **密码在 Mac 上输**：`cpa wifi add`，不回显（`getpass`）；没有 `--psk` 参数（会进 `ps` 和 shell 历史），给脚本用 `--psk-stdin`；不做"从钥匙串读"（那要多开一条管理员授权、读系统钥匙串的路，换来的只是少打一次密码）。走 USB 线送到设备；设备上不打密码（要做就得另写一个不落盘的密码编辑器）。
5. **`psk` 只出现在主机发的 `wifi.set` 一种消息里**：设备的任何回复、`debug.state`、`error` 都不带；`error.msg` 不回显。`check_vectors.py` 守住。cpad 只在链路 Ready、对方 `hello` 是 `cpa-fw/` 时发，不排队、断线不重发（否则可能被 Launcher 启动画面的串口控制台当成命令收下 [源 launcher.md 第 8 条]）。
6. **只给该给的 Mac**：`wifi.*` 只回答绑在当前档案上的 Mac；档案模式关着时不发已存网络和附近网络的名单。附近网络只在设备停在"等 Mac 输密码"的卡上时发；`wifi.set` 只在这张卡开着时收，同意在设备上给。网络名会暴露家和公司在哪，所以不能发给另一个档案的 Mac，也不能让任意一台 Mac 推一个网络进来。到时候在 docs/18 §8.3 T8 旁边加一行威胁。
7. **日志**：cpad 加 `SECRET_FIELDS = {"psk"}`，永远写 `<secret>`，不带长度和哈希，`CPA_LOG_CONTENT=1` 也一样。今天的 `redact_fields` 只看顶层键 [源 logging/redact.py]，而 cpad 会把异常文本 `str(exc)` 记进日志 [源 app.py:370、392]，像"psk too short: …"这样的校验错误会绕过按键名的过滤：**WiFi 的代码路径不记 `str(exc)`**，坏密码那条路径要有测试。`events.jsonl` 不记网络名。
8. **设备内存**：解码后把 Runtime 的接收块 `rx_`、分帧器的 `framer_.buf` 和解码出来的结构清零 [源 runtime.cpp:111、122；line_framer.h:18]。清不掉的写进 SECURITY.md：HWCDC 的 8 KB 接收环形缓冲（Arduino 管）、`framer_scan` 按值返回时留在栈上的拷贝、cpad 里的 Python 字符串。发送队列 `LineQueue` 只装设备发出去的行，`psk` 不会进去 [源 line_queue.h:1]。
9. **存储**：NVS 自己的命名空间，最多 4 个网络；驱动 `WIFI_STORAGE_RAM`，绝不写 `nvs.net80211`；不用 `WiFi.h`（`firmware/app` 里 grep 不许出现）；不在录音时写。
10. **风险定级**：明文的 WiFi 密码是**能拿去别处用的网络凭据**，比 SD 卡上的明文录音更严重：拿到设备的人用 esptool 就能读出来；Launcher 下装的其他固件读的是同一个 NVS，其中 Bruce 本身就是 WiFi 攻击工具 [源 launcher.md:50、114]。IDF 4.4 的 NVS 加密要先开 flash 加密，05 §8 定了不开；IDF 5.x 的 HMAC 方案等换工具链再议。另外 core 2.0.17 开机遇到 NVS 没有空页会擦掉整个 NVS（R4），存的网络会和 Launcher 的设置一起没。
11. **不支持**：开放网络、企业网（eduroam）、网页登录、5 GHz。

### 5.7 闸门 G-WIFI（W1 要过第 1–8 条；W2 还要第 9、10 条）

docs/18 §6 只写了方向（工具链、堆、不发包、不影响 USB）。这里定成能测的条目：

| # | 项 | 通过 |
|---|---|---|
| 1 | 工具链 | 先评估 ADR 0001 方案 C（Arduino ≥3.3.9 / IDF 5.5）：HWCDC 回复卡住修好、#18621 录音全 0 修好，整套 HIL 重跑通过。不换工具链就做 W1，要单独写理由 |
| 2 | 构建 | 静态 RAM ≤98,304 B；镜像 ≤1.75 MiB |
| 3 | 底线 | 先完成 docs/07 §6.7 第 6 条（整个 app 录音和传录音时的堆，真机）；再从 §5.3 的三条路里选，写进 docs/12 G-A6 |
| 4 | 堆（真机探针） | 在 `docs/gates/settings-probes/wifi` 加一个上机运行的环境：链接未开、扫描中、关掉后三种状态的空闲堆和最大块；初始化—扫描—关掉 50 轮后回到 ±1 KiB；扫描中的数字定成 §5.3 第 3 条的会话底线 |
| 5 | 不发包 | 被动扫描时空中没有我们的 probe request（另一台设备抓包）[待] |
| 6 | USB | 扫描以后插上 Mac，链路照常 Ready；插着线但 cpad 没连时扫描，USB 不掉、之后 cpad 照常连上 |
| 7 | 让路 | 扫描中按住 G0：录音先开始，WiFi ≤200 ms 关完；`ring_high` 不涨、`mic_missing == 0`；**开口延迟和不扫描时比**：按 UX-H4 的做法，一按下就说"一二三四"，转写里要有"一"（docs/20 §5.5 UX-H4） |
| 8 | 耗电 | 扫一次多耗多少（估 <1 mAh）[待] |
| 9 | 底噪（W2） | 连着 WiFi 时录一段，和不连比底噪（👤 听一段） |
| 10 | W2 前提 | 第 4、7、9 条在 STA 连着、lwIP 常驻的状态下重跑通过 |

## 6. 语言

### 6.1 盘点

按文件统计（"条"是按文件去重后的条数，"角色"是这条字在屏幕上的用途）[算 `i18n/inventory.py`、`measure.py`]：

| 文件 | 字面量 | 条 | 主要角色 |
|---|---|---|---|
| `modules/board/board_module.cpp` | 43 | 35 | 菜单说明、格内短词、整行 |
| `lib/help/action_help.cpp` | 33 | 32 | 菜单说明 |
| `modules/system/system_logic.cpp` | 28 | 28 | 拼接片段、系统页行 |
| `lib/help/menu_model.cpp` | 21 | 21 | 菜单说明、拼接片段 |
| `lib/shell/lock_screen.cpp` | 19 | 19 | 星期、拼接片段 |
| `lib/shell/rec_result.cpp` | 18 | 18 | 提示（toast） |
| `lib/shell/rec_overlay.cpp` | 17 | 16 | 提示、整行、牌子 |
| `lib/shell/runtime_outbox.cpp` | 17 | 17 | 拼接片段 |
| `lib/shell/shell_menu.cpp` | 17 | 15 | 标题、菜单说明 |
| `lib/shell/confirm.cpp` | 15 | 15 | 拼接片段、整行、提示 |
| 其余 25 个文件 | 145 | 141 | 编辑器、外壳、主页、收件箱、状态栏、Mando 的牌子、键名、年龄…… |
| **合计** | **373** | **357** | 全局去重 299 |

按角色（357 条）：拼接片段 78 · 菜单说明 62 · 提示 54 · 格内短词 44 · 整行 36 · 标题 19 · 星期 14 · 提示栏 12 · Mando 的牌子 9 · 主页格子 9 · 键帽行 8 · 状态栏 6 · 系统页行 6。

固件以外：

| 地方 | 带中文的行 | 怎么处理 |
|---|---|---|
| 固件测试 `firmware/app/test` | 46 个文件，641 行 | 不改：测试默认中文；英文另加（§9） |
| 模拟器脚本、HIL 工具 | 123 行、52 行 | 不改；加一份英文导览脚本 |
| cpad `daemon/src` | 6 个文件，27 行 | §6.11 |
| `cpa` 命令行 | 0 | 本来就是英文 |
| docs/16 和 App SDK（`feat/app-sdk`，还没合入） | 示例 31 行、模板 8 行；`app_rules.py` 规定提示栏以"Tab菜单"结尾 | §6.10 |

### 6.2 字符串表：三种做法

| 做法 | 好处 | 坏处 |
|---|---|---|
| A. 调用处写一对：`tr("队列为空", "Queue is empty")` | 改动最小 | 编译期查不了（C++17 没有 consteval）；同一个词散在各处 |
| B. 一个大 X-macro 表加枚举 | 测试里能遍历 | 300 个 id；调用处看不到原文；一个大文件，和并行分支天天冲突；第三方应用进不了封闭的枚举 |
| **C. 命名的 `constexpr Txt` 常量，放在用它的代码旁边** | 编译期检查；常量名就是 id；按文件分开，冲突少；应用一样能用 | C++ 里遍历不了全部（靠 §9 的脚本扫） |

**选 C。** 共享的词（记一条、排队给 AI、开 session、回复、项目、等你、返回、设置……）放在 `lib/i18n/words.h`，主页格子、Tab 菜单、编辑器标题都引用同一个常量，docs/20 U22 的"一个动作一个名字"（今天靠 `test_vocabulary`）就成了结构上的保证。

### 6.3 `Txt` 和编译期检查

```cpp
// lib/i18n/lang.h
enum class Lang : uint8_t { Zh = 0, En = 1 };
Lang lang();                     // 当前语言。只在 loop 线程里读写
void set_lang(Lang l);           // 只有外壳调用：开机读完 NVS 后、设置页切换时
Lang lang_from_nvs(uint8_t b);   // 不认识的字节 → Zh

// lib/i18n/lang_names.h：唯一允许不在 Txt 里的中文
constexpr const char* kLangNames[] = {"中文", "English"};  // 按 Lang 取，各用自己的文字

// lib/i18n/txt.h
void txt_check_failed(const char* why);  // 故意不是 constexpr：编译期调用它就是编译错误
struct Txt {
  const char* zh;
  const char* en;
  constexpr Txt(const char* z, const char* e, int16_t max_w = kNoLimit) : zh(z), en(e) {
    check_pair(z, e, max_w);  // 不合格时调用 txt_check_failed
  }
};
inline const char* tr(const Txt& t) { return lang() == Lang::En ? t.en : t.zh; }
// both(t)：把两半拼成 "语言 Language"，只给"永远找得回来"的那一行用
```

`check_pair` 是 constexpr 函数（C++14 起能写循环），查六件事：

1. 两种语言都不是空串；
2. 英文里没有汉字、假名、全角标点（U+3000–U+9FFF、U+FF00–U+FFEF），抓"忘了翻、直接复制"。**没有例外**：要同时显示两种文字的地方（语言行、切换提示），在运行时用 `both()` 或从 `kLangNames` 填 `%s`；
3. 两边的 `%` 转换序列一样（`%u %s` 对 `%u %s`），参数顺序不能换；
4. 英文里没有 efontCN_12 缺的常见符号 `— – ✓ ✗ ⚠ ▶ ▸ ▼ ‹ › « » ½ ⏎ ↵ ⌫`，它们会画成 □ [算 `i18n/efont.py`]；
5. 宽度：两种语言都不超过 `max_w`。**按真实字宽算**：ASCII 6 px，汉字和全角标点 12 px，`● … ★ ↑ ← → ◎ ◇ ○ · ×` 这 11 个 6 px；别的非 ASCII 字符按 12 px 算（只会量宽）。我们用到的字只有这几类，所以结果和 §9.3 的真实字模检查一致，不需要为了躲误报退回不查的 `Txt`。带 `%` 的格式串交给 native 测试用最长的参数量；
6. 没有控制字符。

不用 `throw`：ESP32 上编译时关了异常。调一个非 constexpr 的函数，效果一样 [设]。

**只能写成 `constexpr` 常量**（命名空间常量、`static constexpr` 成员或数组）。临时写的 `Txt{...}` 不在编译期求值，检查不会跑；`tools/i18n/check_texts.py` 在 CI 里拦住。数组照样受查：`constexpr Txt kWeekdays[7] = {{"周日", "Sun"}, …}`。

### 6.4 宽度规则

构造函数的第三个参数就是宽度类，用别名写清楚：

| 类 | 上限 | 依据 |
|---|---|---|
| `ToastTxt` | 224 px | toast 胶囊的字宽 `kToastTextW` [源 theme.h:105] |
| `LineTxt` | 232 px | 一行 240 px，两边各 4 px |
| `MenuTxt` | 120 px | `feat/app-sdk` 上 docs/16:197 给应用定的菜单说明上限（还没合入）；第一方也守，抽屉不用变宽 |
| `TileTxt` | 60 px | 第一方格子 75 px，名字最多 60 px [源 theme.cpp `tile`]；应用格子 48 px 由 `check_apps.py` 查 |
| `SignTxt` | 48 px | 牌子 = 字 + 10 px；会议页中心 x=23，靠 `theme::sign` 的左边界兜底（§6.9） |
| `KeyTxt` | 43 px | Tab 菜单键列最多 54 px，键帽 = 字 + 4 px [源 menu_view.h:27] |
| `Txt` | 不查 | 拼接片段，由用它的函数的测试量 |

- 指针数组别写成 `static const char* kX[]`：指针本身不是 const，整个数组进 DRAM。现有代码写的是 `constexpr const char*` [源 key_names.cpp]。

写法：

```cpp
// modules/board/board_texts.h
namespace cpa::txt {
constexpr LineTxt kQueueEmpty{"队列为空", "Queue is empty"};
constexpr ToastTxt kOtherOnMac{"其他项目只在 Mac 上看", "Other projects: see the Mac"};
constexpr Txt kUpdatedAgo{"更新于 %s前", "Updated %s ago"};
}  // namespace cpa::txt

draw_fit(c, x, y, w, tr(txt::kQueueEmpty), st);
std::snprintf(buf, sizeof buf, tr(txt::kUpdatedAgo), age.c_str());
```

返回 `const char*` 的纯函数（`action_help`、`mando_sign`、`store_note_text`……）签名不变，只把里面的字换成 `tr(...)`。主题接口本来就收 `const char*`，不用改。

### 6.5 复数、时间、日期、年龄、键名

- **复数**：只有英文要。`CountTxt{"%u 条笔记等着传到 Mac", "%u note waits for the Mac", "%u notes wait for the Mac"}`，`trn(t, n)` 在 n == 1 时取单数。约 6 处 [算]：控制页的三行图例、键盘丢键次数、主页"N 件事在等你"、看板"N 个 session 没归项目"。
- **数字和单位**不变：`476G`、`1.5M`、`83%`、`4012mV`。
- **时间**：两种语言都是 24 小时制 `13:42`（大号数字只有 0–9 和冒号，👤 第 3 条）。
- **日期**：锁屏卡"10/21 周三"在英文下写"Wed 10/21"，宽都是 60 px。这是唯一一处按语言换参数顺序的地方（`if (lang() == Lang::En)`）。不做 `%1$s`：newlib 对它的支持靠不住 [推]。
- **星期**：状态栏"周三"24 px 变成"Wed"18 px，更窄。
- **年龄**（docs/20 U22 的 `age_text`）：英文"9 s / 40 min / 2 h / 2 d"，紧凑写法"9s / 40m / 2h / 2d"。用缩写，就不用管复数。"……前"：看板"Updated %s ago"，设置页"3 min ago"，"从未"→"never"。
- **键名**：

| 今天 | 英文 | 键帽宽 | 说明 |
|---|---|---|---|
| 退格 | Del | 22 px | 用键帽上印的字 [待 对照真机] |
| 空格 | Space | 34 px | |
| 按住G0 | Hold G0 | 46 px | |
| 松开G0 | G0 up | 34 px | "Release G0"键帽 64 px，超过 54 px 的键列，压住说明 [算] |
| Enter、Tab、Opt+X、Fn+X、G0、`;` `.` `,` `/` `` ` `` | 不变 | | |

### 6.6 提示栏改成结构化的

今天的提示栏是一个字符串，主题按规则切成键帽和说明：开头的可见 ASCII 是键，连着的纯 ASCII 词并成一个键 [源 hint_layout.h；docs/19 §4.2 规则 2、4]。英文全是 ASCII，`Enter open` 会变成一个叫"Enter open"的键帽。所以加一种结构化写法：

```cpp
struct HintKey { const char* keys; Txt words; };   // {"Enter", {"打开", "open"}}
// Module 新增，有默认实现，老模块不用改：本页提示栏上的键，不含 Tab 菜单
virtual uint8_t hintKeys(const HintKey** keys) const { *keys = nullptr; return 0; }
```

- 模块给了 `hintKeys`，外壳按结构画，最后自动加"Tab 菜单 / Tab menu"；没给的照旧解析 `hint()`，中文应用不受影响。
- 量过的 12 条提示栏，英文用一两个词（open、reply、dismiss、back、menu、move、project）全部放得下；最紧的是收件箱和看板的 Sessions，中文 238 px、英文 236 px，都在最紧的一档 [算]。
- docs/16 "提示栏以 Tab菜单 结尾"的规则，对结构化提示不再需要。

### 6.7 Tab 菜单、格子、牌子和其他主题件

- **Tab 菜单**是现拼的：键位表 + `action_help` + 模块的 `help()` [源 menu_model.h]。`HelpRow` 加一个可选的 `const Txt* text`，有就用它，没有用老的 `label`。三个标签"本页 / 去哪 / 控制"→"Here / Go / Control"；最窄的格 54 px，"Control" 42 px 放得下 [算]。
- **主页格子**：`AppTile` 同样加 `const Txt* text`。英文格子名都 ≤48 px（附 A）。
- **Mando 的牌子**：英文短词 Listen、Sniff…、You!、Saved、Huh?、Napping、Here。`theme::sign` 加一条：胶囊左端不小于 x=2（顺手修好 §6.9 的中文"保存中…"出界）。
- **状态栏**："● 在线"→"● online"，"◇ 版本不兼容"→"◇ mismatch"，"录N"→"RN"。英文整体比中文窄 [算]。docs/18 的档案字（"公""家"）到时候用一个字母。
- **锁屏卡**：日期行"Wed 10/21 · Bat 83%" 120 px（中文 114）；两个计数胶囊"Waiting 3""To sync 2"共约 138 px，比中文宽约 40 px，放得下但紧 [算]。卡下面"键帽和文字交错"的两行，每种语言一份 `{是不是键, 文字}` 数组，顺序可以不同。
- **分颜色的句子**：主页"下午好，**2 件事**在等你"拆成前、高亮、后三段，每段一个 `Txt`。英文"Afternoon, **2** waiting for you" 174 px，区域 192 px [算]。
- **设备页的标签列**：英文标签都选不超过 36 px 的词，值列仍是 x=46，不按语言变（§4.5）。

### 6.8 语言存在哪，怎么马上生效

- **存在哪**：NVS，沿用 `nvs_boot` 的命名空间 `cpa`，键 `lang`，一个字节（0 中文，1 英文）。值变了才写 [源 nvs_boot.cpp；设]。
- **纯逻辑**：`lib/core/settings.h` 里 `struct Settings { Lang lang = Lang::En; }`（v0.5.2 以前是中文）。每个设置一个 NVS 键，不存成一整块，免得以后要做版本迁移。
- **设备端**：`lib/hal/nvs_settings.{h,cpp}`（`Preferences`，写法和 `nvs_boot.cpp` 一样）；`Platform` 加 `loadSettings()`、`saveSettings()`；模拟器放在模拟设备的内存里，`debug.reboot` 后还在。
- **开机**：在 `nvs_boot_ticket()` 之后、画第一帧之前读（包括"正在迁移到 SD 卡"那一屏），调 `set_lang()`。没有这个键或读不到就用英文（v0.5.2 以前是中文）；0、1 以外的字节仍然算中文（`lang_from_nvs`）。
- **英文优先（v0.5.2）**：什么都没存的设备用英文开机。以前是中文开机，所以跑过旧版 cpa-fw、从没切过语言的设备要继续用中文。`setup()` 里在 `nvs_boot_ticket()`（每次开机都写 `boot`）**之前**，`nvs_lang_migrate()` 在没有 `lang` 时写一次：`boot` 已经在（旧版 cpa-fw 开过机）就写 0（中文），不在（NVS 擦过）就写 1（英文）。新设备也要写英文：不写的话，第二次开机会看到有 `boot`、没有 `lang`，变成中文。已经存了的 `lang` 一律不动。纯逻辑 `settings.h` 的 `lang_to_store()`，主机测试 `test_settings`；设备上 `test_storage`。注意：在擦过的 NVS 上先跑设备测试也会写 `boot`，之后 app 就用中文开机；检查擦过 NVS 的情况要先刷 app。
- **切换**（设置页 Enter → 外壳）：
  1. `set_lang(新语言)`；
  2. **什么都不清**。排队的 toast 存的是格式化好的文字 [源 toast.h:27、43]，切换后最多再用旧语言显示几秒（≤4.5 s）就消失，不丢；锁屏卡的录音结果本来就存成码和参数（`RecLast` 的 `saved`、`why`、`mode`、`ms`，app_state.h:74-81），画的时候用 `tr()` 写，切换后自动是新语言，也不会把你还没看到的结果清掉（docs/20 U6、B1）。`last_hint` 只用来去重，不显示，也不用清；
  3. 整屏重画；
  4. 用新语言弹 §4.3 的那条提示；
  5. 没在录音就写 NVS；在录音就记一个"待写"，录音结束再写（写 flash 会停 cache，没必要和录音抢）[推]。
- **为什么不用重启**：所有界面文字都在画的时候用 `tr()` 取，模块状态里只存数字和 id。今天已经是这样，唯一的例外是 toast 队列，它几秒就过去 [源]。这条以后写进 docs/16 的规矩（`feat/app-sdk` 合入以后，ST-8）：模块不要把格式化好的界面文字留到下一帧。
- **Launcher**：和别的固件共用 NVS，`cpa` 命名空间不会撞车。整片刷写不擦 NVS（只有 `erase_flash` 或 `erase_region` 擦），擦了就和新设备一样用英文开机（v0.5.2 以前是中文）[源 launcher.md]。

### 6.9 英文比中文宽：量了什么，怎么办

**方法** [算 `i18n/measure.py`]：给按文件去重后的 357 条各写一条尽量短的英文（`i18n/en_draft.py`），用真实字模量宽，按角色和上限比；提示栏和键帽行按主题的真实排法算（键帽 = 字 + 4 px，至少 12 px，三档间距）。

**总体**：241 / 357 条英文更宽（68%），宽度比中位数 ×1.20（设备页标签用了 §4.5 的短词）。按角色：标题 ×1.40、提示 ×1.32、整行 ×1.28、菜单说明 ×1.20、格内短词 ×1.12、提示栏 ×1.00（用短词）、星期 ×0.75。最极端："等你"→"Waiting for you"，×3.75。

**超限的 23 条，全都能改短**：

| 角色 | 条数 | 例子：中文 → 草稿 → 改短后（px） |
|---|---|---|
| 提示（>224） | 11 | "已存到 Mac 队列（不会发给 session）" 210 → 288 → "Queued on the Mac (not sent)" 168 |
| | | "session 的项目不在设备上：去 Mac 回复" 222 → 294 → "Project not on device: reply on Mac" 210 |
| | | "要录更久：松开 G0，再按 Opt+R 开会" 204 → 264 → "Longer? Let go, then Opt+R" 156 |
| 整行（>232） | 3 | "SD 卡无法使用：请在 Mac 上格式化为 FAT32" 240 → 252 → "SD unusable: format FAT32 on a Mac" 204 |
| 菜单说明（>120） | 7 | "删一个字（空时退出）" 120 → "Delete a char (exit if empty)" 174 → "Delete (empty: exit)" 120；其余 6 条在 132–150 px，例如 "Lock (keeps recording)" 132 → "Lock, keep recording" 120 |
| 牌子 | 2 | 会议页的"Saving…"（中文"保存中…"一样）：胶囊 52 px，中心 x=23，左端出屏 → `theme::sign` 加左边界 |
| 键名、设备页标签 | — | "Release G0"→"G0 up"；"Firmware"→"FW"（§4.5） |

**中文本身已经超出的两处**（和语言无关，ST-5 顺手修）：

- 系统页"SD 卡无法使用：请在 Mac 上格式化为 FAT32"240 px，行宽只有 232 px，末尾被截成"…"。改成"SD 卡不能用：在 Mac 上格式化成 FAT32"，216 px。
- 会议页收尾的牌子"保存中…"：胶囊 52 px，中心 x=23，左端在 x=−3。

**字节**：英文表约 5.8 KB，中文约 6.2 KB（含结尾的 0）。同样宽的一行，英文 6 px 一个字节、中文 4 px 一个字节，所以现有的 `FixedStr` 都够：最长的英文提示 49 字节，`ToastText` 能放 96 [算]。

**缺字**：efontCN_12 没有 `— – ✓ ✗ ⚠ ▶ ▸ ▼ ‹ › « » ½ ⏎ ↵ ⌫ ⇧ ⌘ ⌥ ♪`；有 `● … ★ ↑ ← → ◎ ◇ ○ □ · × ° ± ≈ ▲ ◆ ■ ☆ “ ’ é ü` [算]。英文里的破折号写成 `:` 或 `-`，引号用 ASCII 的。

### 6.10 模块和第三方应用（docs/16，在 `feat/app-sdk` 上）

| 应用能写字的地方 | 今天 | 之后 |
|---|---|---|
| `toast(level, text)`、`hint(lv, text)` | `const char*` | 不变，传 `tr(kX)` |
| 提示栏 `hint()` | 一个字符串，以"Tab菜单"结尾 | 新增 `hintKeys()`；老写法照样能用 |
| Tab 菜单 `HelpRow{ev, keys, label}` | `label` 是 `const char*` | 新增可选的 `text` |
| 主页格子 `AppTile{key, label, badge}` | `label` 是 `const char*` | 新增可选的 `text` |
| `Canvas::text`、`draw_fit` | `const char*` | 不变 |

- 都是新增、带默认值的，**今天的应用不改也能编译、能用**，只是英文模式下显示中文。
- `check_apps.py`（`feat/app-sdk` 上）跟着改：两种语言都量；应用里的中文裸字面量报**警告**，不报错；结构化提示不再要求以"Tab菜单"结尾。`new_app.py` 的模板直接生成 `Txt` 写法，番茄钟示例改成双语。谁先合入谁就后改。

### 6.11 Mac 端跟进

- **协议几乎不带界面文字** [源]：设备收到错误只显示错误码的名字，不显示 Mac 发来的 `msg` [源 runtime_outbox.cpp:212-219]；`agent.result` 的失败原因也是码，由设备翻成字。
- **唯一的例外**：录音历史 `rec.items` 里，没有标题的会议由 cpad 写成"会议 42m" [源 recordings/history.py:87]。`rec.items` 的协议和 cpad 已经在 v0.4.1 的开发线上，设备上的录音应用（Opt+V）按 D10 顺延到 v0.4.2，还没画这些字 [源 CHANGELOG]。所以**在 v0.4.2 的设备端开始画 `rec.items` 之前**改成发 `title: null`（schema 本来允许），设备按 `mode` 和 `dur_ms` 自己写"会议 42 分钟 / Meeting 42 min"，现在改成本最低。这样 **`hello` 不加 `lang`**，cpad 不需要知道设备的语言。
- **`cpa` 命令行**本来就是英文，不动。
- **Mac 上笔记里的标签**（"会议：""语音：""[音频]""[全文]""[转写]""（仅转写）""（未转写）""分钟""待办"）[源 recordings/route.py:22-65]：默认**不跟着设备变**（👤 第 4 条）。笔记是长期留着的文件，同一个文件里中英混着不好找；你也可能在 Obsidian 或 MCP 里按这些词搜。**只有你在第 4 条选了英文**，才做 `config.json` 的 `notes_lang: "en"`（只影响以后写的行；导出目录的 README 和 MCP 的截断提示 `mcp/resources.py:109、113` 跟着同一个键走）。
- **文档**：docs/ 继续用中文。README 的按键表改"Opt+M 设置"，加"设置 → 语言"和"想用 WiFi 工具"两节。

## 7. 协议新增（只新增，`v` 仍为 0）

| 阶段 | 变化 | 方向 | 内容 | 兼容 |
|---|---|---|---|---|
| A | `rec.items[].title`：cpad 不再拼"会议 42m" | H→D | 没标题时发 `null` | schema 本来允许 null；加一个合法用例。设备的录音应用还没发 |
| A | `debug.state.ui.lang?` | D→H | `"zh"` / `"en"`，**只在测试版** | 可选；省略 = 旧构建。HIL 用它断言 |
| A | `debug.lang` | H→D | `{lang: "zh" \| "en"}` → `ack`，**只在测试版**；走和设置页 Enter 同一条切换路径（也写 NVS） | 正式固件回 `E_UNKNOWN_TYPE`；CI 和 release 工作流的"正式固件里没有 `debug.(key\|state\|reboot\|rec\|mic\|codec)`"检查加上 `lang`（protocol README 第 16、17 条的做法） |
| B | `debug.state.wifi?` | D→H | `{radio, scans, free_min, block_min, dirty}`，只在测试版 | 可选；G-WIFI 的 HIL 用 |

- W2 的消息（`wifi.get`、`wifi.state`、`wifi.set`、`wifi.forget`）**这次不定**，到排期时按 §5.6 的不变式写。
- **合并约束**：新 schema 和向量要和固件 codec 认识它们的改动同批合入（固件向量测试要求"解码后重新编码逐字段相等"）；`protocol/README.md` 加一条（编号按合并顺序）。

## 8. 预算

| 项 | 今天（integrate/v041） | 阶段 A：设置 + 语言 + W0 | W1（以后，只扫描） | 上限 |
|---|---|---|---|---|
| app 镜像 | 约 1.03 MB | 约 +13 KB（英文约 5.8 KB、`Txt` 对 357 × 8 B ≈ 2.9 KB、代码 1–2 KB、设置页和 W0 约 3 KB） | +358 KB，加页面约 +3 KB | 警告 1.75 MiB；硬上限 2.5 MB |
| 静态 RAM | 约 66,700 B | 约 +27 B（`SysInfo.host_label` 25 B；当前语言、"待写"各 1 B） | +22,772 B → 约 89,500 B | 98,304 B |
| 空闲堆（WiFi 关） | 178.7 KiB | 0（`tr()` 只返回指针） | −31.0 KiB → 147.7 KiB | ≥150 KiB，见 §5.3 |
| 空闲堆（扫描中） | — | — | 猜约 98–113 KiB，**没量** [推] | 会话底线等 G-WIFI 第 4 条 |
| 最大块 | 160.0 KiB | 0 | 最多 129.0 KiB；扫描后可能更少（碎片）[待] | ≥64 KiB |
| NVS | `cpa`：`epoch`、`boot` | +1 键 `lang` | 0 | 分区 20 KB，和 Launcher 等共用 |
| AppState | 3,496 B（含主题 +8） | 0（语言是全局字节，不进 AppState） | +约 4 B（radio 状态） | 4 KB |
| 模块对象 | — | 系统模块 ≤1 KB | 附近列表 8 × 约 40 B ≈ 320 B | 1 KB |
| 运行时间 | — | 每取一条字多一次比较 | 扫描 ≤5 s，只在 WiFi 页 | — |
| 工作量 [推] | — | 9–11 个工作日 | Mac 端评估 1–2 天；闸门 3–5 天（不含换工具链本身，那是 1–2 周）+ 3–4 天 | — |

W2 不排期，不列预算；§5.3 的 STA 探针（静态 +25,408 B、堆 −33.5 KiB、lwIP 常驻再少 6–12 KiB）就是它的起点。

## 9. 测试和 HIL（一律先写测试：RED → GREEN → REFACTOR）

### 9.1 编译期

`test/compile_fail/i18n/` 放一组故意写错的 `Txt`：缺英文、英文里有汉字（包括"语言 Language"这种混写）、`%u` 对 `%s`、超宽、带 `—`。CI 确认它们**编译不过**；再放一组对的，确认能编译 [设]。

### 9.2 固件 native（Unity）

| 测试 | 先写的断言 |
|---|---|
| `test_i18n`（新） | 默认中文；`set_lang` / `lang`；`lang_from_nvs` 遇到不认识的字节回到中文；`tr`、`trn`（n = 0、1、2）、`both`；宽度检查和 §9.3 的真实字模结果一致 |
| `test_settings`（新，纯逻辑） | Enter 切换；切换**不清** toast 队列（排着的录音结果 toast 照样显示）；录音中只记"待写"，录音结束才写；写失败时的提示；`Settings` 默认值 |
| `test_mod_system`（改） | 先开设置页；**每次打开光标都在 WiFi 行**；`;` `.` 只在两行可改的行之间走；`,` `/` 换标签；Enter 在 WiFi 行进 WiFi 页、在语言行调 `setLanguage`；连接行：状态词右对齐、长名字截断、拔线后显示上一台的名字和"离线"；WiFi 页在 Launcher / 整片刷写两种启动下的文字；**设备页最多 8 行，所有警告来源都打开时最后一行一定是警告行**；英文标签都 ≤36 px；三个标签页的胶囊 y 一样；两种语言下每一行都画得完整 |
| 现有宽度测试改成两种语言各跑一遍（`for (Lang l : kLangs) { LangScope s(l); … }`） | `test_text_width`（确认框标题、所有 toast）、`test_vocabulary`（一个动作一个名字，两种语言都成立；"设置"取代"系统"）、`test_theme_hint`（结构化提示放得下）、`test_menu_view`（键帽 ≤47 px）、`test_status_bar_states` |
| `test_wifi_logic`（新，只在 W1 排期时） | 状态机：只在 WiFi 页、背光 On、链路不是 Ready、不在录音和传录音、电量 ≥17%、堆够时才进扫描；G0、Opt+R 时**先发开始录音**、再关 WiFi；链路变成 Ready 立刻关；离开页面、熄屏、锁屏都关；关掉后堆没回来就把扫描关到下次开机，**从不调重启** |

**现有测试不改**：测试默认中文，641 行中文断言照旧有效。`LangScope` 在测试里临时换语言，析构时换回来。

**模糊测试**：`test_fuzz_runtime` 的随机键里有 Opt+M（`kOptKeys` 含 `m`）[源 sim/test/test_fuzz_runtime/fuzz_user.cpp:127]，以后一定会切语言。加三条不变式：

- 语言找得回来：随机序列跑完后，退出所有浮层、按 Opt+M，光标在 WiFi 行，再按 `.`、Enter 一定换到另一种语言（打开设置页后 ≤3 下）；
- 英文下任何一帧都没有缺字（`missing_glyphs == 0`）；
- 模拟器的设置跨 `debug.reboot` 保留，和切换次数无关。

（设备不跳 Launcher、不自己重启，所以模糊测试不需要模拟这两件事。）

### 9.3 真实字体（CI，Python）

`tools/i18n/`，由 `docs/gates/settings-probes/i18n` 的脚本整理而来：

- `texts.py`：扫出所有 `*Txt` 常量，用真实字模量两种语言，按类别和上限比；输出一份 TSV（中文、英文、宽度、类别），审英文时用；
- `check_texts.py`：禁止临时 `Txt{...}`；除 `lib/i18n/lang_names.h` 以外，禁止在 `lib`、`modules` 里新加中文裸字面量（迁移完后从警告升成错误）。

### 9.4 模拟器

- `firmware/sim/scripts/settings.cpas`（新）：Opt+M、截图、`.` Enter 切英文、截图、`debug.reboot`、确认还是英文、Opt+M、Enter 进 WiFi 页、截图、`` ` ``、`.` Enter 切回中文。`--strict` 抓 □。
- `firmware/sim/scripts/v04_tour_en.cpas`（新）：先切英文，再跑一遍现有导览；看拼图里的截断、重叠、对齐。
- `test_theme_sim` 加上英文这一遍。
- W1 排期时：模拟器的假无线（`FakePlatform` 返回固定的附近列表、可控的耗时和堆变化），截附近网络页。

### 9.5 协议和 daemon

- `rec.items` 无标题会议的合法用例（`title: null`）；daemon `test_history_title_null.py`。
- `debug.lang` 合法、非法用例（`lang: "fr"`）；`debug.state.ui.lang`；正式固件里没有 `debug.lang` 这个名字（CI 检查）。
- 只有你选了英文笔记（👤 第 4 条）：`test_notes_lang.py`（默认中文，`notes_lang: "en"` 只影响新行）。

### 9.6 HIL

| 场景 | 在哪 | 内容 |
|---|---|---|
| 夹具（所有场景） | 模拟器和真机 | 开始时读 `debug.state.ui.lang` 记下来，不是 `zh` 就用 `debug.lang` 设成中文；**结束时用 `debug.lang` 恢复成开始时的语言**。不重放按键，不悄悄改掉你设备上的偏好 |
| `settings`（新） | 模拟器和真机 | Opt+M → `mod == system`，光标在 WiFi 行；`.` Enter → `ui.lang == "en"`；`debug.reboot` 后还是 `en`；在英文下跑一遍 `nav` 和 `menu` 的子集；夹具恢复原来的语言 |
| `nav`（改） | 同上 | Opt+M 仍到 `system`；Tab 菜单"去哪"里那一行叫"设置" |
| `test_runtime_budget`（改） | 真机 | "新页面全部画过"加上设置页、设备页、WiFi 页、英文下的主页和菜单；`missing_glyphs == 0`；空闲堆仍 ≥150 KiB，比改之前少不到 1 KiB |
| G-WIFI 的设备套件（W1 排期时） | 真机 | `test_wifi_budget`（三种状态的堆、50 轮开关）、扫描后插 Mac、扫描中按住 G0 和开口延迟、`rec_soak` 期间反复进 WiFi 页（不应扫描） |

### 9.7 真机（👤）

- **阶段 A，约 10 分钟**：切到英文 → 拔电重启 → 还是英文；英文下录一段便条、开一次会；看锁屏卡上英文的计数胶囊和"退格"键帽上印的字；看一眼 WiFi 说明页；切回中文。

## 10. 阶段和工作包

**阶段 A：设置 + 语言 + W0**。发版位置（👤 第 6 条）：默认是 v0.4.2（录音历史与待办，D10）之后的一个小版本，暂叫 **v0.4.3「设置和英文」**。v0.4.1「稳当版」是 docs/20 的 A 包，B 包随主题，都不动。

| 编号 | 内容 | owns_paths | 依赖 | 验收 |
|---|---|---|---|---|
| **ST-1** i18n 和接口 | `Lang`、`Txt`、`check_pair`、宽度类、`tr`、`trn`、`both`、`LangScope`、`kLangNames`；`HintKey` 和 `Module::hintKeys()`；`HelpRow`、`AppTile` 的可选 `text`；主题按结构画提示栏。**只加基础设施，不搬字** | `firmware/app/lib/i18n/{lang.h,lang.cpp,lang_names.h,txt.h,words.h}`（新）、`firmware/app/lib/module_api/module.h`、`firmware/app/lib/theme/{hint_layout,theme}.{h,cpp}`（结构化提示那几行）、`firmware/app/test/core/test_i18n/`（新）、`firmware/app/test/compile_fail/i18n/`（新）、`.github/workflows/ci.yml`（compile-fail 一步） | v0.4.1 发版 | `test_i18n` 绿；compile_fail 每个用例都编译不过；现有测试全绿 |
| **ST-2** 设置存储和切换 | `Settings`、`nvs_settings`、`Platform` 接口、模拟器实现、开机读取、切换流程；测试版 `debug.lang` 和 `debug.state.ui.lang`（schema、向量、固件 codec 同批） | `firmware/app/lib/core/settings.h`（新）、`firmware/app/lib/hal/nvs_settings.{h,cpp}`（新）、`firmware/app/lib/shell/platform.h`、`firmware/app/lib/shell/runtime.cpp`（开机和切换那几行）、`firmware/app/lib/protocol/*`（`debug.lang` 那几行）、`firmware/sim/lib/simdev/*`（设置跨重启）、`firmware/app/test/core/test_settings/`（新）、`protocol/schema/v0/{debug.lang,debug.state}.schema.json`、`protocol/vectors/v0/messages/debug.*.json`、`protocol/README.md`、`daemon/src/cpad/protocol/specs.py`、`.github/workflows/{ci,release}.yml`（正式固件名字检查） | ST-1；D1–D10 合入（`runtime.cpp` 在 A 包里） | `test_settings`；模拟器 `debug.reboot` 后语言还在；`pytest protocol/tools` |
| **ST-3** 设置页和 W0 | 两个标签、设置页、连接和同步两行、WiFi 说明页、设备页 8 行、`help()`、提示栏；存 `host_label` | `firmware/app/modules/system/*`（含新的 `system_texts.h`、`wifi_page.*`）、`firmware/app/lib/module_api/module.h`（`SysInfo.host_label`）、`firmware/app/lib/shell/runtime_link.cpp`（握手时存）、`firmware/app/test/ui/test_mod_system/`、`firmware/sim/scripts/settings.cpas`（新） | ST-2 | `test_mod_system` 两种语言；截图 `missing_glyphs == 0` |
| **ST-4** 搬字 | 35 个文件的中文换成 `Txt`，分四批：菜单和键名 / 外壳和录音 / 主页、看板、收件箱 / 主题和牌子；`words.h`；结构化提示；复数、日期、年龄；名字"系统"→"设置" | `firmware/app/lib/help/*`、`firmware/app/lib/shell/*`（只动字）、`firmware/app/lib/theme/theme_mando.cpp`、`firmware/app/lib/module_api/text_fit.cpp`、`firmware/app/modules/{home,board,inbox}/*`（含新的 `*_texts.h`）、`firmware/app/lib/i18n/words.h` | ST-1；**D1–D10 和 B 包（B1–B6）都合入以后**；在一个短窗口里做完 | 每批只动字、不动逻辑；两种语言的宽度测试全绿；中文截图和搬之前逐像素一致 |
| **ST-5** 英文审一遍 | 改短超宽的；`theme::sign` 左边界；中文本身超出的两处 | 同 ST-4 的文件、`firmware/app/lib/theme/theme.cpp`（`sign`） | ST-4 | §6.9 的 23 条都在限内；两处中文修好 |
| **ST-6** 工具和导览 | `tools/i18n`、英文导览、`test_theme_sim` 的英文一遍、模糊测试的三条不变式、HIL 夹具和 `settings` 场景、CI | `tools/i18n/*`（新）、`firmware/sim/scripts/v04_tour_en.cpas`（新）、`firmware/sim/test/test_theme_sim/`、`firmware/sim/test/test_fuzz_runtime/`、`tools/hil/scenarios/settings.py`（新）、`tools/hil/*`（夹具）、`.github/workflows/ci.yml` | ST-4 | CI 跑 `check_texts.py` 和 `texts.py`；英文拼图看过；模糊测试绿 |
| **ST-7** Mac 端：无标题会议 | `rec.items` 无标题发 `null`；协议用例 | `daemon/src/cpad/recordings/history.py`、`protocol/vectors/v0/messages/rec.items.json`、对应的 `daemon/tests/unit/*` | 无；**要在 v0.4.2 设备端画 `rec.items` 之前合入** | daemon 测试绿 |
| **ST-7b** 英文笔记（只在 👤 第 4 条选英文时） | `notes_lang`；导出 README 和 MCP 截断提示跟着走 | `daemon/src/cpad/recordings/route.py`、`daemon/src/cpad/config/settings.py`（`notes_lang` 那几行）、`daemon/src/cpad/archive/export.py`、`daemon/src/cpad/mcp/resources.py`、对应的 `daemon/tests/unit/*` | 👤 第 4 条 | 默认行为（中文笔记）逐字节不变 |
| **ST-8** App SDK | 模板、`check_apps` 量两种语言、番茄钟双语、docs/16 加"不留格式化好的文字"的规矩 | `tools/apps/*`、`docs/16-writing-apps.md`、`examples/apps/pomodoro/*` | `feat/app-sdk` 合入 | `check_apps.py` 两种语言都过 |
| **ST-H** 真机 | §9.6 的 `settings`、`test_runtime_budget`；👤 §9.7 | `docs/07-hil-checklist.md`、`docs/releases/v0.4.3.md` | 以上全部 | 全过 |
| **ST-DOC** 文档 | README（按键表、语言、想用 WiFi 工具）、docs/04 §8、docs/13 的"系统"、docs/18 §5.6 的"系统 → 档案"改成"设置 → 档案"、CHANGELOG | `README.md`、`docs/04-prd.md`、`docs/13-assistant-v2-plan.md`、`docs/18-context-profiles.md`、`CHANGELOG.md` | 无 | — |

```
阶段 A：
v0.4.1 发版 ──► ST-1（只加基础设施）──► B 包和 v0.4.2 的新界面从一开始就写 Txt
D1–D10 合入 ──► ST-2 ──► ST-3
D1–D10、B1–B6 合入 ──► ST-4 ──► ST-5、ST-6 ──► ST-H（v0.4.3 发版）
ST-7 在 v0.4.2 设备端画 rec.items 之前；ST-7b 只在 👤4 选英文时；ST-8 等 feat/app-sdk；ST-DOC 并行
以后（都等 👤 第 5 条）：
W-M（Mac 端信号评估，和 docs/18 P8-B1 一起）──► 你决定 ──► W-G（含工具链）──► W1（= docs/18 P8-W）
W2：不排期
```

- **ST-1 先合**：它只加类型和检查，不搬字。合进去以后，B 包（`lock_screen`、`rec_overlay`、`rec_pages`、`status_bar`、`shell_render`、`menu_view`、主页、看板、收件箱）和 v0.4.2 的录音应用新写或改写的字直接写 `Txt`，ST-4 少搬一批，也不会和 B 包抢同一批字。
- **阶段 A 一起发**，不先发"有设置页、切到英文却只有一半是英文"的半成品 [设]。
- ST-4 是大范围的机械改动：分四批提交，每批只动字，能单独 rebase。

**以后（W1，只在你同意以后）**：

| 编号 | 内容 | owns_paths | 依赖 | 验收 |
|---|---|---|---|---|
| **W-M** Mac 端信号 | cpad 读默认网关的 MAC 地址（或 CoreWLAN 的 SSID，要定位权限），作为 docs/18 P8-B1 的档案信号；写清楚设备端扫描还剩什么用 | `daemon/src/cpad/...`（到时候定）、docs/18 §7.3 | 👤 第 5 条 | 评估结论写进 docs/18 |
| **W-G** 闸门 | 工具链评估（ADR 0001 方案 C）；§5.7 第 1–8 条；底线定下来 | `docs/gates/wifi.md`（新）、`docs/gates/settings-probes/wifi/*`（加上机运行的环境）、`docs/adr/0001-toolchain.md`（结论）、`docs/12-m3-work-packages.md`（G-A6 底线） | W-M 以后你同意 | §5.7 第 1–8 条都有实测结论 |
| **W1** 附近网络 | 直接调 IDF 的被动扫描；状态机；附近网络页；Mando 嗅；模拟器的假无线 | `firmware/app/lib/hal/wifi_radio.{h,cpp}`（新）、`firmware/app/lib/core/wifi_logic.{h,cpp}`（新）、`firmware/app/modules/system/wifi_page.*`、`firmware/sim/lib/simdev/*`（假无线）、`firmware/app/test/core/test_wifi_logic/`（新）、`firmware/app/test/device/test_wifi_budget/`（新） | W-G | §9.2、§9.6 W1 各项 |

## 11. 风险

| # | 风险 | 怎么应对 |
|---|---|---|
| 1 | 英文超宽要到真机上才发现 | 宽度类在编译期按真实字宽挡；CI 用真实字模再量；英文导览的拼图用眼睛看（§6.9、§9） |
| 2 | 格式串交给 `tr()` 后，GCC 不再按参数查 `%`（`-Wformat` 只认字面量） | 编译期查两种语言的 `%` 序列一样；native 测试把每个格式串在两种语言下都跑一遍 |
| 3 | 临时 `Txt{...}` 绕过编译期检查（C++17 没有 consteval） | `check_texts.py` 拦住 |
| 4 | 搬字和并行的固件分支冲突（`feat/ux-decisions` 在改 keymap、shell、home、menu；B 包改同一批字） | ST-1 先合、B 包直接写 `Txt`；ST-4 等 D1–D10 和 B 包；系统模块的改动（ST-3）基本不碰它们的文件；ST-4 分四批，每批只动字 |
| 5 | 切错了语言看不懂 | "语言 Language"双语行；Opt+M、`.`、Enter 一定切回来；提示用两种文字写怎么换回去；模糊测试守着"≤3 下" |
| 6 | 一下误按就把整个界面换了语言 | 光标每次打开都停在 WiFi 行（docs/18 §5.9；docs/20 U5、U18） |
| 7 | 切换后几秒内 toast 还是旧语言 | 接受：最多 4.5 s；换来的是不丢没看过的结果 |
| 8 | HIL 把你设备上的语言悄悄改掉 | 夹具用测试版的 `debug.lang`，结束时恢复原来的语言（§9.6） |
| 9 | 英文词选得不好（"等你"叫什么） | 附 A 是草稿，👤 第 3 条；共享的词都在 `words.h`，改一处就行 |
| 10 | 你以为设置里会有 WiFi 开关，结果只有说明 | §0 和 WiFi 页都写清楚为什么、怎么用出厂固件；值写"无"不写"关"，不暗示有一个能打开的开关；👤 第 5 条问清楚要 WiFi 做什么 |
| 11 | 链接 WiFi 让空闲堆跌破 150 KiB | 先量（G-WIFI 第 3、4 条），没定底线就不合入任何 WiFi 代码 |
| 12 | WiFi 开着时 `rec_task` 被饿、便条丢头 | W1 只在没连 Mac、不录音时扫；按下录音先录再关；G-WIFI 第 7 条量开口延迟 |
| 13 | 开关 WiFi 留下碎片，最大块回不来 | 关掉后量堆；没回来就关掉扫描到下次开机，不自动重启；50 轮开关进闸门 |
| 14 | NVS 满了，core 2.0.17 开机擦掉整个 NVS（R4） | 语言只在值变了时写一个字节；W2 的网络表另算（§5.6 第 10 条） |
| 15 | "退格"键帽上印的到底是什么字 | 对照真机 [待] |
| 16 | 模糊测试一路按到设置页，切了语言 | 三条不变式（§9.2）；设备不跳 Launcher、不自己重启，没有别的出口 |
| 17 | Mac 端的信号够用，W1 白做 | 先做 W-M，再决定要不要 W1 |

## 12. 不做的事

- 第三种语言；从 SD 卡加载翻译文件（没签名，还要用堆）；gettext、ICU 这类库。
- 按 Mac 的系统语言自动切换；开机让你选语言；12 小时制；按地区换日期格式。
- 翻译从 Mac 来的数据（项目名、session 名、录音标题、Mac 的名字、网络名）。
- 换字库或加英文字体：efontCN_12 的 ASCII 已经够用。
- 设备上一键进 Launcher（launcher.md R8、§4.4）；设备自己重启（§5.4）。
- 这一版不链接任何 WiFi 代码。以后也不做：上网、云同步、OTA、NTP 对公网、企业网（eduroam）和网页登录、开放网络、设备上直接输密码、从钥匙串读密码、flash 加密、自己混淆密码、Arduino `WiFi.h`。
- 设置里的其他开关：亮度、熄屏时间、自动锁时长（docs/20 D2 已定默认）、声音、麦克风增益、主题、会议同意。有需要再加，位置已经留好（语言下面、分隔线上面）。
- 在 Mac 上改的东西不搬到设备上：项目、去向、录音拉取、MCP 权限、时区、`notes_lang`。

## 13. 需要你拍板

不回复就按默认做。

1. **名字和入口**：**默认：Opt+M 改叫"设置"，先打开设置页**；主页格子"M 设置"，诊断信息往右一页（`/`）。代价：看诊断从 1 下变 2 下。备选：仍叫"系统"、先开诊断，设置放第二页。
2. **怎么换语言**：**默认：光标停在 WiFi 行，`.` 到语言行，Enter 直接在中文和 English 之间切换**，一共三下。备选：语言行弹一个两行的选择单，多按两下，好处是以后加第三种语言不用改。
3. **英文词表**（附 A）：**默认：按附 A，24 小时制**。最需要你看一眼的：主页格子 Projects · Waiting · Meeting · Note · AI queue · Session · Settings · Lock · Menu；"等你"在 Mando 的牌子上写"You!"；"退格"写 Del。
4. **Mac 上笔记里的标签**（会议：/语音：/[音频]）：**默认：继续用中文**，设备换成英文也不变，`notes_lang` 不做。备选：在 `config.json` 写 `notes_lang: "en"`，以后写的行用英文（ST-7b）。
5. **WiFi 你想用来做什么**：**默认：这一版只做 W0（看得到连接，告诉你怎么用出厂固件的 WiFi）；W1、W2 都不排期**。
   - 想让设备"认地方"、提醒你换档案：先做 W-M（看 Mac 端的信号够不够，设备 0 B），再决定要不要 W1（docs/18 P8-W，要换工具链、过 G-WIFI）。
   - 想连网，请说是为了什么：A：**不插线也同步到 Mac**：要换工具链、做配对和加密（05 §8 的 Noise），录音和传录音时 WiFi 必须关着，所以只能在空闲时传；几周的工作量，还多一个能被攻击的口子。B：**对时**：价值低，插 USB 时 Mac 已经在对。C：别的，你说。
6. **发版**：**默认：阶段 A 作为一个版本一起发，排在 v0.4.2（录音历史与待办）之后，暂叫 v0.4.3「设置和英文」，不发半成品；ST-1 在 v0.4.1 发版后先合，让 B 包和 v0.4.2 的新界面直接写 `Txt`；ST-7 在 v0.4.2 设备端之前合**。备选：先发设置页加 W0（ST-1–ST-3，中文），英文晚一版。

## 附 A：英文词表草稿

主要的词如下。完整的 357 条在 `docs/gates/settings-probes/i18n/en_draft.py`，ST-4 时搬进各个 `*_texts.h`。

| 中文 | English | 宽（px） | 用在 |
|---|---|---|---|
| 主页 | Home | 24 | 菜单、提示栏 |
| 项目 | Projects | 48 | 格子、标题 |
| 等你 | Waiting | 42 | 格子、标题、锁屏胶囊 |
| 开会 / 会议录音 | Meeting | 42 | 格子、页面标题 |
| 记一条 | Note | 24 | 格子、编辑器标题、菜单 |
| 排队给 AI | AI queue | 48 | 格子、编辑器标题、菜单 |
| 开 session | Session（格子）/ New session | 42 / 66 | 格子 / 编辑器标题和菜单 |
| 回复 | Reply | 30 | |
| 设置 / 设备 / 档案 | Settings / Device / Profiles | 48 / 36 / 48 | 格子、标签胶囊 |
| 连接 / 同步 | Link / Sync | 24 / 24 | 设置页的事实行 |
| 在线 / 离线 | online / offline | 36 / 42 | 连接行右边的状态词 |
| 锁屏 / 解锁 | Lock / Unlock | 24 / 36 | |
| 菜单 | Menu | 24 | |
| 语音便条 | Voice memo | 60 | |
| 返回 | Back | 24 | |
| 待传 | To sync | 42 | 锁屏胶囊 |
| 未归属 | No project | 60 | |
| 在线 / 同步 / 无响应 / 版本不兼容 / 离线 | online / sync / no reply / mismatch / offline | ≤48 | 状态栏 |
| 本页 / 去哪 / 控制 | Here / Go / Control | ≤42 | Tab 菜单 |
| 在呢 / 在听 / 嗅一嗅… / 等你 / 存好了 / 没听到 / 打盹中 / 保存中… | Here / Listen / Sniff… / You! / Saved / Huh? / Napping / Saving… | ≤42 | Mando 的牌子 |
| 无 · 只用 USB 线 | None · USB only | 90 | WiFi 行 |
| 固件 / 启动 / 存储 / 堆 / 电池 | FW / Boot / Store / Heap / Bat | ≤30 | 设备页标签（都 ≤36 px） |
| 强 / 中 / 弱 | good / fair / weak | 24 | 附近网络（W1） |
| 秒 / 分钟 / 小时 / 天 | s / min / h / d | | 年龄 |
| 周日 … 周六 | Sun … Sat | 18 | 状态栏、锁屏 |
| 退格 / 空格 / 按住G0 / 松开G0 | Del / Space / Hold G0 / G0 up | ≤42 | 键名 |
| 早上好 / 中午好 / 下午好 / 晚上好 / 你好 | Morning / Hi / Afternoon / Evening / Hello | ≤54 | 主页问候 |

## 附：证据索引

- **怎么重跑**（证据都在 `docs/gates/settings-probes/`）：
  - 字体文件：模拟器构建时由 `firmware/sim/lib/efont_cn12/gen_font.py` 生成 `firmware/sim/.pio/gen/efont/efont_cn12.c`（M5GFX 0.2.33 的 `lgfx_efont_cn_12`）。
  - 盘点：`python3 -I i18n/inventory.py <仓库根> lits.json`（373 个中文字面量、35 个文件）；量宽：`python3 -I i18n/measure.py <efont_cn12.c> lits.json measured.json`（357 条、23 条超限）。
  - 本文新字串：`python3 -I i18n/width.py <efont_cn12.c> < i18n/strings.txt`；提示栏：`python3 -I i18n/hints.py <efont_cn12.c>`。
  - WiFi 探针：见 `wifi/platformio.ini` 开头的注释（`pio run`、`nm` 取 `_heap_start`、`mapsum.py` 比 map）。
- **系统页**：`modules/system/system_logic.{h,cpp}`（`kSystemLines = 9`、`add()` 满了就丢、录音行让给警告行）、`system_module.{h,cpp}`（`kValueX = 46`、`onKey` 返回 false、`kIdentityRows`、`kDividerGap`）、`lib/module_api/canvas.h:111-116`（内容区 y=12、高 111）。
- **名字和菜单**：`modules/home/home_logic.h:31`、`lib/help/action_help.cpp:23`、`lib/help/menu_model.cpp`（`goto_rank`、`add_control`、`mac_text`）、`lib/input/keymap.cpp`（列表层的 `;` `.` `,` `/`、`` ` `` 和退格都是返回、Fn 方向键）。
- **版式**：`lib/theme/theme.{h,cpp}`（`kToastTextW = 224`、`pill`、`sign`、`tile`）、`lib/theme/hint_layout.{h,cpp}`（切分规则、汉字键名）、`lib/help/menu_view.{h,cpp}`（`kMenuKeyColW = 54`、`label_dx`）、`modules/board/board_module.cpp:262-282`（标签胶囊、`kListX`）、docs/19 §4、§6。
- **状态里的文字**：`lib/shell/toast.h:27,43`（`ToastText`、`last_hint`）、`lib/core/app_state.h:74-81`（`RecLast` 是码和参数）、`lib/shell/rec_result.h:13`（`RecText` 只给 toast 用）。
- **连接**：`lib/protocol/messages.h:65,81`（`HostLabel`）、`lib/shell/runtime_link.cpp:51`（只存 `host_app`）、`lib/module_api/module.h:61-80`（`SysInfo`）。
- **锁和时钟**：`lib/shell/lock.h:27`、`lib/core/app_state.h:115`（开机默认没锁）、`lib/shell/runtime_rec.cpp:140`（时钟是内存里的偏移）。
- **录音**：`lib/hal/audio_task.h:38-45`（`CPA_REC_ALLOC_AT_START=0`、优先级 1、核 0）、`audio_io.h:32-36`（6×2048 样本）。
- **USB 收发**：`lib/usb_transport/{usb_transport.h,line_queue.h}`（8 KB 接收、`LineQueue` 只发不收）、`lib/shell/runtime.cpp:111-123`（`rx_`、`framer_scan`）、`lib/link/line_framer.h:18`。
- **模糊测试**：`firmware/sim/test/test_fuzz_runtime/fuzz_user.cpp:127`（`kOptKeys` 含 `m`）。
- **存储和预算**：`lib/hal/nvs_boot.cpp`（命名空间 `cpa`）、`partitions/cpa_8MB.csv`（NVS 20 KB）、`scripts/check_budget.py`、`test/device/test_runtime_budget/test_main.cpp:377`（`150u * 1024u`）、docs/12:455（G-A6）、docs/13 §10.3、docs/19 §10、docs/releases/v0.4.0.md:83-88、docs/07:624-628（§6.7 第 6 条：整个 app 录音时的堆还没量）。
- **WiFi 探针**：`docs/gates/settings-probes/wifi/`，`_heap_start`：base `0x3fc96810`、scan `0x3fc9e3e8`（+31,704 B）、idf `0x3fc9ee30`（+34,336 B）、ard `0x3fc9f030`（+34,848 B）；镜像 257,632 / 615,696 / 644,576 / 684,432 B [测]。本地框架：`~/.platformio/packages/framework-arduinoespressif32/libraries/WiFi/src/WiFiGeneric.cpp:642-707,763`、`WiFiScan.{h,cpp}`、`tools/sdk/esp32s3/sdkconfig`、`esp_wifi.h`、`esp_netif.h`。
- **WiFi 外部来源**：IDF 4.4.7 S3 WiFi 指南、RAM 用量指南、NVS 和 flash 加密文档；IDF v5.5 NVS 加密（HMAC）；Espressif AR2024-008（IDF 4.4 EOL）；arduino-esp32 #8033、PR #7044；Rust esp-radio 内存数据；M5 Cardputer-Adv 产品页；ESP32-S3 datasheet v2.2 表 5-7；RIOT 的 PHY/USB 修复；atomic14、WLED 的麦克风噪声；Jamf 社区和 Apple 开发者论坛（macOS 取 SSID）。
- **Launcher 和安全**：docs/gates/launcher.md（第 3 节、第 78 行 DDLB、第 98 行、R4、R8、L5、G-L5、第 8 条、NVS 共用、`l_wifi`）、docs/01:178,187、docs/05 §5.9、§5.10、§6.8、§6.10、§8、ADR 0001:77、ADR 0004、docs/18 §5.6、§5.9、§6、§7.3、§8.3、§15 第 8 条、docs/18:790。
- **Mac 端**：`daemon/src/cpad/recordings/history.py:87`、`recordings/route.py:22-65`、`archive/export.py`、`mcp/resources.py:109,113`、`logging/redact.py`（`BODY_FIELDS`、`redact_fields` 只看顶层键）、`app.py:370,392`（`str(exc)` 进日志）、`control/server.py`（0600、`cmd`）、`device/link.py`（发送队列）、`cli/main.py`（`cpa` 子命令）、`protocol/schema/v0/{hello,common,debug.state}.schema.json`、`protocol/README.md` 第 13、16、17、19、20 条、CHANGELOG（v0.4.1 开发线的 `rec.items`；B 包除 B6 还没做）。
- **并行分支**：`feat/ux-decisions`（D1–D10，改 keymap、shell、home、menu、lock 等，未合入）；`feat/app-sdk`（docs/16、`tools/apps`，未合入，只读）。

## 附：ST-4、ST-5 实施时定下的几件事（2026-10-27）

- **英文提示栏的键写在方括号里**：`"[Enter]open [`]back [Tab]menu"`。英文全是 ASCII，按 §6.6 的老规则会并成一个键；结构化的 `hintKeys()` 只覆盖模块，外壳、确认框、锁屏卡下面那两行和卡片上的键行都还是字符串。所以 `hint_layout` 加第 6 条规则：字符串里有"词首的 `[`"就只认方括号，键是括号里的，说明到下一个词首的 `[` 为止。中文字符串没有方括号，切法和以前完全一样（中文截图逐像素不变）。放不下退回一行时去掉括号。方括号让 `Txt` 的宽度检查多算，所以提示栏字符串用不查宽的 `Txt`，由 `test_hint_bars` 两种语言真画一遍（键帽、不退回一行、英文的键和中文一样）。
- **"等你"这个地方英文叫 Inbox**：格子在一个字母键帽和 `!9` 后面只剩 37 px，"Waiting"（42）放不下。计数照旧说 waiting：主页"Afternoon, 2 waiting for you"、锁屏卡 `Waiting 1`。
- **英文主页格子**：一个格子在单字母键帽后面有 53 px，`Opt+R` 后面 31 px，角标再占它的宽度加 4 px。所以 P 写 Board，角标只写个数（`Board 12`；Tab 菜单那一行照旧 `run12`），R 写 Meet，S 写 Session。`test_mod_home` 用两位数的角标把每个格子画一遍，名字必须完整。
- **英文主页提示栏**放不下"双击免提"：写 `[G0]hold, talk after REC [Enter]open`（评审后改的，原来的 "talk at ●REC" 要读两遍）；双击写在 Tab 菜单（`G0 x2`）和锁屏卡上。
- **英文原因开头大写**，因为它们总在句首："Offline: Mac queue only"。请求失败的 toast 在英文里是 `<什么>: <码>, queued`，最长的码（E_UNKNOWN_SESSION）也放得进 224 px。
- **检查**：`tools/i18n/check_texts.py`（CI）——`lib/`、`modules/` 里不在 `constexpr Txt` 常量里的中文字面量一律失败，只放过 `lang_names.h` 和提示栏解析用的键名；`texts.py` 输出全部 428 对的宽度表（没有超限的；英文更宽的 263 对）。模糊测试：英文、Mac 只发 ASCII 的一轮里画出汉字（"语言""中文"除外）就失败；两种语言里，静止的一帧把字画出屏幕就失败（Tab 菜单滑入的那几帧除外）；跑完从主页 Opt+M . Enter 两个方向都切得过去。

## 附：英文截图评审和真机（2026-10-27）

**状态**：阶段 A 的设备端做完了（ST-1 到 ST-6），ST-H 的自动部分在真机上过了（docs/07 §9）。还剩 👤 §9.7 那 10 分钟（拔电重启、英文下录便条和开会、看"退格"键帽上印的字）和 ST-7 / ST-7b（Mac 端）。

- **评审看了两种语言的 35 个脚本**，中文截图没变，英文有 6 处要改，都先写了失败的测试再改：
  - 有字碰到屏幕右边（22 张英文截图，差 1 px，所以模糊测试没抓到）：收件箱和看板的提示栏改成 `[Opt+D]hide`；会议页 "now" 夹在离边 2 px 以内（中文"现在"在最右时也左移 2 px）；确认框的 session 一行按 238 px 截，引用的文字每行按 239 px 折。
  - 模拟器的 `FbCanvas` 现在把墨迹点亮最后一列（x=239）的字也算出界：ASCII 的 u、w、n 会用满它那 6 px 的最后一列，汉字的最后一列是空的。`test_ux_edges` 用两种语言跑全部 ux 脚本和两个导览，每张截图都把屏幕重画一遍来数（Tab 菜单滑入的最后几帧是局部推屏）。`test_hint_bars` 要求以 ASCII 结尾的提示栏离右边至少 1 px。
  - 措辞：`[G0]/[Space]star`（英文里不留没有字的键帽）、`[Opt+N/Q/S]type`、`[G0]hold, talk after REC [Enter]open`、"Hello, no Mac yet"（主页自己的小写一份，收件箱的空状态照旧 "No Mac yet"）、锁屏卡 "√ Saved 0:15" / "× Too short"、"No project 5 (only 2)"。
  - **找得回来**：中文界面下 Tab 菜单"去哪"里 Opt+M 那一行写"设置 Settings"（主页格子和设置页的标签还是"设置"）。
- **真机**（docs/07 §9）：英文的重画预算第一次量了，`CPA_BUDGET_EN max_render_us=38160`（中文 40,944），会议页 22.2 ms、便条页 21.7 ms，都在 50 ms 以内；NVS 里存英文、读回、坏值（7）读成中文、放回原样，都在真机上跑过。
- **HIL 夹具**（§9.6）：每次跑完用 `debug.lang` 放回开始时读到的语言（读不到就是中文），失败了也放。`lang` 场景（§9.6 叫 `settings`）用按键切到另一种语言、`debug.reboot` 后还在、再切回来、重启后也还在；"英文下跑一遍 nav 和 menu"这一条没做。

## 附：评审意见怎么处理的

评审提了 5 条高、8 条中、5 条低。逐条对过代码和 docs/05、07、12、18、19、20、launcher.md、CHANGELOG：17 条成立，全部采纳；第 6 条里"`psk` 也会留在 `LineQueue` 里"一句不成立（`LineQueue` 是发送队列），其余部分采纳。

| 评审意见 | 处理 |
|---|---|
| **H1** 堆没回来就悄悄 `esp_restart()`：锁状态、时钟丢掉；W2 的试连必然触发 | **采纳**。永不自动重启：脏的关闭只把扫描关到下次自然开机，WiFi 行写"关了，重启后再用"（§5.4 第 6 条，附理由：`lock.h:27`、`app_state.h:115`、`runtime_rec.cpp:140`）。W2 的不变式：重启一律你确认、插着电、屏幕亮，锁和时钟先存进 RTC_NOINIT；试连也是确认过的动作（§5.6 第 3 条）。新原则 9 |
| **H2** "去 Launcher…"违反 R8、忽略 DDLB、要 5 下、依赖出厂固件已装、把你送去明文打密码的固件 | **采纳**。删掉这一行、`enterLauncher()`、确认框、两条拒绝提示和 HIL `launcher_hop`。W0 只有说明，加一行"它会把 WiFi 密码明文打到串口"（§4.4，列了不做的五条理由）；"不做"写进 §12 |
| **H3** W2 会让 WiFi 在录音时开着 | **采纳**。硬不变式：录音、传录音时不连、不扫，WiFi 只在空闲时开（原则 3、§5.6 第 2 条）；删掉"录音时守住堆"（§13 第 5 条改写）；W2 排期前 G-WIFI 第 4、7、9 条要在 STA 连着、lwIP 常驻时重测（§5.7 第 10 条） |
| **H4** 内存预算不实：140 KiB 底线靠没量过的数；开着的代价是借来的；§8 和 §5.3 对不上；单位混用；扫描中途来 `file.*` | **采纳**。删掉 140 KiB 的数字，改底线要先完成 docs/07 §6.7 第 6 条（§5.3、G-WIFI 第 3 条）；开着的代价标成 [推]、写明没量，加真机探针 G-WIFI 第 4 条；全文改成 KiB（147.7 / 145.1 KiB），§8 和 §5.3 用同一套算法；W1 只在链路不是 Ready 时扫，一条规则排除 `file.*` 抢占、USB/PHY 和传输碎片（§5.4 第 2 条），G-WIFI 第 6 条随之简化 |
| **H5** `wifi.*` 把网络名单发给任何 Mac，任何 Mac 都能推网络 | **采纳**，写进 W2 不变式（§5.6 第 6 条）：只回答绑在当前档案上的 Mac；档案模式关着时不发名单；附近网络只在"等 Mac"卡上发；`wifi.set` 只在这张卡开着时收。docs/18 §8.3 **这次不改**：W2 没排期，写一个不存在的功能的威胁会误导；排期时在 T8 旁边加一行，已写进第 6 条 |
| **M6** 凭据处理的漏洞：`str(exc)`；设备缓冲；`--from-keychain`；风险定级；NVS 满了整片擦 | **部分采纳**。WiFi 路径不记 `str(exc)`、坏密码要有测试（§5.6 第 7 条）；设备清零 `rx_`、`framer_.buf`、解码结构，清不掉的（HWCDC 接收缓冲、栈上的拷贝、Python 字符串）写进 SECURITY.md（第 8 条）；删掉 `--from-keychain`（第 4 条、§12）；明文密码定成"能拿去别处用的网络凭据"，比录音严重，点名 Bruce（第 10 条）；R4 整片擦写进第 10 条和风险 14。**不采纳的一句**：`LineQueue` 是发送队列（`line_queue.h:1`），`psk` 不会进去 |
| **M7** 打开设置页光标就在语言行，一下误按整个界面换语言；"每次回到第一行"没写；HIL 用按键切、不恢复 | **采纳**。WiFi 行放第一、光标每次打开都回到 WiFi 行，换语言变成 Opt+M、`.`、Enter 三下（§0、§4.2、§4.6、§13 第 2 条）；HIL 夹具用测试版 `debug.lang`，结束时恢复原来的语言（§7、§9.6、风险 8） |
| **M8** 设备页少一行，警告被丢掉；胶囊和行的 y 不一致；存储行合并后太宽 | **采纳**。8 行：栈余并进电池行，幽灵/重置不为 0 时进警告行，录音计数去设置页的同步行，迁移结果替换"剩余"（§4.5，每行量过宽）；三个标签页胶囊统一 y=12；`test_mod_system` 加"警告行一定可见"（§9.2） |
| **M9** 连接行按 6 个字符的名字量的；`host_label` 根本没存 | **采纳**。状态词右对齐钉住，名字放中间用 `draw_fit`，"上次…"去掉（同步行已有）；两种语言名字都有 108 px；`SysInfo.host_label` 25 B，§0、§8 的静态 RAM 改成约 +27 B（§4.2） |
| **M10** 设置页提示栏没有返回键 | **采纳**。"[;.]选 [Enter]改 [`]返回 [Tab]菜单" / "[;.]pick [Enter]change [`]back [Tab]menu"（196 / 232 px）；设备页、WiFi 页同样写返回（§4.6） |
| **M11** WiFi 开着时开始录音会更慢 | **采纳**。先开始录、同时关 WiFi（环形缓冲开机就有，768 ms > 200 ms）；G-WIFI 第 7 条加开口延迟和基线比（UX-H4 的"一"）（§5.4 第 4 条、§5.7） |
| **M12** 顺序和版本名：ST-4 只等 D1–D10；"v0.4.1 录音历史"和 D10 矛盾；docs/16 不在这棵树上 | **采纳**。ST-1 先合、B 包和 v0.4.2 直接写 `Txt`，ST-4 等 D1–D10 和 B1–B6（§10）；阶段 A 定为 v0.4.3（v0.4.2 之后），ST-7 要在 v0.4.2 设备端画 `rec.items` 之前合（§6.11、§13 第 6 条）；docs/16 一律写明"在 `feat/app-sdk` 上，未合入"（§6.4、§6.8、§6.10） |
| **M13** 范围膨胀：W2 写得太细；W1 和 v0.8 绑在一起；`notes_lang` 默认就做 | **采纳**。W2 压成 11 条不变式（§5.6），删掉它的协议、预算、测试、工作包和风险；W1 改成 docs/18 P8-W、不和 v0.8 绑（§5.2）；先做 Mac 端信号评估 W-M（§5.1、§10）；ST-7 只剩 `title: null`，`notes_lang` 变成 ST-7b，只在你选英文时做 |
| **L14** "关 / Off"暗示有一个能打开的开关 | **采纳**。改成"无 · 只用 USB 线" / "None · USB only"，胶囊"(无)" / "(None)"（§0、§4、附 A、风险 10） |
| **L15** 本文自己的字违反编译期检查第 2 条；宽度检查可以精确 | **采纳**。语言行用 `both(Txt{"语言","Language"})`；两条切换提示和"没存上"用 `%s` 从 `kLangNames` 填；第 2 条不再有例外；宽度按 11 个 6 px 符号精确算，删掉"误报时退回不查的 `Txt`"（§4.3、§6.3、§6.4） |
| **L16** 切换时清掉锁屏卡的录音结果行，会丢你没看到的结果 | **采纳**，而且更进一步：`RecLast` 本来就是码和参数（`app_state.h:74-81`），画的时候用 `tr()`；切换时**什么都不清**，toast 最多旧语言几秒（§6.8、`test_settings`） |
| **L17** 模糊测试会按 Opt+M | **采纳**。三条不变式：≤3 下找得回语言、英文无缺字、设置跨重启（§9.2）；没有跳转和重启，不用模拟 |
| **L18** 定结论的证据只在没提交的临时目录 | **采纳**。探针源码和结果、盘点和量宽脚本随本文提交到 `docs/gates/settings-probes/`；`inventory.py` 补上了原先手工做的 `\x` 还原，重跑得到同样的 373 / 35 / 357 / 23；文首和证据索引写了怎么重跑 |
