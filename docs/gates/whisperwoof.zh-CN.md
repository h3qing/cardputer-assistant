[English](whisperwoof.md) · **中文**

# WhisperWoof 集成调研（v0.3 转写·整理·笔记，v0.5 Mac app）

> 2026-10-20，只读调研，**没有 clone、没有运行 WhisperWoof，也没有接设备**。对应 issue #13。
> 读的版本：[h3qing/WhisperWoof](https://github.com/h3qing/WhisperWoof) `main` = `767ae8d`（2026-10-14，即 v2.8.1），全部用 `gh api` 读取。下文 `W/路径:行号` 指 `https://github.com/h3qing/WhisperWoof/blob/767ae8d2315507dd0dca31839e4552e0e4baa175/路径`；"代码搜索"指 GitHub code search（`gh api search/code`，用来找调用点）。
> 标记：**[源]** 读源码或文档得出 · **[本机]** 在这台 Mac 上只看了文件名或"是否存在"，没读任何内容 · **[推]** 由前两者推出 · **[待]** 没验证。

## 0. 结论速览

1. **WhisperWoof 今天没有任何外部进程能用的入口，可以把一段录音交给它转写** [源]。URL scheme `whisperwoof://` 只处理 OAuth 回调；没有打开文件 / 文件关联，没有 AppleScript；HTTP 服务只在 development 渠道启动；IPC 只认它自己的页面；模型服务器只听 127.0.0.1，而且每次启动换密钥（§6）。唯一现成的"外部写入"先例是 Telegram 伴侣 bot：bot 写一个 JSON 收件箱文件，主进程每 10 s 轮询导入，**只收文字**。所以不管选哪个方案，都要在 WhisperWoof 里加代码（是用户自己的仓库，可以提 PR）。
2. **转写能力够用** [源]。SenseVoice（`sense-voice-zh-en`，155 MB，sherpa-onnx 离线）是中英混说最好的本地引擎：作者实测会议 MER 15.6%，RTF 0.03（1 小时约 2 分钟）。本机已经下载了它和清理模型 Qwen3.5 2B [本机]。Whisper 的 Auto 模式"一段录音只认一种语言"，中英混说可能整段变成英文，不适合我们。
3. **"转写一个现成的 WAV"只能在界面里手动做**（Upload Audio）[源]。16 kHz / 16 bit / 单声道 WAV 不经 ffmpeg 直接用，但离线引擎按**固定 15 s** 切段（句子会被切断，没有时间戳），结果存成 DB 笔记，不自动整理。"本机会议转写"那条路好得多：2 分钟一段、在停顿处切、≤20 s 小块、带句子时间戳。Cardputer 录音应该复用这条路，只需小改（§2.3）。
4. **整理和总结都在 renderer 里，而且总结要用户点了才跑** [源]。清理 prompt 每次听写自动跑；"Generate Notes" 总结要在笔记里手动点；Prompt Studio 改过的 prompt 存在 renderer 的 localStorage，主进程读不到。要做到"插上 Mac 就自动出摘要和待办"，WhisperWoof 要加一个主进程后台作业（§3）。
5. **插件系统 = WhisperWoof 当 MCP 客户端** [源]。每次派发临时起一个 stdio MCP server，调一个工具，参数只有 `{text, entryId}`，然后关掉；每次启动后每条命令都要用户在原生对话框里点"允许"；"把听写发给插件"在界面上还没接通。它不是能常驻、能接收事件的插件 API，所以"Cardputer 插件"实际只能是 WhisperWoof 主进程里新加的一个模块（§5）。
6. **加密** [源][本机]：本机存在 `vault/vault.json`，按设计文档这说明静态加密是开着的。外部进程**不能也不该**直接写 WhisperWoof 的数据库或加密收件箱。正确做法是让 WhisperWoof 主进程自己去读一个约定好的目录，再用它自己的函数入库（它写入只需要公钥，锁着也能写）。`~/CardputerNotes` 里的明文归档不在 WhisperWoof 的加密保护范围内（§8）。
7. **推荐**：v0.3 用方案 A「文件契约」：cpad 把校验过的录音和 `manifest.json` 放进 `~/CardputerNotes/recordings/…`；WhisperWoof 新增一个主进程模块去取，用 SenseVoice 转写、整理、总结、入库，再把 `transcript.json` 和 `summary.md` 写回同一目录；cpad 把结果推给设备。v0.5 在 A 上面加方案 B「cpad.sock v1 + WhisperWoof 里的 Cardputer 面板」：socket 做控制面，大文件仍然走 A 的目录。方案 C（cpad 做 MCP server 给 WhisperWoof 调）不适合导入录音，留给 v0.4 给 coding agent 用（§10）。
8. **新发现的风险：麦克风电平** [源][待]。Cardputer 安静时 RMS≈6，Mac 外放 1 kHz 测试音时只有 43–74（int16，sd-card.md §7.2）。WhisperWoof 的静音门限是浮点 RMS 0.001（整段，约合 int16 33）和 0.003（会议小块，约合 int16 98）。如果正常说话的电平只有几十到一百，整段或大部分小块会被当成静音跳过。v0.3 动手前先测 Cardputer 说话时的 RMS，不够就在设备上调增益，或者让 cpad 做归一化。

## 1. 进程架构 [源]

| 进程 | 是什么 | 证据 |
|---|---|---|
| Electron 主进程 | `main.js`（1447 行）+ `src/helpers/`（从 OpenWhispr 继承的管理器；`ipcHandlers.js` 7122 行，395 个 `ipcMain.handle`）+ `src/whisperwoof/bridge/`（WhisperWoof 自己的功能；**只有 bridge 能 import OpenWhispr 代码**） | `W/CLAUDE.md:20,41,80` |
| renderer | 两个窗口：听写浮层（`mainWindow`，Mando）和控制面板（React 19 + Vite + Tailwind v4）。数据只经 `preload.js` 暴露的 IPC 读写 | `W/CLAUDE.md:20` |
| 模型服务器（子进程） | whisper-server（whisper.cpp）、llama-server（llama.cpp，清理模型）、sherpa-onnx WebSocket 服务器（离线：Parakeet / SenseVoice；在线：X-ASR 实时字幕；会议另开一个，端口 6050–6069）。都只听 127.0.0.1，每次启动换密钥（whisper 用随机 `--request-path`，llama 用 `LLAMA_API_KEY`）；sherpa 用打过补丁的自编版本，拒绝带 `Origin` 的握手 | `W/CLAUDE.md:40`；`W/src/whisperwoof/bridge/local-server-auth-pure.js:1-14`；`W/src/helpers/parakeetWsServer.js:51`；`W/docs/security/red-team-2026-09-27.md:45` |
| 原生小工具 | Swift / C：globe（Fn 键）、fast-paste、mic-listener、audio-tap（系统音频，会议的 "Them" 轨）、text-monitor（自动学纠错）、ocr-helper（图片文字）、vault-helper（Touch ID，打成一个 `.app`）、media-remote | `W/electron-builder.json:88-97`；`W/resources/macos-*.swift` |
| MCP 插件 | 派发一次起一个 stdio 子进程，用完关掉 | `W/src/helpers/ipcHandlers.js:3812-3870` |
| ffmpeg | `ffmpeg-static`，转换走管道，不落临时文件 | `W/docs/design/at-rest-encryption.md:113` |

- 菜单栏应用（`LSUIElement: true`，不在 Dock 里），单实例锁，"登录时启动"是设置里的一个开关（`W/electron-builder.json:129`；`W/main.js:230`；`W/src/helpers/ipcHandlers.js:1828-1840`）。**所以 WhisperWoof 不一定在运行**，集成要能容忍它不在。
- 没开 App Sandbox（entitlements 里只有 JIT、禁用库校验、麦克风），同一用户下能读 `~/CardputerNotes`，也能连我们的 AF_UNIX socket（`W/resources/mac/entitlements.mac.plist`）。
- 听写的实际链路在 renderer：`audioManager.processTranscription` = Memory 替换 → 清理（`ReasoningService`）→ 中文标点 → 口述序号变成列表（`W/src/helpers/audioManager.js:1379-1390`）。`core/pipeline/pipeline.ts` 是纯 TS 的编排类，代码搜索 `new Pipeline` 只命中测试和 `eval-dataset.js`，运行时没用它。

## 2. 转写流水线

### 2.1 引擎、模型和中英混说 [源]

| 引擎 | 运行方式 | 语言 | 备注 |
|---|---|---|---|
| Whisper（whisper.cpp），默认 turbo（large-v3-turbo，1.6 GB） | whisper-server，HTTP multipart | 多语言 | Auto 模式只用前 30 s 判一次语言，整段按这种语言解码；中英混说可能整段变成英文。作者明说不修（每次要多跑几遍 encoder）（`W/CLAUDE.md:26`） |
| Parakeet TDT 0.6B v2 / v3 | sherpa-onnx 离线 | 英语和欧洲语言 | 不吃热词（`W/CLAUDE.md:25`） |
| **SenseVoice** `sense-voice-zh-en`（2024-07 int8，155 MB） | sherpa-onnx 离线 | zh / en / ja / ko / yue | 作者的 bench 上中英混说最好：会议 MER 15.6%，RTF 0.03；回复里有逐 token 时间戳、语言、事件标签（`W/src/models/modelRegistryData.json` 的 `parakeetModels["sense-voice-zh-en"]`；`W/docs/design/local-meeting-transcription.md:23-26,41,49-57`） |
| X-ASR zh-en streaming 160 / 480 ms（134 MB） | sherpa-onnx 在线 | zh / en | 实时字幕（Live typing）用，自带标点 |
| 云端：OpenAI / Groq / Mistral 转写；OpenAI Realtime 会议 | HTTP / WebSocket | | 要自带 key，默认关 |

- 模型放在 `~/.cache/openwhispr/`（`W/CLAUDE.md:24`）。本机已有 `sense-voice-zh-en`、`parakeet-tdt-0.6b-v3`、两个 X-ASR、whisper 的 tiny / base / turbo / large-v3 / distil-large-v3，以及 `Qwen_Qwen3.5-2B-Q4_K_M.gguf` [本机]。
- 和中英混说相关的处理：
  - **繁→简**：Whisper 判出 `zh` 但不管字形，会吐繁体；出口统一用 opencc 转简体（`W/src/whisperwoof/core/language/normalize-chinese-script.ts:1-18`，主进程副本 `W/src/whisperwoof/bridge/chinese-script.js`）。
  - **热词劫持**：Auto 模式下英文热词提示会把中文"翻译"成英文；检测到就去掉提示重解一次（`W/src/whisperwoof/core/language/auto-hints.ts:1-12`）。
  - **清理不翻译**：清理 prompt 顶部加一行"和输入同一种语言输出，绝不翻译"；末尾再按这段话的文字构成加一句（例如"中英混合，两种都保留"），放末尾是为了复用 llama-server 的 KV 缓存（`W/src/config/prompts.ts:12-20`；`W/src/whisperwoof/core/language/script-mix.ts:1-12`）。
  - **丢失的全角标点**：Whisper 吐出的 U+FFFD 补回"，"（`W/CLAUDE.md:28`）。
  - **Memory**：用户批准过的误听替换（"super base"→Supabase）。对 sherpa 系引擎这是唯一能纠专名的手段（`W/CLAUDE.md:25`）。

### 2.2 三条路径对比 [源]

| | 听写（Fn） | Upload Audio（上传文件） | 本机会议（v2.5 起） |
|---|---|---|---|
| 入口 | 热键，renderer 录音 | 控制面板里用文件对话框选或拖进去 → `transcribe-audio-file` | 开始会议 → `meeting-transcription-start`，PCM 经 `meeting-transcription-send` 推给主进程 |
| 引擎 | 设置里的转写模型；默认 Live（X-ASR 预览 + 松键后整段重解） | 设置里的本地 provider：`whisper` 或 `nvidia`（= sherpa 系，含 SenseVoice） | 固定 SenseVoice；没下载就走 OpenAI Realtime |
| 切段 | 一段 | sherpa 离线**固定 15 s**（`MAX_SEGMENT_SECONDS = 15`），在线 600 s；Whisper 整段送 server | 每轨满 2 分钟后，在接下来 20 s 里找最安静的 400 ms 切；每段再在 12–20 s 之间最安静的 300 ms 处切成小块 |
| 时间戳 | 无 | 无 | 有：每句按第一个 token 的时间定位 |
| 静音判定 | 少于 2 个有声的 100 ms 窗就跳过 | 整段浮点 RMS < 0.001 直接返回空 | 每个小块 RMS < 0.003 就跳过 |
| 结果去处 | 粘贴 / 剪贴板 / Markdown 笔记，外加 `bf_entries` | DB `notes` 表（`note_type="upload"`，默认 Personal 文件夹），AI 起标题；**不自动清理** | 会议笔记的 `transcript` 列（逐行 JSON），最多每 30 s 存一次 |
| 证据 | `W/CLAUDE.md:27,30` | `W/src/components/notes/UploadAudioView.tsx:322-410`；`W/src/helpers/ipcHandlers.js:1022-1072`；`W/src/helpers/parakeetServer.js:21-25,66-77,148,153-183` | `W/src/helpers/meetingLocalSession.js:9-31`；`W/src/helpers/meetingLocalTranscriber.js:12,26-142`；`W/src/helpers/meetingLocalTranscription.js:33,48,99` |

### 2.3 能不能转写一个现成的 WAV？怎么做？[源]

- **能，但只能由人在界面里操作。** Upload Audio 用系统文件对话框（`select-audio-file`）或拖放拿到路径；主进程用 `resolveUserAudioFile` 检查：必须是绝对路径，realpath 之后是普通文件，扩展名在 `.mp3 .wav .m4a .webm .ogg .flac .aac` 之内；不超过 500 MB；整个文件读进内存再转写（`W/src/whisperwoof/bridge/user-audio-file.js:14-29`；`W/src/helpers/ipcHandlers.js:1022-1072`）。
- 16 kHz / 16 bit / 单声道 PCM WAV 不经 ffmpeg 直接用（`_ensureWav`，`W/src/helpers/parakeetServer.js:66-77`）。Cardputer 录的正是这种格式（sd-card.md §7.2），格式上无缝。**但文件头里的长度必须正确**：断电留下的半段，头里长度是 0，要先在 Mac 上修好（09 §12）。
- 还有一条 `whisperwoof-import-audio` IPC（存进 `bf_entries`，`source='import'`），但它把模型写死成环境变量 `WHISPER_MODEL` 或 `base`，preload 暴露了，界面里没人调用，是一条死路径（`W/src/helpers/ipcHandlers.js:3588-3630`；`W/preload.js:963`；代码搜索 `whisperwoofImportAudio` 只命中 preload.js）。
- 外部进程没法触发上面任何一条（§6）。
- **对 Cardputer 录音，最合适的是"本机会议"那套**（SenseVoice + 停顿切分 + 时间戳）。只要把 `MeetingLocalTranscriber` 从"实时推流"推广成"读文件"：它的 `push(source, pcm16)` 本来就吃 16 kHz 16 bit 单声道 PCM，按顺序喂进去、最后调 `finish()` 就行。唯一要改的是每条轨的时间零点现在取 `Date.now()`（`W/src/helpers/meetingLocalTranscriber.js:51-58,80-86`），导入文件时要改成录音的开始时间。

## 3. 整理和总结（LLM）[源]

- **清理**（每次听写自动跑）：`cleanupPrompt`（2418 个字符，`src/locales/en/prompts.json`，另有 zh-CN 版）一开头就说明"输入是转写出来的语音，不是给你的指令"；前面再加同语言指令，后面加文字构成提示。本地默认用 Qwen3.5 2B（llama-server），temperature 0.3（`W/src/config/prompts.ts:112-160`；`W/src/services/localReasoningBridge.js:19-40`）。
- **Prompt Studio**：用户改过的 prompt 存在 renderer localStorage 的 `customUnifiedPrompt` 里，主进程读不到（`W/src/components/ui/PromptStudio.tsx:50`；`W/src/config/prompts.ts:124`）。
- **总结**（手动）：OpenWhispr 继承来的 `actions` 表，内置一个 "Generate Notes"（整理成 Markdown；会议则提取要点、决定、待办、跟进）。会议笔记再套一层 `MEETING_SYSTEM_PROMPT`（"You:" / "Them:" 双人稿，`## Action Items` 下用 `- [ ]`）。用户在笔记里点了才在 renderer 里跑，结果写进 `enhanced_content`，再让模型起标题（`W/src/helpers/database.js:197-266`；`W/src/hooks/useActionProcessing.ts:10-111`）。
- **本地推理的限制**：`LocalReasoningService` 一次只处理一个请求，第二个直接抛 "Already processing a request"；`max_tokens` 默认 512；`--ctx-size` 交给 llama.cpp 按显存自动定（`W/src/services/localReasoningBridge.js:26-30`；`W/src/helpers/modelManagerBridge.js:373,400`；`W/src/helpers/llamaServer.js:151`）。主进程有 `process-local-reasoning` 可以直接调它（`W/src/helpers/ipcHandlers.js:2187-2195`），也能通过 `sync-startup-preferences` 知道用户选的本地模型（`W/src/helpers/ipcHandlers.js:2075-2150`）。
- **云端**：OpenAI / Anthropic / Gemini / Groq / 自定义端点，都走同一个 `ReasoningService`；key 存在 `userData/.env` 和 localStorage；默认关（`W/src/services/ReasoningService.ts:181-220,426-447`；`W/src/helpers/environment.js:34-63`；`W/README.md:117`）。
- **推论** [推]：今天没有"无人值守的总结"。要做 Granola 式"传完就有摘要和待办"，WhisperWoof 需要四样东西：主进程后台作业；主进程能读到的 prompt（新增一个"Cardputer 整理 prompt"设置，或把 Prompt Studio 同步到主进程）；和听写共用 llama-server 时排队、听写优先；长稿先分段总结再合并（2B 模型，输出默认只有 512 token）。

## 4. 存储

### 4.1 两套"笔记" [源]

| | Markdown 语音笔记 | DB 笔记（从 OpenWhispr 继承） |
|---|---|---|
| 谁写 | Fn+N / Fn+P、剪贴板"存为笔记"、⌘K 的 `/note` | 会议、Upload Audio、手写笔记 |
| 在哪 | `~/Documents/WhisperWoof Notes/`；可改，优先级：环境变量 `WHISPERWOOF_NOTES_DIR` > `whisperwoof-settings.json` 的 `notesDirectory` > 默认 | `userData/transcriptions.db` 的 `notes` 表 |
| 格式 | 一个平铺目录，文件名 `YYYY-MM-DD-HHMMSS.md`，只认不带子目录的 `*.md`；frontmatter：`title`、`date`、`source: voice`、`app: WhisperWoof`，可选 `entry`（`bf_entries` 的 id）、`project`、`project_id`；图片放 `attachments/` | 列：`title, content, note_type, source_file, audio_duration_seconds, enhanced_content, enhancement_prompt, folder_id, transcript, calendar_event_id …`；`transcript` 是逐行 JSON `{text, source: "mic"\|"system", timestamp}`；文件夹表预置 Personal 和 Meetings |
| 加密开着时 | 默认封装成 `*.md.wwenc`；可以选"保持笔记可读" | 整库 SQLCipher |
| 文件变动 | `fs.watch` 去抖 250 ms 刷新界面 | — |
| 证据 | `W/src/whisperwoof/bridge/markdown-route.js:19-40,98-138`；`W/src/whisperwoof/bridge/notes-folder-pure.js:10-48`；`W/src/whisperwoof/bridge/notes-folder.js:21-32,107-121` | `W/src/helpers/database.js:83-123,163-188,338-346,543`；`W/src/whisperwoof/ui/notes/meeting-recording.ts:23-30`；`W/src/utils/parseTranscriptSegments.ts:3-20` |

- 本机两个目录都在：`~/Documents/WhisperWoof Notes` 和 `~/CardputerNotes` [本机]。前者在 `~/Documents` 下，受 TCC 保护，我们的 LaunchAgent（Python）写不进去——这正是 05 §7.8 选 `~/CardputerNotes` 的原因。反过来，WhisperWoof 读 `~/CardputerNotes` 不受 TCC 限制 [推]。
- **一个零代码的小办法和它的坑** [推]：把 WhisperWoof 的笔记目录设成 `~/CardputerNotes`，cpad 的每日笔记 `YYYY-MM-DD.md` 就会直接出现在 WhisperWoof 的 Notes 视图里。但加密开着、又没选"保持笔记可读"时，在 WhisperWoof 里编辑某天的笔记会把它封装成 `.md.wwenc` 并删掉明文；cpad 下次追加会重新建一个明文文件，而 WhisperWoof 读的时候优先读封装版，于是 cpad 新追加的行"看不见"（`W/src/whisperwoof/bridge/vault/vault-files.js:63-68,115-135`）。另外，笔记被封装时 WhisperWoof 不允许换笔记目录（`W/src/whisperwoof/bridge/markdown-route.js:65-72`）。所以只有在"保持笔记可读"打开时才能这么用。

### 4.2 项目 [源]
- `bf_projects(id, name, created_at, integration_target, metadata)`。Fn+P 存进默认项目，没有就自动建 "Inbox"。Markdown 笔记靠 frontmatter 的 `project_id` 归属，所以**只有 WhisperWoof 自己能给笔记归项目**，外部写 frontmatter 拿不到 id（`W/src/whisperwoof/bridge/app-init.js:57-63`；`W/src/whisperwoof/bridge/project-notes.js:31-73`）。
- 和 cpad 的项目（`key / slot / root`，主要是代码仓库）没有对应关系。按名字映射、找不到就建，是合理的做法 [推]。

### 4.3 历史库 [源]
- `transcriptions.db`，WAL 模式，有两个连接（OpenWhispr 的 `database.js` 和 WhisperWoof 的 `app-init.js`）。WhisperWoof 自己的表：

  ```sql
  bf_entries(id TEXT PRIMARY KEY, created_at, source CHECK(source IN ('voice','clipboard','meeting','import')),
             raw_text, polished, routed_to, hotkey_used, duration_ms, project_id, audio_path,
             metadata TEXT /* JSON */, favorite)
  bf_entries_fts USING fts5(raw_text, polished, content=bf_entries)  -- 默认 unicode61 分词，insert/delete/update 三个触发器
  bf_projects, bf_audit_log, 标签表, 链表, bf_image_text
  ```

  OpenWhispr 的表：`transcriptions`、`custom_dictionary`、`notes` + `notes_fts(title, content, enhanced_content)`、`folders`、`actions`、`agent_*`、`google_calendar_*`、`calendar_events`（`W/src/whisperwoof/bridge/app-init.js:41-108,545-600`；`W/src/helpers/database.js:27-346`）。
- FTS5 的 unicode61 会把一整句中文当成一个词，所以 ⌘K 和历史搜索实际用的是 `LIKE`（`W/CLAUDE.md:35`）。删除用 `secure_delete` 和 FTS5 的 `secure-delete`（`W/CLAUDE.md:39`）。
- **新增来源不用改表结构**：Telegram 导入就是 `source='import'` + `routed_to='telegram-companion'` + metadata 里放来源 id（`W/src/whisperwoof/bridge/telegram-sync-pure.js:14-30`）。Cardputer 可以照做：`routed_to='cardputer'`，metadata 放 `rec_id`、`sha256`、`intent` [推]。
- 回放录音只读 `userData/audio` 和 `userData/whisperwoof-images` 里的文件（`resolveAppFile`，`W/CLAUDE.md:36`）。`~/CardputerNotes` 里的 WAV 不能直接在 WhisperWoof 里回放：要么复制进 `userData/audio`（加密开着就封装），要么新加一个受控的根目录。

### 4.4 文件位置
- 生产渠道的 userData 在本机是 `~/Library/Application Support/whisperwoof`（小写），另有 `WhisperWoof-development` [本机]；非生产渠道用 `WhisperWoof-<channel>`（`W/main.js:140-147`）。本机装的是 2.8.0，比最新的 2.8.1 旧一版 [本机]。
- Telegram bot 的默认收件箱路径是 `~/.config/WhisperWoof/telegram-inbox.json`，而 macOS 上主进程轮询的是 `userData/telegram-inbox.json`，两者对不上，不设 `WHISPERWOOF_INBOX` 就收不到（`W/plugins/telegram-companion/index.js:39-40` 对比 `W/src/whisperwoof/bridge/telegram-sync.js:16`）[推，没运行]。教训：跨进程的文件契约，路径要放在双方都显式配置的地方，不能依赖对方的 userData。

## 5. 插件系统（MCP）[源]

- **是什么**：插件就是 MCP server，WhisperWoof 是 MCP 客户端，经 stdio 连接（`@modelcontextprotocol/sdk` ^1.28.0）。一方插件有 Todoist、TickTick、Notion、Slack（各自读一个 API key 环境变量），外加一个 Calendar 条目和 Telegram 伴侣；"任何 MCP server 都能当插件"（`W/plugins/README.md:3-18,43-45`；`W/src/whisperwoof/bridge/plugin-bridge.js:15-21`）。
- **怎么加载**：配置存在 `userData/whisperwoof-plugins.json`（`id, name, description, command, enabled, hotkeyBinding, setup{envKey,…}`），内置默认项在启动时合并进去（`W/src/whisperwoof/bridge/plugin-bridge.js:13-60`）。
- **生命周期**：没有常驻进程。`whisperwoof-dispatch-entry` 每次都是：检查 → 弹原生对话框，让用户允许这条**完全一样的命令**（每次启动、每条命令问一次，答案只记在内存里）→ spawn → `listTools` → 有 `execute` 就调它，否则调第一个工具，参数固定 `{text, entryId}` → 关闭（`W/src/helpers/ipcHandlers.js:3812-3870`；`W/src/whisperwoof/bridge/plugin-bridge.js:94-123`；`W/src/whisperwoof/bridge/plugin-command-pure.js:32-64`）。命令只许可见 ASCII，环境变量只放行插件声明的那一个 key（`W/src/whisperwoof/bridge/plugin-command-pure.js:18-21,71-76`）。
- **权限**：`core/plugins/permissions.ts` 定义了 network / dataTypes / filesystem，`plugin-manager.ts` 是一个完整的 MCP 客户端类，但运行时都没用上（代码搜索只在测试和注释里出现）；实际起作用的闸门只有那个"允许"对话框（`W/src/whisperwoof/core/plugins/permissions.ts:13-56`；`W/src/whisperwoof/core/plugins/plugin-manager.ts:56-186`）。
- **现状**："把听写发给插件"还没接通，README 自己这么写；`whisperwoofDispatchEntry` 只在 preload 里暴露，没人调用。红队报告还提醒 `@whisperwoof` 这个 npm scope 没注册，`npx @whisperwoof/…` 有被抢注的风险（`W/README.md:135,210`；`W/preload.js:988`；`W/docs/security/red-team-2026-09-27.md:136`）。
- **推论** [推]：WhisperWoof 没有"进程内、常驻、能订阅事件"的插件 API。所谓"Cardputer 插件"，实际只能是 WhisperWoof 主进程里新加的一个 bridge 模块（和 `telegram-sync.js` 同级），不是 MCP 插件。

## 6. 外部进程能用的入口（逐项）[源]

| 入口 | 现状 | 证据 |
|---|---|---|
| URL scheme | 注册了 `whisperwoof://`（开发版 `whisperwoof-dev://`），但 `open-url` 只处理 OAuth verifier 和 `upgrade-success`，其余直接忽略 | `W/electron-builder.json:4-7`；`W/main.js:88-92,214-228,536-551,1265-1300` |
| 打开文件 / 拖到图标上 | 没有 `open-file` 处理，也没有 `fileAssociations` | `W/main.js`；`W/electron-builder.json` |
| AppleScript / 快捷指令 | 没有脚本字典 | `W/electron-builder.json:128-133` |
| HTTP | 只有 OAuth 回调桥（127.0.0.1:5199），**只在 development 渠道**启动 | `W/main.js:94,315-317,632-690` |
| IPC | 只认自己的页面（`app-guard.js` 在注册任何 handler 之前装好） | `W/src/whisperwoof/bridge/app-guard.js:1-11`；`W/CLAUDE.md:38` |
| 模型服务器 | 127.0.0.1 + 每次启动换密钥 + sherpa 拒绝 `Origin`；按设计就不给外人用 | `W/src/whisperwoof/bridge/local-server-auth-pure.js:1-14` |
| 命令行 | 没有 | — |
| 目录监视 | 只监视笔记目录（为了刷新界面）。往里放平铺的 `*.md` 会出现在 Notes 视图里（加密开着时明文 `.md` 也照读，因为笔记不在"必须封装"的名单里），但没法归项目、不进 `bf_entries`、不会转写 | `W/src/whisperwoof/bridge/notes-folder.js:107-121`；`W/src/whisperwoof/bridge/vault/vault-files.js:44-68` |
| 轮询收件箱文件 | `telegram-inbox.json`，每 10 s 读一次，条目 `{id, rawText, polished?, from, chatId, durationSec, imported}` → `bf_entries`；锁着时封进加密收件箱；加密开着时导入后从文件里删掉。**只收文字，不收音频** | `W/src/whisperwoof/bridge/telegram-sync.js:16-98`；`W/src/whisperwoof/bridge/app-init.js:533-537` |
| MCP | 只当客户端（§5） | — |
| Webhook（出站） | 能配置（`entry.created` 负载、HMAC 签名、明文 http 只许本机或局域网），但运行时没人调用 `fireWebhooks` | `W/src/whisperwoof/bridge/webhooks.js:143`；`W/src/whisperwoof/bridge/webhooks-pure.js:18-94`；代码搜索 `fireWebhooks` 只命中它自己和测试 |

## 7. 会议转写设计文档：状态和范围 [源]

- 文档标的状态是"草稿，2026-09-26/27 做过 spike 测量"；范围只管录音和转写，不管会议怎么触发，只做 macOS（`W/docs/design/local-meeting-transcription.md:3`）。
- **已经实现**（v2.5.0 起）：SenseVoice 用自己的服务器（端口 6050–6069，不占听写那台）、16 kHz 采集、每轨 2 分钟在停顿处切、≤20 s 小块、句子时间戳、每 30 s 存一次、退出前最多等 20 s 收尾、开始和停止串行执行（`W/CLAUDE.md:31`；`W/README.md:123`）。
- **还没实现**：每个会议一个 `manifest.json`（逐段记录 `recording / queued / done / failed`，作为队列和崩溃恢复的唯一真相）、启动时把没做完的段重新排队、转写时让听写优先、Me / Them 回声去重、可选的 FireRedASR2 精修（MER 13.3%，RTF 0.45）、会议设置项（`W/docs/design/local-meeting-transcription.md:79-113`；`W/CLAUDE.md:31` 原话是 "No rotation, reconnect or checkpoint"）。
- **和我们的关系** [推]：它设计里的"manifest + 段状态 + 崩溃后重排"正好是 Cardputer 录音需要的队列模型。我们的文件契约按这个形状来，WhisperWoof 那边会议和 Cardputer 两种来源就能共用一个队列和 worker。
- Cardputer 只有一个麦克风，在它的模型里就是"只有 mic 轨"。多人同在一个房间需要说话人分离，文档明确说 v1 不做（`W/docs/design/local-meeting-transcription.md:122`）。

## 8. 静态加密对外部写入者的影响 [源]

- 加密开着时：数据库是整库 SQLCipher；音频、图片、笔记、Memory 等文件是 `WWENC1` 格式（每个文件一个随机密钥，封装给 vault 的 X25519 公钥，所以**锁着也能写，只有解锁才能读**）；锁着时数据库关闭，新写入变成 `userData/vault/inbox/` 里的封装条目，解锁后按顺序回放（`W/src/whisperwoof/bridge/vault/wwenc-pure.js:1-14`；`W/src/whisperwoof/bridge/vault/vault-inbox.js:1-27`；`W/docs/design/at-rest-encryption.md:97-125`）。
- 对外部写入者意味着：
  1. **不能直接写数据库**。加不加密都不该写：两个连接、WAL、FTS 触发器，锁着时根本打不开。
  2. **不能往加密收件箱里塞东西**。公钥是公开的，技术上能封，但这正是威胁模型里说的"植入"；收件箱只接受带临时负数 id、逐字段校验过的操作（`W/docs/design/at-rest-encryption.md:200-206`；`W/docs/security/red-team-2026-09-27.md:98`）。
  3. userData 里"必须封装"的存储，明文副本一律不读（`W/src/whisperwoof/bridge/vault/vault-files.js:9-13,44-56`）。
  4. **唯一正确的路**：外部进程把东西放在自己的目录里，WhisperWoof 主进程读了以后用它自己的写入函数入库（锁着就等解锁，或者走它自己的收件箱）。这和 Telegram 一样：导入之前是明文，WhisperWoof 不负责保护（`W/docs/design/at-rest-encryption.md:35,198`）。
- 本机存在 `vault/vault.json` [本机]。按设计，打开加密的第一步写它，关闭加密时最后一步才删它（`W/docs/design/at-rest-encryption.md:146,161`），所以加密应该是开着的。没读内容，不知道"保持笔记可读"开没开。我们的 `~/CardputerNotes` 是用户选定的明文归档，里面的录音、转写、摘要都不受 WhisperWoof 加密保护，需要让用户知情（§12 问题 1）。
- **远期选项** [推]：PRD §5.2 想要"SD 归档的每个文件用 Mac 公钥单独封装"，和 WWENC1 的思路（age 式 ECIES，只用 Node 自带的 crypto）完全一样。可以考虑让 cpad 用 WhisperWoof 的封装公钥封装归档，但这会把两边的格式绑死，v0.3 不做。

## 9. 发布、测试、CI、许可证 [源]

- **发布**：v2.8.1（2026-10-14）。只出 Apple Silicon 的 `.dmg` 和 `.zip`；`VERSION` 文件一变、合进 main 就自动构建并发布；还没有 Developer ID 签名和公证（要等五个仓库 secret），所以不能在应用内自动更新，只提供一个会校验 SHA-256 的 `install.sh`（`W/.github/workflows/release.yml:1-21,44-53,106-121`；`W/docs/release-signing.md:3-15`；`W/README.md:147-153`）。打包后会翻 Electron fuses（禁 RunAsNode / NODE_OPTIONS / --inspect、只加载 asar、校验 asar 完整性）（`W/CLAUDE.md:38`）。
- **测试**：Vitest 只收 `src/whisperwoof/**/*.test.ts(x)`，覆盖率门槛 80%，也只统计 `src/whisperwoof`；树里有 151 个测试文件，README 徽章写 2093 个测试（`W/vitest.config.mts:8-17`；`W/README.md:16`）。所以新代码要放在 `src/whisperwoof/` 下，纯逻辑拆进 `*-pure.js` 才进得了 CI。
- **CI**：ubuntu 上跑测试和 renderer 构建；lint 是软门槛（`|| true`）；typecheck 是硬门槛；macOS 构建只在 PR 上跑（`W/.github/workflows/ci.yml:21-76`）。
- **许可证**：MIT，© 2026 Heqing Huang；fork 自 OpenWhispr（`W/LICENSE:1-3`；`W/README.md:217`）。本仓库是 Apache-2.0。往 WhisperWoof 提的 PR 按 MIT 贡献；从 WhisperWoof 借代码到本仓库（比如停顿切分算法）要在 NOTICE 里保留 MIT 声明。
- **贡献规矩**：先开 issue 讨论再提 PR（`W/README.md:225`）；那边的 CLAUDE.md 要求界面改动遵守 DESIGN.md。

## 10. 集成方案

### 10.1 本仓库这边的约束
- 串口只能由 cpad 一个进程独占（05 §7.1）。任何方案里，WhisperWoof 和它的子进程都**不能**碰 `/dev/cu.usbmodem*`。
- cpad 的控制通道是 `$CPA_HOME/cpad.sock`（默认 `~/Library/Application Support/CardputerAssistant/cpad.sock`，0600）。今天只有 `status` 和 `pause`，一个连接一条请求，读超时 5 s，请求不超过 1024 B，永远不开 TCP（05 §7.9；`daemon/src/cpad/control/server.py:1-3,22-24,46-58`）。
- 笔记目录 `~/CardputerNotes` 不受 TCC 保护（05 §7.8），用户已确认用它做归档。
- 录音格式：16 kHz / 16 bit / 单声道 WAV，每 5 分钟一段，断电可能留下头里长度为 0 的段（09 §12；sd-card.md §7）。
- 传输：v0.2 的 base64 只有 138 KB/s（1 小时约 115 MB，要传 14 分钟），v0.3 要换二进制分帧或 U 盘模式；协议预留了 `voice.begin / chunk / end`（05 §6.10；sd-card.md §1）。这件事和 WhisperWoof 集成无关：集成只从"Mac 上已经有一个校验过的 WAV"开始。
- 新消息类型要先写 schema 和测试向量（05 §6.9）；单行不超过 2048 B（05 §6.7），所以推给设备的摘要要截断或分页。

### 10.2 方案 A（推荐 v0.3）：文件契约 `~/CardputerNotes/recordings`

**目录布局**

```
~/CardputerNotes/
  2026-10-21.md                          cpad 现有的每日笔记（格式不变）；整理完后加一行链接
  recordings/2026-10-21/K3M9Q2T-00042-0A1C/
    audio.wav                            cpad：多段合成一个，修好文件头，16k / 16bit / mono
    manifest.json                        cpad：最后写（tmp + rename）；它出现 = "可以处理了"
    transcript.json                      WhisperWoof：逐句 {start_ms, end_ms, text, lang}，以及引擎、模型、版本
    summary.md                           WhisperWoof：frontmatter + 摘要 / 要点 / 待办（- [ ]）；最后写，它出现 = "做完了"
    error.json                           WhisperWoof：失败时写（原因、次数、下次重试时间）
```

**`manifest.json` v1（草案）**

```json
{
  "v": 1,
  "kind": "cardputer.recording",
  "rec_id": "K3M9Q2T-00042-0A1C",
  "intent": "memo",
  "started_at": "2026-10-21T09:12:03.120+08:00",
  "started_at_approx": false,
  "project": {"key": "acmeshop", "name": "AcmeShop"},
  "audio": {"file": "audio.wav", "sample_rate": 16000, "channels": 1, "bits": 16,
            "duration_ms": 183400, "bytes": 5868844, "sha256": "<64 位十六进制>"},
  "segments": 1,
  "device": {"fw": "cpa-fw/0.3.0"}
}
```

- `rec_id` 用设备的 MonoId（E7-B5-S4，Crockford base32，见 09 §3.5），既是目录名也是幂等键。
- `intent` 在设备上用一个键选（随手记 `memo` / 会议 `meeting` / 待办 `todo`），不用打字；它决定 WhisperWoof 用哪个整理 prompt。
- 设备没对过时，`started_at` 用接收时间并标 `started_at_approx: true`（和 05 §7.8 的 `~` 前缀一个意思）。

**交接规则**
1. cpad 写完 `audio.wav` 并 fsync，再用 tmp + rename 写 `manifest.json`（APFS 同目录 rename 是原子的）。WhisperWoof 只看有 `manifest.json` 的目录。
2. WhisperWoof **只读不改** `audio.wav` 和 `manifest.json`；结果用 tmp + rename 写，`summary.md` 最后写。
3. 幂等：WhisperWoof 用 `(rec_id, sha256)` 判断做没做过（记在自己库里，比如 `bf_entries.metadata`）；cpad 用"有没有 `summary.md`"判断。任何一方崩溃后重来都安全。
4. 两边都不删对方写的文件。原始 WAV 保留多久由用户定（§12 问题 2），由 cpad 执行。

**WhisperWoof 里的处理**（建议新模块 `src/whisperwoof/bridge/cardputer-inbox.js` + `cardputer-inbox-pure.js`）
1. **发现**：启动时扫一遍，之后用 `fs.watch`，再加 10–30 s 轮询兜底；像 `startTelegramSync` 一样从 `app-init.js` 启动。目录可以在设置里改，默认 `~/CardputerNotes/recordings`。
2. **校验**（全部当不可信输入，按它红队报告的标准）：`rec_id` 正则；realpath 必须在根目录之内，拒绝符号链接；`audio.wav` 有大小上限（按时长算，例如 4 小时约 460 MB，沿用 file-import 的 500 MB）；WAV 头必须是 PCM 16k / 16bit / mono，长度和文件大小一致；sha256 对得上；文本字段限长。
3. **等待条件**：加密开着就等解锁（数据库才能写）；用户正在听写就先暂停（会议设计里的"听写优先"）。
4. **转写**：SenseVoice（离线 sherpa 服务器，可以借用会议那台独立服务器），用推广后的 `MeetingLocalTranscriber`（时间零点 = `started_at`）；每句做 Memory 替换、繁简和标点规范化。
5. **整理**：按 `intent` 选 prompt。`memo`：清理 + 一句话标题 + 要点 + 待办；`meeting`：沿用 `MEETING_SYSTEM_PROMPT` 的格式，但改成单人；`todo`：只抽待办。长稿先按 2 分钟一段分别总结再合并。只用用户在 Intelligence 里选的模型；云端默认不用。
6. **入库**：一条 DB 笔记（建议新建 "Cardputer" 文件夹，`transcript` 列放逐句 JSON，`source` 填 `mic`，`enhanced_content` 放摘要），这样就能直接用 Meetings 标签页那套 Granola 式界面和 "Generate Notes"；再加一条 `bf_entries`（`source='import'`，`routed_to='cardputer'`，metadata 放 `rec_id / sha256 / intent`），进历史和 ⌘K。项目按名字找，找不到就建。
7. **写回**：`transcript.json`、`summary.md` 写进同一个目录（设置里可以关，见 §12 问题 1）。
8. **通知**：用它已有的浮层通知或系统通知，告诉用户"Cardputer 录音已整理"。

**cpad 这边**
- 新增 `recordings/` 模块：收段 → 核对 SHA-256 → 合并 → 修文件头 → 写 `audio.wav` 和 `manifest.json` → 记事件（`rec.archived`）。
- 结果监视：看到 `summary.md` 就解析 frontmatter 和 `- [ ]` 行，记事件（`rec.processed`）；往当天的 `YYYY-MM-DD.md` 加一行（`- HH:MM 录音：<标题> [[recordings/…/summary]] ^cpa-<id>`）；把标题、要点、待办推给设备（新消息类型，先写 schema 和向量；受 2048 B 单行限制，只推短摘要，细节按需再取）。
- `cpa status` 显示"等 WhisperWoof 处理的条数 / 最近一次处理时间"；新增 `cpa rec ls|show|retry`。
- 引擎做成可替换的适配器（PRD §5.2"转写引擎可以替换"），形状照 `AgentAdapter`（05 §7.4）：`TranscriberAdapter`，默认实现就是"交给 WhisperWoof 的目录"。
- 契约本身：JSON Schema 和测试向量放在本仓库 `protocol/` 下，WhisperWoof 的测试里放一份副本，两边 CI 都对着同一组向量跑，防止漂移。

**优点**：两边谁不在都不丢东西（目录本身就是队列）；WhisperWoof 改动最小，形状和它已有的 Telegram 收件箱、会议 manifest 设计一致；不开任何新端口；人和 Obsidian 都能直接看文件；纯逻辑好写测试。
**缺点**：延迟是秒级到几十秒级（Granola 式异步够用，"语音回复 session"那种要 1–2 秒的不够）；设备上的状态要靠 cpad 轮询结果文件；明文归档不受 WhisperWoof 加密保护。

### 10.3 方案 B（v0.5 目标）：cpad.sock v1 + WhisperWoof 里的 Cardputer 面板

- cpad 把控制通道升级到 v1（仍然只走 AF_UNIX 0600，不开 TCP）：
  - `hello`：版本和能力；
  - `subscribe`：长连接，按行推送状态变化（设备在线、存储、项目、session、等你收件箱、队列、录音）；
  - `rec.result`：WhisperWoof 处理完主动通知 cpad，cpad 不用再轮询结果文件；
  - `device.show`：把摘要、待办、选项推给设备；
  - `reply.choices`：给 agent session 的"选择式回复"（#16），候选项可以由 WhisperWoof 的 LLM 生成；
  - 现有的 `status`、`pause` 不变。
- WhisperWoof 加 `src/whisperwoof/bridge/cardputer-link.js`（主进程里 `net.connect` 到 cpad.sock，断了退避重连）；renderer 加一个 "Cardputer" 侧栏页：设备状态、SD 剩余空间、项目看板、等你收件箱、录音列表（点进去是 DB 笔记）、待办。renderer 照它自己的安全规矩，只经受保护的 IPC 拿数据。
- 大文件仍然走方案 A 的目录：socket 是控制面，目录是数据面。所以 A 不是一次性的过渡，B 是叠在 A 上面的。
- 短语音（v0.4 语音回复）走 socket 的请求-应答：cpad 把一个几秒的 WAV 路径发给 WhisperWoof，WhisperWoof 转写后回文字。如果 SenseVoice 服务器常驻，5 秒录音按 RTF 0.03 算约 0.15 s 解码 [推][待]。

**优点**：实时、双向，能做成真正的 Mac app 界面；单一真相仍然是 cpad 的 `events.jsonl`。**缺点**：两边都要改协议、要做版本协商；完整体验要求两边同时在线（不在线时退回 A）。

### 10.4 方案 C（不推荐用于导入）：cpad 做 MCP server，WhisperWoof 当插件调用
- 不合适：WhisperWoof 调 MCP 是"每次临时起进程、调一个工具、参数只有 `{text, entryId}`、调完就关"，没有订阅，没有资源，传不了大音频；每次启动每条命令都要点"允许"；派发还没接到界面上（§5）。而且 WhisperWoof 启动的 MCP 进程绝不能去开串口，它只能做 cpad.sock 的薄客户端——那不如让 WhisperWoof 直接连 socket（方案 B）。
- 合适的用途：v0.4 把 Cardputer 的队列和收件箱暴露给 coding agent（05 §4 已经预选 `mcp==2.3.*`，PRD §5.2 也提到 MCP server）。等 WhisperWoof 接通插件派发后，再顺手加一个"把这段听写发到 Cardputer 队列"的插件目标，成本很低。

### 10.5 备选（现在不做）：cpad 自己转写
- cpad 自己跑 sherpa-onnx（Python 包）+ SenseVoice。好处是不依赖 WhisperWoof；坏处是 WhisperWoof 已经做好的 Memory、清理、繁简、Prompt Studio、界面、加密全要重做，也违背用户"和 WhisperWoof 集成"的决定。只作为适配器接口里的一个备用实现，留给"WhisperWoof 不在、又急需短语音转文字"的情况。
- 不要去调 WhisperWoof 安装包里的二进制，也不要直接用 `~/.cache/openwhispr` 里的模型路径：那是它的内部细节，随时会变。

### 10.6 对比

| | A 文件契约 | B socket v1 + 面板 | C MCP | 备选：cpad 自己转写 |
|---|---|---|---|---|
| WhisperWoof 改动 | 中：一个 bridge 模块 + 推广转写器 + 主进程整理作业 + 设置项 | 大：A 的全部 + socket 客户端 + 新界面 | 中：要先接通插件派发 | 无 |
| 本仓库改动 | 中：录音归档 + 结果监视 + 推给设备 | 大：控制协议 v1 | 中：MCP server | 大 |
| 两边必须同时在线 | 否 | 完整体验需要 | 需要 | 否 |
| 延迟 | 秒到几十秒 | 亚秒级 | 每次 spawn，秒级 | 亚秒级 |
| 和加密的契合 | 好（WhisperWoof 自己入库） | 好 | 一般 | 绕开了 WhisperWoof |
| 新增攻击面 | 一个只读目录（同用户） | 一个 0600 socket（同用户） | 每次 spawn，外加 npx 抢注风险 | 无 |
| 通往 v0.5 | 是 B 的数据面 | 就是 v0.5 | 支线 | 支线 |

### 10.7 改动清单

**WhisperWoof（提 PR，先开 issue 讨论契约）**

| # | 改动 | 涉及 |
|---|---|---|
| W1 | `bridge/cardputer-inbox(-pure).js`：发现、校验、状态、失败退避；从 `app-init.js` 启动 | 新文件；`W/src/whisperwoof/bridge/app-init.js:533` 附近 |
| W2 | 推广 `MeetingLocalTranscriber`：可以传时间零点；加"从 WAV 文件喂 PCM"的入口；选 SenseVoice 服务器 | `W/src/helpers/meetingLocalTranscriber.js:26-86` |
| W3 | 主进程后台整理作业：prompt 放到主进程能读的地方；和听写共用 llama-server 时排队、听写优先；长稿分段总结；`max_tokens` 可调 | `W/src/services/localReasoningBridge.js`；`W/src/config/prompts.ts`；`W/src/hooks/useActionProcessing.ts` 里的 prompt 挪到共享模块 |
| W4 | 入库：DB 笔记（"Cardputer" 文件夹）+ `bf_entries`（`routed_to='cardputer'`）；加密开着时等解锁 | `W/src/helpers/database.js:543`；`W/src/whisperwoof/bridge/app-init.js:711` |
| W5 | 写回 `transcript.json`、`summary.md`；可以关 | 新 |
| W6 | 设置页 "Cardputer"：目录、自动整理开关、引擎、写回开关 | `W/src/whisperwoof/ui/settings/` |
| W7 | 测试：pure 模块单测（manifest 校验、路径约束、WAV 头、幂等、状态机），用本仓库的契约向量 | `src/whisperwoof/core/cardputer/*.test.ts` |
| W8（v0.5） | `bridge/cardputer-link.js` + "Cardputer" 侧栏页 | 新 |

**本仓库**

| # | 改动 | 版本 |
|---|---|---|
| C1 | 契约文档 + JSON Schema + 向量（例如 `protocol/recording-manifest.schema.json`）；写一份 ADR："转写和整理交给 WhisperWoof，经文件契约" | v0.3 |
| C2 | cpad `recordings/`：收段、校验、合并、修头、写归档和 manifest、记事件 | v0.3 |
| C3 | cpad 结果监视 + 每日笔记链接 + 推给设备的新消息类型（先写 schema 和向量） | v0.3 |
| C4 | `cpa rec ls\|show\|retry`；`cpa status` 显示 WhisperWoof 的处理状态 | v0.3 |
| C5 | 固件：录音时用一个键选 intent；显示摘要和待办 | v0.3 |
| C6 | 控制通道 v1（`hello`、`subscribe`、`rec.result`、`device.show`、`reply.choices`） | v0.4–v0.5 |
| C7 | MCP server（给 coding agent 用） | v0.4 |

### 10.8 怎样长成 v0.5 的 Mac app
- v0.5 的"Mac app"就是 WhisperWoof 本身，加一个 Cardputer 侧栏页（方案 B）。cpad 继续作为 LaunchAgent 单独运行，理由：它要独占串口；WhisperWoof 是菜单栏应用、可能没开，cpad 要在这时也能收设备数据；cpad 有自己的 Python 测试和事件日志，没必要搬进已经很大的 Electron 主进程（`ipcHandlers.js` 7122 行）。
- 安装体验：WhisperWoof 的 Cardputer 页提供"安装 / 更新 Cardputer 服务"，内部调用 `cpa service install`；以后可以把 cpad 打成独立可执行文件，放进 WhisperWoof 的 `extraResources`。两边现在都没签名，签名和公证要一起规划。
- 顺序：v0.3 做 A（录音 → 转写 → 摘要和待办回到设备）→ v0.4 做控制通道 v1 的一部分（短语音回复、选择式回复、给 agent 的 MCP）→ v0.5 做 WhisperWoof 面板。

## 11. 风险与待验证

| 风险 | 说明 |
|---|---|
| 麦克风电平和静音门限 | Cardputer 安静时 RMS≈6，Mac 外放测试音 43–74（int16，sd-card.md §7.2）；WhisperWoof 整段门限 0.001（约 33）、会议小块门限 0.003（约 98）（`W/src/helpers/parakeetServer.js:25,148`；`W/src/helpers/meetingLocalTranscriber.js:12,116`）。说话声不够大就会被当成静音。[待] 先在正常距离说几段话测 RMS；不够就在设备上调增益，或由 cpad 合并时归一化（并记进 manifest） |
| SenseVoice 对 Cardputer 音频的准确率 | 作者的 bench 用的是会议录音（mp3，16 kHz），ES8311 麦克风、手持距离、噪声都不一样 [待]。最省事的验证：用户拿一段 Cardputer 录音，在 WhisperWoof 的 Upload Audio 里选 SenseVoice 试一次 |
| 长稿总结质量 | 2B 模型、输出默认 512 token、上下文自动定；1 小时中文稿可能上万 token [待]。用 bench-a 的转写稿试一下分段总结 |
| 抢 llama-server | `LocalReasoningService` 一次只处理一个请求，后台整理和听写撞上就报错 [源]。W3 必须排队 |
| 时间戳零点 | `MeetingLocalTranscriber` 用的是 `Date.now()`，导入文件时不改就全错 [源] |
| 断电半段 | 文件头长度为 0，不修就过不了 WAV 校验 [源]。cpad 合并时修好 |
| WhisperWoof 没在运行 | 菜单栏应用，登录启动是可选的 [源]。cpad 要显示"等 WhisperWoof"，设备上也要有对应状态 |
| 明文归档 | `~/CardputerNotes` 不受 WhisperWoof 加密保护 [源]。由用户决定（§12） |
| 两个仓库的契约漂移 | 同一份 schema 和向量，两边 CI 都跑 |
| 安全门槛 | WhisperWoof 刚做完红队审查（sherpa 越界、IPC 全开放等都修了），新入口要同样严格：路径约束、拒绝符号链接、限制大小、逐字段校验、不执行任何东西 [源] |
| userData 路径 | 生产是小写的 `whisperwoof`；Telegram bot 的默认路径对不上 [本机][源]。我们的契约不依赖对方的 userData |
| 版本和更新 | 本机 2.8.0，最新 2.8.1；没签名，不能自动更新 [本机][源] |

## 12. 需要用户决定的问题

1. **结果放哪、要不要写回明文**：转写和摘要的正本放在 `~/CardputerNotes`（明文，Obsidian 能读），还是只放在 WhisperWoof 的加密库里？建议两边都有：归档里是明文正本，WhisperWoof 里是加密副本，写回可以关。本机看起来开着加密，写回明文意味着这部分内容不受保护。
2. **原始 WAV 留多久**：1 小时约 115 MB。永久保留，还是像 WhisperWoof 的 Audio Retention 那样留 7–90 天？
3. **在 WhisperWoof 里落成哪种笔记**：DB 笔记（Meetings 标签页那套 Granola 式界面 + "Generate Notes"），还是 Markdown 语音笔记（`~/Documents/WhisperWoof Notes`，Obsidian 能读），还是两种都要？建议 DB 笔记 + "Cardputer" 文件夹。
4. **总结能不能用云端模型**：默认只用本地 Qwen3.5 2B；长会议质量不够时，是否允许按次选云端？
5. **录音时的意图键**：随手记 / 会议 / 待办分别用哪个键（不打字）。
6. **WhisperWoof 的 PR 怎么排**：建议先在 WhisperWoof 开一个 issue 讨论契约，然后按 W2 → W1 → W4 → W3 → W5 / W6 拆成几个小 PR。

## 13. 复现（只读）

```bash
SHA=767ae8d2315507dd0dca31839e4552e0e4baa175
gh api "repos/h3qing/WhisperWoof/git/trees/$SHA?recursive=1" --jq '.tree[].path'                 # 文件树
gh api -H "Accept: application/vnd.github.raw" "repos/h3qing/WhisperWoof/contents/<路径>?ref=$SHA"  # 读单个文件
gh api -X GET search/code -f q='fireWebhooks repo:h3qing/WhisperWoof' --jq '.items[].path'         # 找调用点（索引是默认分支）
```

本机观察只用了 `ls` 和"文件是否存在"的检查，没有读 WhisperWoof 的数据库、设置或笔记内容。
