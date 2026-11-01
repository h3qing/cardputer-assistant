// The ★ marks and the length of a session's manifest.json (protocol/recording/v1), in session time. Marks
// whose k this app does not know are skipped, as the contract asks of readers.

import Foundation

struct MarksFile: Decodable {
    struct Mark: Decodable {
        let tMs: Int64
        let k: String

        enum CodingKeys: String, CodingKey {
            case tMs = "t_ms", k
        }
    }

    let marks: [Mark]?
}

extension NotesDirectory {
    /// The star marks in seconds, in order; none when there is no readable manifest.
    public func marks(in session: URL) -> [Double] {
        guard let file: MarksFile = versioned(.manifest, in: session) else { return [] }
        return (file.marks ?? []).filter { $0.k == "star" && $0.tMs >= 0 }.map { Double($0.tMs) / 1000 }.sorted()
    }
}

struct LengthFile: Decodable {
    let durationMs: Int64?

    enum CodingKeys: String, CodingKey {
        case durationMs = "duration_ms"
    }
}

extension NotesDirectory {
    /// The session's length in seconds (manifest duration_ms, session time, the time base of marks and the
    /// transcript); nil without a readable manifest.
    public func length(in session: URL) -> Double? {
        let file: LengthFile? = versioned(.manifest, in: session)
        guard let ms = file?.durationMs, ms > 0 else { return nil }
        return Double(ms) / 1000
    }
}
