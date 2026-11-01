// Sending from the Mac (docs/25 §5.4, §5.7): the cpa dest commands as argument arrays, which destinations
// Send to… offers and in what order, ⌥ + a letter, the words after a send, the one-time test's questions, and
// the workflow against a fake cpa. No app is named: the destinations here are made up.

import CardputerCore
import Foundation

func makeDest(_ id: String, key: String? = nil, slot: Int = 5, state: String = "on", leaves: Bool = true,
              fl: String? = "lc", label: String? = nil, verified: String? = nil) -> DestEntry {
    let kind = String(id.split(separator: ".").first ?? "")
    return DestEntry(id: id, kind: kind, label: label ?? id, key: key, slot: slot, state: state, leavesMac: leaves,
                     fl: fl, effect: kind == "url" ? "draft" : nil, verified: verified, preset: kind == "url" ? "bot" : nil)
}

private let dests = [
    makeDest("daily", slot: 1, leaves: false, fl: ""),
    makeDest("clip", key: "c", slot: 2, leaves: false, fl: "l"),
    makeDest("file.sb", key: "f", slot: 3, leaves: false, fl: ""),
    makeDest("app.notes", key: "n", slot: 4, fl: "lc"),
    makeDest("url.bot", key: "x", slot: 6, fl: "lc", label: "Bot", verified: "draft"),
    makeDest("url.raw", key: "y", slot: 7, fl: "lck", label: "Raw"),
    makeDest("url.off", key: "o", slot: 8, state: "off"),
    makeDest("paste", key: "p", slot: 9, fl: "lk"),
]

private func reply(_ exit: Int32, _ text: String) -> FakeCpaClient.Reply {
    .output(CommandOutput(exitCode: exit, stdout: Data(text.utf8)))
}

private let ref = "3QZK1M7-00042-0A01"
private let bot = DestTestTarget(dest: "url.bot", label: "Bot", kind: "url", effect: "draft", takesMap: true)

private func sent(_ outcome: String, via: String?, why: String? = nil, code: String? = nil) -> String {
    let v = via.map { "\"\($0)\"" } ?? "null", w = why.map { "\"\($0)\"" } ?? "null", c = code.map { "\"\($0)\"" } ?? "null"
    return #"{"ok":true,"v":1,"ref":"\#(ref)","dest":"url.bot","label":"Bot","kind":"url","outcome":"\#(outcome)","via":\#(v),"why":\#(w),"code":\#(c),"leaves_mac":true}"#
}

