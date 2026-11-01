"""cpa setup check (docs/27 §2.3, §2.6): each row from every combination of facts. Pure: no I/O here."""

from __future__ import annotations

from dataclasses import replace

import pytest

from cpad.setup import rows as r
from cpad.setup.facts import (
    ClaudeFacts,
    DeviceFacts,
    Facts,
    HooksFacts,
    NotesFacts,
    ServiceFacts,
    SttFacts,
    device_code,
)

PY = "/Users/u/.local/share/uv/tools/cpad/bin/python"
CPA = "/Users/u/.local/bin/cpa"
GOOD = Facts(
    service=ServiceFacts(plist=True, python=PY, this_python=PY, running=True, version="0.5.0", this_version="0.5.0"),
    notes=NotesFacts(shown="~/CardputerNotes", exists=True, writable=True, cloud=False, tcc=None),
    label="书房",
    device=DeviceFacts(bound_code="3F7A", codes=("3F7A",)),
    filevault=True,
    stt=SttFacts(whisper=True, models=("ggml-large-v3-turbo.bin",), risky=None),
    hooks=HooksFacts(readable=True, ours=(CPA,), this_cpa=CPA),
    claude=ClaudeFacts(configured="/Users/u/.local/bin/claude", found="/Users/u/.local/bin/claude"),
)

NO_SERVICE = ServiceFacts(False, None, PY, False, None, "0.5.0")
pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture


def row(facts: Facts, rid: str) -> dict:
    return next(x for x in r.rows(facts) if x["id"] == rid)


def test_a_fully_set_up_mac_is_all_ok_in_order():
    out = r.rows(GOOD)
    assert [x["id"] for x in out] == ["service", "notes", "name", "device", "disk", "stt", "hooks", "claude"]
    assert {x["state"] for x in out} == {"ok"}
    assert all(x["v"] == 1 and x["fix"] == [] and x["blocked_by"] == [] for x in out)
    assert r.all_required_ok(out)


def test_required_rows_are_service_notes_and_device():
    assert {x["id"] for x in r.rows(GOOD) if x["required"]} == {"service", "notes", "device"}


@pytest.mark.parametrize(
    ("service", "state", "fix"),
    [(ServiceFacts(False, None, PY, False, None, "0.5.0"), "act", ["service.install"]),
     (ServiceFacts(True, "/old/.venv/bin/python", PY, True, "0.5.0", "0.5.0"), "warn", ["service.install"]),
     (ServiceFacts(True, PY, PY, False, None, "0.5.0"), "act", ["service.install"]),
     (ServiceFacts(True, PY, PY, True, "0.4.1", "0.5.0"), "act", ["service.install"]),
     (ServiceFacts(True, PY, PY, True, "0.6.0", "0.5.0"), "warn", []),
     (ServiceFacts(True, PY, PY, True, "0.5.0", "0.5.0"), "ok", [])],
)  # fmt: skip
def test_service(service, state, fix):
    got = row(replace(GOOD, service=service), "service")
    assert (got["state"], [f["op"] for f in got["fix"]]) == (state, fix)


def test_service_versions_compare_as_numbers():
    older = ServiceFacts(True, PY, PY, True, "0.9.0", "0.10.0")
    assert row(replace(GOOD, service=older), "service")["state"] == "act"


@pytest.mark.parametrize(
    ("notes", "state"),
    [(NotesFacts("~/CardputerNotes", False, True, False, None), "ok"),
     (NotesFacts("~/CardputerNotes", True, False, False, None), "err"),
     (NotesFacts("~/CardputerNotes", False, False, False, None), "err"),
     (NotesFacts("~/Library/Mobile Documents/x", True, True, True, None), "warn"),
     (NotesFacts("~/Documents/Notes", True, True, False, "Documents"), "warn")],
)  # fmt: skip
def test_notes(notes, state):
    assert row(replace(GOOD, notes=notes), "notes")["state"] == state


def test_a_missing_notes_folder_says_it_is_made_later():
    got = row(replace(GOOD, notes=NotesFacts("~/CardputerNotes", False, True, False, None)), "notes")
    assert "建" in got["detail"]


def test_name():
    assert row(GOOD, "name")["title"].endswith("「书房」")
    unnamed = row(replace(GOOD, label=None), "name")
    assert (unnamed["state"], unnamed["required"], unnamed["fix"][0]["op"]) == ("act", False, "host.name")


def test_a_name_is_shown_display_safe():
    assert "\x1b" not in row(replace(GOOD, label="a\x1bb"), "name")["title"]


