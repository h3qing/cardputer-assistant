// The player and the transcript, shared by the Inbox and Recordings: play / stop (space in the Inbox), and
// every sentence with its [mm:ss], a click plays from there.

import CardputerCore
import SwiftUI

struct PlayerBar: View {
    let playing: Bool
    let segments: Int
    let duration: String
    var failed: String?
    let toggle: () -> Void

    var body: some View {
        CardBox(padding: 12) {
            HStack(spacing: 12) {
                Button(action: toggle) {
                    HStack(spacing: 6) {
                        Image(systemName: playing ? "stop.fill" : "play.fill")
                        Text(playing ? tr("停") : tr("播放"))
                        KeyCap(text: tr("空格"), highlighted: false)
                    }
                }
                .buttonStyle(ThemedButtonStyle(kind: playing ? .secondary : .primary))
                .disabled(segments == 0)
                VStack(alignment: .leading, spacing: 2) {
                    Text(segments == 0 ? tr("没有音频") : (segments == 1 ? tr("{0} · 1 段", duration) : tr("{0} · {1} 段", duration, segments))).font(Theme.bodyStrong)
                        .foregroundStyle(segments == 0 ? Theme.dim : Theme.fg)
                    if let note {
                        Text(note).font(Theme.small).foregroundStyle(failed == nil ? Theme.dim : Theme.error)
                    }
                }
                .help(tr("从笔记目录直接读，只读"))
                Spacer(minLength: 0)
                if playing {
                    Image(systemName: "waveform").foregroundStyle(Theme.accent).font(.system(size: 18))
                }
            }
        }
    }

    var note: String? {
        if let failed {
            return failed
        }
        return segments == 0 ? tr("还在设备上，或者 30 天后已经清理") : nil
    }
}

struct PlayerCard: View {
    @Bindable var model: AppModel
    let entry: InboxEntry

    var body: some View {
        let detail = model.inbox.detail?.id == entry.id ? model.inbox.detail : nil
        PlayerBar(playing: model.player.playingID == entry.id, segments: detail?.audio.count ?? 0,
                  duration: Format.duration(ms: entry.durMs), failed: model.player.failed) { model.togglePlay() }
    }
}

/// Full text, the todos, what was handed to AI, and the transcript by sentence.
struct TranscriptCards: View {
    let detail: EntryDetail
    let playing: Bool
    let play: (Double) -> Void

    var body: some View {
        if let queueText = detail.queueText {
            CardBox {
                SectionLabel(text: tr("交给 AI 的内容") + (detail.queueID.map { tr(" · 排队号 {0}", $0) } ?? ""))
                Text(queueText).font(Theme.body).foregroundStyle(Theme.fg).fixedSize(horizontal: false, vertical: true)
            }
        }
        if !detail.todos.isEmpty {
            CardBox {
                SectionLabel(text: tr("待办"))
                ForEach(detail.todos, id: \.n) { todo in
                    Label(todoLine(todo), systemImage: "square").font(Theme.body).foregroundStyle(Theme.fg)
                }
            }
        }
        if !detail.summary.isEmpty {
            CardBox {
                SectionLabel(text: tr("纪要"))
                ForEach(Array(detail.summary.enumerated()), id: \.offset) { _, line in
                    if line.isEmpty {
                        Spacer().frame(height: 2)
                    } else {
                        Text(Self.plain(line)).font(line.hasPrefix("#") ? Theme.bodyStrong : Theme.body)
                            .foregroundStyle(Theme.fg).fixedSize(horizontal: false, vertical: true)
                    }
                }
            }
        }
        if !detail.sentences.isEmpty {
            CardBox {
                SectionLabel(text: tr("逐句转写 · 点一句从那里播"))
                ForEach(Array(detail.sentences.enumerated()), id: \.offset) { _, sentence in
                    SentenceRow(sentence: sentence) { play(sentence.seconds ?? 0) }
                }
            }
        } else if let text = detail.text {
            CardBox {
                SectionLabel(text: tr("全文"))
                Text(text).font(Theme.body).foregroundStyle(Theme.fg).fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    /// A heading without its # marks, a list item with a bullet.
    static func plain(_ line: String) -> String {
        if line.hasPrefix("#") {
            return line.drop { $0 == "#" }.trimmingCharacters(in: .whitespaces)
        }
        if line.hasPrefix("- ") || line.hasPrefix("* ") {
            return "• " + line.dropFirst(2)
        }
        return line
    }

    func todoLine(_ todo: Todo) -> String {
        ([todo.text] + [todo.owner, todo.due].compactMap { $0 }).joined(separator: " · ")
    }
}

struct SentenceRow: View {
    let sentence: TimedSentence
    let play: () -> Void

    var body: some View {
        Button(action: play) {
            HStack(alignment: .firstTextBaseline, spacing: 10) {
                Text(stamp).font(Theme.monoSmall).foregroundStyle(Theme.dim).frame(width: 48, alignment: .leading)
                Text(sentence.text).font(Theme.body).foregroundStyle(Theme.fg)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .disabled(sentence.seconds == nil)
    }

    var stamp: String {
        guard let seconds = sentence.seconds else { return "[--:--]" }
        let whole = Int(seconds)
        return String(format: "[%02d:%02d]", whole / 60, whole % 60)
    }
}
