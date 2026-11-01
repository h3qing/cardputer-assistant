// The one-time test (docs/25 §3.1, D12), asked the way `cpa dest test ID --send` asks on a terminal:
// `cpa dest test ID --send --json` hands a marked "Test NNNN" to the app and asks nothing; the app asks.
// - a link: "Did it appear?" then "Did it send by itself without you pressing Return?". Appeared and not sent
//   → `cpa dest verify ID --draft` (only then is it a draft destination), sent by itself → `--act`. A preset
//   of your own (effect act) can never become a draft: "not sent" records nothing. Nothing appeared: only a
//   preset that takes a value per chat (takes_map) asks for that chat's link (`cpa dest map ID '*=LINK'`),
//   then tests again.
// - an app: the text is on the clipboard and the app in front; "Did it paste?" and nothing to record.
// Pure: a step and an answer in, the next step and the one command to run out.

public struct DestTestTarget: Hashable, Sendable {
    public let dest: String
    public let label: String
    public let kind: String
    /// url: what it may become (draft) or act; nil for an app.
    public let effect: String?
    /// url: its preset takes a chat's link or id (cpa dest map).
    public let takesMap: Bool

    public init(dest: String, label: String, kind: String, effect: String? = nil, takesMap: Bool = false) {
        self.dest = dest
        self.label = label
        self.kind = kind
        self.effect = effect
        self.takesMap = takesMap
    }

    public init(_ entry: DestEntry) {
        self.init(dest: entry.id, label: entry.label, kind: entry.kind, effect: entry.effect, takesMap: entry.takesMap)
    }
}

public enum DestTestStep: Hashable, Sendable {
    case idle
    /// The test text is on its way.
    case sending(DestTestTarget)
    case appeared(DestTestTarget, marker: String)
    case sentByItself(DestTestTarget, marker: String)
    /// Nothing appeared in a link destination that takes a chat: paste that chat's link.
    case link(DestTestTarget)
    case finished(Toast)
}

public enum DestTestFlow {
    /// What `cpa dest test --send --json` answered.
    public static func afterSend(_ tested: DestTested, target: DestTestTarget) -> DestTestStep {
        guard tested.handed else {
            let why = tested.code == "E_DRAFT_BUSY" ? tr("{0} 里还有上一条草稿：先发掉或清空，再测", target.label)
                : DestFeedback.code(tested.code ?? "", label: target.label)
            return .finished(Toast(text: tr("测试没送出去：") + why, isError: true))
        }
        return .appeared(target, marker: tested.marker)
    }

    /// The question the step asks, nil when it asks none.
    public static func question(_ step: DestTestStep) -> String? {
        switch step {
        case .appeared(let target, let marker) where target.kind == "app":
            return tr("{0} 到前面了吗？在里面按 ⌘V，贴出「测试 {1}」了吗？", target.label, marker)
        case .appeared(let target, let marker): return tr("{0} 里出现「测试 {1}」了吗？", target.label, marker)
        case .sentByItself: return tr("你没按回车，它自己发出去了吗？")
        case .link(let target):
            return tr("在 {0} 里打开要送去的那个对话，复制它的链接（或 id），贴在这里，再测一次", target.label)
        case .idle, .sending, .finished: return nil
        }
    }

    /// The line under a link's question: the test text must not be sent.
    public static func hint(_ step: DestTestStep) -> String? {
        switch step {
        case .appeared(let target, let marker) where target.kind != "app",
             .sentByItself(let target, let marker):
            return tr("测完把 {0} 输入框里的「测试 {1}」删掉，别按回车。", target.label, marker)
        default: return nil
        }
    }

    /// A yes or a no to the step's question.
    public static func answer(_ step: DestTestStep, yes: Bool) -> (DestTestStep, CpaCommand?) {
        switch step {
        case .appeared(let target, let marker):
            return appeared(target, marker: marker, yes: yes)
        case .sentByItself(let target, _):
            return sentByItself(target, yes: yes)
        case .idle, .sending, .link, .finished:
            return (step, nil)
        }
    }

    static func appeared(_ target: DestTestTarget, marker: String, yes: Bool) -> (DestTestStep, CpaCommand?) {
        let label = target.label
        if yes {
            if target.kind == "app" {
                return (done(tr("能用：送过去的字在剪贴板里，{0} 会到前面，你按 ⌘V", label)), nil)
            }
            return (.sentByItself(target, marker: marker), nil)
        }
        if target.kind == "url", target.takesMap {
            return (.link(target), nil)
        }
        return (done(tr("没记下：看看 {0} 开着吗、登录了吗，再送一条测试", label)), nil)
    }

    static func sentByItself(_ target: DestTestTarget, yes: Bool) -> (DestTestStep, CpaCommand?) {
        let label = target.label
        if yes {
            return (done(tr("记下了：{0} 会自己发出去，这台 Mac 不送它", label)), try? CpaCommand.destVerify(target.dest, draft: false))
        }
        guard target.effect == "draft" else {
            return (done(tr("你自己加的去向总当作会自己发出去：这台 Mac 不送它")), nil)
        }
        return (done(tr("记下了：{0} 只填草稿，你在 {1} 里按回车才发出", label, label)),
                try? CpaCommand.destVerify(target.dest, draft: true))
    }

    static func done(_ text: String) -> DestTestStep {
        .finished(Toast(text: text, isError: false))
    }
}