@pytest.mark.parametrize(
    ("device", "state", "words"),
    [(DeviceFacts(None, ()), "wait", "没发现设备"),
     (DeviceFacts(None, ("3F7A",)), "act", "3F7A"),
     (DeviceFacts(None, ("3F7A", "7C21")), "act", "不止一块"),
     (DeviceFacts("3F7A", ()), "wait", "3F7A"),
     (DeviceFacts("3F7A", ("7C21",)), "warn", "7C21"),
     (DeviceFacts("3F7A", ("3F7A", "7C21")), "ok", "3F7A")],
)  # fmt: skip
def test_device(device, state, words):
    got = row(replace(GOOD, device=device), "device")
    assert got["state"] == state and words in got["title"]


def test_an_unbound_board_warns_that_cpad_takes_its_notes():
    """Until docs/27 O3, cpad binds the first board by itself: the row says so before you plug in."""
    before = replace(GOOD, service=NO_SERVICE, device=DeviceFacts(None, ("3F7A",)))
    got = row(before, "device")
    assert "删" in got["detail"] and "↑" in got["detail"]


def test_the_device_row_waits_for_service_and_notes():
    broken = replace(GOOD, service=ServiceFacts(False, None, PY, False, None, "0.5.0"),
                     notes=NotesFacts("~/x", True, False, False, None))  # fmt: skip
    assert row(broken, "device")["blocked_by"] == ["service", "notes"]
    assert row(GOOD, "device")["blocked_by"] == []


@pytest.mark.parametrize(("filevault", "state"), [(True, "ok"), (False, "warn"), (None, "na")])
def test_disk(filevault, state):
    got = row(replace(GOOD, filevault=filevault), "disk")
    assert (got["state"], got["required"]) == (state, False)


@pytest.mark.parametrize(
    ("stt", "state", "fix"),
    [(SttFacts(False, (), None), "off", []),
     (SttFacts(True, (), None), "act", ["stt.setup"]),
     (SttFacts(False, ("ggml-base.bin",), None), "act", []),
     (SttFacts(True, ("ggml-base.bin",), "Downloads"), "warn", ["stt.setup"]),
     (SttFacts(True, ("ggml-base.bin",), None), "ok", [])],
)  # fmt: skip
def test_stt(stt, state, fix):
    got = row(replace(GOOD, stt=stt), "stt")
    assert (got["state"], [f["op"] for f in got["fix"]], got["required"]) == (state, fix, False)


@pytest.mark.parametrize(
    ("hooks", "state", "fix"),
    [(HooksFacts(False, (), CPA), "warn", []),
     (HooksFacts(True, (), CPA), "off", ["hooks.install"]),
     (HooksFacts(True, ("/old/.venv/bin/cpa",), CPA), "warn", ["hooks.install"]),
     (HooksFacts(True, (CPA, "/old/cpa"), CPA), "warn", ["hooks.install"]),
     (HooksFacts(True, (CPA,), None), "ok", []),
     (HooksFacts(True, (CPA,), CPA), "ok", [])],
)  # fmt: skip
def test_hooks(hooks, state, fix):
    got = row(replace(GOOD, hooks=hooks), "hooks")
    assert (got["state"], [f["op"] for f in got["fix"]]) == (state, fix)


def test_all_required_ok_ignores_optional_rows():
    meh = replace(GOOD, label=None, filevault=False, stt=SttFacts(False, (), None))
    assert r.all_required_ok(r.rows(meh))
    assert not r.all_required_ok(r.rows(replace(GOOD, device=DeviceFacts(None, ()))))


def test_device_code_is_the_docs27_code_of_the_normalised_serial():
    assert device_code("02:00:00:00:00:01") == device_code("020000000001") == device_code("02:00:00:00:00:01".upper())
    assert len(device_code("020000000001")) == 4 and device_code("020000000001").isupper() is not False


def test_no_row_carries_a_serial_or_host_id():
    text = repr(r.rows(replace(GOOD, device=DeviceFacts("3F7A", ("7C21",)))))
    assert "020000000001" not in text and "host_id" not in text and "serial" not in text


def test_table_shows_every_row_with_a_symbol_and_the_command():
    out = r.table(r.rows(replace(GOOD, service=ServiceFacts(False, None, PY, False, None, "0.5.0"))))
    assert out.splitlines()[0].startswith("●") and "cpa service install" in out
    assert "✓" in out and "还差 1 步" in out


