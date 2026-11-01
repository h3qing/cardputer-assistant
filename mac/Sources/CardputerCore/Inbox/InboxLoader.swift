// The Inbox list: `cpa filing ls --json` when this cpa has it; otherwise the read-only fallback, "recordings
// without a project" from `cpa rec ls --json` and the notes dir's open files (docs/24 §4, D6). The fallback
// is not the filing list: it only lists, and says what to install to file.

import Foundation

public enum InboxSource: Hashable, Sendable {
    case filing(FilingCounts)
    case fallback
}

public struct InboxPage: Hashable, Sendable {
    public let source: InboxSource
    public let entries: [InboxEntry]
    /// For the next page of the same query (filing only).
    public let nextCursor: String?

    public init(source: InboxSource, entries: [InboxEntry], nextCursor: String?) {
        self.source = source
        self.entries = entries
        self.nextCursor = nextCursor
    }

    /// The line on top: "12 waiting · 1 of them for another Mac's project · 2 still on the device", "All
    /// filed", "8 recordings without a project (read-only)". The count is the one the sidebar and the menu
    /// bar show.
    public var summary: String {
        switch source {
        case .fallback:
            return tr("{0} 段没项目的录音（只读）", entries.count)
        case .filing(let counts):
            let head = counts.waiting == 0 ? tr("都归好了") : tr("{0} 条待归档", counts.waiting)
            let other = counts.otherMac > 0 ? [tr("其中别的电脑的项目 {0} 条", counts.otherMac)] : []
            let device = (counts.onDevice ?? 0) > 0 ? [tr("设备上还有 {0} 段", counts.onDevice ?? 0)] : []
            return ([head] + other + device).joined(separator: " · ")
        }
    }
}

public struct InboxLoader: Sendable {
    public let client: CpaClient
    public let notes: NotesDirectory?

    public init(client: CpaClient, notes: NotesDirectory?) {
        self.client = client
        self.notes = notes
    }

    /// canFile: Capabilities.canFile.
    public func load(_ query: FilingQuery, canFile: Bool, now: Date = Date(),
                     calendar: Calendar = .current) async throws -> InboxPage {
        try await load(query, canFile: canFile, cache: .empty, now: now, calendar: calendar).0
    }

    /// The same, keeping the read-only list's words in `cache` between calls.
    public func load(_ query: FilingQuery, canFile: Bool, cache: Headlines, now: Date = Date(),
                     calendar: Calendar = .current) async throws -> (InboxPage, Headlines) {
        if canFile {
            let list = try await client.filingList(query, now: now, calendar: calendar)
            let page = InboxPage(source: .filing(list.counts), entries: list.rows.filter(query.keeps).map(InboxEntry.init),
                                 nextCursor: list.nextCursor)
            return (page, cache)
        }
        let rows = try await client.recordings(hidden: false)
        let (entries, kept) = Fallback.entries(rows, notes: notes, cache: cache)
        return (InboxPage(source: .fallback, entries: Fallback.filter(entries, query, now: now, calendar: calendar),
                          nextCursor: nil), kept)
    }
}

public enum Fallback {
    /// Recordings without a project, not hidden, off the device; text from result.json, else the transcript,
    /// read only for recordings whose state changed since `cache`.
    public static func entries(_ rows: [RecRow], notes: NotesDirectory?, cache: Headlines) -> ([InboxEntry], Headlines) {
        let listed = rows.filter(isListed)
        let (words, kept) = cache.reading(listed, notes: notes)
        return (listed.map { entry($0, words[$0.sid]) }, kept)
    }

    /// Only date (the folder's day), mode, text and the limit apply here; the rest need cpa filing.
    public static func filter(_ entries: [InboxEntry], _ query: FilingQuery, now: Date, calendar: Calendar) -> [InboxEntry] {
        let days = query.dayRange(now: now, calendar: calendar).map {
            FilingQuery.day($0.0, calendar)...FilingQuery.day($0.1, calendar)
        }
        let words = query.searchWords?.lowercased()
        let kept = entries.filter { entry in
            let inDays = days.map { range in entry.folderDay.map(range.contains) ?? false } ?? true
            let inMode = query.mode.map { entry.kind?.known == $0 } ?? true
            let said = [entry.title, entry.text].compactMap { $0 }.joined(separator: " ").lowercased()
            return inDays && inMode && (words.map(said.contains) ?? true)
        }
        return query.limit.map { Array(kept.prefix($0)) } ?? kept
    }

    static func isListed(_ row: RecRow) -> Bool {
        let offDevice = row.state.known != .onDevice && row.state.known != .pulling && row.dir != nil
        return offDevice && row.project == nil && !row.hidden
    }

    static func entry(_ row: RecRow, _ words: Headline?) -> InboxEntry {
        let day = row.dir?.split(separator: "/").dropFirst().first.map(String.init)
        return InboxEntry(
            id: row.sid, kind: row.mode, filing: nil,
            at: RecordedAt(wallMs: row.startedMs, tzMin: nil, uncertain: row.uncertain), durMs: row.durMs,
            tx: transcription(row.state), title: words?.title, text: words?.text,
            project: nil, asked: nil, device: nil, env: nil, starred: row.starred, intent: words?.intent,
            queue: nil, folderDay: day)
    }

    /// cpa rec ls's states in the filing contract's words: archived is waiting.
    static func transcription(_ state: Open<RecState>) -> Open<Transcription> {
        state.known == .archived ? Open(.waiting) : Open(raw: state.raw)
    }
}
