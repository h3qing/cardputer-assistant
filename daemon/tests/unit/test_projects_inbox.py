"""projects.json validation (05 §7.6, US-05), cwd resolution, and the inbox `need` rules (§7.6, US-07)."""

from __future__ import annotations

import json
import os

import pytest

from cpad.agents.base import SessionInfo
from cpad.inbox import need
from cpad.projects import registry, resolve

# --- registry -------------------------------------------------------------------------------


def _doc(*projects):
    return {"version": 1, "projects": list(projects)}


def _entry(tmp_path, key="cpa", slot=1, **kw):
    root = tmp_path / key
    root.mkdir(exist_ok=True)
    return {"key": key, "name": kw.pop("name", key), "slot": slot, "root": str(root), **kw}


def test_mode_defaults_to_plan(tmp_path):
    (project,) = registry.parse_registry(_doc(_entry(tmp_path)))
    assert project.mode == "plan" and project.ok


def test_bypass_permissions_is_refused(tmp_path):
    with pytest.raises(registry.ProjectsError, match="bypassPermissions"):
        registry.parse_registry(_doc(_entry(tmp_path, spawn_permission_mode="bypassPermissions")))


@pytest.mark.parametrize(
    "change",
    [
        {"key": "Bad"},
        {"key": "waytoolongkey1"},
        {"slot": 0},
        {"slot": 10},
        {"slot": True},
        {"name": "名" * 9},
        {"name": "a\nb"},
        {"root": "relative/path"},
        {"spawn_permission_mode": "yolo"},
    ],
)
def test_invalid_entries_fail_the_whole_file(tmp_path, change):
    with pytest.raises(registry.ProjectsError):
        registry.parse_registry(_doc({**_entry(tmp_path), **change}))


def test_duplicate_key_or_slot_fails(tmp_path):
    a, b = _entry(tmp_path, "a", 1), _entry(tmp_path, "b", 1)
    with pytest.raises(registry.ProjectsError, match="slot"):
        registry.parse_registry(_doc(a, b))
    with pytest.raises(registry.ProjectsError, match="key"):
        registry.parse_registry(_doc(a, {**_entry(tmp_path, "c", 2), "key": "a"}))


def test_missing_root_loads_but_is_not_ok(tmp_path):
    entry = _entry(tmp_path)
    os.rmdir(entry["root"])
    (project,) = registry.parse_registry(_doc(entry))
    assert not project.ok


def test_wrong_version_or_shape_fails():
    for doc in ({"version": 2, "projects": []}, {"version": 1}, [], {"version": 1, "projects": [1]}):
        with pytest.raises(registry.ProjectsError):
            registry.parse_registry(doc)


def test_add_project_stores_realpath_and_validates(tmp_path):
    real = tmp_path / "real"
    real.mkdir()
    link = tmp_path / "link"
    link.symlink_to(real)
    projects = registry.add_project((), key="cpa", path=str(link), slot=1, name="Cardputer 助手")
    assert projects[0].root == os.path.realpath(real) and projects[0].name == "Cardputer 助手"
    with pytest.raises(registry.ProjectsError, match="slot"):
        registry.add_project(projects, key="blog", path=str(real), slot=1)
    with pytest.raises(registry.ProjectsError, match="directory"):
        registry.add_project(projects, key="blog", path=str(tmp_path / "nope"), slot=2)
    with pytest.raises(registry.ProjectsError, match="bypassPermissions"):
        registry.add_project(projects, key="blog", path=str(real), slot=2, mode="bypassPermissions")


def test_add_does_not_require_git(tmp_path):
    (project,) = registry.add_project((), key="plain", path=str(tmp_path), slot=3)
    assert project.slot == 3 and project.name == "plain"


def test_remove_and_round_trip(tmp_path):
    projects = registry.add_project((), key="a", path=str(tmp_path), slot=1, mode="acceptEdits")
    projects = registry.add_project(projects, key="b", path=str(tmp_path), slot=2)
    path = tmp_path / "projects.json"
    registry.save_registry(path, projects)
    assert oct(path.stat().st_mode & 0o777) == "0o600"
    assert registry.load_registry(path) == projects
    assert registry.remove_project(projects, "a") == projects[1:]
    with pytest.raises(registry.ProjectsError):
        registry.remove_project(projects, "zzz")
    assert json.loads(path.read_text())["projects"][0]["spawn_permission_mode"] == "acceptEdits"


def test_load_missing_file_is_empty(tmp_path):
    assert registry.load_registry(tmp_path / "none.json") == ()


