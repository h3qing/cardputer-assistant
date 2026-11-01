// `cpa project add|rename|move|folder|rm --json` (docs/25 §8): projects are named groups this Mac makes, the
// numbers 1-9 the device files voice under; a code folder is optional. argv carries keys, numbers and an
// absolute path; a name is text the user chose, so it goes last, after `--`, checked like `cpa host name`'s.

import Foundation

extension CpaCommand {
    /// `cpa project list` exists only in a cpa that edits projects: the feature probe.
    public static let projectsProbe = CpaCommand(.cpa, ["project", "list", "--json"])

    public static func projectAdd(name: String, slot: Int?) throws -> CpaCommand {
        let number = try slot.map { ["--slot", try checkedSlot($0)] } ?? []
        return projectWrite(["add"] + number, name: try checkedName(name))
    }

    public static func projectRename(_ key: String, name: String) throws -> CpaCommand {
        projectWrite(["rename", try checkedKey(key)], name: try checkedName(name))
    }

    /// The project already on `slot` takes this one's old number.
    public static func projectMove(_ key: String, to slot: Int) throws -> CpaCommand {
        projectWrite(["move", try checkedKey(key), try checkedSlot(slot)])
    }

    /// Link a code folder (an absolute path), or unlink with nil.
    public static func projectFolder(_ key: String, path: String?) throws -> CpaCommand {
        projectWrite(["folder", try checkedKey(key), try path.map { try checkedPath($0, "folder") } ?? "--none"])
    }

    /// Recordings keep the key; filing still shows it.
    public static func projectRemove(_ key: String) throws -> CpaCommand {
        projectWrite(["rm", try checkedKey(key)])
    }

    static func projectWrite(_ arguments: [String], name: String? = nil) -> CpaCommand {
        let verb = Array(arguments.prefix(1)), rest = Array(arguments.dropFirst())
        let tail = name.map { ["--", $0] } ?? []
        return CpaCommand(.cpa, ["project"] + verb + ["--json"] + rest + tail, timeout: 30)
    }

    static func checkedSlot(_ slot: Int) throws -> String {
        guard (1...9).contains(slot) else { throw CommandError.invalid(argument: "slot") }
        return String(slot)
    }

    static func checkedName(_ name: String) throws -> String {
        let clean = name.trimmingCharacters(in: .whitespaces)
        let fits = (1...hostNameBytes).contains(clean.utf8.count)
        guard fits, DisplaySafe.isSafe(clean), !clean.hasPrefix("-") else {
            throw CommandError.invalid(argument: "name")
        }
        return clean
    }
}

extension CpaClient {
    /// One project edit; the list after it.
    public func editProjects(_ command: CpaCommand) async throws -> ProjectList {
        try await decoded(command, FilingContract.projects)
    }
}
