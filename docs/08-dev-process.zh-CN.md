[English](08-dev-process.md) · **中文**

# 08 · 开发流程与工作流

> 记录这个项目实际是怎么开发的：谁做什么、一个里程碑怎么从想法走到 GitHub Release、每一步用什么质量门禁。新加入的贡献者（人或 agent）从这里开始。
> 实际运行过的每个工作流的记录在 [devlog/](devlog/)。

## 1. 角色

| 角色 | 由谁担任 | 职责 |
|---|---|---|
| 产品负责人 | 用户 | 定方向与优先级；做只有人能做的事（插拔硬件、登录、手动检查单）；可以否决任何默认决定 |
| Lead | 主 Claude Code 会话 | 拆里程碑；编写并启动工作流；审阅每一段工作流的输出；修正文档和 CI；提交、开 PR、合并、打 tag |
| 工作流子 agent | 由工作流脚本派出 | 调研、写设计、实现、对抗式评审、修复、在真机上跑测试。**每个 agent 只能改自己名下的目录** |
| CI / CD | GitHub Actions | 每次推送和 PR 都跑全部检查；推送 `v*` 标签时编译固件并发布 Release |
| App 的 Auto-fix | Claude Code 桌面版 | 盯着 PR，CI 失败、出现冲突或评审意见时叫醒 Lead |

## 2. 一个里程碑的生命周期

```
想法 / 用户反馈
   │
   ▼
GitHub：里程碑 + issue（路线图要留在 GitHub 上）
   │
   ▼
① 发现：调研工作流（多角度搜索 → 去重 → 对抗式核实），加真机或 CLI 闸门实验
   │
   ▼
② 设计：设计草稿 → 对抗式评审（只报阻塞问题和要砍的范围）→ 定稿 → docs/NN-*.md
        产出工作包：每个包拥有的目录互不重叠，依赖关系明确，验收命令先写好（TDD）
   │
   ▼
③ 实现：能并行的工作包同时开发（固件 ∥ daemon），每个包都按 RED → GREEN → REFACTOR
   │
   ▼
④ 评审：每个包各派一个对抗式代码评审 → 修复 agent 逐条修（先补测试再改代码）
   │
   ▼
⑤ HIL：独占设备的 agent 在真机上跑端到端场景和长时间运行测试；发现的问题按归属分给修复 agent → 重跑
   │
   ▼
⑥ 发布：Lead 复跑所有测试 → 按 conventional commits 分模块提交 → release/vX.Y.Z 分支 → PR
        → CI 全绿（失败时 Auto-fix 叫醒 Lead 去修）→ 合并 → 打 tag → CD 发布 Release
   │
   ▼
⑦ 记录：devlog、CHANGELOG、docs/releases/vX.Y.Z.md；没做完的事变成 GitHub issue，挂到下一个里程碑
```

**原则**
- **每一段工作流跑完，Lead 都要先读结果再开下一段**，不把多个阶段串成一个巨大的黑盒。
- **证据优先**：所有关于硬件或 CLI 的结论都要有出处，可以是实测数据、源码行号或官方文档。推测必须标明是推测。
- **对抗式核实**：调研结论、设计、代码，都要单独派一个 agent 专门去反驳。
- **只有人能做的事才问人**，其他事情都按默认值推进，并在文档里写明当时用的默认值。

## 3. 质量门禁（CI 里都有）

| 门禁 | 规则 | 在哪里检查 |
|---|---|---|
| 协议契约 | 每种消息至少 1 个合法用例和 1 个非法用例；执行类意图不能出现在 outbox 里；limits 和 schema 必须一致 | `protocol/tools/check_vectors.py` |
| 测试覆盖率 | 纯逻辑代码 ≥ 80%（目前固件约 96%，daemon 约 96.7%） | gcovr / pytest-cov |
| 文件和函数规模 | 文件超过 400 行警告、超过 800 行失败；函数 ≤ 50 行 | `tools/repo/check_sizes.py`、lizard |
| 纯核心隔离 | `lib/core` 等目录不能 include Arduino/M5/esp 头文件 | CI 里的 grep |
| 固件预算 | 静态 RAM ≤ 96KB；app 镜像不超过上限；调用链的栈预算 | `scripts/check_budget.py`、栈检查脚本 |
| 正式版不含测试钩子 | 正式版固件里不能出现 `debug.key` | CI 里的 grep |
| daemon 依赖 | 运行时只依赖 pyserial | `uv tree` |

真机 HIL **不进 CI**：它需要一台物理设备，只在第 ⑤ 步由 agent 或人来跑，结果写进 `docs/07-hil-checklist.md`。

## 4. 设备操作规矩（所有碰真机的 agent 都必须遵守）
1. 后台服务 `cpad`（LaunchAgent）平时占着串口。要用设备之前，先运行 `cpa pause <秒数>`，再用 `lsof /dev/cu.usbmodem*` 确认串口已经空闲。
2. 只能用"安全打开"方式打开串口：打开后先放低 RTS，再放低 DTR（见 docs/03 §3）。
3. 刷固件用 `pio run -t upload`。**绝对不按、也不让人按 G0**（G0 会锁存下载模式，见 docs/03 §2）。
4. 如果设备卡在下载模式、正常复位也救不回来，就停下来报告"需要断电"。
5. 测完要把**正式版固件**刷回去，并恢复 daemon，然后用 `cpa status` 确认 `link: ready`。

## 5. CI / CD

- **CI**（`.github/workflows/ci.yml`）：每次推送和 PR 都跑三组检查：协议契约和仓库工具；固件（native 测试、覆盖率、编译、预算、规模）；daemon（lint、测试、覆盖率、HIL 工具的单元测试）。
- **CD**（`.github/workflows/release.yml`）：推送 `vX.Y.Z` 标签时触发。它会编译正式版固件，再生成 `-app.bin`（从 0x10000 烧，也留给 Launcher 用）、`-full.bin`（从 0x0 整片烧）和 `SHA256SUMS`，最后用 `docs/releases/vX.Y.Z.md` 作为说明发布 GitHub Release。**没有发布说明的话，这个工作流会直接失败**，这样每个版本都一定有记录。
- 第三方 Action 一律锁定到 commit SHA，并在注释里写上对应的版本号。改版本前先用 `gh api` 核对 SHA。

## 6. 分支与提交
- `main` 只通过 PR 合并。每个里程碑开一个 `release/vX.Y.Z` 分支，里程碑里比较大的独立工作可以再开 `feat/*` 分支。
- 提交信息遵循 conventional commits（`feat`/`fix`/`docs`/`test`/`ci`/`chore`/`refactor`/`perf`），按模块拆成多次提交，结尾附上 Co-Authored-By。
- 版本号遵循 SemVer：固件和协议有不兼容变化时，升协议主版本（`v` 字段），并更新契约测试向量。

## 7. 文档地图
| 文档 | 内容 |
|---|---|
| 01 | Mac 连接与初始化（官方文档整理） |
| 02 | 现有项目调研报告 |
| 03 | 硬件联调经验（实测） |
| 04 / 05 / 06 | v0.1 的 PRD、架构与协议、工作包 |
| 07 | HIL 检查单 |
| 08 | 本文：开发流程 |
| 09+ | 之后每个里程碑的设计文档 |
| adr/ | 架构决策记录 |
| gates/ | 闸门实验记录（CLI、Launcher、SD 卡等） |
| releases/ | 每个版本的发布说明（CD 会用到） |
| devlog/ | 工作流运行日志 |
