// docs/24 §2.7, review 2026-10-26: English sentences in English order, words a user says (not "bytes"), dates
// with a month name, and the language Follow the Mac picked shown on its button. This suite runs in English;
// inLanguage(.zh) checks the Chinese next to it.

import CardputerCore
import Foundation

private let cal = Fixture.calendar
private let now = Fixture.now

let englishTests = Suite(name: "English", cases: [
    test("a count on the device is one sentence with its number in place") {
        let later = try CpaJSON.status(try Fixture.data("cpa/status-later.json"))
        return [
            expectEqual(DeviceFacts.onDevice(later), "2 on the device when last seen"),
            expectEqual(inLanguage(.zh) { DeviceFacts.onDevice(later) }, "上次看到设备上有 2 段"),
            expectEqual(L10n.text("设备上还有 {0} 段", [2], in: .en), "2 still on the device"),
        ]
    },
    test("an older date has a month name, never 9/16") {
        [
            expectEqual(Format.relative(now.addingTimeInterval(-20 * 86400), now: now, calendar: cal), "Aug 27"),
            expectEqual(Format.relative(now.addingTimeInterval(-400 * 86400), now: now, calendar: cal), "Aug 12, 2025"),
        ]
    },
    test("Follow the Mac says which language it picked") {
        [
            expectEqual(LanguageChoice.system.title(["en-US"]), "Follow the Mac (English now)"),
            expectEqual(inLanguage(.zh) { LanguageChoice.system.title(["zh-Hans"]) }, "跟随 Mac（现在：中文）"),
            expectEqual(LanguageChoice.en.title(["zh-Hans"]), "English"),
            expectEqual(LanguageChoice.zh.title(["en-US"]), "中文"),
        ]
    },
    test("words a user says: letters and Chinese characters, segments, a project's to-do list") {
        let english = L10n.englishTable.values.joined(separator: "\n")
        return [
            expect(!english.contains("bytes"), "no 'bytes' in the English table"),
            expect(!english.contains("seg."), "no 'seg.'"),
            expect(!english.contains("which project's queue"), "the hand panel is a to-do list, not a queue"),
            expectEqual(L10n.text("{0} · 1 段", ["12s"], in: .en), "12s · 1 segment"),
            expectEqual(L10n.text("{0} · {1} 段", ["40s", 3], in: .en), "40s · 3 segments"),
        ]
    },
    test("the words the Chinese suites check, in English") {
        let summary = InboxPage(source: .filing(FilingCounts(new: 11, otherMac: 1, clipsWaiting: 0, onDevice: 2)),
                                entries: [], nextCursor: nil).summary
        let thesis = ProjectSlot(slot: 3, key: "thesis", name: "Thesis", ok: true)
        return [
            expectEqual(FilingState.allCases.map(Labels.filing), ["Inbox", "Filed", "Handed to AI", "Done", "Hidden"]),
            expectEqual(Format.day(now.addingTimeInterval(-3600), now: now, calendar: cal), "Today"),
            expectEqual(Format.day(now.addingTimeInterval(-86400), now: now, calendar: cal), "Yesterday"),
            expectEqual(Labels.pulled(MacStamp(host: "Office", link: Open(.here)), thisMac: nil),
                        "Office pulled · linked while recording"),
            expectEqual(summary, "12 waiting · 1 of them for another Mac's project · 2 still on the device"),
            expectEqual(Suggestion.line(thesis), "Suggested: 3 Thesis (named in the words) · Enter to approve"),
        ]
    },
    test("one update command, the same wherever the app asks for a newer cpa") {
        [expectEqual(Setup.updateCpa, "uv tool install --force ./daemon")]
    },
])
