"""Projects as named groups (docs/25 §8, 2026-10-26): created on the Mac, optionally linked to a code folder;
add with a derived key and the first free slot, rename, move (swap), link / unlink, remove."""

from __future__ import annotations

import json
import os

import pytest

from cpad.projects import edit, registry, resolve


def _two(tmp_path):
    projects = edit.new_project((), name="Cardputer 助手", key="cpa", folder=str(tmp_path))
    return edit.new_project(projects, name="Blog")


def test_a_project_needs_only_a_name():
    (p,) = edit.new_project((), name="Blog")
    assert (p.key, p.name, p.slot, p.root, p.ok, p.linked) == ("blog", "Blog", 1, None, True, False)


def test_key_is_derived_from_the_name_or_the_slot():
    projects = edit.new_project((), name="My Big Side Project!")
    projects = edit.new_project(projects, name="家务")
    projects = edit.new_project(projects, name="my big side project")
    assert [p.key for p in projects] == ["my-big-side", "p2", "my-big-sid-2"]


def test_slot_is_the_first_free_one(tmp_path):
    projects = edit.new_project((), name="a", slot=2)
    projects = edit.new_project(projects, name="b")
    projects = edit.new_project(projects, name="c")
    assert [(p.key, p.slot) for p in projects] == [("a", 2), ("b", 1), ("c", 3)]


def test_all_nine_slots_taken_is_refused():
    projects = ()
    for i in range(9):
        projects = edit.new_project(projects, name=f"n{i}")
    with pytest.raises(registry.ProjectsError) as err:
        edit.new_project(projects, name="ten")
    assert (err.value.code, err.value.field) == ("E_ALREADY", "slot")


@pytest.mark.parametrize(
    ("kwargs", "code", "field"),
    [
        ({"name": ""}, "E_BAD_ARG", "name"),
        ({"name": "-x"}, "E_BAD_ARG", "name"),
        ({"name": "名" * 9}, "E_BAD_ARG", "name"),
        ({"name": "a‮b"}, "E_BAD_ARG", "name"),
        ({"name": "ok", "key": "Bad"}, "E_BAD_ARG", "key"),
        ({"name": "ok", "key": "cpa"}, "E_ALREADY", "key"),
        ({"name": "ok", "slot": 10}, "E_BAD_ARG", "slot"),
        ({"name": "ok", "slot": 1}, "E_ALREADY", "slot"),
        ({"name": "ok", "folder": "/nope/never"}, "E_BAD_ARG", "folder"),
        ({"name": "ok", "mode": "bypassPermissions"}, "E_BAD_ARG", "mode"),
    ],
)
def test_bad_adds_say_which_argument(tmp_path, kwargs, code, field):
    with pytest.raises(registry.ProjectsError) as err:
        edit.new_project(_two(tmp_path), **kwargs)
    assert (err.value.code, err.value.field) == (code, field)


def test_folder_is_stored_as_realpath(tmp_path):
    real = tmp_path / "real"
    real.mkdir()
    (tmp_path / "link").symlink_to(real)
    (p,) = edit.new_project((), name="x", folder=str(tmp_path / "link"))
    assert p.root == os.path.realpath(real) and p.linked


def test_rename_keeps_key_and_slot(tmp_path):
    projects = edit.rename(_two(tmp_path), "blog", "博客")
    assert [(p.key, p.name, p.slot) for p in projects] == [("cpa", "Cardputer 助手", 1), ("blog", "博客", 2)]
    with pytest.raises(registry.ProjectsError) as err:
        edit.rename(projects, "nope", "x")
    assert (err.value.code, err.value.field) == ("E_UNKNOWN_PROJECT", "key")


def test_move_swaps_with_the_project_in_that_slot(tmp_path):
    projects = edit.move(_two(tmp_path), "blog", 1)
    assert {p.key: p.slot for p in projects} == {"cpa": 2, "blog": 1}
    assert {p.key: p.slot for p in edit.move(projects, "cpa", 7)} == {"cpa": 7, "blog": 1}
    with pytest.raises(registry.ProjectsError) as err:
        edit.move(projects, "cpa", 0)
    assert (err.value.code, err.value.field) == ("E_BAD_ARG", "slot")


def test_link_and_unlink_a_folder(tmp_path):
    projects = edit.link(_two(tmp_path), "blog", str(tmp_path))
    assert edit.registry.find(projects, "blog").root == os.path.realpath(tmp_path)
    projects = edit.link(projects, "blog", None)
    assert edit.registry.find(projects, "blog").root is None
    with pytest.raises(registry.ProjectsError) as err:
        edit.link(projects, "blog", str(tmp_path / "gone"))
    assert (err.value.code, err.value.field) == ("E_BAD_ARG", "folder")


def test_remove(tmp_path):
    assert [p.key for p in edit.remove(_two(tmp_path), "cpa")] == ["blog"]
    with pytest.raises(registry.ProjectsError) as err:
        edit.remove(_two(tmp_path), "zzz")
    assert (err.value.code, err.value.field) == ("E_UNKNOWN_PROJECT", "key")


def test_unlinked_round_trips_and_older_files_still_load(tmp_path):
    path = tmp_path / "projects.json"
    projects = _two(tmp_path)
    registry.save_registry(path, projects)
    assert json.loads(path.read_text())["projects"][1]["root"] is None
    assert registry.load_registry(path) == projects


def test_an_unlinked_project_never_matches_a_cwd(tmp_path):
    projects = edit.new_project((), name="loose")
    assert resolve.match_root(projects, str(tmp_path)) is None


def test_edits_never_mutate_the_input(tmp_path):
    projects = _two(tmp_path)
    before = tuple(projects)
    edit.move(projects, "blog", 1)
    edit.rename(projects, "blog", "x")
    assert projects == before


def test_a_key_seen_in_history_is_never_derived_again():
    """Review 2026-10-26: add 家务 (p1), rm, add 读书 must not get p1, or 家务's recordings show as 读书's."""
    (p,) = edit.new_project((), name="读书", used=frozenset({"p1"}))
    assert p.key != "p1" and p.slot == 1
    (b,) = edit.new_project((), name="Blog", used=frozenset({"blog"}))
    assert b.key == "blog-2"


def test_an_explicit_key_seen_in_history_needs_reuse():
    with pytest.raises(registry.ProjectsError) as err:
        edit.new_project((), name="Blog", key="blog", used=frozenset({"blog"}))
    assert (err.value.code, err.value.field) == ("E_ALREADY", "key")
    (p,) = edit.new_project((), name="Blog", key="blog", used=frozenset({"blog"}), reuse=True)
    assert p.key == "blog"
