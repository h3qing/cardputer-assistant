"""cpa status names the transcriber that claims recordings/ (recording contract v1 .transcriber, docs/22 §4.10)."""

from __future__ import annotations

import json
import os

from cpad.recordings import transcriber

NOW = 1_792_700_000.0


def _claim(root, app="cpa-stt", pid=4242, beat="2026-10-22T10:00:00+00:00"):
    root.mkdir(parents=True, exist_ok=True)
    doc = {"v": 1, "app": app, "pid": pid, "since": beat, "beat_at": beat}
    (root / ".transcriber").write_text(json.dumps(doc), encoding="utf-8")


def test_no_claim(tmp_path):
    info = transcriber.describe(tmp_path / "recordings", NOW)
    assert info == {"app": None, "pid": None, "beat_age_s": None, "fresh": False}
    assert transcriber.line(info) == "none running"


def test_a_fresh_claim(tmp_path):
    from datetime import UTC, datetime

    beat = datetime.fromtimestamp(NOW - 12, UTC).isoformat()
    _claim(tmp_path, beat=beat)
    info = transcriber.describe(tmp_path, NOW)
    assert info == {"app": "cpa-stt", "pid": 4242, "beat_age_s": 12, "fresh": True}
    assert transcriber.line(info) == "cpa-stt pid 4242, beat 12 s ago"


def test_a_stale_claim(tmp_path):
    from datetime import UTC, datetime

    _claim(tmp_path, app="whisperwoof", pid=77, beat=datetime.fromtimestamp(NOW - 540, UTC).isoformat())
    info = transcriber.describe(tmp_path, NOW)
    assert not info["fresh"] and transcriber.line(info) == "whisperwoof pid 77, not running (last beat 9 min ago)"


def test_an_unreadable_or_symlinked_claim_ages_by_mtime(tmp_path):
    (tmp_path / ".transcriber").write_text("{", encoding="utf-8")
    os.utime(tmp_path / ".transcriber", (NOW - 5, NOW - 5))
    info = transcriber.describe(tmp_path, NOW)
    assert info == {"app": None, "pid": None, "beat_age_s": 5, "fresh": True}
    assert transcriber.line(info) == "unreadable claim, beat 5 s ago"
    (tmp_path / ".transcriber").unlink()
    (tmp_path / "elsewhere.json").write_text(json.dumps({"v": 1, "app": "x", "pid": 1, "since": "a", "beat_at": "b"}))
    (tmp_path / ".transcriber").symlink_to(tmp_path / "elsewhere.json")
    assert transcriber.describe(tmp_path, NOW)["app"] is None


def test_a_beat_in_the_future_or_without_a_time_zone_ages_by_mtime(tmp_path):
    _claim(tmp_path, beat="2099-01-01T00:00:00+00:00")
    os.utime(tmp_path / ".transcriber", (NOW - 600, NOW - 600))
    info = transcriber.describe(tmp_path, NOW)
    assert (info["app"], info["beat_age_s"], info["fresh"]) == ("cpa-stt", 600, False)
    _claim(tmp_path, beat="2026-10-22T10:00:00")
    os.utime(tmp_path / ".transcriber", (NOW - 30, NOW - 30))
    assert transcriber.describe(tmp_path, NOW) == {"app": None, "pid": None, "beat_age_s": 30, "fresh": True}
