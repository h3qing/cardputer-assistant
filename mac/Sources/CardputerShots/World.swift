// The made-up world a screen is drawn in: what cpa and cpa-stt would answer. Synthetic recordings (no real
// text or ids); the fixture files in mac/Fixtures for status, rec ls, the notes dir and the MCP outputs. The
// words are English; one project (读书) and one memo stay Chinese so the screens show CJK text too.

import CardputerCore
import Foundation

enum Fixtures {
    static let root = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        .appendingPathComponent("Fixtures")

    static func data(_ path: String) -> Data {
        (try? Data(contentsOf: root.appendingPathComponent(path))) ?? Data()
    }

    static func json(_ path: String) -> [String: Any] {
        ((try? JSONSerialization.jsonObject(with: data(path))) as? [String: Any]) ?? [:]
    }

    /// 2026-10-22 14:30 +08:00, a Thursday.
    static let now = Date(timeIntervalSince1970: 1_792_650_600)
    /// rec-ls.json and the notes dir are of 2026-09-16; shown 36 days later.
    static let shift: Int64 = 36 * 86_400_000

    static let calendar: Calendar = {
        var cal = Calendar(identifier: .gregorian)
        cal.timeZone = TimeZone(secondsFromGMT: 8 * 3600) ?? .current
        cal.firstWeekday = 2
        cal.locale = Locale(identifier: "zh_CN")
        return cal
    }()
}

enum SttWorld {
    case missing, on, off, needsModel
}

struct World {
    var cpa = true
    var filing = true
    var projectList = true
    /// cpa project add|rename|move|folder|rm (docs/25 §8): the Projects page can edit.
    var projectEdit = true
    var stt = SttWorld.missing
    var running = true
    var link = "ready"
    var rows = Row.many
    var counts: [String: Any?] = ["new": 11, "other_mac": 1, "clips_waiting": 0, "on_device": 2]
    var claim = true
    /// This Mac's name (cpa host name --json); the command comes with cpa filing.
    var hostName: String? = "Office"
    /// The scenario's own copy of Fixtures/notes.
    var notesPath = ""
    /// cpa dest (docs/25 R1) answers: Send to…, Waiting to send and the live Destinations card.
    var dest = false
    /// What cpa dest ls prints (Fixtures/dest).
    var destList = "dest/ls.json"
    /// Waiting to send has the two rows of Fixtures/dest/pending.json.
    var destPending = true
    /// A link send goes to the clipboard: the bot still has a draft (D10).
    var destClip = false

    /// The MCP clients the made-up cpa knows: one per snippet in Fixtures/shots/mcp.
    static var mcpClients: [String] {
        let dir = Fixtures.root.appendingPathComponent("shots/mcp")
        let names = (try? FileManager.default.contentsOfDirectory(atPath: dir.path)) ?? []
        return names.filter { $0.hasSuffix(".txt") }.map { String($0.dropLast(4)) }.sorted()
    }

    var statusDoc: [String: Any] {
        let base = Fixtures.json("cpa/status-running.json")
        let recordings = (base["recordings"] as? [String: Any] ?? [:]).merging(
            ["on_device": running ? 2 : NSNull(), "last_result_at": "2026-10-22T14:27:40.000+08:00"]) { $1 }
        let projects = Row.projects.map { ["key": $0.key, "slot": $0.slot, "ok": $0.ok] }
        let warnings = running ? [] : ["cpad is not running: cpa service install"]
        let merged = base.merging(["running": running, "link": running ? link : NSNull(), "recordings": recordings,
                                   "projects": projects, "warnings": warnings]) { $1 }
        // Without cpad, cpa status knows nothing of the device or the agent (status-offline.json).
        return running ? merged : merged.filter { !["device", "agent", "paused_s"].contains($0.key) }
    }
}

/// A filing row of the made-up inbox.
struct Row {
    let id: String
    var kind = "memo"
    var filing = "new"
    let minutesAgo: Int
    let seconds: Int
    var tx = "done"
    var title: String?
    var text: String?
    var project: String?
    var asked: String?
    var host: String? = "Office"
    var link = "here"
    var intent: String?
    var queue: String?
    var starred = false

    /// folder nil: a named group without a code folder (docs/25 §8).
    static let projects: [(key: String, name: String, slot: Int, ok: Bool, folder: String?)] = [
        ("acmeshop", "Acmeshop", 1, true, "acmeshop"), ("cardputer", "Cardputer", 2, true, "Cardputer-Assistant"),
        ("p3", "Thesis", 3, true, nil), ("p4", "Home", 4, true, nil), ("p5", "读书", 5, false, "reading-notes"),
    ]

