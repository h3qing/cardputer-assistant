import CardputerCore
import Foundation

/// A filing row as cpa prints it, with the fields a test cares about.
func makeEntry(_ suffix: String, filing: String = "new", kind: String = "memo", asked: String? = nil,
               project: String? = nil, text: String? = "有字", queue: String? = nil) throws -> InboxEntry {
    let object: [String: Any?] = [
        "id": "3QZK1M7-00042-" + suffix, "kind": kind, "filing": filing, "why": [String](), "asked": asked,
        "at": ["wall_ms": 1_789_540_000_000, "tz_min": 480, "uncertain": false], "dur_ms": 24000,
        "device": nil, "env": ["host": NSNull(), "link": "here"], "prof": nil, "project": project,
        "starred": false, "tx": text == nil ? "waiting" : "done", "title": nil, "text": text,
        "intent": nil, "clips": nil, "queue": queue.map { ["qid": $0, "intent": "todo", "open": true] },
    ]
    let json = object.mapValues { $0 ?? NSNull() }
    return InboxEntry(try FilingContract.row(try JSONSerialization.data(withJSONObject: json)))
}

let testProjects = ProjectMap(ProjectList(projects: [
    ProjectEntry(key: "acmeshop", name: "Acmeshop", slot: 1, ok: true),
    ProjectEntry(key: "cpa", name: "Cardputer", slot: 3, ok: true),
]))

private func press(_ key: InboxKey, _ entry: InboxEntry?, _ pending: KeyPending = .none,
                   suggested: String? = nil) -> KeyResult {
    InboxKeys.reduce(key, entry: entry, projects: testProjects, pending: pending, suggested: suggested)
}

let keysTests = Suite(name: "Keys", language: .zh, cases: [
    test("1-9 file under that slot's project; 0 sends it back to no project; a missing slot says so") {
        let entry = try makeEntry("0A01")
        let id = entry.id
        return [
            expectEqual(press(.digit(3), entry).intent, .act(.file(id: id, project: "cpa", other: false))),
            expectEqual(press(.digit(0), entry).intent, .act(.file(id: id, project: nil, other: false))),
            expectEqual(press(.digit(2), entry).intent, .blocked("没有 2 号项目")),
            expectEqual(press(.digit(3), nil).intent, .none, "nothing selected"),
        ]
    },
    test("the other-Mac group: a number asks first, Enter files with --other, Esc or another key cancels") {
        let entry = try makeEntry("0A02", asked: "acme")
        let first = press(.digit(1), entry)
        let pending = KeyPending.confirmFile(id: entry.id, project: "acmeshop")
        return [
            expectEqual(first.intent, .none),
            expectEqual(first.pending, pending),
            expectEqual(press(.enter, entry, pending).intent, .act(.file(id: entry.id, project: "acmeshop", other: true))),
            expectEqual(press(.enter, entry, pending).pending, .none),
            expectEqual(press(.escape, entry, pending).pending, .none),
            expectEqual(press(.escape, entry, pending).intent, .none),
            expectEqual(press(.digit(3), entry, pending).pending, .confirmFile(id: entry.id, project: "cpa")),
            expectEqual(press(.down, entry, pending), KeyResult(intent: .move(1), pending: .none)),
        ]
    },
    test("q hands to AI, s hands to be started on the device; without a project the next number picks one") {
        let filed = try makeEntry("0A03", project: "cpa")
        let loose = try makeEntry("0A04")
        let pick = KeyPending.pickProject(id: loose.id, spawn: true)
        return [
            expectEqual(press(.letter("q"), filed).intent, .act(.hand(id: filed.id, project: nil, spawn: false, other: false))),
            expectEqual(press(.letter("h"), filed).intent, .none, "no hidden alias"),
            expectEqual(press(.letter("s"), filed).intent, .act(.hand(id: filed.id, project: nil, spawn: true, other: false))),
            expectEqual(press(.letter("s"), loose), KeyResult(intent: .none, pending: pick)),
            expectEqual(press(.digit(1), loose, pick).intent, .act(.hand(id: loose.id, project: "acmeshop", spawn: true, other: false))),
            expectEqual(press(.enter, loose, pick).intent, .act(.hand(id: loose.id, project: nil, spawn: true, other: false))),
            expectEqual(press(.digit(0), loose, pick).intent, .act(.hand(id: loose.id, project: nil, spawn: true, other: false)),
                        "0 is no project here too"),
            expectEqual(press(.digit(2), loose, pick), KeyResult(intent: .blocked("没有 2 号项目"), pending: pick)),
        ]
    },
    test("grey actions say why instead of acting") {
        let meeting = try makeEntry("0A05", kind: "meeting")
        let silent = try makeEntry("0A06", text: nil)
        let handed = try makeEntry("0A07", filing: "handed", queue: "q12")
        return [
            expectEqual(press(.letter("q"), meeting).intent, .blocked("会议里有别人的话，不能交给 AI")),
            expectEqual(press(.letter("s"), silent).intent, .blocked("还没转写，先归档或等转写")),
            expectEqual(press(.letter("q"), handed).intent, .blocked("已经交给 AI 了")),
        ]
    },
    test("moving, x, space, Enter and /") {
        let entry = try makeEntry("0A08")
        return [
            expectEqual(press(.up, entry).intent, .move(-1)),
            expectEqual(press(.letter("k"), entry).intent, .move(-1)),
            expectEqual(press(.letter("j"), entry).intent, .move(1)),
            expectEqual(press(.down, nil).intent, .move(1), "moving works with nothing selected"),
            expectEqual(press(.letter("x"), entry).intent, .act(.done(id: entry.id))),
            expectEqual(press(.space, entry).intent, .togglePlay),
            expectEqual(press(.enter, entry).intent, .openDetail),
            expectEqual(press(.enter, entry, suggested: "cpa").intent, .act(.file(id: entry.id, project: "cpa", other: false)),
                        "Enter approves the suggested project"),
            expectEqual(press(.slash, entry).intent, .focusSearch),
            expectEqual(press(.letter("z"), entry).intent, .none, "an unknown letter does nothing"),
        ]
    },
    test("the hint while a key waits for the next") {
        [
            expectEqual(InboxKeys.hint(.confirmFile(id: "3QZK1M7-00042-0A02", project: "acmeshop"), projects: testProjects),
                        "别的电脑的项目：按 Enter 归到 1 Acmeshop，Esc 取消"),
            expectEqual(InboxKeys.hint(.pickProject(id: "3QZK1M7-00042-0A04", spawn: false), projects: testProjects),
                        "交给 AI：按 1–9 选项目，Enter 不选，Esc 取消"),
            expectEqual(InboxKeys.hint(.none, projects: testProjects), nil),
        ]
    },
])
