// Projects (docs/25 §8, docs/24 §2.6): named groups made on this Mac, a code folder optional. Every edit is
// one `cpa project … --json` whose answer is the projects document after the change.

import CardputerCore
import Foundation

private func vector(_ name: String) throws -> Data {
    try Fixture.data("filing-v1/vectors/valid/\(name).json").vectorData()
}

private func reply(_ data: Data, exit: Int32 = 0) -> FakeCpaClient.Reply {
    .output(CommandOutput(exitCode: exit, stdout: data))
}

private func refused(_ note: String, _ body: () throws -> CpaCommand) -> Check {
    expectThrows(note, matching: { $0 is CommandError }, body)
}

let projectsTests = Suite(name: "Projects", language: .zh, cases: [
    test("linked and folder: an unlinked group has no folder; an older cpa's rows count as linked") {
        let groups = try FilingContract.projects(try vector("projects_unlinked")).projects
        let older = try FilingContract.projects(try vector("projects_two")).projects
        return [
            expectEqual(groups.map(\.linked), [true, false]),
            expectEqual(groups.map(\.folder), ["my-repo", nil]),
            expectEqual(older.map(\.linked), [true, true]),
            expectEqual(older.map(\.folder), [nil, nil]),
        ]
    },
    test("the map keeps linked and folder for the pane") {
        let map = ProjectMap(try FilingContract.projects(try vector("projects_unlinked")))
        return [
            expectEqual(map.slot(1)?.folder, "my-repo"),
            expectEqual(map.slot(2)?.linked, false),
            expectEqual(map.slot(2)?.label, "2 家务"),
            expectEqual(map.firstFree, 3),
        ]
    },
    test("the first free number is nil when all nine are taken") {
        let full = ProjectMap(ProjectList(projects: (1...9).map {
            ProjectEntry(key: "k\($0)", name: "N\($0)", slot: $0, ok: true)
        }))
        return [expectEqual(full.firstFree, nil), expectEqual(ProjectMap.empty.firstFree, 1)]
    },
    test("edit commands: keys and numbers in argv, the name after --, --json always, 30 s") {
        [
            expectEqual(try CpaCommand.projectAdd(name: " 家务 ", slot: 3).arguments,
                        ["project", "add", "--json", "--slot", "3", "--", "家务"]),
            expectEqual(try CpaCommand.projectAdd(name: "Web", slot: nil).arguments, ["project", "add", "--json", "--", "Web"]),
            expectEqual(try CpaCommand.projectRename("web", name: "网站").arguments,
                        ["project", "rename", "--json", "web", "--", "网站"]),
            expectEqual(try CpaCommand.projectMove("web", to: 4).arguments, ["project", "move", "--json", "web", "4"]),
            expectEqual(try CpaCommand.projectFolder("web", path: "/Users/me/web").arguments,
                        ["project", "folder", "--json", "web", "/Users/me/web"]),
            expectEqual(try CpaCommand.projectFolder("web", path: nil).arguments, ["project", "folder", "--json", "web", "--none"]),
            expectEqual(try CpaCommand.projectRemove("web").arguments, ["project", "rm", "--json", "web"]),
            expectEqual(try CpaCommand.projectRemove("web").timeout, 30),
            expectEqual(CpaCommand.projectsProbe.arguments, ["project", "list", "--json"]),
        ]
    },
    test("edit commands refuse what cpa would refuse, before anything runs") {
        [
            refused("empty name") { try CpaCommand.projectAdd(name: "  ", slot: nil) },
            refused("leading dash") { try CpaCommand.projectAdd(name: "-x", slot: nil) },
            refused("25 bytes") { try CpaCommand.projectAdd(name: String(repeating: "名", count: 9), slot: nil) },
            refused("bidi") { try CpaCommand.projectRename("web", name: "a\u{202e}b") },
            refused("slot 10") { try CpaCommand.projectAdd(name: "x", slot: 10) },
            refused("slot 0") { try CpaCommand.projectMove("web", to: 0) },
            refused("not a key") { try CpaCommand.projectMove("Web", to: 1) },
            refused("relative folder") { try CpaCommand.projectFolder("web", path: "web") },
            refused("key like an option") { try CpaCommand.projectRemove("--all") },
        ]
    },
    test("an edit answers with the list after it; a refusal is the filing error") {
        let add = try CpaCommand.projectAdd(name: "家务", slot: nil)
        let move = try CpaCommand.projectMove("web", to: 1)
        let error = Data(#"{"ok":false,"v":1,"error":"E_ALREADY","field":"slot"}"#.utf8)
        let fake = FakeCpaClient([add: reply(try vector("projects_unlinked")), move: reply(error, exit: 1)])
        let after = try await fake.editProjects(add)
        return [
            expectEqual(after.projects.map(\.key), ["web", "p2"]),
            await expectThrowsAsync("taken", matching: {
                ($0 as? CpaError) == .refused(FilingFailure(code: .already, field: "slot"))
            }) { try await fake.editProjects(move) },
        ]
    },
    test("a refused edit says what to change") {
        func said(_ code: FilingErrorCode, _ field: String?) -> String {
            Feedback.failure(CpaError.refused(FilingFailure(code: code, field: field)))
        }
        return [
            expectEqual(said(.already, "slot"), "这个号已经有项目了，或者 9 个号都满了"),
            expectEqual(said(.already, "key"), "这个 key 已经有项目在用"),
            expectEqual(said(.badArg, "folder"), "这个目录不在了，或者不能用"),
            expectEqual(said(.badArg, "slot"), "号要是 1–9"),
            expectEqual(Feedback.failure(CommandError.invalid(argument: "name")), "名字不行：1–24 字节（最多 8 个汉字），不以 - 开头"),
        ]
    },
    test("the probe: a cpa that knows project list can edit projects") {
        let new = FakeCpaClient([.version: reply(Data("cpa 0.1.0".utf8)), .projectsProbe: reply(try vector("projects_two"))])
        return [
            expectEqual(await FeatureProbe.probe(new).projectEdit, .available),
            expectEqual(await FeatureProbe.probe(FakeCpaClient([.version: reply(Data("cpa 0.1.0".utf8))])).projectEdit,
                        .missing),
        ]
    },
])
