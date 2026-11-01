// The documents `cpa filing … --json` and `cpa project ls --json` print (protocol/filing/v1): ls, show,
// result, error and projects. Decode them with FilingContract, which also checks v and the rules.

public struct FilingCounts: Hashable, Sendable, Decodable {
    /// New rows of this Mac.
    public let new: Int
    /// New rows of the other-Mac group.
    public let otherMac: Int
    public let clipsWaiting: Int
    /// Segments still on the device; nil when cpad is not running.
    public let onDevice: Int?

    public init(new: Int, otherMac: Int, clipsWaiting: Int, onDevice: Int?) {
        self.new = new
        self.otherMac = otherMac
        self.clipsWaiting = clipsWaiting
        self.onDevice = onDevice
    }

    /// What waits to be filed: this Mac's new rows and the other-Mac group. The one number the app shows.
    public var waiting: Int {
        new + otherMac
    }

    enum CodingKeys: String, CodingKey {
        case new, otherMac = "other_mac", clipsWaiting = "clips_waiting", onDevice = "on_device"
    }
}

/// `cpa filing ls --json`: one page of rows, newest first.
public struct FilingList: Hashable, Sendable, Decodable {
    public let counts: FilingCounts
    public let nextCursor: String?
    public let rows: [FilingRow]

    enum CodingKeys: String, CodingKey {
        case counts, nextCursor = "next_cursor", rows
    }
}

public struct TimedSentence: Hashable, Sendable, Decodable {
    /// Seconds into the recording; nil when the transcriber did not say.
    public let seconds: Double?
    public let text: String

    public init(seconds: Double?, text: String) {
        self.seconds = seconds
        self.text = text
    }

    enum CodingKeys: String, CodingKey {
        case seconds = "t_s", text
    }
}

public struct Todo: Hashable, Sendable, Decodable {
    public let n: Int
    public let text: String
    public let owner: String?
    public let due: String?
    public let seconds: Double?
    public let confident: Bool

    enum CodingKeys: String, CodingKey {
        case n, text, owner, due, seconds = "t_s", confident
    }
}

/// `cpa filing show ID --json`.
public struct FilingItem: Hashable, Sendable, Decodable {
    public let row: FilingRow
    /// The whole memo text.
    public let text: String?
    /// nil when there is no readable transcript.
    public let sentences: [TimedSentence]?
    public let todos: [Todo]
    /// seg-NNN.wav names in the recording's directory; empty after the 30-day sweep.
    public let audio: [String]
    public let fw: String?
    /// The text handed to AI, read back by its qid.
    public let queueText: String?

    enum CodingKeys: String, CodingKey {
        case row, text, sentences, todos, audio, fw, queueText = "queue_text"
    }
}

/// The inverse of a write, as a neutral description: rebuilt into a command with the app's own verbs.
public struct UndoStep: Hashable, Sendable, Decodable {
    public let op: Open<UndoOp>
    public let id: String
    /// For op file: the project to go back to, nil for none.
    public let project: String?

    public init(op: Open<UndoOp>, id: String, project: String?) {
        self.op = op
        self.id = id
        self.project = project
    }

    enum CodingKeys: String, CodingKey {
        case op, id, args
    }

    enum ArgsKeys: String, CodingKey {
        case project
    }

    /// An undo of file always says its project (a key, or null for none); the other ops have none.
    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        self.op = try container.decode(Open<UndoOp>.self, forKey: .op)
        self.id = try container.decode(String.self, forKey: .id)
        let args = try container.nestedContainer(keyedBy: ArgsKeys.self, forKey: .args)
        self.project = op.known == .file
            ? try args.decode(String?.self, forKey: .project)
            : try args.decodeIfPresent(String.self, forKey: .project)
    }
}

/// A write that succeeded: `cpa filing file|done|hide|reopen|hand … --json`.
public struct FilingResult: Hashable, Sendable, Decodable {
    public let id: String
    /// The state after the write.
    public let filing: FilingState
    /// false when nothing was written (already in that state).
    public let changed: Bool
    public let undo: UndoStep?
    public let queue: QueueItem?
}

public enum FilingErrorCode: String, CaseIterable, Hashable, Sendable, Decodable {
    case badArg = "E_BAD_ARG"
    case unknownID = "E_UNKNOWN_ID"
    case unknownProject = "E_UNKNOWN_PROJECT"
    case noText = "E_NO_TEXT"
    case already = "E_ALREADY"
    case unavailable = "E_UNAVAILABLE"
}

/// A `cpa filing` or `cpa project ls` that failed: a fixed code and the name of the part at fault.
public struct FilingFailure: Hashable, Sendable, Decodable, Error {
    public let code: FilingErrorCode
    public let field: String?

    public init(code: FilingErrorCode, field: String?) {
        self.code = code
        self.field = field
    }

    enum CodingKeys: String, CodingKey {
        case code = "error", field
    }
}

public struct ProjectEntry: Hashable, Sendable, Decodable {
    public let key: String
    public let name: String
    /// 1-9, the number the device shows.
    public let slot: Int
    /// false when the project's linked folder is gone.
    public let ok: Bool
    /// A code folder is linked (docs/25 §8); an older cpa leaves it out, and all its projects had one.
    public let linked: Bool
    /// The linked folder's last path component, never a path; nil when unlinked or from an older cpa.
    public let folder: String?

    public init(key: String, name: String, slot: Int, ok: Bool, linked: Bool = true, folder: String? = nil) {
        self.key = key
        self.name = name
        self.slot = slot
        self.ok = ok
        self.linked = linked
        self.folder = folder
    }

    public init(from decoder: Decoder) throws {
        let box = try decoder.container(keyedBy: CodingKeys.self)
        self.init(key: try box.decode(String.self, forKey: .key), name: try box.decode(String.self, forKey: .name),
                  slot: try box.decode(Int.self, forKey: .slot), ok: try box.decode(Bool.self, forKey: .ok),
                  linked: try box.decodeIfPresent(Bool.self, forKey: .linked) ?? true,
                  folder: try box.decodeIfPresent(String.self, forKey: .folder))
    }

    enum CodingKeys: String, CodingKey {
        case key, name, slot, ok, linked, folder
    }
}

/// `cpa host name --json`: this Mac's name (config.json's host_label), nil when it has none.
public struct HostName: Hashable, Sendable, Decodable {
    public let host: String?

    public init(host: String?) {
        self.host = host
    }

    public init(from decoder: Decoder) throws {
        let box = try decoder.container(keyedBy: CodingKeys.self)
        self.host = try box.decode(String?.self, forKey: .host)
    }

    enum CodingKeys: String, CodingKey {
        case host
    }
}

/// `cpa project ls --json`: this Mac's projects by slot. No local path.
public struct ProjectList: Hashable, Sendable, Decodable {
    public let projects: [ProjectEntry]

    public init(projects: [ProjectEntry]) {
        self.projects = projects
    }
}
