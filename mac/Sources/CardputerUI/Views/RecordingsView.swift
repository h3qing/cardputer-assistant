// Recordings (docs/24 §2.2): every recording from `cpa rec ls --json`, titled by its words (result.json, else
// the transcript; read once per state) or its time and kind, with the time, length, project, ★ and state
// below; hidden ones on request with Show again. The detail reads the open files (transcript, summary.md as
// text, audio), says why a transcription stopped and can show the folder in Finder.

import CardputerCore
import SwiftUI

struct RecordingsView: View {
    @Bindable var model: AppModel

    var state: RecordingsState {
        model.recordings
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            PageHeader(title: tr("录音"), subtitle: subtitle) {
                Toggle(isOn: Binding(get: { state.showHidden }, set: { model.setShowHidden($0) })) {
                    Text(tr("显示藏起来的")).font(Theme.body).foregroundStyle(Theme.ink2).fixedSize()
                }
                .toggleStyle(ThemedSwitchStyle())
                .fixedSize()
            }
            if let error = state.error {
                NoticeBar(tone: .error, symbol: "exclamationmark.triangle", text: error)
            }
            if state.rows.isEmpty {
                EmptyState(pose: .wait, title: tr("还没有录音"), detail: tr("按住设备上的 G0 说一段，连上这台 Mac 以后会取回到这里。"))
            } else {
                HStack(alignment: .top, spacing: 14) {
                    ThemedScroll {
                        LazyVStack(spacing: 6) {
                            ForEach(state.rows) { row in
                                RecordingRow(row: row, words: state.words[row.sid], selected: row.sid == state.selected,
                                             projects: model.projects, now: model.now, calendar: model.services.calendar)
                                    .onTapGesture { model.selectRecording(row.sid) }
                            }
                        }
                        .padding(.bottom, 40)
                    }
                    .frame(width: Theme.listWidth)
                    RecordingDetail(model: model)
                }
            }
        }
        .padding(.horizontal, 22)
        .padding(.top, 18)
        .overlay(alignment: .bottom) {
            if let toast = state.toast {
                ToastView(toast: toast).padding(.bottom, 18)
            }
        }
    }

    /// "23 recordings · 2 still on the device": the segments on the device are cpa status's count, as on the
    /// Device page.
    var subtitle: String {
        let device = model.status.flatMap(DeviceFacts.onDevice).map { " · " + $0 } ?? ""
        return tr("{0} 条录音", state.rows.count) + device
    }
}

struct RecordingRow: View {
    let row: RecRow
    let words: Headline?
    let selected: Bool
    let projects: ProjectMap
    let now: Date
    let calendar: Calendar

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: row.mode?.known == .meeting ? "person.2" : "note.text")
                .foregroundStyle(Theme.dim).frame(width: 18).padding(.top, 1)
            VStack(alignment: .leading, spacing: 6) {
                HStack(alignment: .top, spacing: 6) {
                    Text(title).font(hasWords ? Theme.bodyStrong : Theme.body)
                        .foregroundStyle(row.hidden || !hasWords ? Theme.dim : Theme.fg)
                        .lineLimit(2).fixedSize(horizontal: false, vertical: true)
                    if row.starred {
                        Text("★").foregroundStyle(Theme.warn)
                    }
                    Spacer(minLength: 4)
                    Pill(text: Labels.recState(row.state), tone: Self.tone(row.state))
                }
                HStack(spacing: 6) {
                    Text(meta).font(Theme.small).foregroundStyle(Theme.dim)
                    if let project = row.project {
                        Pill(text: projects.label(key: project), tone: .info, symbol: "folder")
                    }
                    if row.hidden {
                        Pill(text: tr("藏起来了"), symbol: "eye.slash")
                    }
                }
            }
        }
        .padding(.vertical, 10)
        .padding(.horizontal, 12)
        .background(selected ? Theme.select : Theme.card, in: RoundedRectangle(cornerRadius: 8))
        .overlay(alignment: .leading) {
            if selected {
                RoundedRectangle(cornerRadius: 2).fill(Theme.accent).frame(width: 3).padding(.vertical, 8)
            }
        }
        .contentShape(Rectangle())
    }

    var at: RecordedAt {
        RecordedAt(wallMs: row.startedMs, tzMin: nil, uncertain: row.uncertain)
    }

    var hasWords: Bool {
        words?.title != nil || words?.text != nil
    }

    /// The title, else the first words, else "Memo at 13:25".
    var title: String {
        words?.title ?? words?.text ?? Labels.untitled(kind: row.mode, at: at, calendar: calendar)
    }

    var meta: String {
        [row.mode.map(Labels.kind) ?? tr("录音"), Format.duration(ms: row.durMs), Format.when(at, now: now, calendar: calendar)]
            .joined(separator: " · ")
    }

    static func tone(_ state: Open<RecState>) -> Tone {
        switch state.known {
        case .done, .transcript: return .ok
        case .failed: return .error
        case .blocked, .stale: return .warn
        default: return .neutral
        }
    }
}

