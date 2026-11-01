// Every command the app runs, as an argument array (never a shell line). argv carries only ids, project
// keys, enum values and absolute paths, each checked here before anything runs; text the user typed goes
// through stdin (docs/24 §5.2, protocol/filing/v1 write rule 1).

import Foundation

public enum Tool: String, Hashable, Sendable {
    case cpa
    case cpaStt = "cpa-stt"
}

public enum CommandError: Error, Hashable, Sendable {
    /// The named argument is not an id, key, enum value or path this command takes.
    case invalid(argument: String)
    /// An undo whose op this build does not know: shown as “This step cannot be undone”.
    case unsupportedUndo
}

public struct CpaCommand: Hashable, Sendable {
    public let tool: Tool
    public let arguments: [String]
    /// Bytes for the command's stdin; nil means stdin is closed.
    public let stdin: Data?
    /// Seconds before it is stopped (SIGTERM, then SIGKILL a second later).
    public let timeout: TimeInterval
    /// stdout past this many bytes stops the command.
    public let outputCap: Int

    public static let defaultCap = 4 << 20

    init(_ tool: Tool, _ arguments: [String], stdin: Data? = nil, timeout: TimeInterval = 5, outputCap: Int = defaultCap) {
        self.tool = tool
        self.arguments = arguments
        self.stdin = stdin
        self.timeout = timeout
        self.outputCap = outputCap
    }

    /// The subcommand alone ("filing ls"), for logs: never an id, a key or text.
    public var name: String {
        let words = arguments.prefix(2).filter { word in
            word.first?.isLowercase == true && word.allSatisfy { $0.isASCII && ($0.isLowercase || $0 == "-") }
        }
        return ([tool.rawValue] + words).joined(separator: " ")
    }
}

extension CpaCommand {
    public static let status = CpaCommand(.cpa, ["status", "--json"])
    public static let version = CpaCommand(.cpa, ["--version"])
    public static let projects = CpaCommand(.cpa, ["project", "ls", "--json"])
    public static let mcpTools = CpaCommand(.cpa, ["mcp", "tools", "--json"])
    public static let sttStatus = CpaCommand(.cpaStt, ["status", "--json"])
    public static let sttUse = CpaCommand(.cpaStt, ["use"], timeout: 120)
    public static let sttOff = CpaCommand(.cpaStt, ["off"], timeout: 30)
    public static let sttServiceInstall = CpaCommand(.cpaStt, ["service", "install"], timeout: 30)
    /// Unblock and redo cpa-stt's own failed recordings.
    public static let sttRetry = CpaCommand(.cpaStt, ["retry"], timeout: 30)
    /// This Mac's name: the filing contract's host document.
    public static let hostShow = CpaCommand(.cpa, ["host", "name", "--json"])
    /// The MCP clients `cpa mcp setup` knows (G8; a cpa without it exits 2).
    public static let mcpSetupList = CpaCommand(.cpa, ["mcp", "setup", "--list", "--json"])
    /// Before --list: argparse's help names the clients it takes.
    public static let mcpSetupHelp = CpaCommand(.cpa, ["mcp", "setup", "--help"])

    /// cpa rec ls has no limit option: a large archive is a long list.
    public static func recordings(hidden: Bool) -> CpaCommand {
        CpaCommand(.cpa, ["rec", "ls", "--json"] + (hidden ? ["--hidden"] : []), outputCap: 32 << 20)
    }

    public static func unhide(_ sid: String) throws -> CpaCommand {
        CpaCommand(.cpa, ["rec", "unhide", try checkedID(sid)])
    }

    public static func filingShow(_ id: String) throws -> CpaCommand {
        CpaCommand(.cpa, ["filing", "show", try checkedID(id), "--json"])
    }

    /// File under one of this Mac's projects, or under none; --other for the other-Mac group.
    public static func file(_ id: String, project: String?, other: Bool) throws -> CpaCommand {
        let target = try project.map(checkedKey) ?? "--none"
        return write(["file", try checkedID(id), target] + (other ? ["--other"] : []))
    }

    public static func done(_ id: String) throws -> CpaCommand {
        write(["done", try checkedID(id)])
    }

    public static func hide(_ id: String) throws -> CpaCommand {
        write(["hide", try checkedID(id)])
    }

    public static func reopen(_ id: String) throws -> CpaCommand {
        write(["reopen", try checkedID(id)])
    }

    /// Queue a memo for AI (its own text, taken by cpa); a session starts only from the device board.
    public static func hand(_ id: String, project: String?, spawn: Bool, other: Bool) throws -> CpaCommand {
        let key = try project.map { [try checkedKey($0)] } ?? []
        return write(["hand", try checkedID(id)] + key + (spawn ? ["--spawn"] : []) + (other ? ["--other"] : []))
    }

    /// A result's undo, rebuilt with this app's own verbs; the id is checked again.
    public static func undo(_ step: UndoStep) throws -> CpaCommand {
        switch step.op.known {
        case .file: return try file(step.id, project: step.project, other: false)
        case .reopen: return try reopen(step.id)
        case .done: return try done(step.id)
        case .hide: return try hide(step.id)
        case nil: throw CommandError.unsupportedUndo
        }
    }

    /// `cpa mcp setup CLIENT`: a client id as `cpa mcp setup --list` gives it.
    public static func mcpSetup(_ client: String) throws -> CpaCommand {
        guard McpClients.isID(client) else { throw CommandError.invalid(argument: "client") }
        return CpaCommand(.cpa, ["mcp", "setup", client])
    }

    public static func mcpAudit(last: Int) throws -> CpaCommand {
        guard (1...1000).contains(last) else { throw CommandError.invalid(argument: "last") }
        return CpaCommand(.cpa, ["mcp", "audit", "--last", String(last), "--json"])
    }

    /// `cpa export DEST [--zip] [--no-audio]`: a new folder or zip at an absolute path.
    public static func export(to path: String, zip: Bool, audio: Bool) throws -> CpaCommand {
        let flags = (zip ? ["--zip"] : []) + (audio ? [] : ["--no-audio"])
        return CpaCommand(.cpa, ["export"] + flags + [try checkedPath(path, "dest")], timeout: 600)
    }

    /// Link an existing model file for cpa-stt (no copy, no download).
    public static func sttSetup(model: String) throws -> CpaCommand {
        CpaCommand(.cpaStt, ["setup", "--yes", try checkedPath(model, "model")], timeout: 120)
    }

    /// A filing write: cpa may wait on its events lock, so 30 s.
    static func write(_ arguments: [String]) -> CpaCommand {
        CpaCommand(.cpa, ["filing"] + arguments + ["--json"], timeout: 30)
    }

    /// A MonoId, taken in any case and sent in upper case.
    static func checkedID(_ id: String) throws -> String {
        let upper = id.uppercased()
        guard Patterns.isMonoID(upper) else { throw CommandError.invalid(argument: "id") }
        return upper
    }

    static func checkedKey(_ key: String) throws -> String {
        guard Patterns.isProjectKey(key) else { throw CommandError.invalid(argument: "key") }
        return key
    }

    static func checkedPath(_ path: String, _ name: String) throws -> String {
        let clean = path.hasPrefix("/") && DisplaySafe.isSafe(path) && !path.split(separator: "/").contains("..")
        guard clean else { throw CommandError.invalid(argument: name) }
        return path
    }
}
