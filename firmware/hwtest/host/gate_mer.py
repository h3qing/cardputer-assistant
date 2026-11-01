"""Mixed error rate for G-A10 (zh/en): CJK counts per character, Latin per word.

MER = (substitutions + deletions + insertions) / reference tokens x 100, the measure the
WhisperWoof bench quotes for SenseVoice (docs/gates/whisperwoof.md). Punctuation and
case are ignored. Pure; the CLI is `hwtest_host.py mer --ref REF.txt --hyp HYP.txt`.
"""

from __future__ import annotations

import re

_TOKEN = re.compile(r"[㐀-䶿一-鿿豈-﫿]|[a-z0-9]+")


def tokens(text):
    return _TOKEN.findall(text.lower())


def _distance(ref, hyp):
    prev = list(range(len(hyp) + 1))
    for i, r in enumerate(ref, 1):
        cur = [i] + [0] * len(hyp)
        for j, h in enumerate(hyp, 1):
            cur[j] = min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (r != h))
        prev = cur
    return prev[-1]


def mer(reference, hypothesis):
    ref, hyp = tokens(reference), tokens(hypothesis)
    if not ref:
        raise ValueError("empty reference")
    return 100.0 * _distance(ref, hyp) / len(ref)