struct RecordingDetail: View {
    @Bindable var model: AppModel

    var body: some View {
        if let row = model.recordings.selectedRow {
            let detail = model.recordings.detail?.id == row.sid ? model.recordings.detail : nil
            ThemedScroll {
                VStack(alignment: .leading, spacing: 14) {
                    RecordingHeading(model: model, row: row, detail: detail)
                    FlowLayout(spacing: 8, lineSpacing: 8) {
                        if let session = detail?.session {
                            Button { model.reveal(session) } label: { Label(tr("在 Finder 中显示"), systemImage: "folder") }
                                .buttonStyle(ThemedButtonStyle())
                        }
                        if row.hidden {
                            Button { model.unhide(row.sid) } label: { Label(tr("重新显示"), systemImage: "eye") }
                                .buttonStyle(ThemedButtonStyle())
                        }
                        if let detail, !Timecodes.chapters(marks: detail.marks, sentences: detail.sentences).isEmpty {
                            Button { model.copyVlogTimecodes() } label: {
                                Label(tr("复制为 Vlog 时间码"), systemImage: "film")
                            }
                            .buttonStyle(ThemedButtonStyle())
                        }
                        if let detail, detail.sentences.contains(where: { $0.seconds != nil }) {
                            Button { model.exportSRT() } label: { Label(tr("导出 SRT"), systemImage: "captions.bubble") }
                                .buttonStyle(ThemedButtonStyle())
                        }
                    }
                    if row.dir == nil {
                        Reason(text: tr("这段还在设备上，取回以后才能听和看。"))
                    }
                    if let stopped = detail?.stopped, row.state.known == .failed || row.state.known == .blocked {
                        StoppedLine(model: model, text: stopped)
                    }
                    PlayerBar(playing: model.player.playingID == row.sid, segments: detail?.audio.count ?? 0,
                              duration: Format.duration(ms: row.durMs), failed: model.player.failed) {
                        model.playRecording()
                    }
                    if let detail {
                        TranscriptCards(detail: detail, playing: model.player.playingID == row.sid) { seconds in
                            model.playRecording(from: seconds)
                        }
                    }
                }
                .padding(.bottom, 40)
            }
        } else {
            EmptyState(pose: .wait, title: tr("选一段录音"))
        }
    }
}

/// A recording's words (or its time and kind) and what it is.
struct RecordingHeading: View {
    @Bindable var model: AppModel
    let row: RecRow
    let detail: EntryDetail?

    var body: some View {
        let words = model.recordings.words[row.sid]
        let at = RecordedAt(wallMs: row.startedMs, tzMin: nil, uncertain: row.uncertain)
        Text(words?.title ?? detail?.text ?? Labels.untitled(kind: row.mode, at: at, calendar: model.services.calendar))
            .font(Theme.title)
            .foregroundStyle(words?.title == nil && detail?.text == nil ? Theme.ink2 : Theme.fg)
            .lineLimit(3).fixedSize(horizontal: false, vertical: true)
        FlowLayout(spacing: 6, lineSpacing: 6) {
            Pill(text: [row.mode.map(Labels.kind) ?? tr("录音"), Format.duration(ms: row.durMs)].joined(separator: " · "),
                 symbol: row.mode?.known == .meeting ? "person.2" : "note.text")
            Pill(text: Format.when(at, now: model.now, calendar: model.services.calendar), symbol: "clock")
            Pill(text: Labels.recState(row.state), tone: RecordingRow.tone(row.state))
            if let project = row.project {
                Pill(text: model.projects.label(key: project), tone: .info, symbol: "folder")
            }
        }
    }
}

/// Why a transcription stopped, and Retry the failed ones.
struct StoppedLine: View {
    @Bindable var model: AppModel
    let text: String

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: "exclamationmark.triangle").foregroundStyle(Theme.error)
            Text(text).font(Theme.body).foregroundStyle(Theme.fg)
            Spacer(minLength: 8)
            RetryButton(model: model)
        }
        .padding(12)
        .background(Theme.recBg, in: RoundedRectangle(cornerRadius: Theme.radius))
    }
}
