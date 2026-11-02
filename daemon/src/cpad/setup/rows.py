"""The setup rows (protocol/setup/v1, docs/27 §2.3-§2.6) from Facts. Pure.

Order is the page's: service, notes, name, device, disk, then the optional stt, hooks and claude. Required rows:
service, notes, device. A fix is an op from the verb table, never a command string: the app builds the argv itself.
Every text a person sees passes display_safe; no row names a serial, dev or host_id. Titles, details and the table
are in the language of the moment (cpad.i18n), English unless it says Chinese.
"""

from __future__ import annotations

from typing import Any

from cpad.filing.text import display_safe
from cpad.i18n import T
from cpad.setup.facts import ClaudeFacts, DeviceFacts, Facts, HooksFacts, NotesFacts, ServiceFacts, SttFacts

Row = dict[str, Any]
REQUIRED = frozenset({"service", "notes", "device"})
SYMBOL = {"ok": "✓", "act": "●", "wait": "◌", "warn": "!", "off": "○", "skip": "–", "na": "–", "err": "✕"}
LABEL = {"service": ("后台服务", "background service"), "notes": ("笔记目录", "notes folder"),
         "name": ("这台 Mac", "this Mac's name"), "device": ("设备", "device"), "disk": ("磁盘加密", "disk encryption"),
         "stt": ("本地转写", "local transcription"), "hooks": ("Claude Code hooks", "Claude Code hooks"),
         "claude": ("Claude Code", "Claude Code")}  # fmt: skip
COMMAND = {"service.install": ("cpa service install", "cpa service install"),
           "hooks.install": ("cpa hooks install", "cpa hooks install"),
           "host.name": ("cpa host name -- 名字", "cpa host name -- NAME"),
           "stt.setup": ("cpa-stt setup 模型文件", "cpa-stt setup MODEL_FILE")}  # fmt: skip
TAKES = ("后台服务会自动连上它：设备上还没传的打字笔记会存到这台 Mac、从设备上删掉；录音也存到这台 Mac，"
         "但设备上留底 7 天，设置 → 存储 →「再交一次」能交给另一台 Mac。"
         "要留在主力 Mac 上的，先插回主力 Mac，等状态栏的 ↑ 和 录 都变成 0。",
         "The background service connects to it by itself: typed notes not yet synced are saved to this Mac and "
         "deleted from the device; recordings are saved to this Mac too, but the device keeps a copy for 7 days, and "
         'Settings → Storage → "Send again" passes them on to another Mac. To keep them on your main Mac, plug it in '
         "there first and wait until ↑ and R in the status bar are both 0.")  # fmt: skip
TAKING = ("后台服务在跑，正在连它：设备上还没传的笔记和录音会存到这台 Mac。",
          "The background service is running and connecting to it: notes and recordings not yet synced are saved to "
          "this Mac.")  # fmt: skip
MODEL_HINT = ("模型在主力 Mac 的 ~/Library/Application Support/CardputerAssistant/models（ls -l 看链接指向哪个文件），"
              "用 AirDrop 拷过来",
              "The model is in ~/Library/Application Support/CardputerAssistant/models on your main Mac (ls -l shows "
              "the file a link points to): AirDrop it over")  # fmt: skip


def label(rid: str) -> str:
    pair = LABEL.get(rid)
    return T(*pair) if pair else rid


def command(op: str) -> str | None:
    pair = COMMAND.get(op)
    return T(*pair) if pair else None


def _row(rid: str, state: str, title: str, detail: str = "", fix: tuple[Row, ...] = (), blocked: tuple = ()) -> Row:
    return {"v": 1, "id": rid, "state": state, "required": rid in REQUIRED, "blocked_by": list(blocked),
            "title": display_safe(title), "detail": display_safe(detail), "fix": list(fix)}  # fmt: skip


def _semver(text: str) -> tuple[int, ...]:
    return tuple(int(p) if p.isdigit() else -1 for p in text.split("+")[0].split("."))


