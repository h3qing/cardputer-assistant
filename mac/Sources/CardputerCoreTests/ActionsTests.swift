import CardputerCore
import Foundation

private let id = "3QZK1M7-00042-0A1F"

private func result(_ name: String) throws -> FakeCpaClient.Reply {
    let data = try Fixture.data("filing-v1/vectors/valid/\(name).json").vectorData()
    return .output(CommandOutput(exitCode: name.hasPrefix("error") ? 1 : 0, stdout: data))
}

private func step(_ op: String, project: String? = nil) -> UndoStep {
    UndoStep(op: Open(raw: op), id: id, project: project)
}

private func entries() throws -> [InboxEntry] {
    [try makeEntry("0A1E"), try makeEntry("0A1F"), try makeEntry("0A20", filing: "done"), try makeEntry("0A21")]
}

let actionsTests = Suite(name: "Actions", language: .zh, cases: [
    test("this Mac's name goes after --, one short line of 1-24 UTF-8 bytes, never an option") {
        [
            expectEqual(try CpaCommand.hostName("公司").arguments, ["host", "name", "--json", "--", "公司"]),
            expectEqual(try CpaCommand.hostName(" 家 ").arguments, ["host", "name", "--json", "--", "家"]),
            expectEqual(try CpaCommand.hostName(String(repeating: "长", count: 8)).arguments.last?.count, 8, "24 bytes"),
            expectThrows("an option") { try CpaCommand.hostName("-rf") },
            expectThrows("empty") { try CpaCommand.hostName("  ") },
            expectThrows("a control character") { try CpaCommand.hostName("a\u{1B}b") },
            expectThrows("9 Chinese characters are 27 bytes") { try CpaCommand.hostName(String(repeating: "长", count: 9)) },
            expectThrows("too long") { try CpaCommand.hostName(String(repeating: "a", count: 25)) },
        ]
    },
    test("what the Edit menu's Undo names: the step just done") {
        [
            expectEqual(Feedback.actionName(.file(id: id, project: "cpa", other: false), projects: testProjects), "归到 3 Cardputer"),
            expectEqual(Feedback.actionName(.file(id: id, project: nil, other: false), projects: testProjects), "退回没项目"),
            expectEqual(Feedback.actionName(.hand(id: id, project: nil, spawn: false, other: false), projects: testProjects),
                        "交给 AI"),
            expectEqual(Feedback.actionName(.hand(id: id, project: nil, spawn: true, other: false), projects: testProjects),
                        "交给 AI 开工"),
            expectEqual(Feedback.actionName(.done(id: id), projects: testProjects), "看过了"),
            expectEqual(Feedback.actionName(.hide(id: id), projects: testProjects), "藏起来"),
        ]
    },
    test("each action is one cpa filing command") {
        [
            expectEqual(try InboxWorkflow.command(.file(id: id, project: "cpa", other: true)).arguments,
                        ["filing", "file", id, "cpa", "--other", "--json"]),
            expectEqual(try InboxWorkflow.command(.hand(id: id, project: nil, spawn: true, other: false)).arguments,
                        ["filing", "hand", id, "--spawn", "--json"]),
            expectEqual(try InboxWorkflow.command(.done(id: id)).arguments, ["filing", "done", id, "--json"]),
            expectEqual(try InboxWorkflow.command(.hide(id: id)).arguments, ["filing", "hide", id, "--json"]),
            expectThrows("a bad key never runs") { try InboxWorkflow.command(.file(id: id, project: "Bad Key", other: false)) },
        ]
    },
    test("the undo stack keeps the newest step on top, at most 50") {
        let stack = UndoStack.empty.pushing(step("file")).pushing(nil).pushing(step("reopen"))
        let many = (0..<60).reduce(UndoStack.empty) { stack, _ in stack.pushing(step("done")) }
        return [
            expectEqual(stack.top, step("reopen")),
            expectEqual(stack.popped().top, step("file")),
            expectEqual(stack.popped().popped().top, nil),
            expectEqual(stack.count, 2, "nil adds nothing"),
            expectEqual(many.count, 50),
            expectEqual(UndoStack.empty.popped(), UndoStack.empty),
        ]
    },
    test("filing: says where it went, offers ⌘Z, and selects the next new entry") {
        let command = try CpaCommand.file(id, project: "cpa", other: false)
        let fake = FakeCpaClient([command: try result("result_file")])
        let outcome = await InboxWorkflow.perform(.file(id: id, project: "cpa", other: false), entries: try entries(),
                                                  projects: testProjects, client: fake)
        return [
            expectEqual(outcome.toast, Toast(text: "已归到 3 Cardputer · ⌘Z 撤销", isError: false)),
            expectEqual(outcome.undo, step("file")),
            expectEqual(outcome.select, "3QZK1M7-00042-0A21"),
            expect(outcome.changed),
        ]
    },
    test("handing: says the queue id and where to start it") {
        let command = try CpaCommand.hand(id, project: nil, spawn: true, other: false)
        let fake = FakeCpaClient([command: try result("result_hand")])
        let outcome = await InboxWorkflow.perform(.hand(id: id, project: nil, spawn: true, other: false),
                                                  entries: try entries(), projects: testProjects, client: fake)
        return [
            expectEqual(outcome.toast.text, "已交给 AI · 排队号 q12 · 到设备看板上按 Enter 开工 · ⌘Z 撤销"),
            expectEqual(outcome.undo, step("reopen")),
        ]
    },
    test("nothing changed, or no undo: no ⌘Z offered") {
        let done = try CpaCommand.done(id)
        let same = FakeCpaClient([done: try result("result_unchanged")])
        let outcome = await InboxWorkflow.perform(.done(id: id), entries: try entries(), projects: testProjects, client: same)
        return [
            expectEqual(outcome.toast, Toast(text: "没有改：本来就是看过了", isError: false)),
            expectEqual(outcome.undo, nil),
            expectEqual(outcome.select, nil, "the selection stays"),
            expect(!outcome.changed),
        ]
    },
    test("a failure is said in place and the selection stays") {
        let hand = try CpaCommand.hand(id, project: nil, spawn: false, other: false)
        let older = FakeCpaClient([hand: try result("error_cpad_older")])
        let outcome = await InboxWorkflow.perform(.hand(id: id, project: nil, spawn: false, other: false),
                                                  entries: try entries(), projects: testProjects, client: older)
        return [
            expectEqual(outcome.toast, Toast(text: "cpad 比 cpa 老：在终端跑 cpa service install", isError: true)),
            expectEqual(outcome.select, nil),
            expectEqual(outcome.undo, nil),
        ]
    },
    test("a write that timed out may have been done: say so and refresh, offer no undo") {
        let hide = try CpaCommand.hide(id)
        let slow = FakeCpaClient([hide: .failure(.timedOut("cpa filing hide"))])
        let outcome = await InboxWorkflow.perform(.hide(id: id), entries: try entries(), projects: testProjects, client: slow)
        return [
            expectEqual(outcome.toast, Toast(text: "不确定有没有做成，已刷新", isError: true)),
            expect(outcome.changed, "the list is read again"),
            expectEqual(outcome.undo, nil),
            expectEqual(outcome.select, nil),
        ]
    },
    test("⌘Z runs the top step with the app's own verbs and selects that recording again") {
        let reopen = try CpaCommand.reopen(id)
        let fake = FakeCpaClient([reopen: try result("result_withdrawn")])
        let stack = UndoStack.empty.pushing(step("file")).pushing(step("reopen"))
        let (after, outcome) = await InboxWorkflow.undo(stack, client: fake)
        let (empty, nothing) = await InboxWorkflow.undo(.empty, client: fake)
        let (kept, unknown) = await InboxWorkflow.undo(UndoStack.empty.pushing(step("explode")), client: fake)
        return [
            expectEqual(after.top, step("file")),
            expectEqual(outcome.toast, Toast(text: "已撤销", isError: false)),
            expectEqual(outcome.select, id),
            expectEqual(empty, .empty),
            expectEqual(nothing.toast, Toast(text: "没有可以撤销的", isError: false)),
            expectEqual(kept, .empty, "a step this build cannot undo is dropped"),
            expectEqual(unknown.toast, Toast(text: "这一步不能撤销", isError: true)),
        ]
    },
])
