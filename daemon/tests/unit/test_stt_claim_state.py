"""One transcriber at a time through recordings/.transcriber (docs/22 §4.10), and cpa-stt's own state:
per-session retries with 30 s / 2 min / 8 min back-off (§4.6), recent timings, blocked reason."""

from __future__ import annotations

import json
import os
import stat
from datetime import UTC, datetime

import pytest

from cpa_stt import claim, state

NOW = datetime(2026, 10, 22, 12, 0, tzinfo=UTC).timestamp()
ME = 4242


def _iso(ts: float) -> str:
    return datetime.fromtimestamp(ts, UTC).isoformat(timespec="milliseconds")


def _put(root, **doc):
    body = {"v": 1, "app": "whisperwoof", "pid": 77, "since": _iso(NOW - 900), "beat_at": _iso(NOW - 30), **doc}
    (root / ".transcriber").write_text(json.dumps(body), encoding="utf-8")


def _decide(root, alive=lambda pid: True):
    return claim.decide(claim.read(root), me=ME, now=NOW, alive=alive)


def test_no_claim_means_take(tmp_path):
    assert claim.read(tmp_path) is None
    assert _decide(tmp_path) == "take"


def test_someone_elses_fresh_claim_means_busy(tmp_path):
    _put(tmp_path, beat_at=_iso(NOW - 299))
    assert _decide(tmp_path) == "busy"
    assert claim.read(tmp_path).claim.app == "whisperwoof"


def test_a_claim_older_than_five_minutes_can_be_taken(tmp_path):
    _put(tmp_path, beat_at=_iso(NOW - 301))
    assert _decide(tmp_path) == "take"


def test_our_own_claim_is_ours(tmp_path):
    _put(tmp_path, app="cpa-stt", pid=ME)
    assert _decide(tmp_path) == "mine"


def test_another_live_cpa_stt_is_busy_but_a_dead_one_can_be_replaced(tmp_path):
    _put(tmp_path, app="cpa-stt", pid=99)
    assert _decide(tmp_path, alive=lambda pid: True) == "busy"
    assert _decide(tmp_path, alive=lambda pid: False) == "take"


def test_a_dead_pid_of_another_program_still_waits(tmp_path):
    _put(tmp_path, app="whisperwoof", pid=99)
    assert _decide(tmp_path, alive=lambda pid: False) == "busy"


@pytest.mark.parametrize("body", ["{", "[]", '{"v": 1, "app": "X Y", "pid": 1, "since": "x", "beat_at": "y"}'])
def test_an_unreadable_claim_ages_by_its_mtime(tmp_path, body):
    (tmp_path / ".transcriber").write_text(body, encoding="utf-8")
    os.utime(tmp_path / ".transcriber", (NOW - 10, NOW - 10))
    seen = claim.read(tmp_path)
    assert seen.claim is None and seen.beat == NOW - 10
    assert _decide(tmp_path) == "busy"
    os.utime(tmp_path / ".transcriber", (NOW - 400, NOW - 400))
    assert _decide(tmp_path) == "take"


def test_a_symlinked_claim_is_not_followed(tmp_path):
    target = tmp_path / "t.json"
    target.write_text(json.dumps({"v": 1, "app": "cpa-stt", "pid": ME, "since": _iso(NOW), "beat_at": _iso(NOW)}))
    (tmp_path / ".transcriber").symlink_to(target)
    assert claim.read(tmp_path).claim is None


def test_write_then_release(tmp_path):
    claim.write(tmp_path, ME, since=_iso(NOW - 60), now=_iso(NOW))
    path = tmp_path / ".transcriber"
    assert stat.S_IMODE(path.stat().st_mode) == 0o600
    assert json.loads(path.read_text()) == {"v": 1, "app": "cpa-stt", "pid": ME, "since": _iso(NOW - 60),
                                            "beat_at": _iso(NOW)}  # fmt: skip
    assert claim.read(tmp_path).beat == pytest.approx(NOW, abs=1)
    assert not claim.release(tmp_path, ME + 1) and path.exists()
    assert claim.release(tmp_path, ME) and not path.exists()
    assert not claim.release(tmp_path, ME)


def test_release_leaves_another_programs_claim(tmp_path):
    _put(tmp_path, pid=ME)
    assert not claim.release(tmp_path, ME)
    assert (tmp_path / ".transcriber").exists()


