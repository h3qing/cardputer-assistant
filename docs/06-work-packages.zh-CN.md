[English](06-work-packages.md) · **中文**

# 06 · v0.1 工作包与闸门

> 由设计工作流在 2026-10-19 生成，评审修订后定稿。实现 agent 以本文件为任务来源，规格以 [05-architecture.zh-CN.md](05-architecture.zh-CN.md) 为准。
> 能并行的工作包各自拥有**互不重叠的目录**。每个包都按 TDD 来：先写测试（RED），再实现（GREEN），最后重构（REFACTOR）。

## 依赖顺序

```
WP1（协议契约 + 仓库骨架） ──┬──► WP2（固件纯核心） ──► WP4（固件设备层 + UI） ──┐
                             └──► WP3（Mac daemon）  ──────────────────────────┴──► WP5（端到端 HIL）
```

## WP1 · 仓库骨架 + 协议 v0 契约（含 Day-0 闸门记录）

- **拥有的路径**：`LICENSE`, `NOTICE`, `README.md`, `CONTRIBUTING.md`, `SECURITY.md`, `.gitignore`, `.github/`, `protocol/`, `tools/repo/`, `docs/adr/`, `docs/03-hardware-bringup.md (只改 §3 一处更正)`
- **依赖**：无

### 范围

要做：(1) 在仓库根目录 git init，第一次提交用 'chore: bootstrap repo'。保留已有的 .gitignore，追加 .venv/、coverage 产物和 *.map，backups/ 继续排除。(2) LICENSE 用 Apache-2.0。NOTICE 列出 M5Cardputer/M5Unified/M5GFX（MIT）、ArduinoJson（MIT）、efont 字库许可、arduino-esp32 的 LGPL-2.1 说明，以及 ADAPT 来源（cc-buddy-bridge、m5stack-coding-toys、m0rg0t、CardMind，均为 MIT）。README 写骨架，CONTRIBUTING 写草稿（TDD、文件和函数规模、conventional commits），SECURITY.md 写清 v0.1 的信任边界和已知明文风险。(3) protocol/：README（指向 05 §6，写明版本规则）；limits.json，即 05 §6.7 那张表的机器可读版；schema/v0/ 下每种消息一个 JSON Schema，覆盖 05 §6.5 的全部类型，debug.* 也要有；vectors/v0/messages/<type>.json，每种消息至少 1 个合法用例和 1 个非法用例，字节级用例用 line_b64，agent.spawn 要带上全部恶意正文，outbox.item 里出现执行类 kind 必须判为非法；vectors/v0/flows/ 写这几条流程：handshake、tx_parking、outbox_replay_dup_ack、reboot_detect、credit_window、offline_spawn_becomes_queue、agent_result_unknown。tools/check_vectors.py 用 jsonschema 校验全部用例，并检查覆盖面，它的 pytest 要先写。(4) tools/repo/check_sizes.py：超过 400 行警告、超过 800 行失败，支持 --warn/--fail，忽略 .pio、.venv、docs/research，附测试。(5) .github/workflows/ci.yml：先跑 check_sizes、lizard、check_vectors；firmware 和 daemon 两个作业用 hashFiles 判断目录存在才执行。(6) docs/adr/0001-toolchain.md 先写模板，列出 A 和 B 的候选、G2 的四项检查和判定规则，等用户跑完 G2 再把结论填进去；0002-protocol-v0.md 记录 2048/500 的上限、credit 规则，以及执行类操作只走在线请求。(7) 更正 03 §3：HWCDC 的修复只在 Arduino 3.3.9 及以上（PR #12606），3.2.1 仍然需要 kick。不做：任何固件或 daemon 代码，代码生成，HIL 脚本。

### 验收（先写测试）

- (RED 先行) uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/tools -q   # 测试必须覆盖：缺非法用例时报错、outbox.item 携带执行类 kind 时报错
- uv run --with 'jsonschema>=4.23,<5' python protocol/tools/check_vectors.py   # 退出码 0，并打印每种类型的用例数（合法和非法各 ≥1）
- uv run --with pytest pytest tools/repo -q
- python3 tools/repo/check_sizes.py --warn 400 --fail 800 protocol tools
- uvx lizard -L 50 -w protocol/tools tools/repo
- python3 -c "import json;d=json.load(open('protocol/limits.json'));assert d['line_max']==2048 and d['text_max']==500 and d['host_inflight_max']==4096"
- git rev-parse --is-inside-work-tree && git log --oneline -1 | grep -E '^[0-9a-f]+ (chore|docs|feat|test): '
- grep -q 'Apache License' LICENSE && grep -q 'LGPL' NOTICE && grep -q '3.3.9' docs/03-hardware-bringup.md

