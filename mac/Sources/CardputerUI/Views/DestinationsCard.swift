// Integrations → Destinations (docs/25 §3.1, §5.7): the destinations already added (letter, kind, leaves the
// Mac or stays on it, tested or not, a test button), each project's usual destination, and the apps on this
// Mac a destination can be added for (cpa dest presets --installed): Add asks first with cpa's own note, then
// runs `cpa dest add --preset P --yes` (with the chat's link when the preset takes one). Send a test asks in
// place ("Did it appear?" "Did it send by itself without you pressing Return?"). Every name, letter and note
// is cpa's data; the app has no branch for any app.

import CardputerCore
import SwiftUI

struct DestinationsCardBody: View {
    @Bindable var model: AppModel

    var state: DestState {
        model.dest
    }

    var added: [DestEntry] {
        state.dests.filter { ["url", "app", "file", "clip"].contains($0.kind) }
            .sorted { ($0.key == nil ? 1 : 0, $0.slot) < ($1.key == nil ? 1 : 0, $1.slot) }
    }

    var body: some View {
        if let note = state.note {
            Label(note.text, systemImage: note.isError ? "exclamationmark.triangle" : "checkmark.circle")
                .font(Theme.body).foregroundStyle(note.isError ? Theme.error : Theme.ok)
                .fixedSize(horizontal: false, vertical: true)
        }
        if let problem = state.catalog.problem {
            Reason(text: tr("destinations.json：") + problem)
        }
        TestPanel(model: model)
        SectionLabel(text: tr("已加的去向"))
        if added.isEmpty {
            Reason(text: tr("还没加：从下面“这台 Mac 上能接的”里选一个，点加上"))
        }
        ForEach(added) { dest in
            AddedRow(model: model, dest: dest)
        }
        UsualRows(model: model, choices: added.filter(\.isOn))
        SectionLabel(text: tr("这台 Mac 上能接的")).padding(.top, 4)
        if state.presets.presets.isEmpty {
            Reason(text: tr("没找到装了的 app（cpa dest presets --installed）"))
        }
        ForEach(state.presets.presets) { preset in
            PresetRow(model: model, preset: preset, added: state.dests.contains { $0.id == preset.destID && $0.isOn })
        }
    }
}

/// One destination: its letter, name, kind, whether it leaves the Mac, its state, and Send a test.
struct AddedRow: View {
    @Bindable var model: AppModel
    let dest: DestEntry

    var body: some View {
        HStack(spacing: 10) {
            KeyCap(text: dest.key.map { "⌥" + $0.uppercased() } ?? "–", highlighted: dest.key != nil)
            Image(systemName: DestSymbols.kind(dest.kind)).foregroundStyle(Theme.accent).frame(width: 16)
            Text(dest.label).font(Theme.bodyStrong).foregroundStyle(Theme.fg)
            Pill(text: dest.leavesMac ? tr("会离开这台 Mac") : tr("只在本机"), tone: dest.leavesMac ? .warn : .neutral,
                 symbol: dest.leavesMac ? "icloud" : "desktopcomputer")
            Text(DestChoices.state(dest)).font(Theme.small).foregroundStyle(DestChoices.mayAct(dest) ? Theme.warn : Theme.dim)
                .lineLimit(1)
            Spacer(minLength: 8)
            if dest.isOn, ["url", "app"].contains(dest.kind) {
                Button { model.startTest(dest) } label: { Label(tr("送一条测试"), systemImage: "paperplane") }
                    .buttonStyle(ThemedButtonStyle(compact: true))
                    .disabled(model.dest.test != .idle)
            }
        }
        .padding(10)
        .background(Theme.bg.opacity(0.6), in: RoundedRectangle(cornerRadius: 8))
    }
}

/// Each project's usual destination: preselected for its memos in Waiting to send, never sent by itself. One
/// click per choice: a chip per destination; None once one is set.
struct UsualRows: View {
    @Bindable var model: AppModel
    let choices: [DestEntry]

