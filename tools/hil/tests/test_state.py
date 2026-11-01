"""--state FILE: what migrate_seed planted, read back by migrate_check and migrate_drained (09 §9.3)."""

import json

import pytest

from harness.state import STATE_VERSION, Seed, StateError, from_json, load_state, save_state, to_json

SEED = Seed(dev="020000000001", boot="069D183-00015", texts=("mq00", "mq01", "mq02"))


def test_round_trip_through_a_file(tmp_path):
    path = tmp_path / "mig.json"
    save_state(path, SEED)
    assert load_state(path) == SEED
    assert json.loads(path.read_text())["version"] == STATE_VERSION


def test_checked_result_is_kept(tmp_path):
    path = tmp_path / "mig.json"
    checked = Seed(SEED.dev, SEED.boot, SEED.texts, ids=("069D183-00015-0001",), checked=True)
    save_state(path, checked)
    assert load_state(path) == checked


def test_save_replaces_the_file_whole_and_leaves_no_temporary(tmp_path):
    path = tmp_path / "mig.json"
    path.write_text("old")
    save_state(path, SEED)
    assert load_state(path) == SEED
    assert sorted(p.name for p in tmp_path.iterdir()) == ["mig.json"]


def test_missing_file_names_migrate_seed(tmp_path):
    with pytest.raises(StateError, match="migrate_seed"):
        load_state(tmp_path / "none.json")


def test_not_json_is_refused(tmp_path):
    path = tmp_path / "mig.json"
    path.write_text("{")
    with pytest.raises(StateError, match="JSON"):
        load_state(path)


@pytest.mark.parametrize(
    "change",
    [
        {"version": 2},
        {"dev": ""},
        {"boot": 5},
        {"texts": []},
        {"texts": ["a", "a"]},
        {"texts": ["a", ""]},
        {"ids": "x"},
        {"checked": "yes"},
    ],
)
def test_malformed_state_is_refused(change):
    with pytest.raises(StateError):
        from_json({**to_json(SEED), **change})


def test_a_list_is_not_a_state():
    with pytest.raises(StateError):
        from_json([])
