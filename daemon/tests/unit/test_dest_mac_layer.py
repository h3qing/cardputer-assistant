"""The macOS layer (docs/25 §5.3): ClipWriter with fakes, the handoff text, and the read-only parts of
LaunchServices. Writing the clipboard or opening anything for real is refused by tests/conftest.py."""

from __future__ import annotations

import ctypes
import plistlib
import subprocess
import sys
from pathlib import Path

import pytest

from cpad.dest import macos
from cpad.dest.handoff import clean, handed_text, header, map_value, plan, utf16_len
from cpad.dest.model import Capture, UrlOpts
from cpad.dest.pasteboard import CLEAR_AFTER_S, CLIP_ENV, ClipWrite, ClipWriter, NativePasteboard

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture

# --- ClipWriter ---------------------------------------------------------------------------------


class Board:
    def __init__(self, fails: bool = False) -> None:
        self.fails, self.count, self.texts, self.cleared = fails, 0, [], 0

    def write(self, text: str) -> int:
        if self.fails:
            raise OSError("unavailable")
        self.count += 1
        self.texts.append(text)
        return self.count

    def change_count(self) -> int:
        return self.count

    def clear(self) -> None:
        self.cleared += 1


class Later:
    def __init__(self) -> None:
        self.calls = []

    def __call__(self, delay, fn) -> None:
        self.calls.append((delay, fn))


class Run:
    def __init__(self, returncode: int = 0, raises: BaseException | None = None) -> None:
        self.returncode, self.raises, self.calls = returncode, raises, []

    def __call__(self, argv, **kwargs):
        self.calls.append((argv, kwargs))
        if self.raises is not None:
            raise self.raises
        return subprocess.CompletedProcess(argv, self.returncode)


def test_native_write_is_concealed_and_cleared_after_two_minutes_if_still_ours():
    board, later = Board(), Later()
    assert ClipWriter(board, schedule=later, run=Run()).write("秘密", allow_plain=False) == ClipWrite(True, True)
    ((delay, clear),) = later.calls
    assert delay == CLEAR_AFTER_S == 120
    clear()
    assert board.cleared == 1


def test_a_copy_made_since_is_never_cleared():
    board, later = Board(), Later()
    ClipWriter(board, schedule=later, run=Run()).write("a", allow_plain=False)
    board.count += 1  # you copied something else
    later.calls[0][1]()
    assert board.cleared == 0


def test_clearing_is_best_effort():
    board, later = Board(), Later()
    ClipWriter(board, schedule=later, run=Run()).write("a", allow_plain=False)
    board.change_count = lambda: (_ for _ in ()).throw(OSError("gone"))  # type: ignore[method-assign]
    later.calls[0][1]()


def test_without_the_native_pasteboard_plain_pbcopy_needs_a_person():
    run = Run()
    writer = ClipWriter(Board(fails=True), run=run, exe="/bin/true-pbcopy")
    assert writer.write("x", allow_plain=False) == ClipWrite(False, code="E_CLIP_PLAIN") and run.calls == []
    assert writer.write("周五", allow_plain=True) == ClipWrite(True, False)
    ((argv, kwargs),) = run.calls
    assert argv == ["/bin/true-pbcopy"] and kwargs["input"] == "周五".encode() and kwargs["env"] == dict(CLIP_ENV)


@pytest.mark.parametrize(
    ("run", "code"),
    [(Run(1), "E_DEST_IO"), (Run(raises=FileNotFoundError()), "E_DEST_MISSING"),
     (Run(raises=subprocess.TimeoutExpired("pbcopy", 2)), "E_DEST_TIMEOUT"), (Run(raises=OSError()), "E_DEST_IO")],
)  # fmt: skip
def test_plain_failures(run, code):
    assert ClipWriter(None, run=run).write("x", allow_plain=True) == ClipWrite(False, code=code)


def test_plain_pbcopy_survives_a_lone_surrogate():
    run = Run()
    assert ClipWriter(None, run=run).write("a\ud800", allow_plain=True) == ClipWrite(True, False)
    assert run.calls[0][1]["input"] == b"a?"