let destCommandTests = Suite(name: "DestCommands", cases: [
    test("send, file send and drop: ids and destination ids only, said to be by the app") {
        [
            expectEqual(try CpaCommand.destSend(ref, to: nil).arguments, ["dest", "send", ref, "--by", "app", "--json"]),
            expectEqual(try CpaCommand.destSend(ref, to: "url.bot").arguments,
                        ["dest", "send", ref, "url.bot", "--by", "app", "--json"]),
            expectEqual(try CpaCommand.filingSend(ref.lowercased(), to: "clip").arguments,
                        ["filing", "send", ref, "clip", "--by", "app", "--json"]),
            expectEqual(try CpaCommand.destDrop(ref).arguments, ["dest", "drop", ref, "--by", "app", "--json"]),
            expectEqual(try CpaCommand.destSend(ref, to: nil).name, "cpa dest send"),
            expectEqual(try CpaCommand.destSend(ref, to: nil).timeout, 30),
            expectThrows("a ref with a space") { try CpaCommand.destSend("a b", to: nil) },
            expectThrows("a ref starting with -") { try CpaCommand.destDrop("-x") },
            expectThrows("a bad destination id") { try CpaCommand.destSend(ref, to: "URL.Bot") },
            expectThrows("a name over 12") { try CpaCommand.filingSend(ref, to: "url.abcdefghijklm") },
            expectThrows("not a recording id") { try CpaCommand.filingSend("note-1", to: "clip") },
        ]
    },
    test("--again: only for a send, once; add takes the chat's link for every project") {
        let send = try CpaCommand.destSend(ref, to: nil)
        return [
            expectEqual(send.again()?.arguments, ["dest", "send", ref, "--again", "--by", "app", "--json"]),
            expectEqual(send.again()?.again(), nil),
            expectEqual(try CpaCommand.destDrop(ref).again(), nil),
            expectEqual(try CpaCommand.destAdd(preset: "bot-x", link: " https://e.invalid/c/1 ").arguments,
                        ["dest", "add", "--preset", "bot-x", "--yes", "--map", "*=https://e.invalid/c/1"]),
            expectThrows("a link with a space") { try CpaCommand.destAdd(preset: "bot-x", link: "a b") },
        ]
    },
    test("the Destinations card's commands: ls, presets, usual, add with --yes, test, verify, map") {
        [
            expectEqual(CpaCommand.destList.arguments, ["dest", "ls", "--json"]),
            expectEqual(CpaCommand.destPresets.arguments, ["dest", "presets", "--installed", "--json"]),
            expectEqual(CpaCommand.destPending.arguments, ["dest", "pending", "--json"]),
            expectEqual(CpaCommand.destUsual.arguments, ["dest", "usual", "--json"]),
            expectEqual(try CpaCommand.setUsual("cpa", to: "url.bot").arguments, ["dest", "usual", "cpa", "url.bot", "--json"]),
            expectEqual(try CpaCommand.setUsual("cpa", to: nil).arguments, ["dest", "usual", "cpa", "-", "--json"]),
            expectEqual(try CpaCommand.destAdd(preset: "bot").arguments, ["dest", "add", "--preset", "bot", "--yes"]),
            expectEqual(try CpaCommand.destTest("url.bot").arguments, ["dest", "test", "url.bot", "--send", "--json"]),
            expectEqual(try CpaCommand.destVerify("url.bot", draft: true).arguments, ["dest", "verify", "url.bot", "--draft"]),
            expectEqual(try CpaCommand.destVerify("url.bot", draft: false).arguments, ["dest", "verify", "url.bot", "--act"]),
            expectEqual(try CpaCommand.destMap("url.bot", link: " scheme://app/v1/agent?id=Ab_1 ").arguments,
                        ["dest", "map", "url.bot", "*=scheme://app/v1/agent?id=Ab_1"]),
            expectThrows("a bad preset") { try CpaCommand.destAdd(preset: "--yes") },
            expectThrows("a bad project") { try CpaCommand.setUsual("Cpa", to: nil) },
            expectThrows("an empty link") { try CpaCommand.destMap("url.bot", link: "  ") },
            expectThrows("a link with a space") { try CpaCommand.destMap("url.bot", link: "a b") },
            expectThrows("a link with a control character") { try CpaCommand.destMap("url.bot", link: "a\u{7}b") },
            expectThrows("a link starting with -") { try CpaCommand.destMap("url.bot", link: "-x") },
            expectThrows("a link over 512") { try CpaCommand.destMap("url.bot", link: String(repeating: "a", count: 513)) },
        ]
    },
])

