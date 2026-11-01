// A cpa and cpa-stt that answer from a World, the way the real ones print: the filing contract's
// documents, status and rec ls JSON, the MCP texts. A command it does not know exits 2, like argparse.

import CardputerCore
import Foundation

struct ShotClient: CpaClient {
    let world: World
    let store: ShotStore
    let dest: ShotDest

    init(world: World) {
        self.world = world
        self.store = ShotStore(rows: world.rows, counts: world.counts)
        self.dest = ShotDest(world: world)
    }

    func execute(_ command: CpaCommand) async throws -> CommandOutput {
        if command.tool == .cpaStt {
            return try stt(command.arguments)
        }
        guard world.cpa else { throw CpaError.notFound(.cpa) }
        let args = command.arguments
        switch Array(args.prefix(2)) {
        case ["--version"]: return text("cpa 0.1.0\n")
        case ["status", "--json"]: return json(world.statusDoc)
        case ["project", "ls"]: return world.projectList ? json(projectsDoc) : usage
        case ["project", "list"]: return world.projectEdit ? json(projectsDoc) : usage
        case _ where args.first == "project": return world.projectEdit ? json(projectsDoc) : usage
        case ["filing", "ls"]: return world.filing ? json(list(args)) : usage
        case ["filing", "show"]: return world.filing ? show(args[2]) : usage
        case ["filing", "file"], ["filing", "done"], ["filing", "hide"], ["filing", "hand"], ["filing", "reopen"]:
            return world.filing ? json(result(args)) : usage
        case ["rec", "ls"]: return json(recDoc(hidden: args.contains("--hidden")))
        case ["filing", "send"]: return world.dest ? dest.fileSend(args) : usage
        case _ where args.first == "dest" && args.count > 1: return world.dest ? dest.answer(args) : usage
        case ["host", "name"]: return world.filing ? json(["v": 1, "host": (world.hostName as Any?) ?? NSNull()]) : usage
        case ["mcp", "setup"]: return setup(args[2])
        case ["mcp", "tools"]: return CommandOutput(exitCode: 0, stdout: Fixtures.data("shots/mcp-tools.json"))
        case ["mcp", "audit"]: return CommandOutput(exitCode: 0, stdout: Fixtures.data("shots/mcp-audit.jsonl"))
        default: return usage
        }
    }

    var usage: CommandOutput {
        CommandOutput(exitCode: 2, stdout: Data())
    }

    /// Today's cpa: no --list (exit 2), the clients in its help, a snippet per client.
    func setup(_ client: String) -> CommandOutput {
        switch client {
        case "--list": return usage
        case "--help": return text("usage: cpa mcp setup [-h] {\(World.mcpClients.joined(separator: ","))}\n")
        default: return CommandOutput(exitCode: 0, stdout: Fixtures.data("shots/mcp/\(client).txt"))
        }
    }

    func text(_ value: String) -> CommandOutput {
        CommandOutput(exitCode: 0, stdout: Data(value.utf8))
    }

    func json(_ object: Any) -> CommandOutput {
        CommandOutput(exitCode: 0, stdout: (try? JSONSerialization.data(withJSONObject: object)) ?? Data())
    }

    var projectsDoc: [String: Any] {
        ["v": 1, "projects": Row.projects.map {
            ["key": $0.key, "name": $0.name, "slot": $0.slot, "ok": $0.ok, "linked": $0.folder != nil,
             "folder": ($0.folder as Any?) ?? NSNull()]
        }]
    }

    func list(_ args: [String]) -> [String: Any] {
        let state = args.firstIndex(of: "--filing").map { args[$0 + 1] } ?? "new"
        let limit = args.firstIndex(of: "--limit").flatMap { Int(args[$0 + 1]) } ?? 1000
        let (all, counts) = store.snapshot()
        let rows = all.filter { state == "all" || $0.filing == state }
        return ["v": 1, "counts": counts, "next_cursor": NSNull(), "rows": rows.prefix(limit).map(\.document)]
    }

