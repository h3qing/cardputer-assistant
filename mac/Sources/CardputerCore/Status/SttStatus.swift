// `cpa-stt status --json` (docs/22 §4.11, cpa_stt/cli_status.py): on or off, the claim, the command, the
// sandbox, the self-test, recent timings and second writers. Never any transcript text. Read leniently.

public struct SttClaim: Hashable, Sendable, Decodable {
    public let app: String?
    public let pid: Int?
    public let beatAgeS: Int?

    enum CodingKeys: String, CodingKey {
        case app, pid, beatAgeS = "beat_age_s"
    }
}

public struct SttRecent: Hashable, Sendable, Decodable {
    public let session: String
    public let durMs: Int
    public let tookMs: Int
    public let exit: Int?

    enum CodingKeys: String, CodingKey {
        case session, durMs = "dur_ms", tookMs = "took_ms", exit
    }
}

public struct SttStatus: Hashable, Sendable, Decodable {
    public let on: Bool
    /// The recordings folder it watches.
    public let recordings: String?
    public let recordingsExists: Bool?
    public let command: String?
    public let missing: [String]
    public let sandbox: Bool?
    public let claim: SttClaim?
    public let selftestOK: Bool?
    public let selftestAt: String?
    public let blocked: String?
    public let retrying: Int?
    public let recent: [SttRecent]
    public let foreign: [String]
    public let busyBy: String?
    /// The LaunchAgent is installed.
    public let service: Bool?

    enum CodingKeys: String, CodingKey {
        case on, recordings, recordingsExists = "recordings_exists", command, missing, sandbox, claim
        case selftestOK = "selftest_ok", selftestAt = "selftest_at", blocked, retrying, recent, foreign
        case busyBy = "busy_by", service
    }

    public init(from decoder: Decoder) throws {
        let box = try decoder.container(keyedBy: CodingKeys.self)
        self.on = try box.decodeIfPresent(Bool.self, forKey: .on) ?? false
        self.recordings = try box.decodeIfPresent(String.self, forKey: .recordings)
        self.recordingsExists = try box.decodeIfPresent(Bool.self, forKey: .recordingsExists)
        self.command = try box.decodeIfPresent(String.self, forKey: .command)
        self.missing = try box.decodeIfPresent([String].self, forKey: .missing) ?? []
        self.sandbox = try box.decodeIfPresent(Bool.self, forKey: .sandbox)
        self.claim = try box.decodeIfPresent(SttClaim.self, forKey: .claim)
        self.selftestOK = try box.decodeIfPresent(Bool.self, forKey: .selftestOK)
        self.selftestAt = try box.decodeIfPresent(String.self, forKey: .selftestAt)
        self.blocked = try box.decodeIfPresent(String.self, forKey: .blocked)
        self.retrying = try box.decodeIfPresent(Int.self, forKey: .retrying)
        self.recent = try box.decodeIfPresent([SttRecent].self, forKey: .recent) ?? []
        self.foreign = try box.decodeIfPresent([String].self, forKey: .foreign) ?? []
        self.busyBy = try box.decodeIfPresent(String.self, forKey: .busyBy)
        self.service = try box.decodeIfPresent(Bool.self, forKey: .service)
    }
}