def test_a_board_on_a_running_service_does_not_warn_after_the_fact():
    """The installer asked before the service went in; once it runs, the row says what happens, not 'go back'."""
    got = row(replace(GOOD, device=DeviceFacts(None, ("3F7A",))), "device")
    assert "先插回主力 Mac" not in got["detail"] and "这台 Mac" in got["detail"]
    stopped = replace(GOOD, service=NO_SERVICE, device=DeviceFacts(None, ("3F7A",)))
    assert "先插回主力 Mac" in row(stopped, "device")["detail"]


def test_stt_without_homebrew_says_where_to_get_it():
    got = row(replace(GOOD, stt=SttFacts(False, (), None, brew=False)), "stt")
    assert "https://brew.sh" in got["detail"]
    assert "https://brew.sh" not in row(replace(GOOD, stt=SttFacts(False, (), None)), "stt")["detail"]


@pytest.mark.parametrize(
    ("claude", "state", "fix"),
    [(ClaudeFacts(configured=None, found=None), "off", []),
     (ClaudeFacts(configured=None, found="/u/.local/bin/claude"), "act", ["service.install"]),
     (ClaudeFacts(configured="/u/.local/bin/claude", found="/u/.local/bin/claude"), "ok", [])],
)  # fmt: skip
def test_claude(claude, state, fix):
    got = row(replace(GOOD, claude=claude), "claude")
    assert (got["state"], [f["op"] for f in got["fix"]], got["required"]) == (state, fix, False)


def test_claude_missing_says_to_rerun_the_service_install_after():
    got = row(replace(GOOD, claude=ClaudeFacts(None, None)), "claude")
    assert "cpa service install" in got["detail"]


TWO_LEFT = replace(GOOD, service=NO_SERVICE, device=DeviceFacts(None, ()))
MEH = replace(GOOD, service=NO_SERVICE, label=None, stt=SttFacts(True, (), None))


def has_han(text: str) -> bool:
    return any("\u4e00" <= ch <= "\u9fff" for ch in text)


def test_table_foot_names_the_required_rows_left():
    out = r.table(r.rows(TWO_LEFT))
    assert "还差 2 步：后台服务、设备" in out and "required left" not in out


def test_todo_lists_only_rows_that_are_not_ok_in_one_language():
    out = r.todo(r.rows(MEH))
    assert out.startswith("还要做：") and "To do" not in out
    assert "[必需] 后台服务：" in out and "[可选] 这台 Mac：" in out and "required" not in out
    assert "→ cpa service install" in out and "→ cpa host name -- 名字" in out
    assert "磁盘加密" not in out and "Claude Code hooks" not in out
    assert out.index("后台服务") < out.index("这台 Mac")


def test_todo_tells_where_the_model_is_on_the_main_mac():
    out = r.todo(r.rows(replace(GOOD, stt=SttFacts(True, (), None))))
    assert "AirDrop" in out and "models" in out and "用 AirDrop 拷过来" in out and "on your main Mac" not in out


def test_todo_when_everything_is_done():
    assert r.todo(r.rows(GOOD)) == "这台 Mac 都好了"


# --- English (the default) ----------------------------------------------------------------------------


def test_english_rows_table_and_todo_have_no_chinese_of_their_own(en):
    for facts in (GOOD, TWO_LEFT, MEH, replace(GOOD, device=DeviceFacts(None, ("3F7A",)), filevault=False)):
        out = r.rows(replace(facts, label=None))
        assert not any(has_han(x["title"] + x["detail"]) for x in out)
        assert not has_han(r.table(out)) and not has_han(r.todo(out))


def test_english_table_foot_and_todo(en):
    assert r.table(r.rows(TWO_LEFT)).splitlines()[-1] == "2 required left: background service, device"
    out = r.todo(r.rows(MEH))
    assert out.startswith("To do:") and "[required] background service: " in out
    assert "[optional] this Mac's name: " in out and "→ cpa host name -- NAME" in out
    assert "→ cpa-stt setup MODEL_FILE" in out and "AirDrop it over" in out
    assert r.todo(r.rows(GOOD)) == "this Mac is all set"
    assert r.table(r.rows(GOOD)).splitlines()[-1] == "all required rows are ok"


def test_english_titles_name_the_facts(en):
    assert row(GOOD, "name")["title"] == 'This Mac is named "书房"'
    assert row(GOOD, "device")["title"] == "Bound to device code 3F7A"
    assert row(replace(GOOD, service=ServiceFacts(True, PY, PY, True, "0.4.1", "0.5.0")), "service")["title"] == (
        "The background service is the older 0.4.1; this cpa is 0.5.0"
    )
    unbound = row(replace(GOOD, service=NO_SERVICE, device=DeviceFacts(None, ("3F7A",))), "device")
    assert "deleted from the device" in unbound["detail"] and "↑" in unbound["detail"]
