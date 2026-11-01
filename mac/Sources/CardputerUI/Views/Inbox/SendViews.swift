// Send to… and Waiting to send (docs/25 §5.4, §5.7). Waiting to send sits on top of the list: what waits to
// be sent from this Mac (held, or a suggestion from the project's usual destination; never sent by itself).
// Its detail sends to the preselected destination with ⌘↩, elsewhere with ⌥ + a letter, or drops it with ⌥D.
// A memo of the Inbox gets the same Send to… card. A grey destination is still clickable and says why; one
// that only waits for its one-time test opens it (Integrations → Destinations).

import CardputerCore
import SwiftUI

struct PendingGroup: View {
    @Bindable var model: AppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                SectionLabel(text: tr("等你送 {0}", model.dest.pending.count))
                Spacer()
                Text(tr("⌘↩ 送到选好的 · ⌥D 不送了")).font(Theme.small).foregroundStyle(Theme.dim)
            }
            .padding(.horizontal, 4)
            ForEach(model.dest.pending) { row in
                PendingRowView(row: row, selected: model.dest.focus == row.ref, now: model.now,
                               calendar: model.services.calendar)
                    .id("pending-" + row.ref)
                    .onTapGesture { model.selectPending(row.ref) }
            }
            if !model.inbox.entries.isEmpty {
                SectionLabel(text: tr("待归档")).padding(.horizontal, 4).padding(.top, 6)
            }
        }
    }
}

struct PendingRowView: View {
    let row: PendingRow
    let selected: Bool
    let now: Date
    let calendar: Calendar

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: row.st == .failed ? "exclamationmark.triangle" : "paperplane")
                .font(.system(size: 13, weight: .medium))
                .foregroundStyle(row.st == .failed ? Theme.error : Theme.accent)
                .frame(width: 18)
                .padding(.top, 1)
            VStack(alignment: .leading, spacing: 6) {
                Text(row.text ?? tr("（没有字可显示）"))
                    .font(row.text == nil ? Theme.body : Theme.bodyStrong)
                    .foregroundStyle(row.text == nil ? Theme.dim : Theme.fg)
                    .lineLimit(2)
                    .fixedSize(horizontal: false, vertical: true)
                Text(PendingWords.meta(row, now: now, calendar: calendar)).font(Theme.small).foregroundStyle(Theme.dim)
                    .lineLimit(1)
                HStack(spacing: 5) {
                    Pill(text: "→ " + row.label, tone: .info, symbol: "paperplane")
                    Pill(text: PendingWords.why(row), tone: row.st == .failed ? .error : .neutral)
                }
            }
            Spacer(minLength: 0)
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
}

/// The words of a Waiting to send row.
enum PendingWords {
    static func why(_ row: PendingRow) -> String {
        switch (row.why.known, row.st) {
        case (.held?, .failed?): return tr("没送出：") + DestFeedback.code(row.code ?? "")
        case (.held?, _): return tr("暂存")
        case (.usual?, _): return tr("常用去向 · 建议")
        case (.expired?, _): return tr("过期了")
        case (nil, _): return row.why.raw
        }
    }

    /// Voice · #acmeshop · 3 min ago
    static func meta(_ row: PendingRow, now: Date, calendar: Calendar) -> String {
        let source = row.src.known == .typed ? tr("打字") : (row.src.known == .voice ? tr("语音") : row.src.raw)
        let when = Format.instant(row.at).map { Format.relative($0, now: now, calendar: calendar) }
        return ([source] + [row.project.map { "#" + $0 }, when].compactMap { $0 }).joined(separator: " · ")
    }

    /// The sentence on top of a row's detail.
    static func sentence(_ row: PendingRow) -> String {
        switch (row.why.known, row.st) {
        case (.held?, .failed?):
            return tr("上次没送到 {0}：{1}。可以再送一次，或换一个去向。", row.label, DestFeedback.code(row.code ?? ""))
        case (.held?, _): return tr("暂存着，等你决定送不送到 {0}。", row.label)
        case (.usual?, _):
            return tr("{0} 的常用去向是 {1}：只是建议，不按就不会送。", row.project.map { "#" + $0 } ?? tr("这个项目"), row.label)
        default: return tr("要送到 {0} 吗？", row.label)
        }
    }
}

/// The detail of a Waiting to send row: ⌘↩ to the preselected destination, ⌥D not to send, or Send to…
/// elsewhere.
struct PendingDetailView: View {
    @Bindable var model: AppModel
    let row: PendingRow

    var body: some View {
        let choices = model.sendChoices
        let preselected = choices.first { $0.id == row.dest }
        ThemedScroll {
            VStack(alignment: .leading, spacing: 14) {
                Text(row.text ?? tr("（没有字可显示：送的时候 cpa 自己读便条全文）"))
                    .font(Theme.title).foregroundStyle(row.text == nil ? Theme.ink2 : Theme.fg).lineLimit(4)
                    .fixedSize(horizontal: false, vertical: true)
                Text(PendingWords.meta(row, now: model.now, calendar: model.services.calendar))
                    .font(Theme.small).foregroundStyle(Theme.dim)
                CardBox {
                    Text(PendingWords.sentence(row)).font(Theme.body).foregroundStyle(Theme.fg)
                        .fixedSize(horizontal: false, vertical: true)
                    HStack(spacing: 8) {
                        Button { send(preselected) } label: {
                            HStack(spacing: 6) { Image(systemName: "paperplane.fill"); Text(tr("送到 ") + row.label); KeyCap(text: "⌘↩") }
                        }
                        .buttonStyle(ThemedButtonStyle(kind: .primary))
                        .opacity(preselected?.blocked == nil ? 1 : 0.5)
                        .keyboardShortcut(.return, modifiers: .command)
                        Button { model.dropFocused() } label: {
                            HStack(spacing: 6) { Text(tr("不送了")); KeyCap(text: "⌥D") }
                        }
                        .buttonStyle(ThemedButtonStyle(kind: .quiet))
                        .keyboardShortcut("d", modifiers: .option)
                    }
                    .disabled(model.inbox.busy)
                    if let reason = preselected?.blocked ?? (preselected == nil ? tr("{0} 现在不能从这台 Mac 送", row.label) : nil) {
                        Reason(text: reason)
                    }
                }
                SendToCard(model: model, choices: choices, title: tr("或者送到…"), reserved: ["d"])
            }
            .padding(.bottom, 60)
        }
    }

