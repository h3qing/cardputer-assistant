// Inbox: load the list for the filters, keep a selection, run what a key or a button asks (one cpa filing
// command each), show the result for 5 s, take steps back with ⌘Z (the window's undo manager, so the Edit
// menu says what it undoes), play the recording. A load that started before a filter change or an action is
// dropped when it comes back.

import CardputerCore
import Foundation

extension AppModel {
    func loadInbox() async -> Bool {
        guard cpaFound, let features else { return true }
        let generation = inbox.generation
        let loader = InboxLoader(client: services.client, notes: notes)
        do {
            let (page, kept) = try await loader.load(inbox.query, canFile: features.canFile, cache: headlines, now: now,
                                                     calendar: services.calendar)
            headlines = kept
            guard generation == inbox.generation else { return true }
            apply(page)
            return true
        } catch {
            guard generation == inbox.generation else { return true }
            inbox = inbox.with { $0.error = Feedback.failure(error) }
            return false
        }
    }

    func apply(_ page: InboxPage) {
        if case .filing(let fresh) = page.source, fresh != counts {
            setCounts(fresh)
        }
        guard page != inbox.page || inbox.error != nil else { return }
        let before = inbox.selectedEntry
        let entries = page.entries
        let kept = entries.contains { $0.id == inbox.selected } ? inbox.selected : nil
        let selected = kept ?? entries.first { $0.filing == .new }?.id ?? entries.first?.id
        inbox = inbox.with {
            $0.page = page
            $0.error = nil
            $0.selected = selected
        }
        if [.pending, .all, .noText].contains(inbox.query.scope) {
            let seen = inbox.query.scope == .noText ? !entries.isEmpty : entries.contains(where: \.isStuck)
            inbox = inbox.with { $0.stuckSeen = seen }
        }
        let after = inbox.selectedEntry
        if after?.id != before?.id || after?.tx != before?.tx || inbox.detail?.id != selected {
            Task { await loadDetail() }
        }
    }

    /// Waits for the selected recording's detail (CardputerShots draws after it).
    public func settle() async {
        await loadDetail()
        await loadRecordingDetail()
    }

    public func select(_ id: String?) {
        if dest.focus != nil {
            dest = dest.with { $0.focus = nil }
        }
        guard id != inbox.selected else { return }
        inbox = inbox.with {
            $0.selected = id
            $0.pending = .none
            $0.confirmHide = nil
        }
        Task { await loadDetail() }
    }

    func loadDetail() async {
        guard let entry = inbox.selectedEntry else {
            inbox = inbox.with { $0.detail = nil }
            return
        }
        let item = entry.filing != nil ? try? await services.client.filingShow(entry.id) : nil
        let (notes, calendar) = (notes, services.calendar)
        let detail = await Task.detached { DetailReader.entry(entry, item: item, notes: notes, calendar: calendar) }.value
        guard inbox.selected == entry.id, detail != inbox.detail else { return }
        inbox = inbox.with { $0.detail = detail }
    }

    // MARK: keys and buttons

    /// The project the selected entry's words name (Enter approves it).
    public var suggestion: ProjectSlot? {
        inbox.selectedEntry.flatMap { Suggestion.of($0, projects: projects) }
    }

    public func press(_ key: InboxKey) {
        // A Waiting to send row has the focus: the filing keys have no memo to act on.
        let entry = dest.focus == nil ? inbox.selectedEntry : nil
        let result = InboxKeys.reduce(key, entry: entry, projects: projects, pending: inbox.pending,
                                      suggested: entry == nil ? nil : suggestion?.key)
        inbox = inbox.with { $0.pending = result.pending }
        switch result.intent {
        case .none: break
        case .move(let step): move(step)
        case .act(let action): perform(action)
        case .blocked(let reason): say(Toast(text: reason, isError: true))
        case .togglePlay: if entry != nil { togglePlay() }
        case .openDetail, .focusSearch: break
        }
    }

    /// ↑↓ j k through Waiting to send (on top) and then the Inbox, as one list.
    func move(_ step: Int) {
        let refs = dest.pending.map(\.ref)
        let ids = inbox.entries.map(\.id)
        let count = refs.count + ids.count
        guard count > 0 else { return }
        let here = dest.focus.flatMap { refs.firstIndex(of: $0) }
            ?? inbox.selected.flatMap { ids.firstIndex(of: $0) }.map { $0 + refs.count }
        let next = min(max(here.map { $0 + step } ?? 0, 0), count - 1)
        if next < refs.count {
            selectPending(refs[next])
        } else {
            select(ids[next - refs.count])
        }
    }

    public func perform(_ action: InboxAction) {
        Task { await act(action) }
    }

    /// One action to the end: the command, the feedback line, the undo step, the next selection, a fresh list.
    public func act(_ action: InboxAction) async {
        guard !inbox.busy else { return }
        inbox = inbox.with {
            $0.busy = true
            $0.confirmHide = nil
            $0.pending = .none
            $0.generation += 1
        }
        let outcome = await InboxWorkflow.perform(action, entries: inbox.entries, projects: projects,
                                                  client: services.client)
        await finish(outcome, named: Feedback.actionName(action, projects: projects))
        if case .file = action, outcome.changed {
            await offerUsual(action.id)
        }
    }

