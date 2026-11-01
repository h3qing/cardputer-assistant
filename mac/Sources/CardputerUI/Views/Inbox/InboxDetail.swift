// One recording of the Inbox: the words, where and when, a suggested project (Enter approves), the 1-9
// buttons with the project names on them (the current one lit), Hand to AI / Done / Hide…, the player and the
// transcript sentence by sentence (click one to play from there). A grey button always says why; the
// read-only list shows only what it can do (listen, Finder) and one line on what filing needs.

import CardputerCore
import SwiftUI

struct InboxDetailView: View {
    @Bindable var model: AppModel

    var body: some View {
        if let row = model.dest.focusedRow {
            PendingDetailView(model: model, row: row)
        } else if let entry = model.inbox.selectedEntry {
            ThemedScroll {
                VStack(alignment: .leading, spacing: 14) {
                    DetailHeading(entry: entry, model: model)
                    StuckCard(model: model, entry: entry)
                    ConfirmCard(model: model, entry: entry)
                    if entry.filing == nil {
                        ReadOnlyCard(model: model)
                    } else {
                        SuggestionCard(model: model, entry: entry)
                        ProjectButtons(model: model, entry: entry)
                        ActionButtons(model: model, entry: entry)
                        SendToCard(model: model, choices: model.sendChoices, noProject: entry.project == nil)
                    }
                    PlayerCard(model: model, entry: entry)
                    if let detail = model.inbox.detail, detail.id == entry.id {
                        TranscriptCards(detail: detail, playing: model.player.playingID == entry.id) { seconds in
                            model.play(from: seconds)
                        }
                    }
                }
                .padding(.bottom, 60)
            }
        } else {
            EmptyState(pose: .wait, title: tr("选一条录音"), detail: tr("用 ↑ ↓ 或 j k 在左边移动。"))
        }
    }
}

struct DetailHeading: View {
    let entry: InboxEntry
    @Bindable var model: AppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(entry.title(calendar: model.services.calendar))
                .font(Theme.title).foregroundStyle(entry.hasWords ? Theme.fg : Theme.ink2).lineLimit(3)
                .fixedSize(horizontal: false, vertical: true)
            FlowLayout(spacing: 6, lineSpacing: 6) {
                Pill(text: (entry.kind.map(Labels.kind) ?? tr("录音")) + " · " + Format.duration(ms: entry.durMs),
                     symbol: entry.kind?.known == .meeting ? "person.2" : "note.text")
                Pill(text: Format.when(entry.at, now: model.now, calendar: model.services.calendar), symbol: "clock")
                if let pulled = Labels.pulled(entry.env, thisMac: model.hostName) {
                    Pill(text: pulled, symbol: "desktopcomputer")
                }
                Pill(text: Labels.transcription(entry.tx), tone: Tone.of(entry.tx), symbol: Tone.symbol(entry.tx))
                if let filing = entry.filing, !(filing == .new && model.inbox.query.scope == .pending) {
                    Pill(text: Labels.filing(filing), tone: filing == .new ? .info : .neutral)
                }
            }
        }
    }
}

/// A transcription that failed or cannot run: why, and Retry the failed ones (cpa-stt retry) when cpa-stt is
/// here.
struct StuckCard: View {
    @Bindable var model: AppModel
    let entry: InboxEntry

    var body: some View {
        if entry.tx.known == .failed || entry.tx.known == .blocked {
            let detail = model.inbox.detail?.id == entry.id ? model.inbox.detail : nil
            HStack(spacing: 10) {
                Image(systemName: Tone.symbol(entry.tx)).foregroundStyle(Tone.of(entry.tx).foreground)
                Text(detail?.stopped ?? Labels.transcription(entry.tx)).font(Theme.body).foregroundStyle(Theme.fg)
                Spacer(minLength: 8)
                RetryButton(model: model)
            }
            .padding(12)
            .background(Theme.recBg, in: RoundedRectangle(cornerRadius: Theme.radius))
        }
    }
}

