[English](19-visual-theme.md) · **中文**

# 19 · 视觉主题：Mando 陪伴（方向一）

> 角色：界面负责人起草，Lead 定稿、提交。日期 2026-10-21。你选了方向一。本文是实现计划：令牌和色板、画布和显示接口、公开主题接口、素材管线、每一屏的布局、Mando 的状态机、分阶段的文件、测试、预算、风险和不做的事。只读了代码、文档和示意图，**没有接设备，也没有改代码**。
> 依据：方向一的规格、两位评审的意见和三份调研（`scratchpad/visual/companion_spec.json`）；示意图 `scratchpad/visual/companion/01_home … 07_states.png`、`frames/anim_listen_1-4.png`、`frames/anim_regions@2x.png`，以及生成它们的脚本 `companion/src/{lib,s_home,s_board,s_menu,s_rec,s_lock,s_states,sprite,build_all}.py`；固件 `lib/hal/m5_display.*`、`lib/module_api/{canvas,module}.h`、`lib/shell/{platform.h,shell*.cpp,status_bar.*,rec_overlay.*,lock_screen.*,toast.*,confirm.*,runtime*.cpp,power.*}`、`lib/help/menu_view.*`、`modules/{home,board,inbox,system}`；模拟器 `firmware/sim/lib/{simfb,simpng,simdev}`、`firmware/sim/README.md`；`ci.yml`、`scripts/check_budget.py`、`scripts/stack_budget.py`、`tools/repo/check_sizes.py`；`feat/app-sdk` 分支上的 docs/16 和 `AppTile`（未合入，只读）；WhisperWoof（只读）`website/mando/hd/{wait,think,review,hop}.webp`、`src/assets/fonts/{Nunito-normal-latin.woff2,OFL.txt}`、`LICENSE`。
> 标记同 docs/13：**[源]** 读过的代码或文档 · **[测]** 已有实测 · **[算]** 推算 · **[设]** 设计假设，可以改 · **[待]** 要用模拟器、oracle 或真机验证 · **👤** 需要你。
> 坐标都是 240×135 屏上的像素，(x, y, w, h) 从左上角算。字宽按 efontCN_12：ASCII 6 px，汉字 12 px，`● … ← ↑ ★ ·` 6 px [源 sim/README]。对比度按 WCAG 公式、用面板实际显示的 RGB565 值算 [算 `scratchpad/theme_plan/contrast.py`]。

## 0. 一页摘要

**目标**：把纯黑配青蓝的"工业风"换成 WhisperWoof 的样子：暖棕深底、焦糖色焦点、圆角卡片和键帽，再请 Mando 进来。Mando 只在状态变化时动：点头是在听，托腮是在同步，歪头加 "!" 是等你，跳一下是存好了，闭眼加 z Z 是锁屏打盹。录音的 ● 永远钉在左上角。

**怎么做**：

1. **8 位调色板画布**：还是 240×135、32,400 B，只把 M5GFX 的 RGB332 换成 `palette_8bit`，多 768 B 色板（堆上）。256 项里用约 130 项：24 个颜色令牌、Mando 的 15 色、压暗副本、圆角和 ● 的过渡色、两条数字渐变、6 级暖光，其余留给以后（§2）。
2. **只认令牌**：`Color` 从 8 个扩到 24 个。老 8 个的名字和意思不变，只换成暖色，所以 v0.4 的模块和 docs/16 的应用不改代码就换了颜色。画布只收 `Color`，传整数编译不过。色板的 0x00、0x10、0xE0、0xFF 四项是洋红"哨兵"：v0.4.0 那 8 个 RGB565 常量的低字节正好落在这四个位置，万一有人绕过去传了常量，画出来是洋红，模拟器测试一眼抓住（§3.2）。
3. **一份画法，两边共用**：圆角、●、精灵、大数字、压暗、暖光都写成纯函数（`lib/theme/raster.*`），设备和模拟器用同一份代码直接写 8 位缓冲区。只有汉字仍各走各的 efont，这部分已有 M5GFX oracle 对拍。所以模拟器截图和设备逐像素一致，截图仍是评审工具（§3.6）。
4. **局部推屏和动画 tick**：新增 `Display::present(Rect)` 和一个 200 ms 的动画 tick。录音页每帧只推 Mando 框、波形带和大 ● 三块，约 2.8 ms；整秒刷新照旧。录音时屏幕的 SPI 时间从约 104 ms/s 降到约 27 ms/s [算]。**只在背光 On 时动**：调暗、熄屏时什么都不动，● 定在全亮（§3.5、§7）。
5. **素材**：WhisperWoof 原版 Mando，4 位色，46 px 17 帧、64 px 12 帧，约 24 KB；Nunito 800 的数字 30/34 px，约 4 KB；连色板表一共约 30 KB（上限 60 KB）。生成器在 `tools/theme/`，只读 WhisperWoof，输出的头文件提交进仓库；固件编译不依赖 WhisperWoof（§5）。
6. **公开主题接口** `lib/theme/theme.h`：令牌、卡片、键帽、胶囊、Mando 的牌子、格子、提示栏、Mando 本身。第三方应用（docs/16）直接用；`Module::animate()` 让应用页面也能动，同样只在背光 On（§4）。
7. **分四个阶段，每个都能单独发**：S0 色板和令牌（不改布局）→ S1 卡片、键帽、胶囊 → S2 Mando、大数字、动画（录音、锁屏、主页）→ S3 Tab 菜单抽屉和动效（§8）。

**评审意见**只采纳方向一以内的：调暗时 ● 定在全亮；呼吸最暗一步在底色上不低于 4.5:1（最后定为 #cb574c，4.66:1）；每张卡都有一条更亮的 1 px 顶边；背光不是 On 时不动。其他方向的布局（大标题卡主页、44/48 px 数字、红色实心 REC 胶囊、图标）不引入（§1、§12）。

**👤 要你的两件事**：S1 之后在真机上看 43% 和 10.5% 背光下的暖色深底、ink3 文字、芥末色和焦糖色（约 10 分钟）；S2、S3 之后各跑一次 `test_runtime_budget` 和 `rec_soak`（HIL，Lead 约时间）。

## 1. 方向一和评审意见怎么落地

| 来源 | 内容 | 处理 |
|---|---|---|
| 方向一 | 示意图 01–07 和动画帧 | **目标**：布局、坐标、配色、Mando 的姿势和牌子都按示意图（§6、§7） |
| 评审 1、2 | ● 在调暗背光下要冻结在全亮 | **采纳**。背光离开 On 时 `breathe(0)`，呼吸停在第 0 步 |
| 评审 1 | 呼吸最低点 #a2463d 只有 3.2:1，要提到约 70%、不低于 4.5:1 | **采纳并加严**。在 bg 上要 4.5:1，算出来最暗只能到约 83% 亮度：#cb574c，4.66:1 [算]。70%（#b04c43）只有 3.6:1，不够 |
| 规格 | 状态栏的 ● 和大 ● 一起呼吸 | **改**：状态栏的 ● 不呼吸。它坐在 rec_bg 胶囊上，最暗一步只有 3.47:1；固定槽位的 ● 永远是全亮的 Rec 色（对 rec_bg 4.81:1）。只有录音页的大 ● 呼吸，也少推一块区域 |
| 评审 1、2 | 卡片只靠填充（对 bg 1.15:1）在面板上会消失，要顶边、色条或描边 | **采纳**。普通卡加 1 px `CardRim` 顶边（对卡 1.59:1，对 bg 1.84:1）；抽屉和锁屏卡用 `Rim`（对 sheet 2.25:1）；选中的卡有 `Live` 描边；等你的卡另有芥末色条 |
| 评审 1、2 | 背光不是 On 时不动，熄屏时不置脏 | **采纳**（§3.5、§7.4） |
| 评审 1 | 要读的文字不低于 ink3，约 4.5:1 | **采纳并修正**：WhisperWoof 的 ink3 #8f8276 在卡上只有 4.47:1，提到 **#96897b**（卡上 4.97:1、bg 上 5.73:1）。选中底上不用 ink3 |
| 评审 1、2 | 11×9 的小狗头可能被看成狐狸或猫 | 先用；有人看错就换成 5×5 的圆点图（不能用 "●" 字，免得和 REC 的 ● 混淆）。一行改动 |
| 评审 1、2 | 芥末（等你）和焦糖（焦点）只差 1.17:1 | 照方向一的规矩：等你永远带 "!"、左侧色条或"等你"两个字，从不只靠颜色 |
| 评审 1 | 主页太挤（五个区） | 保持示意图的布局，只在方向一里面做减法：去掉"主页 · 日期"标题（星期挪进状态栏，示意图本来如此），"按住 G0 说一段"挪进提示栏，`!0` 不显示 |
| 评审的嫁接建议 | 大标题卡主页、44/48 px 数字、红色实心 REC 胶囊、12×12 图标、波形渐隐、每次移动光标都滑、电池图标、Dim 专用色板 | **不引入**：这些来自方向二、三的布局（§12） |

## 2. 令牌和色板

### 2.1 颜色令牌（`Color`）

色值来自 WhisperWoof 的深色主题（DESIGN.md、`src/index.css` 的 `.dark`）加 REC [源 research A]。老 8 个排在最前面，值和意思不变。"索引"是色板里的位置（§2.2），由生成的表映射，不等于枚举值。

| 值 | 令牌 | 色值 | 索引 | 用途 | 对比度（bg / card 上） |
|---|---|---|---|---|---|
| 0 | `Bg` | #120e0b | 0x01 | 页面底 | — |
| 1 | `Fg`（别名 `Ink`） | #f3ede6 | 0x02 | 正文 | 17.1 / 14.8 |
| 2 | `Dim`（别名 `Ink3`） | #96897b | 0x03 | 次要文字、提示 | 5.73 / 4.97 |
| 3 | `Accent` | #e2a867 | 0x04 | 焦点、G0、"在听"、波形 | 9.58 / 8.30 |
| 4 | `Ok` | #72c291 | 0x05 | 跑N、存好了 | 9.24 |
| 5 | `Warn` | #dcc15a | 0x06 | 等你、!N、★ | 11.2 / 9.69 |
| 6 | `Error`（别名 `Rec`） | #f4675b | 0x07 | ● 和录音计时、错误 | 6.46；rec_bg 上 4.81 |
| 7 | `Select` | #463525 | 0x08 | 选中卡、未落地胶囊的底 | （底色） |
| 8 | `Sheet` | #1a1512 | 0x09 | 锁屏卡；Mando 边缘预混的底 | 对 bg 1.06 |
| 9 | `Card` | #221c18 | 0x0A | 卡片、Tab 抽屉 | 对 bg 1.15 |
| 10 | `Raised` | #2c241e | 0x0B | 中性胶囊 | 对 bg 1.26 |
| 11 | `Line` | #3a2f27 | 0x0C | 轨道、滚动条 | 对 bg 1.44 |
| 12 | `Cap` | #43372d | 0x0D | 键帽 | 对 bg 1.63 |
| 13 | `Rim` | #5a4e44 | 0x0E | 抽屉、锁屏卡的顶边 | 对 sheet 2.25 |
| 14 | `CardRim` | #4a3e35 | 0x0F | 普通卡的顶边 [设] | 对 card 1.59 |
| 15 | `Ink2` | #bfb2a6 | 0x11 | 第二级文字、键帽上的字 | 9.35 / 8.10；cap 上 5.74 |
| 16 | `Accent2` | #c48a4c | 0x12 | 毛色：波形第二色、选中项的键帽 | — |
| 17 | `OnAccent` | #1d130b | 0x13 | 焦糖底上的字 | accent 上 9.25 |
| 18 | `Live` | #926d45 | 0x14 | 选中卡描边、未落地胶囊描边、时间轴 | — |
| 19 | `OkBg` | #272f25 | 0x15 | 绿色胶囊底 | ok 在上面 6.89 |
| 20 | `WarnBg` | #372f1d | 0x16 | 芥末胶囊底 | warn 在上面 8.02 |
| 21 | `RecBg` | #3a2520 | 0x17 | 状态栏 REC 胶囊底 | rec 在上面 4.81 |
| 22 | `InfoBg` | #36291c | 0x18 | Mando 的牌子（在呢 / 在听） | accent 在上面 7.14 |
| 23 | `RecDot` | 呼吸（§2.3） | 0x19 | 录音页的大 ●，只用在这里 | 最暗一步 4.66 |

- 内部还有一项 `RecDotEdge`（0x1A），是大 ● 的抗锯齿边，跟着呼吸变，不对模块开放。
- 老 8 个都换了值：`Fg` 是暖白，`Dim` 是 ink3，`Accent` 从青色变焦糖色，`Select` 从深蓝变 #463525。今天画在 `Select` 上的字是 `Fg`（10.6:1）或 `Dim`；`Dim` 在 Select 上只有 3.5:1，S0 把这些地方改成 `Ink2`（5.77:1）。例外：表示"现在不可用"的灰字（离线时的队列项，`test_offline_items_are_drawn_grey`）仍用 `Dim`，和 WCAG 对禁用控件的例外一样。
- 深色面在 RGB565 下互不相同：bg/sheet/card/raised/line 显示为 #100c08/#181410/#211c18/#292418/#392c21 [算]。

### 2.2 色板布局（256 项，`palette_8bit`）

