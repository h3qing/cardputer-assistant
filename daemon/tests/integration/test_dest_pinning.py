"""Pinning the app a destination hands to (docs/25 §5.3, §7.3): which copy, which signature, which apps never; and
what the one-time test counts (D12). Through main(), against a fake Mac and a fake pasteboard."""

from __future__ import annotations

import json
import time
from dataclasses import dataclass, field
from pathlib import Path

import pytest

from cpad.cli.main import main as cpa
from cpad.dest.macos import Signature
from cpad.events.log import EventLog
from cpad.events.records import iso
from tests.support import send_home as sh

OLD = Path("/Users/x/Downloads/My Notes.app")


@dataclass
class PickyMac(sh.FakeMac):
    """Signatures per app path, and extra copies of a bundle id that come back first."""

    signatures: dict[Path, Signature] = field(default_factory=dict)
    first: dict[str, Path] = field(default_factory=dict)

    def app_paths(self, bundle: str) -> tuple[Path, ...]:
        found = super().app_paths(bundle)
        return (self.first[bundle], *found) if bundle in self.first else found

    def signature(self, app: Path) -> Signature:
        return self.signatures.get(app, Signature(True, sh.TEAM))


@pytest.fixture
def home(short_tmp, monkeypatch):
    path = short_tmp / "cpa"
    monkeypatch.setenv("CPA_HOME", str(path))
    return path


@pytest.fixture
def mac(monkeypatch):
    fake, board = sh.install(monkeypatch, later_s=0.0)
    picky = PickyMac(fake.apps)
    monkeypatch.setattr("cpad.dest.cli_presets.MAC", lambda: picky)
    return picky, board


def dests(home: Path) -> dict:
    return {d["id"]: d for d in json.loads((home / "destinations.json").read_text(encoding="utf-8"))["dests"]}


def test_an_app_without_a_team_id_is_refused_unless_it_is_apple_s_own(home, mac, capsys):
    picky, _ = mac
    notes = picky.apps[sh.NOTES].path
    picky.signatures[notes] = Signature(True, None)  # e.g. codesign -s -: no certificate, no Team ID
    assert cpa(["dest", "add", "--app", sh.NOTES, "--local", "--yes"]) == 2
    assert "no Team ID" in capsys.readouterr().err
    picky.signatures[notes] = Signature(True, None, apple=True)
    assert cpa(["dest", "add", "--app", sh.NOTES, "--local", "--yes"]) == 0
    assert dests(home)["app.my-notes"]["app"] == {"bundle": sh.NOTES, "team": None}


def test_the_first_copy_that_passes_is_pinned_not_the_first_one_found(home, mac):
    picky, _ = mac
    picky.first[sh.NOTES] = OLD
    picky.signatures[OLD] = Signature(False, None)
    assert cpa(["dest", "add", "--app", sh.NOTES, "--local", "--yes"]) == 0
    assert dests(home)["app.my-notes"]["app"]["team"] == sh.TEAM


@pytest.mark.parametrize("bundle", ["com.apple.Terminal", "com.apple.terminal", "com.googlecode.iterm2"])
def test_a_denied_app_is_refused_for_any_kind(home, mac, capsys, bundle):
    picky, _ = mac
    picky.apps[bundle] = sh.info(bundle, "Shell")
    assert cpa(["dest", "add", "--app", bundle, "--yes"]) == 2
    assert "may not handle destinations" in capsys.readouterr().err
    assert not (home / "destinations.json").exists()


def test_verify_draft_needs_a_test_that_went_through_the_link(home, mac, capsys):
    assert cpa(["dest", "add", "--preset", "grok-bot", "--yes"]) == 0
    test = {"ev": "dest.handed", "at": iso(time.time()), "item": "test-1234", "dest": "url.grok", "kind": "url",
            "via": "clip", "by": "cli", "to": None}  # fmt: skip
    EventLog(home / "events.jsonl").append((test,))
    assert cpa(["dest", "verify", "url.grok", "--draft"]) == 2


def test_a_test_into_a_bot_that_still_holds_a_draft_is_not_handed(home, mac, capsys):
    picky, board = mac
    assert cpa(["dest", "add", "--preset", "grok-bot", "--yes"]) == 0
    no_bot = "e3b0c442"  # sha256("")[:8]: the draft of a link without a Bot
    real = {"ev": "dest.handed", "at": iso(time.time()), "item": "3QZK1M7-00045-0000", "dest": "url.grok",
            "kind": "url", "via": "url", "by": "app", "to": no_bot}  # fmt: skip
    EventLog(home / "events.jsonl").append((real,))
    capsys.readouterr()
    assert cpa(["dest", "test", "url.grok", "--send", "--json"]) == 1
    doc = json.loads(capsys.readouterr().out)
    assert (doc["handed"], doc["code"]) == (False, "E_DRAFT_BUSY")
    assert picky.opened == [] and board.texts == []


def test_an_earlier_test_does_not_block_the_next_test(home, mac, capsys):
    picky, _ = mac
    assert cpa(["dest", "add", "--preset", "grok-bot", "--yes"]) == 0
    assert cpa(["dest", "test", "url.grok", "--send"]) == 0
    assert cpa(["dest", "test", "url.grok", "--send"]) == 0
    assert [kind for kind, _ in picky.opened] == ["url", "url"]


def test_an_app_signed_without_apple_s_certificate_is_refused(home, mac, capsys):
    picky, _ = mac
    picky.signatures[picky.apps[sh.NOTES].path] = Signature(False, None)
    assert cpa(["dest", "add", "--app", sh.NOTES, "--local", "--yes"]) == 2
    assert "not signed by a certificate Apple issued" in capsys.readouterr().err
