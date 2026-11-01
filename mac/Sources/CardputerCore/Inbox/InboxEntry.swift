// One line of the Inbox list, from a filing row (cpa filing ls) or, before cpa filing is installed, from a
// cpa rec ls row and its open files (the read-only fallback: filing is nil). The app never decides a filing
// state; it only says which buttons are grey and why (docs/24 §2.1, guarding against slips).

import Foundation

public struct InboxEntry: Hashable, Sendable, Identifiable {
    public let id: String
    public let kind: Open<RecordingKind>?
    /// nil in the read-only fallback.
    public let filing: FilingState?
    public let at: RecordedAt
    public let durMs: Int?
    public let tx: Open<Transcription>
    public let title: String?
    /// One line, at most 280 characters, display_safe.
    public let text: String?
    public let project: String?
    /// Another Mac's key pressed while recording (the other-Mac group).
    public let asked: String?
    public let device: String?
    public let env: MacStamp?
    public let starred: Bool
    public let intent: Open<MemoIntent>?
    public let queue: QueueItem?
    /// The recording folder's day (YYYY-MM-DD), known in the fallback only.
    let folderDay: String?

    public init(_ row: FilingRow) {
        self.init(id: row.id, kind: row.kind, filing: row.filing, at: row.at, durMs: row.durMs, tx: row.tx,
                  title: DisplaySafe.excerpt(row.title, limit: 80), text: DisplaySafe.excerpt(row.text, limit: 280),
                  project: row.project, asked: row.asked, device: row.device, env: row.env, starred: row.starred,
                  intent: row.intent, queue: row.queue, folderDay: nil)
    }

    init(id: String, kind: Open<RecordingKind>?, filing: FilingState?, at: RecordedAt, durMs: Int?,
         tx: Open<Transcription>, title: String?, text: String?, project: String?, asked: String?, device: String?,
         env: MacStamp?, starred: Bool, intent: Open<MemoIntent>?, queue: QueueItem?, folderDay: String?) {
        self.id = id
        self.kind = kind
        self.filing = filing
        self.at = at
        self.durMs = durMs
        self.tx = tx
        self.title = title
        self.text = text
        self.project = project
        self.asked = asked
        self.device = device
        self.env = env
        self.starred = starred
        self.intent = intent
        self.queue = queue
        self.folderDay = folderDay
    }

    /// The title, else the text, else "Memo · 24s · not transcribed yet".
    public var headline: String {
        title ?? text ?? [kind.map(Labels.kind) ?? tr("录音"), Format.duration(ms: durMs), noTextReason].joined(separator: " · ")
    }

    var noTextReason: String {
        switch tx.known {
        case .failed, .stale, .blocked: return Labels.transcription(tx)
        default: return tr("还没转写")
        }
    }

    public static var needsFiling: String { tr("归档、交给 AI、看过了都要 cpa filing") }
    /// Why Hand to AI (q, s) is grey, or nil when it is not.
    public var handBlocked: String? {
        if filing == nil {
            return Self.needsFiling
        }
        if kind?.known == .meeting {
            return tr("会议里有别人的话，不能交给 AI")
        }
        if queue?.open == true {
            return tr("已经交给 AI 了")
        }
        if filing == .hidden {
            return tr("藏起来的录音不能交给 AI")
        }
        return title == nil && text == nil ? tr("还没转写，先归档或等转写") : nil
    }

    /// Why the 1-9 buttons are grey, or nil when they are not.
    public var fileBlocked: String? {
        filing == nil ? Self.needsFiling : nil
    }

    /// In the other-Mac group: a number needs Enter to confirm, and the command gets --other.
    public var needsConfirmation: Bool {
        asked != nil
    }
}

extension Array where Element == InboxEntry {
    /// The new entry to select after acting on `id`: the next new one below it, else the nearest above.
    public func nextNew(after id: String) -> String? {
        guard let index = firstIndex(where: { $0.id == id }) else {
            return first { $0.filing == .new }?.id
        }
        let below = self[(index + 1)...].first { $0.filing == .new }
        let above = self[..<index].last { $0.filing == .new }
        return (below ?? above)?.id
    }
}