| 索引 | 内容 | 项数 |
|---|---|---|
| 0x00 | **哨兵**（洋红 #ff00ff）：v0.4.0 的 `0x0000`、`0xF800` 落在这里 | 1 |
| 0x01–0x0F | 令牌 0–14 | 15 |
| 0x10 | **哨兵**：`0x8410`、`0x0010` | 1 |
| 0x11–0x19 | 令牌 15–23 | 9 |
| 0x1A | `RecDotEdge`（呼吸） | 1 |
| 0x1B–0x1F | 留给以后的令牌 | 5 |
| 0x20–0x2E | Mando m1–m15（sprite 里的 4 位值 n 画成 0x1F+n） | 15 |
| 0x30–0x3E | Mando 的压暗副本（压暗画法：0x2F+n） | 15 |
| 0x40–0x5F | 令牌的压暗副本 | 至多 32 |
| 0x60–0x9F | 圆角和 ● 的过渡色（按 RGB565 去重后约 35 项） | 至多 64 |
| 0xA0–0xA3 | 数字渐变：Fg 在 Bg 上，20/40/60/80% | 4 |
| 0xA4–0xA7 | 数字渐变：Fg 在 Sheet 上 | 4 |
| 0xA8–0xAD | 暖光：bg→glow(#6e4521) 6 级 | 6 |
| 0xAE–0xDF | 预留（以后的令牌、应用） | 50 |
| 0xE0 | **哨兵**：`0x07E0`、`0xFFE0` | 1 |
| 0xE1–0xFE | 预留 | 30 |
| 0xFF | **哨兵**：`0xFFFF`、`0x07FF` | 1 |

用到约 130 项 [算]（示意图注册了 115 个名字，去重后 92 个 RGB565 颜色 [测 build_all.py]）。没用的项一律洋红，所以任何"画错索引"都是洋红。

### 2.3 派生项

- **圆角和 ● 的过渡色**：每对（填充色, 下面的颜色）一级 50% 混色，表 `kAa[24][24]`（576 B）。主题实际用到的配对（示意图里约 29 对）各有一项精确色；其他配对映射到最近的已有项，永远不是哨兵。所以第三方应用随便组合也不会出错色，只是边缘没那么圆。
- **数字渐变**：Nunito 数字有 6 级灰度（0–5）：0 不画，5 画 `Fg`，1–4 用渐变的四项。只烘焙两对：Fg 在 Bg 上（录音计时）、Fg 在 Sheet 上（锁屏时钟）。四项索引连续，以后真要换成 VLW 字体也能直接用 [源 research B §1]。在别的底上画数字时降级为"灰度 ≥3 画 Fg、否则不画"，不会出错色。
- **压暗**（Tab 菜单下面的页面）：每个令牌和 Mando 色一个压暗副本，等于原色向 bg 混 62%（和示意图同值）。查表 `kDim[256]`（256 B），其他项（过渡色、渐变）映射到最近的压暗副本，`dim(bg) = bg`。不用棋盘格抖动。
- **暖光**（锁屏卡外的一角）：bg→glow 6 级加 4×4 Bayer 有序抖动，只画在锁屏卡外、仍是 bg 的像素上，静止不动。
- **REC 呼吸**：`RecDot` 和 `RecDotEdge` 两项，4 步 × 200 ms：

| 步 | RecDot | 在 bg 上 | RecDotEdge |
|---|---|---|---|
| 0 | #f4675b（rec，100%） | 6.46:1 | 和 bg 各半 |
| 1 | #e05f54 | 5.64:1 | 同上 |
| 2 | #cb574c（约 83%） | 4.66:1 | 同上 |
| 3 | 同第 1 步 | 5.64:1 | 同上 |

  每个周期 0.8 s（约 1.25 Hz），从不变暗到看不见。背光调暗、熄屏、菜单打开时停在第 0 步。只改这两项色板，然后推大 ● 所在的 12×12 区域。

### 2.4 尺寸

| 名字 | 值 | 用在 |
|---|---|---|
| `kGutter` | 4 px | 屏幕两侧 |
| `kCapH` | 12 px，圆角 2–3 | 键帽、状态栏胶囊、看板计数胶囊 |
| `kSignH` | 13 px，圆角 = 高的一半 | Mando 的牌子、锁屏卡的小胶囊 |
| 格子 | 高 16、行距 19、圆角 5 | 主页 |
| 卡片 | 圆角 5（主页等你卡）/ 6（看板） | 卡片 |
| 抽屉 | 圆角 8，只圆左边两角 | Tab 菜单 |
| 锁屏卡 | 圆角 10 | 锁屏 |
| live-words 胶囊 | 高 14，圆角 7 | "存到 未归属" |

- 字体全是 efontCN_12，不加中文字库（flash +0 B）。页面标题用假粗体：在 x 和 x+1 各画一次。
- 大数字是 Nunito ExtraBold 800，**等宽单元**（WhisperWoof 的规则：数字用 tabular），计时器走字时不跳：计时 30 px，单元 17、冒号 7、高 21；锁屏时钟 34 px，单元 20、冒号 8、高 24 [算 示意图实测字形 16×21 / 19×24]。只有 `0-9` 和 `:`。

### 2.5 对比度规则（测试就是验收）

- 要读的字 ≥ 4.5:1，正文（ink）≥ 7:1。`test_theme_palette` 列出主题实际用到的每一对（字色, 底色），逐对断言。
- 选中底（Select、Accent）上只用 `Fg`、`Ink2`、`OnAccent`；唯一的例外是表示不可用的灰字（`Dim`）。
- 呼吸的每一步在 bg 上 ≥ 4.5:1；状态栏的 ● 在 RecBg 上 ≥ 4.5:1。
- 等你不只靠颜色：永远带 "!"、色条或"等你"。
- 这些是屏幕上的算法值。43% 和 10.5% 背光下实际看得清不清，要真机看（👤，§11 第 1 条）。

## 3. 画布和显示接口的变化

### 3.1 设备：调色板画布

`M5Display::begin()` 只改这几行 [源 m5_display.cpp:46-55；research B §1]：

```cpp
sprite_.setColorDepth(lgfx::palette_8bit);
if (!sprite_.createSprite(kScreenW, kScreenH)) return false;   // 32,400 B，和今天一样
sprite_.createPalette(theme::kPalette888, 256);                // +768 B 堆
raster_ = theme::Raster{static_cast<uint8_t*>(sprite_.getBuffer()), kScreenW, kScreenH};
```

- M5GFX 在调色板精灵上把颜色参数截成 `c & 0xFF` 当索引，文字颜色也存原始值 [源 research B，colortype.hpp:775、LGFXBase.hpp:692-703]。所以文字用 `setTextColor(uint32_t(fg_index), uint32_t(bg_index))`。[待 oracle，S0 加用例]
- M5GFX 的运行时抗锯齿（`fillSmooth*`、`drawWideLine`、`pushAlphaImage`）在调色板画布上会出错色 [源 research B]，一律不用。圆角、● 和数字都用预先烘好的色板项。
- 非文字的画法直接写 `getBuffer()` 指向的 8 位缓冲区（一字节一像素、行距 240）。S0 在 `begin()` 里断言 `bufferLength() == 240×135`。[待]
- 推屏时每个像素查色板转成 RGB565，时间不变，整屏约 13 ms，受线速限制 [源 research B §4]。

### 3.2 只有令牌：传不进 RGB565 常量

- **编译期**：`Canvas` 的每个调用只收 `enum class Color`，整数传不进来；`canvas.h` 里 `static_assert(!std::is_convertible<uint16_t, Color>::value)`。设备上的 `M5Canvas` 藏在 hal 内部的 `PaletteSprite` 包装里，作为私有成员；包装只有收 `PalIndex` 的方法，`PalIndex` 只能由 `theme::index_of(Color)` 或主题内部的表构造。`M5Display` 自己也拿不到裸的 `fillRect(…, 0xF800)`。
- **运行时**：§2.2 的哨兵。`test_theme_palette` 断言每个令牌落在非哨兵的索引、颜色和表一致；模拟器的 `test_theme_sim` 断言 `v04_tour` 每一帧都没有哨兵索引。超出令牌范围的值（`static_cast<Color>(24)` 以上）`index_of` 给哨兵 0x00，画出来是洋红，不会冒充 Bg（评审 2026-10-22）。
- **显式转换也挡住**：`static_cast<Color>(0xF800)` 会截成 0x00，正好是 Bg，`0x8410` 截成 0x10，正好是 Accent2，哨兵抓不到。所以 ci.yml 的 include 规则那一步再 grep 一次：`lib`、`modules`、`src` 里除了 `lib/theme`，不许出现 `static_cast<Color>`、`(Color)x`、`Color{…}`。
- **一份表**：今天的颜色表有两份，hal 的 `rgb565()` 和 sim 的 `rgb565_of()`，靠 `-Werror=switch` 对齐 [源 sim/README "Code the simulator repeats"]。以后只有生成器写出的 `theme::kPalette888` 这一份。

### 3.3 `Canvas` 新原语（`lib/module_api/canvas.h`）

原来的四个调用不变。新调用都有默认实现，v0.4 的 `Canvas`（比如别人写的测试替身）照样能编译，只是退化成方角、没有图：

```cpp
constexpr uint8_t kCornerTL = 1, kCornerTR = 2, kCornerBL = 4, kCornerBR = 8, kCornersAll = 15;
enum class ArtId : uint8_t;                       // lib/theme/art.h（生成的帧列表）
enum class DigitFont : uint8_t { Timer, Clock };  // Nunito 800，30 px / 34 px，等宽

class Canvas {
 public:
  // v0.1：fill、text、textWidth、lineHeight（不变）
  // v0.5（docs/19 §3.3）。默认实现：方角、文字代替数字、不画图、不压暗。
  virtual void fillRound(const Rect& r, uint8_t radius, Color paint, Color under,
                         uint8_t corners = kCornersAll);       // 圆角：一级过渡色
  virtual void dot(int16_t cx, int16_t cy, uint8_t diameter, Color paint, Color under);
  virtual void art(int16_t x, int16_t y, ArtId id, bool dimmed = false);  // 4 位图，0 透明
  virtual int16_t digits(int16_t x, int16_t y, const char* text, DigitFont f, Color ink,
                         Color under);                          // 只画 0-9 和 :，返回宽度
  virtual int16_t digitsWidth(const char* text, DigitFont f) const;
  virtual void dim(const Rect& r);    // 把 r 里每个像素换成它的压暗副本（Tab 菜单下面）
  virtual void glow(const Rect& r);   // 锁屏的暖光：只改 r 里仍是 Bg 的像素
};
```

- 实现只有一份：`theme::RasterCanvas`（`lib/theme/raster_canvas.*`）在 `Raster`（指针、宽、高）上实现以上全部和 `fill()`，`text()` / `textWidth()` 留给子类。`M5Display` 和模拟器的 `FbCanvas` 都继承它，各自只实现文字。
- `FakeCanvas`（native 测试）把新调用记成 `ShapeOp`（类型、矩形、颜色、`ArtId`、文本），测试可以断言"画了一张卡""Mando 在 (7,14)"。`digits()` 同时记一条 `TextOp`，所以 `has("0:12")` 仍然能找到计时。

### 3.4 `Display`（`lib/shell/platform.h`）

```cpp
class Display {
 public:
  virtual Canvas& canvas() = 0;
  virtual void present() = 0;                                    // 整帧（不变）
  // v0.5：只推 r（裁到屏内）。默认推整帧。
  virtual void present(const Rect& r) { (void)r; present(); }
  // v0.5：REC 呼吸到第 step 步（0-3，0 = 全亮）。只改 RecDot / RecDotEdge 两项色板；
  // 调用方接着推显示大 ● 的区域。默认什么都不做。
  virtual void breathe(uint8_t step) { (void)step; }
};
```

- 设备：`present(r)` 是 `Display.setClipRect(r); sprite_.pushSprite(0, 0); Display.clearClipRect();`，pushImage 在目标端裁剪，只传可见部分 [源 research B §4，LGFXBase.cpp:1435-1453]。`breathe()` 用 `setPaletteColor()`。
- 子类只重写一个 `present` 时要 `using Display::present;`；Runtime 总是通过 `Display&` 调用，不受影响。

### 3.5 渲染循环和动画 tick

今天：脏了就整屏重画、整屏推，最多 20 fps，另外每秒一次 [源 runtime.cpp:220-230]。录音电平每 128 ms 一块，`mirror_rec` 每块都置脏，所以录音时每秒约 8 次整屏推，约 104 ms/s [源 research B §0]。

以后分成两条路：

```cpp
void Runtime::render(uint32_t now) {
  const bool due = shell_.dirty() && now - last_render_ >= kRenderMinMs;
  const bool stale = !rendered_ || now - last_render_ >= kRenderEveryMs;
  if (due || stale) return render_full(now);       // 和 v0.4.0 一样：整屏画、整屏推
  if (level_ != Backlight::On) return;             // 调暗、熄屏：什么都不动
  const AnimFrame f = shell_.animate(disp_.canvas(), now);  // 只重画到点的区域
  if (f.breath >= 0) disp_.breathe(static_cast<uint8_t>(f.breath));
  for (uint8_t i = 0; i < f.n; ++i) disp_.present(f.rects[i]);
}
```

规则：

1. **动画只在 `Backlight::On` 时走**。`sync_backlight()` 把等级告诉 Shell（`shell_.set_backlight(lv)`）；离开 On 时 `disp_.breathe(0)`，● 回到全亮。
2. **动画不置脏、不碰电源**：`animate()` 只重画几块小区域并返回要推的矩形（最多 4 块）。它不会让屏幕多亮一秒，也不会在熄屏时画东西。
3. **整屏重画只画"冻结"的样子**：Mando 的姿势、呼吸的步数、波形和时间轴都存在 Shell（或模块）的动画状态里，只有 `animate()` 推进它们。所以调暗时每秒那次整屏刷新只更新计时数字，姿势和波形不动。
4. **电平不再触发整屏重画**：`Runtime::rec_event` 收到 `EvtKind::Level` 时调 `shell_.on_level(level)` 记进电平历史（纯函数，不置脏）；`mirror_rec` 发现只有 `level` 变了时，更新 AppState 但不置脏。历史在调暗、熄屏时也照记，会议时间轴不会缺一段。
5. **渲染仍在 loop 任务里**，音频任务的优先级和时序不动。最长的一步还是整屏重画（画约 3–5 ms + 推约 13 ms），远低于每步 50 ms 的上限 [算]。
6. 节奏：Mando、波形、呼吸每 200 ms 一次（`kAnimTickMs`）；锁屏的 z 每 600 ms；抽屉和分段滑块每 50 ms 一帧、共 3 帧。中途来了按键（整屏重画）就直接跳到终点。

Shell 新增（`lib/shell/shell.h`）：

```cpp
struct AnimFrame { uint8_t n = 0; Rect rects[4]; int8_t breath = -1; };  // breath -1：不变
void set_backlight(Backlight lv);              // Runtime::sync_backlight
void on_level(uint16_t rms);                   // 每个音频块（Runtime::rec_event）
void moment(Moment m, uint32_t now);           // 存好了 / 没听到（§4.3）
AnimFrame animate(Canvas& c, uint32_t now);    // 只在 On 时由 Runtime 调用
```

`stack_budget.py` 加一条链：`loop → Runtime::render → Shell::animate`。

### 3.6 模拟器（`firmware/sim/lib/simfb`）

- `Frame` 仍是 8 位，但存的是色板索引，不再是 RGB332。`rgb565_of`、`rgb332_of`、`panel_rgb` 删掉，换成 `theme::kPalette888` 的 RGB565 显示值。
- `FbCanvas` 继承 `theme::RasterCanvas`，只保留 efont 文字（`walk` 不变，传的是索引）。
- `SimDisplay` 持有一份色板副本：`breathe()` 改副本，`present(r)` 只把 r 从精灵拷到面板，`present()` 拷整帧。PNG 用"面板帧 + 当时的色板"写，仍是 8 位索引 PNG（`simpng` 不用改格式）。
- 局部推屏加色板变化在模拟器里也忠实，前提是 `RecDot` 的像素只出现在会被推的大 ● 区域里。`test_theme_sim` 检查这一点。
- oracle（`oracle/oracle.sh`）加调色板用例：M5GFX 的 `palette_8bit` 精灵加同一份 `RasterCanvas` 写缓冲区，和 `FbCanvas` 逐字节比。`test_efont/oracle_golden.h` 只从 oracle 重新生成。[待：本机要有 SDL2]
- 背光仍不模拟亮度：调暗、熄屏的截图是全亮像素，`shots.json` 写着等级（和今天一样）。

## 4. 公开主题接口（第三方应用用）

### 4.1 `lib/theme/theme.h`

纯代码，不 include Arduino / M5 / esp（加进 ci.yml 的 include 规则）。全部画在 `Canvas&` 上，所以应用、外壳、`FakeCanvas` 都能用。

```cpp
#pragma once
#include "canvas.h"

namespace cpa {
namespace theme {

// 尺寸（docs/19 §2.4）
constexpr int16_t kGutter = 4;
constexpr int16_t kCapH = 12;
constexpr int16_t kSignH = 13;
constexpr uint8_t kRadiusTile = 5;
constexpr uint8_t kRadiusCard = 6;
constexpr uint8_t kRadiusSheet = 8;

// 一种语气 = 一对（底色, 字色）。每对都 ≥ 4.5:1（test_theme_tokens）。
enum class Tone : uint8_t { Neutral, Info, Ok, Warn, Rec, Accent };
struct ToneInk { Color fill; Color ink; };
ToneInk tone_ink(Tone t);  // Neutral Raised/Ink2 · Info InfoBg/Accent · Ok OkBg/Ok ·
                           // Warn WarnBg/Warn · Rec RecBg/Rec · Accent Accent/OnAccent

// 卡片：圆角填充加一条更亮的 1 px 顶边（光靠填充在面板上看不见）。
enum class CardKind : uint8_t { Plain, Selected, Sheet };  // Card+CardRim · Select+Live 描边 · Card+Rim
Color card_fill(CardKind k);                               // 卡里的字画在这个颜色上
void card(Canvas& c, const Rect& r, CardKind k = CardKind::Plain, Color under = Color::Bg,
          uint8_t radius = kRadiusCard);

// 键帽 [Enter]：Cap 底、Ink2 字（G0 用 Accent）。返回右边的 x。
int16_t keycap_width(const Canvas& c, const char* key);
int16_t keycap(Canvas& c, int16_t x, int16_t y, const char* key, Color under = Color::Bg,
               Color ink = Color::Ink2, Color cap = Color::Cap);

// 胶囊：跑1、!2、等你 1。返回宽度。数量为 0 时调用方画普通 Ink3 字。
int16_t pill(Canvas& c, int16_t x, int16_t y, const char* text, Tone t, Color under,
             int16_t h = kCapH);

// Mando 的牌子：以 xc 为中心的胶囊，高 13。返回它的矩形。
Rect sign(Canvas& c, int16_t xc, int16_t y, const char* text, Tone t, Color under = Color::Bg);

// 等你的卡左边的竖色条（3 px 宽的胶囊）。
void stripe(Canvas& c, int16_t x, int16_t y, int16_t h, Tone t, Color under);

// 页面标题：假粗体（x 和 x+1 各画一次）。返回宽度。
int16_t title(Canvas& c, int16_t x, int16_t y, const char* utf8, Color under = Color::Bg);

// WhisperWoof 的 live-words：还没落地的东西（存到 未归属）。Select 底、Live 描边、Accent 字。
int16_t live_capsule(Canvas& c, int16_t x, int16_t y, const char* text, Color under);

// Mando 头上的 "!"（等你）或 "?"（没听到）气泡；step 0 低 2 px，1 到位（两步弹出）。
void bubble(Canvas& c, int16_t x, int16_t y, char mark, Tone t, Color under, uint8_t step);

// 主页格子（kHomeTiles 和 docs/16 的 AppTile 都用它）。
struct TileSpec { const char* key; const char* label; const char* badge; Color badge_ink; };
void tile(Canvas& c, const Rect& r, const TileSpec& t, bool selected);

// 提示栏：把模块的 hint() 字符串拆成"键帽 + 说明"（§4.2）；放不下时退回 v0.4 的一行字。
// hint_fits 就是这条规则（docs/16 的 check_apps 用它查应用的提示）。
void hint_bar(Canvas& c, const char* hint);
bool hint_fits(const Canvas& c, const char* hint);
// 同样的写法画在别处（确认卡里的 "Enter 开始    ` 取消"）：键是键帽，说明用 ink。返回宽度。
int16_t keys_text(Canvas& c, int16_t x, int16_t y, const char* text, Color ink, Color under);
// 提示行上的 toast：整行胶囊，语气色。
void toast_bar(Canvas& c, const char* text, Tone t);

// Mando（§7）
enum class MandoSize : uint8_t { M46, M64 };           // 26×46 / 36×64
enum class MandoAct : uint8_t { Idle, Listen, Tilt, Sniff, NeedsYou, Hop, NoVoice, Doze };
Rect mando_box(MandoSize s, int16_t x, int16_t y);      // 含气泡
Rect mando(Canvas& c, int16_t x, int16_t y, MandoSize s, MandoAct a, uint8_t pose,
           Color under = Color::Bg);                    // 画 sprite（和气泡），返回画到的区域
const char* mando_sign(MandoAct a);   // 在呢 在听 在听 嗅一嗅… 等你 存好了 没听到 打盹中
Tone mando_sign_tone(MandoAct a);     // Info Info Info Neutral Warn Ok Neutral Neutral

}  // namespace theme
}  // namespace cpa
```

### 4.2 提示栏的键帽规则（`lib/theme/hint.*`，纯函数）

模块的 `hint()` 字符串**一个字都不改**（docs/16 的 `expect_app_contract` 要求以"Tab菜单"结尾，测试也查原文）。主题按下面的规则把它拆开画：

1. 按空格切成词，连续空格只是分组。
2. 一个词开头的可见 ASCII（`!` 到 `~`）是键，剩下的是说明：`Enter打开` → [Enter] 打开；`;.选择` → [;.] 选择；`Opt+1-9项目` → [Opt+1-9] 项目；`Enter开session` → [Enter] 开session（空格会把说明切成下一个词，所以提示栏里写 `开session`；别处按 docs/20 U22 写"开 session"）。
3. 键后面没有说明、下一个词以汉字开头时，下一个词就是说明：`Enter 确认` → [Enter] 确认；`` ` 取消`` → 键帽 `` ` `` 加说明“取消”。
4. 连续的纯 ASCII 词并成一个键：`Opt N/Q/S 类型` → [Opt N/Q/S] 类型。
5. 以汉字开头的词是普通说明（Ink3），`退格`、`空格` 两个汉字键名例外。
6. `G0` 的键帽字是 Accent，其余是 Ink2，说明是 Ink3。单个字符的键（`` ` ``、`0`）键帽至少 12 px 宽，和菜单一样，否则 `` ` `` 看着像一个污点。
7. 排版：从 x=4 开始，项间距 8；放不下时间距 4；再放不下用 x=1、间距 3；还放不下就退回 v0.4 的画法（一行 Ink3，`draw_fit`）。`hint_fits` 说的就是前三档里有没有放得下的。外壳和模块的提示都应该在第一档放下（`test_mod_board` 查），挤到后两档的看着拥挤，要删一项（Tab 菜单里都还有）。

### 4.3 模块和 AppState 的新增（都有默认值）

```cpp
// module.h
// v0.5（docs/19 §3.5、§7）：上次调用以来这个页面上动了什么，画进 c，返回要推的矩形
// （w == 0：没有）。Shell 每 kAnimTickMs 调一次，只在背光 On、页面上面没有浮层和
// 菜单时调。不能置脏。默认什么都不动。
virtual Rect animate(Canvas& c, const Rect& area, uint32_t now) {
  (void)c; (void)area; (void)now;
  return Rect{};
}

