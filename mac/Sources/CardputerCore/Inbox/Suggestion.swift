// A stopgap for "suggest a project, I click Approve" (docs/24 §2.1, gap G3): until a local model's
// suggestion arrives in the public interface, a new memo whose words name exactly one of this Mac's
// projects (its name or key) has that project suggested; Enter approves it. A word match, said as one.

import Foundation

public enum Suggestion {
    /// Names shorter than this hit too much (a one-character Chinese name such as "home").
    static let shortest = 2

    /// The one project the entry's words name, or nil (none, several, or not a new row of this Mac).
    public static func of(_ entry: InboxEntry, projects: ProjectMap) -> ProjectSlot? {
        guard entry.filing == .new, entry.project == nil, !entry.needsConfirmation else { return nil }
        let words = [entry.title, entry.text].compactMap { $0 }.joined(separator: " ").lowercased()
        guard !words.isEmpty else { return nil }
        let named = projects.slots.filter { slot in
            [slot.name, slot.key].contains { term in mentions(words, term.lowercased()) }
        }
        return named.count == 1 ? named.first : nil
    }

    public static func line(_ slot: ProjectSlot) -> String {
        tr("建议：{0}（文字里提到了）· 按 Enter 批准", slot.label)
    }

    /// A Latin term only as a whole word; any other term anywhere (Chinese has no spaces).
    static func mentions(_ words: String, _ term: String) -> Bool {
        guard term.count >= shortest else { return false }
        guard term.allSatisfy(\.isASCII) else { return words.contains(term) }
        var start = words.startIndex
        while let found = words.range(of: term, range: start..<words.endIndex) {
            let before = found.lowerBound == words.startIndex ? nil : words[words.index(before: found.lowerBound)]
            let after = found.upperBound == words.endIndex ? nil : words[found.upperBound]
            if !isWordCharacter(before), !isWordCharacter(after) {
                return true
            }
            start = found.upperBound
        }
        return false
    }

    static func isWordCharacter(_ character: Character?) -> Bool {
        guard let character, character.isASCII else { return false }
        return character.isLetter || character.isNumber || character == "-" || character == "_"
    }
}