def service_row(s: ServiceFacts) -> Row:
    install = ({"op": "service.install", "label": T("装上", "Install")},)
    if not s.plist:
        return _row("service", "act", T("后台服务还没装", "The background service is not installed"),
                    T("装上以后，登录时自动启动。", "Once installed, it starts at login."), install)  # fmt: skip
    if s.python != s.this_python:
        return _row("service", "warn", T("后台服务跑的是另一份安装", "The background service runs another install"),
                    T("重装一次，让它跑这个 cpa。", "Install it again so it runs this cpa."), install)  # fmt: skip
    if not s.running:
        return _row("service", "act", T("后台服务装了，但没在跑", "The background service is not running"),
                    T("被关掉了就到 系统设置 → 通用 → 登录项 里打开，再装一次。",
                      "If it was turned off, turn it on in System Settings → General → Login Items, then install it "
                      "again."), install)  # fmt: skip
    running, this = _semver(s.version or "0"), _semver(s.this_version)
    if running < this:
        return _row("service", "act", T(f"后台服务是旧版本 {s.version}，这个 cpa 是 {s.this_version}",
                                        f"The background service is the older {s.version}; this cpa is "
                                        f"{s.this_version}"), "", install)  # fmt: skip
    if running > this:
        return _row("service", "warn", T(f"后台服务 {s.version} 比这个 cpa {s.this_version} 新",
                                         f"The background service {s.version} is newer than this cpa {s.this_version}"),
                    T("装一次新版本的 cpa。", "Install the newer cpa."))  # fmt: skip
    return _row(
        "service", "ok", T(f"后台服务在跑，版本 {s.version}", f"The background service is running, version {s.version}")
    )


def notes_row(n: NotesFacts) -> Row:
    if not n.writable:
        return _row("notes", "err", T(f"笔记目录写不进去：{n.shown}", f"Cannot write the notes folder: {n.shown}"),
                    T("换一个目录，或者改它的权限。", "Pick another folder or change its permissions."))  # fmt: skip
    if n.cloud:
        return _row("notes", "warn", T(f"笔记目录在网盘里：{n.shown}", f"Notes folder in a cloud drive: {n.shown}"),
                    T("网盘会把笔记明文传上去；换一个不同步的目录。",
                      "A cloud drive uploads the notes as plain text; pick a folder that does not sync."))  # fmt: skip
    if n.tcc:
        return _row("notes", "warn", T(f"笔记目录在 {n.tcc} 里：{n.shown}", f"Notes folder in {n.tcc}: {n.shown}"),
                    T("后台服务写那里要系统授权；建议用 ~/CardputerNotes。",
                      "The background service needs system permission to write there; ~/CardputerNotes is "
                      "suggested."))  # fmt: skip
    later = "" if n.exists else T("第一条笔记来的时候建好。", "It is made when the first note arrives.")
    return _row("notes", "ok", T(f"笔记目录 {n.shown}", f"Notes folder {n.shown}"), later)


def name_row(name: str | None) -> Row:
    if name:
        return _row("name", "ok", T(f"这台 Mac 叫「{name}」", f'This Mac is named "{name}"'))
    return _row("name", "act", T("这台 Mac 还没有名字", "This Mac has no name yet"),
                T("设备上会显示这个名字；别用真名。", "The device shows this name; do not use your real name."),
                ({"op": "host.name", "label": T("改名字…", "Rename…")},))  # fmt: skip


def _no_device(blocked: tuple[str, ...]) -> Row:
    return _row("device", "wait", T("没发现设备", "No device found"),
                T("常见原因：数据线只能充电；第一次插线时没点“允许配件连接”；设备停在 Launcher 里；设备没开。",
                  "Common causes: a charge-only cable; “Allow accessory to connect” was not clicked the first time; "
                  "the device is in the Launcher; the device is off."), blocked=blocked)  # fmt: skip