def test_the_real_clipboard_is_out_of_bounds_in_tests():
    with pytest.raises(AssertionError):
        NativePasteboard().write("x")
    with pytest.raises(AssertionError):
        ClipWriter(None).write("x", allow_plain=True)


# --- handoff -----------------------------------------------------------------------------------


def test_header_and_cleaning():
    assert header("voice", "acmeshop") == "[Cardputer 语音·可能有错字或别人的话·#acmeshop] "
    assert header("typed", None) == "[Cardputer 打字] "
    assert clean(" a\nb\tc\x00d\x7fe\x85f\u202eg\u200bh\ufeff ") == "a b c d e fgh"
    assert clean("一\u2028二\u2029三") == "一 二 三"  # line and paragraph separators: still one line
    cap = Capture("r", "voice", "url.x", "pick", None, "!rm -rf ~\n/clear")
    assert handed_text(cap) == "[Cardputer 语音·可能有错字或别人的话] !rm -rf ~ /clear"
    assert handed_text(Capture("r", "typed", "url.x", text="\n\u200b")) is None


def test_a_lone_surrogate_becomes_a_replacement_character():
    assert clean("a\ud800b\udfff") == "a\ufffdb\ufffd"
    opts = UrlOpts("t://a?q={text}", "b.c", None)
    assert plan(opts, clean("x\udc00"), None).url == "t://a?q=x%EF%BF%BD"


def test_lengths_are_utf16_code_units():
    assert utf16_len("a中😀") == 4


def test_map_value_and_plan():
    opts = UrlOpts("test-scheme://a?t={text}&id={p}&pr={project}", "b.c", None, focus="test-scheme://f?id={p}",
                   max_text_utf16=10, map=(("*", "all"), ("cpa", "c1")))  # fmt: skip
    assert (map_value(opts, "cpa"), map_value(opts, "x"), map_value(opts, None)) == ("c1", "all", "all")
    assert map_value(UrlOpts("t://a?q={text}", "b.c", None), "cpa") == ""
    ok = plan(opts, "hi", "cpa")
    assert (ok.url, ok.focus, ok.reason) == ("test-scheme://a?t=hi&id=c1&pr=cpa", "test-scheme://f?id=c1", "ok")
    assert plan(opts, "😀" * 6, "cpa").reason == "too_long"
    assert plan(UrlOpts("t://a?q={text}", "b.c", None, max_url=16), "abcdefghij", None).url is None


# --- LaunchServices, read-only -----------------------------------------------------------------

mac_only = pytest.mark.skipif(sys.platform != "darwin", reason="macOS frameworks")


def test_the_launch_spec_has_the_packed_layout_of_lsopen_h():
    assert ctypes.sizeof(macos.LaunchSpec) == 36  # 3 pointers, UInt32 flags, pointer; #pragma pack(2)
    assert macos.LaunchSpec.flags.offset == 24 and macos.LaunchSpec.ref_con.offset == 28


@mac_only
def test_bundle_lookup_and_signature_of_a_system_app():
    ls = macos.LaunchServices()
    (path, *_) = ls.app_paths("com.apple.finder")
    assert path.name == "Finder.app"
    signature = ls.signature(path)
    assert signature == macos.Signature(True, None, apple=True)  # an Apple platform binary has no Team ID
    assert ls.app_paths("invalid.example.does-not-exist") == ()
    assert ls.signature(Path("/nonexistent/X.app")) == macos.Signature(False, None)
    assert ls.info(path).bundle == "com.apple.finder"


@mac_only
def test_an_ad_hoc_signature_is_not_valid(tmp_path):
    """codesign -s - signs with no certificate: no Team ID, no Apple anchor; it must never pass as pinned."""
    app = _app(tmp_path, "Adhoc", {"CFBundleIdentifier": "invalid.example.adhoc", "CFBundleExecutable": "x"})
    (app / "Contents" / "MacOS").mkdir()
    exe = app / "Contents" / "MacOS" / "x"
    exe.write_bytes(Path("/usr/bin/true").read_bytes())
    exe.chmod(0o755)
    done = subprocess.run(["/usr/bin/codesign", "-s", "-", "--force", str(app)], capture_output=True, check=False)
    if done.returncode != 0:
        pytest.skip("codesign is not available")
    assert macos.LaunchServices().signature(app) == macos.Signature(False, None)


