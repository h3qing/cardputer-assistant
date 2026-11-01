// The contracts' string patterns, checked by hand (ECMA-262 semantics: the whole string, no trailing newline).

public enum Patterns {
    static let crockford = Set("0123456789ABCDEFGHJKMNPQRSTVWXYZ")
    static let lowerDigits = Set("abcdefghijklmnopqrstuvwxyz0123456789")
    static let hexLower = Set("0123456789abcdef")
    static let digits = Set("0123456789")

    /// A recording session id, EEEEEEE-BBBBB-SSSS in upper-case Crockford base32.
    public static func isMonoID(_ text: String) -> Bool {
        let parts = text.split(separator: "-", omittingEmptySubsequences: false)
        return parts.map(\.count) == [7, 5, 4] && parts.allSatisfy { $0.allSatisfy(crockford.contains) }
    }

    /// A project key: ^[a-z0-9][a-z0-9-]{0,11}$
    public static func isProjectKey(_ text: String) -> Bool {
        guard let first = text.first, (1...12).contains(text.count) else { return false }
        return lowerDigits.contains(first) && text.allSatisfy { lowerDigits.contains($0) || $0 == "-" }
    }

    /// An extensible enum value or an error's field: ^[a-z][a-z0-9_]{0,15}$
    public static func isToken(_ text: String) -> Bool {
        guard let first = text.first, (1...16).contains(text.count) else { return false }
        return first.isASCII && first.isLowercase && text.allSatisfy { lowerDigits.contains($0) || $0 == "_" }
    }

    /// A row's device: 12 lower-case hex digits (an HMAC, never the device's own id).
    public static func isDeviceID(_ text: String) -> Bool {
        text.count == 12 && text.allSatisfy(hexLower.contains)
    }

    /// A queue item id: q and 1-10 digits.
    public static func isQueueID(_ text: String) -> Bool {
        text.hasPrefix("q") && (2...11).contains(text.count) && text.dropFirst().allSatisfy(digits.contains)
    }

    /// An audio file name: seg-NNN.wav or seg-NNNN.wav, no directory.
    public static func isSegmentName(_ text: String) -> Bool {
        guard text.hasPrefix("seg-"), text.hasSuffix(".wav") else { return false }
        let number = text.dropFirst(4).dropLast(4)
        return (3...4).contains(number.count) && number.allSatisfy(digits.contains)
    }

    /// next_cursor: 1-512 of [A-Za-z0-9_-].
    public static func isCursor(_ text: String) -> Bool {
        (1...512).contains(text.count) && text.allSatisfy { ($0.isASCII && ($0.isLetter || $0.isNumber)) || $0 == "_" || $0 == "-" }
    }

    /// A date for --from / --to: YYYY-MM-DD.
    public static func isDate(_ text: String) -> Bool {
        let parts = text.split(separator: "-", omittingEmptySubsequences: false)
        return parts.map(\.count) == [4, 2, 2] && parts.allSatisfy { $0.allSatisfy(digits.contains) }
    }

    /// A display_safe text of `range` code points.
    public static func isSafeText(_ text: String, _ range: ClosedRange<Int>) -> Bool {
        range.contains(text.unicodeScalars.count) && DisplaySafe.isSafe(text)
    }
}
