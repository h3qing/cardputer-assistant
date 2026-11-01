// What a click or a key does in the Inbox (docs/24 §2.1, feedback): one cpa filing command, then a short line
// that says the result ("Filed under 3 acmeshop · ⌘Z to undo"), the undo step to keep, and the entry to
// select next. A failure is said in place and the selection stays. ⌘Z rebuilds the result's undo with the
// app's own verbs.

public enum InboxAction: Hashable, Sendable {
    case file(id: String, project: String?, other: Bool)
    case hand(id: String, project: String?, spawn: Bool, other: Bool)
    case done(id: String)
    case hide(id: String)
    case reopen(id: String)

    public var id: String {
        switch self {
        case .file(let id, _, _), .hand(let id, _, _, _), .done(let id), .hide(let id), .reopen(let id): return id
        }
    }
}

public struct Toast: Hashable, Sendable {
    public let text: String
    public let isError: Bool
    /// Stays until it is closed (a text waiting on the clipboard), instead of 5 s.
    public let sticky: Bool

    public init(text: String, isError: Bool, sticky: Bool = false) {
        self.text = text
        self.isError = isError
        self.sticky = sticky
    }
}

public struct ActionOutcome: Hashable, Sendable {
    public let toast: Toast
    /// The step to push on the undo stack.
    public let undo: UndoStep?
    /// The entry to select now; nil keeps the selection.
    public let select: String?
    /// Something was written: refresh the list now.
    public let changed: Bool

    static func failed(_ text: String) -> ActionOutcome {
        ActionOutcome(toast: Toast(text: text, isError: true), undo: nil, select: nil, changed: false)
    }
}

/// The steps ⌘Z can take back, newest on top; at most 50.
public struct UndoStack: Hashable, Sendable {
    public static let limit = 50
    public static let empty = UndoStack(steps: [])

    public let steps: [UndoStep]

    public var top: UndoStep? {
        steps.last
    }

    public var count: Int {
        steps.count
    }

    public func pushing(_ step: UndoStep?) -> UndoStack {
        guard let step else { return self }
        return UndoStack(steps: Array((steps + [step]).suffix(Self.limit)))
    }

    public func popped() -> UndoStack {
        UndoStack(steps: Array(steps.dropLast()))
    }
}

public enum InboxWorkflow {
    public static func command(_ action: InboxAction) throws -> CpaCommand {
        switch action {
        case .file(let id, let project, let other): return try .file(id, project: project, other: other)
        case .hand(let id, let project, let spawn, let other): return try .hand(id, project: project, spawn: spawn, other: other)
        case .done(let id): return try .done(id)
        case .hide(let id): return try .hide(id)
        case .reopen(let id): return try .reopen(id)
        }
    }

    public static func perform(_ action: InboxAction, entries: [InboxEntry], projects: ProjectMap,
                               client: CpaClient) async -> ActionOutcome {
        do {
            let result = try await client.perform(try command(action))
            let toast = Toast(text: Feedback.text(action, result: result, projects: projects), isError: false)
            let select = result.changed ? entries.nextNew(after: action.id) : nil
            return ActionOutcome(toast: toast, undo: result.changed ? result.undo : nil, select: select,
                                 changed: result.changed)
        } catch CpaError.timedOut {
            // It may have been written after all: read the list again, offer no undo.
            return ActionOutcome(toast: Toast(text: Feedback.unsure, isError: true), undo: nil, select: nil, changed: true)
        } catch {
            return .failed(Feedback.failure(error))
        }
    }

    /// Takes the top step back. A step this build cannot rebuild is dropped; a failed one is kept to retry.
    public static func undo(_ stack: UndoStack, client: CpaClient) async -> (UndoStack, ActionOutcome) {
        guard let step = stack.top else {
            return (stack, ActionOutcome(toast: Toast(text: tr("没有可以撤销的"), isError: false), undo: nil, select: nil,
                                         changed: false))
        }
        do {
            let command = try CpaCommand.undo(step)
            _ = try await client.perform(command)
            return (stack.popped(), ActionOutcome(toast: Toast(text: tr("已撤销"), isError: false), undo: nil,
                                                  select: step.id, changed: true))
        } catch CommandError.unsupportedUndo {
            return (stack.popped(), .failed(tr("这一步不能撤销")))
        } catch {
            return (stack, .failed(Feedback.failure(error)))
        }
    }
}

/// The words of the feedback line.
public enum Feedback {
    public static var undoHint: String { tr("⌘Z 撤销") }
    public static var unsure: String { tr("不确定有没有做成，已刷新") }
    public static func text(_ action: InboxAction, result: FilingResult, projects: ProjectMap) -> String {
        guard result.changed else { return tr("没有改：本来就是") + Labels.filing(result.filing) }
        let said = done(action, result: result, projects: projects)
        return result.undo == nil ? said : said + " · " + undoHint
    }

    static func done(_ action: InboxAction, result: FilingResult, projects: ProjectMap) -> String {
        switch action {
        case .file(_, let project, _):
            return project.map { tr("已归到 ") + projects.label(key: $0) } ?? tr("已退回没项目")
        case .hand(_, _, let spawn, _):
            let qid = result.queue.map { tr(" · 排队号 ") + $0.qid } ?? ""
            return spawn ? tr("已交给 AI{0} · 到设备看板上按 Enter 开工", qid) : tr("已交给 AI，排进待办{0}", qid)
        case .done: return tr("看过了")
        case .hide: return tr("藏起来了")
        case .reopen: return tr("已重新打开")
        }
    }

    /// The step as the Edit menu's Undo names it: Undo “File under 3 Thesis”.
    public static func actionName(_ action: InboxAction, projects: ProjectMap) -> String {
        switch action {
        case .file(_, let project, _): return project.map { tr("归到 ") + projects.label(key: $0) } ?? tr("退回没项目")
        case .hand(_, _, let spawn, _): return spawn ? tr("交给 AI 开工") : tr("交给 AI")
        case .done: return tr("看过了")
        case .hide: return tr("藏起来")
        case .reopen: return tr("重新打开")
        }
    }

    public static func failure(_ error: Error) -> String {
        switch error {
        case let error as CpaError: return Labels.failure(error)
        case CommandError.unsupportedUndo: return tr("这一步不能撤销")
        case CommandError.invalid(argument: "name"): return Labels.refusal(FilingFailure(code: .badArg, field: "name"))
        default: return tr("这一步没做成：参数不对")
        }
    }
}
