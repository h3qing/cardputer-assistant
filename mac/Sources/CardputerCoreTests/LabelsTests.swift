import CardputerCore
import Foundation

private let cal = Fixture.calendar

private func at(_ hour: Int, _ minute: Int, uncertain: Bool = false) -> RecordedAt {
    // 2026-09-16 at hour:minute +08:00
    let wall = Int64(1_789_488_000_000) + Int64(hour * 3600 + minute * 60) * 1000
    return RecordedAt(wallMs: wall, tzMin: 480, uncertain: uncertain)
}

let labelsTests = Suite(name: "Labels", language: .zh, cases: [
    test("one number for what waits: this Mac's new rows and the other-Mac group, everywhere") {
        let counts = { (new: Int, other: Int, device: Int?) in
            InboxPage(source: .filing(FilingCounts(new: new, otherMac: other, clipsWaiting: 0, onDevice: device)),
                      entries: [], nextCursor: nil).summary
        }
        return [
            expectEqual(FilingCounts(new: 11, otherMac: 1, clipsWaiting: 0, onDevice: 2).waiting, 12),
            expectEqual(counts(11, 1, 2), "12 条待归档 · 其中别的电脑的项目 1 条 · 设备上还有 2 段"),
            expectEqual(counts(16, 0, 2), "16 条待归档 · 设备上还有 2 段"),
            expectEqual(counts(0, 1, nil), "1 条待归档 · 其中别的电脑的项目 1 条"),
            expectEqual(counts(0, 0, 0), "都归好了"),
        ]
    },
    test("which Mac pulled it: this Mac only when the name is this Mac's; no name is a Mac without a name") {
        let office = MacStamp(host: "公司", link: Open(.here))
        let nameless = MacStamp(host: nil, link: Open(.unknown))
        return [
            expectEqual(Labels.mac(office, thisMac: "公司"), "这台 Mac"),
            expectEqual(Labels.mac(office, thisMac: "家"), "公司"),
            expectEqual(Labels.mac(office, thisMac: nil), "公司"),
            expectEqual(Labels.mac(nameless, thisMac: "家"), "没起名的 Mac"),
            expectEqual(Labels.mac(nil, thisMac: "家"), nil),
        ]
    },
    test("one line for the Mac and the link: Office pulled · linked while recording") {
        [
            expectEqual(Labels.pulled(MacStamp(host: "公司", link: Open(.here)), thisMac: nil), "公司 取回 · 录音时连着它"),
            expectEqual(Labels.pulled(MacStamp(host: "公司", link: Open(.unknown)), thisMac: "公司"), "这台 Mac 取回 · 录音时没连"),
            expectEqual(Labels.pulled(MacStamp(host: nil, link: nil), thisMac: nil), "没起名的 Mac 取回"),
            expectEqual(Labels.pulled(MacStamp(host: "家", link: Open(raw: "later")), thisMac: nil), "家 取回"),
            expectEqual(Labels.pulled(nil, thisMac: nil), nil),
        ]
    },
    test("a recording without words is named by its time and kind") {
        [
            expectEqual(Labels.untitled(kind: Open(.memo), at: at(13, 25), calendar: cal), "13:25 的便条"),
            expectEqual(Labels.untitled(kind: Open(.meeting), at: at(9, 5), calendar: cal), "09:05 的会议"),
            expectEqual(Labels.untitled(kind: nil, at: at(9, 5, uncertain: true), calendar: cal), "~09:05 的录音"),
            expectEqual(Labels.untitled(kind: Open(.memo), at: RecordedAt(wallMs: nil, tzMin: nil, uncertain: true),
                                        calendar: cal), "时间未知的便条"),
        ]
    },
    test("why a transcription stopped, in words") {
        [
            expectEqual(Labels.blockedReason("engine"), "转写命令跑不起来"),
            expectEqual(Labels.blockedReason("disabled"), "转写关着"),
            expectEqual(Labels.resultError("E_STT"), "转写命令出错"),
            expectEqual(Labels.resultError("E_AUDIO"), "音频读不了"),
            expectEqual(Labels.resultError("E_LATER"), "E_LATER"),
            expectEqual(Labels.resultError("E\u{1B}X"), "E X"),
            expectEqual(Labels.mcpError("E_DISABLED"), "被拒：写入没开"),
            expectEqual(Labels.mcpError("E_OTHER"), "E_OTHER"),
        ]
    },
    test("the device line without the firmware, for the menu") {
        let status = try CpaJSON.status(try Fixture.data("cpa/status-running.json"))
        return [
            expectEqual(Labels.deviceShort(status), "设备连着"),
            expectEqual(Labels.deviceHeadline(status), "设备连着 · 固件 v0.4.0"),
        ]
    },
])

let suggestionTests = Suite(name: "Suggestion", language: .zh, cases: [
    test("exactly one project named in the words is suggested") {
        let thesis = ProjectMap(ProjectList(projects: [
            ProjectEntry(key: "acmeshop", name: "Acmeshop", slot: 1, ok: true),
            ProjectEntry(key: "cardputer", name: "Cardputer", slot: 2, ok: true),
            ProjectEntry(key: "thesis", name: "论文", slot: 3, ok: true),
            ProjectEntry(key: "home", name: "家", slot: 4, ok: true),
        ]))
        let suggest = { (text: String) in Suggestion.of(try makeEntry("0C01", text: text), projects: thesis)?.key }
        return [
            expectEqual(try suggest("论文第三章的实验图要重画"), "thesis"),
            expectEqual(try suggest("把 acmeshop 的接口改一下"), "acmeshop", "the key, any case"),
            expectEqual(try suggest("Cardputer 的固件"), "cardputer", "the name"),
            expectEqual(try suggest("论文和 Acmeshop 都要改"), nil, "two projects: no suggestion"),
            expectEqual(try suggest("cardputers 很多"), nil, "a Latin name only as a whole word"),
            expectEqual(try suggest("回家买菜"), nil, "a one-character name is too easy to hit"),
            expectEqual(try suggest("今天天气不错"), nil),
            expectEqual(Suggestion.of(try makeEntry("0C02", project: "thesis", text: "论文"), projects: thesis), nil,
                        "already filed"),
            expectEqual(Suggestion.of(try makeEntry("0C03", asked: "acme", text: "论文"), projects: thesis), nil,
                        "another Mac's project waits for its own confirmation"),
            expectEqual(Suggestion.of(try makeEntry("0C04", filing: "done", text: "论文"), projects: thesis), nil,
                        "only new rows"),
        ]
    },
    test("the suggestion line says it is a word match, not a model") {
        let slot = ProjectSlot(slot: 3, key: "thesis", name: "论文", ok: true)
        return [expectEqual(Suggestion.line(slot), "建议：3 论文（文字里提到了）· 按 Enter 批准")]
    },
])
