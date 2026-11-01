// One recording of the filing list: protocol/filing/v1 row.schema.json. Ids, times and states and at most
// 80 + 280 characters of display_safe text; never a path, the device's own id or a network.

/// The filing state; a closed enum: an unknown value refuses the document.
public enum FilingState: String, CaseIterable, Hashable, Sendable, Decodable {
    case new, filed, handed, done, hidden
}

public struct RecordedAt: Hashable, Sendable, Decodable {
    public let wallMs: Int64?
    public let tzMin: Int?
    public let uncertain: Bool

    public init(wallMs: Int64?, tzMin: Int?, uncertain: Bool) {
        self.wallMs = wallMs
        self.tzMin = tzMin
        self.uncertain = uncertain
    }

    enum CodingKeys: String, CodingKey {
        case wallMs = "wall_ms", tzMin = "tz_min", uncertain
    }
}

/// The Mac-side stamp: the Mac that pulled it (nil: it had no name) and whether it was recorded while linked
/// to it (nil: not a word this cpa writes). Both keys are always there.
public struct MacStamp: Hashable, Sendable, Decodable {
    public let host: String?
    public let link: Open<MacLink>?

    public init(host: String?, link: Open<MacLink>?) {
        self.host = host
        self.link = link
    }

    public init(from decoder: Decoder) throws {
        let box = try decoder.container(keyedBy: CodingKeys.self)
        self.host = try box.decode(String?.self, forKey: .host)
        self.link = try box.decode(Open<MacLink>?.self, forKey: .link)
    }

    enum CodingKeys: String, CodingKey {
        case host, link
    }
}

/// A queue item handed from a recording (cpa filing hand); open until it is spawned or done.
public struct QueueItem: Hashable, Sendable, Decodable {
    public let qid: String
    public let intent: Open<QueueIntent>
    public let open: Bool

    public init(qid: String, intent: Open<QueueIntent>, open: Bool) {
        self.qid = qid
        self.intent = intent
        self.open = open
    }
}

public struct FilingRow: Hashable, Sendable, Identifiable, Decodable {
    public let id: String
    public let kind: Open<RecordingKind>
    public let filing: FilingState
    public let why: [Open<FilingReason>]
    /// Only on a new row of the other-Mac group: the key pressed while recording.
    public let asked: String?
    public let at: RecordedAt
    public let durMs: Int?
    /// 12 hex digits, one per device and Mac; never the device's own id.
    public let device: String?
    public let env: MacStamp?
    /// The effective project when it is one of this Mac's keys.
    public let project: String?
    public let starred: Bool
    public let tx: Open<Transcription>
    public let title: String?
    public let text: String?
    public let intent: Open<MemoIntent>?
    public let queue: QueueItem?

    enum CodingKeys: String, CodingKey {
        case id, kind, filing, why, asked, at, durMs = "dur_ms", device, env, project, starred, tx, title, text
        case intent, queue
    }

    /// In the other-Mac group: filing it here needs --other and a second confirmation.
    public var isOtherMac: Bool {
        asked != nil
    }
}
