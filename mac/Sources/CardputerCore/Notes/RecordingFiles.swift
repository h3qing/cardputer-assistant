// The transcriber's files in a session dir (protocol/recording/v1): result.json, transcript.json and the
// human transcript.md. Read leniently (a reader, not the validator), refused when v is not the integer 1;
// every text comes back display_safe.

import Foundation

public struct MemoText: Hashable, Sendable, Decodable {
    public let text: String
    public let intent: Open<MemoIntent>
}

/// result.json: done (level transcript or summary) or failed, with the memo's text and the title.
public struct RecordingResult: Hashable, Sendable {
    public let state: String
    public let level: String?
    public let title: String?
    public let memo: MemoText?
    public let errorCode: String?
}

/// status.json: the transcriber's progress on one recording (queued, transcribing, summarizing, blocked
/// with a reason) and which program wrote it.
public struct SessionStatus: Hashable, Sendable, Decodable {
    public let state: String
    public let reason: String?
    public let by: String?

    public init(state: String, reason: String?, by: String?) {
        self.state = state
        self.reason = reason
        self.by = by
    }
}

/// recordings/.transcriber: {v, app, pid, since, beat_at}.
public struct TranscriberClaim: Hashable, Sendable, Decodable {
    public let app: String?
    public let pid: Int?
    public let since: String?
    public let beatAt: String?

    public init(app: String?, pid: Int?, since: String?, beatAt: String?) {
        self.app = app
        self.pid = pid
        self.since = since
        self.beatAt = beatAt
    }

    enum CodingKeys: String, CodingKey {
        case app, pid, since, beatAt = "beat_at"
    }
}

struct ResultFile: Decodable {
    struct Failure: Decodable {
        let code: String
    }

    let state: String
    let level: String?
    let title: String?
    let memo: MemoText?
    let error: Failure?
}

struct TranscriptFile: Decodable {
    struct Line: Decodable {
        let tMs: Int64
        let text: String

        enum CodingKeys: String, CodingKey {
            case tMs = "t_ms", text
        }
    }

    let sentences: [Line]
}

extension NotesDirectory {
    public func result(in session: URL) -> RecordingResult? {
        guard let file: ResultFile = versioned(.result, in: session) else { return nil }
        let memo = file.memo.flatMap { memo in
            DisplaySafe.excerpt(memo.text, limit: 2000).map { MemoText(text: $0, intent: memo.intent) }
        }
        return RecordingResult(state: file.state, level: file.level, title: DisplaySafe.excerpt(file.title, limit: 80),
                               memo: memo, errorCode: file.error?.code)
    }

    /// status.json, display_safe; nil when there is none.
    public func status(in session: URL) -> SessionStatus? {
        guard let file: SessionStatus = versioned(.status, in: session) else { return nil }
        let clean = { (text: String?) in DisplaySafe.excerpt(text, limit: 24) }
        return SessionStatus(state: clean(file.state) ?? Labels.unknown, reason: clean(file.reason), by: clean(file.by))
    }

    /// transcript.json, else transcript.md; nil when neither is there or readable.
    public func transcript(in session: URL) -> [TimedSentence]? {
        if let file: TranscriptFile = versioned(.transcriptJSON, in: session) {
            return file.sentences.map { TimedSentence(seconds: Double($0.tMs) / 1000, text: DisplaySafe.clean($0.text)) }
        }
        guard let data = try? read(.transcriptMarkdown, in: session) else { return nil }
        return TranscriptMarkdown.parse(String(decoding: data, as: UTF8.self))
    }

    func versioned<T: Decodable>(_ file: OpenFile, in session: URL) -> T? {
        (try? read(file, in: session)).flatMap(VersionedFile.decode)
    }
}

/// transcript.md lines: `- [MM:SS] text ^tHHMMSS` (H:MM:SS past an hour; -2, -3 for more in one second).
public enum TranscriptMarkdown {
    public static func parse(_ text: String) -> [TimedSentence] {
        text.split(separator: "\n").compactMap { line(String($0)) }
    }

    static func line(_ line: String) -> TimedSentence? {
        guard line.hasPrefix("- ["), let close = line.firstIndex(of: "]"),
              let caret = line.range(of: " ^t", options: .backwards) else { return nil }
        let clock = line[line.index(line.startIndex, offsetBy: 3)..<close]
        let body = line[line.index(after: close)..<caret.lowerBound]
        guard let seconds = blockSeconds(String(line[caret.upperBound...])) ?? clockSeconds(String(clock)) else {
            return nil
        }
        return TimedSentence(seconds: Double(seconds), text: DisplaySafe.clean(body.trimmingCharacters(in: .whitespaces)))
    }

    /// HHMMSS of the block id, before any -2 suffix.
    static func blockSeconds(_ id: String) -> Int? {
        let digits = id.prefix { $0 != "-" }
        guard digits.count == 6, let value = Int(digits) else { return nil }
        return value / 10000 * 3600 + value / 100 % 100 * 60 + value % 100
    }

    static func clockSeconds(_ clock: String) -> Int? {
        let parts = clock.split(separator: ":").compactMap { Int($0) }
        guard (2...3).contains(parts.count) else { return nil }
        return parts.reduce(0) { $0 * 60 + $1 }
    }
}