    func show(_ id: String) -> CommandOutput {
        guard let row = store.snapshot().rows.first(where: { $0.id == id }) else {
            return CommandOutput(exitCode: 1, stdout: Data(#"{"ok":false,"v":1,"error":"E_UNKNOWN_ID","field":"id"}"#.utf8))
        }
        return json(row.show)
    }

    func result(_ args: [String]) -> [String: Any] {
        let id = args[2]
        let filing = ["file": "filed", "done": "done", "hide": "hidden", "hand": "handed", "reopen": "new"][args[1]] ?? "new"
        let project = args[1] == "file" && args[3] != "--none" ? args[3] : nil
        let qid = args[1] == "hand" ? "q13" : nil
        store.write(id: id, filing: filing, project: project, queue: qid)
        let undo: Any = ["op": args[1] == "file" ? "file" : "reopen", "id": id, "args": ["project": NSNull()]]
        let queue: Any = qid.map { ["qid": $0, "intent": "spawn", "open": true] } ?? NSNull()
        return ["ok": true, "v": 1, "id": id, "filing": filing, "changed": true, "undo": undo, "queue": queue]
    }

    /// rec-ls.json, 36 days later; without --hidden the hidden ones are left out, as cpa does.
    func recDoc(hidden: Bool) -> [[String: Any]] {
        let rows = (try? JSONSerialization.jsonObject(with: Fixtures.data("cpa/rec-ls.json"))) as? [[String: Any]] ?? []
        return rows.filter { hidden || ($0["hidden"] as? Bool) != true }.map { row in
            guard let started = row["started_ms"] as? Int64 else { return row }
            return row.merging(["started_ms": started + Fixtures.shift]) { $1 }
        }
    }

    func stt(_ args: [String]) throws -> CommandOutput {
        guard world.stt != .missing else { throw CpaError.notFound(.cpaStt) }
        guard args == ["status", "--json"] else { return text(args == ["retry"] ? "removed 1 failed result(s)\n" : "") }
        let base = (try? JSONSerialization.jsonObject(with: Fixtures.data("cpa/stt-status.json"))) as? [String: Any] ?? [:]
        let notes = world.notesPath + "/recordings"
        let changes: [String: Any] = switch world.stt {
        case .on: ["recordings": notes]
        case .off: ["on": false, "recordings": notes, "service": false]
        case .needsModel: ["on": false, "recordings": notes, "blocked": "model_missing", "missing": ["model"],
                           "selftest_ok": false, "service": false]
        case .missing: [:]
        }
        return json(base.merging(changes) { $1 })
    }
}

/// What the made-up cpa has written so far: a write changes the row, as cpa filing would.
final class ShotStore: @unchecked Sendable {
    private let lock = NSLock()
    private var rows: [Row]
    private var counts: [String: Any?]

    init(rows: [Row], counts: [String: Any?]) {
        self.rows = rows
        self.counts = counts
    }

    func snapshot() -> (rows: [Row], counts: [String: Any]) {
        lock.lock()
        defer { lock.unlock() }
        return (rows, counts.mapValues { $0 ?? NSNull() })
    }

    func write(id: String, filing: String, project: String?, queue: String?) {
        lock.lock()
        defer { lock.unlock() }
        guard let index = rows.firstIndex(where: { $0.id == id }) else { return }
        let before = rows[index]
        let key = before.asked == nil ? "new" : "other_mac"
        if before.filing == "new", filing != "new", let count = counts[key] as? Int {
            counts[key] = count - 1
        }
        rows[index] = before.changed(filing: filing, project: project ?? before.project, queue: queue)
    }
}

extension Row {
    func changed(filing: String, project: String?, queue: String?) -> Row {
        var copy = self
        copy.filing = filing
        copy.project = project
        copy.queue = queue ?? self.queue
        copy.asked = filing == "new" ? asked : nil
        return copy
    }
}
