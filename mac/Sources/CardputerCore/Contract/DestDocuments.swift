// The documents of protocol/dest-cli/v1 (docs/25 §5.4): what `cpa dest pending|send|drop|usual|test --send`
// and `cpa filing send` print with --json. Decode them with DestContract, which also checks v and the rules.
// A field the schema lists as required is required even when it may be null.

/// pending.rows[].why
public enum PendingWhy: String, KnownValue {
    case held, usual, expired
}

/// A destination's kind (docs/25 §2.2).
public enum DestKind: String, KnownValue {
    case daily, todo, queue, session, clip, paste, file, url, app
}

/// pending.rows[].src: spoken or typed.
public enum CaptureSource: String, KnownValue {
    case voice, typed
}

/// sent.outcome
public enum SendOutcome: String, KnownValue {
    case handed, delivered, failed
}

/// How another app was handed the text.
public enum HandVia: String, KnownValue {
    case url, clip
}

/// sent.why: why a link went to the clipboard instead.
public enum ClipWhy: String, KnownValue {
    case tooLong = "too_long"
    case draftBusy = "draft_busy"
}

/// pending.rows[].st, a closed enum: waits for a confirmation or the device, or a delivery failed.
public enum HeldState: String, Hashable, Sendable, Decodable {
    case hold, failed
}

public struct PendingRow: Hashable, Sendable, Decodable, Identifiable {
    public let ref: String
    public let why: Open<PendingWhy>
    /// The preselected destination: ⌘↩ sends there.
    public let dest: String
    /// The destination's label, or its id when it was removed since.
    public let label: String
    public let kind: Open<DestKind>?
    /// nil when the destination is unknown: read as lck.
    public let fl: String?
    public let st: HeldState?
    public let code: String?
    public let project: String?
    public let src: Open<CaptureSource>
    public let at: String
    /// At most 280 characters, display_safe; nil when cpa has no text for it.
    public let text: String?

    public var id: String {
        ref
    }

    enum CodingKeys: String, CodingKey {
        case ref, why, dest, label, kind, fl, st, code, project, src, at, text
    }

    public init(from decoder: Decoder) throws {
        let box = try decoder.container(keyedBy: CodingKeys.self)
        ref = try box.decode(String.self, forKey: .ref)
        why = try box.decode(Open<PendingWhy>.self, forKey: .why)
        dest = try box.decode(String.self, forKey: .dest)
        label = try box.decode(String.self, forKey: .label)
        kind = try box.decode(Open<DestKind>?.self, forKey: .kind)
        fl = try box.decode(String?.self, forKey: .fl)
        st = try box.decode(HeldState?.self, forKey: .st)
        code = try box.decode(String?.self, forKey: .code)
        project = try box.decode(String?.self, forKey: .project)
        src = try box.decode(Open<CaptureSource>.self, forKey: .src)
        at = try box.decode(String.self, forKey: .at)
        text = try box.decode(String?.self, forKey: .text)
    }
}

/// `cpa dest pending --json`: held rows first, then the usual destinations' suggestions.
public struct PendingList: Hashable, Sendable, Decodable {
    public let rows: [PendingRow]

    public init(rows: [PendingRow]) {
        self.rows = rows
    }
}

/// `cpa dest send … --json`, `cpa filing send … --json`: what a send did. Never the text.
public struct DestSent: Hashable, Sendable, Decodable {
    public let ref: String
    public let dest: String
    public let label: String
    public let kind: Open<DestKind>
    public let outcome: Open<SendOutcome>
    public let via: Open<HandVia>?
    public let why: Open<ClipWhy>?
    public let code: String?
    public let leavesMac: Bool

    enum CodingKeys: String, CodingKey {
        case ref, dest, label, kind, outcome, via, why, code, leavesMac = "leaves_mac"
    }

    public init(from decoder: Decoder) throws {
        let box = try decoder.container(keyedBy: CodingKeys.self)
        ref = try box.decode(String.self, forKey: .ref)
        dest = try box.decode(String.self, forKey: .dest)
        label = try box.decode(String.self, forKey: .label)
        kind = try box.decode(Open<DestKind>.self, forKey: .kind)
        outcome = try box.decode(Open<SendOutcome>.self, forKey: .outcome)
        via = try box.decode(Open<HandVia>?.self, forKey: .via)
        why = try box.decode(Open<ClipWhy>?.self, forKey: .why)
        code = try box.decode(String?.self, forKey: .code)
        leavesMac = try box.decode(Bool.self, forKey: .leavesMac)
    }
}

/// `cpa dest drop REF --json`
public struct DestDropped: Hashable, Sendable, Decodable {
    public let ref: String
    public let dest: String
}

public struct UsualEntry: Hashable, Sendable, Decodable {
    public let project: String
    public let dest: String
}

/// `cpa dest usual [PROJECT [DEST|-]] --json`: every project's usual destination after the call.
public struct UsualList: Hashable, Sendable, Decodable {
    public let usual: [UsualEntry]

    public init(usual: [UsualEntry]) {
        self.usual = usual
    }

    public func dest(for project: String?) -> String? {
        usual.first { $0.project == project }?.dest
    }
}

/// `cpa dest test ID --send --json`: one marked test text was handed, or not (code).
public struct DestTested: Hashable, Sendable, Decodable {
    public let dest: String
    public let marker: String
    public let handed: Bool
    public let via: Open<HandVia>?
    public let code: String?

    enum CodingKeys: String, CodingKey {
        case dest, marker, handed, via, code
    }

    public init(from decoder: Decoder) throws {
        let box = try decoder.container(keyedBy: CodingKeys.self)
        dest = try box.decode(String.self, forKey: .dest)
        marker = try box.decode(String.self, forKey: .marker)
        handed = try box.decode(Bool.self, forKey: .handed)
        via = try box.decode(Open<HandVia>?.self, forKey: .via)
        code = try box.decode(String?.self, forKey: .code)
    }
}

/// A refused dest command: an open code (E_…) and the argument at fault; nothing was written.
public struct DestFailure: Hashable, Sendable, Decodable, Error {
    public let code: String
    public let field: String?

    public init(code: String, field: String?) {
        self.code = code
        self.field = field
    }

    enum CodingKeys: String, CodingKey {
        case code = "error", field
    }
}
