// Send to… (docs/25 §5.4, §5.7): which destinations a memo can be sent to from the Mac, in what order, which
// are grey and why, and ⌥ + a letter. Only kinds this Mac hands over itself are offered (links, apps, files,
// the clipboard): handing to AI stays Hand to AI (cpa filing hand), and a destination that may act by itself
// (fl has k) is never sent to from the Mac (D14); a link not tested yet opens its test instead. A meeting has
// no Send to…. Pure: lists in, lists out.

public struct DestChoice: Hashable, Sendable, Identifiable {
    public let dest: DestEntry
    /// Why it cannot be sent to from here, nil when it can.
    public let blocked: String?

    public var id: String {
        dest.id
    }

    /// Grey only because the one-time test has not run yet: a click opens it.
    public var needsTest: Bool {
        blocked != nil && DestChoices.untested(dest)
    }
}

/// What ⌥ + a letter does.
public enum DestPick: Hashable, Sendable {
    case send(String)
    case blocked(String)
}

public enum DestChoices {
    /// The order of Send to…: links first (a draft to send with Return), then apps, files, the clipboard.
    static let kindOrder = ["url", "app", "file", "clip"]
    public static var noWords: String { tr("还没转写：没有字可送") }
    /// A memo of the Inbox; `usual`: its project's usual destination, offered first.
    public static func forEntry(_ entry: InboxEntry, dests: [DestEntry], usual: String?) -> [DestChoice] {
        guard entry.filing != nil, entry.kind?.known != .meeting else { return [] }
        let words = entry.title != nil || entry.text != nil
        return ordered(dests, first: usual).map { dest in
            DestChoice(dest: dest, blocked: words ? blocked(dest) : noWords)
        }
    }

    /// A row of Waiting to send: its preselected destination first.
    public static func forPending(_ row: PendingRow, dests: [DestEntry]) -> [DestChoice] {
        ordered(dests, first: row.dest).map { DestChoice(dest: $0, blocked: blocked($0)) }
    }

    public static func pick(_ letter: Character, in choices: [DestChoice]) -> DestPick {
        let key = letter.lowercased()
        guard let choice = choices.first(where: { $0.dest.key == key }) else {
            return .blocked(tr("没有字母 {0} 的去向", key.uppercased()))
        }
        return choice.blocked.map(DestPick.blocked) ?? .send(choice.id)
    }

    /// The letters that send somewhere, in the list's order (⌥ + each).
    public static func letters(_ choices: [DestChoice]) -> [String] {
        choices.compactMap(\.dest.key)
    }

    static func ordered(_ dests: [DestEntry], first: String?) -> [DestEntry] {
        let offered = dests.filter { $0.isOn && kindOrder.contains($0.kind) }
        return offered.sorted { lhs, rhs in
            let left = (lhs.id == first ? 0 : 1, kindOrder.firstIndex(of: lhs.kind) ?? 9, lhs.slot)
            let right = (rhs.id == first ? 0 : 1, kindOrder.firstIndex(of: rhs.kind) ?? 9, rhs.slot)
            return left < right
        }
    }

    /// fl has k (or cannot be read): only the device may confirm it.
    public static func mayAct(_ dest: DestEntry) -> Bool {
        (dest.fl ?? "lck").contains("k")
    }

    /// A link that may become a draft and has not been tested on this Mac.
    public static func untested(_ dest: DestEntry) -> Bool {
        dest.kind == "url" && dest.effect == "draft" && dest.verified == nil && mayAct(dest)
    }

    public static var testFirst: String { tr("还没测过，可能会自己发出去。先到 集成 → 去向 送一条测试，测过才能从 Mac 送") }
    public static var actsByItself: String { tr("会自己发出去：这台 Mac 不能送它") }
    static func blocked(_ dest: DestEntry) -> String? {
        guard mayAct(dest) else { return nil }
        return untested(dest) ? testFirst : actsByItself
    }

    /// The Destinations card's words for a destination's state.
    public static func state(_ dest: DestEntry) -> String {
        switch dest.state {
        case "on": break
        case "off": return tr("关着")
        case "needs_optin": return tr("还没允许离开这台 Mac")
        case "unavailable": return tr("这个版本用不了")
        default: return dest.state
        }
        if dest.kind == "app" {
            return tr("剪贴板 + 调到前面，你按 ⌘V")
        }
        guard dest.kind == "url" else { return mayAct(dest) ? actsByItself : tr("能用") }
        switch dest.verified {
        case "draft": return tr("只填草稿 · 测过了")
        case "act": return actsByItself
        default: return mayAct(dest) ? tr("还没测：先送一条测试，测过才能从 Mac 送") : tr("只填草稿")
        }
    }
}
