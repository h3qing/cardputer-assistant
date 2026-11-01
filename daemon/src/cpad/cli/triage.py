"""cpa filing triage (docs/23 §3.6, D13): the filing list one recording at a time in this terminal, one key each,
for a Mac without WhisperWoof. No web page and no port (docs/15 §3.12).

- the rows are those of `cpa filing ls --orphans` (filing.view.listing), read once, the other-Mac group last;
- 1-9 file the recording under the project in that slot of this Mac (the 1-9 the device shows), 0 takes the
  project away, x is done, q hands a memo to AI and s hands it to be started as a session (each then asks 1-9):
  the writes of `cpa filing file|done|hand` (cli.filing: one record or none, decided again under the events lock),
  so a hand is still started only from the device board, in plan mode (ADR 0002); a row of the other-Mac group
  asks y/n first (D15); q and s refuse a meeting (D16) and a memo with no text yet before asking anything;
- every write says one line and moves to the next row; a refusal says why in words and stays; u takes this run's
  writes back one by one, newest first, says what the row went back to and returns to it; n skips; ? helps and
  shows the row again; Esc, Ctrl-D or Ctrl-C quits; a key a Chinese input method sent says to switch it off;
- keys typed before a row is on screen are thrown away (Term.drop, just before it is printed), so a double press
  files one row, never the next one unseen; never between q / s and its digit, or a digit and its y;
- p or space plays the first segment with /usr/bin/afplay: an argument array, no stdio, the file a confined
  descriptor read as /dev/fd/N (filing.view.segment), never a path; p again, moving on or quitting stops it; a
  player that ended with an error is said at the next key;
- the words are filing.screen's or fixed, in the language of the moment (cpad.i18n); not a terminal: refused,
  nothing read or written.
"""

from __future__ import annotations

import os
import subprocess
import sys
from collections.abc import Callable, Mapping
from dataclasses import dataclass, replace
from typing import Any, Protocol

from cpad.archive.fs import ArchiveError, Root
from cpad.cli import filing as cli_filing
from cpad.cli import term, triage_send
from cpad.config.settings import ConfigError, Paths, load_config
from cpad.filing import actions, query, screen, view
from cpad.filing.actions import Ask
from cpad.filing.args import E_ALREADY, E_BAD_ARG, E_NO_TEXT, E_UNAVAILABLE, FilingError
from cpad.filing.fold import Known
from cpad.filing.text import display_safe
from cpad.i18n import T

AFPLAY = "/usr/bin/afplay"
PAGE = query.LIMIT_MAX
QUIT = (term.ESC, term.EOF)
YES = ("y", "Y", term.ENTER)
SLOTS = tuple("123456789")
DIGITS = ("0", *SLOTS)
Row = Mapping[str, Any]
Spawn = Callable[..., Any]  # subprocess.Popen
QUIET = ("退出：这次什么都没动", "Quit: nothing changed this run")
NOT_A_TERMINAL = ("error: cpa filing triage reads one key at a time and needs a terminal; from a script use "
                  "cpa filing ls --json, then file, done or hand")  # fmt: skip

# --- one run -----------------------------------------------------------------------------------------


class Term(Protocol):
    def key(self) -> str: ...

    def say(self, line: str) -> None: ...

    def drop(self) -> None: ...


@dataclass(frozen=True, slots=True)
class Desk:
    """What a run reads once: the rows, this Mac's slots, the archive, and how to start a player."""

    paths: Paths
    root: Root
    rows: tuple[Row, ...]
    counts: Mapping[str, Any]
    known: Known
    spawn: Spawn


@dataclass(frozen=True, slots=True)
class Taken:
    """A write of this run that can be taken back: the row it was made on, and its undo (filing.actions)."""

    at: int
    undo: Mapping[str, Any]


@dataclass(frozen=True, slots=True)
class Seat:
    i: int = 0  # the row shown; len(rows) is the end
    taken: tuple[Taken, ...] = ()
    playing: Any = None  # the afplay child
    quit: bool = False


