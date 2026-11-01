"""cpa-stt pure parts: the sandbox profile (§4.5), stdout parsing (§4.3), the phantom-text guards (§4.9) and
the contract documents it writes (§4.7)."""

from __future__ import annotations

import hashlib
import json

import pytest

from cpa_stt import contract_out, gate, parse, profile
from cpa_stt.parse import Sentence
from cpad.recordings import contract
from tests.support import stt_tree

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture

SID = stt_tree.SID
WD = "/private/var/folders/x/T/cpa-stt-abc"
CACHE = "/private/var/folders/x/C"
MACH = ("com.apple.MTLCompilerService", "com.apple.system.opendirectoryd.membership")

# --- sandbox profile ---------------------------------------------------------------------------


def test_profile_denies_network_and_writes_outside_the_workdir():
    text = profile.build(WD, CACHE, MACH)
    lines = text.splitlines()
    assert lines[:3] == ["(version 1)", "(allow default)", "(deny network*)"]
    assert f'(deny file-write* (require-not (subpath "{WD}")))' in lines
    assert (
        '(allow file-write* (literal "/dev/null") (literal "/dev/dtracehelper")'
        f' (subpath "{CACHE}/com.apple.metal"))' in lines
    )
    assert lines.index("(deny mach-lookup)") < len(lines) - 1
    assert lines[-1] == (
        '(allow mach-lookup (global-name "com.apple.MTLCompilerService")'
        ' (global-name "com.apple.system.opendirectoryd.membership"))'
    )


def test_profile_without_a_cache_dir_or_services():
    text = profile.build(WD, None, ())
    assert "com.apple.metal" not in text and "global-name" not in text
    assert text.splitlines()[-1] == "(deny mach-lookup)"


@pytest.mark.parametrize("bad", ['/tmp/a"b', "/tmp/a\\b", "/tmp/a\nb", "relative/wd"])
def test_profile_refuses_paths_it_cannot_quote(bad):
    with pytest.raises(ValueError):
        profile.build(bad, CACHE, MACH)


# --- stdout -----------------------------------------------------------------------------------


def test_parse_reads_timestamps_and_trims():
    out = b"\n[00:00:03.000 --> 00:00:07.760]   Ask not what your country can do for you.\n"
    assert parse.parse(out) == (Sentence(3000, "Ask not what your country can do for you."),)


def test_parse_hours_lines_without_stamps_and_blank_lines():
    out = "[01:02:03.456 --> 01:02:05.000]  第一句\n\n   plain line  \n".encode()
    assert parse.parse(out) == (Sentence(3723456, "第一句"), Sentence(0, "plain line"))


def test_parse_drops_empty_stamped_lines_and_cleans_control_characters():
    out = b"[00:00:01.000 --> 00:00:02.000]   \na\tb\x07c\r\n"
    assert parse.parse(out) == (Sentence(0, "a b c"),)


def test_parse_survives_bad_utf8_and_cuts_long_text():
    assert parse.parse(b"\xff\xfeok\n")[0].text.endswith("ok")
    assert len(parse.parse(b"x" * 20000)[0].text) == 10000
    assert parse.parse(b"") == ()


