"""Phantom-text guards (docs/22 §4.9). Pure.

Whisper invents text on silence and noise. Three guards, any one is enough for "nothing heard":
1. the configured command may use VAD (the whisper.cpp example adds --vad when a Silero model is there);
2. loud_enough(): cpa-stt measures the PCM's RMS before running anything; below SILENCE_RMS of full scale
   (WhisperWoof's device-calibrated 0.00084, about 27.5 in int16) the command is not run at all;
3. keep(): known phantom sentences, the prompt given to the engine and the user's drop_lines are removed,
   and so are lines that are only a bracketed tag such as [BLANK_AUDIO].
"""

from __future__ import annotations

import re
import sys
import unicodedata
from array import array
from collections.abc import Iterable

from cpa_stt.parse import Sentence

SILENCE_RMS = 0.00084
FULL_SCALE = 32768
PHANTOMS = (  # whole lines Whisper is known to invent on silence and noise (equal after normalize())
    "明镜需要您的支持 欢迎订阅明镜",
    "请不吝点赞 订阅 转发 打赏支持明镜与点点栏目",
    "优优独播剧场——YoYo Television Series Exclusive",
    "中文字幕志愿者 李宗盛",
    "谢谢观看",
    "感谢观看",
    "请订阅我的频道",
    "中文字幕志願者 李宗盛",  # -l auto sometimes answers in traditional characters
    "謝謝觀看",
    "感謝觀看",
    "請訂閱我的頻道",
    "Thank you for watching",
    "Thanks for watching",
)
MARKS = (  # never in real speech: a line holding one is dropped whole
    "amaraorg",
    "明镜需要您的支持",
    "欢迎订阅明镜",
    "优优独播剧场",
    "点点栏目",
    "明鏡需要您的支持",
    "歡迎訂閱明鏡",
    "優優獨播劇場",
    "點點欄目",
    "索兰娅",
    "索蘭婭",
)
TAG_ONLY = re.compile(r"^[\[(（【][^\])）】]*[\])）】]$")
NOT_WORD = re.compile(r"[\W_]+")


def loud_enough(chunks: Iterable[bytes], n_samples: int) -> bool:
    """RMS of 16-bit little-endian PCM >= the silence threshold; stops reading as soon as it is."""
    if n_samples <= 0:
        return False
    need = (SILENCE_RMS * FULL_SCALE) ** 2 * n_samples
    total = 0
    for chunk in chunks:
        samples = array("h", chunk[: len(chunk) - len(chunk) % 2])
        if sys.byteorder == "big":
            samples.byteswap()
        total += sum(x * x for x in samples)
        if total >= need:
            return True
    return False


def normalize(text: str) -> str:
    return NOT_WORD.sub("", unicodedata.normalize("NFKC", text).casefold())


def _phantom(text: str, lines: frozenset[str]) -> bool:
    key = normalize(text)
    return not key or key in lines or TAG_ONLY.match(text.strip()) is not None or any(m in key for m in MARKS)


def keep(sentences: Iterable[Sentence], extra: Iterable[str]) -> tuple[Sentence, ...]:
    """Drop phantom lines; `extra` (prompts, drop_lines) only ever matches a whole line."""
    lines = frozenset(normalize(p) for p in (*PHANTOMS, *extra))
    return tuple(s for s in sentences if not _phantom(s.text, lines))