# --- cwd -> project -------------------------------------------------------------------------


def _projects(tmp_path):
    for sub in ("repo", "repo/sub", "repo-other"):
        (tmp_path / sub).mkdir(parents=True, exist_ok=True)
    projects = registry.add_project((), key="repo", path=str(tmp_path / "repo"), slot=1)
    projects = registry.add_project(projects, key="sub", path=str(tmp_path / "repo/sub"), slot=2)
    return registry.add_project(projects, key="other", path=str(tmp_path / "repo-other"), slot=3)


def test_longest_prefix_wins_and_respects_path_components(tmp_path):
    projects = _projects(tmp_path)
    real = os.path.realpath(tmp_path)
    assert resolve.match_root(projects, f"{real}/repo/sub/deep").key == "sub"
    assert resolve.match_root(projects, f"{real}/repo").key == "repo"
    assert resolve.match_root(projects, f"{real}/repo-other/x").key == "other"
    assert resolve.match_root(projects, f"{real}/repox") is None


def test_git_common_dir_fallback_is_cached(tmp_path):
    projects = _projects(tmp_path)
    worktree = tmp_path / "wt"
    worktree.mkdir()
    calls = []

    def git(cwd):
        calls.append(cwd)
        return os.path.join(os.path.realpath(tmp_path), "repo", ".git")

    clock = iter([0.0, 10.0, 70.0])
    resolver = resolve.Resolver(git=git, clock=lambda: next(clock))
    assert resolver.project_for(projects, str(worktree)).key == "repo"
    assert resolver.project_for(projects, str(worktree)).key == "repo"
    assert len(calls) == 1
    resolver.project_for(projects, str(worktree))
    assert len(calls) == 2


def test_unmatched_cwd_is_other(tmp_path):
    resolver = resolve.Resolver(git=lambda cwd: None)
    assert resolver.project_for(_projects(tmp_path), "/nowhere") is None
    assert resolver.project_for(_projects(tmp_path), None) is None


def test_run_git_handles_failures(tmp_path):
    assert resolve.run_git(str(tmp_path / "missing")) is None
    assert resolve.run_git(str(tmp_path)) is None


# --- inbox need -----------------------------------------------------------------------------


def _info(sid="8d3e7a19", status="idle", kind="bg"):
    return SessionInfo(session_id=f"{sid}-0000-0000-0000-000000000000", sid=sid, short=sid, kind=kind,
                       status=status, state=None, name="设置页面和字体修复", cwd="/x", pid=1)  # fmt: skip


def test_since_survives_while_status_is_unchanged():
    first = need.track((), ((_info(), "cpa"),), now=100.0)
    later = need.track(first, ((_info(), "cpa"),), now=130.0)
    assert later[0].since == 100.0
    flipped = need.track(later, ((_info(status="busy"), "cpa"),), now=140.0)
    assert flipped[0].since == 140.0


def test_waiting_needs_you_immediately_idle_after_threshold():
    (waiting,) = need.track((), ((_info(status="waiting"), "cpa"),), now=0.0)
    (idle,) = need.track((), ((_info(status="idle"), "cpa"),), now=0.0)
    assert need.needs(waiting, now=0.0, idle_s=60, dismissed=frozenset())
    assert not need.needs(idle, now=59.9, idle_s=60, dismissed=frozenset())
    assert need.needs(idle, now=60.0, idle_s=60, dismissed=frozenset())


def test_busy_shell_unknown_and_unregistered_never_need():
    for status in ("busy", "shell", "unknown"):
        (t,) = need.track((), ((_info(status=status), "cpa"),), now=0.0)
        assert not need.needs(t, now=999.0, idle_s=60, dismissed=frozenset())
    (other,) = need.track((), ((_info(status="waiting"), None),), now=0.0)
    assert not need.needs(other, now=999.0, idle_s=60, dismissed=frozenset())


def test_dismiss_lasts_until_the_state_changes():
    (t,) = need.track((), ((_info(status="waiting"), "cpa"),), now=0.0)
    dismissed = need.dismiss(frozenset(), t)
    assert not need.needs(t, now=1.0, idle_s=60, dismissed=dismissed)
    busy = need.track((t,), ((_info(status="busy"), "cpa"),), now=5.0)
    back = need.track(busy, ((_info(status="waiting"), "cpa"),), now=9.0)
    assert need.needs(back[0], now=9.0, idle_s=60, dismissed=dismissed)
    assert need.prune(dismissed, back) == frozenset()
