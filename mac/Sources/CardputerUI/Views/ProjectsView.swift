// Projects (docs/24 §2.6, docs/25 §8): the numbers 1-9 as the device shows them, each a named group made
// here. New (on the first free number or an empty row), rename (the button or a double click on the name),
// change number (the number's menu, or drag ≡ onto another row: both swap), link a code folder or not, remove
// with a second click. The key stays out of sight (a tooltip on the name): Thesis is p3 wherever it is moved.
// Voice filed under a project stays in this app until you hand it on yourself; only a linked project can
// start an agent session from the device.

import CardputerCore
import SwiftUI

struct ProjectsView: View {
    @Bindable var model: AppModel

    var editable: Bool {
        model.features?.projectEdit == .available
    }

    var body: some View {
        ThemedScroll {
            VStack(alignment: .leading, spacing: 16) {
                PageHeader(title: tr("项目"), subtitle: tr("设备上的数字 1–9：录音时按一个数字，就归到那个项目"))
                newCard
                numbersCard
                if let toast = model.projectsPane.toast {
                    ToastView(toast: toast)
                }
            }
            .padding(.horizontal, 22)
            .padding(.vertical, 18)
        }
    }

    var newCard: some View {
        CardBox {
            SectionLabel(text: tr("新建项目"))
            HStack(spacing: 8) {
                BoxField(prompt: tr("名字，比如 论文 或 Website"), text: $model.projectsPane.newName) { model.addProject() }
                    .frame(maxWidth: 320)
                Button { model.addProject() } label: { Label(tr("新建"), systemImage: "plus") }
                    .buttonStyle(ThemedButtonStyle(kind: .primary))
                    .disabled(!canAdd || model.projects.firstFree == nil)
            }
            .disabled(!editable || model.projectsPane.busy)
            Reason(text: newReason)
            if !editable {
                CommandChip(command: Setup.updateCpa, copy: model.copy).fixedSize()
            }
        }
    }

    var canAdd: Bool {
        !model.projectsPane.newName.trimmingCharacters(in: .whitespaces).isEmpty
    }

    var newReason: String {
        guard editable else { return tr("改项目要新版 cpa（cpa project add / rename / move）。在 Cardputer Assistant 的目录里更新：") }
        guard let free = model.projects.firstFree else { return tr("9 个号都满了：先删一个。") }
        return tr("放在第一个空号 {0}，或点空行的“放在这里”。名字最多 24 个字母（8 个汉字）；代码目录可以以后再连，也可以不连。", free)
    }

    var numbersCard: some View {
        CardBox {
            HStack {
                SectionLabel(text: tr("1–9（点号码选新号，或拖 ≡ 到另一行；都是对调）"))
                Spacer()
                CommandChip(command: "cpa project ls", copy: model.copy).fixedSize()
            }
            ForEach(model.projects.numbers) { row in
                ProjectRow(model: model, row: row, editable: editable)
            }
            Reason(text: tr("只有连了代码目录的项目，设备上才能开 AI 会话。没连的项目照样收录音，再从“待归档”送到任何 app。删除项目不会删录音。"))
        }
    }
}

struct ProjectRow: View {
    @Bindable var model: AppModel
    @Environment(\.isSnapshot) private var snapshot
    let row: NumberRow
    let editable: Bool

    var body: some View {
        let content = HStack(spacing: 10) {
            handle
            number
            if let slot = row.slot {
                filled(slot)
            } else {
                empty
            }
        }
        .padding(.vertical, 2)
        .contentShape(Rectangle())
        // ImageRenderer cannot draw a drop target or a drag source (a yellow block): a snapshot leaves them out.
        return Group {
            if snapshot {
                content
            } else {
                content.dropDestination(for: String.self) { keys, _ in
                    guard editable, let key = keys.first, model.projects.project(key: key) != nil else { return false }
                    model.moveProject(key, to: row.number)
                    return true
                }
            }
        }
    }

    /// ≡ to drag a project onto another row; blank on an empty row or with an old cpa.
    @ViewBuilder var handle: some View {
        let symbol = Image(systemName: "line.3.horizontal").font(Theme.small).foregroundStyle(Theme.dim).frame(width: 14)
        if let slot = row.slot, editable {
            if snapshot {
                symbol
            } else {
                symbol.contentShape(Rectangle()).draggable(slot.key).help(tr("拖到另一行就换号"))
            }
        } else {
            Color.clear.frame(width: 14, height: 1)
        }
    }

