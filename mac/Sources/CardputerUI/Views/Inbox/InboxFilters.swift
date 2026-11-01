// The filters, in one row (docs/24 §2.1, filters without typing): the state as a segmented choice (Inbox ·
// Handed to AI · Done · All), Other Mac and No words only while there are such recordings, then Date ▾,
// Memo/meeting ▾ and Projects ▾ as menus (the 1-9 buttons are in the detail). Each maps onto `cpa filing ls`
// arguments; the read-only list has date, kind and words only.

import CardputerCore
import SwiftUI

struct InboxFilters: View {
    @Bindable var model: AppModel
    @Binding var searchText: String

    var query: FilingQuery {
        model.inbox.query
    }

    var canFile: Bool {
        model.features?.canFile == true
    }

    var body: some View {
        HStack(spacing: 8) {
            if canFile {
                Segmented(items: InboxScope.main.map { ($0, Self.word($0), badge($0)) }, selected: query.scope) { scope in
                    model.filter { $0.replacing(scope: scope) }
                }
                ForEach(extraScopes, id: \.self) { scope in
                    Chip(text: Self.word(scope), on: query.scope == scope, badge: badge(scope)) {
                        model.filter { $0.replacing(scope: query.scope == scope ? .pending : scope) }
                    }
                }
            }
            datesMenu
            kindMenu
            if canFile, !model.projects.slots.isEmpty {
                projectsMenu
            }
            if !query.text.isNilOrEmpty {
                Chip(text: tr("清掉搜索：{0}", query.text ?? ""), on: true, action: clearSearch)
            }
            Spacer(minLength: 0)
        }
    }

    static func word(_ scope: InboxScope) -> String {
        scope == .otherMac ? tr("别的电脑") : Labels.scope(scope)
    }

    /// Other Mac while its group has new rows; No words while the list shows one. Either while it is chosen.
    var extraScopes: [InboxScope] {
        let other = (model.counts?.otherMac ?? 0) > 0 || query.scope == .otherMac
        let silent = model.inbox.stuckSeen || query.scope == .noText
        return (other ? [.otherMac] : []) + (silent ? [.noText] : [])
    }

    func badge(_ scope: InboxScope) -> String? {
        switch scope {
        case .pending: return model.counts.map { String($0.waiting) }
        case .otherMac: return model.counts.map { String($0.otherMac) }
        default: return nil
        }
    }

    var datesMenu: some View {
        FilterMenu(title: query.dates == .all ? tr("日期") : Labels.dates(query.dates), on: query.dates != .all) {
            ForEach(DateRange.allCases, id: \.self) { range in
                Button(Self.mark(query.dates == range) + Labels.dates(range)) { model.filter { $0.replacing(dates: range) } }
            }
        }
    }

    var kindMenu: some View {
        FilterMenu(title: query.mode.map { Labels.kind(Open($0)) } ?? tr("便条/会议"), on: query.mode != nil) {
            Button(Self.mark(query.mode == nil) + tr("便条和会议")) { model.filter { $0.replacing(mode: .some(nil)) } }
            ForEach([RecordingKind.memo, .meeting], id: \.self) { kind in
                Button(Self.mark(query.mode == kind) + Labels.kind(Open(kind))) {
                    model.filter { $0.replacing(mode: .some(kind)) }
                }
            }
        }
    }

    var projectsMenu: some View {
        let chosen = model.projects.slots.filter { query.projects.contains($0.key) }
        let title = chosen.isEmpty ? tr("项目") : chosen.map(\.label).joined(separator: tr("、"))
        return FilterMenu(title: title, on: !chosen.isEmpty) {
            Button(Self.mark(chosen.isEmpty) + tr("全部项目")) { model.filter { $0.replacing(projects: []) } }
            ForEach(model.projects.slots) { slot in
                Button(Self.mark(query.projects.contains(slot.key)) + slot.label) {
                    model.filter { $0.replacing(projects: toggled(slot.key)) }
                }
            }
        }
    }

    /// A tick before the chosen item of a menu.
    static func mark(_ on: Bool) -> String {
        on ? "✓ " : "    "
    }

    func clearSearch() {
        searchText = ""
        model.search("")
    }

    func toggled(_ key: String) -> [String] {
        query.projects.contains(key) ? query.projects.filter { $0 != key } : query.projects + [key]
    }
}

extension InboxScope {
    /// The four states always shown.
    static let main: [InboxScope] = [.pending, .handed, .done, .all]
}

extension InboxEntry {
    /// Its transcription failed or stopped: what No words lists.
    var isStuck: Bool {
        tx.known == .failed || tx.known == .stale
    }
}

extension Optional where Wrapped == String {
    var isNilOrEmpty: Bool {
        self?.isEmpty ?? true
    }
}
