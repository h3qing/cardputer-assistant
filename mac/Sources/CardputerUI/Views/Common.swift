// Pieces several screens share: the page header, the feedback line, a notice bar, Mando's empty state and
// a command to copy.

import CardputerCore
import SwiftUI

struct PageHeader<Trailing: View>: View {
    let title: String
    var subtitle: String?
    @ViewBuilder var trailing: Trailing

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: Theme.grid * 2) {
            VStack(alignment: .leading, spacing: 4) {
                Text(title).font(Theme.pageTitle).foregroundStyle(Theme.fg)
                if let subtitle {
                    Text(subtitle).font(Theme.body).foregroundStyle(Theme.ink2).lineLimit(1)
                }
            }
            Spacer(minLength: Theme.grid)
            trailing
        }
    }
}

extension PageHeader where Trailing == EmptyView {
    init(title: String, subtitle: String? = nil) {
        self.init(title: title, subtitle: subtitle) { EmptyView() }
    }
}

/// The 5 s line at the bottom: what happened, or what went wrong and what to do.
struct ToastView: View {
    let toast: Toast

    var body: some View {
        Label(toast.text, systemImage: toast.isError ? "exclamationmark.circle.fill" : "checkmark.circle.fill")
            .font(Theme.bodyStrong)
            .foregroundStyle(toast.isError ? Theme.error : Theme.fg)
            .padding(.horizontal, 16)
            .padding(.vertical, 10)
            .background(toast.isError ? Theme.recBg : Theme.select, in: Capsule())
            .overlay(Capsule().stroke(toast.isError ? Theme.error.opacity(0.5) : Theme.live, lineWidth: 1))
            .shadow(color: .black.opacity(0.4), radius: 12, y: 4)
    }
}

/// A notice across the page: what is going on and, when it helps, the one command that fixes it.
struct NoticeBar: View {
    let tone: Tone
    let symbol: String
    let text: String
    var command: String?
    var copy: ((String) -> Void)?

    var body: some View {
        HStack(alignment: .center, spacing: 10) {
            Image(systemName: symbol).foregroundStyle(tone.foreground)
            // The command keeps its width and the words wrap in the rest: never a column of single characters.
            Text(text).font(Theme.body).foregroundStyle(Theme.fg).fixedSize(horizontal: false, vertical: true)
                .layoutPriority(1)
            Spacer(minLength: Theme.grid)
            if let command, let copy {
                CommandChip(command: command, copy: copy).fixedSize()
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 10)
        .background(tone.background, in: RoundedRectangle(cornerRadius: Theme.smallRadius))
        .overlay(alignment: .leading) {
            RoundedRectangle(cornerRadius: 2).fill(tone.foreground).frame(width: 3).padding(.vertical, 6)
        }
    }
}

/// A command in monospace with a copy button that says Copied for a moment. `shown` is what it reads (the
/// home folder as ~); the copy is always `command` as it is.
struct CommandChip: View {
    let command: String
    var shown: String?
    let copy: (String) -> Void
    @State private var copied = false

    init(command: String, shown: String? = nil, copy: @escaping (String) -> Void) {
        self.command = command
        self.shown = shown
        self.copy = copy
    }

    var body: some View {
        HStack(spacing: 6) {
            Text(shown ?? command).font(Theme.mono).foregroundStyle(Theme.fg).lineLimit(1).truncationMode(.middle)
                .help(command)
            Button {
                copy(command)
                copied = true
                Task {
                    try? await Task.sleep(nanoseconds: 1_500_000_000)
                    copied = false
                }
            } label: {
                Label(copied ? tr("已复制") : tr("复制"), systemImage: copied ? "checkmark" : "doc.on.doc")
            }
            .buttonStyle(ThemedButtonStyle(kind: .quiet, compact: true))
        }
        .padding(.leading, 10)
        .padding(.vertical, 3)
        .background(Theme.bg.opacity(0.6), in: RoundedRectangle(cornerRadius: 6))
    }
}

/// A block of text to paste somewhere (a config snippet), with its copy button.
struct SnippetBox: View {
    let text: String
    let copy: (String) -> Void
    @State private var copied = false

    var body: some View {
        ZStack(alignment: .topTrailing) {
            Text(text)
                .font(Theme.monoSmall)
                .foregroundStyle(Theme.fg)
                .frame(maxWidth: .infinity, alignment: .leading)
                .fixedSize(horizontal: false, vertical: true)
                .padding(12)
                .padding(.trailing, 72)
                .background(Theme.bg, in: RoundedRectangle(cornerRadius: 8))
            Button {
                copy(text)
                copied = true
                Task {
                    try? await Task.sleep(nanoseconds: 1_500_000_000)
                    copied = false
                }
            } label: {
                Label(copied ? tr("已复制") : tr("复制"), systemImage: copied ? "checkmark" : "doc.on.doc")
            }
            .buttonStyle(ThemedButtonStyle(kind: .secondary, compact: true))
            .padding(8)
        }
    }
}

/// Mando and a few words, for an empty list or a page that cannot show anything yet.
struct EmptyState<Extra: View>: View {
    let pose: MandoPose
    let title: String
    var detail: String?
    @ViewBuilder var extra: Extra

    var body: some View {
        VStack(spacing: Theme.grid * 2) {
            MandoView(pose: pose, scale: pose.frame.height > 50 ? 2.5 : 3)
            Text(title).font(Theme.title).foregroundStyle(Theme.fg)
            if let detail {
                Text(detail).font(Theme.body).foregroundStyle(Theme.ink2).multilineTextAlignment(.center)
                    .frame(maxWidth: 420)
            }
            extra
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(Theme.inset * 2)
    }
}

extension EmptyState where Extra == EmptyView {
    init(pose: MandoPose, title: String, detail: String? = nil) {
        self.init(pose: pose, title: title, detail: detail) { EmptyView() }
    }
}

/// "Updated 12 s ago" under a page title when the last answer is getting old.
func staleNote(_ at: Date?, now: Date) -> String? {
    guard let at else { return nil }
    let seconds = Int(now.timeIntervalSince(at))
    return seconds >= 10 ? tr("上次更新：{0} 秒前", seconds) : nil
}

/// A path as people write it: the home folder (or its real path) as ~.
func displayPath(_ path: String, home: String) -> String {
    let homes = [home, (home as NSString).resolvingSymlinksInPath, "/private" + home].filter { $0.count > 1 }
    guard let match = homes.first(where: { path == $0 || path.hasPrefix($0 + "/") }) else { return path }
    return "~" + path.dropFirst(match.count)
}
