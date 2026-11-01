[English](cli-gates.md) · **中文**

# CLI 闸门实测：G7 · G8 · G3 · G4（附 G9 的一部分）

> 2026-10-19 在本机实测，没有接设备。任务来源：[06-work-packages.md](../06-work-packages.zh-CN.md) 末尾的 Mac/CLI 闸门；规格见 [05-architecture.md](../05-architecture.zh-CN.md) §7.5 和 §6.5。
> 下文 `<T>` 代表 `$TMPDIR` 的 realpath（`/private/var/folders/…/T`）。其他 session 的名字和 cwd 已经隐去。

## 0. 环境与前提（先读）

| 项 | 值 |
|---|---|
| CLI | `~/.local/bin/claude` → `~/.local/share/claude/versions/2.1.272`，`claude --version` = `2.1.272 (Claude Code)` |
| 系统 | macOS 26 |
| 执行位置 | Claude 桌面端里的一个 agent shell（不是普通终端） |
| 认证 | `claude auth status` → `loggedIn:false, authMethod:"none"`；daemon.log：`auth: no token found, will re-check keychain every 30s` |

**影响**：这次起的每个后台 session，第一轮都只得到一条合成回复 `Login expired · Please run /login`。transcript 里这条回复的 `model:"<synthetic>"`、`isApiErrorMessage:true`，说明**模型一次也没被调用**。所以：

- 解析格式、`--` 的处理、权限模式、session ID 是否保持这些问题，都能确定回答。
- "模型是否真的回复了新 prompt"、"等权限 / 提问 时的 status"这两类问题，在这里没法复现。第 7 节给了复测脚本，在已经登录的普通终端里跑一遍就能补上。
- 可能的原因有两个：独立 CLI 本来就没登录过（桌面端用的是自带的 2.1.299，认证由宿主提供）；或者 agent shell 的沙箱读不到钥匙串。按安全规则，这次没有去查凭据，这一点留给用户在终端里执行 `claude auth status` 确认。

**安全措施**：每个 session 都开在 `mktemp -d "$TMPDIR/cpa-gate-*.XXXXXX"` 新建的空目录里；除了 G8 的对照组故意不加 `--`，都显式传了 `--permission-mode plan`；prompt 都是 "Reply with the single word OK" 这种无害文本；没有用 bypassPermissions 或 `--dangerously-*`。一共 **5 个 session**（含 1 个 resume 复制出来的副本），全部 `claude stop` + `claude rm` 了。结束时 `claude agents --json --all` 回到基线（只剩 2 个桌面端 interactive session），`claude daemon status` = `not running`，4 个临时目录也删了。

## 1. 结论总表

| 闸门 | 结论 | 对规格的影响 |
|---|---|---|
| G7 短 id | ✅ 通过 | stdout 第 1 行是 `backgrounded · <8hex>[ · <name>][ (…)]`，`<8hex>` = `sessionId[:8]` = `agents --json` 的 `id`。**`kind` 实际输出 `"background"`，不是 `"bg"`** |
| G8 `--` 与 plan | ✅ 通过 | 加了 `--`，`-c test` 被当成 prompt，日志里能看到 `⏸ plan mode on`。对照组不加 `--`，`-c` 被当成 `--continue`，session 崩溃，**但 CLI 退出码仍然是 0** |
| G3 stop→resume | ✅ 通过，但**要改 argv** | `claude --bg --resume <sessionId> -- <text>`（**不带任何其他 flag**）：ID 不变，新 prompt 进入同一个 transcript，plan 模式保留。**照 05 §7.5 的写法加 `--permission-mode`，会复制出一个新 session**。模型回复本身没验证到（认证问题） |
| G4 status 语义 | ⚠️ 部分 | 观察到 `busy` / `idle`，后台 session 的 `state` 有 `blocked`、`stopped`、`failed`。`waiting`、`shell`、等权限、提问都复现不了（模型调不到）。桌面端 session 会被列出；`daemon`、`daemon-worker` 从来没出现在 JSON 里 |
| G9（只做了不费事的部分） | ⚠️ 部分 | 从没被信任过的目录**不会卡住**，prompt 直接提交。LaunchAgent 没测。发现一个新风险：session 进程的环境继承自共享的 transient daemon |

