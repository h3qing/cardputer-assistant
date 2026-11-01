"""Recording contract v1 (protocol/recording/v1) and the file.* host invariants, against the reference tools.

- every result / status vector gets the same verdict from cpad's reader as from check_recording.py;
- manifests cpad builds (complete, with gaps, recovered, memo, with the env stamp) are valid for the reference checker;
- docs/23 §4.2: the schemas use only the keywords WhisperWoof's validator supports, and env.host is the hello's
  host_label rule (the device accepts every name `cpa host name` does);
- RecPuller traffic over FakeFiles passes protocol/tools/file_flows.check_file_flow (gate, E_BUSY,
  at most 4 in flight, reply kinds, file.del only after every byte).
"""

from __future__ import annotations

import hashlib
import json
import sys

import pytest

from cpad.config.settings import Config
from cpad.protocol.limits import LIMITS
from cpad.recordings import contract, index
from tests.support import fake_files as ff
from tests.support.protocol_files import PROTOCOL
from tests.support.rec_shell import Shell, until

V1 = PROTOCOL / "recording" / "v1"
# WhisperWoof PR #151, src/whisperwoof/core/cardputer/contract-schema.js:16-37: any other keyword (bar x-*) makes its
# validator throw at startup, so a schema change must stay inside this set (docs/23 §3.2, §4.2).
WW_KEYWORDS = frozenset({"$schema", "$id", "title", "description", "$defs", "x-knownValues", "$ref", "type", "const",
                         "enum", "pattern", "minimum", "maximum", "minLength", "maxLength", "required", "properties",
                         "items", "minItems", "maxItems", "anyOf", "allOf", "if", "then"})  # fmt: skip
CASES = [
    (kind, path.name, json.loads(path.read_text(encoding="utf-8")))
    for kind in ("valid", "invalid")
    for path in sorted((V1 / "vectors" / kind).glob("*.json"))
]
READERS = {"result": contract.check_result, "status": contract.check_status}


@pytest.fixture(scope="module")
def reference():
    tools = [str(PROTOCOL / "recording" / "tools"), str(PROTOCOL / "tools")]
    sys.path[:0] = tools
    try:
        import check_recording  # noqa: PLC0415
        import file_flows  # noqa: PLC0415
        import schemas  # noqa: PLC0415

        yield check_recording, check_recording.load(V1), file_flows, schemas.load_contract(PROTOCOL)
    finally:
        for path in tools:
            sys.path.remove(path)


def test_vector_digests_match():
    lines = (V1 / "VECTORS.sha256").read_text(encoding="utf-8").splitlines()
    for line in lines:
        digest, name = line.split(maxsplit=1)
        assert hashlib.sha256((V1 / name).read_bytes()).hexdigest() == digest, name
    assert len(lines) == len(CASES)


@pytest.mark.parametrize("case", [c for c in CASES if c[2]["doc"] in READERS], ids=lambda c: f"{c[0]}:{c[1]}")
def test_cpad_reads_results_and_statuses_like_the_contract(case):
    kind, _name, vector = case
    verdict = READERS[vector["doc"]](vector["data"])
    if kind == "valid":
        assert verdict.ok, verdict
    else:
        assert verdict == contract.Verdict(vector["reason"]["code"], (vector["reason"]["field"],))


async def _archive(tmp_path, segments: list[ff.Segment]) -> dict:
    files = ff.FakeFiles()
    files.add(*segments)
    shell = Shell(tmp_path, files)
    async with shell.connected():
        await until(lambda: not files.segments and shell.archived())  # a cut session on the same connection too
    rec = segments[0].rec
    return json.loads((shell.session(rec) / "manifest.json").read_text())


async def test_manifests_cpad_writes_are_valid_v1(tmp_path, reference):
    check_recording, contract_v1, _, _ = reference
    meeting = ff.meeting(["K3M9Q2T-00042-0A1C", "K3M9Q2T-00042-0A1D", "K3M9Q2T-00042-0A1E"])
    recovered = [meeting[0], ff.Segment(**{**meeting[2].__dict__, "last": False, "rcv": True,
                                           "header": ff.wav_header(0)})]  # fmt: skip
    docs = [
        await _archive(tmp_path / "a", meeting),
        await _archive(tmp_path / "b", [ff.memo("K3M9Q2T-00042-0A1F")]),
        await _archive(tmp_path / "c", recovered),
    ]
    for doc in docs:
        assert check_recording.validate(contract_v1, "manifest", doc) == check_recording.Verdict("ok"), doc
    assert docs[2]["gaps"] == [1, 3] and docs[2]["segments"][1]["repaired"] is True


