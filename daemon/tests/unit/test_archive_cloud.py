"""Which export targets sync online (docs/15 §2.4): iCloud Drive, CloudStorage, iCloud Desktop & Documents,
legacy sync folders at the top of home, and folders a macOS File Provider marked."""

from __future__ import annotations

import subprocess
import sys

import pytest

from cpad.archive import cloud

ICLOUD_DOCS = "Library/Mobile Documents/com~apple~CloudDocs"


def _plain(path):
    return frozenset()


@pytest.fixture
def user_home(tmp_path):
    home = tmp_path / "user"
    home.mkdir()
    return home


@pytest.mark.parametrize("folder", ["Library/Mobile Documents/com~apple~CloudDocs", "Library/CloudStorage/Dropbox"])
def test_icloud_drive_and_cloud_storage(user_home, folder):
    (user_home / folder).mkdir(parents=True)
    assert cloud.in_cloud(user_home / folder / "backup", user_home, _plain)


@pytest.mark.parametrize("folder", ["Desktop", "Documents"])
def test_desktop_and_documents_count_only_while_icloud_keeps_them(user_home, folder):
    (user_home / folder).mkdir()
    assert not cloud.in_cloud(user_home / folder / "backup", user_home, _plain)
    (user_home / ICLOUD_DOCS / folder).mkdir(parents=True)
    assert cloud.in_cloud(user_home / folder / "backup", user_home, _plain)
    assert cloud.in_cloud(user_home / folder, user_home, _plain)
    assert not cloud.in_cloud(user_home / "Backups" / "x", user_home, _plain)


@pytest.mark.parametrize("folder", ["Dropbox", "Dropbox (Personal)", "Google Drive", "OneDrive - Acme"])
def test_legacy_sync_folders_at_the_top_of_home(user_home, folder):
    assert cloud.in_cloud(user_home / folder / "backup", user_home, _plain)
    assert not cloud.in_cloud(user_home / "Projects" / folder / "backup", user_home, _plain)


@pytest.mark.parametrize("mark", sorted(cloud.MARKS))
def test_a_folder_marked_by_a_sync_provider_counts_for_everything_below_it(user_home, mark):
    marked = user_home / "Work"
    (marked / "sub").mkdir(parents=True)
    names = {marked: frozenset({mark, "com.apple.provenance"})}
    assert cloud.in_cloud(marked / "sub" / "backup", user_home, lambda p: names.get(p, frozenset()))
    assert not cloud.in_cloud(user_home / "Other" / "backup", user_home, lambda p: names.get(p, frozenset()))


def test_other_extended_attributes_do_not_count(user_home):
    assert not cloud.in_cloud(user_home / "backup", user_home, lambda p: frozenset({"com.apple.macl"}))


@pytest.mark.skipif(sys.platform != "darwin", reason="File Provider marks are macOS extended attributes")
def test_real_extended_attributes_are_read_without_following_symlinks(user_home):
    folder = user_home / "Synced"
    folder.mkdir()
    subprocess.run(["/usr/bin/xattr", "-w", "com.apple.icloud.desktop", "1", str(folder)], check=True)
    assert "com.apple.icloud.desktop" in cloud.xattr_names(folder)
    assert cloud.in_cloud(folder / "backup", user_home)
    link = user_home / "link"
    link.symlink_to(folder)
    assert "com.apple.icloud.desktop" not in cloud.xattr_names(link)
    assert cloud.xattr_names(user_home / "missing") == frozenset()


def test_no_marks_off_macos(user_home, monkeypatch):
    monkeypatch.setattr(cloud.sys, "platform", "linux")
    assert cloud.xattr_names(user_home) == frozenset()
