// One recording's detail from the notes dir's open files (and `cpa filing show` when this cpa has it), built
// off the main thread: the folder is looked for on the recording's own day first, a transcript can be
// megabytes. Also why its transcription stopped, from status.json or result.json, in words.

import CardputerCore
import Foundation

enum DetailReader {
    static func entry(_ entry: InboxEntry, item: FilingItem?, notes: NotesDirectory?, calendar: Calendar) -> EntryDetail {
        let start = entry.at.wallMs.map { Date(timeIntervalSince1970: TimeInterval($0) / 1000) }
        let session = notes?.session(id: entry.id, near: start, calendar: calendar)
        let audio = session.flatMap { notes?.audio(in: $0) } ?? []
        let stopped = session.flatMap { stoppedReason(in: $0, notes: notes) }
        if let item {
            let names = Set(item.audio)
            return EntryDetail(id: entry.id, text: item.text, sentences: item.sentences ?? [], todos: item.todos,
                               summary: [], audio: audio.filter { names.contains($0.lastPathComponent) },
                               queueText: DisplaySafe.excerpt(item.queueText, limit: 2000), queueID: item.row.queue?.qid,
                               session: session, stopped: stopped)
        }
        let sentences = session.flatMap { notes?.transcript(in: $0) } ?? []
        let text = session.flatMap { notes?.result(in: $0)?.memo?.text } ?? entry.text
        return EntryDetail(id: entry.id, text: text, sentences: sentences, todos: [], summary: [], audio: audio,
                           queueText: nil, queueID: nil, session: session, stopped: stopped)
    }

    static func recording(_ row: RecRow, notes: NotesDirectory?) -> EntryDetail {
        let session = row.dir.flatMap { notes?.session(dir: $0) }
        let result = session.flatMap { notes?.result(in: $0) }
        let stopped = session.flatMap { stoppedReason(in: $0, notes: notes) }
            ?? row.reason.map(Labels.blockedReason) ?? row.result?.errorCode.map(Labels.resultError)
        return EntryDetail(
            id: row.sid, text: result?.memo?.text, sentences: session.flatMap { notes?.transcript(in: $0) } ?? [],
            todos: [], summary: session.flatMap { notes?.summary(in: $0) } ?? [],
            audio: session.flatMap { notes?.audio(in: $0) } ?? [], queueText: nil, queueID: nil, session: session,
            stopped: stopped, marks: session.flatMap { notes?.marks(in: $0) } ?? [],
            length: session.flatMap { notes?.length(in: $0) })
    }

    /// Blocked: No model (status.json blocked) or Failed: The transcribe command failed (result.json failed);
    /// nil otherwise.
    static func stoppedReason(in session: URL, notes: NotesDirectory?) -> String? {
        if let result = notes?.result(in: session), result.state == "failed" {
            return tr("失败：") + (result.errorCode.map(Labels.resultError) ?? Labels.unknown)
        }
        guard let status = notes?.status(in: session), status.state == "blocked" else { return nil }
        return tr("跑不了：") + (status.reason.map(Labels.blockedReason) ?? Labels.unknown)
    }
}