def _desk(paths: Paths, spawn: Spawn) -> Desk:
    root = Root.at(load_config(paths.config).notes_path())
    raw = {"orphans": True, "limit": PAGE}
    first = view.listing(paths, root, raw, view.on_device(paths))
    found, doc = tuple(first["rows"]), first
    while doc["next_cursor"]:
        doc = view.listing(paths, root, {**raw, "cursor": doc["next_cursor"]}, None)
        found = (*found, *doc["rows"])
    rows = tuple(sorted(found, key=lambda row: row["asked"] is not None))
    return Desk(paths, root, rows, first["counts"], view.known(paths.projects), spawn)


def _stop(proc: Any) -> None:
    if proc is not None and proc.poll() is None:
        proc.kill()  # afplay keeps nothing worth a graceful end, and SIGKILL cannot hang
        proc.wait()


def _stopped(seat: Seat) -> Seat:
    _stop(seat.playing)
    return replace(seat, playing=None)


def _show(desk: Desk, t: Term, seat: Seat) -> Seat:
    """The row (or the end); whatever was typed before it was there is thrown away first."""
    rows, end = desk.rows, ["", T(*screen.END)]
    found = screen.lines(rows[seat.i], seat.i + 1, len(rows), desk.known) if seat.i < len(rows) else end
    t.drop()
    for line in found:
        t.say(line)
    return seat


def _try(work: Callable[[], dict[str, Any]]) -> tuple[dict[str, Any] | None, FilingError | None]:
    try:
        return work(), None
    except FilingError as err:
        return None, err
    except (ConfigError, OSError):
        return None, FilingError(E_UNAVAILABLE)


def _write(desk: Desk, t: Term, seat: Seat, work: Callable[[], dict[str, Any]],
           said: Callable[[dict[str, Any]], str]) -> Seat:  # fmt: skip
    doc, err = _try(work)
    if doc is None:
        t.say(f"✗ {screen.refused(err)}")
        return seat
    t.say(said(doc) + (T(*screen.UNDO_TAIL) if doc["undo"] else ""))
    taken = (*seat.taken, Taken(seat.i, doc["undo"])) if doc["undo"] else seat.taken
    return _show(desk, t, replace(_stopped(seat), i=seat.i + 1, taken=taken))


def _sure(t: Term, row: Row, what: str) -> bool:
    """The other-Mac group: y (or Enter) before a write (D15); any other key leaves it."""
    if not row["asked"]:
        return True
    t.say(T(f"  {screen.asked(row)}。{what}？y/n", f"  {screen.asked(row)}. {what}? y/n"))
    if t.key() in YES:
        return True
    t.say(T(*screen.NOT_MOVED))
    return False


def _file(desk: Desk, t: Term, seat: Seat, key: str) -> Seat:
    row = desk.rows[seat.i]
    project = None if key == "0" else desk.known.slots.get(int(key))
    if key != "0" and project is None:
        t.say(T(f"✗ {key} 号没有项目（cpa project ls）", f"✗ no project in slot {key} (cpa project ls)"))
        return seat
    asked = row["asked"] and display_safe(row["asked"])
    what = (T(f"不要 {asked}，当成没项目", f"drop {asked} and leave it without a project") if project is None
            else T(f"归到 {key} {project}", f"file under {key} {project}"))  # fmt: skip
    if not _sure(t, row, what):
        return seat
    ask = Ask("file", row["id"], project=project, none=project is None, other=bool(row["asked"]))

    def said(doc: dict[str, Any]) -> str:
        if project is not None:
            return f"→ {key} {project}"
        if not doc["changed"]:
            return T("→ 本来就没有项目", "→ it had no project anyway")
        return T(f"→ 不再算 {asked}", f"→ no longer under {asked}") if asked else T("→ 退回待归档", "→ back to unfiled")

    return _write(desk, t, seat, lambda: cli_filing.write(desk.paths, ask), said)