    func send(_ choice: DestChoice?) {
        guard let choice else {
            model.say(Toast(text: tr("{0} 现在不能从这台 Mac 送", row.label), isError: true))
            return
        }
        if choice.needsTest {
            model.openTest(choice.dest)
        } else if let reason = choice.blocked {
            model.say(Toast(text: reason, isError: true))
        } else {
            model.send(to: nil)
        }
    }
}

/// Send to…: one button per destination, the preselected first; ⌥ + its letter does the same.
struct SendToCard: View {
    @Bindable var model: AppModel
    let choices: [DestChoice]
    var title = tr("送到…")
    /// Letters ⌥ already means something else for (⌥D drops a Waiting to send row).
    var reserved: Set<String> = []
    /// A memo not filed yet: what is sent carries no #project.
    var noProject = false

    var body: some View {
        if !choices.isEmpty {
            CardBox {
                HStack {
                    SectionLabel(text: title)
                    Spacer()
                    Text(tr("⌥ 加字母，一下就送")).font(Theme.small).foregroundStyle(Theme.dim)
                }
                FlowLayout(spacing: 8, lineSpacing: 8) {
                    ForEach(choices) { choice in
                        button(choice)
                    }
                }
                .disabled(model.inbox.busy)
                ForEach(reasons, id: \.self) { reason in
                    Reason(text: reason)
                }
                if noProject {
                    Reason(text: tr("没有项目：送过去不带 #项目；要带就先按数字归档"))
                }
                Reason(text: tr("送出去的字只填进输入框：到那个 app 里按回车才发出。"))
            }
        }
    }

    /// "<label>: it may send by itself…": each reason once, with the destinations it greys.
    var reasons: [String] {
        let grey = choices.filter { $0.blocked != nil }
        let order = grey.compactMap(\.blocked).reduce(into: [String]()) { if !$0.contains($1) { $0.append($1) } }
        return order.map { reason in
            grey.filter { $0.blocked == reason }.map(\.dest.label).joined(separator: tr("、")) + tr("：") + reason
        }
    }

    func button(_ choice: DestChoice) -> some View {
        Button { tap(choice) } label: {
            HStack(spacing: 6) {
                Image(systemName: DestSymbols.kind(choice.dest.kind))
                Text(choice.dest.label)
                if choice.dest.leavesMac {
                    Image(systemName: "icloud").font(.system(size: 10)).foregroundStyle(Theme.dim)
                }
                if let key = choice.dest.key {
                    KeyCap(text: "⌥" + key.uppercased())
                }
            }
        }
        .buttonStyle(ThemedButtonStyle())
        .opacity(choice.blocked == nil ? 1 : 0.5)
        .help(choice.blocked ?? DestChoices.state(choice.dest))
        .modifier(OptionShortcut(key: choice.dest.key.flatMap { reserved.contains($0) ? nil : $0 }))
    }

    func tap(_ choice: DestChoice) {
        if choice.needsTest {
            model.openTest(choice.dest)
        } else if let reason = choice.blocked {
            model.say(Toast(text: reason, isError: true))
        } else {
            model.send(to: choice.id)
        }
    }
}

/// ⌥ + the letter, when there is one.
struct OptionShortcut: ViewModifier {
    let key: String?

    func body(content: Content) -> some View {
        if let key, let char = key.first {
            content.keyboardShortcut(KeyEquivalent(char), modifiers: .option)
        } else {
            content
        }
    }
}

enum DestSymbols {
    static func kind(_ kind: String) -> String {
        switch kind {
        case "url": return "link"
        case "app": return "macwindow"
        case "file": return "doc.text"
        case "clip": return "doc.on.clipboard"
        default: return "paperplane"
        }
    }
}

/// The line after a text went to the clipboard: it stays until Got it (the clipboard clears itself after 2
/// minutes), with Copy again for the same text once more.
struct StickyToastView: View {
    @Bindable var model: AppModel
    let toast: Toast

    var body: some View {
        HStack(spacing: 10) {
            Label(toast.text, systemImage: "doc.on.clipboard").font(Theme.bodyStrong).foregroundStyle(Theme.fg)
            if model.dest.recopy != nil {
                Button(tr("再复制")) { model.recopy() }.buttonStyle(ThemedButtonStyle(compact: true))
                    .disabled(model.inbox.busy)
            }
            Button(tr("知道了")) { model.dismissToast() }.buttonStyle(ThemedButtonStyle(kind: .quiet, compact: true))
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 8)
        .background(Theme.select, in: Capsule())
        .overlay(Capsule().stroke(Theme.live, lineWidth: 1))
        .shadow(color: .black.opacity(0.4), radius: 12, y: 4)
    }
}