@mac_only
def test_opening_is_refused_in_tests():
    with pytest.raises(AssertionError):
        macos.LaunchServices().open_url("about:blank", Path("/System/Applications/TextEdit.app"))


def _app(root: Path, name: str, info: dict) -> Path:
    app = root / f"{name}.app"
    (app / "Contents").mkdir(parents=True)
    (app / "Contents" / "Info.plist").write_bytes(plistlib.dumps(info))
    return app


def test_read_info_and_installed_apps(tmp_path):
    a = _app(tmp_path, "Chat", {"CFBundleIdentifier": "invalid.example.chat", "CFBundleShortVersionString": "1.2",
                                "CFBundleURLTypes": [{"CFBundleURLSchemes": ["Test-Scheme", 5]}, "junk"]})  # fmt: skip
    _app(tmp_path / "Utils", "Notes", {"CFBundleIdentifier": "invalid.example.notes", "CFBundleDisplayName": "笔记"})
    _app(tmp_path, "Broken", {"CFBundleName": "no id"})
    (tmp_path / "Empty.app").mkdir()
    info = macos.read_info(a)
    assert (info.bundle, info.name, info.version, info.schemes) == ("invalid.example.chat", "Chat", "1.2",
                                                                    frozenset({"test-scheme"}))  # fmt: skip
    apps = macos.installed_apps([tmp_path, tmp_path / "missing"])
    assert [(x.name, x.bundle) for x in apps] == [("Chat", "invalid.example.chat"), ("笔记", "invalid.example.notes")]


class FakeCF:
    """Stands in for the frameworks so _launch can be checked without LaunchServices."""

    array_callbacks = 0

    def __init__(self) -> None:
        self.specs, self.released, self.urls = [], [], []

    def file(self, path: Path) -> int:
        return 100

    def url(self, _alloc, data, length, _enc, _base) -> int:
        self.urls.append(data[:length].decode())
        return 200

    def array(self, _alloc, values, count, _callbacks) -> int:
        return 300 if count == 1 and values[0] == 200 else 0

    def open(self, spec, _out) -> int:
        s = spec._obj
        self.specs.append((s.app_url, s.item_urls, s.pass_thru, s.flags))
        return 0

    def release(self, ref) -> None:
        self.released.append(ref)


def test_launch_always_names_the_pinned_app_and_releases_what_it_made():
    ls = macos.LaunchServices()
    cf = FakeCF()
    ls._cf = cf  # type: ignore[assignment]
    app = Path("/Applications/Test Chat.app")
    assert macos.LaunchServices._launch(ls, app, (cf.url(None, b"test-scheme://a?q=x", 19, 0, None),)) == 0
    assert macos.LaunchServices._launch(ls, app, ()) == 0
    assert cf.specs == [(100, 300, None, macos.LAUNCH_DEFAULTS), (100, None, None, macos.LAUNCH_DEFAULTS)]
    assert sorted(cf.released) == [100, 100, 200, 300] and cf.urls == ["test-scheme://a?q=x"]


@mac_only
def test_the_pasteboard_calls_exist_and_reading_the_change_count_works():
    """Read-only: the selectors the writer sends are NSPasteboard's, and changeCount answers (nothing is written)."""
    objc = ctypes.CDLL("/usr/lib/libobjc.A.dylib")
    ctypes.CDLL("/System/Library/Frameworks/AppKit.framework/AppKit")
    objc.objc_getClass.restype, objc.objc_getClass.argtypes = ctypes.c_void_p, [ctypes.c_char_p]
    objc.sel_registerName.restype, objc.sel_registerName.argtypes = ctypes.c_void_p, [ctypes.c_char_p]
    objc.class_getInstanceMethod.restype = ctypes.c_void_p
    objc.class_getInstanceMethod.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    board_class = objc.objc_getClass(b"NSPasteboard")
    for selector in (b"prepareForNewContentsWithOptions:", b"setString:forType:", b"clearContents", b"changeCount"):
        assert objc.class_getInstanceMethod(board_class, objc.sel_registerName(selector)), selector
    assert isinstance(NativePasteboard().change_count(), int)