## 2. G7：能不能从 `claude --bg` 的 stdout 拿到短 id

```sh
D1=$(mktemp -d "${TMPDIR%/}/cpa-gate-g7.XXXXXX"); cd "$D1"
claude --bg --name cpa-gate/g7 --permission-mode plan -- "Reply with the single word OK" >out 2>err   # 退出码 0，用时 0.73s（含 daemon 冷启动）
```

stdout 是管道（不是 TTY），cpad 调用时也是这种情况。下面是原样内容，分隔符是 U+00B7 `·`，字节为 `\xc2\xb7`：

```
backgrounded · 2a9c4f17 · cpa-gate/g7
  claude agents             list sessions
  claude attach 2a9c4f17    open in this terminal
  claude logs 2a9c4f17      show recent output
  claude stop 2a9c4f17      stop this session
```

只有当 daemon 还没在跑时，stderr 才有这一行：`Starting background service…`

第 1 行另外见过的两种形状（来自 G3 和 G8）：

```
backgrounded · 9b0d2e4c                                                        ← resume 复制出的副本，没有名字
backgrounded · 7a3f5c21 · cpa-gate/g8-nodash (idle — send a prompt to start)   ← 没有 prompt
```

它在 `claude agents --json` 里的样子（`<T>` 已替换）：

```json
{"pid":96190,"id":"2a9c4f17","cwd":"<T>/cpa-gate-g7.c02ZIJ","kind":"background","startedAt":1792455500000,
 "sessionId":"2a9c4f17-d3b8-4e21-a6f5-7c0e9b1d2f38","name":"cpa-gate/g7","status":"idle","state":"blocked"}
```

**结论**：
- 只解析 id，不解析名字：`^backgrounded · ([0-9a-f]{8})\b`（只看第 1 行）。这个 id 永远等于 `sessionId[:8]`，所以 `sid` 和 `short` 实际上是同一个值。
- 后台 session 的字段：`pid id cwd kind startedAt sessionId name status state`。interactive session 只有 `cwd kind name pid sessionId startedAt status`，和 05 §7.5 一致。
- **`kind` 的取值**：`agents --json` 输出的是 `"interactive"` 和 `"background"`。`~/.claude/sessions/<pid>.json` 注册表里写的是 `"bg"`，二进制里的枚举是 `interactive|bg|daemon|daemon-worker`。daemon 的解析器要把 `background` 映射成 `bg`（顺带兼容 `bg`）。如果按 05 §6.5 的字面写成"等于 `bg`"，后台 session 会全部被当成未知 kind。
- `agents --json` 每次耗时约 0.18s（墙钟），daemon 没在跑时也能用，因为它读的是注册表。

## 3. G8：`--` 是否结束选项解析；plan 模式和 `--bg` 一起用是否生效

```sh
D2=$(mktemp -d "${TMPDIR%/}/cpa-gate-g8.XXXXXX"); cd "$D2"
claude --bg --name cpa-gate/g8 --permission-mode plan -- "-c test"
# stdout: backgrounded · b10007f5 · cpa-gate/g8      退出码 0
claude logs b10007f5      # 去掉 ANSI 后：
```

```
❯ -c test
⏸ plan mode on (shift+tab to cycle) · ← for agents
⏺ Login expired · Please run /login
```

transcript 里第一条 user 消息是 `'-c test'`，`permissionMode:"plan"`。

**对照组**（第 5 个 session）：同样的正文，不加 `--`：

```sh
claude --bg --name cpa-gate/g8-nodash --permission-mode plan "-c test"
# 退出码 0
# stdout: backgrounded · 7a3f5c21 · cpa-gate/g8-nodash (idle — send a prompt to start)
# stderr: note: started a copy of that conversation as 7a3f5c21. To continue a session under its own id, pass its full session id …
# agents --json --all: {"id":"7a3f5c21",…,"state":"failed"}
# daemon.log: bg settled 7a3f5c21 (crashed): exit 1 before init — error: unknown option '- test'
```

