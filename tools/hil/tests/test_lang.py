"""docs/21 §9.6: a HIL run puts back the language the device showed at its start (debug.lang at the end,
also after a failure; English when unknown), and the lang scenario's pure parts: debug.state's ui.lang, the keys."""

import asyncio
from types import SimpleNamespace

import pytest

from harness import keys as k
from harness.lang import SWITCH_KEYS, restore_lang, shown_lang, start_lang
from harness.select import UI
from scenarios import SCENARIOS
from scenarios.lang import lang as lang_scenario


class FakeDevice:
    def __init__(self, reply=None, error=None):
        self.sent = []
        self.reply = reply or {"t": "ack"}
        self.error = error

    async def request(self, wire, timeout=5.0):
        self.sent.append(dict(wire))
        if self.error:
            raise self.error
        return SimpleNamespace(msg=wire), SimpleNamespace(msg=self.reply)


def test_restore_sends_debug_lang_and_reports_the_ack():
    dev = FakeDevice()
    assert asyncio.run(restore_lang(dev)) is None
    assert asyncio.run(restore_lang(dev, "zh")) is None
    assert dev.sent == [{"t": "debug.lang", "lang": "en"}, {"t": "debug.lang", "lang": "zh"}]


def test_restore_says_what_went_wrong_but_never_raises():
    assert "E_BAD_FIELD" in asyncio.run(restore_lang(FakeDevice(reply={"t": "error", "code": "E_BAD_FIELD"})))
    assert "TimeoutError" in asyncio.run(restore_lang(FakeDevice(error=TimeoutError("no reply"))))


def test_the_start_language_is_read_and_english_when_unknown():
    assert asyncio.run(start_lang(FakeDevice(reply={"t": "debug.state", "ui": {"lang": "en"}}))) == "en"
    assert asyncio.run(start_lang(FakeDevice(reply={"t": "debug.state", "ui": {"lang": "zh"}}))) == "zh"
    assert asyncio.run(start_lang(FakeDevice(reply={"t": "debug.state", "ui": {}}))) == "en"
    assert asyncio.run(start_lang(FakeDevice(reply={"t": "debug.state", "ui": {"lang": "fr"}}))) == "en"
    assert asyncio.run(start_lang(FakeDevice(error=TimeoutError("no reply")))) == "en"


def test_shown_lang_reads_debug_state_ui():
    assert shown_lang({"ui": {"lang": "en"}}) == "en"
    assert shown_lang({"ui": {}}) is None
    assert shown_lang({}) is None


def test_the_switch_is_three_presses_opt_m_dot_enter():
    want = (k.Press(("opt", "m")), k.Press((".",)), k.ENTER)
    assert want == SWITCH_KEYS


def test_lang_is_a_ui_scenario():
    assert "lang" in UI and "lang" in SCENARIOS


class LangDevice:
    """The lang scenario's device: Opt+M . Enter flips ui.lang (a build that reports none switches to Chinese), a
    reboot keeps it; `wanted` is every ui.lang the scenario waited for."""

    def __init__(self, rig, lang):
        self.rig, self.lang, self.wanted = rig, lang, []

    def _state(self):
        return SimpleNamespace(msg={"t": "debug.state", "ui": {"lang": self.lang} if self.lang else {}})

    async def state(self):
        return self._state()

    async def home(self):
        pass

    async def presses(self, keys):
        self.lang = "en" if self.lang == "zh" else "zh"

    async def wait_state(self, pred, timeout, what=""):
        self.wanted.append(what)
        if pred(self._state().msg):
            return self._state()
        raise TimeoutError(what)

    async def reboot(self):
        self.rig.link = SimpleNamespace(hello=SimpleNamespace(boot=self.rig.link.hello.boot + 1))


def run_lang_scenario(start):
    rig = SimpleNamespace(link=SimpleNamespace(hello=SimpleNamespace(boot=1)))
    dev = LangDevice(rig, start)
    return asyncio.run(lang_scenario(SimpleNamespace(rig=rig, dev=dev))), dev.wanted


@pytest.mark.parametrize(("start", "wanted"), [("en", ["zh", "en"]), ("zh", ["en", "zh"])])
def test_the_lang_scenario_switches_to_the_other_language_and_back(start, wanted):
    res, got = run_lang_scenario(start)
    assert res["ok"] and res["start"] == start
    assert got == [f"ui.lang {w}" for w in wanted]


def test_the_lang_scenario_ends_in_english_when_the_start_is_unknown():
    res, got = run_lang_scenario(None)
    assert not res["ok"] and res["checks"][0] == {"name": "reports_its_language", "ok": False, "got": None}
    assert got == ["ui.lang zh", "ui.lang en"]


class FakeRig:
    """create_rig's stand-in: a link that came up, a scenario run that fails at its first step."""

    def __init__(self):
        self.link = SimpleNamespace(hello=None, port=SimpleNamespace(device="/dev/null"))
        self.tap = None
        self.closed = False

    async def start(self):
        pass

    async def ready(self, timeout=None):
        raise RuntimeError("the rig fell over")

    async def close(self, keep=False):
        self.closed = True


def test_the_run_puts_the_language_back_even_after_a_failure(monkeypatch, tmp_path):
    from harness import runner

    rig, restored = FakeRig(), []

    async def fake_restore(dev, lang):
        restored.append((dev.rig, lang))
        return None

    monkeypatch.setattr(runner, "create_rig", lambda **_: rig)
    monkeypatch.setattr(runner, "restore_lang", fake_restore)
    args = runner.parse_args(["--scenarios", "nav", "--report", str(tmp_path / "hil.json")])
    assert asyncio.run(runner._run(("nav",), args)) == 1
    assert restored == [(rig, "en")] and rig.closed  # failed before the start was read: English
