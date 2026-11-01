// The two documents the Destinations card reads that are not in a versioned contract (docs/25 §5.5):
// `cpa dest ls --json` (the destinations, their letters and states) and `cpa dest presets --installed --json`
// (the apps on this Mac a destination can be added for; each preset is data shipped with cpa, never code
// here). Read leniently: a row whose id is not a destination id is left out, every text is made display-safe.

import Foundation

public struct DestEntry: Hashable, Sendable, Identifiable {
    public let id: String
    public let kind: String
    public let label: String
    /// The letter (a-z) that sends to it, nil when none.
    public let key: String?
    public let slot: Int
    /// on, off, needs_optin or unavailable (an unknown one is shown as it is).
    public let state: String
    public let leavesMac: Bool
    /// nil when cpa did not say: read as lck.
    public let fl: String?
    /// url only: draft (it may become a draft) or act.
    public let effect: String?
    /// url only: what this Mac's one-time test answered (draft, act), nil before it.
    public let verified: String?
    public let preset: String?
    /// url: its preset takes a chat's link or id (cpa dest map).
    public let takesMap: Bool

    public init(id: String, kind: String, label: String, key: String?, slot: Int, state: String, leavesMac: Bool,
                fl: String?, effect: String?, verified: String?, preset: String?, takesMap: Bool = false) {
        self.id = id
        self.kind = kind
        self.label = label
        self.key = key
        self.slot = slot
        self.state = state
        self.leavesMac = leavesMac
        self.fl = fl
        self.effect = effect
        self.verified = verified
        self.preset = preset
        self.takesMap = takesMap
    }

    public var isOn: Bool {
        state == "on"
    }
}

public struct DestCatalog: Hashable, Sendable {
    /// Why destinations.json cannot be used; nil when it can.
    public let problem: String?
    public let dests: [DestEntry]

    public static let empty = DestCatalog(problem: nil, dests: [])

    public init(problem: String?, dests: [DestEntry]) {
        self.problem = problem
        self.dests = dests
    }

    public static func decode(_ data: Data) throws -> DestCatalog {
        let doc = try Lenient.object(data)
        let rows = (doc["dests"] as? [[String: Any]] ?? []).compactMap(entry)
        return DestCatalog(problem: Lenient.text(doc["problem"], limit: 120), dests: rows)
    }

    static func entry(_ row: [String: Any]) -> DestEntry? {
        guard let id = row["id"] as? String, DestPatterns.isDestID(id) else { return nil }
        let kind = String(id.split(separator: ".").first ?? "")
        let fl = (row["fl"] as? String).flatMap { DestPatterns.isFlags($0) ? $0 : nil }
        return DestEntry(id: id, kind: kind, label: Lenient.text(row["label"], limit: 24) ?? id, key: Lenient.letter(row["key"]),
                         slot: row["slot"] as? Int ?? 0, state: Lenient.token(row["state"]) ?? "unavailable",
                         leavesMac: row["leaves_mac"] as? Bool ?? true, fl: fl, effect: Lenient.token(row["effect"]),
                         verified: Lenient.token(row["verified"]), preset: Lenient.token(row["preset"], dash: true),
                         takesMap: row["takes_map"] as? Bool ?? false)
    }
}

/// One preset whose app is installed: what the card offers to add.
public struct PresetEntry: Hashable, Sendable, Identifiable {
    public let preset: String
    public let label: String
    /// The destination it adds: <kind>.<name>.
    public let destID: String
    public let kind: String
    public let keyHint: String?
    public let leavesMac: Bool
    public let mayBeDraft: Bool
    public let version: String?
    /// cpa's own words on what is sent where: the confirmation shows them.
    public let note: String
    /// url: it takes a chat's link or id, asked for before the first test (cpa dest add --map).
    public var takesMap = false

    public var id: String {
        preset
    }
}

public struct PresetList: Hashable, Sendable {
    public let presets: [PresetEntry]
    public let problems: [String]

    public static let empty = PresetList(presets: [], problems: [])

    public static func decode(_ data: Data) throws -> PresetList {
        let doc = try Lenient.object(data)
        let rows = (doc["presets"] as? [[String: Any]] ?? []).compactMap(entry)
        let problems = (doc["problems"] as? [Any] ?? []).compactMap { Lenient.text($0, limit: 160) }
        return PresetList(presets: rows, problems: problems)
    }

    /// ^[a-z][a-z0-9-]{1,23}$ (protocol/dest-preset/v1).
    public static func isPresetName(_ text: String) -> Bool {
        guard let first = text.first, (2...24).contains(text.count), DestPatterns.lower.contains(first) else { return false }
        return text.allSatisfy { Patterns.lowerDigits.contains($0) || $0 == "-" }
    }

    static func entry(_ row: [String: Any]) -> PresetEntry? {
        guard let preset = row["preset"] as? String, isPresetName(preset), let id = row["id"] as? String,
              DestPatterns.isDestID(id), row["installed"] as? Bool != false else { return nil }
        return PresetEntry(preset: preset, label: Lenient.text(row["label"], limit: 24) ?? preset, destID: id,
                           kind: Lenient.token(row["kind"]) ?? "", keyHint: Lenient.letter(row["key_hint"]),
                           leavesMac: row["leaves_mac"] as? Bool ?? true, mayBeDraft: row["may_be_draft"] as? Bool ?? false,
                           version: Lenient.text(row["version"], limit: 24), note: Lenient.text(row["note"], limit: 280) ?? "",
                           takesMap: row["takes_map"] as? Bool ?? false)
    }
}

/// Reading a JSON object field by field without trusting it.
enum Lenient {
    static func object(_ data: Data) throws -> [String: Any] {
        guard let doc = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw ContractError.invalid(field: "(root)")
        }
        return doc
    }

    static func text(_ value: Any?, limit: Int) -> String? {
        DisplaySafe.excerpt(value as? String, limit: limit)
    }

    /// One letter a-z, in lower case.
    static func letter(_ value: Any?) -> String? {
        guard let text = value as? String, text.count == 1, let char = text.first, char.isASCII, char.isLetter else { return nil }
        return text.lowercased()
    }

    /// A short lower-case word (state, effect); with dash, a preset name too.
    static func token(_ value: Any?, dash: Bool = false) -> String? {
        guard let text = value as? String else { return nil }
        return Patterns.isToken(text) || (dash && PresetList.isPresetName(text)) ? text : nil
    }
}
