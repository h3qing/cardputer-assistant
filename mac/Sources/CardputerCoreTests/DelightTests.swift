// The two small delights: inbox zero (today's recap, at most one celebration a day) and vlog timecodes (★ marks
// and transcript lines as chapter lines, and an SRT). All pure; the clock and calendar come in.

import CardputerCore
import Foundation

private let cal = Fixture.calendar
private let now = Fixture.now
/// 2026-09-16 09:00 +08:00, the morning of `now`'s day.
private let morning: Int64 = 1_789_520_400_000

private func row(_ sid: String, mode: String = "memo", at: Int64? = morning, dur: Int = 60_000,
                 project: String? = nil, marks: Int = 0, uncertain: Bool = false, hidden: Bool = false) throws -> RecRow {
    let object: [String: Any] = [
        "sid": sid, "mode": mode, "started_ms": at.map { $0 as Any } ?? NSNull(), "uncertain": uncertain,
        "dur_ms": dur, "state": "done", "project": project.map { $0 as Any } ?? NSNull(), "marks": marks,
        "hidden": hidden,
    ]
    return try JSONDecoder().decode(RecRow.self, from: JSONSerialization.data(withJSONObject: object))
}

private func sentence(_ seconds: Double?, _ text: String) -> TimedSentence {
    TimedSentence(seconds: seconds, text: text)
}