def _unbound(d: DeviceFacts, blocked: tuple[str, ...], serving: bool) -> Row:
    if not d.codes:
        return _no_device(blocked)
    if len(d.codes) > 1:
        return _row("device", "act", T(f"桌上不止一块：{'、'.join(d.codes)}",
                                       f"More than one board is plugged in: {', '.join(d.codes)}"),
                    T("拔掉不用的那块。", "Unplug the one you do not use."), blocked=blocked)  # fmt: skip
    return _row("device", "act", T(f"插着一块 Cardputer，设备码 {d.codes[0]}",
                                   f"A Cardputer is plugged in, device code {d.codes[0]}"),
                T(*(TAKING if serving else TAKES)), blocked=blocked)  # fmt: skip


def device_row(d: DeviceFacts, blocked: tuple[str, ...], serving: bool = False) -> Row:
    if d.bound_code is None:
        return _unbound(d, blocked, serving)
    if d.bound_code in d.codes:
        return _row("device", "ok", T(f"已绑定设备码 {d.bound_code}", f"Bound to device code {d.bound_code}"),
                    blocked=blocked)  # fmt: skip
    if d.codes:
        return _row("device", "warn", T(f"插着的不是绑定的那台：{d.codes[0]}。换了设备？",
                                        f"The board plugged in is not the bound one: {d.codes[0]}. A new device?"),
                    T(f"绑定的是 {d.bound_code}。要换：cpa device forget，再插上。",
                      f"The bound one is {d.bound_code}. To switch: cpa device forget, then plug it in again."),
                    blocked=blocked)  # fmt: skip
    return _row("device", "wait", T(f"绑定的设备 {d.bound_code} 没插着",
                                    f"The bound device {d.bound_code} is not plugged in"), blocked=blocked)  # fmt: skip


def disk_row(on: bool | None) -> Row:
    if on is None:
        return _row("disk", "na", T("不知道 FileVault 开没开", "Cannot tell whether FileVault is on"))
    if on:
        return _row("disk", "ok", T("FileVault 已打开", "FileVault is on"))
    return _row("disk", "warn", T("FileVault 没打开", "FileVault is off"),
                T("Mac 上的笔记是明文：系统设置 → 隐私与安全性 → FileVault。",
                  "Notes on this Mac are plain text: System Settings → Privacy & Security → FileVault."))  # fmt: skip


def stt_row(s: SttFacts) -> Row:
    setup = ({"op": "stt.setup", "label": T("设置模型", "Set up a model")},)
    brew = ("brew install whisper-cpp" if s.brew else
            T("先装 Homebrew（https://brew.sh），再 brew install whisper-cpp",
              "Install Homebrew (https://brew.sh) first, then brew install whisper-cpp"))  # fmt: skip
    if not s.whisper and not s.models:
        return _row(
            "stt",
            "off",
            T("本地转写没设置", "Local transcription is not set up"),
            T(f"{brew}，再 cpa-stt setup 你的模型文件。", f"{brew}, then cpa-stt setup your model file."),
        )
    if not s.whisper:
        return _row("stt", "act", T("有模型，没有 whisper-cli", "A model but no whisper-cli"), brew)
    if not s.models:
        return _row("stt", "act", T("有 whisper-cli，还没有模型", "whisper-cli but no model yet"),
                    T("从旧 Mac 拷一个 ggml 模型过来，再 cpa-stt setup 它。",
                      "Copy a ggml model over from your old Mac, then cpa-stt setup it."), setup)  # fmt: skip
    if s.risky:
        return _row("stt", "warn", T(f"模型链接指进 {s.risky}，清理时会断",
                                     f"The model link points into {s.risky} and breaks when that is cleaned"),
                    T("把模型挪到稳的地方，再 cpa-stt setup。", "Move the model somewhere stable, then cpa-stt setup."),
                    setup)  # fmt: skip
    return _row("stt", "ok", T(f"本地转写可用：{s.models[0]}", f"Local transcription works: {s.models[0]}"))


