// Extensible enums (x-knownValues in the contracts): a value this build does not know is kept and shown as
// Unknown, never a reason to refuse the row.

public protocol KnownValue: RawRepresentable, CaseIterable, Hashable, Sendable where RawValue == String {}

public struct Open<Known: KnownValue>: Hashable, Sendable, Decodable, CustomStringConvertible {
    public let raw: String

    public init(raw: String) {
        self.raw = raw
    }

    public init(_ known: Known) {
        self.raw = known.rawValue
    }

    /// nil when this build does not know the value.
    public var known: Known? {
        Known(rawValue: raw)
    }

    public var description: String {
        raw
    }

    public init(from decoder: Decoder) throws {
        self.raw = try decoder.singleValueContainer().decode(String.self)
    }
}

/// row.kind and the recording contract's mode.
public enum RecordingKind: String, KnownValue {
    case memo, meeting
}

/// row.why
public enum FilingReason: String, KnownValue {
    case unfiled, orphan
    case noText = "no_text"
}

/// row.tx: the transcription.
public enum Transcription: String, KnownValue {
    case pulling, waiting, processing, blocked, transcript, done, failed, stale
}

/// row.env.link: recorded while linked to this Mac (here) or not known to be (unknown).
public enum MacLink: String, KnownValue {
    case here, unknown
}

/// row.intent: what the memo's transcriber made of it.
public enum MemoIntent: String, KnownValue {
    case note, todo
}

/// queue.intent: todo, or spawn (to be started as a session from the device board).
public enum QueueIntent: String, KnownValue {
    case todo, spawn
}

/// result.undo.op
public enum UndoOp: String, KnownValue {
    case file, reopen, done, hide
}
