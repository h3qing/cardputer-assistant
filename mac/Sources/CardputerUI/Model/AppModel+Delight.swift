// Two small delights, local and quiet. Inbox zero: when the last waiting recording is filed, Mando's dot is a
// sparkle for a few seconds (once a day) and the empty Inbox shows today's recap from `cpa rec ls --json`.
// Vlog timecodes: a recording's ★ marks and lines copied as chapter lines, or saved as an SRT where you
// choose.

import CardputerCore
import Foundation

extension AppModel {
    static let celebratedKey = "celebratedDay"

    // MARK: inbox zero

    /// Every new count goes through here: from some waiting to none celebrates (once a day) and reads the recap.
    func setCounts(_ fresh: FilingCounts) {
        let old = counts?.waiting
        counts = fresh
        guard fresh.waiting == 0, old != 0 else { return }
        Task { await loadRecap() }
        let today = InboxZero.day(now, calendar: services.calendar)
        guard InboxZero.celebrates(from: old, to: fresh.waiting, lastDay: celebratedDay, today: today) else { return }
        celebratedDay = today
        if remembers {
            UserDefaults.standard.set(today, forKey: Self.celebratedKey)
        }
        celebrate()
    }

    /// The sparkle, for InboxZero.sparkleFor seconds.
    public func celebrate() {
        sparkling = true
        Task {
            try? await Task.sleep(nanoseconds: UInt64(InboxZero.sparkleFor * 1_000_000_000))
            sparkling = false
        }
    }

    /// Today's memos from cpa rec ls; the recap stays as it was when cpa does not answer.
    public func loadRecap() async {
        guard cpaFound, let rows = try? await services.client.recordings(hidden: false) else { return }
        let fresh = TodayRecap.make(rows, now: now, calendar: services.calendar)
        if fresh != recap {
            recap = fresh
        }
    }

    // MARK: vlog timecodes

    /// The selected recording and its detail, when both are loaded.
    var recordingPair: (row: RecRow, detail: EntryDetail)? {
        guard let row = recordings.selectedRow, let detail = recordings.detail, detail.id == row.sid else { return nil }
        return (row, detail)
    }

    /// "00:01:23 ★ words" lines on the clipboard.
    public func copyVlogTimecodes() {
        guard let (_, detail) = recordingPair else { return }
        let text = Timecodes.chapters(marks: detail.marks, sentences: detail.sentences)
        guard !text.isEmpty else { return }
        copy(text)
        recordings = recordings.with { $0.toast = Toast(text: tr("已复制为 Vlog 时间码"), isError: false) }
    }

    /// The transcript as <id>.srt, written only where the save panel says.
    public func exportSRT() {
        guard let (row, detail) = recordingPair else { return }
        let text = Timecodes.srt(sentences: detail.sentences, marks: detail.marks, duration: detail.length)
        guard !text.isEmpty else { return }
        Task {
            guard let url = await system.chooseDestination(row.sid + ".srt") else { return }
            let toast: Toast
            do {
                try Data(text.utf8).write(to: url, options: .atomic)
                toast = Toast(text: tr("已导出到 {0}", url.lastPathComponent), isError: false)
            } catch {
                toast = Toast(text: tr("没写成 {0}", url.lastPathComponent), isError: true)
            }
            recordings = recordings.with { $0.toast = toast }
        }
    }
}