def _done(desk: Desk, t: Term, seat: Seat) -> Seat:
    ask = Ask("done", desk.rows[seat.i]["id"])
    return _write(desk, t, seat, lambda: cli_filing.write(desk.paths, ask), lambda doc: T("→ 看过了", "→ done"))


def _hand(desk: Desk, t: Term, seat: Seat, spawn: bool) -> Seat:
    row = desk.rows[seat.i]
    refused = (E_BAD_ARG, "id") if row["kind"] != "memo" else (E_NO_TEXT, "text") if row["text"] is None else None
    if refused is not None:
        t.say(f"✗ {T(*screen.REFUSED[refused])}")
        return seat
    t.say(T(f"交给 AI{'（到设备上开）' if spawn else ''}：按 1-9 选项目（别的键取消）",
            f"To AI{' (start on the device)' if spawn else ''}: 1-9 for the project (other keys cancel)"))  # fmt: skip
    key = t.key()
    project = desk.known.slots.get(int(key)) if key in SLOTS else None
    if project is None:
        t.say(T(*screen.NOT_MOVED))
        return seat
    if not _sure(t, row, T(f"交给 AI，排到 {key} {project}", f"hand it to AI under {key} {project}")):
        return seat
    ask = Ask("hand", row["id"], project=project, other=bool(row["asked"]), spawn=spawn)
    where = T(" · 到设备看板上选中它按 Enter 开（plan）", " · select it on the device board, Enter starts it (plan)")
    return _write(desk, t, seat, lambda: cli_filing.hand(desk.paths, ask),
                  lambda doc: T("→ 已排队 ", "→ queued ") + f"{doc['queue']['qid']} · {key} {project}"
                  + (where if spawn else ""))  # fmt: skip


def _undo(desk: Desk, t: Term, seat: Seat) -> Seat:
    """The newest write of this run taken back, on its row; one that no longer applies is said and dropped, one
    that cannot be written now is kept for the next u."""
    if not seat.taken:
        t.say(T("✗ 这次还没有可以撤销的", "✗ nothing to undo in this run yet"))
        return seat
    last, rest = seat.taken[-1], seat.taken[:-1]
    doc, err = _try(lambda: cli_filing.write(desk.paths, actions.ask_of(last.undo)))
    if doc is None:
        started = last.undo["op"] == "reopen" and (err.code, err.field) == (E_ALREADY, "queue")
        why = T(*screen.UNDO_STARTED) if started else screen.refused(err)
        t.say(T(f"✗ 撤不了：{why}", f"✗ cannot undo: {why}"))
        return seat if err.code == E_UNAVAILABLE else replace(seat, taken=rest)
    t.say(screen.back(doc["filing"]))
    return _show(desk, t, replace(_stopped(seat), i=last.at, taken=rest))


def _play(desk: Desk, t: Term, seat: Seat) -> Seat:
    if seat.playing is not None and seat.playing.poll() is None:
        t.say(T("■ 停了", "■ stopped"))
        return _stopped(seat)
    try:
        name, fd = view.segment(desk.paths, desk.root, desk.rows[seat.i]["id"])
    except (ArchiveError, FilingError, OSError):
        t.say(T("✗ 没有能播的音频", "✗ no audio to play"))
        return seat
    try:
        proc = desk.spawn([AFPLAY, f"/dev/fd/{fd}"], pass_fds=(fd,), stdin=subprocess.DEVNULL,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)  # fmt: skip
    except OSError:
        t.say(T(f"✗ 播放不了（没有 {AFPLAY}）", f"✗ cannot play (no {AFPLAY})"))
        return seat
    finally:
        os.close(fd)
    t.say(T(f"▶ 播放 {name}（p 停）", f"▶ playing {name} (p stops)"))
    return replace(seat, playing=proc)


