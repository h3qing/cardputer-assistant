"""cpa-stt against the recording contract v1 reference checker (protocol/recording/tools/check_recording.py):
it reads every manifest vector exactly like the contract, and every file it writes is contract-valid."""

from __future__ import annotations

import json
import sys

import pytest

from cpa_stt import contract_out, manifest
from cpa_stt.parse import Sentence
from tests.support import stt_tree
from tests.support.protocol_files import PROTOCOL

V1 = PROTOCOL / "recording" / "v1"
MANIFESTS = [
    (kind, path.name, json.loads(path.read_text(encoding="utf-8")))
    for kind in ("valid", "invalid")
    for path in sorted((V1 / "vectors" / kind).glob("manifest_*.json"))
]
AT = "2026-10-21T14:49:02.000+08:00"


@pytest.fixture(scope="module")
def reference():
    tools = [str(PROTOCOL / "recording" / "tools"), str(PROTOCOL / "tools")]
    sys.path[:0] = tools
    try:
        import check_recording  # noqa: PLC0415

        yield check_recording, check_recording.load(V1)
    finally:
        for path in tools:
            sys.path.remove(path)


@pytest.mark.parametrize("case", MANIFESTS, ids=lambda c: f"{c[0]}:{c[1]}")
def test_cpa_stt_reads_manifests_like_the_contract(case):
    kind, _name, vector = case
    verdict = manifest.check(vector["data"])
    if kind == "valid":
        assert verdict.ok, verdict
    else:
        assert verdict == manifest.Verdict(vector["reason"]["code"], (vector["reason"]["field"],))


def test_the_test_tree_writes_valid_manifests(reference):
    check, contract_v1 = reference
    doc = stt_tree.manifest(stt_tree.SID, [(0, stt_tree.tone(160), 0), (2, stt_tree.tone(160), 9000)], gaps=[1])
    assert check.validate(contract_v1, "manifest", doc) == check.Verdict("ok")


def test_every_document_cpa_stt_writes_is_valid(reference):
    check, contract_v1 = reference
    sid = stt_tree.SID
    said = (Sentence(0, "你好"), Sentence(1500, "world"))
    sha = {"transcript_json": "0" * 64, "transcript_md": "1" * 64}
    docs = [
        ("status", contract_out.status(sid, "transcribing", AT)),
        ("status", contract_out.status(sid, "blocked", AT, reason="model_missing")),
        ("status", contract_out.status(sid, "blocked", AT, reason="engine")),
        ("transcript", contract_out.transcript(sid, 200, "whisper-cli", said)),
        ("transcript", contract_out.transcript(sid, 0, "whisper-cli", ())),
        ("result", contract_out.result_done(sid, "memo", sha, "whisper-cli", said, AT)),
        ("result", contract_out.result_done(sid, "memo", sha, "whisper-cli", (), AT)),
        ("result", contract_out.result_done(sid, "meeting", sha, "whisper-cli", said, AT)),
        ("result", contract_out.result_failed(sid, "memo", "E_STT", True, AT, "whisper-cli")),
        ("result", contract_out.result_failed(sid, "meeting", "E_INVALID", False, AT)),
    ]
    for doc_type, doc in docs:
        assert check.validate(contract_v1, doc_type, doc) == check.Verdict("ok"), (doc_type, doc)


@pytest.mark.parametrize(
    ("env", "verdict"),
    [
        ({"host": "家", "link": "here"}, ("ok", ())),
        ({"host": None, "link": "other"}, ("ok", ())),
        ({"host": "家" * 9, "link": "unknown"}, ("E_INVALID", ("env.host",))),  # 27 B > 24
        ({"host": "Studio", "link": "Here"}, ("E_INVALID", ("env.link",))),
    ],
)
def test_a_clip_with_cpads_env_stamp_reads_like_the_contract(reference, env, verdict):
    """docs/22 cap and docs/23 env are both optional manifest fields; one manifest may carry both."""
    check, contract_v1 = reference
    clip = json.loads((V1 / "vectors" / "valid" / "manifest_clip.json").read_text(encoding="utf-8"))["data"]
    doc = {**clip, "env": env}
    assert check.validate(contract_v1, "manifest", doc) == check.Verdict(*verdict)
    assert manifest.check(doc) == manifest.Verdict(*verdict)
