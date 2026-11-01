import CardputerCore
import CryptoKit
import Foundation

/// One vector file of protocol/filing/v1: {doc, name, data, reason?}.
private struct Vector {
    let file: String
    let doc: String
    let data: Data
    let reason: (code: String, field: String)?

    static func all(_ group: String) throws -> [Vector] {
        let dir = Fixture.url("filing-v1/vectors/\(group)")
        let names = try FileManager.default.contentsOfDirectory(atPath: dir.path).filter { $0.hasSuffix(".json") }
        return try names.sorted().map { try load(dir.appendingPathComponent($0)) }
    }

    /// `data` is cut from the file's own bytes (it is the last key), never parsed and written again: a vector
    /// may hold what a parser would not carry over unchanged (a lone surrogate).
    static func load(_ url: URL) throws -> Vector {
        let raw = try Data(contentsOf: url)
        let head = try JSONSerialization.jsonObject(with: try cutData(raw, keepHead: true)) as? [String: Any] ?? [:]
        let reason = (head["reason"] as? [String: String]).map { (code: $0["code"] ?? "", field: $0["field"] ?? "") }
        return Vector(file: url.lastPathComponent, doc: head["doc"] as? String ?? "", data: try cutData(raw, keepHead: false),
                      reason: reason)
    }

