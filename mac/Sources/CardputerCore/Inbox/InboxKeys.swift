// The Inbox keys (docs/24 §2.1, docs/23 §5.2), only while the list has the focus: ↑↓ j k move; 1-9 file under
// that slot's project, 0 back to none; q hands to AI (as a to-do), s hands it to be started from the device
// board (Start); x Done; space plays; Enter approves the suggested project, if there is one; / searches. Two
// keys need a second one, always with a hint and Esc: a number on another Mac's project waits for Enter, and
// handing a memo without a project waits for its number. Pure: a key and the selected entry in, an intent and
// what waits next out.

public enum InboxKey: Hashable, Sendable {
    case digit(Int)
    case up, down, enter, escape, space, slash
    case letter(Character)
}

/// What a key is waiting for.
public enum KeyPending: Hashable, Sendable {
    case none
    /// Another Mac's project group: Enter files it here (with --other); nil project is 0.
    case confirmFile(id: String, project: String?)
    /// Handing a memo without a project: 1-9 picks one, Enter hands it without.
    case pickProject(id: String, spawn: Bool)
}

public enum InboxIntent: Hashable, Sendable {
    case none
    case move(Int)
    case act(InboxAction)
    /// The action is grey: say why, do nothing.
    case blocked(String)
    case togglePlay, openDetail, focusSearch
}

public struct KeyResult: Hashable, Sendable {
    public let intent: InboxIntent
    public let pending: KeyPending

    public init(intent: InboxIntent, pending: KeyPending) {
        self.intent = intent
        self.pending = pending
    }

    static func just(_ intent: InboxIntent) -> KeyResult {
        KeyResult(intent: intent, pending: .none)
    }
}

public enum InboxKeys {
    /// `suggested`: the project key Suggestion found for the entry, approved by Enter.
    public static func reduce(_ key: InboxKey, entry: InboxEntry?, projects: ProjectMap, pending: KeyPending,
                              suggested: String? = nil) -> KeyResult {
        switch pending {
        case .none:
            if key == .enter, let suggested, let entry, entry.fileBlocked == nil {
                return .just(.act(.file(id: entry.id, project: suggested, other: false)))
            }
            return fresh(key, entry: entry, projects: projects)
        case .confirmFile(let id, let project):
            return confirming(key, id: id, project: project, entry: entry, projects: projects)
        case .pickProject(let id, let spawn):
            return picking(key, id: id, spawn: spawn, entry: entry, projects: projects)
        }
    }

    /// The line shown while a key waits for the next one.
    public static func hint(_ pending: KeyPending, projects: ProjectMap) -> String? {
        switch pending {
        case .none:
            return nil
        case .confirmFile(_, let project):
            let target = project.map { tr("归到 ") + projects.label(key: $0) } ?? tr("退回没项目")
            return tr("别的电脑的项目：按 Enter {0}，Esc 取消", target)
        case .pickProject:
            return tr("交给 AI：按 1–9 选项目，Enter 不选，Esc 取消")
        }
    }

    static func fresh(_ key: InboxKey, entry: InboxEntry?, projects: ProjectMap) -> KeyResult {
        switch key {
        case .up, .letter("k"): return .just(.move(-1))
        case .down, .letter("j"): return .just(.move(1))
        case .slash: return .just(.focusSearch)
        default: break
        }
        guard let entry else { return .just(.none) }
        switch key {
        case .digit(let number): return file(number, entry: entry, projects: projects)
        case .letter("q"): return hand(entry, spawn: false, projects: projects)
        case .letter("s"): return hand(entry, spawn: true, projects: projects)
        case .letter("x"): return .just(entry.fileBlocked.map(InboxIntent.blocked) ?? .act(.done(id: entry.id)))
        case .space: return .just(.togglePlay)
        case .enter: return .just(.openDetail)
        default: return .just(.none)
        }
    }

    static func file(_ number: Int, entry: InboxEntry, projects: ProjectMap) -> KeyResult {
        if let reason = entry.fileBlocked {
            return .just(.blocked(reason))
        }
        guard let target = target(number, projects: projects) else { return .just(.blocked(missing(number))) }
        if entry.needsConfirmation {
            return KeyResult(intent: .none, pending: .confirmFile(id: entry.id, project: target.key))
        }
        return .just(.act(.file(id: entry.id, project: target.key, other: false)))
    }

    static func hand(_ entry: InboxEntry, spawn: Bool, projects: ProjectMap) -> KeyResult {
        if let reason = entry.handBlocked {
            return .just(.blocked(reason))
        }
        if entry.project == nil, !projects.slots.isEmpty {
            return KeyResult(intent: .none, pending: .pickProject(id: entry.id, spawn: spawn))
        }
        return .just(.act(.hand(id: entry.id, project: nil, spawn: spawn, other: entry.needsConfirmation)))
    }

    static func confirming(_ key: InboxKey, id: String, project: String?, entry: InboxEntry?,
                           projects: ProjectMap) -> KeyResult {
        switch key {
        case .enter: return .just(.act(.file(id: id, project: project, other: true)))
        case .escape: return .just(.none)
        case .digit(let number): return entry.map { file(number, entry: $0, projects: projects) } ?? .just(.none)
        default: return fresh(key, entry: entry, projects: projects)
        }
    }

    static func picking(_ key: InboxKey, id: String, spawn: Bool, entry: InboxEntry?, projects: ProjectMap) -> KeyResult {
        let other = entry?.needsConfirmation ?? false
        switch key {
        case .enter, .digit(0):
            return .just(.act(.hand(id: id, project: nil, spawn: spawn, other: other)))
        case .escape:
            return .just(.none)
        case .digit(let number):
            guard let slot = projects.slot(number) else {
                return KeyResult(intent: .blocked(missing(number)), pending: .pickProject(id: id, spawn: spawn))
            }
            return .just(.act(.hand(id: id, project: slot.key, spawn: spawn, other: other)))
        default:
            return fresh(key, entry: entry, projects: projects)
        }
    }

    /// Where a number files to: 0 is no project (key nil), 1-9 a slot of this Mac if it has one.
    struct FileTarget {
        let key: String?
    }

    static func target(_ number: Int, projects: ProjectMap) -> FileTarget? {
        number == 0 ? FileTarget(key: nil) : projects.slot(number).map { FileTarget(key: $0.key) }
    }

    static func missing(_ number: Int) -> String {
        tr("没有 {0} 号项目", number)
    }
}
