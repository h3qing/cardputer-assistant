// protocol/dest-cli/v1 (the copy in Fixtures/dest-cli-v1): every valid vector decodes, every invalid one is
// refused with the vector's own code and field; plus the two documents outside the contract that the
// Destinations card reads (cpa dest ls --json, cpa dest presets --json), read leniently and made
// display-safe.

import CardputerCore
import CryptoKit
import Foundation

private struct DestVector {
    let file: String
    let doc: String
    let data: Data
    let reason: String?

    static func all(_ group: String) throws -> [DestVector] {
        let dir = Fixture.url("dest-cli-v1/vectors/\(group)")
        let names = try FileManager.default.contentsOfDirectory(atPath: dir.path).filter { $0.hasSuffix(".json") }
        return try names.sorted().map { try load(dir.appendingPathComponent($0)) }
    }

    static func load(_ url: URL) throws -> DestVector {
        let object = try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any] ?? [:]
        let reason = (object["reason"] as? [String: String]).map { "\($0["code"] ?? "") \($0["field"] ?? "")" }
        let data = try JSONSerialization.data(withJSONObject: object["data"] ?? [:], options: [.fragmentsAllowed])
        return DestVector(file: url.lastPathComponent, doc: object["doc"] as? String ?? "", data: data, reason: reason)
    }

    func decode() throws -> Any {
        switch doc {
        case "pending": return try DestContract.pending(data)
        case "sent": return try DestContract.sent(data)
        case "dropped": return try DestContract.dropped(data)
        case "usual": return try DestContract.usual(data)
        case "tested": return try DestContract.tested(data)
        case "error": return try DestContract.failure(data)
        default: throw ContractError.invalid(field: "(doc)")
        }
    }
}

private func destVector(_ name: String) throws -> Data {
    try DestVector.load(Fixture.url("dest-cli-v1/vectors/valid/\(name).json")).data
}

private func json(_ text: String) -> Data {
    Data(text.utf8)
}