    /// Filed into a project with a usual destination: its Waiting to send suggestion takes the focus, so ⌘↩
    /// sends it with its #project (a digit, then ⌘↩).
    func offerUsual(_ id: String) async {
        _ = await loadDest()
        if dest.pending.contains(where: { $0.ref == id && $0.why.known == .usual }) {
            selectPending(id)
        }
    }

    public func undo() {
        Task { await undoNow() }
    }

    public func undoNow() async {
        guard !inbox.busy else { return }
        inbox = inbox.with {
            $0.busy = true
            $0.generation += 1
        }
        let before = inbox.undo
        let (stack, outcome) = await InboxWorkflow.undo(before, client: services.client)
        inbox = inbox.with { $0.undo = stack }
        await finish(outcome, named: nil)
        if stack == before, stack.top != nil {
            // It failed and is kept: offer it again in the Edit menu.
            registerUndo(named: tr("上一步"))
        }
    }

    func finish(_ outcome: ActionOutcome, named: String?) async {
        inbox = inbox.with {
            $0.busy = false
            $0.undo = $0.undo.pushing(outcome.undo)
        }
        if outcome.undo != nil, let named {
            registerUndo(named: named)
        }
        say(outcome.toast)
        if let next = outcome.select {
            select(next)
        }
        if outcome.changed {
            _ = await loadInbox()
            _ = await loadCounts()
        }
    }

    /// One step in the window's undo manager: the Edit menu then says Undo “File under 3 Thesis”.
    func registerUndo(named name: String) {
        guard let manager = undoManager else { return }
        manager.registerUndo(withTarget: self) { model in
            MainActor.assumeIsolated { model.undo() }
        }
        manager.setActionName(name)
    }

    /// Hide needs a second click on the same recording.
    public func hide(_ id: String) {
        guard inbox.confirmHide == id else {
            inbox = inbox.with { $0.confirmHide = id }
            return
        }
        perform(.hide(id: id))
    }

    public func cancelHide() {
        inbox = inbox.with { $0.confirmHide = nil }
    }

    /// The feedback line, gone after 5 s unless another took its place; a sticky one stays until it is closed.
    func say(_ toast: Toast) {
        inbox = inbox.with { $0.toast = toast }
        guard !toast.sticky else { return }
        Task {
            try? await Task.sleep(nanoseconds: 5_000_000_000)
            if inbox.toast == toast {
                inbox = inbox.with { $0.toast = nil }
            }
        }
    }

    /// Closes a line that stays (Got it).
    public func dismissToast() {
        inbox = inbox.with { $0.toast = nil }
    }

    // MARK: filters

    public func filter(_ change: (FilingQuery) -> FilingQuery) {
        inbox = inbox.with {
            $0.query = change($0.query)
            $0.selected = nil
            $0.generation += 1
        }
        Task { _ = await loadInbox() }
    }

    public func search(_ words: String) {
        let text = words.trimmingCharacters(in: .whitespacesAndNewlines)
        inbox = inbox.with { $0.search = words }
        filter { $0.replacing(text: text.isEmpty ? nil : text) }
    }

    // MARK: playback and the transcriber

    public func togglePlay() {
        guard let detail = inbox.detail, detail.id == inbox.selected else { return }
        player.toggle(id: detail.id, urls: detail.audio, notes: notes)
    }

    public func play(from seconds: Double) {
        guard let detail = inbox.detail else { return }
        player.play(id: detail.id, urls: detail.audio, notes: notes, from: seconds)
    }

    /// Retry the failed ones: cpa-stt retry (it redoes only its own failed recordings and clears blocked).
    public func retryTranscription() {
        Task {
            let note: Toast
            do {
                _ = try await services.client.run(.sttRetry)
                note = Toast(text: tr("已让 cpa-stt 重新转写失败的录音"), isError: false)
            } catch {
                note = Toast(text: Feedback.failure(error), isError: true)
            }
            say(note)
            recordings = recordings.with { $0.toast = note }
            refreshSoon([.list, .status, .stt])
        }
    }
}

extension InboxState {
    func with(_ change: (inout InboxState) -> Void) -> InboxState {
        var copy = self
        change(&copy)
        return copy
    }
}

extension FilingQuery {
    /// A copy with some filters changed; the page cursor is dropped.
    func replacing(scope: InboxScope? = nil, dates: DateRange? = nil, projects: [String]? = nil,
                   mode: RecordingKind?? = nil, text: String?? = nil) -> FilingQuery {
        FilingQuery(scope: scope ?? self.scope, dates: dates ?? self.dates, projects: projects ?? self.projects,
                    withoutProject: withoutProject, mode: mode ?? self.mode, device: device, link: link,
                    text: text ?? self.text, limit: limit, cursor: nil)
    }
}