// app_state.h：UiFlags 多两个字段（+8 B），Shell 在存好了 / 丢弃时写入
enum class Moment : uint8_t { None, Saved, NoVoice };
struct UiFlags { bool menu = false; bool locked = false; Moment moment = Moment::None; uint32_t moment_ms = 0; };
```

### 4.4 给应用的规矩

- 只用令牌和 `theme.h`，不要自己挑颜色；每对（字色, 底色）都要在 §2.5 的表里，`check_apps.py` 以后可以查。
- 要读的字用 `Fg`、`Ink2` 或 `Dim`（ink3）；`Dim` 不放在 `Select` 或 `Accent` 上（表示不可用的灰字除外）。
- 只有状态变了才动（WhisperWoof 的规则）。`animate()` 返回的区域要小（Mando 框那么大）；不要整页动。
- REC 的颜色（`Error` / `RecDot`）只给录音和真正的错误用；`Warn` 只给"等你"。
- 合并 `feat/app-sdk` 时：`AppTile.badge_fg` 本来就是 `Color`，换上暖色即可；主页多出的第四行用 `theme::tile()` 画，排版见 §6.3 的"四行版"（`home_grid(格子数)`）；`tools/apps/app_rules.py` 允许的 include 加上 `theme`，否则应用 include `theme.h` 会被 `check_apps` 拒掉；`check_apps` 的提示宽度改用 `theme::hint_fits`（按纯文字算 240 px 放得下，画成键帽后可能放不下，会退回灰字）。见 §13。

## 5. 素材管线（`tools/theme/`）

### 5.1 文件

| 文件 | 做什么 |
|---|---|
| `tokens.py` | 唯一的令牌表：名字、枚举顺序、色值、色板布局、哨兵、要精确烘焙的过渡配对、压暗系数 0.62、呼吸四步 |
| `gen_palette.py` | 用 `tokens.py` 和 `mando_colours.json` 生成 256 项色板、令牌索引表、`kAa[24][24]`、`kDim[256]`、两条数字渐变、暖光、呼吸表 → `lib/theme/gen/palette_gen.h` |
| `gen_mando.py` | **只读** WhisperWoof 的 `website/mando/hd/{wait,think,review,hop}.webp`：所有帧裁同一个框（各动作所有帧的并集，规格记为 (82,12,302,404)），缩到 46 和 64 px，边缘预混在 Sheet 色上，覆盖不到 30% 的像素算透明，用 64 px 全部帧做一次 15 色中位切分，两种尺寸共用。挑出 §7.1 的关键帧，打包成 4 位（高半字节在前）→ `gen/mando46.h`、`gen/mando64.h`、`gen/art_ids.h`，并写 `tools/theme/mando_colours.json`（提交，这样只重做色板时不需要 WhisperWoof） |
| `gen_icons.py` | 手画的小图：状态栏 11×9 的头（只用 Mando 色）、打盹的眼皮（wait 第 0 帧上改 6 个像素）→ `gen/icons.h` |
| `gen_digits.py` | **只读** `src/assets/fonts/Nunito-normal-latin.woff2`：weight 800，30 和 34 px，`0-9` 和 `:`，每个字形放进等宽单元，灰度量化成 0–5，每像素 4 位 → `gen/digits_gen.h` |
| `gen_all.py` | 依次跑上面四个。`--whisperwoof PATH`；`--check` 只比较不写，有差异就退出 1；`--palette-only` 不需要 WhisperWoof |
| `sources.json` | 每个输入的路径和 sha256、Pillow 版本，也写进每个生成头文件的开头注释 |
| `tests/` | pytest：色板 ≤256、哨兵没被占、每个令牌都有项、白名单配对都有精确项、`kDim` 完整、打包和解包互逆、两次生成逐字节相同、`--check` 能发现改动 |
| `README.md` | 怎么重新生成；许可证 |

- 生成的头文件每个都不超过 400 行（每行 48 字节，按尺寸分文件），`check_sizes` 照常检查。数组是 `const`，进 flash，不占静态 RAM。
- 设备上不解码 PNG 或 WebP（pngle 每次至少要 32 KB）[源 research B §3]，也不从 SD 卡读图或字体（录音时卡归音频任务）。

### 5.2 重新生成

```sh
uv run --with pillow --with fonttools --with brotli python tools/theme/gen_all.py \
    --whisperwoof <h3qing/WhisperWoof 的本地克隆>