let delightTests = Suite(name: "Delight", cases: [
    test("today's recap: voice memos started today, their minutes, the projects they went to, the stars") {
        let rows = [
            try row("A", dur: 90_000, project: "p1", marks: 2),
            try row("B", at: morning + 3_600_000, dur: 45_000, project: "p3"),
            try row("C", at: morning + 7_200_000, dur: 30_000, project: "p1", marks: 1),
            try row("D", mode: "meeting", dur: 600_000, project: "p4", marks: 5),
            try row("E", at: morning - 86_400_000, project: "p2", marks: 3),
            try row("F", at: nil),
            try row("G", uncertain: true),
            try row("H", project: "p5", hidden: true),
        ]
        let recap = TodayRecap.make(rows, now: now, calendar: cal)
        return [
            expectEqual(recap, TodayRecap(day: "2026-09-16", memos: 3, minutes: 3, projects: 2, stars: 3)),
            expectEqual(recap.day, InboxZero.day(now, calendar: cal), "the day it counts"),
            expect(recap.anything),
            expectEqual(TodayRecap.make([], now: now, calendar: cal), TodayRecap(day: "2026-09-16", memos: 0, minutes: 0, projects: 0, stars: 0)),
            expect(!TodayRecap.make([], now: now, calendar: cal).anything),
        ]
    },
    test("today's minutes: rounded, and a short memo still counts as one") {
        [
            expectEqual(TodayRecap.make([try row("A", dur: 5_000)], now: now, calendar: cal).minutes, 1),
            expectEqual(TodayRecap.make([try row("A", dur: 150_000)], now: now, calendar: cal).minutes, 3),
            expectEqual(TodayRecap.make([try row("A", dur: 0)], now: now, calendar: cal).minutes, 0),
        ]
    },
    test("inbox zero: only from some waiting to none, at most once a day") {
        let today = InboxZero.day(now, calendar: cal)
        return [
            expectEqual(today, "2026-09-16"),
            expect(InboxZero.celebrates(from: 3, to: 0, lastDay: nil, today: today)),
            expect(InboxZero.celebrates(from: 1, to: 0, lastDay: "2026-09-15", today: today)),
            expect(!InboxZero.celebrates(from: 1, to: 0, lastDay: today, today: today), "once a day"),
            expect(!InboxZero.celebrates(from: 0, to: 0, lastDay: nil, today: today), "already empty"),
            expect(!InboxZero.celebrates(from: nil, to: 0, lastDay: nil, today: today), "the first answer"),
            expect(!InboxZero.celebrates(from: 2, to: nil, lastDay: nil, today: today), "not known"),
            expect(!InboxZero.celebrates(from: 2, to: 1, lastDay: nil, today: today)),
        ]
    },
    test("a timecode: HH:MM:SS, whole seconds down; SRT's with milliseconds") {
        [
            expectEqual(Timecodes.clock(0), "00:00:00"),
            expectEqual(Timecodes.clock(83.9), "00:01:23"),
            expectEqual(Timecodes.clock(3_725), "01:02:05"),
            expectEqual(Timecodes.clock(-4), "00:00:00"),
            expectEqual(Timecodes.srtClock(83.456), "00:01:23,456"),
            expectEqual(Timecodes.srtClock(3_725.0004), "01:02:05,000"),
        ]
    },
    test("vlog chapters with ★: 00:00:00 first, then only the starred lines (YouTube's chapters)") {
        let sentences = [sentence(3, "开场"), sentence(12.5, "去海边"), sentence(40, "路上"), sentence(80, "日落\n很美"),
                         sentence(nil, "无时间")]
        return [
            expectEqual(Timecodes.chapters(marks: [13, 14, 95, 100], sentences: sentences),
                        "00:00:00 开场\n00:00:12 ★ 去海边\n00:01:20 ★ 日落 很美"),
            expectEqual(Timecodes.chapters(marks: [5, 61], sentences: []), "00:00:00\n00:00:05 ★\n00:01:01 ★",
                        "no transcript"),
            expectEqual(Timecodes.chapters(marks: [2], sentences: [sentence(10, "后来")]), "00:00:00 后来\n00:00:02 ★",
                        "a mark before the first line has its own"),
            expectEqual(Timecodes.chapters(marks: [0.4], sentences: [sentence(0, "开场"), sentence(9, "后来")]),
                        "00:00:00 ★ 开场", "a star at the start is the first chapter, once"),
        ]
    },
    test("vlog chapters without ★: every line with its time") {
        let sentences = [sentence(0, "开场"), sentence(12.5, "去海边"), sentence(12.5, "  "), sentence(80, "日落")]
        return [
            expectEqual(Timecodes.chapters(marks: [], sentences: sentences), "00:00:00 开场\n00:00:12 去海边\n00:01:20 日落"),
            expectEqual(Timecodes.chapters(marks: [], sentences: []), ""),
        ]
    },
    test("SRT: one cue a line, ending where the next begins (the last at the end), ★ kept") {
        let sentences = [sentence(0, "开场"), sentence(3.2, "去海边"), sentence(nil, "无时间")]
        let want = "1\n00:00:00,000 --> 00:00:03,200\n开场\n\n2\n00:00:03,200 --> 00:00:09,000\n★ 去海边\n"
        return [
            expectEqual(Timecodes.srt(sentences: sentences, marks: [4], duration: 9), want),
            expectEqual(Timecodes.srt(sentences: [sentence(5, "末")], marks: [], duration: nil),
                        "1\n00:00:05,000 --> 00:00:09,000\n末\n", "no length known: four seconds"),
            expectEqual(Timecodes.srt(sentences: [sentence(5, "末")], marks: [], duration: 2),
                        "1\n00:00:05,000 --> 00:00:09,000\n末\n", "a length before it: four seconds"),
            expectEqual(Timecodes.srt(sentences: [], marks: [3], duration: 9), ""),
        ]
    },
    test("SRT: lines in the same second are one cue, empty lines none, every cue at least a second") {
        let sentences = [sentence(2, "一"), sentence(2, "二"), sentence(2.5, " \n "), sentence(2.4, "三"), sentence(9, "四")]
        let want = "1\n00:00:02,000 --> 00:00:03,000\n一 二\n\n2\n00:00:02,400 --> 00:00:09,000\n三\n\n"
            + "3\n00:00:09,000 --> 00:00:13,000\n四\n"
        return [expectEqual(Timecodes.srt(sentences: sentences, marks: [], duration: nil), want)]
    },
    test("the ★ marks of a manifest, in seconds, in order; kinds not known are skipped; its length") {
        let (dir, notes) = try notesCopy("marks")
        let session = dir.appendingPathComponent("recordings/2026-09-16/0545-3qzk1m7-00042-0034")
        let file = session.appendingPathComponent("manifest.json")
        var object = try JSONSerialization.jsonObject(with: Data(contentsOf: file)) as? [String: Any] ?? [:]
        object["marks"] = [["t_ms": 9000, "k": "star"], ["t_ms": 1500, "k": "star"], ["t_ms": 4000, "k": "flag"]]
        try JSONSerialization.data(withJSONObject: object).write(to: file)
        let none = dir.appendingPathComponent("recordings/2026-09-16/0546-3qzk1m7-00042-0035")
        return [
            expectEqual(notes.marks(in: session), [1.5, 9]),
            expectEqual(notes.marks(in: none), []),
            expectEqual(notes.length(in: dir.appendingPathComponent("recordings/2026-09-16/0544-3qzk1m7-00042-0033")),
                        33.408, "manifest duration_ms"),
            expectEqual(notes.length(in: dir.appendingPathComponent("recordings/nowhere")), nil),
            expectEqual(notes.marks(in: dir.appendingPathComponent("recordings/nowhere")), []),
        ]
    },
])