**结论**：
- `--` 能结束选项解析，以 `-` 开头的正文会原样成为 prompt。PRD 的 L6 推断被证实了：不加 `--`，`-c` 会被当成 `--continue`。
- `--permission-mode plan` 和 `--bg` 一起用是生效的：footer 显示 plan，transcript 里记的是 `permissionMode:"plan"`。
- **新发现**：session 起来以后马上崩溃，`claude --bg` 照样返回 0 并打印 `backgrounded`。所以 cpad 不能只看退出码判断成败。拿到 short 以后，应该再查一次 `agents --json --all`，`state` 是 `failed` 或 `crashed` 就当失败处理。
- 05 的两层防护都保留：argv 里固定带 `--`，另外拒绝以 `-` 开头的正文。顺带确认了：以 `!` 开头的正文（`"!sleep 15"`）在 argv 里**不会**进入 bash 模式，只会作为普通文本发给模型。

## 4. G3：对空闲但进程还活着的后台 session 先 stop 再 resume

前提是第 2 节的 `2a9c4f17`：`status:"idle"`，进程 96190 还活着。

```sh
claude stop 2a9c4f17                    # stdout: "stopped 2a9c4f17"，退出码 0，进程随即退出
claude agents --json                    # 2a9c4f17 不再出现
claude agents --json --all              # {"id":"2a9c4f17",…,"state":"stopped"}（没有 pid，也没有 status）
```

**4a：照 05 §7.5 第 3 步的写法（带 `--permission-mode`）→ 会复制出副本 ❌**

```sh
claude --bg --resume 2a9c4f17-d3b8-4e21-a6f5-7c0e9b1d2f38 --permission-mode plan -- "Reply with the single word SECOND"
# stdout: backgrounded · 9b0d2e4c
# stderr: note: background session 2a9c4f17 keeps its own saved options, so the flags you passed started a copy as 9b0d2e4c. Without flags, the same command continues 2a9c4f17 itself.
```

`--all` 里同时出现了 `2a9c4f17`（stopped）和 `9b0d2e4c`（新的 sessionId，名字照抄 `cpa-gate/g7`）。副本的 transcript 里先是原来的历史，再接上 SECOND。之后把 `9b0d2e4c` stop 并 rm 了。

**4b：不带其他 flag → ID 不变 ✅**

```sh
claude --bg --resume 2a9c4f17-d3b8-4e21-a6f5-7c0e9b1d2f38 -- "Reply with the single word THIRD"
# stdout: backgrounded · 2a9c4f17 · cpa-gate/g7
# stderr: Starting background service…
#         note: woke session 2a9c4f17 with its saved options (--name, --permission-mode).
# agents --json: {"pid":96191,"id":"2a9c4f17",…,"sessionId":"2a9c4f17-d3b8-4e21-a6f5-7c0e9b1d2f38","status":"idle","state":"blocked"}
```

`claude logs 2a9c4f17` 和 transcript 里，同一个文件按顺序出现：`OK` → `Continue from where you left off.`（`isMeta:true`，CLI 恢复时自动插入）→ `No response requested.`（合成）→ `THIRD`（`permissionMode:"plan"`）。footer 显示 `plan mode on`。

**结论**：
- 只要 resume 时**只传 `--resume <完整小写 sessionId>` 和 `-- <text>`**，ID 就不变，新 prompt 进入同一个会话，并且沿用 spawn 时保存的 `--name` 和 `--permission-mode`。
- 05 §7.5 的 reply 第 3 步和 04 US-10 都要改成 `claude --bg --resume <sessionId> -- <text>`，去掉 `--permission-mode <mode>`。权限模式在 spawn 时就固定了，正好符合"默认 plan"的安全要求。
- 检测 fork 用这几个信号，任意一个出现就返回 `E_REPLY_FORKED`：stderr 含 `started a copy`；stdout 的 id 不等于目标 short；`--all` 里多出新的 sessionId。
- `claude logs <id>` 对已停止的 session 会失败（`job not found — it may have already exited`，退出码 1）。
- **没验证到的**：模型是否真的处理并回复了 THIRD（认证问题）。可以确定的是 prompt 已经作为新的用户轮次提交，进入了同一个 sessionId。用第 7 节的脚本补测。

## 5. G4：status 的取值对应什么情况

