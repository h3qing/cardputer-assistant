"""An in-memory archive Reader for the cpa mcp tests (the real one is cpad.mcp.backend over cpad.archive).

It records every call, honours `after` keys like the real pager, and raises the archive's own errors.
Filters (dates, mode, project...) are the archive's job; the fake only remembers what it was asked.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from dataclasses import dataclass, field
from datetime import date
from typing import Any

from cpad.archive.fs import ArchiveError
from cpad.mcp.tools import Keyed, ToolError

CANARY = "ZZ-CANARY-7f3a91-ZZ"
MEETING = "3qzk1m7-00042-0a1c"
MEMO = "3qzk1m7-00042-0a1f"

NOTE = "\n".join([
    "- 09:20 #cpa 买牛奶 ^cpa-3qzk1m7-0000c-0005",
    f"- [ ] 10:00 #cpa 给 Bob 回电话 {CANARY} ^cpa-3qzk1m7-0000c-0006",
    "- [x] 10:30 修好打印机 ^cpa-3qzk1m7-0000c-0007",
])  # fmt: skip


def _matching(text: str, query: str) -> list[tuple[int, str]]:
    return [(n, line) for n, line in enumerate(text.splitlines()) if query.casefold() in line.casefold()]


def _after(rows: Iterable[Keyed], after: str | None) -> list[Keyed]:
    return [r for r in rows if after is None or r.key < after]


def filing_row(rec: str, wall_ms: int, **over: Any) -> dict[str, Any]:
    """A row of the filing list (protocol/filing/v1 row) as cpad.filing.view gives it."""
    return {"id": rec, "kind": "memo", "filing": "new", "why": ["unfiled"], "asked": None,
            "at": {"wall_ms": wall_ms, "tz_min": 480, "uncertain": False}, "dur_ms": 24000, "device": None,
            "env": None, "prof": None, "project": None, "starred": False, "tx": "done", "title": "报价",
            "text": f"周五前发报价单。{CANARY}", "intent": "todo", "clips": None, "queue": None, **over}  # fmt: skip


@dataclass
class FakeReader:
    notes: dict[str, str] = field(default_factory=lambda: {"2026-10-21": NOTE, "2026-10-20": "- 旧的一天\n"})
    recs: list[Keyed] = field(default_factory=lambda: [
        Keyed(f"2026-10-21/1405-{MEMO}", {"id": MEMO, "date": "2026-10-21", "started": "2026-10-21T14:05:00+08:00",
                                          "mode": "memo", "state": "done", "title": f"便条 {CANARY}",
                                          "has_summary": True, "has_transcript": True, "todos_n": 0,
                                          "secret_path": "/Users/x/CardputerNotes"}),
        Keyed(f"2026-10-21/1402-{MEETING}", {"id": MEETING, "date": "2026-10-21",
                                             "started": "2026-10-21T14:02:00+08:00", "mode": "meeting",
                                             "project": "cpa", "duration_s": 675, "state": "done",
                                             "title": "客户同步：定价和交付", "has_summary": True,
                                             "has_transcript": True, "todos_n": 2}),
    ])  # fmt: skip
    sentences: dict[str, list[dict[str, Any]]] = field(default_factory=lambda: {
        MEETING: [{"i": 0, "t_s": 12.3, "text": f"我们先过一下 pricing 的方案。{CANARY}"},
                  {"i": 1, "t_s": 12.3, "text": "OK."},
                  {"i": 2, "t_s": 361.2, "text": "交付时间定在周五。"}]})  # fmt: skip
    summaries: dict[str, str] = field(default_factory=lambda: {MEETING: f"## 摘要\n讨论了 pricing。{CANARY}\n"})
    todo_rows: list[Keyed] = field(default_factory=lambda: [
        Keyed("2026-10-21/n/000002", {"text": f"给 Bob 回电话 {CANARY}", "done": False, "date": "2026-10-21",
                                      "project": "cpa", "source": {"kind": "note", "date": "2026-10-21"},
                                      "block": "^cpa-3qzk1m7-0000c-0006"}),
    ])  # fmt: skip
    capture_rows: list[Keyed] = field(default_factory=lambda: [
        Keyed("2026-10-21T15:00:00.000Z/3QZK1M7-0000D-0001", {
            "id": "3QZK1M7-0000D-0001", "kind": "note", "at": "2026-10-21T15:00:00.000Z", "project": "home",
            "text": f"Pricing idea {CANARY}", "dev": "020000000001"}),
    ])  # fmt: skip
    queue_rows: list[Keyed] = field(default_factory=lambda: [
        Keyed("q0000000002", {"qid": "q2", "project": "cpa", "intent": "todo", "text": f"修 bug {CANARY}",
                              "origin": "device", "uncertain": False, "sid": None}),
    ])  # fmt: skip
    unfiled_rows: list[dict[str, Any]] = field(
        default_factory=lambda: [
            filing_row(MEMO.upper(), 1792620300000),
            filing_row("3QZK1M7-00040-0000", 1792516500000, kind="meeting"),
        ]
    )
    calls: list[tuple[str, Any]] = field(default_factory=list)

    def days(self, a: Mapping[str, Any], after: str | None) -> Iterable[Keyed]:
        self.calls.append(("days", (dict(a), after)))
        rows = [Keyed(d, {"date": d, "bytes": len(t.encode()), "recordings": 2}) for d, t in self.notes.items()]
        return _after(sorted(rows, key=lambda r: r.key, reverse=True), after)

    def day(self, day: date, offset: int, budget: int) -> Mapping[str, Any]:
        self.calls.append(("day", (day, offset, budget)))
        text = self.notes.get(day.isoformat())
        if text is None:
            raise ArchiveError("E_NOT_FOUND")
        lines = text.splitlines(keepends=True)
        taken, size = [], 0
        for line in lines[offset:]:
            size += len(line.encode())
            if size > budget and taken:
                break
            taken.append(line)
        end = offset + len(taken)
        return {"date": day.isoformat(), "text": "".join(taken), "total_lines": len(lines),
                "next_offset": end if end < len(lines) else None}  # fmt: skip

    def recordings(self, a: Mapping[str, Any], after: str | None) -> Iterable[Keyed]:
        self.calls.append(("recordings", (dict(a), after)))
        return iter(_after(self.recs, after))

    def recording(self, sid: str, budget: int) -> Mapping[str, Any]:
        self.calls.append(("recording", (sid, budget)))
        if sid not in self.summaries and sid not in self.sentences:
            raise ArchiveError("E_NOT_FOUND")
        meta = {
            "mode": "meeting",
            "duration_s": 675,
            "device": {"dev": "020000000001"},
            "project": {"key": "cpa", "name": "Cardputer 助手"},
            "marks": [{"t_s": 12.8, "k": "star"}],
        }
        result = {
            "state": "done",
            "level": "summary",
            "title": "客户同步",
            "pipeline": {"app": "WW"},
            "actions": [{"n": 1, "text": "发报价单", "confident": True, "extra": 1}],
        }
        summary = {"text": self.summaries.get(sid, "")[:budget], "truncated": False}
        return {"id": sid.upper(), "state": "done", "meta": meta, "result": result, "summary": summary}

    def transcript(self, sid: str, offset: int, budget: int) -> Mapping[str, Any]:
        self.calls.append(("transcript", (sid, offset, budget)))
        if sid not in self.sentences:
            raise ArchiveError("E_NOT_FOUND")
        rows = self.sentences[sid][offset:]
        return {"sentences": rows, "total": len(self.sentences[sid]), "next_offset": None}

    def todos(self, a: Mapping[str, Any], after: str | None) -> Iterable[Keyed]:
        self.calls.append(("todos", (dict(a), after)))
        return _after(self.todo_rows, after)

    def captures(self, a: Mapping[str, Any], after: str | None) -> Iterable[Keyed]:
        self.calls.append(("captures", (dict(a), after)))
        return _after(self.capture_rows, after)

    def queue(self, project: str | None) -> Iterable[Keyed]:
        self.calls.append(("queue", project))
        return list(self.queue_rows)

    def unfiled(self, a: Mapping[str, Any]) -> Mapping[str, Any]:
        """The whole list in one page, as cpad.filing.view.listing gives it; filters are the archive's job."""
        self.calls.append(("unfiled", dict(a)))
        counts = {"new": len(self.unfiled_rows), "other_mac": 0, "clips_waiting": 0, "on_device": None}
        return {"v": 1, "counts": counts, "next_cursor": None, "rows": list(self.unfiled_rows)}

    def filing_item(self, sid: str) -> Mapping[str, Any]:
        """cpa filing show's document of an unfiled row."""
        self.calls.append(("filing_item", sid))
        row = next((r for r in self.unfiled_rows if r["id"].lower() == sid), None)
        if row is None:
            raise ToolError("E_NOT_FOUND")
        sentences = self.sentences.get(sid, [])
        return {"v": 1, "row": row, "text": row["text"], "todos": [], "audio": ["seg-000.wav"], "fw": "cpa-fw/0.4.0",
                "sentences": [{"t_s": s["t_s"], "text": s["text"]} for s in sentences], "queue_text": None}  # fmt: skip

    def search(self, query, scopes, start, end, limit, after=None) -> Mapping[str, Any]:
        """Every matching note line, newest day first, at position "<date>/1#<k>"; a page starts at `after`."""
        self.calls.append(("search", (query, scopes, start, end, limit, after)))
        hits = [{"source": {"kind": "notes", "date": d}, "line": n + 1, "offset": n, "snippet": line,
                 "pos": f"{d}/1#{k}"}
                for d, text in sorted(self.notes.items(), reverse=True)
                for k, (n, line) in enumerate(_matching(text, query))]  # fmt: skip
        rest = hits[next((n for n, h in enumerate(hits) if h["pos"] == after), 0) :]
        return {"hits": rest[:limit], "partial": False, "scanned": len(self.notes),
                "next": rest[limit]["pos"] if len(rest) > limit else None}  # fmt: skip


@dataclass
class FakeWriter:
    projects: tuple[str, ...] = ("cpa",)
    added: list[tuple[str, str]] = field(default_factory=list)

    def queue_add(self, project: str, text: str) -> str:
        if project not in self.projects:
            raise ToolError("E_BAD_ARG", "project")
        self.added.append((project, text))
        return f"q{len(self.added)}"


def fake_status() -> dict[str, Any]:
    return {"daemon": "not running", "link": None, "device": {}, "agent": {}, "projects": [], "queue_open": 1,
            "recordings": {}}  # fmt: skip