def _route(desk: Desk, t: Term, seat: Seat) -> Seat:
    """g, then a destination's letter: this memo's text sent there from the Mac (docs/25 D4); never a meeting."""
    row = desk.rows[seat.i]
    if row["kind"] != "memo":
        t.say(T(*triage_send.MEETING))
        return seat
    dest = triage_send.pick(desk.paths, t)
    if dest is None or not triage_send.send(desk.paths, t, row["id"], dest, field="id"):
        return seat
    return _show(desk, t, replace(_stopped(seat), i=seat.i + 1))


def _skip(desk: Desk, t: Term, seat: Seat) -> Seat:
    t.say(T("→ 跳过", "→ skipped"))
    return _show(desk, t, replace(_stopped(seat), i=seat.i + 1))


def _help(desk: Desk, t: Term, seat: Seat) -> Seat:
    for line in screen.HELP:
        t.say(T(*line))
    return _show(desk, t, seat)


def _reaped(t: Term, seat: Seat) -> Seat:
    """A player that ended by itself is let go; said when it failed (a bad file, no audio device)."""
    code = seat.playing.poll() if seat.playing is not None else None
    if code is None:
        return seat
    if code != 0:
        t.say(T("✗ 播放失败", "✗ playback failed"))
    return replace(seat, playing=None)


DO: Mapping[str, Callable[[Desk, Term, Seat], Seat]] = {
    "x": _done, "q": lambda d, t, s: _hand(d, t, s, spawn=False), "s": lambda d, t, s: _hand(d, t, s, spawn=True),
    "p": _play, " ": _play, "u": _undo, "n": _skip, "?": _help, "g": _route,
}  # fmt: skip


def _step(desk: Desk, t: Term, seat: Seat, key: str) -> Seat:
    seat = _reaped(t, seat)
    if key in QUIT:
        return replace(seat, quit=True)
    if seat.i >= len(desk.rows):
        return _undo(desk, t, seat) if key == "u" else replace(seat, quit=True)
    if key in DIGITS:
        return _file(desk, t, seat, key)
    do = DO.get(key)
    if do is None:
        t.say(T(*triage_send.UNKNOWN) if key.isascii() else T(*screen.IME))
        return seat
    return do(desk, t, seat)


def go(desk: Desk, t: Term) -> int:
    quit_, sent = triage_send.go(desk.paths, t)
    if quit_:
        t.say(T(f"退出：这次送了 {sent} 条", f"Quit: {sent} sent this run") if sent else T(*QUIET))
        return 0
    if not desk.rows:
        t.say(T("都归好了：没有待归档的录音", "All filed: no unfiled recordings"))
        return 0
    t.say(screen.opening(desk.counts))
    seat = _show(desk, t, Seat())
    try:
        while not seat.quit:
            seat = _step(desk, t, seat, t.key())
    except KeyboardInterrupt:
        pass
    finally:
        _stop(seat.playing)
    n = len(seat.taken)
    t.say(T(f"退出：这次做了 {n} 件", f"Quit: {n} done this run") if n else T(*QUIET))
    return 0


def run(paths: Paths, t: Term, spawn: Spawn = subprocess.Popen) -> int:
    """Read the list, then go through it with `t`."""
    return go(_desk(paths, spawn), t)


# --- the command -------------------------------------------------------------------------------------


def _is_terminal() -> bool:
    return sys.stdin.isatty() and sys.stdout.isatty()


def filing_triage(paths: Paths) -> int:
    if not _is_terminal():
        print(NOT_A_TERMINAL, file=sys.stderr)
        return 1
    try:
        desk = _desk(paths, subprocess.Popen)
    except (ConfigError, OSError):
        print(f"error: {FilingError(E_UNAVAILABLE)}: {cli_filing.HINTS[(E_UNAVAILABLE, None)]}", file=sys.stderr)
        return 1
    with term.raw(sys.stdin, sys.stdout) as t:
        return go(desk, t)


def add_parser(acts: Any) -> None:
    triage = acts.add_parser("triage", help="one recording at a time in this terminal: 1-9 file, x done, q/s hand, "
                             "p play, u undo")  # fmt: skip
    triage.set_defaults(fn=lambda paths, args, deps: filing_triage(paths))
