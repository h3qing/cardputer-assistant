// docs/24 §2.7: the app in English or Chinese. Every UI string is tr("<Chinese key> {0}", value); the English
// table has a line for each, with the same placeholders, and no Chinese literal is shown without tr.

import CardputerCore
import Foundation

private let sources = Fixture.root.deletingLastPathComponent().appendingPathComponent("Sources")
private let appDirs = ["CardputerCore", "CardputerUI", "CardputerAssistant"]
/// Every target that may call tr(): the app and the screenshots (which draw the app's words).
private let trDirs = appDirs + ["CardputerShots"]

private func appFiles(_ dirs: [String] = appDirs) throws -> [(name: String, text: String)] {
    try dirs.flatMap { dir -> [(name: String, text: String)] in
        let root = sources.appendingPathComponent(dir)
        let paths = FileManager.default.enumerator(atPath: root.path)?.compactMap { $0 as? String } ?? []
        return try paths.filter { $0.hasSuffix(".swift") && !$0.contains("Localization/") }.sorted().map { path in
            (dir + "/" + path, try String(contentsOf: root.appendingPathComponent(path), encoding: .utf8))
        }
    }
}

private let trKey = try! NSRegularExpression(pattern: #"\btr\((?:in: [^,]+, )?"((?:[^"\\]|\\.)*)""#)
private let trCall = try! NSRegularExpression(pattern: #"\btr\("#)
private let literal = try! NSRegularExpression(pattern: #""((?:[^"\\\n]|\\.)*)""#)

private func matches(_ regex: NSRegularExpression, _ text: String) -> [(range: NSRange, group: String)] {
    regex.matches(in: text, range: NSRange(text.startIndex..., in: text)).compactMap { match in
        Range(match.range(at: 1), in: text).map { (match.range, String(text[$0])) }
    }
}

private func hasChinese(_ text: String) -> Bool {
    text.unicodeScalars.contains { (0x4E00...0x9FFF).contains($0.value) || (0x3000...0x303F).contains($0.value)
        || (0xFF00...0xFFEF).contains($0.value) }
}

private func placeholders(_ text: String) -> Set<String> {
    Set(matches(try! NSRegularExpression(pattern: #"(\{\d\})"#), text).map(\.group))
}

/// The tr( calls on a line whose key the scan cannot read (a variable, a key on the next line): no English
/// line is checked for them.
private func unreadable(_ line: String) -> Int {
    let code = line.trimmingCharacters(in: .whitespaces).hasPrefix("//") ? "" : line
    let all = NSRange(code.startIndex..., in: code)
    return trCall.numberOfMatches(in: code, range: all) - trKey.numberOfMatches(in: code, range: all)
}

/// Chinese literals on a line that are not the key of a tr(…): shown untranslated.
private func bare(_ line: String) -> [String] {
    let code = line.trimmingCharacters(in: .whitespaces).hasPrefix("//") ? "" : line
    let keys = Set(matches(trKey, code).map(\.group))
    return matches(literal, code).map(\.group).filter { hasChinese($0) && !keys.contains($0) }
}

let l10nTests = Suite(name: "L10n", cases: [
    test("the Mac's first preferred language decides; Settings can pick one") {
        [
            expectEqual(Language.preferred(["zh-Hans-CN", "en-US"]), .zh),
            expectEqual(Language.preferred(["zh-Hant-TW"]), .zh),
            expectEqual(Language.preferred(["en-US", "zh-Hans-CN"]), .en),
            expectEqual(Language.preferred([]), .en),
            expectEqual(LanguageChoice.system.resolved(["zh-Hans"]), .zh),
            expectEqual(LanguageChoice.en.resolved(["zh-Hans"]), .en),
            expectEqual(LanguageChoice.zh.resolved(["en-US"]), .zh),
        ]
    },
    test("a key in each language, with its values in place") {
        [
            expectEqual(L10n.text("{0} 条待归档", [3], in: .zh), "3 条待归档"),
            expectEqual(L10n.text("{0} 条待归档", [3], in: .en), "3 waiting"),
            expectEqual(L10n.text("{0} 出错了（退出码 {1}）", ["cpa", 2], in: .en), "cpa failed (exit code 2)"),
            expectEqual(L10n.text("没有这个键", [], in: .en), "没有这个键", "an unknown key shows as written"),
        ]
    },
    test("every tr() in the app has an English line with the same placeholders") {
        let english = L10n.englishTable
        let keys = try appFiles(trDirs).flatMap { matches(trKey, $0.text).map(\.group) }
        return [expect(keys.count > 400, "\(keys.count) keys")] + keys.map { key in
            guard let line = english[key] else { return expect(false, "no English for \(key)") }
            return expect(placeholders(line) == placeholders(key) && !hasChinese(line.replacingOccurrences(of: "中文", with: "")),
                          "placeholders or Chinese in the English of \(key)")
        }
    },
    test("every tr( in the sources has its key written in place, so the English check above sees it") {
        [expect(true)] + (try appFiles(trDirs).flatMap { file in
            file.text.components(separatedBy: "\n").enumerated().compactMap { index, line in
                unreadable(line) == 0 ? nil : expect(false, "\(file.name):\(index + 1): tr( without its key in place")
            }
        })
    },
    test("the tests run in English unless a suite asks for Chinese") {
        [expectEqual(L10n.language, .en), expectEqual(formatTests.language, .zh)]
    },
    test("no Chinese literal is shown without tr") {
        [expect(true)] + (try appFiles().flatMap { file in
            file.text.components(separatedBy: "\n").enumerated().flatMap { index, line in
                bare(line).map { expect(false, "\(file.name):\(index + 1): \($0)") }
            }
        })
    },
])