def test_a_beat_in_the_future_counts_as_the_files_mtime(tmp_path):
    _put(tmp_path, beat_at="2099-01-01T00:00:00+00:00")
    os.utime(tmp_path / ".transcriber", (NOW - 600, NOW - 600))
    assert claim.read(tmp_path).beat == NOW - 600
    assert _decide(tmp_path) == "take"
    os.utime(tmp_path / ".transcriber", (NOW - 10, NOW - 10))
    assert _decide(tmp_path) == "busy"


def test_a_beat_without_a_time_zone_is_unreadable_and_ages_by_mtime(tmp_path):
    _put(tmp_path, beat_at="2026-10-22T12:00:00")
    os.utime(tmp_path / ".transcriber", (NOW - 400, NOW - 400))
    seen = claim.read(tmp_path)
    assert seen.claim is None and seen.beat == NOW - 400
    assert _decide(tmp_path) == "take"


def test_take_creates_a_missing_claim_exclusively(tmp_path):
    assert claim.take(tmp_path, ME, since=_iso(NOW), now=_iso(NOW), stale=False)
    assert not claim.take(tmp_path, ME + 1, since=_iso(NOW), now=_iso(NOW), stale=False)
    assert claim.read(tmp_path).claim.pid == ME
    assert [p.name for p in tmp_path.iterdir()] == [".transcriber"]


def test_take_replaces_a_stale_claim_and_reads_it_back(tmp_path, monkeypatch):
    _put(tmp_path, beat_at=_iso(NOW - 900))
    assert claim.take(tmp_path, ME, since=_iso(NOW), now=_iso(NOW), stale=True)
    assert (claim.read(tmp_path).claim.app, claim.read(tmp_path).claim.pid) == ("cpa-stt", ME)
    racer = claim.Seen(claim.Claim("cpa-stt", ME + 1, _iso(NOW), _iso(NOW)), NOW)
    monkeypatch.setattr(claim, "read", lambda root: racer)  # another taker replaced it right after us
    assert not claim.take(tmp_path, ME, since=_iso(NOW), now=_iso(NOW), stale=True)


def test_pid_alive():
    assert claim.pid_alive(os.getpid())
    assert not claim.pid_alive(2**22 + 12345)


# --- state ----------------------------------------------------------------------------------------


def test_backoff_30s_2min_8min_then_exhausted():
    s, sid, t = state.SttState(), "3QZK1M7-00042-0A1F", 1_000_000
    assert state.due(s, sid, t)
    s, exhausted = state.fail(s, sid, t)
    assert not exhausted and not state.due(s, sid, t + 29_999) and state.due(s, sid, t + 30_000)
    s, exhausted = state.fail(s, sid, t + 30_000)
    assert not exhausted and state.failures(s, sid).next_ms == t + 30_000 + 120_000
    s, exhausted = state.fail(s, sid, t + 150_000)
    assert not exhausted and state.failures(s, sid).next_ms == t + 150_000 + 480_000
    s, exhausted = state.fail(s, sid, t + 630_000)
    assert exhausted
    s = state.clear(s, sid)
    assert state.failures(s, sid) is None and state.due(s, sid, t)


def test_recent_keeps_the_last_ten():
    s = state.SttState()
    for k in range(12):
        s = state.add_recent(s, state.Recent(f"S{k}", 1000, 100 + k, 0))
    assert [r.session for r in s.recent] == [f"S{k}" for k in range(2, 12)]


def test_save_and_load_round_trip(tmp_path):
    path = tmp_path / "stt" / "state.json"
    s, _ = state.fail(state.SttState(blocked="engine", selftest_ok=False, selftest_at="t", config_sig="abc"), "S", 5)
    s = state.add_recent(s, state.Recent("S", 1000, 250, 1))
    s = state.replace(s, foreign=("2026-10-21/1402-x",), busy_by="whisperwoof")
    state.save(path, s)
    assert state.load(path) == s
    assert stat.S_IMODE(path.stat().st_mode) == 0o600


@pytest.mark.parametrize("text", [None, "{", "[]", '{"failures": 3}'])
def test_missing_or_corrupt_state_starts_fresh(tmp_path, text):
    path = tmp_path / "state.json"
    if text is not None:
        path.write_text(text, encoding="utf-8")
    assert state.load(path) == state.SttState()
