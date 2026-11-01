// display_safe() of the filing contract (protocol/filing/v1, docs/23 §3.3 rule 7), applied once more to
// every text a person sees: C0, DEL, C1 (U+0080-009F) and U+2028 / U+2029 become spaces, so a text is one
// line and no terminal escape survives; bidirectional controls (U+202A-202E, U+2066-2069) are dropped.

import Foundation

public enum DisplaySafe {
    static func becomesSpace(_ value: UInt32) -> Bool {
        value <= 0x1F || (0x7F...0x9F).contains(value) || value == 0x2028 || value == 0x2029
    }

    static func isBidi(_ value: UInt32) -> Bool {
        (0x202A...0x202E).contains(value) || (0x2066...0x2069).contains(value)
    }

    public static func clean(_ text: String) -> String {
        let scalars = text.unicodeScalars
            .filter { !isBidi($0.value) }
            .map { becomesSpace($0.value) ? " " : $0 }
        return String(String.UnicodeScalarView(scalars))
    }

    /// True when clean() would change nothing (the contract's safeText pattern).
    public static func isSafe(_ text: String) -> Bool {
        !text.unicodeScalars.contains { becomesSpace($0.value) || isBidi($0.value) }
    }

    /// clean(), trimmed and cut to `limit` characters; nil when nothing is left.
    public static func excerpt(_ text: String?, limit: Int) -> String? {
        guard let text else { return nil }
        let cut = String(trimmed(clean(text)).prefix(limit))
        let result = trimmed(cut)
        return result.isEmpty ? nil : result
    }

    static func trimmed(_ text: String) -> String {
        text.trimmingCharacters(in: .whitespacesAndNewlines)
    }
}