git diff --stat firmware/app/lib/theme/gen tools/theme/mando_colours.json
(cd firmware/app && pio test -e native -f 'core/test_theme_*')
```

- Pillow 读 WebP 动画靠自带的 libwebp；读 woff2 要 FreeType 带 brotli，否则 `gen_digits.py` 先用 fontTools 解成 TTF（在临时目录，不写 WhisperWoof）。[待：在 Lead 的机器上确认]
- CI 没有 WhisperWoof：`repo` 任务只跑 `pytest tools/theme`、`gen_all.py --check --palette-only`、ruff、`check_sizes`、`lizard -L 50`。sprite 和数字以提交的头文件为准。
- WhisperWoof 的两个目录（主仓库和 `cardputer-inbox` worktree）里这几个文件逐字节相同 [测 cmp]，任选一个都行。

### 5.3 来源和许可证

- **WhisperWoof**（MIT，Copyright (c) 2026 Heqing Huang，和本仓库同一作者）：Mando sprite 帧和深色设计令牌。NOTICE 加一节：来源文件、sha256、转换方式（裁剪、缩放、15 色、4 位）。
- **Nunito**（SIL OFL 1.1，Copyright 2014 The Nunito Project Authors，没有保留字体名）：只烘焙了 11 个字形的位图。OFL 全文从 WhisperWoof 的 `src/assets/fonts/OFL.txt` 拷到 `LICENSES/OFL-1.1.txt`，NOTICE 写明来源和用途。
- 生成的头文件开头写来源、许可证和"不要手改，用 tools/theme/gen_all.py 重新生成"。

## 6. 每一屏的布局（对齐示意图）

坐标来自示意图脚本 [源 companion/src/s_*.py、lib.py]。"S1/S2/S3"是这部分在哪个阶段做完。

### 6.1 状态栏（y 0–11，除锁屏外每页都有）

| 位置 | 内容 | 颜色 |
|---|---|---|
| x=1 | **录音时**：REC 胶囊，宽 = 字宽 + 6、高 12，`RecBg`；字在 x=4：`● 12:48`（● 和计时之间空一格，计时从 x=16 起，同示意图 04–05；v0.4.0 是 `●12:48`，一次画字） | 字 `Rec`，**不呼吸** |
| 接着（+4，或 x=3） | 链路：在线 → Mando 头 11×9（y=1）；同步中 → 头 + `◎`（Accent）；`◇ 无响应`（Warn）；`◇ 版本不兼容`（Error）；`○ 离线`（Ink3） | |
| 接着 +6 | `↑N` | N=0 Ink3，否则 Ink2 |
| 接着 +6 | `!N`，**只在 N>0 时**：芥末胶囊，宽 = 字宽 + 6 | Warn / WarnBg |
| 接着 +4 | `录N`，只在 N>0 时 | Ink2 |
| 右对齐 236 | 电量 `94%` | Ink3；低于 10% 是 Error |
| 向左 5 | 时间 `09:41` | Ink2 |
| 向左 4 | 星期 `周三`（取自 `lock_date_text`；时间未知或放不下时不画） | Ink3 |
| 再向左 6 | `※kN`（键盘告警，不为 0 时） | Warn |

- 在线的 "● 在线" 换成小狗头。所以不录音时状态栏上不再有 "●"，`test_rec_visible` 查到的 ● 只可能是 REC 的，更严格了。
- 放不下时先省星期（纯函数排版，测试最长的组合：版本不兼容、↑200、!2、※k9）。

### 6.2 提示栏和 toast（y 123–134）

- 提示栏：§4.2 的键帽画法。键帽高 12、圆角 2、`Cap` 底；说明在键帽右边 3 px。
- toast：整行胶囊 (2, 123, 236, 12)，Info → InfoBg/Accent，Ok → OkBg/Ok，Warn → WarnBg/Warn，Error → RecBg/Rec；字从 x=8 开始，宽 224 px（`kToastTextW`），`draw_fit`；固定的提示都按这个宽度写，`test_text_width` 用 `toast_bar` 画出来查（docs/20 B6）。规则不变：一次一条、2.5 s，锁屏时不显示 [源 toast.h、shell_render.cpp]。

### 6.3 主页（示意图 01，S1 换样式，S2 完成）

```
[头] ↑0 [!1]                    周三 09:41 94%
 ▟▙  ! 早上好，1 件事在等你
 ▛▜  ┌─────────────────────────────┐
 ▐▌  ┃ 支付回调重试        9 分钟  │
 ▐▌  ┃ acmeshop · 等你回复         │
     └─────────────────────────────┘
[P]项目 跑2   [I]等你   !1   [R]开会
[N]记一条     [Q]排队给 AI   [S]开 session
[M]系统       [L]锁屏        [Tab]菜单
[G0]按住说一段  [;.,/]移动  [Enter]打开
```

| 元素 | 位置 | 说明 |
|---|---|---|
| Mando | 46 px，(7,14)，26×46 | §7 的状态；等你时气泡在 (31,14)，9×11 圆角 4，加两像素的尾巴 |
| 问候 | (44,15) | `早上好，` Ink ＋ `1 件事` Warn ＋ `在等你` Ink。5–11 点早上好，11–13 中午好，13–18 下午好，其余晚上好，时间未知时"你好"。没有等你的事："早上好，都处理完了"。Mac 还没发来过 session 快照（`sessions.live` 为假，Mando 在嗅）：`你好，` 接 `还没连上 Mac`（Ink3），不说"都处理完了" |
| 等你卡 | (42,30,194,28)，Plain，圆角 5 | 芥末色条 (46,35,3,18)；标题（session 名）(54,32) Ink，截到龄期左边；龄期右对齐到 230，Warn，写法是全机唯一的年龄函数 `age_text`（docs/20 U22，四舍五入）：`9 分钟` / `2 小时` / `3 天`（示意图写 `9 分`；列表行右边是紧凑的 `9分钟`）；第二行 (54,45) Ink3：`项目键 · 等你回复` [设]。没有等你的事时卡里只有 `没有等你的 session`（Ink3），没有色条；还没有快照时是 `嗅一嗅… Mac 没响应` |
| 格子 | 三列 x = 4 / 83 / 161，宽 75 / 74 / 75；y0 = 64，高 16，行距 19 | 选中：Accent 填充、圆角 5，键帽 Accent2、字 OnAccent。没选中：不填充，键帽 Cap / Ink2，字 Ink。键帽在 x+2，字在键帽后 4 px，截到 x+w−4。角标右对齐到 x+w−4：`跑N` Ink3，`!N` Warn。没有角标、字又放不下时（`开 session` 60 px），字左移 3 px、右边放到 x+w−2，紧贴键帽 1 px、离圆角 2 px，整段画出 |
| 四行版 | docs/16 的应用格子 | `home_grid(格子数)`：超过 9 个格子时 y0 = 60（内容区里 48），格子高 14，行距 16，四行到 122 为止，等你卡（到 58）不动；格子里的键帽在 y+1（16 px 的格子里是 y+2） |
| 提示 | `G0按住说一段 ;.,/移动 Enter打开` | 改了：Tab 菜单已经是第 9 个格子，问候下面的"按住 G0 说一段"挪到这里。主页的提示不以"Tab菜单"结尾：加上它最紧也要约 246 px，放不下；主页的 Tab 提醒就是第 9 个格子（docs/13 §5.6 已注明） |

- 去掉"主页 · 10/21 周三"标题：星期在状态栏里。`home_date()` 和它的测试留着，当前没有调用方就删（Lead 决定）。

### 6.4 项目看板（示意图 02，S1）

| 元素 | 位置 | 说明 |
|---|---|---|
| 标题行 | (5,14) | `项目` 假粗体，后面空 6 px 接一句 Ink3：`2 个在跑，1 个在等你`。有横幅时这句换成横幅：版本不兼容（Error）、Mac 上未找到 Claude Code（Warn）、`更新于 5 分钟前`（Ink3） |
| 卡片 | x=4，w=232，h=28，y = 29 + 31k，圆角 6；一屏 3 张，光标始终可见 | 选中：Selected（Select 填充 + Live 描边）；其他：Plain |
| 编号键帽 | (x+5, y+8) | 选中 Accent / OnAccent，否则 Cap / Ink2 |
| 键名 | (x+23, y+2) | Ink；项目不可用时 Warn |
| 名字 | (x+23, y+14) | Ink3，选中时 Ink2 |
| 计数 | 从 x+w−5 往左，y+8，间距 3 | `跑N` Ok/OkBg，`等N` Warn/WarnBg，`队N` Ink2/Raised；0 时是普通的 Ink3 字；离线时 `跑--` |
| 其他 | 同样的卡，没有编号 | `其他`，第二行 `N 个 session 没归项目`，计数 `跑N 共N` |

- 提示：`Enter打开 Opt+1-9项目  Tab菜单`。`;.选择` 删了（Tab 菜单本页里有），否则只能挤到第三档间距，一直顶到 x=238。
- 项目页（会话 / 队列）和等你的列表（评审 2026-10-22）：行高 14，字在行内 y+1，左右各缩进 8（`kListX`），选中行是 14 px 的 Select 胶囊（原来 12 px，字贴着胶囊上下边）。页签写 `Sessions(N)`（合并 docs/20 时从 `会话(N)` 改回：session 全机只用一个名字，开 session 的确认框也写"先看 Sessions"，U22、U25）。队列的提示是 `Enter开session `返回  Tab菜单`（`Opt+D完成` 在 Tab 菜单里，放进提示栏就只能挤到第三档）。
- 离线（或 Mac 上没有 Claude）时队列里的项是灰字（Ink3）；选中的那一项改用 Ink2（Select 上的 Ink3 只有 3.5:1），右边加一个 `离线` 中性胶囊，说明为什么按了没反应。
- 等你：等着你的 session 不再在名字前写一个普通的 `!`，而是主页卡上那条 3 px 芥末色条（行内 (8, y+3, 3, 8)），名字从 x=15 起。空的时候画 Mando（46 px，Idle）和 `在呢` 牌子，右边 `都处理完了`（Ink）、`没有等你的 session`（Ink3）。

### 6.5 Tab 菜单（示意图 03，S3；S1 只把行换成键帽和选中胶囊）

| 元素 | 位置 | 说明 |
|---|---|---|
| 状态栏 | y 0–11 | 不压暗、不遮挡；录音时 ● 一直在 |
| 下面的页 | (0,12,240,111) | 照常画，然后 `dim()`；Mando 停在当时的姿势，也一起压暗 |
| 抽屉 | x=sx，y=13，w=240−sx，h=109，`Card`，圆角 8，只圆左边两角，下面是压暗的 bg | sx 默认 66（示意图）。这一页最宽的一行放不下时往左扩，最多到 sx=6，见下 |
| 顶边 | (sx+7, 13, w−7, 1) | `Rim` |
| 分段 | (sx+5, 17, w−9, 15) Bg 胶囊；滑块 Accent 胶囊，宽 = 一段，高 13，y=18 | 本页 / 去哪 / 控制；滑块里的字 OnAccent，其余 Ink2。浮层上只有两段 |
| 行 | y = 36 + 14k，一屏 6 行（原来 7 行） | 选中：Accent 胶囊，高 14。键帽 (x+2, y+1)，高 12、圆角 3、至少 12 宽（选中 Accent2 / OnAccent，否则 Cap / Ink2）；说明从 2 + 这一页**多数行**的键帽宽 + 5 开始（一个字母的键是 19，同示意图），更宽的键帽只推开它自己那一行（不超过 54），Ink / OnAccent。角标右对齐到行尾 −6：打开看板的行 `跑N`（Ink3），打开等你的行 `!N`（Warn），数是 0 不画；在选中的焦糖色行上一律 OnAccent（芥末在焦糖上只有 1.17:1）。`1-9` 这种只说明不执行的行：键帽 Raised、字 Ink3（4.55:1；Cap 上的 Ink3 只有 3.5:1）。控制页的事实（声音、电池、SD、Mac）不是键，不画键帽，就是 Ink3 字（Card 上 4.97:1） |
| 滚动条 | x=237，宽 2，轨道 `Line`、滑块 `Ink3` | 代替原来的 ↑ ↓ 标记 |
| 提示 | 每一页都是 `;.选 Enter做 ,/换页 Tab关`，画成键帽 | 去掉了 `字母直达`（示意图没有；有它提示栏只能挤到第二档间距）。字母照样直达（`menu_key`） |

- **抽屉宽度**：`sheet_x = clamp(240 − 8 − 最宽一行的需要, 6, 66)`（纯函数；需要 = 那一行说明的起点 + 说明 + 角标或 4 px）。说明里不再重复键帽上已经写着的键：去掉 `（按 1-9）`、`（按 Opt+1-9）`、`（按 0-9）` 这类尾巴（docs/13 §6.1 写的"行尾写按 1-9"在 v0.5 由键帽代替），控制页的锁屏行只写 `锁屏`（怎么解锁锁屏卡上写着）。这样本页、去哪、控制三页的抽屉都在 66 左右（控制页的 SD 行长时最多往左几 px），不再盖住几乎整页、让人找不到自己在哪。
- **滑入**：打开时 3 帧、每帧 50 ms，抽屉的 x 从 240 走到 sx（t = .45 / .85 / 1）。第 1 帧整屏推（压暗就在这一帧），第 2、3 帧只推抽屉矩形。关上时不滑，直接关。
- **滑块**：`,` `/` 换页时整屏重画一次（行变了），然后滑块 3 帧、每帧 50 ms，只推分段控件。
- 背光不是 On 时没有滑动，直接画终点。滑动中按键直接跳到终点。
- 去掉"菜单 · 会议录音"标题和 `[本页]` 方括号：分段滑块就表示在哪一页，抽屉后面也看得见原来的页。

### 6.6 语音便条（示意图 04，S2）

```
[●0:07] [头] ↑0 [!1]           周三 09:41 94%
  ▟▙      语音便条                 第 1 段
  ▛▜      ● 0:07          （Nunito 30 px）
  ▐▌      ┃┃·┃┃┃·┃┃┃┃┃·┃┃┃┃·┃┃┃┃┃·┃┃┃
  ▐▌
 (在听)
[1]acmeshop  ([2]cardputer)  [0]不归属       ← gh #53：1–9 和 0 各是什么，选中的亮着
[G0]松开结束  [1-9]选项目                    ← 0 不归属只在图例里写（评审：两处叠着像画错了）
```

