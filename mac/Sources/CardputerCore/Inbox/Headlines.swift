// The words of recordings that cpa rec ls lists without any: result.json's title and memo text, else the
// first sentences of the transcript. Read once per recording state (id, state, result's state and level,
// folder) and kept: a list refreshed every 3 s reads only the recordings that changed.

import Foundation

public struct Headline: Hashable, Sendable {
    public let title: String?
    /// One line, at most 280 characters, display_safe.
    public let text: String?
    public let intent: Open<MemoIntent>?
}

public struct Headlines: Hashable, Sendable {
    struct Key: Hashable, Sendable {
        let sid: String
        let state: String
        let result: String
        let dir: String?

        init(_ row: RecRow) {
            self.sid = row.sid
            self.state = row.state.raw
            self.result = [row.result?.state, row.result?.level].map { $0 ?? "-" }.joined(separator: "/")
            self.dir = row.dir
        }
    }

    public static let empty = Headlines(known: [:])

    let known: [Key: Headline]

    public var count: Int {
        known.count
    }

    /// The headline of each row (read when its key is new) and the cache for the next time, holding only
    /// these rows.
    public func reading(_ rows: [RecRow], notes: NotesDirectory?) -> ([String: Headline], Headlines) {
        let pairs = rows.map { row -> (Key, Headline) in
            let key = Key(row)
            return (key, known[key] ?? Self.read(row, notes: notes))
        }
        let next = Dictionary(pairs, uniquingKeysWith: { first, _ in first })
        let bySid = Dictionary(pairs.map { ($0.0.sid, $0.1) }, uniquingKeysWith: { first, _ in first })
        return (bySid, Headlines(known: next))
    }

    static func read(_ row: RecRow, notes: NotesDirectory?) -> Headline {
        guard let notes, let session = row.dir.flatMap(notes.session(dir:)) else {
            return Headline(title: nil, text: nil, intent: nil)
        }
        let result = notes.result(in: session)
        let said = result?.memo?.text ?? notes.transcript(in: session)?.prefix(8).map(\.text).joined(separator: " ")
        return Headline(title: result?.title, text: DisplaySafe.excerpt(said, limit: 280), intent: result?.memo?.intent)
    }
}
