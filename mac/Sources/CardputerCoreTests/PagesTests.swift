import CardputerCore
import Foundation

private let cal = Fixture.calendar
private let now = Fixture.now

private func status(_ name: String) throws -> CpaStatus {
    try CpaJSON.status(try Fixture.data("cpa/\(name).json"))
}

private func stt(_ fields: [String: Any]) throws -> SttStatus {
    try CpaJSON.stt(try JSONSerialization.data(withJSONObject: fields))
}

private func claim(_ app: String, beatAgo: TimeInterval) -> TranscriberClaim {
    let beat = ISO8601DateFormatter().string(from: now.addingTimeInterval(-beatAgo))
    return TranscriberClaim(app: app, pid: 1, since: nil, beatAt: beat)
}

private let everything = Capabilities(cpa: .available, filing: .available, projectList: .available,
                                      hostName: .missing, stt: .available)
private let today = Capabilities(cpa: .available, filing: .missing, projectList: .missing, hostName: .missing,
                                 stt: .missing)

let mcpTests = Suite(name: "Mcp", cases: [
    test("cpa mcp tools --json: name, title, read or write") {
        let data = Data(#"""
            [{"name":"recordings","title":"List recordings","annotations":{"readOnlyHint":true}},
             {"name":"file_memo","title":"File a memo","annotations":{"readOnlyHint":false},"x":1}]
            """#.utf8)
        let tools = try McpJSON.tools(data)
        return [
            expectEqual(tools.map(\.name), ["recordings", "file_memo"]),
            expectEqual(tools.map(\.write), [false, true]),
            expectThrows("not an array") { try McpJSON.tools(Data("{}".utf8)) },
        ]
    },
    test("cpa mcp audit --json: one call per line, the client, the tool, never content; bad lines skipped") {
        let lines = [
            #"{"at":"2026-09-16T14:20:00.000+08:00","event":"start","pid":1}"#,
            #"{"at":"2026-09-16T14:21:00.000+08:00","client":{"name":"alpha-one"},"method":"tools/call","tool":"recordings","error":null}"#,
            "not json",
            #"{"at":"2026-09-16T14:22:00.000+08:00","client":null,"method":"tools/call","tool":"file_memo","error":"denied"}"#,
        ]
        let calls = McpJSON.audit(Data(lines.joined(separator: "\n").utf8))
        return [
            expectEqual(calls.count, 3),
            expectEqual(calls[1].client, "alpha-one"),
            expectEqual(calls[1].what, "recordings"),
            expectEqual(calls[0].what, "start"),
            expectEqual(calls[2].error, "denied"),
        ]
    },
    test("a snippet's first line says where it goes; the rest is what to paste, without control characters") {
        let snippet = McpJSON.snippet(Data("# Somewhere: here\n[mcp]\u{1B}[31m\ncommand = \"/x/cpa\"\n\n".utf8))
        let added = McpJSON.snippet(Data("Add to ~/x.json (global):\n{}\n".utf8))
        return [
            expectEqual(added.where, "~/x.json (global)"),
            expectEqual(snippet.where, "Somewhere: here"),
            expectEqual(snippet.body, "[mcp] [31m\ncommand = \"/x/cpa\""),
        ]
    },
    test("every client's body pastes as it is: no instruction line, JSON that parses") {
        let dir = Fixture.url("shots/mcp")
        let names = try FileManager.default.contentsOfDirectory(atPath: dir.path).filter { $0.hasSuffix(".txt") }.sorted()
        let snippets = try names.map { McpJSON.snippet(try Data(contentsOf: dir.appendingPathComponent($0))) }
        return [expectEqual(names.count, 4), expectEqual(snippets.filter(\.isJSON).count, 2)]
            + zip(names, snippets).flatMap { name, snippet -> [Check] in
                let first = snippet.body.split(separator: "\n").first.map(String.init) ?? ""
                let parsed = try? JSONSerialization.jsonObject(with: Data(snippet.body.utf8))
                return [
                    expect(!first.hasPrefix("#") && !first.hasPrefix("Add to"), "\(name): \(first)"),
                    expect(!(snippet.where ?? "").isEmpty, "\(name): where"),
                    expect(!snippet.isJSON || parsed != nil, "\(name): the body is not JSON"),
                ]
            }
    },
    test("any other MCP client: stdio, the absolute cpa, mcp serve, CPA_HOME only when set") {
        let plain = McpJSON.generic(cpaPath: "/Users/you/.local/bin/cpa", cpaHome: nil)
        let homed = McpJSON.generic(cpaPath: "/Users/you/.local/bin/cpa", cpaHome: "/tmp/cpa home")
        let doc = { (body: String) in
            ((try? JSONSerialization.jsonObject(with: Data(body.utf8))) as? [String: Any]).flatMap {
                ($0["mcpServers"] as? [String: Any])?["cardputer"] as? [String: Any]
            } ?? [:]
        }
        return [
            expectEqual(doc(plain.body)["command"] as? String, "/Users/you/.local/bin/cpa"),
            expectEqual(doc(plain.body)["args"] as? [String], ["mcp", "serve"]),
            expectEqual(doc(plain.body)["type"] as? String, "stdio"),
            expect(doc(plain.body)["env"] == nil),
            expectEqual((doc(homed.body)["env"] as? [String: String])?["CPA_HOME"], "/tmp/cpa home"),
            expect(plain.isJSON),
        ]
    },
    test("cpa's list of MCP clients; tabs named from the id unless cpa gives a label") {
        let data = Data(#"["alpha-one", {"id": "beta", "label": "Beta 2"}, {"id": "--x"}, {"id": "Bad"}]"#.utf8)
        let listed = try McpClients.parse(data)
        return [
            expectEqual(listed.map(\.id), ["alpha-one", "beta"]),
            expectEqual(listed.map(\.label), ["Alpha One", "Beta 2"]),
            expectEqual(McpClients.generic.id, "generic"),
            expectThrows("not a list") { try McpClients.parse(Data("{}".utf8)) },
        ]
    },
])

let pagesTests = Suite(name: "Pages", language: .zh, cases: [
    test("integrations: where spoken words go (not yet), then five ways for developers to read") {
        let ready = IntegrationCatalog.cards(features: everything, transcriber: claim("cpa-stt", beatAgo: 30), now: now,
                                             cpaPath: "/Users/me/.local/bin/cpa")
        let early = IntegrationCatalog.cards(features: today, transcriber: nil, now: now, cpaPath: nil)
        let byKind = Dictionary(uniqueKeysWithValues: early.map { ($0.kind, $0) })
        let cli = Dictionary(uniqueKeysWithValues: (byKind[.cli]?.commands ?? []).map { ($0.text, $0.readiness) })
        let readyCli = ready.first { $0.kind == .cli }?.commands ?? []
        return [
            expectEqual(ready.map(\.kind), [.destinations, .mcp, .cli, .files, .transcriber, .export]),
            expectEqual(ready.filter(\.forDevelopers).map(\.kind), [.mcp, .cli, .files, .transcriber, .export]),
            expectEqual(ready.first?.readiness, .needs("要先有 cpa dest（R1）")),
            expect(ready.dropFirst().allSatisfy { $0.readiness == .ready }),
            expectEqual(byKind[.cli]?.readiness, .ready, "status and rec ls work today"),
            expectEqual(cli["cpa status --json"], .ready),
            expectEqual(cli["cpa rec ls --json"], .ready),
            expectEqual(cli["cpa rec show SID --json"], .ready),
            expectEqual(cli["cpa filing ls --json"], .needs("装好 cpa filing 后")),
            expectEqual(cli["cpa project ls --json"], .needs("装好 cpa filing 后")),
            expectEqual(byKind[.transcriber]?.readiness, .needs("要先装 cpa-stt 或别的转写程序")),
            expectEqual(readyCli.first?.text, "/Users/me/.local/bin/cpa status --json", "the absolute path, ready to paste"),
            expect(!early.flatMap { [$0.title, $0.sentence] + $0.lines }.joined().contains("docs/"), "no docs/ in the words"),
        ]
    },
    test("the files and transcriber cards name every open file and the contract") {
        let cards = IntegrationCatalog.cards(features: everything, transcriber: nil, now: now, cpaPath: nil)
        let files = cards.first { $0.kind == .files }?.lines.joined(separator: "\n") ?? ""
        let transcriber = cards.first { $0.kind == .transcriber }?.lines.joined(separator: "\n") ?? ""
        return ["manifest.json", "seg-NNN.wav", "transcript.json", "transcript.md", "result.json", "memo.text",
                "summary.md", "recordings/.transcriber", "protocol/recording/v1/README.md"].map { word in
            expect(files.contains(word), "files card lacks \(word)")
        } + [expect(transcriber.contains("protocol/recording/v1/README.md"), "transcriber card lacks the contract")]
    },
    test("a brief to paste into any app's coding agent: path, commands, rules, contracts, MCP") {
        let brief = IntegrationCatalog.brief(features: everything, cpaPath: "/Users/me/.local/bin/cpa", cpaHome: nil,
                                             notes: "~/CardputerNotes")
        return ["/Users/me/.local/bin/cpa status --json", "参数数组", "stdin", "protocol/filing/v1/README.md",
                "protocol/recording/v1/README.md", #""args""#, "mcp", "~/CardputerNotes"].map { word in
            expect(brief.contains(word), "the brief lacks \(word)")
        } + [expect(brief.hasPrefix("# "), "Markdown")]
    },
    test("who transcribes: the claim, or nobody, or a stale claim") {
        [
            expectEqual(IntegrationCatalog.transcriberLine(claim("cpa-stt", beatAgo: 30), now: now), "cpa-stt 正在转写"),
            expectEqual(IntegrationCatalog.transcriberLine(claim("cpa-stt", beatAgo: 301), now: now),
                        "cpa-stt 认领过，但 5 分钟没有心跳"),
            expectEqual(IntegrationCatalog.transcriberLine(nil, now: now), "现在没有转写程序"),
        ]
    },
    test("the local transcription switch") {
        let on = try stt(["on": true])
        let used = try stt(["on": false, "selftest_ok": true])
        let never = try stt(["on": false, "missing": [String]()])
        let noModel = try stt(["on": false, "blocked": "model_missing", "selftest_ok": false])
        let linkGone = try stt(["on": false, "missing": ["/Users/me/Library/Application Support/x/models/ggml.bin"],
                                "selftest_ok": true])
        let other = claim("othernotes", beatAgo: 20)
        return [
            expectEqual(SttSwitch.of(features: today, stt: nil, claim: nil, now: now), .unavailable("装好 cpa-stt 才能本地转写")),
            expectEqual(SttSwitch.of(features: everything, stt: on, claim: claim("cpa-stt", beatAgo: 5), now: now), .on),
            expectEqual(SttSwitch.of(features: everything, stt: used, claim: nil, now: now), .off(model: .ok)),
            expectEqual(SttSwitch.of(features: everything, stt: never, claim: nil, now: now), .off(model: .firstTime),
                        "never turned on: no config yet, so no word on the model either"),
            expectEqual(SttSwitch.of(features: everything, stt: noModel, claim: nil, now: now), .off(model: .missing)),
            expectEqual(SttSwitch.of(features: everything, stt: linkGone, claim: nil, now: now), .off(model: .missing),
                        "missing holds absolute paths"),
            expectEqual(SttSwitch.off(model: .firstTime).reason, "第一次开：先选一个模型文件（whisper 的 .bin）"),
            expectEqual(SttSwitch.off(model: .missing).reason, "缺模型：先选一个模型文件"),
            expectEqual(SttSwitch.of(features: everything, stt: used, claim: other, now: now),
                        .taken("othernotes 正在转写，两个只能开一个")),
            expectEqual(SttSwitch.of(features: everything, stt: used, claim: claim("othernotes", beatAgo: 900), now: now),
                        .off(model: .ok), "a stale claim does not block"),
        ]
    },
    test("device facts in Chinese; never the device's own id") {
        let late = Date(timeIntervalSince1970: 1_792_641_600)  // 2026-10-22 12:00 +08:00
        let facts = DeviceFacts.facts(status: try status("status-later"), lastPull: "2026-10-22T11:50:00.000+08:00",
                                      now: late, calendar: cal)
        let down = DeviceFacts.facts(status: try status("status-offline"), lastPull: nil, now: now, calendar: cal)
        let text = facts.map { "\($0.label)=\($0.value)" }
        let linked = DeviceFacts.facts(status: try status("status-running"), lastPull: nil, now: now, calendar: cal)
        return [
            expectEqual(text, ["上次看到设备上有=2 段", "等转写=19 段", "转写失败=2 段", "待发给设备=0 条",
                               "最近取回=10 分钟前", "最近转写=昨天 14:50", "存储=SD 卡", "cpad 版本=0.1.0"],
                        "paused: what the device had when last seen"),
            expectEqual(linked.first?.label, "设备上还有"),
            expectEqual(facts.first { $0.label == "转写失败" }?.kind, .failed, "the tile that offers a retry"),
            expectEqual(down.first?.value, "—（cpad 没在跑）"),
            expect(!text.joined().contains("02005e100001") && !text.joined().contains("02:00:5e")),
        ]
    },
    test("the menu: the dot, the number, the first line and five new entries") {
        let page = InboxPage(source: .filing(FilingCounts(new: 7, otherMac: 1, clipsWaiting: 0, onDevice: 2)),
                             entries: try (1...7).map { try makeEntry(String(format: "0B%02d", $0)) }, nextCursor: nil)
        let menu = MenuSummary.make(status: try status("status-later"), page: page, cpaFound: true, fresh: true)
        let fallback = MenuSummary.make(status: try status("status-running"),
                                        page: InboxPage(source: .fallback, entries: [], nextCursor: nil), cpaFound: true,
                                        fresh: true)
        let missing = MenuSummary.make(status: nil, page: nil, cpaFound: false, fresh: true)
        let old = MenuSummary.make(status: try status("status-running"), page: page, cpaFound: true, fresh: false)
        return [
            expectEqual(menu.light, .unlinked),
            expectEqual(menu.count, 8),
            expectEqual(menu.headline, "设备暂停 43 秒 · 上次看到设备上有 2 段"),
            expectEqual(menu.recent.count, 5),
            expectEqual(fallback.count, nil, "the read-only list has no number"),
            expectEqual(fallback.headline, "设备连着", "no firmware version in the menu"),
            expectEqual(missing.light, .down),
            expectEqual(missing.headline, "没找到 cpa"),
            expectEqual(old.light, .unknown, "an answer older than 90 s, or a failing cpa status, is not trusted"),
            expectEqual(old.headline, "不确定：cpa status 一阵子没回答了"),
        ]
    },
])