| 元素 | 位置 | 说明 |
|---|---|---|
| Mando | 64 px，(10,19)，36×64 | 有声音：Listen（点头）；安静：Tilt（歪头）；§7 |
| 牌子 | 中心 x=28，y=87 | 录音中 `在听`（Info）；Cue 不画牌子；Finishing `保存中…`（Neutral，Mando 托腮） |
| 标签 | (60,18) `语音便条` Ink3；`第 N 段` Ink3 右对齐到 236，y=18 | |
| 大 ● | 中心 (66,44)，直径 9，`RecDot` | 推屏区 (60,38,12,12)，呼吸 |
| 计时 | (76,33)，Timer 字体，Ink 在 Bg 上 | `m:ss`（`rec_clock_text`） |
| 波形 | 35 根，x = 62 + 5k，3 px 宽，中心 y=75，半高 ≤11 | 最新的在右；每 3 根交替 Accent / Accent2；高 ≥5 时四角用过渡色修圆。区域 (62,63,174,25)。图例占三行时（约 5 个项目以上）中心 y=70、半高 ≤7、区域 (62,63,173,15)，35 根照旧 |
| 1–9 图例（gh #53） | 一行：(7,103)，在牌子下面；两行：(60,94) 和 (7,108)；三行：再加 (60,80)，波形变矮。从左边起的行从 x=7 开始，亮着的胶囊就从 x=4 起，和提示栏对齐 | 按数字键那一排的顺序：`[1] 项目键` … `[9] 项目键`，然后 `[0] 不归属`。数字是 10 px 宽的键帽，字 Ink（要对的就是数字，所以它最亮），空 2 px 接项目键，Ink2；项目之间 8 px（放不下时 6 px）。**选中的那个**画成原来"存到"的 live-words 胶囊：Select 底、Live 描边、键帽 Accent / OnAccent、字 Accent；按数字的那一刻（下一次整屏）就换过去。**按数字从不挪动图例**（评审 2026-10-22：6 个项目时按 8，图例从两行跳成三行、波形变矮、所有项目挪位）：只要 1–9 里有空着的数字，`[0]` 后面就留一个 `[9] ?` 的格子，算在排版里，只有选了没有项目的数字时才画出来并亮着；行数、截字和每一项的位置都和选了哪个无关（`test_rec_legend` 对 0–9 个项目、每个数字都核对）。显示项目键（ASCII，一个字母 6 px，协议上最多 12 个），不显示名字；放不下时所有键截到同一个宽度，一次少一个字母，截在 `-` 上就去掉它，`不归属` 不截；**截过的键后面画两个 1×1 的点**（基线上，占 3 px，算进排版），免得 `tax-202` 看起来像另一个完整的键（"…"会占一个中文字宽）。**行数：先找不用截字的最少行数**（评审让选：认清哪个项目比波形高度要紧——你的两个项目一行；三个 12 字母的键两行、整个显示；5–6 个项目三行、整个显示）；三行也放不下才退到每个键至少留 6 个字母的最少行数，再不行三行（9 个 12 字母的键：三行，每个 6 个字母加两个点）。原来的"存到 #项目键"那一行去掉了：亮着的那个就是去向；**选中的键被截了**，标题行的 `语音便条` 换成 `存到 #完整的键`（存到 Ink3，键 Accent），宽限和免提时那一行照旧是它们的提示。纯函数在 `rec_legend.h`（`legend_list`、`legend_fit`、`legend_plan`、`memo_legend`） |
| 提示 | `G0松开结束 1-9选项目`，画成键帽 [G0] 松开结束 [1-9] 选项目（示意图 04）；`0 不归属` 只在图例里 | 原来是 `松开 G0 结束  1-9 项目  0 不归属`；gh #53 的图例上来以后评审发现提示栏的 `[0] 不归属` 和图例的 `0 不归属` 上下叠着、x 几乎一样，像画错了，就从三种提示栏里都去掉了（免提的又写回完整的"选项目"） |
| REC | 计时右边 5 px，y=38，`REC`，Rec | 只在录着时（保存时不画）。v0.3.1 你选的提示是"看到 ● REC 再说话"，计时起来以后屏上要真有 ● 和 REC（评审 2026-10-22；另一个办法是改那句话，要你点头，没改） |

- **Cue**（麦克风还没起来）：不画大 ● 和计时，那里画两行原文：(60,36) `● 准备`（Warn），(60,52) `看到 ● REC 再说话`（Ink3）；Mando 是 Idle，不画牌子；状态栏是 `●0:00`。
- **Finishing**：计时停在最后的时间；波形留着最后那一刻，全部画成 `Live`（示意图生成器里"还没落地的字"的颜色），不留一个空洞；牌子 `保存中…`，提示栏空着（不再说两遍"保存中…"）。
- **电平**：`level_q` 是 dB 刻度：块的 RMS（高通以后的，见下）从 −60 dBFS（RMS 33，屋里的底噪，画成点）到 −24 dBFS（RMS 2,068，满高），36 dB 分 11 级，每级约 3.3 dB。`q = round(255 × clamp((20·log10(rms / 32768) + 60) / 36, 0, 1))`，每块算一次 `log10f`，每秒 8 次。
  - 原来是线性的（`255 × min(rms, 8192) / 8192`），在真机上不动。你 2026-10-22 的四段便条里，说话的块 RMS 是 155–1,083（中位 454，约 −37 dBFS），全都画成 3 px 的点 [测]。
  - **高通**（2026-10-22 第二次真机检查）：屋里的低频嗡嗡声（峰在 35 和 68 Hz，七成能量在 100 Hz 以下）有 −52 到 −57 dBFS，高过"有声音"的保持线（−56 dBFS，§7.1），停顿里 Mando 一直点头，没人说话也画 5 px 的柱子 [测]。现在块的 RMS 在高通之后算：`rec::level_of`（`lib/rec/level_hpf.*`），录音任务的 `RecEngine::write` 每块调一次。
    - 4 阶 Butterworth，130 Hz：两节 RBJ 高通 biquad（Q 0.5412 和 1.3066），float，转置直接 II 型。状态在同一段录音的块之间接着用（换段也接着），`RecEngine::start` 清零。35 Hz −45.6 dB，50 / 60 Hz −33 / −27 dB，68 Hz −22.5 dB，100 Hz −9.6 dB，150 Hz −1.2 dB，300 Hz 以上差不到 0.01 dB [算；`test_rec_level` 测 35–68 Hz ≥ 20 dB、300–3,400 Hz ±1 dB]。
    - 原定的一节 2 阶 120 Hz 在 68 Hz 只有 −10.3 dB（35 Hz −21.4 dB），够不到 20 dB；一节 2 阶要在 68 Hz 有 20 dB 得放到约 215 Hz，300 Hz 就掉 1 dB。所以用两节 [算]。
    - 只动电平：WAV 一个字节不改（`test_rec_level` 逐字节比）。
    - 合成的一段（`test_rec_level`）：−52 dBFS 的嗡嗡声（35 + 68 Hz 各一半，或全在 68 Hz）里说 2 s、停 2 s，三次。不高通时停顿的块是 5 px、保持住声音；高通以后停顿的第 2 整块声音就结束，停顿里每块都是点，说话时一直有声音 [测]。
    - 代价：每个样本两节 biquad（10 次乘加）加一次平方累加。主机上（Apple 芯片的 Mac，-O2）每块 2,048 个样本约 7 µs [测]。ESP32-S3 上（-Os，`xtensa-esp32s3-elf-g++` 反汇编）内循环约 69 条指令，估 70–110 cycle / 样本：每秒 1.1–1.8 M cycle，240 MHz 一个核的 0.5–0.75%，每块（128 ms）0.6–0.9 ms [算，待真机]。录音任务的栈：`level_of` 48 B，在写卡那条链返回以后才调，不叠在它上面；`RecEngine::write` 112 → 128 B（`.su`）。
  - 四段便条经过高通（同一份 WAV 在主机上过 `level_of`，`test/core/support/memo_levels.h` 的 `level`）：说话的块 136–1,056（中位 390，约 −38.5 dBFS），比原来低 0–3.3 dB（中位 0.9 dB，人声 150 Hz 附近的基音）；底噪 5–33 [测]。分类不变：180 个说话块都是 ≥ 9 px 的柱子、都算有声音，底噪都是点、离说话远的都不算，有声音一共切换 14 次；只有便条 0 结尾那块渐弱（RMS 55，高通后 52，正好 −56 dBFS）让声音早一块（128 ms）结束 [测]。
  - 现在说话占 4–9 级：第 5 百分位、中位、第 95 百分位、最大分别是 4、7、8、9；相邻两块平均差约 1.3 级。底噪仍是点；比最响的那块再响 5 dB 才到满高 [测]。
  - 每段录音的第一块算安静（q = 0）。那是麦克风启动时 codec 的振铃（四段都是 RMS 575–600，高通后 265–277，约 10 ms 衰减），不是你的声音；不算的话，开头会冒出一根 13 px 的柱子，Mando 也会点一下头。
  - 柱子不做起落平滑：每根已经是 128 ms 的 RMS。会议时间轴用同一个刻度（半高 ≤8），合并时取最大值照旧。
- 去掉 v0.4 的"● REC 0:07 → 未归属"和"第 1 段 · 电 94%"两行：● 和计时变成大字，去向变成胶囊，电量在状态栏里。
- **1–9 图例**（gh #53，来自你的一条便条的需求：说话的界面上，屏幕要显示 1 到 9 各是什么项目）：按住说、松开后的宽限、免提、Cue 和保存中都一直在；按住 G0 结束后的卡片盖住整页时不画。只由整屏重画画，动画 tick 不碰它：tick 只问它留下多少地方（`rec_room`：三行时波形变矮），所以 tick 推的波形区永远在图例上面。试过、没用的三种：一条会翻页的条（翻到别页时选中的看不见）、3×3 键盘格（项目少时一堆空格，会议页的时间轴没地方）、开始录和按数字后亮 2 秒的卡片（盖住刚开始的波形，想再看要多按一下）。

### 6.7 会议录音（示意图 05，S2）

| 元素 | 位置 | 说明 |
|---|---|---|
| 标题 | (6,15) `开会` 假粗体，空 5 px 接 `去向 · 第 N 段`，Ink3（`未归属 · 第 1 段`），截到 ★ 胶囊左边 4 px | 段号原来在时间轴下面居中，示意图那里没有，挪到标题行 |
| ★ 胶囊 | 高 14，右对齐到 236，y=14，Warn/WarnBg | 平时 `★N`；刚加星后的 600 ms（沿用 `kStarFlashMs`）是 `★ 已标记 12:31`（加星那一刻的计时） |
| Mando | 46 px，(10,31) | Listen / Tilt |
| 牌子 | 中心 x=23，y=79 | `在听` |
| 大 ● | 中心 (52,44)，直径 9 | 推屏区 (46,38,12,12) |
| 计时 | (62,33)，Timer 字体 | `mm:ss` |
| 时间轴 | x 48–236，中心 y=98 | 整场会议 94 格，半高 ≤8，`Live`，最新 7 格 Accent。**第一次合并以前**每格 1 px 宽、间距 2 px，从 x=48 往右长，`Rec` 末端圆点（直径 5）紧跟在最新一格后面 2 px，1 px `Live` 基线只画到圆点，后面不留空轨道；**合并以后**格子铺满整个 188 px：间距 = 188 / 格数，柱宽 = 间距 − 1，圆点在 (237,98)（评审：原来合并后柱子缩到一半宽，后面一条平轨道接一个钉在最右的圆点，像进度条）。★ 在加星那一格的中间：竖线 1×4 Warn (sx, 85)，`★` Warn (sx−2, 73)；离上一个画出来的 ★ 不到 8 px 的只画竖线（合并后挨得太近的星会糊成一团）。下面 y=109：左边 `0:00`，`现在` 右边对齐圆点外缘（+3），但不压到 `0:00`；都是 Ink3 |
| 1–9 图例（gh #53） | 一行：(48,59)，计时下面、★ 上面；放不下（每个键不到 5 个字母）就再用 (7,109)，这时时间轴下面的 `0:00` 和 `现在` 不画；它的胶囊离最响的柱子 1 px、离提示栏 1 px | 和便条页同一种画法和顺序，选中的亮着；标题行照旧写去向。会议页**不留** `?` 的格子：标题已经写着 `#8`，留格子会把你的两个项目截成 5 个字母；按没有项目的数字时图例里没有亮的，别的都不动。9 个长键时连 `[0] 不归属` 也放进去会让每个键不到 4 个字母，0 就只画一个光键帽 `[0]`（标题写着"未归属"，选 0 时它照样亮着），每个键 3 个字母加两个点，是最挤的情况。Cue 和结束确认框时不画（确认框会盖住它）。tick 不碰它：两行时时间轴的重画区只到柱子底下 (46,73,194,34)，`现在` 不挪 |
| 提示 | `G0加★ 1-9项目 Opt+R结束 Tab菜单` | 改了：原来的 `G0/空格★` 拆不成键帽；空格加星照旧，菜单本页里有 |