| 情况 | kind | status | state | 来源 |
|---|---|---|---|---|
| 桌面端 session 正在跑一轮（本次编排 session） | interactive | `busy` | — | 实测 |
| 桌面端 session 一轮结束 | interactive | `idle` | — | 实测 |
| 后台 session 刚 spawn 或刚 resume，这一轮已结束（因"未登录"） | background | `idle` | `blocked` | 实测，5 个 session 都是这样 |
| 后台 session 已 stop（只有 `--all` 能看到） | background | （没有） | `stopped` | 实测 |
| 后台 session 启动就崩溃（只有 `--all` 能看到） | background | （没有） | `failed` | 实测 |
| 等权限确认 / AskUserQuestion 提问 | — | 推测是 `waiting` | 推测是 `blocked` | **没复现**：要模型发起工具调用 |
| REPL 空闲，但还有 shell 在跑 | — | 推测是 `shell` | — | 只读了代码：`status = idle && <条件> ? "shell" : status`，agent 视图把 `busy` 和 `shell` 都显示成 working |

从 2.1.272 二进制里读到的枚举：

- status：`busy shell idle waiting`
- state：`starting running resuming adopted crashed working blocked done stopped failed`
- `waitingFor` 的文案：`"input needed"`、`"dialog open"`、`"sandbox request"` 等
- agent 视图的规则：`tempo === "blocked"` 或 `status === "waiting"` 时显示"需要处理"

**桌面端 session 会不会被列出**：会。两个 Claude 桌面端 session 都出现了，`kind:"interactive"`，注册表里是 `entrypoint:"claude-desktop"`，版本 2.1.299，和 CLI 2.1.272 共用 `~/.claude/sessions/`。第三方 App 用 Agent SDK 起的 claude 进程（本机 ChatCut 的那个）**没被列出**；进程内的 subagent 也不会单独列出。

**`daemon` 和 `daemon-worker` 是什么**：是后台服务自己的进程。`claude daemon run --origin transient`（supervisor）在第一次 `claude --bg` 时按需启动，下面挂着 `claude bg-pty-host` 和 `claude bg-spare`（预热的备用 session 池）。最后一个 client 断开 5s 后，supervisor 会自己退出（daemon.log：`idle 5s with no clients — exiting`）。在两次 daemon 生命周期里，这些进程**都没写注册表，也没出现在 `agents --json` 里**。按 `claude daemon --help`，常驻服务模式在这个版本里被禁用了，这两个 kind 很可能是留给它的。05 §7.5 "跳过这两种 kind" 的规则保留就行。

**结论（部分通过）**：
- 05 的回退规则（收件箱 = 所有不是 `busy` 也不是 `shell` 的 session）可以照用。
- 建议 WP3 解析时把 `state` 也读进来：
  - 后台 session 的 `state:"blocked"` 应该算"等你"。本例的 blocked 来自"未登录"，确实需要人去处理。
  - `state` 为 `failed` 或 `crashed` 的应该报成 spawn 失败。
- 用户在终端里跑完第 7 节的脚本之后，再把 `waiting` 和 `shell` 的语义填进来。

### 5a. 2.1.306 的映射（2026-10-21，#5；读二进制 `printAgentsJson` 加一次只读的 `agents --json` 实测）

| 情况 | status | waitingFor | state（后台） | cpad 的 `need` |
|---|---|---|---|---|
| 等权限确认、ExitPlanMode 计划审批、进入 plan | `waiting` | `permission prompt` | `blocked` | 立即算 |
| AskUserQuestion、MCP 要输入 | `waiting` | `input needed` | `blocked` | 立即算 |
| sandbox / worker / goal 请求 | `waiting` | `sandbox request` / `worker request` / `goal proposal` | `blocked` | 立即算 |
| 有对话框开着（原因不明） | `waiting` | `dialog open` | `blocked` | 立即算 |
| 后台 session 这一轮失败（例如没登录） | `idle` | — | `blocked` | 立即算 |
| 一轮正常结束 | `idle` | — | `done` | 空闲满 `inbox_idle_s` 才算 |
| 正在跑 | `busy`（2.1.306 把 `shell` 也输出成 `busy`） | — | `working` | 不算 |

