import CardputerCore
import Foundation

private let id = "3QZK1M7-00042-0A1F"
private let cal = Fixture.calendar
private let now = Fixture.now  // Wednesday 2026-09-16

private func ls(_ query: FilingQuery) throws -> [String] {
    try CpaCommand.filingList(query, now: now, calendar: cal).arguments
}

private func refused(_ note: String, _ body: () throws -> CpaCommand) -> Check {
    expectThrows(note, matching: { $0 is CommandError }, body)
}

let commandTests = Suite(name: "Commands", cases: [
    test("read commands: fixed argument arrays, always --json, short timeouts") {
        [
            expectEqual(CpaCommand.status.arguments, ["status", "--json"]),
            expectEqual(CpaCommand.status.tool, .cpa),
            expectEqual(CpaCommand.status.timeout, 5),
            expectEqual(CpaCommand.recordings(hidden: false).arguments, ["rec", "ls", "--json"]),
            expectEqual(CpaCommand.recordings(hidden: true).arguments, ["rec", "ls", "--json", "--hidden"]),
            expectEqual(CpaCommand.projects.arguments, ["project", "ls", "--json"]),
            expectEqual(try CpaCommand.filingShow(id).arguments, ["filing", "show", id, "--json"]),
            expectEqual(CpaCommand.version.arguments, ["--version"]),
            expectEqual(CpaCommand.sttStatus.arguments, ["status", "--json"]),
            expectEqual(CpaCommand.sttStatus.tool, .cpaStt),
            expectEqual(CpaCommand.hostShow.arguments, ["host", "name", "--json"]),
            expectEqual(CpaCommand.sttRetry.arguments, ["retry"]),
            expectEqual(CpaCommand.sttRetry.tool, .cpaStt),
            expectEqual(CpaCommand.mcpSetupList.arguments, ["mcp", "setup", "--list", "--json"]),
        ]
    },
    test("output caps: 4 MiB, 32 MiB for cpa rec ls (it has no limit option)") {
        [
            expectEqual(CpaCommand.status.outputCap, 4 << 20),
            expectEqual(CpaCommand.recordings(hidden: true).outputCap, 32 << 20),
        ]
    },
    test("filing writes wait 30 s: cpa may wait on its events lock") {
        [
            expectEqual(try CpaCommand.file(id, project: nil, other: false).timeout, 30),
            expectEqual(try CpaCommand.hand(id, project: nil, spawn: true, other: false).timeout, 30),
            expectEqual(try CpaCommand.filingShow(id).timeout, 5),
        ]
    },
    test("filing writes carry only an id, a key and flags") {
        [
            expectEqual(try CpaCommand.file(id, project: "acmeshop", other: false).arguments,
                        ["filing", "file", id, "acmeshop", "--json"]),
            expectEqual(try CpaCommand.file(id, project: nil, other: false).arguments,
                        ["filing", "file", id, "--none", "--json"]),
            expectEqual(try CpaCommand.file(id, project: "cpa", other: true).arguments,
                        ["filing", "file", id, "cpa", "--other", "--json"]),
            expectEqual(try CpaCommand.done(id).arguments, ["filing", "done", id, "--json"]),
            expectEqual(try CpaCommand.hide(id).arguments, ["filing", "hide", id, "--json"]),
            expectEqual(try CpaCommand.reopen(id).arguments, ["filing", "reopen", id, "--json"]),
            expectEqual(try CpaCommand.hand(id, project: nil, spawn: false, other: false).arguments,
                        ["filing", "hand", id, "--json"]),
            expectEqual(try CpaCommand.hand(id, project: "cpa", spawn: true, other: true).arguments,
                        ["filing", "hand", id, "cpa", "--spawn", "--other", "--json"]),
            expectEqual(try CpaCommand.file("3qzk1m7-00042-0a1f", project: nil, other: false).arguments.dropFirst(2).first, id,
                        "ids are taken in any case and sent in upper case"),
        ]
    },
    test("bad ids and keys are refused before anything runs") {
        [
            refused("short id") { try CpaCommand.done("3QZK1M7-00042") },
            refused("option as id") { try CpaCommand.hide("--all") },
            refused("letter I") { try CpaCommand.reopen("3QZK1M7-00042-0A1I") },
            refused("upper key") { try CpaCommand.file(id, project: "Acmeshop", other: false) },
            refused("option as key") { try CpaCommand.file(id, project: "-x", other: false) },
            refused("long key") { try CpaCommand.hand(id, project: "abcdefghijklm", spawn: false, other: false) },
            refused("sid of unhide") { try CpaCommand.unhide("../x") },
            expectEqual(try CpaCommand.unhide("3QZK1M7-00042-0036").arguments, ["rec", "unhide", "3QZK1M7-00042-0036"]),
        ]
    },
    test("undo is rebuilt from the neutral object with our own verbs") {
        [
            expectEqual(try CpaCommand.undo(UndoStep(op: Open(.file), id: id, project: nil)).arguments,
                        ["filing", "file", id, "--none", "--json"]),
            expectEqual(try CpaCommand.undo(UndoStep(op: Open(.file), id: id, project: "acme")).arguments,
                        ["filing", "file", id, "acme", "--json"]),
            expectEqual(try CpaCommand.undo(UndoStep(op: Open(.reopen), id: id, project: nil)).arguments,
                        ["filing", "reopen", id, "--json"]),
            expectEqual(try CpaCommand.undo(UndoStep(op: Open(.done), id: id, project: nil)).arguments,
                        ["filing", "done", id, "--json"]),
            expectEqual(try CpaCommand.undo(UndoStep(op: Open(.hide), id: id, project: nil)).arguments,
                        ["filing", "hide", id, "--json"]),
            expectThrows("unknown op", matching: { $0 as? CommandError == .unsupportedUndo }) {
                try CpaCommand.undo(UndoStep(op: Open(raw: "purge"), id: id, project: nil))
            },
            refused("bad id in undo") { try CpaCommand.undo(UndoStep(op: Open(.done), id: "x", project: nil)) },
        ]
    },
    test("each inbox scope maps to fixed cpa filing ls arguments") {
        [
            expectEqual(try ls(FilingQuery()), ["filing", "ls", "--json", "--filing", "new"]),
            expectEqual(try ls(FilingQuery(scope: .otherMac)), ["filing", "ls", "--json", "--filing", "new", "--orphans"]),
            expectEqual(try ls(FilingQuery(scope: .handed)), ["filing", "ls", "--json", "--filing", "handed"]),
            expectEqual(try ls(FilingQuery(scope: .done)), ["filing", "ls", "--json", "--filing", "done"]),
            expectEqual(try ls(FilingQuery(scope: .noText)),
                        ["filing", "ls", "--json", "--filing", "all", "--tx", "failed", "--tx", "stale"]),
            expectEqual(try ls(FilingQuery(scope: .all)), ["filing", "ls", "--json", "--filing", "all"]),
        ]
    },
    test("date buttons become --from / --to; weeks start on Monday") {
        [
            expectEqual(try ls(FilingQuery(dates: .today)).suffix(4), ["--from", "2026-09-16", "--to", "2026-09-16"]),
            expectEqual(try ls(FilingQuery(dates: .thisWeek)).suffix(4), ["--from", "2026-09-14", "--to", "2026-09-16"]),
            expectEqual(try ls(FilingQuery(dates: .lastWeek)).suffix(4), ["--from", "2026-09-07", "--to", "2026-09-13"]),
            expectEqual(try ls(FilingQuery(dates: .all)).contains("--from"), false),
        ]
    },
    test("project, mode, device, Mac, paging filters") {
        let query = FilingQuery(projects: ["acmeshop", "cpa"], mode: .memo, device: "5c0e9a7b3d21", link: .here,
                                limit: 50, cursor: "WyJmaWxpbmci")
        return [
            expectEqual(try ls(query), ["filing", "ls", "--json", "--filing", "new", "--device", "5c0e9a7b3d21",
                                        "--link", "here", "--mode", "memo", "--project", "acmeshop",
                                        "--project", "cpa", "--limit", "50", "--cursor", "WyJmaWxpbmci"]),
            expectEqual(try ls(FilingQuery(withoutProject: true)).last, "--none"),
            refused("project and none") { try CpaCommand.filingList(FilingQuery(projects: ["cpa"], withoutProject: true), now: now, calendar: cal) },
            refused("device MAC") { try CpaCommand.filingList(FilingQuery(device: "02:00:5e:10:00:01"), now: now, calendar: cal) },
            refused("limit 0") { try CpaCommand.filingList(FilingQuery(limit: 0), now: now, calendar: cal) },
            refused("cursor") { try CpaCommand.filingList(FilingQuery(cursor: "a b"), now: now, calendar: cal) },
        ]
    },
    test("search words go through stdin, never argv") {
        let words = "报价 --filing hidden"
        let command = try CpaCommand.filingList(FilingQuery(text: words), now: now, calendar: cal)
        return [
            expectEqual(Array(command.arguments.suffix(2)), ["--text", "-"]),
            expect(!command.arguments.contains { $0.contains("报价") }),
            expectEqual(command.stdin, Data(words.utf8)),
            expectEqual(try CpaCommand.filingList(FilingQuery(text: "   "), now: now, calendar: cal).stdin, nil),
            refused("201 characters") {
                try CpaCommand.filingList(FilingQuery(text: String(repeating: "字", count: 201)), now: now, calendar: cal)
            },
        ]
    },
    test("only the other-Mac scope keeps rows by asked; every other scope keeps all") {
        let rows = try FilingContract.list(try Vector.mixed()).rows
        return [
            expectEqual(rows.filter(FilingQuery(scope: .otherMac).keeps).map(\.id), ["3QZK1M7-00045-0001"]),
            expectEqual(rows.filter(FilingQuery().keeps).count, rows.count),
        ]
    },
    test("a command's name for logs never carries an id, a key, a path or search words") {
        let named = [
            (try CpaCommand.file(id, project: "acmeshop", other: true), "cpa filing file"),
            (try CpaCommand.unhide(id), "cpa rec unhide"),
            (try CpaCommand.export(to: "/Users/me/out", zip: true, audio: false), "cpa export"),
            (try CpaCommand.sttSetup(model: "/Users/me/m.bin"), "cpa-stt setup"),
            (try CpaCommand.filingList(FilingQuery(text: "报价"), now: now, calendar: cal), "cpa filing ls"),
            (try CpaCommand.mcpSetup("alpha-one"), "cpa mcp setup"),
            (CpaCommand.version, "cpa"),
        ]
        return named.map { command, name in expectEqual(command.name, name) }
    },
    test("integrations, export and local transcription commands") {
        [
            expectEqual(try CpaCommand.mcpSetup("alpha-one").arguments, ["mcp", "setup", "alpha-one"]),
            refused("a client id that is not a word") { try CpaCommand.mcpSetup("--list") },
            expectEqual(McpClients.choices(help: "usage: cpa mcp setup [-h] {alpha-one,beta,gamma}\n\noptions:\n"),
                        ["alpha-one", "beta", "gamma"], "the client ids cpa's own help lists"),
            expectEqual(McpClients.choices(help: "usage: cpa mcp setup [-h] client\n"), []),
            expectEqual(CpaCommand.mcpSetupHelp.arguments, ["mcp", "setup", "--help"]),
            expectEqual(CpaCommand.mcpTools.arguments, ["mcp", "tools", "--json"]),
            expectEqual(try CpaCommand.mcpAudit(last: 20).arguments, ["mcp", "audit", "--last", "20", "--json"]),
            expectEqual(try CpaCommand.export(to: "/Users/me/out.zip", zip: true, audio: false).arguments,
                        ["export", "--zip", "--no-audio", "/Users/me/out.zip"]),
            expectEqual(try CpaCommand.export(to: "/Users/me/out", zip: false, audio: true).timeout, 600),
            refused("relative export path") { try CpaCommand.export(to: "out", zip: false, audio: true) },
            refused("option as path") { try CpaCommand.export(to: "-x", zip: false, audio: true) },
            expectEqual(CpaCommand.sttUse.arguments, ["use"]),
            expectEqual(CpaCommand.sttUse.timeout, 120),
            expectEqual(CpaCommand.sttOff.arguments, ["off"]),
            expectEqual(CpaCommand.sttServiceInstall.arguments, ["service", "install"]),
            expectEqual(try CpaCommand.sttSetup(model: "/Users/me/m.bin").arguments, ["setup", "--yes", "/Users/me/m.bin"]),
            refused("relative model path") { try CpaCommand.sttSetup(model: "m.bin") },
        ]
    },
])

private enum Vector {
    static func mixed() throws -> Data {
        let object = try JSONSerialization.jsonObject(with: try Fixture.data("filing-v1/vectors/valid/ls_mixed.json"))
        return try JSONSerialization.data(withJSONObject: (object as? [String: Any])?["data"] ?? [:])
    }
}