- **EndConfirm**：`结束录音？`（Warn）和 ``Enter 结束    ` 继续录`` 原文不变，放在 Plain 卡 (44,58,192,62) 里，那一行的键画成键帽（`keys_text`，和提示栏同一个字符串、同一种画法）；Mando 不动，● 和计时照常。
- **开会确认框**（录音之前）：文字**一字不改**，行的位置也不变（`开始会议录音？` Warn 在第 1 行 y=24，`请先告知在场的人` 第 3 行 y=48，``Enter 开始    ` 取消`` 第 6 行 y=84），底下垫一张 Plain 卡 (4,14,232,106)。第 6 行的 `Enter` 和 `` ` `` 画成键帽，字一个没改（字符串常量 `kMeetingConfirmKeys`，提示栏也用它，测试查原文）；卡右边 (190,40) 坐着 Mando（46 px，Idle，不画牌子），离字远远的，卡不再半空。提示不变。
- 时间轴按整场会议算：电平历史在熄屏和调暗时也照记（§3.5 第 4 条），格子满了两两合并，每格代表的时间翻倍。

### 6.8 锁屏（示意图 06，S1 卡片形状，S2 Mando、时钟、暖光）

| 元素 | 位置 | 说明 |
|---|---|---|
| 黑屏（卡没亮） | 全 Bg | 录音时左上角是 v0.4.0 那一行，字不变：`●42:10 ★2`（会议）/ `● REC 0:12`（便条）/ `● 准备`；放进状态栏 REC 槽一样的胶囊 (1,0,字宽+6,12)，字在 x=4：Rec 在 RecBg 上（4.81:1），准备是 Warn 在 WarnBg 上。卡亮着时暖光离这个胶囊至少 2 px（原来暖光里被挖出一块方方正正的黑，像花屏；Dim 时还是 x=0 的裸字） |
| 卡片 | (12,8,216,94)，Sheet，圆角 10；录音时下移到 y=14，不挡 ● 那一行 | 顶边 `Rim` (21,8,198,1) |
| 暖光 | 卡外、仍是 Bg 的像素 | 中心 (−6,−10)、半径 170、6 级加 Bayer，静止 |
| Mando | 46 px，(25,17)，打盹帧 | z 在 (47,22) Ink3、(53,15) Ink2、(59,7) `Z` Ink，每 600 ms 多亮一个，三个都亮以后从头来；只在卡亮着时动 |
| 牌子 | 中心 x=38，y=67，`打盹中`，Neutral | |
| 已锁屏 | (77,14) Ink3 | |
| 时钟 | (76,29)，Clock 字体，Ink 在 Sheet 上 | 时间未知时画普通字 `--:--`（Ink2） |
| 日期行 | (78,58) Ink2 | `10/21 周三 · 电 94%`（v0.4 的格式） |
| 小胶囊 | y=77，高 13，间距 4 | `等你 N` Warn、`待传 N` Neutral（N 是待传录音段数）；`↑N` Neutral，只在 N>0 时 |
| 提示 | 居中，y=108：[Opt+L] 再 [Enter] 解锁；y=122：先点 [G0] 再按住，说一段 | 开会时第二行是 `开会中：[G0] 加 ★`。录音时卡下移，两行改在 y=110 和 123 |

- 卡上仍然只有数量，没有项目名、session 名或任何正文（docs/13 §7.1）。

### 6.9 没有示意图的页面

S0 换颜色，S1 加卡片和选中胶囊。评审（2026-10-22）说这几页还是"工业风"，按同一套零件重排了，文字不变：

- **系统**：标题 `系统` 画在固件那一行的标签位置（假粗体，x=4），后面是固件版本；其余每行是"标签 / 值"：标签是这一行的第一个词，Ink3，x=4；值是后面的部分，Ink，x=46（最宽的标签 `outbox` 之后），多个空格并成一个；告警行（不是标签加值的一句话）从 x=4 整行画，Warn。`固件 / dev / 启动` 三行和下面之间有一条 1 px `Line` 分隔线，上下各空 1 px。9 行加分隔线正好到 y=122。`SystemPage` 多一个 `key_len`（标签的字节数，告警行是 0），行的文字不变。
- **编辑页（Compose）**：标题 `笔记 → acmeshop` 放进 (4,13) 的 Raised 中性胶囊；正文放在 Plain 卡 (4,28,232,92) 里，内边距 6，一行 36 个字符、5 行；字数 `22/500` 在卡里右下角，Ink3；"开头不能是 '-'" 在卡里左下角，Warn。
- **放弃确认**（Fn+`）：和开会确认用同一种卡 (4,28,232,92)，`放弃已输入的内容？` Warn 在 (12,40)，``Enter 放弃    ` 返回编辑`` 在 (12,72) 画成键帽，字不变。
- **等你、项目页**：见 §6.4 的最后几条。
- **执行确认框、迁移进度页**：只换颜色，没有新布局。

## 7. Mando 状态机

### 7.1 动作、帧和牌子

帧号是 WhisperWoof sprite 的帧序号（每个动作 24 帧，hop 15 帧，原来 80 ms 一帧）。设备上只取 4 个关键姿势，每个 200 ms，这是 Mando 素材包里写的、你偏好的节奏 [源 research A §4]。

| 动作 | 姿势（46 px / 64 px） | 节奏 | 牌子 | 气泡 |
|---|---|---|---|---|
| Idle 在呢 | wait 0 | 不动 | 在呢（Info） | — |
| Listen 在听（有声音） | review 0/6/12/18 | 循环 | 在听（Info） | — |
| Tilt 在听（没声音） | wait 0/6/12/18 | 循环；和 Listen 切换时不换相位 | 在听（Info） | — |
| Sniff 嗅一嗅… | think 0/6/12/18 | 循环，最多 8 s，然后停在第 0 个姿势 | 嗅一嗅…（Neutral）；保存时写"保存中…" | — |
| NeedsYou 等你 | wait 0（安静地坐着，示意图 01） | 不动；气泡两步弹出 | 等你（Warn） | `!` Warn |
| Hop 存好了 | hop 0/4/8/12 | 放一次（0.8 s），然后停在 wait 0 | 存好了（Ok） | — |
| NoVoice 没听到 | wait 12（最歪） | 不动；气泡两步弹出，2.5 s 后回到原状态 | 没听到（Neutral） | `?` Neutral |
| Doze 打盹 | 打盹帧（wait 0 闭眼） | 不动；z 每 600 ms 一个 | 打盹中（Neutral） | — |

- 46 px 一共 17 帧：wait 4 + review 4 + think 4 + hop 4 + 打盹 1，每帧 598 B，共 10,166 B。64 px 一共 12 帧：review 4 + wait 4 + think 4，每帧 1,152 B，共 13,824 B（只在语音便条上用）。
- Hop 和 NoVoice 从录音结束时算起；便条到 5 分钟上限时 G0 还按着（docs/20 U15），屏幕先停在上限页，松开 G0 回到主页时才从头算（`Runtime::rec_apply`），所以跳的那一下不会在上限页后面放完。
- "有声音"（`LevelHistory::voice`，和柱子用同一个 q）：一块 ≥ −50 dBFS（q ≥ 71，RMS ≥ 104，柱高 7 px）就开始；之后高于 −56 dBFS（q > 28，RMS ≥ 53）的块让它继续；连续 2 块（256 ms）都不高于 −56 dBFS 才结束。所以字和字之间的停顿里他还在点头，敲一下桌子他只点一两下。
  - 四段便条里 186 / 186 个说话块都算有声音，开头没有延迟；离说话远的 18 个安静块一个都不算；一共切换 14 次，正好是每段话的开头和结尾 [测]。
  - 原来是"最近 2 块的 RMS ≥ 1024（约 −30 dBFS）"：180 个说话块里只有 2 块够，Mando 几乎一直歪头 [测]。`rec_overlay.h` 的 `kVoiceLevel`（`rec_mando` 拿 `RecInfo::level` 比）一起改成 104。
- 示意图的 07_states 没有 64 px 的 hop 和 NoVoice：便条存好或丢弃后页面就关了，这两个动作只在主页（46 px）上出现。

### 7.2 哪一页用哪个动作（`theme::mando_pick`，纯函数）

| 页面 | 条件（从上往下，取第一个满足的） | 动作 | 牌子画在哪 |
|---|---|---|---|
| 主页 | `ui.moment == Saved` 且不到 0.8 s | Hop | 主页没有牌子 |
| 主页 | `ui.moment == NoVoice` 且不到 2.5 s | NoVoice | |
| 主页 | 有 session 在等你 | NeedsYou | |
| 主页 | 链路 Ready 且 `outbox.pending > 0`，或 Mac 插着但 cpad 没响应 | Sniff | |
| 主页 | 其余 | Idle | |
| 语音便条 | Cue / Recording 有声 / Recording 没声 / Finishing | Idle（不画牌子）/ Listen / Tilt / Sniff（保存中…） | (28,87) |
| 会议 | Recording 有声 / 没声 / EndConfirm / Finishing | Listen / Tilt / Idle（在听）/ Sniff | (23,79) |
| 锁屏卡亮着 | — | Doze | (38,67) |
| Tab 菜单打开 | — | 下面那一页的动作，停住不动、压暗 | — |
| 看板、等你、系统、编辑、确认 | — | 没有 Mando | — |

- `Moment` 由 Shell 写入 AppState：录音的 `EvtKind::Saved` 和笔记保存成功时写 `Saved`，`Discarded`（太短）时写 `NoVoice`（§4.3）。主页的 Mando 从 AppState 读，不看 toast 文字。
- 便条存好或丢弃后回到主页时才看得到 Hop / NoVoice；在别的页上就没有这个动作。

### 7.3 时序

- `kAnimTickMs = 200`：Mando 换姿势、波形左移、呼吸走一步，在同一个 tick 里。
- 气泡弹出：进入 NeedsYou 或 NoVoice 后的两个 tick，第 0 步比终点低 2 px，第 1 步到位（WhisperWoof 的 "?" 是往上弹的）。
- Hop：4 个 tick 放完，第 5 个 tick 停在 wait 0。
- Sniff：8 s 后停住，免得 Mac 插着却没开 cpad 时 Mando 一直托腮（WhisperWoof 8 s 后也改口"Still working…"）。
- z：每 3 个 tick（600 ms）多一个，三个都亮后下一步清空重来。
- 波形：每个 tick 从电平历史取最后 35 块的快照（约 1.5 块 / tick），所以看起来每帧左移约 2 根。
- 呼吸：每个 tick 走一步，4 步一圈。

### 7.4 只在背光 On 时动

| 背光 | 什么时候 | 动画 |
|---|---|---|
| On | 插着电；没插电但 60 s 内按过键；锁屏卡亮着 | 照 §7.3 |
| Dim | 没插电录音超过 60 s；锁屏时在开会 | **全部停住**：姿势、波形、z 不动，● 定在第 0 步（全亮）。每秒那次整屏刷新只让计时数字走 |
| Off | 熄屏、锁屏且卡没亮 | 不画（v0.4 的规则：不置脏就不画） |

- 菜单打开、页面上面有浮层时，Shell 不调模块的 `animate()`。
- 动画从不推迟 60 s 熄屏：它不经过 `power_key`，也不改 `last_key_ms`。

### 7.5 每一帧推哪些区域

推屏时间 = 像素数 × 16 bit ÷ 40 MHz + 约 30 µs [源 research B §4]。

| 场景 | 每个 tick 推的区域 | 像素 | 时间 |
|---|---|---|---|
| 语音便条 | Mando (10,19,36,64)；波形 (62,63,174,25)（图例三行时 (62,63,173,15)）；大 ● (60,38,12,12) | 2,304 + 4,350（2,595）+ 144 | 约 2.8 ms / 200 ms |
| 会议 | Mando (10,31,26,46)；大 ● (46,38,12,12)；多出一格时：合并以前推"变了色的最新几格到圆点"那一条（高 17）和 `现在` 挪动的那一小块（高 12），合并以后格子都挪了，推柱子和 ★ 那一带 (46,73,194,34)；合并或加星时推整个时间轴 (46,73,194,49) | 1,196 + 144（+约 600；合并后 +6,596） | 约 0.6 ms（+0.3 ms；合并后 +2.7 ms） |
| 主页 | Mando 加气泡 (7,12,34,48)，只在动的时候 | 1,632 | 约 0.7 ms |
| 锁屏卡 | z (45,5,22,22)，每 600 ms | 484 | 约 0.2 ms |
| 抽屉滑入 | 第 1 帧整屏；第 2、3 帧抽屉 | 32,400；≤19,314 | 13 ms；≤7.7 ms |
| 分段滑块 | (sx+5,17,≤165,15) × 3 帧 | ≤2,475 | 约 1 ms |

录音时（On）：每秒 5 个 tick × 2.8 ms + 1 次整屏 13 ms ≈ 27 ms/s；v0.4.0 约 8 次整屏 ≈ 104 ms/s [算]。模拟器 `test_anim_cost` 实测（同一份运行时代码）：便条 29.5 ms/s，会议前 5 s 20.5 ms/s，会议过了 12 s（合并后每 256 ms 多一格、格子都挪）29.2 ms/s，都在 32 ms/s 以内 [测 模拟器]。

gh #53 的 1–9 图例（2026-10-22，`test_anim_cost`，主机 Apple 芯片的 Mac，三次的中位数）：SPI 不变或更少：便条 25.9 ms/s，9 个 12 字母键的便条 23.3 ms/s（波形区变矮），9 个键的会议过了 12 s 26.0 ms/s [测 模拟器]。一次整屏的 CPU：2 个项目的便条约 495 → 538 µs（+40 µs），会议约 543 → 605 µs，9 个键的便条约 502 → 613 µs（+110 µs，最多的情况），会议约 514 → 596 µs；一个 tick 多 7–8 µs（问图例占几行：量一遍键的宽度再算，不截字），会议的 tick 反而少了（时间轴没变就不算重画区）[测 模拟器]。按设备上会议整页 21.8 ms（其中推屏约 13 ms）折算，CPU 部分约是主机的 16 倍：图例让一次整屏多约 2 ms（按 40 倍算也只多 4.4 ms），tick 多约 0.1–0.3 ms，离 50 ms 都远 [算]。`test_runtime_budget` 加了 `memo_us`（9 个 12 字母键的便条整页加推屏）和 `memo_tick_us`，都要 < 50 ms [待 真机]。

## 8. 分阶段与文件

每个阶段结束都跑 §9.4 的全部检查，看过模拟器截图，Lead 才提交。阶段内先写测试（RED），再写实现（GREEN），再整理。

### S0 色板画布和令牌（不改布局）

新增：
- `tools/theme/{tokens.py,gen_palette.py,gen_all.py,sources.json,README.md}`、`tools/theme/tests/test_gen_palette.py`
- `firmware/app/lib/theme/{palette.h,palette.cpp,raster.h,raster.cpp,raster_canvas.h,raster_canvas.cpp}`、`lib/theme/gen/palette_gen.h`（生成）
- `test/core/test_theme_palette/`、`test/core/test_theme_raster/`（S0 只有 fill 和裁剪）
- `firmware/sim/test/test_theme_sim/`（没有哨兵、局部推屏、色板副本）

修改：
- `lib/module_api/canvas.h`：`Color` 扩到 24 个和三个别名，`static_assert`
- `lib/shell/platform.h`：`present(Rect)`、`breathe()`
- `lib/hal/m5_display.{h,cpp}`：`palette_8bit`、`PaletteSprite`、继承 `RasterCanvas`、`present(Rect)`
- `firmware/sim/lib/simfb/{fb.h,fb.cpp,fb_canvas.h,fb_canvas.cpp,sim_display.h}`、`lib/simpng/png.{h,cpp}`（色板从主题来）
- `firmware/sim/oracle/*`、`test/test_efont/oracle_golden.h`（从 oracle 重新生成）、`test/test_fb`、`test/test_shots`
- 只把 `Select` 上表示信息的 `Dim` 字改成 `Ink2`（board 的名字和 session 状态、inbox、menu_view 的标记）：这是对比度要求，不是改布局；不可用的灰字不动
- `ci.yml`：include 规则加 `lib/theme`；`repo` 任务加 `tools/theme`（pytest、ruff、check_sizes、lizard、`--check --palette-only`）
- `scripts/stack_budget.py`：`APP_DIRS` 加 `theme`；`platformio.ini` 的 `cardputer-adv-test` 在 `lib_deps` 里加 `theme`（device 套件要显式列出本地库）
- `NOTICE`（WhisperWoof 的设计令牌）、`firmware/sim/README.md`（"颜色"一节）

结果：每一页都是暖色，布局和 v0.4.0 一样。

### S1 形状：卡片、键帽、胶囊

新增：`lib/theme/{theme.h,theme.cpp,hint.h,hint.cpp}`；raster 加 `fill_round`、`dot`、`remap`；`test/core/test_theme_hint/`、`test/ui/test_theme_widgets/`。

修改：
- `canvas.h`：§3.3 的新原语（`art`、`digits` 的默认实现先留着）
- `test/ui/support/fake_canvas.h`：记录 `ShapeOp`
- `lib/shell/status_bar.cpp`（胶囊、!N 只在不为 0 时、星期）、`shell_render.cpp`（提示栏、toast、编辑页和确认框的卡）
- `lib/shell/lock_screen.cpp`（卡片、顶边、小胶囊、键帽提示；时钟暂时用 efont 字）
- `lib/help/menu_view.cpp`（行改成键帽和选中胶囊；仍是全宽）
- `modules/board/board_module.cpp`、`board_logic.*`（卡片、计数胶囊、开头那句话）
- `modules/home/home_module.cpp`（格子样式；布局 S2 再改）
- `modules/inbox`、`modules/system`（选中胶囊、标题）

结果：示意图 02 完成，其他页有了卡片和键帽。👤 这时候拿真机看一次暖色深底、ink3、芥末色和焦糖色（§11 第 1 条）。

### S2 Mando、大数字、动画

新增：
- `tools/theme/{gen_mando.py,gen_icons.py,gen_digits.py,mando_colours.json}` 和它们的测试
- `lib/theme/gen/{mando46.h,mando64.h,icons.h,art_ids.h,digits_gen.h}`（生成）
- `lib/theme/{art.h,art.cpp,digits.h,digits.cpp,mando.h,mando.cpp,anim.h,anim.cpp,level_history.h,level_history.cpp}`
- `test/core/test_theme_art/`、`test_theme_digits/`、`test_theme_anim/`、`test_level_history/`；`test/ui/test_runtime_anim/`
- `LICENSES/OFL-1.1.txt`

修改：
- `canvas.h`：`art`、`digits`、`glow`
- `module.h`：`Module::animate()`
- `app_state.h`：`Moment`、`UiFlags.moment(_ms)`
- `lib/shell/runtime.{h,cpp}`（`render` 分成 `render_full` 和动画；`sync_backlight` 里 `set_backlight` 和 `breathe(0)`）
- `runtime_rec.cpp`（`on_level`；只有电平变了时不置脏；`Saved` / `Discarded` 写 `Moment`）
- `shell.{h,cpp}`（`animate`、`set_backlight`、`on_level`、`moment`，动画状态和电平历史）
- `rec_overlay.cpp`，新增 `rec_anim.cpp`（让 `rec_overlay` 不超过 400 行）
- `lock_screen.cpp`（Mando、时钟、暖光、z）、`status_bar.cpp`（小狗头）
- `modules/home/{home_module.*,home_logic.*}`（最终布局、问候、等你卡、`animate`）
- `firmware/sim/scripts/theme_states.cpas`（新，按 §7.2 一个状态截一张）
- `NOTICE`（Mando 和 Nunito）

结果：示意图 01、04、05、06、07 和动画帧完成。👤 HIL：`test_runtime_budget`、`rec_soak`。

### S3 Tab 菜单抽屉和动效

修改：`lib/help/menu_view.{h,cpp}`（抽屉、分段、滚动条、抽屉宽度、一屏 6 行）；`lib/shell/shell_menu.cpp`（下面的页先画再 `dim`，然后画抽屉；滑入和滑块的状态）；`lib/theme/anim.*`（两种滑动）；`test/ui/test_menu_view`；`test/core/test_theme_anim`。

结果：示意图 03 完成。👤 HIL：`test_runtime_budget`、`rec_soak`，菜单打开时录一段。

## 9. 测试

### 9.1 新的纯函数测试（先写，RED → GREEN）

| 套件 | 测什么 |
|---|---|
| `core/test_theme_palette` | 每个令牌映射到非哨兵索引，颜色和 `tokens.py` 一致；四个哨兵是洋红，v0.4.0 的 8 个常量的低字节都落在哨兵上；深色面在 RGB565 下互不相同；§2.5 列出的每一对 ≥4.5:1、ink ≥7:1；`kAa` 对称、白名单配对有精确项、其他配对不是哨兵；`kDim` 让亮度只降不升、`dim(bg)=bg`；呼吸每一步在 bg 上 ≥4.5:1、第 0 步等于 Rec；`aa_of(RecDot, Bg) == RecDotEdge` |
| `core/test_theme_raster` | fill 裁剪；圆角的角像素和过渡色（r=2…10，四角任选）；dot 直径 5 和 9 的像素图；`remap` 只改矩形内；暖光单调、只改 Bg 像素、Bayer 稳定 |
| `core/test_theme_hint` | §4.2 的七条规则，用今天所有的 `hint()` 字符串逐个验；三级退路；最长的字符串放得下或退回原样 |
| `core/test_theme_art` | 每帧的宽高；4 位解包（高半字节在前）；透明；画到边上被裁；压暗画法用 0x2F+n；生成数据的校验和不变 |
| `core/test_theme_digits` | 等宽单元：`12:48` 宽 75（Timer）、`09:41` 宽 88（Clock）；渐变索引连续；未烘焙的配对退化为阈值；非数字字符跳过 |
| `core/test_theme_anim` | `mando_pick` 的整张表（§7.2）；4 姿势 × 200 ms；Listen 和 Tilt 切换不换相位；Hop 只放一次；气泡两步；Sniff 8 s 后停；z 每 600 ms；滑动 3 帧、按键直接跳到终点；`anim_due` 只在 On 为真；呼吸在 Dim 时是 0 |
| `core/test_level_history` | 推入、取最后 35 块、94 格满了两两合并（取最大值）、★ 的位置跟着合并、RMS 到柱高的 dB 曲线（底噪、说话、−24 dBFS 封顶）、第一块算安静、"有声音"的迟滞、四段真机便条的电平 |
| `core/test_rec_level` | 电平的高通（§6.6）：35、50、60、68 Hz 降 ≥ 20 dB，300–3,400 Hz 在 ±1 dB 里，8 kHz 方波原样过；状态在块之间接着，在哪儿切块都一样；经过 `RecEngine`：WAV 逐字节不变，电平是高通以后的，下一段录音从头算；−52 dBFS 的嗡嗡声里说话、停 2 s：停顿的第 2 整块不再有声音，停顿里都是点 |
| `ui/test_theme_widgets` | 在 `FakeCanvas` 上：卡片一定画了顶边或描边；键帽宽度；牌子居中；胶囊为 0 时画普通字；格子选中和没选中的颜色；toast 的语气色 |
| `ui/test_runtime_anim` | Runtime 加 `FakeDisplay`：On 时录音每 200 ms 推三块、不整屏推；Dim 时零次局部推屏、`breathe(0)`、波形快照不变；Off 时什么都不推；动画从不置脏；只有电平变了不会整屏重画；1 s 刷新照旧 |
| `tools/theme/tests` | §5.1 |

### 9.2 `test_rec_visible` 不变，加上色板

- 原来的四个测试一行不改，照样通过：状态栏的 `●mm:ss`、锁屏那一行的格式和颜色（`Color::Error`）都保持 v0.4.0 的样子。
- 新增 `test_dot_has_contrast_on_its_background`：对 {便条, 会议} × {锁屏, 菜单, toast} × 四个阶段，含 ● 的那次画字，其字色和底色在色板上的对比度 ≥4.5:1，且字色是 `Error`（Cue 是 `Warn`）。
- 新增 `test_dot_frozen_when_dimmed`：Runtime 在 Dim 时 `breathe` 只被调成 0，状态栏的 ● 从来不用 `RecDot`。
- 模拟器的 `test_theme_sim` 补一条：录音的每张截图里，状态栏左边 (0,0,80,12) 至少有 20 个 `Rec` 索引的像素，On 和 Dim 都是。

### 9.3 要改的现有测试（有意为之，逐条写进提交说明）

| 阶段 | 测试 | 改什么 | 为什么 |
|---|---|---|---|
| S0 | sim `test_fb` | RGB332 换成色板索引 | 画布格式变了 |
| S0 | sim `test_shots` 第 93 行 | 按 `Rec` 索引数红像素，不再按 0xE0 | 同上 |
| S0 | sim `test_efont/oracle_golden.h` | 从 oracle 重新生成 | 帧里存的是索引 |
| S1 | `test_mod_board` | `跑1 等1 队2` 分成三次画；选中填充在 y=29（卡），不在 y=12 | 计数变胶囊；看板变卡片 |
| S1 | `test_mod_home` | `P 项目` 分成键帽 `P` 和 `项目`；选中格子的底色是 `Accent` | 格子样式 |
| S1 | `test_menu_view` | 选中行的填充色是 `Accent` | 行样式 |
| S1 | `test_rec_overlay` 第 173 行 | `松开 G0` 分成 `松开` 和键帽 `G0` 两次画 | 提示栏键帽（字符串本身不变） |
| S2 | `test_status_bar_states` | 在线：断言画了小狗头（`ArtId::Head`），不再是 "●" | 示意图 |
| S2 | `test_mod_home` | 去掉 `主页 · 10/21 周三` 和 `按住 G0 说一段` 的断言；提示结尾的断言改成"以 `Enter打开` 结尾、Tab 菜单是第 9 个格子"；最急一条的断言改成卡里的标题、龄期和 `项目键 · 等你回复` | 主页布局 |
| S2 | `test_rec_overlay` 第 171、193、204 行 | `● REC 0:12` / `● REC 12:31` 改成：大 ● 画了、`digits("0:12")`、牌子 `在听`；状态栏 `●0:12`。第 165 行（`● 准备`）、`#cpa`、`未归属`、`保存中`、`★2`、`第 3 段`、`★ 已标记`、开会确认框和结束确认框的原文都不改 | 便条、会议布局 |
| S2 | `test_runtime_rec` 第 305 行 | `● REC` 改成状态栏的 `●` 加计时 | 同上 |
| S3 | `test_menu_view` | 去掉 `菜单 · 会议录音` 标题和 `[本页]`；一屏 6 行；↑ ↓ 标记改成滚动条；`row_y` 按 14 px | 抽屉布局 |

`test_lock_power`（锁屏便条那一行 `● REC 0:12`、`已锁屏`）、`test_rec_visible`、`test_shell_dispatch_order`、`test_exit_keys` 和所有 core 测试都不改。

### 9.4 每个阶段都跑的检查

在 `firmware/app`：
```sh
pio test -e native
uvx gcovr -r . --filter 'lib/|modules/' --exclude 'lib/(hal|usb_transport)/' --fail-under-line 80
pio run -e cardputer-adv && python3 scripts/check_budget.py .pio/build/cardputer-adv \
  && python3 scripts/check_partitions.py .pio/build/cardputer-adv/partitions.bin
! grep -a -q -E 'debug\.(key|state|reboot|rec|mic|codec)' .pio/build/cardputer-adv/firmware.bin
pio run -e cardputer-adv-test
pio test -e cardputer-adv-test -f 'device/*' --without-uploading --without-testing
python3 ../../tools/repo/check_sizes.py --warn 400 --fail 800 lib test modules src
uvx lizard==1.24.1 -l cpp -L 50 -w lib modules src
# include 规则（ci.yml，目录里加上 lib/theme）
uv run --no-project --with pytest pytest scripts test/core/support -q
```

在仓库根目录：
```sh
pio test -d firmware/sim && pio run -d firmware/sim
firmware/sim/.pio/build/sim/cpa-sim shots firmware/sim/scripts/v04_tour.cpas --out firmware/sim/out/theme_tour
firmware/sim/.pio/build/sim/cpa-sim shots firmware/sim/scripts/theme_states.cpas --out firmware/sim/out/theme_states  # S2 起
uv run --with pillow --with pytest pytest tools/theme -q
python3 tools/theme/gen_all.py --check --palette-only
```

**看 PNG**：每个阶段都把 `theme_tour/contact.png` 和对应的示意图并排看：截断、重叠、颜色、Mando 的位置。`shots.json` 里 `missing_glyphs` 全是 0。新加的文字（在呢、嗅一嗅…、打盹中、存好了、没听到、早上好等）用示意图的 efont 解码器查过，字库里都有 [测]；设备测试 `test_runtime_budget` 会再断言一次。

## 10. 预算

| 项目 | 现在（v0.4.0） | 主题之后 | 上限 | 依据 |
|---|---|---|---|---|
| 画布 | 32,400 B，RGB332 | 32,400 B，`palette_8bit` | — | [源] |
| 色板 | 0 | +768 B（堆） | — | [源 LGFX_Sprite.hpp:398-416] |
| 空闲堆 | 182,944 B | 约 182,100 B | ≥ 153,600 B（150 KB） | [测 docs/07 第 848 行]，[算] |
| 最大连续块 | 163,828 B | 约 163,000 B（色板紧跟在精灵后面分配） | ≥ 65,536 B | [测]，[算] |
| 静态 RAM | 65,968 B | 约 66,300 B（动画状态 ~24 B、电平历史 ~190 B、波形和时间轴快照 ~130 B） | 96 KB | [测 docs/07 第 898 行]，[算] |
| AppState | 3,488 B | +8 B（`Moment`） | 4 KB | [源 docs/13 §10.3] |
| loop 栈余量 | 5,788 B | 不应变少：主题函数的栈帧都很小，没有大数组 | ≥ 4 KB；单帧 ≤ 4 KB | [测]，[待 stack_budget.py] |
| 新素材 flash | 0 | Mando 46 px 10,166 B + 64 px 13,824 B + 数字约 4.1 KB + 小图约 0.2 KB + 色板和表约 1.7 KB（色板 768、`kAa` 576、`kDim` 256、其余）≈ 30 KB | ≤ 60 KB | [算] |
| app 镜像 | 969,824 B | 约 1,020,000 B（素材约 30 KB + 代码约 20 KB） | 警告线 1,835,008 B；硬上限 2,621,440 B | [测]，[算] |
| 色板项 | — | 约 130 / 256 | 256 | [算] |
| 录音时屏幕 SPI（On） | 约 104 ms/s | 约 27 ms/s | — | [算] §7.5 |
| 最长的一步 loop | 整屏约 15–18 ms | 不变（动画帧约 3–4 ms）；评审估整屏 35–50 ms、抽屉一帧 25–30 ms | ≤ 50 ms，回复 p95 ≤ 100 ms | [算]；测试版里 `Runtime::render_times()`（`Platform::micros()`，设备上是 `esp_timer`）记最长的一次整屏和一次动画帧，`test_runtime_budget` 打在 `CPA_BUDGET` 行上（`max_render_us`、`max_tick_us`，再加一个满载会议页的 `meeting_us`；gh #53 加了图例最满的便条页 `memo_us` 和它的一个 tick `memo_tick_us`），都要 < 50 ms [待 真机] |
| 中文字库 | efontCN_12 213,444 B | 不变 | — | [源 research B §5] |

## 11. 风险

| # | 风险 | 怎么应对 |
|---|---|---|
| 1 | 面板对比度没量过：bg、sheet、card 在 RGB565 里只差 1–2 级，43% 背光下可能连成一片；10.5% 时大概只有 ink、accent 和 REC 看得清 | 卡片都有顶边或描边；关键信息不靠深色面区分。👤 S1 后真机看一次（43% 和 10.5%）；不行就调 `tokens.py` 一处、重新生成，不用改代码 |
| 2 | M5GFX 在调色板精灵上的文字颜色到底是不是"原始值当索引"没有在真机上验证 | S0 第一件事就是 oracle 用例（M5GFX 本身，在 Mac 上跑）；不对就在 `PaletteSprite` 里换写法，模块不受影响 |
| 3 | 直接写 `getBuffer()` 的缓冲区，布局和 M5GFX 以为的不一致 | `begin()` 断言 `bufferLength`；oracle 拿同一份 `RasterCanvas` 写 M5GFX 的精灵缓冲区，和模拟器逐字节比 |
| 4 | 芥末和焦糖只差 1.17:1 | 等你永远带 "!"、色条或文字；真机看时专门看这一对 |
| 5 | 状态栏小狗头被认成狐狸或猫 | 换成 5×5 的圆点图，一行改动 |
| 6 | Mando 太可爱、看久了烦（Pebble Time 的教训） | 空闲时不动；Sniff 最多 8 s；Hop 只跳一次；Dim 和熄屏时全停 |
| 7 | 打盹是在 wait 第 0 帧上改眼睛，素材包里没有真正的睡姿 | 46 px 时靠 z Z 表意；真要睡姿得请 WhisperWoof 出新图（不在这次范围内） |
| 8 | 范围大：`Canvas` 每个模块都在用，还有模拟器、`FakeCanvas` 和 6 屏重排 | 分 S0–S3，每阶段能单独发；新调用都有默认实现；改的测试逐条列出（§9.3） |
| 9 | 和 `feat/app-sdk` 冲突：两边都改 `canvas.h`、`module.h`、主页 | 谁先合都行，冲突只在新增的声明上；主页的第四行按 §6.3 的四行版用 `theme::tile()`；Lead 合并时跑 `test_app_tiles` |
| 10 | 只有电平变了不再整屏重画，可能有测试或 HIL 依赖"电平一变就重画" | S2 跑 `test_runtime_rec`、`rec_flows`；HIL 的 `rec_memo`、`rec_meeting` 看的是 `debug.state`，不看像素 |
| 11 | 抽屉滑入时整屏推可能撕裂（Cardputer 没接 TE 引脚） | 只有第 1 帧整屏，其后只推抽屉；滑动只在打开时 |
| 12 | 暖光每次整屏重画都要算（卡外约 12,000 像素） | 用平方距离加阈值表，不开方；S2 在测试版里用 `esp_timer` 量 [待]；太慢就只在卡第一次亮起时画 |
| 13 | 生成器依赖 Pillow 的 WebP 和 woff2 支持 | `sources.json` 记版本；退路是 fontTools 解 woff2；生成结果提交进仓库，CI 不需要重新生成 |
| 14 | 堆和时间都是推算，没在 v0.4.0 上量过 | S2、S3 后各跑一次 `test_runtime_budget` 和 `rec_soak`（👤） |
| 15 | 电平改成线性以后，真机上的说话（RMS 大多 1,000–4,000）可能只到柱子高度的 15–50%，波形显得矮 | **已改（2026-10-22）**：真机上手持正常说话的块 RMS 大多 224–847（−43 到 −32 dBFS），比预计低 7–19 dB，线性刻度下全是点。改成 −60 到 −24 dBFS 的 dB 刻度（§6.6），"有声音"改成 −50 / −56 dBFS 加迟滞（§7.1）；模拟器的 `mic … speech` 也改成真机的电平（字 225–1,100，停顿 16）。还要真机看：吵的房间（底噪到 −50 dBFS 时 Mando 会一直点头）；开会时 1–2 m 外的人声（可能比手持低 10–16 dB，接近门槛）；以后改麦克风增益时，刻度两端和门槛要挪同样的 dB。**又改（2026-10-22，第二次真机检查）**：屋里的低频嗡嗡声（峰在 35 / 68 Hz，七成能量在 100 Hz 以下，−52 到 −57 dBFS）高过 −56 dBFS 的保持线，停顿里 Mando 一直点头、画 5 px 的柱子。电平改成 130 Hz 4 阶高通以后的 RMS（§6.6，`lib/rec/level_hpf.*`），只动电平，WAV 不变；合成测试里 −52 dBFS 的嗡嗡声在停顿的第 2 整块就不算有声音、柱子都是点；四段便条的说话和底噪分类不变。还要真机看：嗡嗡声 100 Hz 以上那三成如果是 200 Hz 以上的谐波，高通挡不住，约 −57 dBFS，刚在保持线下面；宽带的吵（风扇、空调的嘶声）高通也挡不住 |

## 12. 不做的事

- **不用 16 位画布**，不用 M5GFX 的运行时抗锯齿，不用 VLW 字体，不加 efontCN_16 或粗体：标题仍是假粗体 12 px。
- **不引入其他方向的布局**：主页"一张大标题卡"、44/48 px 数字、红色实心的 REC 胶囊、12×12 的单色图标、波形旧柱渐隐、每次移动光标都滑动的选中块、电池图标、Dim 时换一套高对比色板。
- **状态栏的 ● 不呼吸**（§1）。
- **没有新的 Mando 美术**：睡姿是改眼睛；64 px 没有 hop 和 NoVoice。
- **执行确认框、迁移进度页不重排**：没有示意图，只换颜色。项目页、等你、系统、编辑页和放弃确认按 §6.9 用同一套零件重排了（评审 2026-10-22），文字不变。
- **提示字符串**只改了对齐示意图和挤不下的几条：主页、会议页、便条（`G0松开结束 1-9选项目`，gh #53 后不再写 0）、看板（去掉 `;.选择`）、队列（去掉 `Opt+D完成`）、菜单（去掉 `字母直达`）；去掉的键都还在 Tab 菜单里。主页卡上的龄期用 docs/20 U22 的 `age_text`（`9 分钟`，合并 v0.4.1 时从示意图的 `9 分` 改过来）。
- **模拟器不模拟背光亮度**，调暗时的 PNG 仍是全亮像素。
- **应用不能自己挑颜色**：没有每个应用一种强调色；色板里给以后留了 0xAE–0xDF。
- **锁屏且熄屏时的录音样子**和 v0.4.0 的字一样（左上角一行红字），只是放进了状态栏 REC 槽那样的胶囊（§6.8）。
- **真机的面板检查、`test_runtime_budget` 和 `rec_soak`** 不在这份计划里做，等各阶段做完由 Lead 约 HIL（👤）。

## 13. 评审后的修改（2026-10-22）

两份评审（逐屏对示意图、按 RGB565 算对比度；代码和检查）提的问题都按上面各节改了。这里只列改了什么、没改什么、合并时要注意什么。

**对比度（< 4.5:1 的三处）**：菜单里只说明的键帽改成 Raised 底 Ink3，控制页的事实改成普通 Ink3 字（§6.5）；离线时选中的队列项用 Ink2 加 `离线` 胶囊（§6.4）；锁屏时的 ● 那一行放进 RecBg 胶囊，暖光离它 2 px（§6.8）。新增的边缘对 `Warn`/`Select`（等你行选中时的色条）进了 `tokens.py` 的 `AA_PAIRS`。

**对齐示意图**：会议时间轴铺满、★ 不糊、段号进标题行（§6.7）；便条波形线性、保存时留着波形（§6.6）；菜单的说明位置、角标、提示（§6.5）；状态栏 ● 和计时之间的空格（§6.1）；几条提示栏（§12）；主页龄期（`age_text`，docs/20 U22）、等你时 Mando 坐着、离线时不说"都处理完了"（§6.3、§7.1）；没有示意图的几页重排（§6.9）。

**代码**：`index_of` 对超出令牌的值给哨兵，ci.yml 不许在 `lib/theme` 外面把数转成 `Color`（§3.2）；新的公开接口 `theme::hint_fits`、`theme::keys_text`（§4.1）；`home_grid` 给四行主页（§6.3）；`tools/theme` 的重新生成测试只在 `CPA_WHISPERWOOF` 指向一个 checkout、而且装的是 `mando_colours.json` 里写的 Pillow 版本时才跑，去掉了写死的 `/Users/…` 路径；`test_rec_visible` 不再拿压暗的 ● 当原色查，并要求至少有一个没压暗的 ● ≥ 4.5:1；测试版里记最长的整屏和动画帧（§10）。

**"看到 ● REC 再说话"**：v0.3.1 你选的这句话，计时起来以后屏上原来只有大 ● 和计时、没有 "REC"。这次选了不改你的话的办法：录着时在计时右边画一个小的 `REC`（Rec 色，§6.6）。如果你更想改话（比如"看到计时走起来再说话"），要动 `rec_pages.cpp` 一行、`test_rec_overlay`、`test_theme_sim` 和 docs/04、11、19（👤）。

**模拟器脚本**：`v04_tour` 的会议一段原来 0:05 以后没有 `mic`，时间轴不长、★ 落不上去；现在每段都喂 `mic … speech`（设备上每 128 ms 一块）。`theme_states` 的 `home-hop` 等 300 ms 再拍（原来拍到的是便条页的"保存中…"）。

**还没做的（要真机，👤）**：面板上看暖色和 Dim 时 RecBg 上的 `● mm:ss`；`test_runtime_budget`（空闲堆、最大块、`max_render_us`、`max_tick_us`、`meeting_us` < 50 ms）；菜单开着时的 `rec_soak`；M5GFX oracle 对 `push(Rect)` 和 `breathe`；dB 电平在吵的房间里和开会距离上的样子（§11 第 15 条）。四行主页的模拟器截图要等 `feat/app-sdk` 合进来、有了应用格子才拍得到，现在只有 `home_grid` 的单元测试。

**合并 `feat/app-sdk` 时**：主页用 `home_grid(格子数)` 排四行；`tools/apps/app_rules.py` 的允许列表加 `theme`；`check_apps` 的提示宽度改用 `theme::hint_fits`（§4.4）。这三处在本分支上做不了：那两个文件只在 `feat/app-sdk` 上。

**合并 #43（49e5b99）时**：两边都把 `Runtime::update` 改成了 `bool redraw = true`（`runtime.h`）。留一份 `update()`，用 #43 的 `same_link` 和 `sync_link_slice`，保留本分支把 `render` 拆成 `render_full` 和 `animate`（以及测试版的计时）。合完再跑 `test_a_pull_does_not_redraw_per_chunk`：它只数整屏，应该照样通过。

## 附：证据索引

- **方向一和评审**：`scratchpad/visual/companion_spec.json` 的 `direction`（palette、fonts、motion、cost、risks）、`judges[0..1]`（scores、recommendation、synthesis_ideas）、`research[0..2]`。
- **示意图和脚本**：`scratchpad/visual/companion/{01_home,02_board,03_menu,04_memo,05_meeting,06_lock,07_states}.png`、`frames/anim_listen_{1..4}.png`、`frames/anim_regions@2x.png`；`companion/src/lib.py`（色板、圆角、胶囊、状态栏、键帽、牌子）、`s_home.py`、`s_board.py`、`s_menu.py`、`s_rec.py`、`s_lock.py`、`s_states.py`、`sprite.py`（裁剪、缩放、15 色）、`build_all.py`（只用色板里的颜色）。示意图色板去重后 92 个颜色 [测 本文作者重跑 `lib.PAL`，没写 PNG]。
- **固件**：`lib/hal/m5_display.cpp:14-25`（`rgb565()`）、`:46-55`（`begin`、`setColorDepth(8)`）、`:57`（整屏 `pushSprite`）；`lib/module_api/canvas.h`（四个调用、8 个颜色、布局常量）；`lib/shell/runtime.cpp:220-230`（`render`）、`runtime_rec.cpp`（`mirror_rec` 的 `same_rec` 含 `level`）；`rec_core.cpp:173`（`EvtKind::Level`）；`status_bar.cpp`、`rec_overlay.cpp`、`lock_screen.cpp`、`shell_render.cpp`、`toast.h`、`power.cpp`（`backlight_of`）；`lib/help/menu_view.cpp`（`kMenuVisibleRows`、`kMenuKeyColW`）；`modules/home/home_module.cpp`、`home_logic.h`（`kHomeTiles`）、`modules/board/board_module.cpp`。
- **模拟器**：`firmware/sim/lib/simfb/{fb.h,fb.cpp,fb_canvas.*,sim_display.h}`、`lib/simpng/png.h`、`README.md`（"How close it is to the device"、"Code the simulator repeats"）。
- **测试**：`test/ui/test_rec_visible/test_main.cpp`、`test_status_bar_states`、`test_mod_home`、`test_mod_board`、`test_menu_view`、`test_rec_overlay`、`test_runtime_rec`、`test_lock_power`；`test/ui/support/fake_canvas.h`；sim `test_fb`、`test_shots`。
- **CI 和预算**：`.github/workflows/ci.yml`（include 规则、gcovr、budget、sizes、lizard、sim）；`scripts/check_budget.py`（静态 RAM ≤96 KB、镜像 ≤2.5 MB、1.75 MiB 警告）；`scripts/stack_budget.py`（`APP_DIRS`、单帧 4 KB、loop 12 KB）；`test/device/test_runtime_budget`（栈 ≥4 KB、堆 ≥150 KB、最大块 ≥64 KB）；`docs/07-hil-checklist.md` 第 848、898、902 行（v0.4.0 的实测）。
- **App SDK**：`feat/app-sdk` 的 `module.h`（`AppTile`、`Module::tile`、`ModuleContext::appTile`）、`modules/home/home_logic.h`（`kHomeAppsMax = 3`，第四行）、`docs/16-writing-apps.md` §3.4、§4、§6。
- **WhisperWoof**（只读）：`website/mando/hd/{wait,think,review,hop}.webp`（sha256 前缀 4cb38c44…、91489609…、9924f586…、ee4dc0db…）；`src/assets/fonts/Nunito-normal-latin.woff2`（20fc9b6f…）和 `OFL.txt`；`LICENSE`（MIT，Copyright (c) 2026 Heqing Huang）。两个目录里这些文件逐字节相同。
- **调研**：research A（WhisperWoof 的设计语言、`pickMandoAction`、RGB332 毁掉了暖色）；research B（调色板画布、`c & 0xFF`、运行时抗锯齿出错色、推屏时间表、flash 余量）；research C（同类产品：Flipper、Pebble、claude-desktop-buddy、Pwnagotchi）。