    static let many: [Row] = [
        Row(id: "3QZK1M7-00042-0033", minutesAgo: 3, seconds: 33, title: "Send Alice the quote",
            text: "To-do: send Alice the quote by Friday, and ask about the delivery date.", intent: "todo"),
        Row(id: "3QZK1M7-00042-0034", minutesAgo: 18, seconds: 12, tx: "transcript",
            text: "Swap Mando's icon for the new one. Then check the number in the menu bar."),
        Row(id: "3QZK1M7-00045-0001", minutesAgo: 42, seconds: 24,
            text: "The acme API docs need an update. Send them over before next Monday.", asked: "acme", host: nil),
        Row(id: "3QZK1M7-00042-0010", minutesAgo: 65, seconds: 33, tx: "waiting"),
        Row(id: "3QZK1M7-00042-0039", kind: "meeting", minutesAgo: 130, seconds: 660,
            title: "Weekly: Cardputer release plan",
            text: "The Mac app gets done this week. Firmware v0.4.1 ships next Monday.", link: "unknown"),
        Row(id: "3QZK1M7-00042-0040", minutesAgo: 190, seconds: 8, tx: "processing"),
        Row(id: "3QZK1M7-00042-0041", minutesAgo: 300, seconds: 41,
            text: "Redraw the figures in chapter 3 of the thesis. The axis labels are too small.", starred: true),
        Row(id: "3QZK1M7-00042-0042", minutesAgo: 1040, seconds: 19, text: "买咖啡豆和猫粮。", host: "Home"),
        Row(id: "3QZK1M7-00042-0035", kind: "meeting", minutesAgo: 1730, seconds: 27, tx: "failed", host: "Home"),
        Row(id: "3QZK1M7-00042-0044", minutesAgo: 4400, seconds: 72,
            text: "Finish chapter 5 of Thinking, Fast and Slow. Note the anchoring examples."),
        Row(id: "3QZK1M7-00042-0045", kind: "meeting", minutesAgo: 5800, seconds: 1500, title: "1:1 with my advisor",
            text: "Clean the dataset first. Then run the control experiment.", link: "unknown"),
        Row(id: "3QZK1M7-00042-0046", minutesAgo: 8600, seconds: 15, tx: "stale"),
    ]

    static let handed: [Row] = [
        Row(id: "3QZK1M7-00042-0A1F", filing: "handed", minutesAgo: 95, seconds: 24, title: "Quote",
            text: "Send Alice the quote by Friday.", project: "cardputer", intent: "todo", queue: "q12"),
        Row(id: "3QZK1M7-00042-0A20", filing: "handed", minutesAgo: 2900, seconds: 31,
            text: "Finish the cpa filing contract tests, undo above all.", project: "cardputer", queue: "q9"),
    ]

    var wallMs: Int64 {
        Int64(Fixtures.now.timeIntervalSince1970 * 1000) - Int64(minutesAgo) * 60_000
    }

    var document: [String: Any] {
        let fields: [String: Any?] = [
            "id": id, "kind": kind, "filing": filing, "why": asked == nil ? ["unfiled"] : ["orphan"], "asked": asked,
            "at": ["wall_ms": wallMs, "tz_min": 480, "uncertain": tx == "stale"], "dur_ms": seconds * 1000,
            "device": "5c0e9a7b3d21", "env": ["host": host as Any? ?? NSNull(), "link": link], "prof": nil,
            "project": project, "starred": starred, "tx": tx, "title": title, "text": text, "intent": intent,
            "clips": nil, "queue": queue.map { ["qid": $0, "intent": "spawn", "open": true] },
        ]
        return fields.mapValues { $0 ?? NSNull() }
    }

    /// cpa filing show: the row, its sentences, a todo for the first, the audio the notes dir has.
    var show: [String: Any] {
        let sentences = Row.sentences(text ?? "").enumerated().map {
            ["t_s": Double($0.offset) * 3.2, "text": $0.element]
        }
        let todos = intent == "todo" ? [["n": 1, "text": title ?? "", "owner": "me", "due": "Friday", "t_s": 0.0,
                                         "confident": true]] : []
        return ["v": 1, "row": document, "text": text ?? NSNull(), "sentences": text == nil ? NSNull() : sentences,
                "todos": todos, "audio": ["seg-000.wav"], "fw": "cpa-fw/0.4.1",
                "queue_text": queue == nil ? NSNull() : (text ?? "")]
    }

    /// The text cut into sentences: at ". " in English, at ， and 。 in Chinese.
    static func sentences(_ text: String) -> [String] {
        let chinese = text.unicodeScalars.contains { (0x4E00...0x9FFF).contains($0.value) }
        let parts = chinese ? text.split(whereSeparator: { "，。".contains($0) }).map(String.init)
            : text.components(separatedBy: ". ").map { $0.hasSuffix(".") ? String($0.dropLast()) : $0 }
        return parts.filter { !$0.isEmpty }.map { $0 + (chinese ? "。" : ".") }
    }
}

/// Read only once built: a scenario's World is never changed after the client gets it (writes go to ShotStore).
extension World: @unchecked Sendable {}
