// Inbox (docs/24 §2.1): the line on top, the filters (one row), notices, the list and the detail. The keys
// work while the list has the focus: 1-9 file, 0 no project, q Hand to AI · as a to-do, s Hand to AI · to
// start, x Done, space play, Enter approves a suggestion, ↑↓ j k move, / search, ⌘Z undo (the Edit menu's
// Undo). With cpa dest: Waiting to send on top of the list, ⌥ + a letter sends (the Send to… buttons carry
// the shortcuts), ⌘↩ sends a Waiting to send row to its preselected destination and ⌥D drops it (docs/25
// §5.7).

import CardputerCore
import SwiftUI

struct InboxView: View {
    @Bindable var model: AppModel
    @FocusState private var listFocused: Bool
    @FocusState private var searchFocused: Bool
    @State private var searchText = ""
    @Environment(\.undoManager) private var undoManager

    var inbox: InboxState {
        model.inbox
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            PageHeader(title: title, subtitle: inbox.page?.summary ?? tr("正在读取…")) {
                if let note = staleNote(model.answeredAt, now: model.now) {
                    Text(note).font(Theme.small).foregroundStyle(Theme.dim)
                }
                SearchField(prompt: tr("搜索文字"), text: $searchText, focus: $searchFocused) {
                    model.search(searchText)
                    listFocused = true
                }
                .frame(width: 230)
            }
            InboxFilters(model: model, searchText: $searchText)
            if InboxNotices.any(model) {
                InboxNotices(model: model)
            }
            if inbox.page != nil, inbox.entries.isEmpty, model.dest.pending.isEmpty {
                emptyState
            } else {
                HStack(alignment: .top, spacing: 14) {
                    VStack(spacing: 8) {
                        InboxList(model: model)
                        KeyHints(canFile: !inbox.isFallback, canSend: model.features?.canSend == true)
                    }
                    .frame(width: Theme.listWidth)
                    .simultaneousGesture(TapGesture().onEnded { listFocused = true })
                    .focusable()
                    .focused($listFocused)
                    .focusEffectDisabled()
                    .onKeyPress(phases: .down, action: handle)
                    InboxDetailView(model: model)
                }
            }
        }
        .padding(.horizontal, 22)
        .padding(.top, 18)
        .overlay(alignment: .bottom) {
            if let toast = inbox.toast, toast.sticky {
                StickyToastView(model: model, toast: toast).padding(.bottom, 48)
            } else if let toast = inbox.toast {
                ToastView(toast: toast).padding(.bottom, 48)
            }
        }
        .onAppear {
            searchText = inbox.search
            listFocused = true
            model.undoManager = undoManager
        }
        .onChange(of: undoManager) { _, manager in model.undoManager = manager }
        .onExitCommand { listFocused = true }
    }

    var title: String {
        model.features?.canFile == false ? tr("没项目的录音") : tr("待归档")
    }

    @ViewBuilder var emptyState: some View {
        if inbox.isFallback {
            EmptyState(pose: .wait, title: tr("没有没项目的录音"), detail: tr("设备上的录音取回以后，没归到项目的会出现在这里。"))
        } else if inbox.query == FilingQuery(), model.counts?.otherMac ?? 0 == 0 {
            InboxZeroView(model: model)
        } else {
            EmptyState(pose: .think, title: tr("这些筛选下没有录音"), detail: tr("换一个状态或日期，或者清掉搜索词。"))
        }
    }

    func handle(_ press: KeyPress) -> KeyPress.Result {
        guard let key = Self.inboxKey(press) else { return .ignored }
        if key == .slash {
            searchFocused = true
            return .handled
        }
        model.press(key)
        return .handled
    }

    /// A key press without ⌘, ⌃ or ⌥, as an inbox key; nil for anything else.
    static func inboxKey(_ press: KeyPress) -> InboxKey? {
        guard press.modifiers.isDisjoint(with: [.command, .control, .option]) else { return nil }
        switch press.key {
        case .upArrow: return .up
        case .downArrow: return .down
        case .return: return .enter
        case .escape: return .escape
        case .space: return .space
        default: break
        }
        guard press.characters.count == 1, let char = press.characters.first, char.isASCII else { return nil }
        if let digit = char.wholeNumberValue {
            return .digit(digit)
        }
        if char == "/" {
            return .slash
        }
        return char.isLetter ? .letter(Character(char.lowercased())) : nil
    }
}

/// The notices above the list, most urgent first. (A key waiting for the next one is asked in the detail.)
struct InboxNotices: View {
    @Bindable var model: AppModel

    static func any(_ model: AppModel) -> Bool {
        model.inbox.isFallback || model.status?.running == false || model.inbox.error != nil
    }

    var body: some View {
        VStack(spacing: 8) {
            if model.inbox.isFallback {
                NoticeBar(tone: .info, symbol: "lock",
                          text: tr("这台 Mac 的 cpa 还不会归档：归档功能（feat/filing-p1）还没合进 main。合并以后在仓库目录里运行："),
                          command: Setup.updateCpa, copy: model.copy)
            }
            if model.status?.running == false {
                NoticeBar(tone: .warn, symbol: "bolt.horizontal.circle",
                          text: tr("cpad 没在跑：这里照常能用，设备上的录音要等它起来才会取回。"),
                          command: "cpa service install", copy: model.copy)
            }
            if let error = model.inbox.error {
                NoticeBar(tone: .error, symbol: "exclamationmark.triangle", text: error)
            }
        }
    }
}

/// The keys, always in sight under the list: what each does, at most two short lines.
struct KeyHints: View {
    let canFile: Bool
    var canSend = false

    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            HStack(spacing: 8) {
                hint("↑↓", tr("移动"))
                if canFile {
                    hint("1–9", tr("归档"))
                    hint("0", tr("没项目"))
                    hint("x", tr("看过了"))
                }
                hint(tr("空格"), tr("播放"))
                Spacer(minLength: 0)
            }
            if canFile {
                HStack(spacing: 8) {
                    hint("q", tr("排进待办"))
                    hint("s", tr("开工"))
                    hint("⌘Z", tr("撤销"))
                    if canSend {
                        hint(tr("⌥字母"), tr("送到"))
                    }
                    Spacer(minLength: 0)
                }
            }
        }
        .padding(.horizontal, 4)
        .padding(.bottom, 10)
    }

    func hint(_ key: String, _ text: String) -> some View {
        HStack(spacing: 4) {
            KeyCap(text: key)
            Text(text).font(Theme.small).foregroundStyle(Theme.dim).fixedSize()
        }
    }
}
