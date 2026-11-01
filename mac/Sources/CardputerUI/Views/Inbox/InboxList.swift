// The list of the Inbox: per row the words (title or first sentence, two lines), memo or meeting and how
// long, when (relative and the clock, ~ when unsure), which Mac pulled it, the transcription state (icon,
// word and colour), and the project or the other Mac's key. The device shows only when there are two or more.

import CardputerCore
import SwiftUI

struct InboxList: View {
    @Bindable var model: AppModel

    var body: some View {
        let entries = model.inbox.entries
        let devices = Set(entries.compactMap(\.device))
        ScrollViewReader { proxy in
            ThemedScroll {
                LazyVStack(spacing: 6) {
                    if !model.dest.pending.isEmpty {
                        PendingGroup(model: model)
                    }
                    ForEach(entries) { entry in
                        InboxRow(entry: entry, selected: entry.id == model.inbox.selected && model.dest.focus == nil,
                                 projects: model.projects,
                                 now: model.now, calendar: model.services.calendar, showDevice: devices.count > 1,
                                 showFiling: model.inbox.query.scope != .pending, thisMac: model.hostName)
                            .id(entry.id)
                            .onTapGesture { model.select(entry.id) }
                    }
                }
                .padding(.bottom, 60)
            }
            .onChange(of: model.inbox.selected) { _, id in
                guard let id else { return }
                withAnimation(.easeOut(duration: 0.15)) { proxy.scrollTo(id) }
            }
            .onChange(of: model.dest.focus) { _, ref in
                guard let ref else { return }
                withAnimation(.easeOut(duration: 0.15)) { proxy.scrollTo("pending-" + ref) }
            }
        }
    }
}

struct InboxRow: View {
    let entry: InboxEntry
    let selected: Bool
    let projects: ProjectMap
    let now: Date
    let calendar: Calendar
    let showDevice: Bool
    let showFiling: Bool
    let thisMac: String?

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: Tone.symbol(entry.tx))
                .font(.system(size: 13, weight: .medium))
                .foregroundStyle(Tone.of(entry.tx).foreground)
                .frame(width: 18)
                .padding(.top, 1)
            VStack(alignment: .leading, spacing: 6) {
                Text(entry.title(calendar: calendar))
                    .font(entry.filing == .new && entry.hasWords ? Theme.bodyStrong : Theme.body)
                    .foregroundStyle(entry.hasWords ? Theme.fg : Theme.dim)
                    .lineLimit(2)
                    .fixedSize(horizontal: false, vertical: true)
                Text(meta).font(Theme.small).foregroundStyle(Theme.dim).lineLimit(1)
                pills
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

    /// Memo · 24s · 3 min ago · 14:27 · Office
    var meta: String {
        let kind = entry.kind.map(Labels.kind) ?? tr("录音")
        let parts = [kind, Format.duration(ms: entry.durMs), Format.when(entry.at, now: now, calendar: calendar)]
            + [Labels.mac(entry.env, thisMac: thisMac)].compactMap { $0 }
            + (showDevice ? entry.device.map { [tr("设备 …") + $0.suffix(4)] } ?? [] : [])
        return parts.joined(separator: " · ")
    }

    @ViewBuilder var pills: some View {
        let tags = tagList
        if !tags.isEmpty {
            HStack(spacing: 5) {
                ForEach(tags, id: \.text) { tag in
                    Pill(text: tag.text, tone: tag.tone, symbol: tag.symbol)
                }
            }
        }
    }

    struct Tag {
        let text: String
        let tone: Tone
        var symbol: String?
    }

    var tagList: [Tag] {
        let project = entry.project.map { Tag(text: projects.label(key: $0), tone: .info, symbol: "folder") }
        let asked = entry.asked.map { Tag(text: tr("别的电脑的项目 · {0}", $0), tone: .warn, symbol: "desktopcomputer") }
        let tx = entry.tx.known == .done ? nil : Tag(text: Labels.transcription(entry.tx), tone: Tone.of(entry.tx))
        let filing = showFiling ? entry.filing.map { Tag(text: Labels.filing($0), tone: .neutral) } : nil
        let star = entry.starred ? Tag(text: "★", tone: .warn) : nil
        let todo = entry.intent?.known == .todo ? Tag(text: tr("待办"), tone: .neutral, symbol: "checklist") : nil
        return [project, asked, filing, tx, todo, star].compactMap { $0 }
    }
}
