// The detail's buttons: 1-9 with the project names (the current one lit, a suggested one outlined) and 0 No
// project; then Hand to AI · as a to-do (q), Hand to AI · to start (s, started on the device board with
// Enter), Done (x), Hide… (asks again) and Finder. A grey button says why.

import CardputerCore
import SwiftUI

struct ProjectButtons: View {
    @Bindable var model: AppModel
    let entry: InboxEntry

    var body: some View {
        CardBox {
            HStack {
                SectionLabel(text: picking ? tr("交给 AI：排进哪个项目的待办") : tr("归到项目"))
                Spacer()
                Text(picking ? tr("按数字键选") : tr("按数字键，一下就归")).font(Theme.small).foregroundStyle(Theme.dim)
            }
            if model.projects.slots.isEmpty {
                Reason(text: tr("这台 Mac 还没有项目：在“项目”页新建一个"))
            } else {
                FlowLayout(spacing: 8, lineSpacing: 8) {
                    ForEach(model.projects.slots) { slot in
                        Button { model.press(.digit(slot.slot)) } label: { label(slot) }
                            .buttonStyle(ThemedButtonStyle(selected: entry.project == slot.key || suggested == slot.key))
                    }
                    Button { model.press(.digit(0)) } label: {
                        HStack(spacing: 6) { KeyCap(text: "0"); Text(tr("没项目")) }
                    }
                    .buttonStyle(ThemedButtonStyle(kind: .quiet))
                }
                .disabled(model.inbox.busy)
            }
        }
    }

    var suggested: String? {
        model.suggestion?.key
    }

    var picking: Bool {
        if case .pickProject(let id, _) = model.inbox.pending {
            return id == entry.id
        }
        return false
    }

    func label(_ slot: ProjectSlot) -> some View {
        HStack(spacing: 6) {
            KeyCap(text: String(slot.slot), highlighted: entry.project == slot.key)
            Text(slot.name)
            if !slot.ok {
                Image(systemName: "exclamationmark.triangle").foregroundStyle(Theme.warn)
            }
        }
    }
}

struct ActionButtons: View {
    @Bindable var model: AppModel
    let entry: InboxEntry

    var body: some View {
        CardBox {
            SectionLabel(text: tr("接下来"))
            FlowLayout(spacing: 8, lineSpacing: 8) {
                Group {
                    keyed(tr("交给 AI · 排进待办"), key: "q", symbol: "tray.and.arrow.down") { model.press(.letter("q")) }
                    keyed(tr("交给 AI · 开工（到设备上按 Enter）"), key: "s", symbol: "play.rectangle") { model.press(.letter("s")) }
                }
                .disabled(entry.handBlocked != nil || model.inbox.busy)
                keyed(tr("看过了"), key: "x", symbol: "checkmark") { model.press(.letter("x")) }
                    .disabled(model.inbox.busy)
                hideButton
                if let session = model.inbox.detail?.session {
                    Button { model.reveal(session) } label: { Label(tr("在 Finder 中显示"), systemImage: "folder") }
                        .buttonStyle(ThemedButtonStyle(kind: .quiet))
                }
            }
            if let reason = entry.handBlocked {
                Reason(text: reason.contains(tr("交给 AI")) ? reason : tr("交给 AI：") + reason)
            }
            if entry.queue != nil {
                Reason(text: tr("开工的那一项：到设备的看板上按 Enter 开始（一律先做计划）。"))
            }
        }
    }

    func keyed(_ title: String, key: String, symbol: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 6) {
                Image(systemName: symbol)
                Text(title)
                KeyCap(text: key)
            }
        }
        .buttonStyle(ThemedButtonStyle())
    }

    @ViewBuilder var hideButton: some View {
        if model.inbox.confirmHide == entry.id {
            HStack(spacing: 6) {
                Button(tr("确定藏起来（设备和 AI 也看不到）")) { model.hide(entry.id) }
                    .buttonStyle(ThemedButtonStyle(kind: .danger))
                Button(tr("算了")) { model.cancelHide() }
                    .buttonStyle(ThemedButtonStyle(kind: .quiet))
            }
        } else {
            Button { model.hide(entry.id) } label: { Label(tr("藏起来…"), systemImage: "eye.slash") }
                .buttonStyle(ThemedButtonStyle(kind: .quiet))
                .disabled(entry.filing == .hidden || model.inbox.busy)
        }
    }
}