    /// The number; on a project, a menu of 1-9 to move it there in two clicks (the one there takes its number).
    @ViewBuilder var number: some View {
        let cap = KeyCap(text: String(row.number), highlighted: row.slot != nil)
        if let slot = row.slot, editable, !snapshot {
            Menu {
                ForEach(model.projects.numbers) { target in
                    Button(target.slot.map { tr("{0} · 和“{1}”对调", target.number, $0.name) } ?? tr("{0} · 空", target.number)) {
                        model.moveProject(slot.key, to: target.number)
                    }
                    .disabled(target.number == slot.slot)
                }
            } label: { cap }
                .menuStyle(.button)
                .buttonStyle(.plain)
                .menuIndicator(.hidden)
                .fixedSize()
                .frame(width: 22)
                .disabled(model.projectsPane.busy)
                .help(tr("换到别的号"))
        } else {
            cap.frame(width: 22)
        }
    }

    var empty: some View {
        HStack(spacing: 10) {
            Text(tr("空")).font(Theme.body).foregroundStyle(Theme.dim)
            Spacer()
            if !editable {
                EmptyView()
            } else if model.projectsPane.newName.trimmingCharacters(in: .whitespaces).isEmpty {
                Text(tr("拖一个项目到这里，或先在上面输入名字")).font(Theme.small).foregroundStyle(Theme.dim)
            } else {
                Button(tr("放在这里")) { model.addProject(slot: row.number) }
                    .buttonStyle(ThemedButtonStyle(kind: .quiet, compact: true))
                    .disabled(model.projectsPane.busy)
            }
        }
    }

    @ViewBuilder func filled(_ slot: ProjectSlot) -> some View {
        if model.projectsPane.renaming == slot.key {
            renaming
        } else {
            Text(slot.name).font(Theme.bodyStrong).foregroundStyle(Theme.fg).lineLimit(1)
                .frame(width: 180, alignment: .leading)
                .help(tr("命令行里的 key：{0}", slot.key))
                .onTapGesture(count: 2) {
                    if editable { model.startRename(slot.key) }
                }
            FolderPill(slot: slot)
            Spacer(minLength: 6)
            if !editable {
                EmptyView()
            } else if model.projectsPane.confirmRemove == slot.key {
                confirm(slot)
            } else {
                actions(slot)
            }
        }
    }

    var renaming: some View {
        HStack(spacing: 8) {
            BoxField(prompt: tr("新名字"), text: $model.projectsPane.renameText) { model.confirmRename() }
                .frame(maxWidth: 240)
            Button(tr("改好")) { model.confirmRename() }.buttonStyle(ThemedButtonStyle(kind: .primary, compact: true))
            Button(tr("取消")) { model.cancelRename() }.buttonStyle(ThemedButtonStyle(kind: .quiet, compact: true))
            Spacer()
        }
    }

    func actions(_ slot: ProjectSlot) -> some View {
        HStack(spacing: 4) {
            Button(tr("改名")) { model.startRename(slot.key) }.buttonStyle(ThemedButtonStyle(kind: .quiet, compact: true))
            if slot.linked {
                Button(tr("断开目录")) { model.unlinkFolder(slot.key) }.buttonStyle(ThemedButtonStyle(kind: .quiet, compact: true))
            } else {
                Button(tr("连目录…")) { model.linkFolder(slot.key) }.buttonStyle(ThemedButtonStyle(kind: .quiet, compact: true))
            }
            Button(tr("删除")) { model.removeProject(slot.key) }.buttonStyle(ThemedButtonStyle(kind: .danger, compact: true))
        }
        .disabled(model.projectsPane.busy)
    }

    func confirm(_ slot: ProjectSlot) -> some View {
        HStack(spacing: 6) {
            Text(tr("删掉“{0}”？录音会留着", slot.name)).font(Theme.small).foregroundStyle(Theme.warn)
            Button(tr("删除")) { model.removeProject(slot.key) }.buttonStyle(ThemedButtonStyle(kind: .danger, compact: true))
            Button(tr("取消")) { model.cancelRemove() }.buttonStyle(ThemedButtonStyle(kind: .quiet, compact: true))
        }
    }
}

/// The folder a project is linked to, gone, or none.
struct FolderPill: View {
    let slot: ProjectSlot

    var body: some View {
        if !slot.linked {
            Pill(text: tr("没连目录"), tone: .neutral, symbol: "folder")
        } else if !slot.ok {
            Pill(text: tr("目录不在了"), tone: .warn, symbol: "exclamationmark.triangle")
        } else {
            Pill(text: slot.folder ?? tr("连着目录"), tone: .ok, symbol: "folder.fill")
        }
    }
}
