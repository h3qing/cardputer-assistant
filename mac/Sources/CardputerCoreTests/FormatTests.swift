import CardputerCore
import Foundation

private let cal = Fixture.calendar
private let now = Fixture.now

private func ago(_ seconds: TimeInterval) -> Date {
    now.addingTimeInterval(-seconds)
}

let formatTests = Suite(name: "Format", language: .zh, cases: [
    test("durations read like cpa rec ls: seconds under 10 minutes, whole minutes above") {
        [
            expectEqual(Format.duration(ms: 51_000), "51s"),
            expectEqual(Format.duration(ms: 299_400), "4m59s"),
            expectEqual(Format.duration(ms: 245_000), "4m05s"),
            expectEqual(Format.duration(ms: 499), "0s"),
            expectEqual(Format.duration(ms: 59_600), "1m00s"),
            expectEqual(Format.duration(ms: 600_000), "10m"),
            expectEqual(Format.duration(ms: 660_000), "11m"),
            expectEqual(Format.duration(ms: nil), "-"),
        ]
    },
    test("relative time in Chinese") {
        [
            expectEqual(Format.relative(ago(20), now: now, calendar: cal), "刚刚"),
            expectEqual(Format.relative(now.addingTimeInterval(90), now: now, calendar: cal), "刚刚", "a clock ahead"),
            expectEqual(Format.relative(ago(3 * 60 + 5), now: now, calendar: cal), "3 分钟前"),
            expectEqual(Format.relative(ago(59 * 60), now: now, calendar: cal), "59 分钟前"),
            expectEqual(Format.relative(ago(2 * 3600 + 10), now: now, calendar: cal), "2 小时前"),
            expectEqual(Format.relative(ago(20 * 3600), now: now, calendar: cal), "昨天 18:30"),
            expectEqual(Format.relative(ago(3 * 86400), now: now, calendar: cal), "3 天前"),
            expectEqual(Format.relative(ago(20 * 86400), now: now, calendar: cal), "8月27日"),
            expectEqual(Format.relative(ago(400 * 86400), now: now, calendar: cal), "2025年8月12日"),
        ]
    },
    test("clock time at the recording's own offset, ~ when the device clock was unsure") {
        let wall: Int64 = 1_789_489_800_000  // 2026-09-16 00:30 +08:00
        return [
            expectEqual(Format.clock(wallMs: wall, tzMin: 480, uncertain: false), "00:30"),
            expectEqual(Format.clock(wallMs: wall, tzMin: -420, uncertain: true), "~09:30"),
            expectEqual(Format.clock(wallMs: nil, tzMin: 480, uncertain: true), "时间未知"),
            expectEqual(Format.clock(wallMs: wall, tzMin: nil, uncertain: false, calendar: cal), "00:30"),
        ]
    },
    test("day headers: Today, Yesterday, then the date") {
        [
            expectEqual(Format.day(ago(3600), now: now, calendar: cal), "今天"),
            expectEqual(Format.day(ago(86400), now: now, calendar: cal), "昨天"),
            expectEqual(Format.day(ago(5 * 86400), now: now, calendar: cal), "9月11日"),
        ]
    },
    test("display_safe: C0, DEL, C1, U+2028/9 become spaces; bidi controls go") {
        [
            expectEqual(DisplaySafe.clean("a\u{1b}[31mb"), "a [31mb"),
            expectEqual(DisplaySafe.clean("x\u{7f}y\u{85}z\u{2028}w"), "x y z w"),
            expectEqual(DisplaySafe.clean("ab\u{202e}cd\u{2066}e"), "abcde"),
            expectEqual(DisplaySafe.clean("周五前给 Alice 发报价单。"), "周五前给 Alice 发报价单。"),
            expect(DisplaySafe.isSafe("周五 OK.")),
            expect(!DisplaySafe.isSafe("a\u{9b}b")),
            expect(!DisplaySafe.isSafe("a\u{2069}")),
        ]
    },
    test("labels of the filing states, the Mac stamp, the intent and the firmware") {
        [
            expectEqual(FilingState.allCases.map(Labels.filing), ["待归档", "已归档", "已交给 AI", "看过了", "藏起来了"]),
            expectEqual(Labels.macLink(Open(.here)), "录音时连着这台 Mac"),
            expectEqual(Labels.macLink(Open(.unknown)), "录音时没连电脑"),
            expectEqual(Labels.macLink(Open(raw: "other")), "未知"),
            expectEqual(Labels.intent(Open(.todo)), "待办"),
            expectEqual(Labels.kind(Open(.memo)), "便条"),
            expectEqual(Labels.firmware("cpa-fw/0.4.1"), "v0.4.1"),
            expectEqual(Labels.firmware("dev"), "dev"),
        ]
    },
    test("excerpt: display_safe, trimmed, cut to a number of characters, nil when empty") {
        [
            expectEqual(DisplaySafe.excerpt("  你好\u{202e}世界  ", limit: 280), "你好世界"),
            expectEqual(DisplaySafe.excerpt(String(repeating: "字", count: 300), limit: 280)?.count, 280),
            expectEqual(DisplaySafe.excerpt("\u{202e}  ", limit: 280), nil),
            expectEqual(DisplaySafe.excerpt(nil, limit: 280), nil),
        ]
    },
])