    var body: some View {
        if !choices.isEmpty, !model.projects.slots.isEmpty {
            SectionLabel(text: tr("项目的常用去向（只在等你送里预选，不会自己送）")).padding(.top, 4)
            VStack(alignment: .leading, spacing: 6) {
                ForEach(model.projects.slots) { slot in
                    row(slot)
                }
            }
            .disabled(model.dest.busy)
        }
    }

    func row(_ slot: ProjectSlot) -> some View {
        let current = model.dest.usual.dest(for: slot.key)
        return HStack(spacing: 6) {
            KeyCap(text: String(slot.slot))
            Text(slot.name).font(Theme.body).foregroundStyle(Theme.fg).frame(width: 96, alignment: .leading)
                .lineLimit(1)
            ForEach(choices) { dest in
                Chip(text: dest.label, on: current == dest.id) {
                    model.setUsual(slot.key, to: current == dest.id ? nil : dest.id)
                }
            }
            if current != nil {
                Chip(text: tr("不设"), on: false) { model.setUsual(slot.key, to: nil) }
            }
            Spacer(minLength: 0)
        }
    }
}

/// An app on this Mac: its name and version, whether what you send leaves the Mac, and Add (asks first).
struct PresetRow: View {
    @Bindable var model: AppModel
    let preset: PresetEntry
    let added: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 10) {
                Image(systemName: DestSymbols.kind(preset.kind)).foregroundStyle(Theme.accent).frame(width: 16)
                Text(preset.label).font(Theme.bodyStrong).foregroundStyle(Theme.fg)
                if let version = preset.version {
                    Text(tr("已安装 ") + version).font(Theme.small).foregroundStyle(Theme.dim)
                }
                if let key = preset.keyHint {
                    Text(tr("字母 ") + key.uppercased()).font(Theme.small).foregroundStyle(Theme.dim)
                }
                Spacer(minLength: 8)
                if added {
                    Pill(text: tr("已加上"), tone: .ok, symbol: "checkmark")
                } else {
                    Button { model.askAdd(preset) } label: { Label(tr("加上"), systemImage: "plus") }
                        .buttonStyle(ThemedButtonStyle(kind: .primary, compact: true))
                        .disabled(model.dest.busy || model.dest.adding != nil)
                }
            }
            if model.dest.adding == preset {
                AddConfirm(model: model, preset: preset)
            }
        }
        .padding(10)
        .background(Theme.bg.opacity(0.6), in: RoundedRectangle(cornerRadius: 8))
    }
}

/// The app's own confirmation before `cpa dest add --yes`: cpa's note, the letter, where the words go.
struct AddConfirm: View {
    @Bindable var model: AppModel
    let preset: PresetEntry

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(preset.leavesMac ? tr("你在 {0} 里按回车发出的字，会到 {1} 的云端。", preset.label, preset.label)
                 : tr("字只留在这台 Mac 上。"))
                .font(Theme.bodyStrong).foregroundStyle(Theme.fg).fixedSize(horizontal: false, vertical: true)
            if !preset.note.isEmpty {
                Text(preset.note).font(Theme.small).foregroundStyle(Theme.ink2).fixedSize(horizontal: false, vertical: true)
            }
            if preset.takesMap {
                Text(tr("所有项目的便条都送进 {0} 里的同一个对话，开头写着 #项目。在 {1} 里打开那个对话，复制它的链接（或 id）贴在这里；空着也行，之后测试时再贴。", preset.label, preset.label))
                    .font(Theme.small).foregroundStyle(Theme.ink2).fixedSize(horizontal: false, vertical: true)
                TextField(tr("链接或 id"), text: $model.dest.addLink)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit { model.confirmAdd() }
            }
            if preset.mayBeDraft {
                Text(tr("加上以后先送一条测试：确认它只填进输入框、不会自己发出去，才能从这台 Mac 一下送过去。"))
                    .font(Theme.small).foregroundStyle(Theme.dim).fixedSize(horizontal: false, vertical: true)
            }
            HStack(spacing: 8) {
                Button { model.confirmAdd() } label: { Label(tr("加上"), systemImage: "return") }
                    .buttonStyle(ThemedButtonStyle(kind: .primary))
                Button(tr("取消")) { model.cancelAdd() }
                    .buttonStyle(ThemedButtonStyle(kind: .quiet))
            }
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Theme.warnBg, in: RoundedRectangle(cornerRadius: Theme.radius))
    }
}