/// cpa-stt retry: only cpa-stt's own failed recordings, so only when cpa-stt is installed.
struct RetryButton: View {
    @Bindable var model: AppModel

    var body: some View {
        if model.features?.stt == .available {
            Button { model.retryTranscription() } label: { Label(tr("重新转写失败的"), systemImage: "arrow.clockwise") }
                .buttonStyle(ThemedButtonStyle(compact: true))
                .help(tr("cpa-stt retry：重做 cpa-stt 自己转失败的录音，并解除“跑不了”"))
        }
    }
}

/// The read-only list: listening works; the rest needs cpa filing.
struct ReadOnlyCard: View {
    @Bindable var model: AppModel

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: "lock").foregroundStyle(Theme.ink2)
            Text(InboxEntry.needsFiling).font(Theme.body).foregroundStyle(Theme.ink2)
            Spacer(minLength: 8)
            if let session = model.inbox.detail?.session {
                Button { model.reveal(session) } label: { Label(tr("在 Finder 中显示"), systemImage: "folder") }
                    .buttonStyle(ThemedButtonStyle(kind: .quiet, compact: true))
            }
        }
        .padding(12)
        .background(Theme.card, in: RoundedRectangle(cornerRadius: Theme.radius))
    }
}

/// The project the words name (a word match until a local model's suggestion exists, G3): Enter approves.
struct SuggestionCard: View {
    @Bindable var model: AppModel
    let entry: InboxEntry

    var body: some View {
        if let slot = model.suggestion, model.inbox.pending == .none {
            HStack(spacing: 10) {
                Image(systemName: "lightbulb").foregroundStyle(Theme.accent)
                Text(Suggestion.line(slot)).font(Theme.bodyStrong).foregroundStyle(Theme.fg)
                Spacer(minLength: 8)
                Button { model.press(.enter) } label: { Label(tr("批准"), systemImage: "return") }
                    .buttonStyle(ThemedButtonStyle(kind: .primary))
                    .disabled(model.inbox.busy)
            }
            .padding(12)
            .background(Theme.infoBg, in: RoundedRectangle(cornerRadius: Theme.radius))
        }
    }
}

/// While a key waits for the next (another Mac's project, or handing without a project): the same choice
/// as buttons.
struct ConfirmCard: View {
    @Bindable var model: AppModel
    let entry: InboxEntry

    var body: some View {
        switch model.inbox.pending {
        case .confirmFile(let id, let project) where id == entry.id:
            let target = project.map { tr("归到 ") + model.projects.label(key: $0) } ?? tr("退回没项目")
            card(tr("这段录音按的是别的电脑的项目（{0}）。按 Enter {1}，Esc 取消。", entry.asked ?? "", target), confirm: target)
        case .pickProject(let id, _) where id == entry.id:
            card(tr("交给 AI 之前选一个项目：按数字键，或点下面的项目；按 Enter 不选。"), confirm: tr("不选，直接交"))
        default:
            EmptyView()
        }
    }

    func card(_ text: String, confirm: String) -> some View {
        HStack(spacing: 10) {
            Image(systemName: "questionmark.circle.fill").foregroundStyle(Theme.warn)
            Text(text).font(Theme.body).foregroundStyle(Theme.fg).fixedSize(horizontal: false, vertical: true)
            Spacer(minLength: 8)
            Button { model.press(.enter) } label: { Label(confirm, systemImage: "return") }
                .buttonStyle(ThemedButtonStyle(kind: .primary))
            Button(tr("取消")) { model.press(.escape) }
                .buttonStyle(ThemedButtonStyle(kind: .quiet))
        }
        .padding(12)
        .background(Theme.warnBg, in: RoundedRectangle(cornerRadius: Theme.radius))
    }
}

extension InboxEntry {
    /// It has a title or a first sentence to show.
    var hasWords: Bool {
        title != nil || text != nil
    }

    /// The words of a row, or its time and kind ("Memo at 13:25"; the state says why, next to it).
    func title(calendar: Calendar) -> String {
        hasWords ? headline : Labels.untitled(kind: kind, at: at, calendar: calendar)
    }
}