let destContractTests = Suite(name: "DestContract", cases: [
    test("the copied vectors are unchanged (VECTORS.sha256)") {
        let sums = try String(contentsOf: Fixture.url("dest-cli-v1/VECTORS.sha256"), encoding: .utf8)
        let lines = sums.split(separator: "\n").map { $0.split(separator: " ", omittingEmptySubsequences: true) }
        return try lines.map { parts in
            let digest = SHA256.hash(data: try Fixture.data("dest-cli-v1/" + parts[1]))
            return expectEqual(digest.map { String(format: "%02x", $0) }.joined(), String(parts[0]), String(parts[1]))
        } + [expect(lines.count == 33, "33 vectors, got \(lines.count)")]
    },
    test("every valid vector decodes") {
        try DestVector.all("valid").map { vector in
            do {
                _ = try vector.decode()
                return expect(true)
            } catch {
                return expect(false, "\(vector.file): \(error)")
            }
        }
    },
    test("every invalid vector is refused with its code and field") {
        try DestVector.all("invalid").map { vector in
            do {
                _ = try vector.decode()
                return expect(false, "\(vector.file): accepted, want \(vector.reason ?? "?")")
            } catch let error as ContractError {
                return expectEqual("\(error.code) \(error.field)", vector.reason ?? "?", vector.file)
            }
        }
    },
    test("a pending row: held and failed with its code; a removed destination has no fl and no text") {
        let rows = try DestContract.pending(try destVector("pending_held_and_gone")).rows
        return [
            expectEqual(rows.count, 2),
            expectEqual(rows[0].why.known, .held), expectEqual(rows[0].st, .failed),
            expectEqual(rows[0].code, "E_DRAFT_BUSY"), expectEqual(rows[0].kind?.known, .url),
            expectEqual(rows[0].fl, "lc"), expectEqual(rows[0].project, "acmeshop"), expectEqual(rows[0].src.known, .voice),
            expect(rows[0].text != nil), expectEqual(rows[0].id, rows[0].ref),
            expectEqual(rows[1].st, .hold), expectEqual(rows[1].fl, nil), expectEqual(rows[1].text, nil),
            expectEqual(rows[1].label, rows[1].dest, "a removed destination shows its id"),
        ]
    },
    test("unknown why, kind and src values are kept, not refused") {
        let row = try DestContract.pending(try destVector("pending_unknown_values")).rows[0]
        return [expectEqual(row.why.raw, "later"), expectEqual(row.why.known, nil), expectEqual(row.kind?.raw, "cmd"),
                expectEqual(row.src.raw, "photo"), expectEqual(row.fl, "lck")]
    },
    test("sent: handed through the clipboard because the bot had a draft") {
        let sent = try DestContract.sent(try destVector("sent_clip_busy"))
        let failed = try DestContract.sent(try destVector("sent_failed"))
        return [expectEqual(sent.outcome.known, .handed), expectEqual(sent.via?.known, .clip),
                expectEqual(sent.why?.known, .draftBusy), expectEqual(sent.code, nil), expect(sent.leavesMac),
                expectEqual(failed.outcome.known, .failed), expectEqual(failed.code, "E_DEST_MISSING"),
                expectEqual(failed.via, nil)]
    },
    test("tested, dropped and usual") {
        let tested = try DestContract.tested(try destVector("tested"))
        let gone = try DestContract.tested(try destVector("tested_not_handed"))
        let dropped = try DestContract.dropped(try destVector("dropped"))
        let usual = try DestContract.usual(try destVector("usual"))
        return [expectEqual(tested.marker, "4821"), expect(tested.handed), expectEqual(tested.via?.known, .url),
                expect(!gone.handed), expectEqual(gone.code, "E_DEST_MISSING"),
                expectEqual(dropped.ref, "3QZK1M7-00045-0000"),
                expectEqual(usual.usual.map(\.project), ["acmeshop", "cpa"]), expectEqual(usual.usual[1].dest, "clip")]
    },
    test("an error keeps a code this build does not know") {
        let later = try DestContract.failure(try destVector("error_later_code"))
        let device = try DestContract.failure(try destVector("error_needs_device"))
        return [expectEqual(later, DestFailure(code: "E_LATER", field: "dest")),
                expectEqual(device.code, "E_NEEDS_DEVICE"),
                expectThrows("ok true is not an error") { try DestContract.failure(json(#"{"ok":true,"v":1,"error":"E_BUSY"}"#)) }]
    },
    test("pending refuses a missing nullable field, a text over 280 and a bad time") {
        let base = #"{"ref":"R1","why":"usual","dest":"clip","label":"剪贴板","kind":"clip","fl":"","st":null,"code":null,"project":null,"src":"voice","at":"2026-10-22T09:00:00Z""#
        let long = String(repeating: "字", count: 281)
        return [
            expectThrows("text missing") { try DestContract.pending(json(#"{"ok":true,"v":1,"rows":[\#(base)}]}"#)) },
            expectThrows("281") { try DestContract.pending(json(#"{"ok":true,"v":1,"rows":[\#(base),"text":"\#(long)"}]}"#)) },
            expectThrows("time", matching: { ($0 as? ContractError) == .invalid(field: "rows[0].at") }) {
                try DestContract.pending(json(#"{"ok":true,"v":1,"rows":[\#(base.replacingOccurrences(of: "09:00:00Z", with: "9am")),"text":null}]}"#))
            },
            expectEqual(try DestContract.pending(json(#"{"ok":true,"v":1,"rows":[\#(base),"text":null}]}"#)).rows.count, 1),
        ]
    },
    test("cpa dest ls --json: rows read leniently, a bad id dropped, labels and keys made safe") {
        let doc = json(#"""
        {"problem":null,"dests":[
         {"slot":3,"key":"x","id":"url.bot","kind":"url","label":"Bot‮","state":"on","leaves_mac":true,"fl":"lck",
          "profiles":[],"file":null,"handler":{"bundle":"b","team":null},"effect":"draft","verified":null,"preset":"bot","newer_preset":false},
         {"slot":1,"key":null,"id":"daily","kind":"daily","label":"今日笔记","state":"on","leaves_mac":false,"fl":"","profiles":[],"file":null},
         {"slot":9,"key":"XY","id":"Bad Id","kind":"url","label":"x","state":"on","leaves_mac":true,"fl":"lc"}]}
        """#)
        let catalog = try DestCatalog.decode(doc)
        let bot = catalog.dests.first
        return [expectEqual(catalog.dests.map(\.id), ["url.bot", "daily"]), expectEqual(bot?.label, "Bot"),
                expectEqual(bot?.key, "x"), expectEqual(bot?.effect, "draft"), expectEqual(bot?.verified, nil),
                expectEqual(bot?.preset, "bot"), expect(bot?.isOn == true), expectEqual(catalog.dests[1].key, nil),
                expectEqual(catalog.problem, nil),
                expectThrows("not JSON") { try DestCatalog.decode(json("nope")) }]
    },
    test("cpa dest presets --json: installed apps with their note and key hint") {
        let doc = json(#"""
        {"presets":[{"preset":"bot","label":"Bot","kind":"url","id":"url.bot","key_hint":"x","leaves_mac":true,
          "effect":"unknown","may_be_draft":true,"shipped":true,"installed":true,"version":"0.75.0","note":"Fills a draft."},
         {"preset":"BAD","label":"x","kind":"url","id":"url.x","installed":true,"note":"n"}],"problems":["p.json: bad"]}
        """#)
        let presets = try PresetList.decode(doc)
        let first = presets.presets.first
        return [expectEqual(presets.presets.map(\.preset), ["bot"]), expectEqual(first?.keyHint, "x"),
                expectEqual(first?.version, "0.75.0"), expect(first?.leavesMac == true), expect(first?.mayBeDraft == true),
                expectEqual(first?.note, "Fills a draft."), expectEqual(presets.problems, ["p.json: bad"])]
    },
])