def test_parse_keeps_every_line_of_a_long_meeting():
    lines = b"".join(b"[%02d:%02d:%02d.000 --> 00:00:00.000]   line %d\n" % (k // 3600, k // 60 % 60, k % 60, k)
                     for k in range(14400))  # fmt: skip
    assert len(lines) > 256 * 1024 and parse.parse(lines)[-1] == Sentence(14399000, "line 14399")


# --- phantom text guards -------------------------------------------------------------------------


def _chunks(blob: bytes, size: int = 4096):
    return [blob[k : k + size] for k in range(0, len(blob), size)]


def test_silence_and_quiet_audio_are_not_loud_enough():
    assert not gate.loud_enough(_chunks(bytes(32000)), 16000)
    assert not gate.loud_enough(_chunks(stt_tree.level(16000, 27)[44:]), 16000)
    assert gate.loud_enough(_chunks(stt_tree.level(16000, 28)[44:]), 16000)
    assert gate.loud_enough(_chunks(stt_tree.tone(16000)[44:], 333), 16000)


def test_loudness_of_nothing_is_silence():
    assert not gate.loud_enough([], 0)
    assert not gate.loud_enough([b"\x01"], 0)


@pytest.mark.parametrize(
    "line",
    [
        "明镜需要您的支持 欢迎订阅明镜",
        "明镜需要您的支持，欢迎订阅明镜！",
        "优优独播剧场——YoYo Television Series Exclusive",
        "字幕由Amara.org社区提供",
        "请不吝点赞 订阅 转发 打赏支持明镜与点点栏目",
        "Thank you for watching!",
        "[BLANK_AUDIO]",
        "(silence)",
        "[音乐]",
        "謝謝觀看",
        "感謝觀看",
        "請訂閱我的頻道",
        "請不吝點贊 訂閱 轉發 打賞支持明鏡與點點欄目",
        "優優獨播劇場——YoYo Television Series Exclusive",
        "中文字幕志願者 李宗盛",
        "字幕by索兰娅",
        "字幕by索蘭婭",
        "明鏡需要您的支持，歡迎訂閱明鏡",
    ],
)
def test_known_phantom_lines_are_dropped(line):
    assert gate.keep((Sentence(0, line),), ()) == ()


def test_real_speech_prompt_echo_and_user_drop_lines():
    said = Sentence(0, "把 settings page 改成英文")
    echo = Sentence(2000, "Cardputer, WhisperWoof.")
    mine = Sentence(4000, "嗯嗯嗯")
    kept = gate.keep((said, echo, mine), ("Cardputer, WhisperWoof", "嗯嗯嗯"))
    assert kept == (said,)


def test_user_lines_and_short_phantoms_only_drop_whole_lines():
    said = (
        Sentence(0, "嗯，我想说的是"),
        Sentence(1, "大家谢谢观看我的演示"),
        Sentence(2, "Thank you for watching my demo"),
    )
    assert gate.keep(said, ("嗯",)) == said
    assert gate.keep((Sentence(0, "好的 字幕由Amara.org社区提供"),), ()) == ()


# --- documents written by cpa-stt ---------------------------------------------------------------


def test_status_names_its_writer_and_is_contract_valid():
    doc = contract_out.status(SID, "transcribing", "2026-10-21T14:45:12.000+08:00")
    assert doc == {"v": 1, "session": SID, "attempt": 1, "state": "transcribing", "by": "cpa-stt",
                   "at": "2026-10-21T14:45:12.000+08:00"}  # fmt: skip
    blocked = contract_out.status(SID, "blocked", "2026-10-21T14:45:12.000+08:00", reason="engine")
    assert blocked["reason"] == "engine"
    assert contract.check_status(doc).ok and contract.check_status(blocked).ok


def test_transcript_doc():
    doc = contract_out.transcript(SID, 200, "whisper-cli", (Sentence(1200, "你好"),))
    assert doc == {"v": 1, "session": SID, "attempt": 1, "lead_ms": 200, "engine": {"stt": "whisper-cli"},
                   "sentences": [{"t_ms": 1200, "text": "你好"}]}  # fmt: skip


def test_stt_label_is_the_program_name_cut_to_a_label():
    assert contract_out.stt_label("/opt/homebrew/bin/whisper-cli") == "whisper-cli"
    assert contract_out.stt_label("/x/" + "a" * 80) == "a" * 64


def test_transcript_md_lines_and_block_ids():
    sentences = (Sentence(754000, "第一句"), Sentence(754500, "第二句"), Sentence(754900, "第三句"),
                 Sentence(3723456, "an hour in"))  # fmt: skip
    assert contract_out.transcript_md(sentences) == (
        "- [12:34] 第一句 ^t001234\n"
        "- [12:34] 第二句 ^t001234-2\n"
        "- [12:34] 第三句 ^t001234-3\n"
        "- [1:02:03] an hour in ^t010203\n"
    )


def test_transcript_md_says_nothing_was_heard():
    assert contract_out.transcript_md(()) == "（没听到）\n"


def test_memo_text_joins_and_cuts_to_2000_code_points():
    assert contract_out.memo_text((Sentence(0, "一"), Sentence(1, "two"))) == "一 two"
    assert contract_out.memo_text(()) is None
    assert len(contract_out.memo_text((Sentence(0, "字" * 3000),))) == 2000


def _outputs():
    return {"transcript_json": hashlib.sha256(b"j").hexdigest(), "transcript_md": hashlib.sha256(b"m").hexdigest()}


def test_done_result_for_a_memo_carries_the_text_as_a_note():
    doc = contract_out.result_done(
        SID, "memo", _outputs(), "whisper-cli", (Sentence(0, "记一下"),), "2026-10-21T14:49:02.000+08:00"
    )
    assert doc["state"] == "done" and doc["level"] == "transcript" and doc["error"] is None
    assert doc["memo"] == {"text": "记一下", "intent": "note"}
    assert doc["outputs"]["transcript_json"] == {"path": "transcript.json", "sha256": _outputs()["transcript_json"]}
    assert doc["pipeline"] == {"app": "cpa-stt", "stt": "whisper-cli", "llm": None, "prompt": None}
    assert contract.check_result(doc).ok


def test_done_result_for_a_meeting_or_nothing_heard_has_no_memo():
    meeting = contract_out.result_done(SID, "meeting", _outputs(), "x", (Sentence(0, "议程"),), "2026-10-21T14:49:02Z")
    quiet = contract_out.result_done(SID, "memo", _outputs(), "x", (), "2026-10-21T14:49:02Z")
    assert meeting["memo"] is None and quiet["memo"] is None
    assert contract.check_result(meeting).ok and contract.check_result(quiet).ok


def test_failed_result():
    doc = contract_out.result_failed(SID, "memo", "E_AUDIO", False, "2026-10-21T14:49:02Z")
    assert doc["state"] == "failed" and doc["error"] == {"code": "E_AUDIO", "retryable": False}
    assert doc["outputs"] == {} and doc["pipeline"]["app"] == "cpa-stt"
    assert contract.check_result(doc).ok


def test_encode_is_utf8_json_with_a_final_newline():
    data = contract_out.encode({"text": "你好"})
    assert data.endswith(b"\n") and json.loads(data) == {"text": "你好"} and "你好".encode() in data
