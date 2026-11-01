[English](CONTRIBUTING.md) · **中文**

# 贡献指南（草稿）

欢迎贡献。v0.x 期间接口还会变，动手前先开 issue 说一下想做什么。下面的规则 CI 会检查，评审时也会看。

## 先读这些

- 做什么：[docs/04-prd.md](docs/04-prd.zh-CN.md)；怎么搭：[docs/05-architecture.md](docs/05-architecture.zh-CN.md)。
- 协议以 05 §6 为唯一真相源，机器可读版本在 [`protocol/`](protocol/)。改协议的流程见 [protocol/README.md](protocol/README.zh-CN.md)。
- 重要决定写成 ADR，放在 [docs/adr/](docs/adr/)。
- 文档用英文写在原来的路径上。有中文版的，放在旁边的 `<名字>.zh-CN.md`，两个文件第一行互相链接。改了一个，就在同一个 PR 里改另一个。

## TDD：RED → GREEN → REFACTOR

1. **RED**：先写测试，跑一遍，确认它因为"功能还没有"而失败。
2. **GREEN**：写刚好让测试通过的实现。
3. **REFACTOR**：在测试保持全绿的前提下整理代码。

- 修 bug 时，先写一个能复现它的测试。
- 测试失败时改实现，不改测试来迁就实现（除非测试本身写错了，并在 PR 里说明）。
- 覆盖率目标 80%（固件纯逻辑和 daemon 分开统计）。HAL 不计入覆盖率，由真机 HIL 覆盖。
- 三层测试都要有：单元、集成（daemon 对接 pty 假设备和假 `claude`）、端到端（真机 HIL 加人工检查单）。

## 代码规模与结构

| 规则 | 上限 | 检查 |
|---|---|---|
| 文件行数 | 200–400 行为宜，超过 400 警告，超过 800 失败 | `python3 tools/repo/check_sizes.py --warn 400 --fail 800 <路径>` |
| 函数长度 | < 50 行 | `uvx lizard -L 50 -w <路径>`；WP4 起固件另加 clang-tidy `readability-function-size` |
| 嵌套 | ≤ 4 层 | clang-tidy `NestingThreshold 4`；Python 用 ruff `C901` |

- 按功能或领域组织目录（固件 `lib/<领域>`、`modules/<功能>`，daemon `cpad/<领域>`），不建 `models/`、`utils/` 这类按类型分的目录。
- 简单优先：不写用不到的功能和抽象；能 50 行解决的不写 200 行。

## 不可变数据

- 业务逻辑一律写成纯函数：`旧状态 + 输入 → 新状态 + 副作用`，返回新值，不原地修改。
- Python 用 `@dataclass(frozen=True, slots=True)`、`tuple`、`dataclasses.replace`；持久化只追加。
- 唯一的例外：串口收发环、显示 sprite、文件句柄这类天生可变的硬件资源，只允许出现在固件的 `hal/`、`usb_transport/` 和 daemon 的 I/O 外壳里（[04 §16](docs/04-prd.zh-CN.md) 第 5 条）。

## 加一个应用（固件模块，v0.4.0）

应用就是一个模块，接口见 [05 §5.4](docs/05-architecture.zh-CN.md)，设计见 [13 §10](docs/13-assistant-v2-plan.zh-CN.md)。照着 `modules/home/`（主页）或 `modules/inbox/`（等你）写。路径都相对 `firmware/app/`。

1. **代码**：放在 `modules/<名字>/`。`<名字>_logic.{h,cpp}` 放值类型的界面状态和纯函数，`<名字>_module.{h,cpp}` 只是一层薄外壳。不用堆、不阻塞；不 include Arduino、M5 或 esp 的头文件（CI 的 include 规则会查）；模块对象不超过 1 KB。
2. **注册**：在 `src/module_list.cpp` 的手写列表里加一行（不用自注册，原因见 05 §5.5）。id 符合 `^[a-z][a-z0-9_]{1,15}$`，不能重复。再把模块目录名加进 `platformio.ini` 里 `cardputer-adv-test` 的 `lib_deps` 和 `scripts/stack_budget.py` 的 `APP_DIRS`，设备测试和栈预算才会算上它。
3. **快捷键**：只用单键和两键组合，一个字母只代表一件东西；已占用和预留的字母见 [13 §5.2](docs/13-assistant-v2-plan.zh-CN.md)，不用 Z。要改四处：
   - `lib/input/keymap.h`：新的 `Action` 加在最后，旧动作的数值不变；
   - `lib/input/keymap.cpp`：在 `kKeymap` 的 `Layer::Global` 里绑一个 `Opt+字母`（`test_keymap_static` 检查幽灵键）；
   - `lib/shell/shell.cpp`：在 `global_key()` 里打开你的模块；
   - `lib/help/action_help.cpp`：给新动作写一句中文说明，菜单的"去哪"页显示它（`test_action_help` 检查每个动作都有）。
