"""UrlAdapter and AppAdapter (docs/25 §5.3, §3.6) against a fake Mac and a fake pasteboard: nothing real opens."""

from __future__ import annotations

from dataclasses import dataclass, field, replace
from pathlib import Path

import pytest

from cpad.dest.handlers import DRAFT_HOLD_S, AppAdapter, UrlAdapter, resolve
from cpad.dest.handoff import draft_target
from cpad.dest.macos import APP_NOT_FOUND, AppInfo, Signature
from cpad.dest.model import AppOpts, Capture, Delivery, Dest, UrlOpts
from cpad.dest.pasteboard import ClipWriter

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture

APP = Path("/Applications/Test Chat.app")
BUNDLE, TEAM = "invalid.example.chat", "ABCDE12345"
TEXT = "周五前给 Alice 发报价\n第二行"
OPTS = UrlOpts("test-scheme://app/v1/prompt?text={text}&id={p}", BUNDLE, TEAM, effect="draft", verified="draft",
               focus="test-scheme://app/v1/agent?id={p}", max_text_utf16=60, max_url=2048, overflow="clip",
               map=(("*", "bot1"),))  # fmt: skip
CHAT = Dest("url.chat", "url", "Chat", 8, leaves_mac=True, optin="2026-10-22T09:00:00+00:00", url=OPTS)
NOTES = Dest("app.notes", "app", "Notes", 9, app=AppOpts(BUNDLE, TEAM))


@dataclass
class FakeMac:
    paths: tuple[Path, ...] = (APP,)
    sig: Signature = Signature(True, TEAM)
    schemes: frozenset[str] = frozenset({"test-scheme"})
    status: int = 0
    opened: list[tuple[str, object]] = field(default_factory=list)

    def app_paths(self, bundle: str) -> tuple[Path, ...]:
        return self.paths if bundle == BUNDLE else ()

    def signature(self, app: Path) -> Signature:
        return self.sig

    def info(self, app: Path) -> AppInfo | None:
        return AppInfo(app, BUNDLE, "Test Chat", "1.0", self.schemes)

    def open_url(self, url: str, app: Path) -> int:
        self.opened.append(("url", url))
        return self.status

    def open_app(self, app: Path) -> int:
        self.opened.append(("app", app))
        return self.status


@dataclass
class FakeBoard:
    fails: bool = False
    texts: list[str] = field(default_factory=list)

    def write(self, text: str) -> int:
        if self.fails:
            raise OSError("no pasteboard")
        self.texts.append(text)
        return len(self.texts)

    def change_count(self) -> int:
        return len(self.texts)

    def clear(self) -> None:
        self.texts.append("")


class Clock:
    def __init__(self) -> None:
        self.now = 1000.0

    def __call__(self) -> float:
        return self.now


def delivery(dest: Dest = CHAT, text: str = TEXT, by: str = "cli", source: str = "voice", project: str = "cpa"):
    capture = Capture("3QZK1M7-0000C-0005", source, dest.id, "pick", project, text)  # type: ignore[arg-type]
    return Delivery(f"x:{dest.id}", capture, dest, 1, by)  # type: ignore[arg-type]


def make(mac: FakeMac | None = None, board: FakeBoard | None = None, clip_on: bool = True, clock: Clock | None = None,
         recent: dict | None = None):  # fmt: skip
    mac, board = mac or FakeMac(), board or FakeBoard()
    writer = ClipWriter(board, schedule=lambda _s, _fn: None, run=_no_run)
    return UrlAdapter(mac, writer, lambda: clip_on, clock or Clock(), recent=recent or {}), mac, board


def _no_run(*_a, **_k):
    raise AssertionError("pbcopy must not run here")


# --- resolve: the pinned handler -------------------------------------------------------------


@pytest.mark.parametrize(
    ("mac", "scheme", "expected"),
    [(FakeMac(), "test-scheme", APP), (FakeMac(paths=()), "test-scheme", "E_DEST_MISSING"),
     (FakeMac(sig=Signature(True, "ZZZZZ99999")), None, "E_HANDLER_CHANGED"),
     (FakeMac(sig=Signature(False, TEAM)), None, "E_HANDLER_CHANGED"),
     (FakeMac(schemes=frozenset()), "test-scheme", "E_HANDLER_CHANGED"), (FakeMac(schemes=frozenset()), None, APP)],
)  # fmt: skip
def test_resolve(mac, scheme, expected):
    assert resolve(mac, OPTS, scheme) == expected


