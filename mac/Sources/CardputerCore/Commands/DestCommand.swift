// The cpa dest commands the app runs (docs/25 §5.4, §5.5), as argument arrays. argv carries only recording
// ids, destination ids, preset names, project keys and, for `cpa dest map`, the link of a bot the user
// pasted (an address, never a memo's text: cpa reads that itself). Sends and drops say they were chosen in
// the app (--by app). `cpa dest add` runs with --yes only after the app's own confirmation showed cpa's note.

import Foundation

extension CpaCommand {
    public static let destList = CpaCommand(.cpa, ["dest", "ls", "--json"])
    /// LaunchServices looks each preset's app up: a little slower than a read of a file.
    public static let destPresets = CpaCommand(.cpa, ["dest", "presets", "--installed", "--json"], timeout: 10)
    public static let destPending = CpaCommand(.cpa, ["dest", "pending", "--json"])
    public static let destUsual = CpaCommand(.cpa, ["dest", "usual", "--json"])

    /// Send a waiting row: to its preselected destination (nil), or to another one. again: once more,
    /// although it was already sent there (cpa answers E_ALREADY without it).
    public static func destSend(_ ref: String, to dest: String?, again: Bool = false) throws -> CpaCommand {
        let target = try dest.map { [try checkedDest($0)] } ?? []
        return destWrite(["send", try checkedRef(ref)] + target + (again ? ["--again"] : []) + byApp)
    }

    /// Send any memo of the Inbox to a destination (origin mac).
    public static func filingSend(_ id: String, to dest: String, again: Bool = false) throws -> CpaCommand {
        let more = (again ? ["--again"] : []) + byApp + ["--json"]
        return CpaCommand(.cpa, ["filing", "send", try checkedID(id), try checkedDest(dest)] + more, timeout: 30)
    }

    /// The same send once more: what Send again and Copy again run.
    public func again() -> CpaCommand? {
        let isSend = ["dest", "filing"].contains(arguments.first ?? "") && arguments.dropFirst().first == "send"
        guard isSend, let at = arguments.firstIndex(of: "--by"), !arguments.contains("--again") else { return nil }
        var more = arguments
        more.insert("--again", at: at)
        return CpaCommand(tool, more, timeout: timeout)
    }

    /// cpa dest send REF DEST --again: the Copy again of a text that went to the clipboard.
    public static func sendAgain(_ doc: DestSent) -> CpaCommand? {
        try? destSend(doc.ref, to: doc.dest, again: true)
    }

    public static func destDrop(_ ref: String) throws -> CpaCommand {
        destWrite(["drop", try checkedRef(ref)] + byApp)
    }

    /// A project's usual destination; nil forgets it.
    public static func setUsual(_ project: String, to dest: String?) throws -> CpaCommand {
        destWrite(["usual", try checkedKey(project), try dest.map(checkedDest) ?? "-"])
    }

    /// link: the chat to fill for every project, for a preset that takes one (nil: none yet).
    public static func destAdd(preset: String, link: String? = nil) throws -> CpaCommand {
        guard PresetList.isPresetName(preset) else { throw CommandError.invalid(argument: "preset") }
        let map = try link.map { ["--map", "*=" + (try checkedLink($0))] } ?? []
        return CpaCommand(.cpa, ["dest", "add", "--preset", preset, "--yes"] + map, timeout: 30)
    }

    /// Hand one marked test text to the app; asks nothing (the app asks).
    public static func destTest(_ dest: String) throws -> CpaCommand {
        CpaCommand(.cpa, ["dest", "test", try checkedDest(dest), "--send", "--json"], timeout: 30)
    }

    /// This Mac's answer to the test: it appeared and was not sent (draft), or it was sent by itself (act).
    public static func destVerify(_ dest: String, draft: Bool) throws -> CpaCommand {
        CpaCommand(.cpa, ["dest", "verify", try checkedDest(dest), draft ? "--draft" : "--act"], timeout: 30)
    }

    /// The bot to fill for every project: its link or id, as the user pasted it (cpa checks it against the
    /// preset's pattern).
    public static func destMap(_ dest: String, link: String) throws -> CpaCommand {
        CpaCommand(.cpa, ["dest", "map", try checkedDest(dest), "*=" + (try checkedLink(link))], timeout: 30)
    }

    /// A pasted link or id: printable ASCII, no leading dash, at most 512 characters, trimmed.
    static func checkedLink(_ link: String) throws -> String {
        let value = link.trimmingCharacters(in: .whitespacesAndNewlines)
        let printable = value.unicodeScalars.allSatisfy { (0x21...0x7e).contains($0.value) }
        guard (1...512).contains(value.count), printable, !value.hasPrefix("-") else {
            throw CommandError.invalid(argument: "link")
        }
        return value
    }

    static let byApp = ["--by", "app"]

    static func destWrite(_ arguments: [String]) -> CpaCommand {
        CpaCommand(.cpa, ["dest"] + arguments + ["--json"], timeout: 30)
    }

    static func checkedRef(_ ref: String) throws -> String {
        guard DestPatterns.isRef(ref) else { throw CommandError.invalid(argument: "ref") }
        return ref
    }

    static func checkedDest(_ dest: String) throws -> String {
        guard DestPatterns.isDestID(dest) else { throw CommandError.invalid(argument: "dest") }
        return dest
    }
}
