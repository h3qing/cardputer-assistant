import CardputerCore
import Foundation

private let id = "3QZK1M7-00042-0A1F"

private func ok(_ path: String) throws -> FakeCpaClient.Reply {
    .output(CommandOutput(exitCode: 0, stdout: try Fixture.data(path)))
}

private func vector(_ name: String) throws -> FakeCpaClient.Reply {
    let object = try JSONSerialization.jsonObject(with: try Fixture.data("filing-v1/vectors/valid/\(name).json"))
    let data = try JSONSerialization.data(withJSONObject: (object as? [String: Any])?["data"] ?? [:])
    let exit: Int32 = name.hasPrefix("error") ? 1 : 0
    return .output(CommandOutput(exitCode: exit, stdout: data))
}

private func raw(_ exit: Int32, _ text: String) -> FakeCpaClient.Reply {
    .output(CommandOutput(exitCode: exit, stdout: Data(text.utf8)))
}

private func fails(_ want: CpaError, _ note: String, _ body: () async throws -> Any) async -> Check {
    await expectThrowsAsync(note, matching: { ($0 as? CpaError) == want }, body)
}

let clientTests = Suite(name: "Client", language: .zh, cases: [
    test("typed reads decode the command's JSON") {
        let fake = FakeCpaClient([
            .status: try ok("cpa/status-running.json"),
            .recordings(hidden: false): try ok("cpa/rec-ls.json"),
            .projects: try vector("projects_two"),
            try .filingShow(id): try vector("show_memo"),
            .sttStatus: try ok("cpa/stt-status.json"),
            .version: raw(0, "cpa 0.1.0\n"),
        ])
        return [
            expectEqual(try await fake.status().link?.known, .ready),
            expectEqual(try await fake.recordings(hidden: false).count, 16),
            expectEqual(try await fake.projects().projects.count, 2),
            expectEqual(try await fake.filingShow(id).queueText, "周五前给 Alice 发报价单。"),
            expectEqual(try await fake.sttStatus().claim?.app, "cpa-stt"),
            expectEqual(try await fake.version(), "cpa 0.1.0"),
        ]
    },
    test("writes return the result document") {
        let fake = FakeCpaClient([try .file(id, project: "cpa", other: false): try vector("result_file")])
        let result = try await fake.perform(try .file(id, project: "cpa", other: false))
        return [expectEqual(result.filing, .filed), expectEqual(result.undo?.project, nil)]
    },
    test("a non-zero exit becomes a typed error") {
        let done = try CpaCommand.done(id)
        let already = FakeCpaClient([done: raw(1, #"{"ok":false,"v":1,"error":"E_ALREADY","field":"filing"}"#)])
        let other = FakeCpaClient([done: raw(2, #"{"ok":false,"v":1,"error":"E_BAD_ARG","field":"other"}"#)])
        let usage = FakeCpaClient([done: raw(2, "")])
        let crash = FakeCpaClient([done: raw(1, "Traceback")])
        return [
            await fails(.refused(FilingFailure(code: .already, field: "filing")), "E_ALREADY") { try await already.perform(done) },
            await fails(.refused(FilingFailure(code: .badArg, field: "other")), "E_BAD_ARG") { try await other.perform(done) },
            await fails(.failed("cpa filing done", exitCode: 2), "exit 2 outside a probe is a failure, not a missing command") {
                try await usage.perform(done)
            },
            await fails(.failed("cpa filing done", exitCode: 1), "crash") { try await crash.perform(done) },
        ]
    },
    test("bad output, a later contract and a timeout are errors, never guesses") {
        let broken = FakeCpaClient([.status: raw(0, "{not json"), .projects: raw(0, #"{"v":2,"projects":[]}"#)])
        let slow = FakeCpaClient([.status: .failure(.timedOut("cpa status"))])
        return [
            await fails(.badOutput(.invalid(field: "(root)")), "bad JSON") { try await broken.status() },
            await fails(.badOutput(.version), "v 2") { try await broken.projects() },
            await fails(.timedOut("cpa status"), "timeout") { try await slow.status() },
            await fails(.failed("cpa rec ls", exitCode: 2), "unknown to the fake") { try await slow.recordings(hidden: false) },
        ]
    },
    test("errors in Chinese, saying what to do") {
        [
            expectEqual(Labels.failure(.refused(FilingFailure(code: .unavailable, field: "cpad"))),
                        "cpad 比 cpa 老：在终端跑 cpa service install"),
            expectEqual(Labels.failure(.refused(FilingFailure(code: .noText, field: "text"))), "还没转写，先归档或等转写"),
            expectEqual(Labels.failure(.refused(FilingFailure(code: .badArg, field: "other"))), "这是别的电脑的项目，要先确认"),
            expectEqual(Labels.failure(.refused(FilingFailure(code: .badArg, field: "id"))), "会议里有别人的话，不能交给 AI"),
            expectEqual(Labels.failure(.refused(FilingFailure(code: .already, field: "queue"))),
                        "交给 AI 的那一项还在排队或已经开始了"),
            expectEqual(Labels.failure(.notFound(.cpa)), "没找到 cpa"),
            expectEqual(Labels.failure(.timedOut("cpa status")), "cpa 没有及时回答"),
            expectEqual(Labels.failure(.timedOut("cpa-stt use")), "cpa-stt 没有及时回答"),
            expectEqual(Labels.failure(.unsupported("cpa filing ls")), "这个 cpa 还没有这个命令：先更新 cpa"),
        ]
    },
    test("a failed command names its tool and, where it helps, the likely cause") {
        [
            expectEqual(Labels.failure(.failed("cpa-stt use", exitCode: 2)), "cpa-stt 出错了（退出码 2）：多半是缺模型，先选模型文件"),
            expectEqual(Labels.failure(.failed("cpa export", exitCode: 2)),
                        "cpa 出错了（退出码 2）：目标要是新的，不能在 iCloud 同步的文件夹（桌面、文稿、iCloud 云盘）或笔记目录里"),
            expectEqual(Labels.failure(.failed("cpa host name", exitCode: 2)), "cpa 出错了（退出码 2）：名字最多 24 字节（8 个汉字）"),
            expectEqual(Labels.failure(.failed("cpa status", exitCode: 1)), "cpa 出错了（退出码 1）"),
            expectEqual(Labels.failure(.outputTooLarge("cpa rec ls")), "cpa 的输出太大，没有读"),
            expectEqual(Labels.failure(.launchFailed(.cpaStt)), "cpa-stt 启动失败"),
        ]
    },
])

let featureTests = Suite(name: "Features", cases: [
    test("everything there: every probe answers") {
        let fake = FakeCpaClient([
            .version: raw(0, "cpa 0.2.0\n"),
            FeatureProbe.filingProbe: try vector("ls_empty"),
            .projects: try vector("projects_two"),
            FeatureProbe.hostProbe: try vector("host_named"),
            .sttStatus: try ok("cpa/stt-status.json"),
        ])
        let found = await FeatureProbe.probe(fake)
        return [
            expectEqual(found, Capabilities(cpa: .available, filing: .available, projectList: .available,
                                            hostName: .available, stt: .available)),
            expect(found.canFile),
        ]
    },
    test("today's cpa: no filing, no project ls --json, no host name, no cpa-stt") {
        let fake = FakeCpaClient([.version: raw(0, "cpa 0.1.0\n"), .sttStatus: .failure(.notFound(.cpaStt))])
        let found = await FeatureProbe.probe(fake)
        return [
            expectEqual(found, Capabilities(cpa: .available, filing: .missing, projectList: .missing,
                                            hostName: .missing, stt: .missing)),
            expect(!found.canFile),
        ]
    },
    test("no cpa at all") {
        let fake = FakeCpaClient([:], fallback: .failure(.notFound(.cpa)))
        let found = await FeatureProbe.probe(fake)
        return [expectEqual(found.cpa, .missing), expectEqual(found.filing, .missing)]
    },
    test("only a probe reads exit 2 as a missing command") {
        let fake = FakeCpaClient([.version: raw(0, "cpa 0.1.0\n"), FeatureProbe.filingProbe: raw(2, "")])
        return [
            expectEqual(await FeatureProbe.probe(fake).filing, .missing),
            await fails(.failed("cpa filing ls", exitCode: 2), "outside the probe") {
                try await fake.run(FeatureProbe.filingProbe)
            },
            await fails(.unsupported("cpa filing ls"), "the probe's reading") {
                try await fake.run(FeatureProbe.filingProbe, exit2MeansMissing: true)
            },
        ]
    },
    test("a timeout, bad JSON or a later v is broken, not missing") {
        let probe = FeatureProbe.filingProbe
        let slow = FakeCpaClient([.version: raw(0, "cpa"), probe: .failure(.timedOut("cpa filing ls"))])
        let garbled = FakeCpaClient([.version: raw(0, "cpa"), probe: raw(0, "[")])
        let later = FakeCpaClient([.version: raw(0, "cpa"), probe: raw(0, #"{"v":2}"#)])
        return [
            expectEqual(await FeatureProbe.probe(slow).filing, .broken(.timedOut("cpa filing ls"))),
            expectEqual(await FeatureProbe.probe(garbled).filing, .broken(.badOutput(.invalid(field: "(root)")))),
            expectEqual(await FeatureProbe.probe(later).filing, .broken(.badOutput(.version))),
        ]
    },
])

let projectMapTests = Suite(name: "ProjectMap", cases: [
    test("1-9 from cpa project ls --json, named on the buttons") {
        let map = ProjectMap(try FilingContract.projects(try Fixture.data("filing-v1/vectors/valid/projects_two.json").vectorData()))
        return [
            expectEqual(map.slot(1)?.key, "acmeshop"),
            expectEqual(map.slot(1)?.label, "1 Acmeshop"),
            expectEqual(map.slot(2)?.ok, false),
            expectEqual(map.slot(3), nil),
            expectEqual(map.project(key: "cpa")?.slot, 2),
            expectEqual(map.label(key: "cpa"), "2 Cardputer"),
            expectEqual(map.label(key: "acme"), "acme", "another Mac's key keeps its key"),
        ]
    },
    test("before project ls --json: the keys of cpa status, named by their key") {
        let map = ProjectMap(try CpaJSON.status(try Fixture.data("cpa/status-running.json")))
        return [expectEqual(map.slots.map(\.label), ["1 acmeshop", "2 cardputer"])]
    },
    test("names are display_safe; a repeated or impossible slot is dropped") {
        let map = ProjectMap(ProjectList(projects: [
            ProjectEntry(key: "b", name: "B\u{202e}ad", slot: 2, ok: true),
            ProjectEntry(key: "a", name: "A", slot: 1, ok: true),
            ProjectEntry(key: "c", name: "C", slot: 2, ok: true),
            ProjectEntry(key: "d", name: "D", slot: 10, ok: true),
        ]))
        return [expectEqual(map.slots.map(\.label), ["1 A", "2 Bad"])]
    },
    test("load falls back to cpa status when project ls --json is missing") {
        let status = try CpaJSON.status(try Fixture.data("cpa/status-running.json"))
        let old = FakeCpaClient([:])
        let new = FakeCpaClient([.projects: try vector("projects_two")])
        return [
            expectEqual(try await ProjectMap.load(old, status: status, listed: false).slots.map(\.key), ["acmeshop", "cardputer"]),
            expectEqual(try await ProjectMap.load(new, status: status, listed: true).slots.map(\.key), ["acmeshop", "cpa"]),
            expectEqual(try await ProjectMap.load(old, status: nil, listed: false), ProjectMap.empty),
            await expectThrowsAsync("a listed command that fails is an error, not a fallback") {
                try await ProjectMap.load(old, status: status, listed: true)
            },
        ]
    },
    test("all nine numbers, each with its project or empty") {
        let map = ProjectMap(try CpaJSON.status(try Fixture.data("cpa/status-running.json")))
        let rows = map.numbers
        return [
            expectEqual(rows.count, 9),
            expectEqual(rows.map(\.number), Array(1...9)),
            expectEqual(rows[0].slot?.key, "acmeshop"),
            expectEqual(rows[5].slot, nil),
        ]
    },
])

extension Data {
    /// The `data` of a vector file.
    func vectorData() throws -> Data {
        let object = try JSONSerialization.jsonObject(with: self)
        return try JSONSerialization.data(withJSONObject: (object as? [String: Any])?["data"] ?? [:])
    }
}
