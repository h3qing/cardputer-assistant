// Reads the documents of protocol/dest-cli/v1 the way check_dest_cli does: v the integer 1 (else E_VERSION),
// ok the expected boolean, then the schema and its conditional rules, naming the path of the first fault
// (rows[0].dest). Unknown fields are ignored; unknown values of the extensible enums are kept (Open).

import Foundation

public enum DestContract {
    public static func pending(_ data: Data) throws -> PendingList {
        let doc: PendingList = try FilingContract.decode(data, versioned: true, ok: true)
        for (index, row) in doc.rows.enumerated() {
            try DestRules.row(row, at: "rows[\(index)]")
        }
        return doc
    }

    public static func sent(_ data: Data) throws -> DestSent {
        let doc: DestSent = try FilingContract.decode(data, versioned: true, ok: true)
        try DestRules.sent(doc)
        return doc
    }

    public static func dropped(_ data: Data) throws -> DestDropped {
        let doc: DestDropped = try FilingContract.decode(data, versioned: true, ok: true)
        try FilingRules.check(DestPatterns.isRef(doc.ref), "ref")
        try FilingRules.check(DestPatterns.isDestID(doc.dest), "dest")
        return doc
    }

    public static func usual(_ data: Data) throws -> UsualList {
        let doc: UsualList = try FilingContract.decode(data, versioned: true, ok: true)
        for (index, entry) in doc.usual.enumerated() {
            try FilingRules.check(Patterns.isProjectKey(entry.project), "usual[\(index)].project")
            try FilingRules.check(DestPatterns.isDestID(entry.dest), "usual[\(index)].dest")
        }
        return doc
    }

    public static func tested(_ data: Data) throws -> DestTested {
        let doc: DestTested = try FilingContract.decode(data, versioned: true, ok: true)
        try FilingRules.check(DestPatterns.isDestID(doc.dest), "dest")
        try FilingRules.check(doc.marker.count == 4 && doc.marker.allSatisfy(Patterns.digits.contains), "marker")
        try FilingRules.check(doc.via.map { Patterns.isToken($0.raw) } ?? true, "via")
        try FilingRules.check(doc.code.map(DestPatterns.isCode) ?? true, "code")
        try FilingRules.check(doc.handed ? doc.code == nil : doc.code != nil, "code")
        try FilingRules.check(doc.handed ? doc.via != nil : doc.via == nil, "via")
        return doc
    }

    public static func failure(_ data: Data) throws -> DestFailure {
        let doc: DestFailure = try FilingContract.decode(data, versioned: true, ok: false)
        try FilingRules.check(DestPatterns.isCode(doc.code), "error")
        try FilingRules.check(doc.field.map(Patterns.isToken) ?? true, "field")
        return doc
    }
}

enum DestRules {
    static func row(_ row: PendingRow, at base: String) throws {
        let at = { (key: String) in FilingRules.join(base, key) }
        try FilingRules.check(DestPatterns.isRef(row.ref), at("ref"))
        try FilingRules.check(Patterns.isToken(row.why.raw), at("why"))
        try FilingRules.check(DestPatterns.isDestID(row.dest), at("dest"))
        try FilingRules.check(DestPatterns.isLabel(row.label) || DestPatterns.isDestID(row.label), at("label"))
        try FilingRules.check(row.kind.map { DestPatterns.isKind($0.raw) } ?? true, at("kind"))
        try FilingRules.check(row.fl.map(DestPatterns.isFlags) ?? true, at("fl"))
        try FilingRules.check(row.why.known != .held || row.st != nil, at("st"))
        try FilingRules.check(row.code.map(DestPatterns.isCode) ?? true, at("code"))
        try FilingRules.check(row.st != .failed || row.code != nil, at("code"))
        try FilingRules.check(row.project.map(Patterns.isProjectKey) ?? true, at("project"))
        try FilingRules.check(Patterns.isToken(row.src.raw), at("src"))
        try FilingRules.check(DestPatterns.isTime(row.at), at("at"))
        try FilingRules.check(row.text.map { Patterns.isSafeText($0, 1...280) } ?? true, at("text"))
    }