let destChoiceTests = Suite(name: "DestChoices", language: .zh, cases: [
    test("Send to… lists what the Mac can send to: the preselected first, then links, apps, files, the clipboard") {
        let entry = try makeEntry("0A01")
        let choices = DestChoices.forEntry(entry, dests: dests, usual: nil)
        let usual = DestChoices.forEntry(entry, dests: dests, usual: "file.sb")
        return [
            expectEqual(choices.map(\.id), ["url.bot", "url.raw", "app.notes", "file.sb", "clip"]),
            expectEqual(usual.map(\.id).first, "file.sb"),
            expect(!choices.map(\.id).contains("daily"), "AI and the daily note are not 送到 (cpa filing hand)"),
            expect(!choices.map(\.id).contains("url.off"), "an off destination is not offered"),
            expect(!choices.map(\.id).contains("paste"), "paste has no executor yet"),
        ]
    },
    test("a destination that may send by itself is grey and says what to do; the others are not") {
        let choices = DestChoices.forEntry(try makeEntry("0A01"), dests: dests, usual: nil)
        let raw = choices.first { $0.id == "url.raw" }
        return [expect(raw?.blocked?.contains("测试") == true, "\(raw?.blocked ?? "nil")"),
                expectEqual(choices.first { $0.id == "url.bot" }?.blocked, nil),
                expect(DestChoices.mayAct(makeDest("app.x", fl: nil)), "an unreadable fl is lck")]
    },
    test("a meeting has no Send to…; a memo without words is grey; the read-only list has none") {
        let meeting = try makeEntry("0A02", kind: "meeting")
        let silent = DestChoices.forEntry(try makeEntry("0A03", text: nil), dests: dests, usual: nil)
        return [expect(DestChoices.forEntry(meeting, dests: dests, usual: nil).isEmpty),
                expect(silent.allSatisfy { $0.blocked == DestChoices.noWords })]
    },
    test("⌥ + a letter picks the destination bound to it; a grey or missing one says why") {
        let choices = DestChoices.forEntry(try makeEntry("0A01"), dests: dests, usual: nil)
        return [
            expectEqual(DestChoices.pick("x", in: choices), .send("url.bot")),
            expectEqual(DestChoices.pick("X", in: choices), .send("url.bot")),
            expect({ if case .blocked = DestChoices.pick("y", in: choices) { return true }; return false }()),
            expectEqual(DestChoices.pick("q", in: choices), .blocked("没有字母 Q 的去向")),
            expectEqual(DestChoices.letters(choices), ["x", "y", "n", "f", "c"]),
        ]
    },
    test("a pending row offers the same list with its own destination first") {
        let row = try DestContract.pending(Data(#"{"ok":true,"v":1,"rows":[{"ref":"R1","why":"usual","dest":"clip","label":"剪贴板","kind":"clip","fl":"l","st":null,"code":null,"project":"cpa","src":"voice","at":"2026-10-22T09:00:00Z","text":"t"}]}"#.utf8)).rows[0]
        return [expectEqual(DestChoices.forPending(row, dests: dests).first?.id, "clip"),
                expectEqual(DestChoices.forPending(row, dests: dests).count, 5)]
    },
    test("the Destinations card says each destination's state in words") {
        [
            expectEqual(DestChoices.state(makeDest("url.bot", fl: "lc", verified: "draft")), "只填草稿 · 测过了"),
            expectEqual(DestChoices.state(makeDest("url.raw", fl: "lck")), "还没测：先送一条测试，测过才能从 Mac 送"),
            expectEqual(DestChoices.state(makeDest("url.act", fl: "lck", verified: "act")), "会自己发出去：这台 Mac 不能送它"),
            expectEqual(DestChoices.state(makeDest("paste", fl: "lk")), "会自己发出去：这台 Mac 不能送它"),
            expectEqual(DestChoices.state(makeDest("app.notes", fl: "lc")), "剪贴板 + 调到前面，你按 ⌘V"),
            expectEqual(DestChoices.state(makeDest("url.off", state: "off")), "关着"),
            expectEqual(DestChoices.state(makeDest("url.new", state: "needs_optin")), "还没允许离开这台 Mac"),
        ]
    },
])

let destFlowTests = Suite(name: "DestFlow", language: .zh, cases: [
    test("the words after a send come from cpa's document: link, clipboard and why, a file, a failure") {
        let url = DestFeedback.sent(try DestContract.sent(Data(sent("handed", via: "url").utf8)))
        let busy = DestFeedback.sent(try DestContract.sent(Data(sent("handed", via: "clip", why: "draft_busy").utf8)))
        let long = DestFeedback.sent(try DestContract.sent(Data(sent("handed", via: "clip", why: "too_long").utf8)))
        let failed = DestFeedback.sent(try DestContract.sent(Data(sent("failed", via: nil, code: "E_DEST_MISSING").utf8)))
        let done = DestFeedback.sent(try DestContract.sent(Data(sent("delivered", via: nil).utf8)))
        return [
            expectEqual(url, Toast(text: "已填进 Bot · 到 Bot 里按回车发出", isError: false)),
            expectEqual(busy, Toast(text: "在剪贴板 · 2 分钟内到 Bot 按 ⌘V（Bot 里还有上一条草稿）", isError: false, sticky: true)),
            expectEqual(long.text, "在剪贴板 · 2 分钟内到 Bot 按 ⌘V（太长，链接放不下）"),
            expect(!url.sticky && !done.sticky, "only the clipboard line stays"),
            expect(failed.isError && failed.text.contains("Bot 不在这台 Mac 上了"), failed.text),
            expectEqual(done, Toast(text: "已送到 Bot", isError: false)),
        ]
    },
    test("a refusal says what to do; an unknown code is shown as it is") {
        [
            expect(DestFeedback.refusal(DestFailure(code: "E_NEEDS_DEVICE", field: "dest")).contains("送一条测试")),
            expect(!DestFeedback.refusal(DestFailure(code: "E_NEEDS_DEVICE", field: "dest")).contains("设备上确认")),
            expect(DestFeedback.refusal(DestFailure(code: "E_ALREADY", field: "dest")).contains("再按一次")),
            expect(DestFeedback.refusal(DestFailure(code: "E_NO_TEXT", field: "text")).contains("cpa-stt")),
            expect(DestFeedback.refusal(DestFailure(code: "E_BUSY", field: "ref")).contains("正在送")),
            expectEqual(DestFeedback.refusal(DestFailure(code: "E_LATER", field: "dest")), "没送：E_LATER"),
            expect(Labels.failure(.refusedDest(DestFailure(code: "E_DEST_OFF", field: "dest"))).contains("关着")),
        ]
    },
    test("the one-time test of a link: appeared? then sent by itself? each answer is one cpa dest verify") {
        let tested = try DestContract.tested(Data(#"{"ok":true,"v":1,"dest":"url.bot","marker":"4821","handed":true,"via":"url","code":null}"#.utf8))
        let asked = DestTestFlow.afterSend(tested, target: bot)
        let (second, none) = DestTestFlow.answer(asked, yes: true)
        let (draft, verify) = DestTestFlow.answer(second, yes: false)
        let (act, verifyAct) = DestTestFlow.answer(second, yes: true)
        return [
            expectEqual(asked, .appeared(bot, marker: "4821")),
            expectEqual(DestTestFlow.question(asked), "Bot 里出现「测试 4821」了吗？"),
            expectEqual(DestTestFlow.question(second), "你没按回车，它自己发出去了吗？"),
            expectEqual(DestTestFlow.hint(asked), "测完把 Bot 输入框里的「测试 4821」删掉，别按回车。"),
            expectEqual(none, nil),
            expectEqual(verify?.arguments, ["dest", "verify", "url.bot", "--draft"]),
            expectEqual(verifyAct?.arguments, ["dest", "verify", "url.bot", "--act"]),
            expect({ if case .finished(let toast) = draft { return toast.text.contains("只填草稿") }; return false }()),
            expect({ if case .finished(let toast) = act { return toast.text.contains("这台 Mac 不送它") }; return false }()),
        ]
    },
    test("an app is only asked whether it came forward; your own link preset never becomes a draft") {
        let notes = DestTestTarget(dest: "app.notes", label: "N", kind: "app")
        let (appOK, appCommand) = DestTestFlow.answer(.appeared(notes, marker: "1000"), yes: true)
        let mine = DestTestTarget(dest: "url.mine", label: "M", kind: "url", effect: "act")
        let (notSent, noVerify) = DestTestFlow.answer(.sentByItself(mine, marker: "1000"), yes: false)
        let (sentMine, actVerify) = DestTestFlow.answer(.sentByItself(mine, marker: "1000"), yes: true)
        return [
            expect({ if case .finished(let toast) = appOK { return toast.text.contains("⌘V") }; return false }()),
            expectEqual(appCommand, nil), expectEqual(DestTestFlow.hint(.appeared(notes, marker: "1000")), nil),
            expect(DestTestFlow.question(.appeared(notes, marker: "1000"))?.contains("⌘V") == true),
            expect({ if case .finished(let toast) = notSent { return toast.text.contains("不送它") }; return false }()),
            expectEqual(noVerify, nil),
            expect({ if case .finished = sentMine { return true }; return false }()),
            expectEqual(actVerify?.arguments, ["dest", "verify", "url.mine", "--act"]),
        ]
    },
    test("nothing appeared: only a link that takes a chat asks for its link; the others say what to check") {
        let tested = try DestContract.tested(Data(#"{"ok":true,"v":1,"dest":"url.bot","marker":"4821","handed":true,"via":"url","code":null}"#.utf8))
        let (link, _) = DestTestFlow.answer(DestTestFlow.afterSend(tested, target: bot), yes: false)
        let plain = DestTestTarget(dest: "url.plain", label: "P", kind: "url", effect: "draft")
        let (noMap, noCommand) = DestTestFlow.answer(.appeared(plain, marker: "1000"), yes: false)
        let (appGone, _) = DestTestFlow.answer(.appeared(DestTestTarget(dest: "app.notes", label: "N", kind: "app"), marker: "1000"), yes: false)
        let notHanded = DestTestFlow.afterSend(try DestContract.tested(Data(#"{"ok":true,"v":1,"dest":"url.bot","marker":"4821","handed":false,"via":null,"code":"E_DEST_MISSING"}"#.utf8)), target: bot)
        let busy = DestTestFlow.afterSend(try DestContract.tested(Data(#"{"ok":true,"v":1,"dest":"url.bot","marker":"4821","handed":false,"via":null,"code":"E_DRAFT_BUSY"}"#.utf8)), target: bot)
        return [
            expectEqual(link, .link(bot)),
            expect(DestTestFlow.question(link)?.contains("链接") == true),
            expect(DestTestFlow.question(link)?.contains("Bot 的") == false, "the word comes from the label only"),
            expect({ if case .finished(let toast) = noMap { return toast.text.contains("登录") }; return false }()),
            expectEqual(noCommand, nil),
            expect({ if case .finished(let toast) = appGone { return !toast.isError }; return false }()),
            expect({ if case .finished(let toast) = notHanded { return toast.isError }; return false }()),
            expect({ if case .finished(let toast) = busy { return toast.text.contains("清空") }; return false }()),
            expectEqual(DestTestFlow.answer(.idle, yes: true).0, .idle),
        ]
    },
    test("every failure code cpa dest answers has words and a next step") {
        let codes = ["E_DEST_MISSING", "E_HANDLER_CHANGED", "E_CLIP_PLAIN", "E_DRAFT_BUSY", "E_TOO_LONG", "E_DEST_TIMEOUT",
                     "E_INTERRUPTED", "E_BAD_TEXT", "E_DEST_IO", "E_DEST_PATH", "E_INTERNAL"]
        return codes.map { code in expect(DestFeedback.code(code) != code, code) }
            + [expectEqual(DestFeedback.code("E_LATER"), "E_LATER")]
    },
    test("the workflow: a send runs one command, says the result and asks for a fresh list") {
        let command = try CpaCommand.destSend(ref, to: nil)
        let ok = await DestWorkflow.send(command, client: FakeCpaClient([command: reply(0, sent("handed", via: "url"))]))
        let failed = await DestWorkflow.send(command, client: FakeCpaClient([command: reply(1, sent("failed", via: nil, code: "E_X"))]))
        let refused = await DestWorkflow.send(command, client: FakeCpaClient([command: reply(1, #"{"ok":false,"v":1,"error":"E_NEEDS_DEVICE","field":"dest"}"#)]))
        let slow = await DestWorkflow.send(command, client: FakeCpaClient([command: .failure(.timedOut("cpa dest send"))]))
        return [
            expectEqual(ok.toast.text, "已填进 Bot · 到 Bot 里按回车发出"), expect(ok.changed), expectEqual(ok.undo, nil),
            expect(failed.toast.isError && failed.changed, "a failed send stays in pending: refresh"),
            expect(refused.toast.isError && !refused.changed && refused.toast.text.contains("测试"), refused.toast.text),
            expect(slow.changed && slow.toast.isError),
        ]
    },
    test("already sent there: the next press sends it again; a clipboard hand-off offers Copy again") {
        let command = try CpaCommand.filingSend(ref, to: "url.bot")
        let already = await DestWorkflow.send(command, client: FakeCpaClient([command: reply(1, #"{"ok":false,"v":1,"error":"E_ALREADY","field":"dest"}"#)]))
        let clip = await DestWorkflow.send(command, client: FakeCpaClient([command: reply(0, sent("handed", via: "clip", why: "draft_busy"))]))
        let link = await DestWorkflow.send(command, client: FakeCpaClient([command: reply(0, sent("handed", via: "url"))]))
        return [
            expectEqual(already.again?.arguments, ["filing", "send", ref, "url.bot", "--again", "--by", "app", "--json"]),
            expect(!already.toast.isError && !already.changed && already.toast.text.contains("再按一次"), already.toast.text),
            expectEqual(clip.recopy?.arguments, ["dest", "send", ref, "url.bot", "--again", "--by", "app", "--json"]),
            expect(clip.toast.sticky), expectEqual(link.recopy, nil), expectEqual(link.again, nil),
        ]
    },
    test("the workflow: drop, the lists, a test send and a plain command") {
        let drop = try CpaCommand.destDrop(ref)
        let fake = FakeCpaClient([
            drop: reply(0, #"{"ok":true,"v":1,"ref":"\#(ref)","dest":"url.bot"}"#),
            .destPending: reply(0, #"{"ok":true,"v":1,"rows":[]}"#),
            .destUsual: reply(0, #"{"ok":true,"v":1,"usual":[{"project":"cpa","dest":"url.bot"}]}"#),
            .destList: reply(0, #"{"problem":null,"dests":[]}"#),
            .destPresets: reply(0, #"{"presets":[],"problems":[]}"#),
            try .destTest("url.bot"): reply(0, #"{"ok":true,"v":1,"dest":"url.bot","marker":"4821","handed":true,"via":"url","code":null}"#),
            try .destVerify("url.bot", draft: true): reply(0, "url.bot: fills a draft only\n"),
        ])
        let dropped = await DestWorkflow.drop(drop, client: fake)
        let step = await DestWorkflow.test(bot, client: fake)
        let plain = await DestWorkflow.plain(try .destVerify("url.bot", draft: true), client: fake)
        let failedPlain = await DestWorkflow.plain(try .destVerify("url.x", draft: true), client: fake)
        return [
            expectEqual(dropped.toast.text, "不送了"), expect(dropped.changed),
            expectEqual(try await fake.destPending().rows.count, 0),
            expectEqual(try await fake.destUsual().usual.first?.dest, "url.bot"),
            expectEqual(try await fake.destCatalog().dests.count, 0),
            expectEqual(try await fake.destPresets().presets.count, 0),
            expectEqual(step, .appeared(bot, marker: "4821")),
            expectEqual(plain, nil), expect(failedPlain != nil),
        ]
    },
    test("Integrations: the Destinations card is ready once cpa dest answers, and the command line card lists its commands") {
        let can = Capabilities(cpa: .available, filing: .available, projectList: .available, hostName: .available,
                               stt: .available, dest: .available)
        let cannot = Capabilities(cpa: .available, filing: .available, projectList: .available, hostName: .available,
                                  stt: .available)
        let ready = IntegrationCatalog.cards(features: can, transcriber: nil, now: Fixture.now, cpaPath: "/u/bin/cpa")
        let early = IntegrationCatalog.cards(features: cannot, transcriber: nil, now: Fixture.now, cpaPath: nil)
        let cli = Dictionary(uniqueKeysWithValues: (ready.first { $0.kind == .cli }?.commands ?? []).map { ($0.text, $0.readiness) })
        let earlyCli = early.first { $0.kind == .cli }?.commands ?? []
        return [
            expectEqual(ready.first?.readiness, .ready), expect(can.canSend), expect(!cannot.canSend),
            expectEqual(early.first?.readiness, .needs("要先有 cpa dest（R1）")),
            expectEqual(cli["/u/bin/cpa dest pending --json"], .ready),
            expectEqual(cli["/u/bin/cpa dest send REF --json"], .ready),
            expectEqual(earlyCli.first { $0.text == "cpa dest pending --json" }?.readiness, .needs("装好 cpa dest 后")),
        ]
    },
    test("the feature probe: cpa dest pending answering means the Mac can send") {
        let fake = FakeCpaClient([.version: reply(0, "cpa 0.4.2\n"), FeatureProbe.destProbe: reply(0, #"{"ok":true,"v":1,"rows":[]}"#)])
        let old = FakeCpaClient([.version: reply(0, "cpa 0.4.1\n")])
        return [expectEqual(await FeatureProbe.probe(fake).dest, .available),
                expectEqual(await FeatureProbe.probe(old).dest, .missing)]
    },
])