def hooks_row(h: HooksFacts) -> Row:
    install = ({"op": "hooks.install", "label": T("看改动", "Review changes")},)
    if not h.readable:
        return _row("hooks", "warn",
                    T("Claude Code 的 settings.json 读不了", "Claude Code's settings.json is unreadable"),
                    T("先手动修好它（不是合法的 JSON）。", "Fix it by hand first (it is not valid JSON)."))  # fmt: skip
    if not h.ours:
        return _row("hooks", "off", T("Claude Code 的 hooks 没装", "Claude Code hooks are not installed"),
                    T("装了以后设备上能看到 Claude 在做什么。", "With them the device shows what Claude is doing."),
                    install)  # fmt: skip
    if h.this_cpa is not None and set(h.ours) != {h.this_cpa}:
        return _row("hooks", "warn", T("Claude Code 的 hooks 指向另一个 cpa", "Claude Code hooks point to another cpa"),
                    T("重装 hooks，让它们用这个 cpa。", "Install the hooks again so they use this cpa."),
                    install)  # fmt: skip
    return _row("hooks", "ok", T("Claude Code 的 hooks 装好了", "Claude Code hooks are installed"))


def claude_row(c: ClaudeFacts) -> Row:
    if c.configured:
        return _row("claude", "ok", T("设备上能开 Claude Code session", "The device can start Claude Code sessions"))
    if c.found:
        return _row("claude", "act", T("找到了 Claude Code，后台服务还不知道",
                                       "Claude Code is here, but the background service does not know yet"),
                    T("再装一次后台服务，设备上才能开 session。",
                      "Install the background service again so the device can start sessions."),
                    ({"op": "service.install", "label": T("重装", "Reinstall")},))  # fmt: skip
    return _row("claude", "off", T("没找到 Claude Code", "Claude Code not found"),
                T("装好 Claude Code 以后再跑一次 cpa service install，设备上才能开 session。",
                  "Install Claude Code, then run cpa service install again so the device can start "
                  "sessions."))  # fmt: skip


def rows(f: Facts) -> tuple[Row, ...]:
    head = (service_row(f.service), notes_row(f.notes))
    blocked = tuple(r["id"] for r in head if r["state"] != "ok")
    serving = head[0]["state"] == "ok"
    return (*head, name_row(f.label), device_row(f.device, blocked, serving), disk_row(f.filevault), stt_row(f.stt),
            hooks_row(f.hooks), claude_row(f.claude))  # fmt: skip


def all_required_ok(out: tuple[Row, ...]) -> bool:
    return all(r["state"] == "ok" for r in out if r["required"])


def _fixes(r: Row, indent: str) -> list[str]:
    return [f"{indent}→ {found}" for f in r["fix"] if (found := command(f.get("op")))]


def _lines(r: Row) -> list[str]:
    head = f"{SYMBOL.get(r['state'], '?')} {label(r['id'])}  {r['title']}"
    detail = [f"    {r['detail']}"] if r["detail"] else []
    return [head, *detail, *_fixes(r, "    ")]


def _foot(out: tuple[Row, ...]) -> str:
    left = [r["id"] for r in out if r["required"] and r["state"] != "ok"]
    if not left:
        return T("必需的都好了", "all required rows are ok")
    names = [label(i) for i in left]
    return T(f"还差 {len(left)} 步：{'、'.join(names)}", f"{len(left)} required left: {', '.join(names)}")


def table(out: tuple[Row, ...]) -> str:
    return "\n".join([*(line for r in out for line in _lines(r)), _foot(out)])


def _todo_lines(r: Row) -> list[str]:
    need = T("必需", "required") if r["required"] else T("可选", "optional")
    head = f"  {SYMBOL.get(r['state'], '?')} [{need}] {label(r['id'])}{T('：', ': ')}{r['title']}"
    detail = [f"      {r['detail']}"] if r["detail"] else []
    hint = [f"      {T(*MODEL_HINT)}"] if r["id"] == "stt" and (r["state"] == "off" or r["fix"]) else []
    return [head, *detail, *hint, *_fixes(r, "      ")]


def todo(out: tuple[Row, ...]) -> str:
    """Only what is left, required first, in one language (the installer's last screen)."""
    left = sorted((r for r in out if r["state"] not in ("ok", "na", "skip")), key=lambda r: not r["required"])
    if not left:
        return T("这台 Mac 都好了", "this Mac is all set")
    return "\n".join([T("还要做：", "To do:"), *(line for r in left for line in _todo_lines(r)), _foot(out)])
