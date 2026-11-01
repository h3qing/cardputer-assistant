// One session of `cpa rec ls --json` (daemon recordings/sessions.py): ids, times, paths and states, never
// a title or any text. dir is relative to the notes dir; nil while the session is still on the device.

public enum RecState: String, KnownValue {
    case onDevice = "on_device"
    case pulling, archived, processing, blocked, done, transcript, failed, stale
}

/// The transcriber's latest result.json, as cpad saw it.
public struct RecResult: Hashable, Sendable, Decodable {
    public let state: String?
    public let level: String?
    public let errorCode: String?

    public init(state: String?, level: String?, errorCode: String?) {
        self.state = state
        self.level = level
        self.errorCode = errorCode
    }

    enum CodingKeys: String, CodingKey {
        case state, level, errorCode = "error_code"
    }
}

public struct RecRow: Hashable, Sendable, Identifiable, Decodable {
    public let sid: String
    public let mode: Open<RecordingKind>?
    public let startedMs: Int64?
    public let uncertain: Bool
    public let durMs: Int?
    public let state: Open<RecState>
    /// Why it is blocked: busy, model_missing or disabled.
    public let reason: String?
    public let dir: String?
    public let segments: Int?
    public let gaps: [Int]
    public let result: RecResult?
    public let audioSwept: Bool
    public let project: String?
    public let marks: Int
    public let starred: Bool
    public let hidden: Bool

    public var id: String {
        sid
    }

    enum CodingKeys: String, CodingKey {
        case sid, mode, startedMs = "started_ms", uncertain, durMs = "dur_ms", state, reason, dir, segments, gaps
        case result, audioSwept = "audio_swept", project, marks, starred, hidden
    }

    public init(from decoder: Decoder) throws {
        let box = try decoder.container(keyedBy: CodingKeys.self)
        self.sid = try box.decode(String.self, forKey: .sid)
        self.mode = try box.decodeIfPresent(Open<RecordingKind>.self, forKey: .mode)
        self.startedMs = try box.decodeIfPresent(Int64.self, forKey: .startedMs)
        self.uncertain = try box.decodeIfPresent(Bool.self, forKey: .uncertain) ?? false
        self.durMs = try box.decodeIfPresent(Int.self, forKey: .durMs)
        self.state = try box.decode(Open<RecState>.self, forKey: .state)
        self.reason = try box.decodeIfPresent(String.self, forKey: .reason)
        self.dir = try box.decodeIfPresent(String.self, forKey: .dir)
        self.segments = try box.decodeIfPresent(Int.self, forKey: .segments)
        self.gaps = try box.decodeIfPresent([Int].self, forKey: .gaps) ?? []
        self.result = try box.decodeIfPresent(RecResult.self, forKey: .result)
        self.audioSwept = try box.decodeIfPresent(Bool.self, forKey: .audioSwept) ?? false
        self.project = try box.decodeIfPresent(String.self, forKey: .project)
        self.marks = try box.decodeIfPresent(Int.self, forKey: .marks) ?? 0
        self.starred = try box.decodeIfPresent(Bool.self, forKey: .starred) ?? false
        self.hidden = try box.decodeIfPresent(Bool.self, forKey: .hidden) ?? false
    }
}
