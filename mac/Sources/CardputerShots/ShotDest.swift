// The made-up cpa dest (docs/25 §5.4, §5.5): its lists come from Fixtures/dest, and a send, a drop or a
// test answers the way the real one prints them (protocol/dest-cli/v1). A send to a link hands it over
// (or, with World.destClip, puts it on the clipboard because the bot has a draft); a sent row leaves Waiting to
// send.

import CardputerCore
import Foundation

final class DestStore: @unchecked Sendable {
    private let lock = NSLock()
    private var pending: [[String: Any]]

    init(pending: [[String: Any]]) {
        self.pending = pending
    }

    func rows() -> [[String: Any]] {
        lock.lock()
        defer { lock.unlock() }
        return pending
    }

    /// The row's preselected destination, and the row leaves Waiting to send.
    func take(_ ref: String) -> String? {
        lock.lock()
        defer { lock.unlock() }
        let dest = pending.first { $0["ref"] as? String == ref }?["dest"] as? String
        pending.removeAll { $0["ref"] as? String == ref }
        return dest
    }
}

struct ShotDest {
    let world: World
    let store: DestStore

    init(world: World) {
        self.world = world
        let doc = Fixtures.json("dest/pending.json")
        self.store = DestStore(pending: world.destPending ? doc["rows"] as? [[String: Any]] ?? [] : [])
    }

    func answer(_ args: [String]) -> CommandOutput {
        switch args[1] {
        case "ls": return file(world.destList)
        case "presets": return file("dest/presets.json")
        case "pending": return json(["ok": true, "v": 1, "rows": store.rows()])
        case "usual": return file("dest/usual.json")
        case "send": return send(ref: args[2], dest: args[3].hasPrefix("--") ? nil : args[3])
        case "drop": return dropped(args[2])
        case "test": return json(["ok": true, "v": 1, "dest": args[2], "marker": "4821", "handed": true, "via": "url",
                                  "code": NSNull()])
        case "verify", "map", "add": return CommandOutput(exitCode: 0, stdout: Data("ok\n".utf8))
        default: return CommandOutput(exitCode: 2, stdout: Data())
        }
    }

    /// cpa filing send ID DEST
    func fileSend(_ args: [String]) -> CommandOutput {
        sent(ref: args[2], dest: args[3])
    }

    func send(ref: String, dest: String?) -> CommandOutput {
        let preselected = store.take(ref)
        return sent(ref: ref, dest: dest ?? preselected ?? "clip")
    }

    func sent(ref: String, dest: String) -> CommandOutput {
        let row = (Fixtures.json(world.destList)["dests"] as? [[String: Any]] ?? []).first { $0["id"] as? String == dest }
        let kind = row?["kind"] as? String ?? "clip"
        let handed = ["url", "app"].contains(kind)
        let via: Any = handed ? (world.destClip || kind == "app" ? "clip" : "url") : NSNull()
        let why: Any = handed && world.destClip ? "draft_busy" : NSNull()
        return json(["ok": true, "v": 1, "ref": ref, "dest": dest, "label": row?["label"] ?? dest, "kind": kind,
                     "outcome": handed ? "handed" : "delivered", "via": via, "why": why, "code": NSNull(),
                     "leaves_mac": row?["leaves_mac"] ?? false])
    }

    func dropped(_ ref: String) -> CommandOutput {
        let dest = store.take(ref) ?? "clip"
        return json(["ok": true, "v": 1, "ref": ref, "dest": dest])
    }

    func file(_ path: String) -> CommandOutput {
        CommandOutput(exitCode: 0, stdout: Fixtures.data(path))
    }

    func json(_ object: Any) -> CommandOutput {
        CommandOutput(exitCode: 0, stdout: (try? JSONSerialization.data(withJSONObject: object)) ?? Data())
    }
}