    static func sent(_ doc: DestSent) throws {
        try FilingRules.check(DestPatterns.isRef(doc.ref), "ref")
        try FilingRules.check(DestPatterns.isDestID(doc.dest), "dest")
        try FilingRules.check(DestPatterns.isLabel(doc.label), "label")
        try FilingRules.check(DestPatterns.isKind(doc.kind.raw), "kind")
        try FilingRules.check(Patterns.isToken(doc.outcome.raw), "outcome")
        try FilingRules.check(doc.via.map { Patterns.isToken($0.raw) } ?? true, "via")
        try FilingRules.check(doc.why.map { Patterns.isToken($0.raw) } ?? true, "why")
        try FilingRules.check(doc.code.map(DestPatterns.isCode) ?? true, "code")
        switch doc.outcome.known {
        case .failed?:
            try FilingRules.check(doc.code != nil, "code")
            try FilingRules.check(doc.via == nil, "via")
        case .handed?:
            try FilingRules.check(doc.via != nil, "via")
            try FilingRules.check(doc.code == nil, "code")
        case .delivered?:
            try FilingRules.check(doc.via == nil, "via")
            try FilingRules.check(doc.code == nil, "code")
        case nil:
            break
        }
    }
}

/// The dest-cli contract's patterns (common.schema.json), checked by hand.
public enum DestPatterns {
    static let lower = Set("abcdefghijklmnopqrstuvwxyz")
    static let refChars = Set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-")
    static let codeChars = Set("ABCDEFGHIJKLMNOPQRSTUVWXYZ_")

    /// ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$: a recording id or another capture's id.
    public static func isRef(_ text: String) -> Bool {
        guard let first = text.first, (1...64).contains(text.count) else { return false }
        return first.isASCII && (first.isLetter || first.isNumber) && text.allSatisfy(refChars.contains)
    }

    /// ^[a-z]{3,7}(\.[a-z0-9][a-z0-9-]{0,11})?$: a kind, or <kind>.<name>.
    public static func isDestID(_ text: String) -> Bool {
        let parts = text.split(separator: ".", maxSplits: 1, omittingEmptySubsequences: false)
        guard isKind(String(parts[0])) else { return false }
        return parts.count == 1 || Patterns.isProjectKey(String(parts[1]))
    }

    /// ^[a-z]{3,7}$
    public static func isKind(_ text: String) -> Bool {
        (3...7).contains(text.count) && text.allSatisfy(lower.contains)
    }

    /// ^l?c?p?k?$: the flags in their order, each at most once.
    public static func isFlags(_ text: String) -> Bool {
        var rest = Substring(text)
        for flag in "lcpk" where rest.first == flag {
            rest = rest.dropFirst()
        }
        return rest.isEmpty
    }

    /// ^E_[A-Z_]{1,30}$
    public static func isCode(_ text: String) -> Bool {
        text.hasPrefix("E_") && (1...30).contains(text.count - 2) && text.dropFirst(2).allSatisfy(codeChars.contains)
    }

    /// 1-24 characters, display_safe.
    public static func isLabel(_ text: String) -> Bool {
        Patterns.isSafeText(text, 1...24)
    }

    /// ^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:.]+([+-][0-9]{2}:[0-9]{2}|Z)$
    public static func isTime(_ text: String) -> Bool {
        guard text.count > 11, Patterns.isDate(String(text.prefix(10))), text.dropFirst(10).first == "T" else { return false }
        let rest = text.dropFirst(11)
        let zone: Substring
        if rest.hasSuffix("Z") {
            zone = "Z"
        } else {
            zone = rest.suffix(6)
            let digits = zone.dropFirst().split(separator: ":", omittingEmptySubsequences: false)
            guard zone.count == 6, "+-".contains(zone.first ?? " "), digits.map(\.count) == [2, 2],
                  digits.allSatisfy({ $0.allSatisfy(Patterns.digits.contains) }) else { return false }
        }
        let clock = rest.dropLast(zone.count)
        return !clock.isEmpty && clock.allSatisfy { Patterns.digits.contains($0) || $0 == ":" || $0 == "." }
    }
}