@dataclass
class TwoCopies(FakeMac):
    """An old or spoofed copy with the same bundle id comes back first."""

    bad: Path = Path("/Users/x/Downloads/Test Chat.app")
    signatures: dict = field(default_factory=dict)

    def app_paths(self, bundle: str) -> tuple[Path, ...]:
        return (self.bad, APP) if bundle == BUNDLE else ()

    def signature(self, app: Path) -> Signature:
        return self.signatures.get(app, Signature(True, TEAM))


def test_resolve_takes_the_first_copy_that_passes_every_check():
    mac = TwoCopies(signatures={TwoCopies.bad: Signature(True, None)})
    assert resolve(mac, OPTS, "test-scheme") == APP
    mac = TwoCopies(signatures={TwoCopies.bad: Signature(True, TEAM), APP: Signature(False, TEAM)})
    assert resolve(mac, OPTS, "test-scheme") == TwoCopies.bad
    assert resolve(TwoCopies(signatures={TwoCopies.bad: Signature(False, TEAM), APP: Signature(False, TEAM)}), OPTS,
                   None) == "E_HANDLER_CHANGED"  # fmt: skip


@pytest.mark.parametrize(("sig", "expected"), [(Signature(True, None, apple=True), APP),
                                               (Signature(True, None), "E_HANDLER_CHANGED"),
                                               (Signature(True, TEAM), "E_HANDLER_CHANGED")])  # fmt: skip
def test_no_team_id_is_trusted_only_for_apple_s_own_apps(sig, expected):
    """An ad hoc signature has no Team ID either: pinning to None must not match it."""
    assert resolve(FakeMac(sig=sig), replace(OPTS, team=None), None) == expected


# --- UrlAdapter ------------------------------------------------------------------------------


def test_a_link_is_opened_with_the_header_on_one_line_and_never_twice_as_a_draft():
    adapter, mac, board = make()
    result = adapter.deliver(delivery())
    assert (result.ok, result.via) == (True, "url") and board.texts == []
    ((kind, url),) = mac.opened
    assert kind == "url" and url.startswith("test-scheme://app/v1/prompt?text=%5BCardputer%20")
    assert "%0A" not in url and url.endswith("&id=bot1")
    assert "%E7%AC%AC%E4%BA%8C%E8%A1%8C" in url  # 第二行, after a space instead of the newline


def test_a_second_draft_for_the_same_target_goes_to_the_clipboard_and_brings_it_forward():
    clock = Clock()
    adapter, mac, board = make(clock=clock)
    assert adapter.deliver(delivery()).via == "url"
    second = adapter.deliver(delivery(text="再一条"))
    assert (second.ok, second.via, second.ref) == (True, "clip", "draft_busy")
    assert board.texts == ["[Cardputer 语音·可能有错字或别人的话·#cpa] 再一条"]
    assert mac.opened[-1] == ("url", "test-scheme://app/v1/agent?id=bot1")
    clock.now += DRAFT_HOLD_S + 1
    assert adapter.deliver(delivery(text="半小时后")).via == "url"


def test_a_draft_another_process_filled_is_busy_until_the_hold_ends():
    """cpa sends from the Mac run in a new process each time: the drafts it seeds from dest.handed{to} count."""
    clock = Clock()
    target = (CHAT.id, draft_target(OPTS, "cpa"))
    adapter, _, board = make(clock=clock, recent={target: clock.now - 60})
    assert adapter.deliver(delivery()).ref == "draft_busy" and len(board.texts) == 1
    stale, _, _ = make(clock=clock, recent={target: clock.now - DRAFT_HOLD_S - 1})
    assert stale.deliver(delivery()).via == "url"


def test_another_target_is_not_busy():
    adapter, mac, _ = make()
    other = replace(CHAT, url=replace(OPTS, map=(("*", "bot1"), ("lab", "bot2"))))
    assert adapter.deliver(delivery(other)).via == "url"
    assert adapter.deliver(delivery(other, project="lab")).via == "url"


def test_too_long_goes_to_the_clipboard_only_when_clip_is_on():
    long_text = "长" * 60
    adapter, mac, board = make()
    result = adapter.deliver(delivery(text=long_text))
    assert (result.ok, result.via, result.ref) == (True, "clip", "too_long") and board.texts[0].endswith(long_text)
    off, mac_off, board_off = make(clip_on=False)
    assert off.deliver(delivery(text=long_text)).code == "E_TOO_LONG"
    assert mac_off.opened == [] and board_off.texts == []


def test_overflow_without_a_focus_link_brings_the_app_forward():
    adapter, mac, _ = make()
    nofocus = replace(CHAT, url=replace(OPTS, focus=None))
    assert adapter.deliver(delivery(nofocus, text="长" * 60)).via == "clip"
    assert mac.opened == [("app", APP)]


