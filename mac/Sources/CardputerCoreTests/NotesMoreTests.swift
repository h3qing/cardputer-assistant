import CardputerCore
import Foundation

private let fm = FileManager.default
private let done = "recordings/2026-09-16/0544-3qzk1m7-00042-0033"
private let cal = Fixture.calendar
private let now = Fixture.now

let notesMoreTests = Suite(name: "NotesMore", language: .zh, cases: [
    test("audio bytes: a segment of a session inside the root, no link, capped") {
        let (dir, notes) = try notesCopy("audiobytes")
        let session = dir.appendingPathComponent(done)
        let segment = session.appendingPathComponent("seg-000.wav")
        try fm.createSymbolicLink(atPath: session.appendingPathComponent("seg-001.wav").path,
                                  withDestinationPath: segment.path)
        let outside = try Fixture.tempDir("loose").appendingPathComponent("seg-000.wav")
        try Data(count: 10).write(to: outside)
        return [
            expectEqual(try notes.readAudio(segment).count, 364),
            expectThrows("link", matching: { $0 as? NotesError == .notRegular }) {
                try notes.readAudio(session.appendingPathComponent("seg-001.wav"))
            },
            expectThrows("outside", matching: { $0 as? NotesError == .outside }) { try notes.readAudio(outside) },
            expectThrows("not a segment", matching: { $0 as? NotesError == .outside }) {
                try notes.readAudio(session.appendingPathComponent("result.json"))
            },
        ]
    },
    test("a segment's length from its size (16 kHz mono 16-bit after a 44-byte header), without reading it") {
        let (dir, notes) = try notesCopy("seglength")
        let session = dir.appendingPathComponent(done)
        let long = session.appendingPathComponent("seg-001.wav")
        try Data(count: 44 + 32_000 * 3).write(to: long)
        return [
            expectEqual(notes.segmentSeconds(session.appendingPathComponent("seg-000.wav")), 0.01),
            expectEqual(notes.segmentSeconds(long), 3),
            expectEqual(notes.segmentSeconds(session.appendingPathComponent("seg-009.wav")), nil),
            expectEqual(Playback.locate(4.5, durations: [3, 3, 3]).segment, 1),
            expectEqual(Playback.locate(4.5, durations: [3, 3, 3]).offset, 1.5),
            expectEqual(Playback.locate(99, durations: [3, 3]).segment, 1, "past the end: the last one"),
            expectEqual(Playback.locate(-1, durations: [3]).offset, 0),
            expectEqual(Playback.locate(1, durations: []).segment, 0),
        ]
    },
    test("summary.md as plain lines: links become their words, images their alt text, no blank runs") {
        let (dir, notes) = try notesCopy("summary")
        let session = dir.appendingPathComponent(done)
        let text = "# 纪要\n\n- 看 [报价单](https://example.com/x) 和 ![图](file:///etc/hosts)\n\n\n- 周五\u{1B}[2J 交\n"
        try Data(text.utf8).write(to: session.appendingPathComponent("summary.md"))
        return [
            expectEqual(notes.summary(in: session), ["# 纪要", "", "- 看 报价单 和 图", "", "- 周五 [2J 交"]),
            expectEqual(notes.summary(in: dir.appendingPathComponent("recordings/2026-09-16/0453-3qzk1m7-00042-0010")), nil),
        ]
    },
    test("the latest pull: written_at of the newest session's manifest, looked for in the newest day only") {
        let (dir, notes) = try notesCopy("pull")
        let (_, other) = try notesCopy("pull-empty-day")
        try fm.createDirectory(at: other.root.appendingPathComponent("recordings/2026-09-17/0100-3qzk1m7-00042-0099"),
                               withIntermediateDirectories: true)
        return [
            expectEqual(notes.latestPull(), "2026-09-16T05:50:00.000+08:00"),
            expectEqual(other.latestPull(), nil, "the newest day has no manifest yet: older days are not scanned"),
            expect(dir.path != other.root.path),
        ]
    },
    test("when: relative and clock, ~ when the device was unsure") {
        let sure = RecordedAt(wallMs: Int64((now.timeIntervalSince1970 - 180) * 1000), tzMin: 480, uncertain: false)
        let yesterday = RecordedAt(wallMs: 1_789_477_800_000, tzMin: 480, uncertain: false)  // 09-15 21:10
        let lastWeek = RecordedAt(wallMs: 1_789_276_200_000, tzMin: 480, uncertain: false)  // 09-13 13:10
        let unsure = RecordedAt(wallMs: Int64((now.timeIntervalSince1970 - 7200) * 1000), tzMin: 480, uncertain: true)
        return [
            expectEqual(Format.when(sure, now: now, calendar: cal), "3 分钟前 · 14:27"),
            expectEqual(Format.when(unsure, now: now, calendar: cal), "2 小时前 · ~12:30"),
            expectEqual(Format.when(RecordedAt(wallMs: nil, tzMin: nil, uncertain: true), now: now, calendar: cal), "时间未知"),
            expectEqual(Format.when(yesterday, now: now, calendar: cal), "昨天 21:10", "the clock only once"),
            expectEqual(Format.when(lastWeek, now: now, calendar: cal), "3 天前 · 13:10"),
            expectEqual(Format.instant("2026-10-21T14:50:31.000+08:00"), Date(timeIntervalSince1970: 1_792_565_431)),
            expectEqual(Format.instant("2026-09-16T05:49:00+08:00"), Date(timeIntervalSince1970: 1_789_508_940)),
            expectEqual(Format.instant("yesterday"), nil),
        ]
    },
    test("filter words and the row's Mac") {
        [
            expectEqual(InboxScope.allCases.map(Labels.scope), ["待归档", "别的电脑的项目", "已交给 AI", "看过了", "没转出文字", "全部"]),
            expectEqual(DateRange.allCases.map(Labels.dates), ["全部日期", "今天", "本周", "上周"], "not 全部 twice in one row"),
            expectEqual(Labels.mac(MacStamp(host: "公司", link: nil), thisMac: nil), "公司"),
            expectEqual(Labels.mac(MacStamp(host: nil, link: Open(.here)), thisMac: nil), "没起名的 Mac"),
            expectEqual(Labels.mac(nil, thisMac: nil), nil),
        ]
    },
])