    /// The bytes after `"data":` up to the file's last `}`; or, with keepHead, the object without its data.
    static func cutData(_ raw: Data, keepHead: Bool) throws -> Data {
        guard let key = raw.range(of: Data(#""data":"#.utf8)), let close = raw.lastIndex(of: UInt8(ascii: "}")) else {
            throw ContractError.invalid(field: "(vector)")
        }
        if keepHead {
            let before = raw[..<key.lowerBound]
            let comma = before.lastIndex(of: UInt8(ascii: ",")) ?? before.endIndex
            return Data(raw[..<comma]) + Data("}".utf8)
        }
        return Data(raw[key.upperBound..<close])
    }

    func decode() throws -> Any {
        switch doc {
        case "ls": return try FilingContract.list(data)
        case "show": return try FilingContract.item(data)
        case "result": return try FilingContract.result(data)
        case "error": return try FilingContract.failure(data)
        case "projects": return try FilingContract.projects(data)
        case "row": return try FilingContract.row(data)
        case "host": return try FilingContract.host(data)
        default: throw ContractError.invalid(field: "(doc)")
        }
    }
}

private func vector(_ name: String) throws -> Data {
    try Vector.load(Fixture.url("filing-v1/vectors/valid/\(name).json")).data
}

/// A lone surrogate is refused by the JSON parser itself, before any field is read: the whole document.
private let wholeDocument: Set<String> = ["ls_text_lone_surrogate.json"]

private func rejection(_ vector: Vector) -> Check {
    let want = vector.reason.map { "\($0.code) \($0.field)" } ?? "?"
    do {
        _ = try vector.decode()
        return expect(false, "\(vector.file): accepted, want \(want)")
    } catch let error as ContractError {
        let got = "\(error.code) \(error.field)"
        let whole = wholeDocument.contains(vector.file) && got == "E_INVALID (root)"
        return whole ? expect(true) : expectEqual(got, want, vector.file)
    } catch {
        return expect(false, "\(vector.file): \(error)")
    }
}

/// Left out of the copy (see Fixtures/filing-v1/SOURCE): it carries a real device MAC address.
private let leftOut: Set<String> = ["vectors/invalid/ls_device_mac.json"]

let contractTests = Suite(name: "Contract", language: .zh, cases: [
    test("the copied vectors are unchanged (VECTORS.sha256)") {
        let sums = try String(contentsOf: Fixture.url("filing-v1/VECTORS.sha256"), encoding: .utf8)
        let lines = sums.split(separator: "\n").map { $0.split(separator: " ", omittingEmptySubsequences: true) }
            .filter { !leftOut.contains(String($0[1])) }
        return try lines.map { parts in
            let digest = SHA256.hash(data: try Fixture.data("filing-v1/" + parts[1]))
            let hex = digest.map { String(format: "%02x", $0) }.joined()
            return expectEqual(hex, String(parts[0]), String(parts[1]))
        } + [expect(lines.count == 45, "45 vectors, got \(lines.count)")]
    },
    test("every valid vector decodes") {
        let vectors = try Vector.all("valid")
        return [expectEqual(vectors.count, 22)] + vectors.map { vector in
            do {
                _ = try vector.decode()
                return expect(true)
            } catch {
                return expect(false, "\(vector.file): \(error)")
            }
        }
    },
    test("every invalid vector is refused with exactly its code and field") {
        let vectors = try Vector.all("invalid")
        return [expectEqual(vectors.count, 23)] + vectors.map(rejection)
    },
    test("a row whose device is a MAC address is refused (in place of ls_device_mac.json)") {
        let text = String(decoding: try vector("ls_new_memo"), as: UTF8.self)
            .replacingOccurrences(of: "5c0e9a7b3d21", with: "02:00:5e:10:00:01")
        return [expectThrows("MAC", matching: { $0 as? ContractError == .invalid(field: "rows[0].device") }) {
            try FilingContract.list(Data(text.utf8))
        }]
    },
    test("ls: counts, cursor and the rows' fields") {
        let list = try FilingContract.list(try vector("ls_mixed"))
        let handed = list.rows[1]
        let orphan = list.rows[2]
        return [
            expectEqual(list.counts, FilingCounts(new: 1, otherMac: 1, clipsWaiting: 0, onDevice: nil)),
            expectEqual(list.nextCursor, "WyJmaWxpbmciLCIxNzkyNjIwMTIwMDAwLzNRWksxTTctMDAwNDAtMDAwMCJd"),
            expectEqual(list.rows.map(\.filing), [.filed, .handed, .new, .done, .hidden]),
            expectEqual(handed.queue, QueueItem(qid: "q12", intent: Open(.spawn), open: true)),
            expectEqual(handed.env, MacStamp(host: "公司", link: Open(.unknown))),
            expectEqual(handed.at, RecordedAt(wallMs: 1_792_639_380_000, tzMin: 480, uncertain: false)),
            expectEqual(handed.durMs, 24_000),
            expectEqual(handed.intent, Open(.todo)),
            expectEqual(orphan.asked, "acme"),
            expectEqual(orphan.why, [Open(.orphan)]),
            expectEqual(orphan.isOtherMac, true),
            expectEqual(handed.isOtherMac, false),
            expectEqual(list.rows[0].kind.known, .meeting),
            expectEqual(list.rows[3].device, nil),
        ]
    },
    test("a later cpa: unknown fields are ignored, unknown enum values are kept as unknown") {
        let row = try FilingContract.list(try vector("ls_unknown_values")).rows[0]
        return [
            expectEqual(row.kind.raw, "capture"),
            expectEqual(row.kind.known, nil),
            expectEqual(row.tx.raw, "queued_remote"),
            expectEqual(row.why.map(\.raw), ["unfiled", "later"]),
            expectEqual(row.env?.link?.raw, "other"),
            expectEqual(Labels.kind(row.kind), "未知"),
            expectEqual(Labels.transcription(row.tx), "未知"),
        ]
    },
    test("show: the whole text, sentences, todos, audio names and the queue text") {
        let item = try FilingContract.item(try vector("show_memo"))
        return [
            expectEqual(item.row.id, "3QZK1M7-00042-0A1F"),
            expectEqual(item.sentences, [TimedSentence(seconds: 0, text: "待办：周五前给 Alice 发报价单。"),
                                         TimedSentence(seconds: nil, text: "OK.")]),
            expectEqual(item.todos.first?.text, "给 Alice 发报价单"),
            expectEqual(item.todos.first?.seconds, 361),
            expectEqual(item.audio, ["seg-000.wav"]),
            expectEqual(item.fw, "cpa-fw/0.4.0"),
            expectEqual(item.queueText, "周五前给 Alice 发报价单。"),
        ]
    },
    test("result: state after, changed, the neutral undo and the queue item") {
        let filed = try FilingContract.result(try vector("result_file"))
        let handed = try FilingContract.result(try vector("result_hand"))
        let same = try FilingContract.result(try vector("result_unchanged"))
        return [
            expectEqual(filed.filing, .filed),
            expectEqual(filed.undo, UndoStep(op: Open(.file), id: "3QZK1M7-00042-0A1F", project: nil)),
            expectEqual(handed.undo?.op.known, .reopen),
            expectEqual(handed.queue?.qid, "q12"),
            expectEqual(same.changed, false),
            expectEqual(same.undo, nil),
        ]
    },
    test("error and projects documents") {
        let failure = try FilingContract.failure(try vector("error_unknown_id"))
        let bare = try FilingContract.failure(try vector("error_unavailable"))
        let projects = try FilingContract.projects(try vector("projects_two"))
        return [
            expectEqual(failure.code, .unknownID),
            expectEqual(bare, FilingFailure(code: .unavailable, field: nil)),
            expectEqual(projects.projects, [ProjectEntry(key: "acmeshop", name: "Acmeshop", slot: 1, ok: true),
                                            ProjectEntry(key: "cpa", name: "Cardputer", slot: 2, ok: false)]),
        ]
    },
    test("host: this Mac's name, or null when it has none") {
        [
            expectEqual(try FilingContract.host(try vector("host_named")), HostName(host: "公司")),
            expectEqual(try FilingContract.host(try vector("host_none")), HostName(host: nil)),
            expectThrows("25 characters", matching: { $0 as? ContractError == .invalid(field: "host") }) {
                try FilingContract.host(Data(#"{"v":1,"host":"\#(String(repeating: "长", count: 25))"}"#.utf8))
            },
            expectThrows("no host key", matching: { $0 as? ContractError == .invalid(field: "host") }) {
                try FilingContract.host(Data(#"{"v":1}"#.utf8))
            },
        ]
    },
    test("a row's env carries both host and link, each maybe null; an undo of file always says its project") {
        let text = String(decoding: try vector("ls_new_memo"), as: UTF8.self)
        let noLink = text.replacingOccurrences(of: #""link": "unknown""#, with: #""other": 1"#)
        let undo = #"{"ok":true,"v":1,"id":"3QZK1M7-00042-0A1F","filing":"filed","changed":true,"#
            + #""undo":{"op":"file","id":"3QZK1M7-00042-0A1F","args":{}},"queue":null}"#
        return [
            expect(text.contains(#""link": "unknown""#), "the vector still has the link this test removes"),
            expectThrows("env without link", matching: { $0 as? ContractError == .invalid(field: "rows[0].env.link") }) {
                try FilingContract.list(Data(noLink.utf8))
            },
            expectThrows("file undo without project", matching: { $0 as? ContractError == .invalid(field: "undo.args.project") }) {
                try FilingContract.result(Data(undo.utf8))
            },
        ]
    },
    test("v must be the integer 1: missing, 2, \"1\" and true are all refused") {
        let docs = [#"{"projects":[]}"#, #"{"v":2,"projects":[]}"#, #"{"v":"1","projects":[]}"#,
                    #"{"v":true,"projects":[]}"#, #"{"v":1.5,"projects":[]}"#]
        return docs.map { doc in
            expectThrows(doc, matching: { $0 as? ContractError == .version }) {
                try FilingContract.projects(Data(doc.utf8))
            }
        } + [
            expectThrows("not an object", matching: { $0 as? ContractError == .invalid(field: "(root)") }) {
                try FilingContract.projects(Data("[1]".utf8))
            },
            expectThrows("not JSON", matching: { $0 as? ContractError == .invalid(field: "(root)") }) {
                try FilingContract.projects(Data("{".utf8))
            },
        ]
    },
])