def test_an_encoded_link_over_max_url_overflows():
    adapter, _, board = make()
    tight = replace(CHAT, url=replace(OPTS, max_text_utf16=1000, max_url=120))
    assert adapter.deliver(delivery(tight, text="长" * 20)).ref == "too_long" and len(board.texts) == 1


def test_no_overflow_configured_fails_without_touching_anything():
    adapter, mac, board = make()
    strict = replace(CHAT, url=replace(OPTS, overflow=None))
    assert adapter.deliver(delivery(strict, text="长" * 60)).code == "E_TOO_LONG"
    assert mac.opened == [] and board.texts == []


def test_a_busy_draft_without_the_clipboard_fails():
    adapter, _, _ = make(clip_on=False)
    assert adapter.deliver(delivery()).ok
    assert adapter.deliver(delivery()).code == "E_DRAFT_BUSY"


def test_the_clipboard_without_markers_needs_a_person():
    adapter, mac, _ = make(board=FakeBoard(fails=True))
    assert adapter.deliver(delivery(text="长" * 60, by="auto")).code == "E_CLIP_PLAIN"
    assert mac.opened == []


@pytest.mark.parametrize(("mac", "code"), [(FakeMac(paths=()), "E_DEST_MISSING"),
                                           (FakeMac(sig=Signature(True, None)), "E_HANDLER_CHANGED"),
                                           (FakeMac(status=APP_NOT_FOUND), "E_DEST_MISSING"),
                                           (FakeMac(status=-50), "E_DEST_IO")])  # fmt: skip
def test_failures_are_final(mac, code):
    adapter, _, _ = make(mac=mac)
    result = adapter.deliver(delivery())
    assert (result.ok, result.code, result.retryable) == (False, code, False)


def test_a_failed_open_does_not_hold_the_draft():
    mac = FakeMac(status=-50)
    adapter, _, _ = make(mac=mac)
    assert not adapter.deliver(delivery()).ok
    mac.status = 0
    assert adapter.deliver(delivery()).via == "url"


@pytest.mark.parametrize("text", [None, "", " \n\t​‮ "])
def test_nothing_to_hand_over(text):
    adapter, mac, _ = make()
    assert adapter.deliver(delivery(text=text)).code == "E_BAD_TEXT" and mac.opened == []


def test_a_url_dest_without_options_is_missing():
    adapter, _, _ = make()
    assert adapter.deliver(delivery(replace(CHAT, url=None))).code == "E_DEST_MISSING"


def test_probe_checks_the_handler_without_opening_anything():
    adapter, mac, _ = make()
    assert adapter.probe(CHAT).state == "ok"
    assert make(mac=FakeMac(paths=()))[0].probe(CHAT).state == "missing"
    assert make(mac=FakeMac(sig=Signature(True, None)))[0].probe(CHAT).reason == "E_HANDLER_CHANGED"
    assert adapter.probe(replace(CHAT, url=None)).state == "needs_setup"
    assert mac.opened == []


# --- AppAdapter ------------------------------------------------------------------------------


def app_adapter(mac: FakeMac | None = None, board: FakeBoard | None = None):
    mac, board = mac or FakeMac(), board or FakeBoard()
    return AppAdapter(mac, ClipWriter(board, schedule=lambda _s, _fn: None, run=_no_run)), mac, board


def test_app_copies_with_the_header_and_brings_the_app_forward_never_pastes():
    adapter, mac, board = app_adapter()
    result = adapter.deliver(delivery(NOTES, source="typed"))
    assert (result.ok, result.via) == (True, "clip")
    assert board.texts == ["[Cardputer 打字·#cpa] 周五前给 Alice 发报价 第二行"]
    assert mac.opened == [("app", APP)]


CHANGED = [(FakeMac(paths=()), "E_DEST_MISSING"), (FakeMac(sig=Signature(True, "ZZZZZ99999")), "E_HANDLER_CHANGED")]


@pytest.mark.parametrize(("mac", "code"), CHANGED)
def test_app_refuses_a_changed_handler_before_copying(mac, code):
    adapter, _, board = app_adapter(mac=mac)
    assert adapter.deliver(delivery(NOTES)).code == code and board.texts == []


def test_app_failures():
    adapter, mac, _ = app_adapter(board=FakeBoard(fails=True))
    assert adapter.deliver(delivery(NOTES, by="auto")).code == "E_CLIP_PLAIN" and mac.opened == []
    assert app_adapter()[0].deliver(delivery(NOTES, text=" ")).code == "E_BAD_TEXT"
    assert app_adapter()[0].deliver(delivery(replace(NOTES, app=None))).code == "E_DEST_MISSING"
    assert app_adapter()[0].probe(NOTES).state == "ok"