/// The one-time test, asked in place: Did it appear? then Did it send by itself without you pressing Return?
/// (a link), or Did it paste? (an app); or the chat's link, then again.
struct TestPanel: View {
    @Bindable var model: AppModel

    var body: some View {
        let step = model.dest.test
        if step != .idle {
            VStack(alignment: .leading, spacing: 10) {
                HStack(spacing: 8) {
                    Image(systemName: "questionmark.bubble.fill").foregroundStyle(Theme.warn)
                    Text(tr("一次性测试")).font(Theme.smallStrong).foregroundStyle(Theme.dim)
                }
                content(step)
            }
            .padding(14)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Theme.warnBg, in: RoundedRectangle(cornerRadius: Theme.radius))
        }
    }

    @ViewBuilder func content(_ step: DestTestStep) -> some View {
        switch step {
        case .sending(let target):
            HStack(spacing: 8) {
                ProgressView().controlSize(.small)
                Text(tr("正在把“测试”送进 {0}…", target.label)).font(Theme.body).foregroundStyle(Theme.fg)
            }
        case .appeared(let target, _):
            question(step, yes: target.kind == "app" ? tr("贴出来了") : tr("出现了"), no: tr("没出现"))
        case .sentByItself:
            question(step, yes: tr("发出去了"), no: tr("没有，只填进去了"))
        case .link:
            LinkAsk(model: model, step: step)
        case .finished(let toast):
            HStack(spacing: 10) {
                Text(toast.text).font(Theme.body).foregroundStyle(toast.isError ? Theme.error : Theme.fg)
                    .fixedSize(horizontal: false, vertical: true)
                Spacer(minLength: 8)
                Button(tr("好")) { model.closeTest() }.buttonStyle(ThemedButtonStyle(kind: .primary))
            }
        case .idle:
            EmptyView()
        }
    }

    func question(_ step: DestTestStep, yes: String, no: String) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(DestTestFlow.question(step) ?? "").font(Theme.title).foregroundStyle(Theme.fg)
                .fixedSize(horizontal: false, vertical: true)
            if let hint = DestTestFlow.hint(step) {
                Text(hint).font(Theme.small).foregroundStyle(Theme.warn).fixedSize(horizontal: false, vertical: true)
            }
            HStack(spacing: 8) {
                Button(yes) { model.answerTest(yes: true) }.buttonStyle(ThemedButtonStyle(kind: .secondary))
                Button(no) { model.answerTest(yes: false) }.buttonStyle(ThemedButtonStyle(kind: .secondary))
                Spacer()
                Button(tr("算了")) { model.closeTest() }.buttonStyle(ThemedButtonStyle(kind: .quiet))
            }
        }
    }
}

/// Nothing appeared: paste the chat's link (or id), then `cpa dest map ID '*=LINK'` and the test again.
struct LinkAsk: View {
    @Bindable var model: AppModel
    let step: DestTestStep

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(DestTestFlow.question(step) ?? "").font(Theme.bodyStrong).foregroundStyle(Theme.fg)
                .fixedSize(horizontal: false, vertical: true)
            HStack(spacing: 8) {
                TextField(tr("链接或 id"), text: $model.dest.testLink)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit { model.submitTestLink() }
                Button(tr("再测一次")) { model.submitTestLink() }
                    .buttonStyle(ThemedButtonStyle(kind: .primary))
                    .disabled(model.dest.testLink.trimmingCharacters(in: .whitespaces).isEmpty)
                Button(tr("算了")) { model.closeTest() }.buttonStyle(ThemedButtonStyle(kind: .quiet))
            }
        }
    }
}