4. **主页格子**：在 `modules/home/home_logic.h` 的 `kHomeTiles` 里加一格 `{Opt+字母, "字母", "名字"}`，并把它加进 `modules/home/home_module.cpp` 的菜单行 `kHelp`。格子自己不执行任何事，只是 `ctx.press(Opt+字母)`，所以效果一定和直接按快捷键相同。v0.4.0 的九宫格已经满了，加格子要改 `kHomeTileCount` 和布局，并更新 `test_mod_home`。
5. **菜单的"本页"**：实现 `help()`，返回一个静态数组。每行是 `HelpRow{ev, keys, label}`：
   - `ev` 是菜单里在这一行按 Enter 时替你按的键；`keys` 是行首写的键名；`label` 是一句说明，不超过 120 px（ASCII 6 px，汉字 12 px）；
   - 一组键（如 `1-9`）把 `ev` 留空（`kind` 为 `None`），只显示、不执行；
   - 不用列 Tab 和 Opt+L，外壳会处理；
   - 你的模块处理某个键时如果会调用 `ctx.press()`，这一行就直接写 `press()` 的那个目标键（比如主页的行重放 Opt+P，而不是 P），因为重放出来的键不能再排键。
6. **提示栏**：`hint()` 最多写 3 个最常用的键，最右边固定写"Tab菜单"，整行放得进 240 px。
7. **测试**（先写，看它失败）：
   - `test/ui/test_mod_<名字>/`：用 `FakeContext`，它的 `pressed` 记下 `press()` 收到的每个键；
   - `test/ui/test_menu_dispatch/`：把你的模块加进 Rig，对"本页"的每一行按 Enter，效果要和直接按那个键完全相同；
   - `test_keymap_static`、`test_action_help` 全过；改了按键，再按下面 PR 检查单的第一行核对文档。

## 安全检查（每次提交前）

- 没有硬编码的密钥、令牌或个人数据；配置走环境变量或 `$CPA_HOME`。
- 输入在边界处校验（协议字段按 `protocol/` 的 schema 和 limits）。
- 子进程用参数列表，不经过 shell；用户正文放在 `--` 之后。
- 错误信息只给错误码，不带正文、路径或命令输出；日志里的正文只记长度和哈希前 8 位。
- 漏洞请按 [SECURITY.md](SECURITY.zh-CN.md) 私下报告。

## 提交信息

用 conventional commits：`<type>: <description>`，type 取 `feat`、`fix`、`refactor`、`docs`、`test`、`chore`、`perf`、`ci` 之一。例如：

```
feat: add outbox crash-injection tests
fix: drop lines longer than 2048 bytes before parsing
```

一个提交只做一件事；PR 描述写清楚动机、做法和怎么验证的。

## PR 检查单

在 PR 描述里逐条打勾，不适用的写"不适用"：

- [ ] **按键**：改了 `kKeymap` 或 `action_help` 时，README 的[按键](README.zh-CN.md#按键)表和 [docs/04 §8](docs/04-prd.zh-CN.md) 的表跟着改了，键和说明都对得上；只用单键和两键组合（`test_keymap_static`）。
- [ ] **测试**：先写的测试，看到过它失败；`pio test -e native` 和 daemon 的 `pytest` 全过，覆盖率不低于 80%。
- [ ] **规模**：`check_sizes.py --warn 400 --fail 800` 和 `lizard -L 50` 通过。
- [ ] **协议**：只新增、不修改，`v` 仍是 0；schema、向量和两端编解码一起改（[protocol/README.md](protocol/README.zh-CN.md)）。
- [ ] **安全**：按上面的"安全检查"过一遍。
- [ ] **文档**：链接都能打开；用户能看到的变化写进 [CHANGELOG.md](CHANGELOG.zh-CN.md)。

## 真机

- 日常刷固件不要按 G0（会锁进下载模式，见 [03 §2](docs/03-hardware-bringup.zh-CN.md)）。刷机前先执行 `cpa pause`，让 daemon 让出串口。
- 打开串口必须用"安全打开"的控制线顺序，绝不出现 (RTS, DTR) = (1, 0)。