async def test_a_manifest_with_the_env_stamp_is_valid_v1(tmp_path, reference):
    check_recording, contract_v1, _, _ = reference
    files = ff.FakeFiles()
    shell = Shell(tmp_path, files, config=Config(notes_dir=str(tmp_path / "notes"), host_label="八个汉字的主机名"))
    async with shell.connected():
        files.add(ff.memo("K3M9Q2T-00042-0A1F", up_ms=905000))
        shell.sim.pong(up_ms=910000)
        await until(lambda: shell.archived())
    doc = json.loads((shell.session("K3M9Q2T-00042-0A1F") / "manifest.json").read_text())
    assert doc["env"] == {"host": "八个汉字的主机名", "link": "here"}
    assert check_recording.validate(contract_v1, "manifest", doc) == check_recording.Verdict("ok")


def _keywords(schema) -> set[str]:
    """Every keyword of a schema, walked the way contract-schema.js assertSupported walks it."""
    if not isinstance(schema, dict):
        return set()
    found = set(schema)
    for key in ("properties", "$defs"):
        found |= {k for sub in schema.get(key, {}).values() for k in _keywords(sub)}
    for key in ("anyOf", "allOf"):
        found |= {k for sub in schema.get(key, ()) for k in _keywords(sub)}
    for key in ("items", "if", "then"):
        found |= _keywords(schema.get(key))
    return found


def test_the_schemas_use_only_keywords_whisperwoof_supports():
    used = set().union(*(_keywords(json.loads(p.read_text())) for p in (V1 / "schema").glob("*.schema.json")))
    assert {k for k in used if not k.startswith("x-")} <= WW_KEYWORDS
    assert {"oneOf", "else", "not", "format", "additionalProperties", "patternProperties"}.isdisjoint(used)


def test_env_host_is_the_host_hello_label_rule():
    label = json.loads((V1 / "schema" / "common.schema.json").read_text())["$defs"]["hostLabel"]
    hello = json.loads((PROTOCOL / "schema" / "v0" / "common.schema.json").read_text())["$defs"]["hostLabel"]
    assert (label["pattern"], label["minLength"], label["x-maxBytes"]) == (hello["pattern"], hello["minLength"],
                                                                           hello["x-maxBytes"])  # fmt: skip
    assert label["x-maxBytes"] == label["maxLength"] == LIMITS["host_label_max"]


async def test_puller_traffic_keeps_the_host_invariants(tmp_path, reference):
    _, _, file_flows, v0 = reference
    files = ff.FakeFiles(busy_after_gets=6)
    files.add(*ff.meeting(["K3M9Q2T-00042-0A1C", "K3M9Q2T-00042-0A1D"], n_bytes=9000), ff.memo("K3M9Q2T-00042-0A1F"))
    shell = Shell(tmp_path, files)
    async with shell.connected():
        await until(lambda: files.live)
        await shell.settle()
        files.live = False
        files.add(ff.memo("K3M9Q2T-00042-0A1G"))
        shell.sim.pong()
        await until(lambda: not files.segments)
        await shell.settle()
    flow = _with_hello(shell.sim.as_flow("rec_puller"), {"n": 3, "rev": 4, "live": False})
    assert file_flows.check_file_flow(v0, flow) == ()
    assert len(index.fold(shell.events("rec.archived")).archived) == 3


async def test_the_listing_that_finishes_a_cut_session_keeps_the_host_invariants(tmp_path, reference):
    _, _, file_flows, v0 = reference
    files = ff.FakeFiles()
    files.add(ff.meeting(["K3M9Q2T-00042-0A1C", "K3M9Q2T-00042-0A1D"], n_bytes=9000)[0])
    hello = files.status()
    shell = Shell(tmp_path, files)
    async with shell.connected():
        await until(lambda: shell.archived())
        await shell.settle()
    flow = _with_hello(shell.sim.as_flow("rec_cut_session"), hello)
    assert file_flows.check_file_flow(v0, flow) == ()
    sent = [msg["t"] for _, side, msg in shell.sim.trace if side == "host" and msg["t"] != "file.get"]
    assert sent == ["file.list", "file.del", "file.list"] and shell.archived()[0]["gaps"] == [1]


def _with_hello(flow: dict, files: dict) -> dict:
    """The flow, preceded by the hello the puller started from (Shell.connected does not trace it)."""
    hello = {"v": 0, "t": "hello", "id": "d0", "re": "h0", "role": "device", "fw": "cpa-fw/0.3.0",
             "dev": "020000000001", "boot": "K3M9Q2T-00042", "up_ms": 1, "rst": "poweron", "time": False,
             "pending": 0, "bat_mv": 4100, "revs": {"projects": 0, "sessions": 0, "queue": 0},
             "files": files}  # fmt: skip
    return {**flow, "steps": [{"t": -1, "from": "device", "msg": hello}, *flow["steps"]]}