- `waitingFor` 只在 `waiting` 时出现。解析成 `SessionInfo.waiting_for`：permission / question / dialog（不认识的取值归 dialog，仍然算等你），给 hooks（#33）合并时当作回退的原因。
- 新规则（`inbox/need.py` 的 `waiting()`）：`waiting`，或者后台 session 的 `state` 是 `blocked` 而且没在跑，都立即算"等你"。测试用 `tests/fixtures/agents/agents-2.1.306.json` 和 fake-claude 复现这几种行。
- **还没实测**：登录状态下真的去等权限 / 提问时的输出（第 7 节 G4a 脚本）。上表的 `waiting` 行来自二进制，实测只看到过 `waiting` + `dialog open`。

## 6. G9（只做了不费事的部分）

- **没被信任过的目录**：5 个 session 都开在全新的临时目录里，没有一个卡在信任对话框上，prompt 都立即提交了。✅
- **LaunchAgent 下的环境**：没测。要装一个 LaunchAgent，属于持久配置，不在这次的范围里。
- **新风险（证据是 daemon.log 和 ps，原因属于推断）**：session 进程是 daemon **预先** fork 出来的 `bg-spare`。日志里先是 `bg spare spawned host`，之后才有 `bg claimed-spare <id>`。所以 session 的环境很可能来自启动 daemon 的那个 `claude --bg` 调用方，不一定是 cpad 这次调用的 `spawn_env`。比如用户已经在终端里起过一个后台 session，那 daemon 就是终端启动的，cpad 开出的 session 用的是终端的环境。WP5 跑 `which uv pio node` 那个场景时，要把这一点算进去。
- **认证是硬前提**：daemon 启动时要能在钥匙串里找到 CLI 的 token，否则每个 session 都只会回 `Login expired`。`cpa status` 自检时最好把 `claude auth status` 的 `loggedIn` 一起检查。

## 7. 复测脚本（在已登录的普通终端里，每段最多 1 个 session）

先确认 `claude auth status` 里是 `"loggedIn": true`。每段跑完都要 `claude stop <id>; claude rm <id>`。

```sh
poll() { for i in $(seq 1 40); do claude agents --json | python3 -c \
  "import json,sys;[print(x.get('id'),x.get('status'),x.get('state'),x.get('waitingFor')) for x in json.load(sys.stdin) if x.get('name')=='$1']"; sleep 1; done; }

# G3：模型确实回复了；ID 不变
D=$(mktemp -d "${TMPDIR%/}/cpa-g3.XXXXXX"); cd "$D"
claude --bg --name cpa-gate/g3 --permission-mode plan -- "Reply with the single word OK"   # 记下 <id>
poll cpa-gate/g3                       # 等到 idle；claude logs <id> 里应该有 OK
SID=$(claude agents --json | python3 -c "import json,sys;print([x['sessionId'] for x in json.load(sys.stdin) if x.get('name')=='cpa-gate/g3'][0])")
claude stop <id>; claude --bg --resume "$SID" -- "Reply with the single word SECOND"
claude agents --json --all | grep -c "$SID"   # 应该是 1；claude logs <id> 里应该有 SECOND，stderr 里不能有 "started a copy"

# G4a：等权限（manual 模式，空目录，写一个无害文件）
D=$(mktemp -d "${TMPDIR%/}/cpa-g4.XXXXXX"); cd "$D"
claude --bg --name cpa-gate/g4perm --permission-mode manual -- "create a file named a.txt containing hi"; poll cpa-gate/g4perm

# G4b：提问
claude --bg --name cpa-gate/g4ask --permission-mode plan -- "Use the AskUserQuestion tool to ask me whether I prefer red or blue. Do nothing else."; poll cpa-gate/g4ask
```

## 8. 本次留下的东西

- `~/.claude/projects/-private-var-folders-…-T-cpa-gate-{g4-NXOvYc,g7-c02ZIJ,g8-H4ZBod}/`：`claude rm` 不会删 transcript 目录。里面只有本次的测试 prompt，留着无害，用户可以自己删。
- `~/.claude/daemon.log`：本次第一次启动 daemon 时创建，约 3KB，记的是 daemon 的生命周期。
- 没改过 `~/.claude/settings.json`，没生成 `daemon.json`，也没碰任何真实项目目录。
