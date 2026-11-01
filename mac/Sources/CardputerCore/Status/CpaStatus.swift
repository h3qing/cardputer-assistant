// `cpa status --json` (daemon cli/main.py, app.py status()): fields are only ever added, so every field is
// read when it is there and nothing else is assumed. The device's own ids (serial, dev: its factory MAC
// address; boot) are deliberately not read (docs/24 §2.3).

public enum DeviceLink: String, KnownValue {
    case ready, handshaking, offline, paused
}

public struct DeviceStatus: Hashable, Sendable, Decodable {
    public let fw: String?
    /// Messages waiting to be sent to the device.
    public let pending: Int?
    public let store: String?
    public let launcher: Bool?
}

public struct AgentStatus: Hashable, Sendable, Decodable {
    public let status: String?
    public let version: String?
    public let spawn: Bool?
    public let reply: Bool?
    public let login: Bool?
}

public struct StatusProject: Hashable, Sendable, Decodable {
    public let key: String
    public let slot: Int
    public let ok: Bool

    public init(key: String, slot: Int, ok: Bool) {
        self.key = key
        self.slot = slot
        self.ok = ok
    }
}

public struct RecordingCounts: Hashable, Sendable, Decodable {
    /// Segments still on the device; nil when cpad is not running.
    public let onDevice: Int?
    public let pulling: Int?
    public let waiting: Int?
    public let processing: Int?
    public let blocked: Int?
    public let failed: Int?
    public let lastResultAt: String?

    enum CodingKeys: String, CodingKey {
        case onDevice = "on_device", pulling, waiting, processing, blocked, failed, lastResultAt = "last_result_at"
    }
}

public struct CpaStatus: Hashable, Sendable, Decodable {
    public let running: Bool
    public let version: String?
    /// cpad's control API level (docs/23 §1.3); nil before it existed.
    public let api: Int?
    /// nil when cpad is not running.
    public let link: Open<DeviceLink>?
    public let pausedS: Double?
    public let device: DeviceStatus?
    public let agent: AgentStatus?
    public let projects: [StatusProject]
    public let projectsError: String?
    public let queueOpen: Int?
    public let recordings: RecordingCounts?
    public let warnings: [String]
    /// G1 (docs/24 §1.3): the notes dir, once cpa prints it.
    public let notesDir: String?
    /// G2: this Mac's name, once `cpa host name` exists.
    public let hostLabel: String?

    enum CodingKeys: String, CodingKey {
        case running, version, api, link, pausedS = "paused_s", device, agent, projects
        case projectsError = "projects_error", queueOpen = "queue_open", recordings, warnings
        case notesDir = "notes_dir", hostLabel = "host_label"
    }

    public init(from decoder: Decoder) throws {
        let box = try decoder.container(keyedBy: CodingKeys.self)
        self.running = try box.decodeIfPresent(Bool.self, forKey: .running) ?? false
        self.version = try box.decodeIfPresent(String.self, forKey: .version)
        self.api = try box.decodeIfPresent(Int.self, forKey: .api)
        self.link = try box.decodeIfPresent(Open<DeviceLink>.self, forKey: .link)
        self.pausedS = try box.decodeIfPresent(Double.self, forKey: .pausedS)
        self.device = try box.decodeIfPresent(DeviceStatus.self, forKey: .device)
        self.agent = try box.decodeIfPresent(AgentStatus.self, forKey: .agent)
        self.projects = try box.decodeIfPresent([StatusProject].self, forKey: .projects) ?? []
        self.projectsError = try box.decodeIfPresent(String.self, forKey: .projectsError)
        self.queueOpen = try box.decodeIfPresent(Int.self, forKey: .queueOpen)
        self.recordings = try box.decodeIfPresent(RecordingCounts.self, forKey: .recordings)
        self.warnings = try box.decodeIfPresent([String].self, forKey: .warnings) ?? []
        self.notesDir = try box.decodeIfPresent(String.self, forKey: .notesDir)
        self.hostLabel = try box.decodeIfPresent(String.self, forKey: .hostLabel)
    }
}

/// The menu bar's dot: green linked, grey not linked, red when cpad is not running or cpa failed; unknown
/// (grey, with words) when cpa status has not answered for a while.
public enum StatusLight: Hashable, Sendable {
    case linked, unlinked, down, unknown

    public static func of(_ status: CpaStatus?) -> StatusLight {
        guard let status, status.running else { return .down }
        return status.link?.known == .ready ? .linked : .unlinked
    }
}