## WP2 · 固件纯核心库（native 可测：协议、链路、outbox、输入、状态）

- **拥有的路径**：`firmware/app/platformio.ini`, `firmware/app/lib/core/`, `firmware/app/lib/protocol/`, `firmware/app/lib/link/`, `firmware/app/lib/outbox/`, `firmware/app/lib/input/`, `firmware/app/test/core/`, `firmware/app/src/ (仅桩代码，WP4 接手)`
- **依赖**：WP1

### 范围

要做：(1) platformio.ini：三个 env，cardputer-adv、cardputer-adv-test、native，写法照 05 §3；工具链按 ADR-0001，没定之前用 A；使用 -std=gnu++17；ArduinoJson 锁定 7.4.3；native env 设置 lib_ignore=hal,usb_transport，并定义 CPA_PROTOCOL_DIR。(2) core/：FixedStr<N>、Result、Effects/Step、MonoId（Crockford base32 编码；生成和解析；boot 先递增再发 ID 的约束由接口来表达）、AppState 的各个切片，加 static_assert(sizeof(AppState)<=4096)；切片 reducer 包括 projects、sessions（need 新变成 true 时产生 toast 事件，age 在本地往上加）、queue、link info、outbox info、agent 请求状态（在途 ≤1；超时或链路断开时视为结果未知，转成排队草稿）。(3) protocol/：messages.h 定义 05 §6.5 的全部类型，codec.cpp 用一个复用的 JsonDocument，limits.h 镜像 limits.json，errors.h。(4) link/：LineFramer（上限 2048、丢弃不以 { 开头的行、忽略 \r）；LinkSession 是纯状态机（Detached/Attached/Ready；每条主机主动发来的消息恰好回复一条；15s 超时；收到新的 hello 时重新握手，在途条目退回待发）；transport.h 只放接口。(5) outbox/：OutboxCore 是纯函数（按 ID 顺序、在途 ≤4、重试间隔 3/6/12/30s、处理 stored/duplicate/rejected、链路断开时在途退回、容量 200 条且剩余 ≥64KB、启动扫描规则：孤立的 .tmp 删除，.tmp 旁边已有同名 .json 时不删、只计数）；file_store.h 是接口；FakeStore 能在任意一步注入崩溃，放在 test/core/support。(6) input/：KeySet、GhostFilter（矩形检测）、ChordDetector（G0 不到 500ms 算 Tap，满 500ms 算 Long；只有 Backspace 自动重复，500/60ms；按住 Ctrl/Opt/Alt 时不产生 Char）、keymap 表（04 §8 的全部按键），加上 keymap 静态幽灵检查。(7) src/main.cpp 只放桩：引用各个 core 类型，确认能用 xtensa 编译。不做：hal、usb_transport、shell、module_api、模块、任何 Arduino/M5/esp_* 头文件、SD、Leader 键、睡眠、hooks。

### 验收（先写测试）

- (RED 先行，先写测试) cd firmware/app && pio test -e native -f 'core/*'   # 套件：test_line_framer, test_codec_vectors（读 ../../protocol/vectors/v0）, test_link_flows（读 flows）, test_mono_id, test_outbox_core（每一步注入崩溃：0 丢失 0 重复）, test_outbox_boot_scan, test_ghost_filter, test_chord_detector（包括 499/500ms 的边界）, test_keymap_static, test_reducers, test_limits_match_json, test_agent_request_unknown_result
- cd firmware/app && gcovr -r . --filter 'lib/(core|protocol|link|outbox|input)/' --fail-under-line 80
- cd firmware/app && ! grep -rEn '#include\s*[<\"](Arduino([^J]|$)|M5|esp_)' lib/core lib/protocol lib/link lib/outbox lib/input
- cd firmware/app && pio run -e cardputer-adv   # 用 xtensa 和 gnu++17 编译桩代码加核心库
- python3 tools/repo/check_sizes.py --warn 400 --fail 800 firmware/app/lib firmware/app/test
- uvx lizard -l cpp -L 50 -w firmware/app/lib/core firmware/app/lib/protocol firmware/app/lib/link firmware/app/lib/outbox firmware/app/lib/input

## WP3 · Mac daemon cpad 与 cpa 命令行

- **拥有的路径**：`daemon/`
- **依赖**：WP1

### 范围

要做：(1) 用 uv 建 Python 3.12 项目，包名 cpad，入口 cpa。运行时依赖只有 pyserial==3.5；开发依赖有 pytest、pytest-asyncio、pytest-cov、hypothesis、ruff、jsonschema、lizard。(2) protocol/：用 frozen dataclass 表示消息，手写校验器，limits 从 ../protocol/limits.json 读，序列化时 ensure_ascii=False，超过 2048 就从尾部减条目。(3) device/：PortFinder（绑定序列号，30s 退避）、SafeOpen（控制线序列）、Handshake（等 2s 重试，共 3 次；校验 fw 前缀和 v；6s 内释放）、DeviceLink（读线程转 call_soon_threadsafe；credit 窗口 ≤4096；5s ping；15s 掉线；检测 boot 变化后重新同步）。(4) hub/：HubState、reduce、EffectRunner、snapshot 构建（遵守上限，rev 不受 age 影响）。(5) events/：events.jsonl 只追加，每次 fsync，ack 之前必须落盘；按 (dev,id) 去重，索引启动时重建；项目已删除的条目标 orphan 保存。queue/：用纯函数从事件折叠出队列，qid 格式 q<n>。notes/：每日 markdown 带块 ID，写入幂等，启动时补写。(6) projects/：projects.json 的校验（root 必须是存在的目录、存 realpath、不要求 git；key 和 slot 唯一；mode 默认 plan，遇到 bypassPermissions 拒绝加载）；cwd 用最长前缀匹配，匹配不上再用 git common-dir，缓存 60s；检查文件修改时间自动重载。(7) agents/：AgentAdapter 协议，加 claude_code 实现。probe 解析版本和绝对路径；list 只在设备 Ready 时每 3s 跑一次 agents --json，解析要宽松；spawn 的 argv 是 [bin,'--bg','--name',name,'--permission-mode',mode,'--',text]，cwd=root，env=spawn_env，从输出解析 short，解析不到就对比前后两次列表；reply 只在 config.experimental_bg_reply 打开时可用，流程是 stop → resume → 用 agents --json --all 核对 ID，发现 fork 就返回 E_REPLY_FORKED 并关闭 reply；拒绝以 '-' 开头的正文，sid 必须唯一对上。(8) inbox/：need 判定，waiting 立即，idle 满 60s，(sid,st,since) 级别的忽略只存内存。(9) control/：AF_UNIX 0600 的 cpad.sock，支持 pause 和 status。cli/：daemon、status、project add|ls|rm、queue ls|add|done、pause、service install|uninstall（plistlib 生成 plist，用 zsh -lic env 按白名单生成 spawn_env）、device forget。logging/：RedactFilter。启动时检查 $CPA_HOME 的 0700/0600 权限。(10) 测试辅助都放在 daemon/tests：FakeDevice（pty，模拟 TX 被扣住、RX 超过 8192 就丢字节）、tests/fixtures/bin/fake-claude（记录 argv/cwd/env，按 fixture 输出，可以翻转状态）、脱敏后的 agents --json fixture（各种 kind 和 status 都要有，名字用中文）。不做：HTTP 或 TCP 服务、hooks、settings 安装器、MCP、SQLite、pydantic、starlette、配对、mods、转写、队列 markdown、设备离线时的轮询。

### 验收（先写测试）

- (RED 先行) cd daemon && uv run pytest tests/unit tests/contract -q   # contract 测试读 ../protocol/vectors/v0
- cd daemon && uv run pytest tests/integration -q   # 至少包括：test_handshake_tx_parking, test_credit_window_rx_overflow, test_safe_open_never_rts1_dtr0, test_spawn_argv_hostile_texts, test_spawn_default_permission_mode_plan, test_bypass_mode_rejected, test_offline_intents_never_execute, test_events_fsync_before_ack, test_replay_dedupe, test_orphan_item_kept, test_notes_idempotent, test_inbox_debounce_and_dismiss, test_reply_disabled_by_default, test_reply_fork_detected, test_pause_socket_perms, test_foreign_board_released_within_6s, test_no_poll_without_device
- cd daemon && uv run pytest --cov=cpad --cov-fail-under=80
- cd daemon && uv run ruff check . && uv run ruff format --check .
- cd daemon && uv tree --no-dev --depth 1 | grep -vE '^(cpad|└──|├──) ?(pyserial)?' | grep -c . | grep -qx 0 || (echo 'runtime deps must be pyserial only' && false)
- cd daemon && CPA_HOME=$(mktemp -d) uv run cpa project add demo "$(mktemp -d)" --slot 1 && CPA_HOME=$_ true
- python3 tools/repo/check_sizes.py --warn 400 --fail 800 daemon/src daemon/tests && uvx lizard -l python -L 50 -w daemon/src

## WP4 · 固件设备层与界面：HAL、USB 传输、shell、三个模块、组装

- **拥有的路径**：`firmware/app/lib/hal/`, `firmware/app/lib/usb_transport/`, `firmware/app/lib/module_api/`, `firmware/app/lib/shell/`, `firmware/app/modules/`, `firmware/app/src/`, `firmware/app/test/ui/`, `firmware/app/test/device/`, `firmware/app/scripts/`, `firmware/app/.clang-tidy`
- **依赖**：WP2

### 范围

要做：(1) module_api/：Module、ModuleContext、Canvas、Rect、ComposeSpec、ConfirmSpec、OnlineRequest，接口按 05 §5.4。(2) shell/：导航栈（≤4 层）；按键分发顺序为浮层→全局→模块；Compose（只允许 ASCII 单行，1–500B，显示字节计数；spawn/feedback 不能以 '-' 开头；Opt+0..9 改目标，Opt+N/Q/S 改类型；Fn+` 取消，有内容时二次确认）；Confirm（打开后 400ms 内的按键忽略；按 05 §5.6 的表决定执行还是排队；结果未知时把文本存成 uncertain 排队项）；Toast（2.5s，最多 3 条）；StatusBar（● ⟳ ◐ ○、↑N、!N、电量、时间、⚠kN）；提示栏。(3) modules/：board（主页列表加'其他'行；项目页有 Sessions 和队列两个标签，用 , / 切换；Enter 在 session 上进入回复，在队列项上用 qid 开 session；Opt+D 标记完成或忽略；离线时这几项变灰）；inbox（按 need 排序；Enter 回复；Opt+D 忽略；后台 session 显示 attach 提示，interactive 显示'在 Mac 上处理'）；system（04 US-16 列出的字段）。在 src/module_list.cpp 里用静态列表注册。(4) hal/：M5Canvas 全屏 8bpp 加 efontCN_12，实现 Canvas，能检测缺字就画 □；keyboard 把 M5Cardputer 的按键转成 KeySet，加看门狗（集合非空且 10s 不变就重新初始化）；LfsStore 实现 FileStore（tmp、close、rename）；NVS 存 epoch 和开机计数（先递增）；power（用电池 60s 后关背光，任意键唤醒但吞掉这次按键，G0 唤醒后直接进 Compose）；/cfg/projects.json 缓存。(5) usb_transport/：begin 之前先 setRxBufferSize(8192)，再设 TX 4096、超时 0；每 20ms kick 一次；发送队列 ≤4 行，满了只丢 outbox.item。(6) src/main.cpp：SET_LOOP_TASK_STACK_SIZE(16*1024)；每圈最多 4 行入站；按需渲染，最多 20fps；只在 CPA_TEST_HOOKS 下编入 debug.key、debug.state（带时间戳）、debug.reboot。(7) scripts/check_budget.py（静态 RAM ≤96KB，app ≤2.5MB，读 map 文件）；scripts/pre_upload.py（cpa 命令存在就调用 cpa pause 120，不存在就跳过）；.clang-tidy 配置 readability-function-size，LineThreshold 50、NestingThreshold 4。不做：SD 卡、音频、扬声器、Leader 键、睡眠、OTA、WiFi/BLE、历史浏览、设备上的输入法、审批、manifest 或能力位校验、SDL 截图测试、golden 文件。

### 验收（先写测试）

- (RED 先行) cd firmware/app && pio test -e native -f 'ui/*'   # 套件：test_shell_dispatch_order, test_compose_limits_and_dash_guard, test_confirm_400ms_guard, test_confirm_execute_vs_queue, test_agent_result_unknown_saves_queue, test_mod_board, test_mod_inbox_attach_hint_bg_only, test_mod_system, test_status_bar_states（全部用 FakeCanvas 和 FakeContext）
- cd firmware/app && pio test -e native && gcovr -r . --filter 'lib/|modules/' --exclude 'lib/(hal|usb_transport)/' --fail-under-line 80
- cd firmware/app && ! grep -rEn '#include\s*[<\"](Arduino([^J]|$)|M5|esp_)' lib/shell lib/module_api modules
- cd firmware/app && pio run -e cardputer-adv && python3 scripts/check_budget.py .pio/build/cardputer-adv
- cd firmware/app && pio run -e cardputer-adv-test && ! grep -a -q 'debug.key' .pio/build/cardputer-adv/firmware.bin
- cd firmware/app && pio check -e cardputer-adv --skip-packages --fail-on-defect high   # 要通过函数规模和嵌套规则
- (接真机，先执行 cpa pause 600) cd firmware/app && pio test -e cardputer-adv-test -f 'device/*'   # LittleFS 写入改名后重开、NVS 开机计数持久、200 次保存 p95<=100ms、8KB RX 突发 0 丢失、空闲堆>=150KB、栈剩余>=4KB

## WP5 · 端到端 HIL、人工检查单与发布文档

- **拥有的路径**：`tools/hil/`, `docs/07-hil-checklist.md`, `README.md (快速上手一节，接手自 WP1)`
- **依赖**：WP3, WP4

### 范围

要做：(1) tools/hil/run_e2e.py：复用 daemon 的环境（uv run --project daemon），用临时的 CPA_HOME 和 notes_dir，把 PATH 指向 daemon/tests/fixtures/bin/fake-claude；通过测试版固件的 debug.key 和 debug.state 驱动设备；每个场景输出一份 JSON 报告。场景按 05 §9.3 的 1 到 11 实现：握手且不复位、速记闭环、离线补发、丢 ack 去重、debug.reboot 重新同步、看板计数、开 session 的 argv（含 --permission-mode plan 和 --）、离线开 session 变成排队项且 fake-claude 调用 0 次、延迟 p95（状态翻转 100 次 ≤5s，G0 打开 Compose ≤300ms）、真实中文名渲染、1 小时浸泡。另有一个 --real-claude-smoke 场景，只能人工触发，在临时目录里用真的 claude 以 plan 模式开一个 session，然后 stop 并 rm。(2) tools/hil/tests：p95 计算、报告解析、场景选择这些辅助逻辑的单元测试，先写测试。(3) docs/07-hil-checklist.md：G1 的 CER 测试（抄打 10 分钟已知文本，CER ≤0.5%，没有超过 1s 的卡顿）、10 次断电（开关拨 OFF 并拔线）、拔插 USB、电池背光、G0 长按、插一块别的 ESP32-S3 板子、README 计时。每一项都要记录日期和结果。(4) README 的快速上手：刷固件（不按 G0）、cpa service install、cpa project add、看到看板；E5 下载模式的恢复办法。要在全新的 macOS 用户下实测，≤15 分钟。不做：把 HIL 放进 CI、自动化测试里起真的 Claude session、v0.2 的任何功能。

### 验收（先写测试）

- (RED 先行) uv run --project daemon pytest tools/hil/tests -q
- cpa pause 1 || true; uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk --report "$TMPDIR/hil.json"   # 退出码为 0；报告里 latency.p95_board_s<=5 且 latency.p95_compose_ms<=650（v0.4.1 起含 0.35 s 的点两下窗口，gh #51；以前 300）
- uv run --project daemon python tools/hil/run_e2e.py --scenarios soak --minutes 60 --report "$TMPDIR/soak.json"   # 0 丢失，TX 没有卡住
- test -f docs/07-hil-checklist.md && ! grep -q '\[ \]' docs/07-hil-checklist.md   # 人工检查单全部勾选并提交
- (人工，可选) uv run --project daemon python tools/hil/run_e2e.py --scenarios real_claude_smoke --project-dir "$(mktemp -d)"

## 待验证的硬件和 CLI 闸门

- G2 工具链 spike（第 0 天，最多半天，需要真机）：改一下 hwtest，在 A（espressif32@6.10.0）和 B（pioarduino 54.03.21-2）上各跑一遍：先用 M5.Speaker 响一声，再用 !mic 录 2 秒，看 rms 和 zeros。B 还要另外满足三项：握手后往返 ≤100ms（保留 kick）、SD 写入 1MB 后改名、M5Cardputer 1.2.0 / M5Unified 0.2.26 / M5GFX 0.2.33 都能编译。顺带确认 A 在 -std=gnu++17 下能编译。结论写进 ADR-0001
- G1 键盘（第 0 天，人工按键）：用 hwtest 记录原始键码，测 Opt+每一个键、Ctrl+Shift+S、Fn+Shift+A、Ctrl+Alt+W；确认 M5Cardputer 1.2.0 里 Fn+` 就是 Esc，Opt 能被正确上报，; . , / 不按 Fn 时的键码；再抄打一段已知文本 10 分钟，要求 CER ≤0.5%，并且没有超过 1 秒的卡顿或锁死
- G10 LittleFS（M1）：写 tmp、close、rename 之后重新打开，验证原子性；保存 200 次，p95 ≤100ms；10 次真实断电（开关拨 OFF 并拔掉 USB），每次都在看到'已保存'之后立刻断电，要求 0 丢失
- G6 HWCDC 稳定性（M4）：在 begin 之前设置 setRxBufferSize(8192)，发 8KB 突发数据不丢字节；连着 USB 浸泡 1 小时，TX 不卡、消息不丢（v0.1）。8 小时浸泡放到 v0.1.x
- 内存和栈预算（M1/M2）：在全屏 8bpp sprite 加 efontCN_12 的情况下，空闲堆 ≥150KB，最大连续块 ≥64KB；静态 RAM ≤96KB；loop 栈 16KB 时剩余量最低值 ≥4KB
- 中文字形覆盖（M2）：用中文 session 名（例如'设置页面和字体修复'、'Acme Web 登录页面改版'）在设备上渲染，记录缺字情况，看 M5GFX 能不能检测缺字并画成 □
- isPlugged() 在只接充电器（没有数据连接）时的表现：确认会被当成用电池（Detached），从而 60 秒后关背光
- G0 运行时读取：通过 BtnA 读短按（<500ms）和长按（≥500ms）；确认只要不在上电时按住 G0，就不会锁进下载模式。另外做一次 E5 的恢复演练：开关拨 OFF 并拔线，等几秒再插上
- 插一块别的 ESP32-S3 板子（如果手头有）：daemon 要在 6 秒内释放端口，30 秒内不再碰它；序列号绑定之后完全不再碰它
- [Mac/CLI 闸门，不需要真机] G3：对一个空闲但进程还活着的后台 session，执行 claude stop <short> 后再 claude --bg --resume <sessionId> -- <text>，检查 session ID 是否不变（用 agents --json --all 核对），新的 prompt 是否被处理
- [Mac/CLI 闸门] G4：分别制造'等权限确认'、'提问'、'一轮结束'三种情况，对比 agents --json 的 status；确认桌面端 session 是否总是被列出；daemon 和 daemon-worker 这两种 kind 分别是什么
- [Mac/CLI 闸门] G7 和 G8：从 claude --bg 的 stdout 里能不能稳定拿到短 id；--permission-mode plan 和 --bg 一起用是否生效；-- 是否被当成选项结束符（用正文 '-c test' 来测）
- [Mac/CLI 闸门] G9：从 LaunchAgent 开一个 session，让它执行 which uv pio node，看环境是否正常；再在一个从没被信任过的目录里开 session，看会不会卡住

## 需要用户确认的决定（先按默认值推进）

| 问题 | 默认值 | 为什么重要 |
|---|---|---|
| 从设备开出的 Claude Code session 用什么权限模式？（M3 之前必须确认） | 所有项目默认 plan；可以在 projects.json 里按项目改成 acceptEdits 或 manual；auto 只有项目里明确写了才用；bypassPermissions 永远拒绝 | 你本机的 ~/.claude/settings.json 里 defaultMode 是 auto。如果不显式传 --permission-mode，从口袋里误触或丢了的设备上开出的 session，会以 auto 模式直接改代码。默认用 plan 的话，最坏结果只是多出一份计划，等你回到 Mac 审阅 |
| 固件工具链：Day-0 spike 通过后，是否接受 pioarduino（实际上只有一个维护者）作为 v0.1 的工具链？ | 接受。前提是 G2 当天四项检查全部通过（CDC 往返 ≤100ms、扬声器响过之后麦克风 RMS 不为 0、SD 写入后改名、三个库都能编译），并且锁死到 release zip 地址；只要有一项不过，就用 espressif32@6.10.0 | IDF 4.4 已经 EOL，v0.3 做 WiFi/BLE 时会缺安全修复；从 Arduino 2 迁到 3 有破坏性的 API 变化，代码越少迁起来越便宜。反过来说，pioarduino 只有一个人维护，也是供应链风险 |
| 你主要在终端里跑 Claude Code，还是在 Claude 桌面端里跑？是否接受 v0.1 的反馈先进队列，只有后台 session 能直接回复？ | v0.1 不升级 CLI。反馈进项目队列；G3 通过后，可以手动打开 experimental_bg_reply，直接回复空闲的后台 session。v0.2 再评估升级到 2.1.287 以上，用 mods 注入 | 快速反馈是你的核心诉求之一，但 2.1.272 没有办法把文字注入 interactive session。v0.2 选哪条路（mods 还是别的），取决于你实际在哪里跑 session |
| 笔记写到哪里？ | ~/CardputerNotes/YYYY-MM-DD.md，每天一个文件，每条笔记带 Obsidian 块 ID ^cpa-<id> | 如果直接写进放在 ~/Documents 或 iCloud 下的 Obsidian vault，LaunchAgent 里跑的 Python 会被 macOS 的 TCC 挡住，需要你手动给 uv 的 Python 授予完全磁盘访问权限 |
| 不可变规则写的是 No exceptions。是否接受把可变的硬件缓冲限定在 hal/ 和 usb_transport/ 里，作为唯一的例外？ | 接受：串口收发环、显示 sprite、文件句柄这些天生可变的东西只出现在这两个目录（以及 daemon 的外壳层）；所有业务逻辑都是纯函数，状态只整体替换 | 不划这条边界，固件就没法驱动硬件；划得太宽，又会违反你的工程规则 |
| 等你收件箱的判定规则是否合适？ | waiting 立即进入；idle 持续 60 秒以上才进入；只算已登记项目里的 session；可以手动按 Opt+D 忽略，直到它的状态再变化；空闲很久的 session 不会被自动隐藏 | 你在 Mac 前正在用的那个 session 也会变成 idle。规则太松，!N 永远不会归零；规则太严，又会漏掉真正在等你的 session |
| 速记的默认目标跟着当前项目走，还是一律未归属？ | 跟着当前上下文：正在看某个项目页，或者选中了某个 session，就归到那个项目；否则未归属 | 决定你回到 Mac 后要不要再整理一遍，也影响误归类的概率 |
| 设备丢了的话，还没同步的条目是明文，v0.1 能接受吗？ | 接受。收到 ack 就删除，状态栏显示待同步数量，并写进 SECURITY.md；v0.2 评估加 PIN 锁，以及给 SD 卡做加密封装 | 我们不默认开 Flash 加密，因为那是单向操作，开了以后社区刷机工具都用不了。所以拿到设备的人可以读出还没同步的内容 |
| 这些占位名可以吗：仓库和产品名、cpa、cpad、cpa-fw、$CPA_HOME=~/Library/Application Support/CardputerAssistant、LaunchAgent 的 label？ | 先用这些占位名，在首次公开发布之前统一改 | 名字会出现在协议的 fw 前缀、文件路径和 LaunchAgent label 里，发布以后再改，就要处理兼容和迁移 |
| 从设备开出的 session，直接在项目主目录里跑，还是开在独立的 git worktree 里？ | 项目主目录（v0.1 不做 worktree） | 开在 worktree 里可以避免和你正在用的 session 改同一批文件，但会增加清理成本和复杂度；另外，本项目目前还不是 git 仓库 |
