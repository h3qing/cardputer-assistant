// Send to…, Waiting to send and the Destinations card (docs/25 §5.4, §5.7): each click or key runs one public
// cpa dest command and says the result for 5 s. A click on the Mac counts as the confirmation only for a
// destination that fills a draft (fl without k); the others are grey and say why. Nothing here knows any app:
// names, letters and notes come from cpa.

import CardputerCore
import Foundation

extension AppModel {
    /// Waiting to send, the destinations and the usual ones; nothing while this cpa has no cpa dest.
    func loadDest() async -> Bool {
        guard cpaFound, features?.canSend == true else {
            if dest != DestState() {
                dest = DestState()
            }
            return true
        }
        async let pending = try? services.client.destPending()
        async let catalog = try? services.client.destCatalog()
        async let usual = try? services.client.destUsual()
        let (rows, dests, usuals) = await (pending, catalog, usual)
        let waiting = rows?.rows ?? dest.pending
        let focus = waiting.contains { $0.ref == dest.focus } ? dest.focus : nil
        let fresh = dest.with {
            $0.pending = waiting
            $0.catalog = dests ?? $0.catalog
            $0.usual = usuals ?? $0.usual
            $0.focus = focus
        }
        if fresh != dest {
            dest = fresh
        }
        return rows != nil
    }

    /// The installed apps a destination can be added for (Integrations only: it asks LaunchServices).
    func loadPresets() async {
        guard cpaFound, features?.canSend == true, let found = try? await services.client.destPresets() else { return }
        dest = dest.with { $0.presets = found }
    }

    // MARK: Send to… and Waiting to send

    /// What the selected Waiting to send row or Inbox memo can be sent to, in the order Send to… shows.
    public var sendChoices: [DestChoice] {
        if let row = dest.focusedRow {
            return DestChoices.forPending(row, dests: dest.dests)
        }
        guard features?.canSend == true, let entry = inbox.selectedEntry else { return [] }
        return DestChoices.forEntry(entry, dests: dest.dests, usual: dest.usual.dest(for: entry.project))
    }

    public func selectPending(_ ref: String) {
        guard dest.focus != ref else { return }
        dest = dest.with { $0.focus = ref }
        inbox = inbox.with {
            $0.pending = .none
            $0.confirmHide = nil
        }
    }

    /// ⌥ + a letter: the destination bound to it, or why not.
    public func sendLetter(_ letter: Character) {
        switch DestChoices.pick(letter, in: sendChoices) {
        case .send(let id): send(to: id)
        case .blocked(let reason): say(Toast(text: reason, isError: true))
        }
    }

    /// A Send to… button, ⌘↩ (nil: the Waiting to send row's preselected destination) or ⌥ + a letter.
    public func send(to destID: String?) {
        Task { await sendNow(to: destID) }
    }

    public func sendNow(to destID: String?) async {
        guard !inbox.busy, let asked = sendCommand(to: destID) else { return }
        await run(dest.command(for: asked), asked: asked)
    }

    /// Copy again: the text that went to the clipboard, once more.
    public func recopy() {
        guard let command = dest.recopy else { return }
        Task {
            guard !inbox.busy else { return }
            await run(command, asked: command)
        }
    }

    func run(_ command: CpaCommand, asked: CpaCommand) async {
        inbox = inbox.with {
            $0.busy = true
            $0.generation += 1
        }
        let result = await DestWorkflow.send(command, client: services.client)
        dest = dest.with {
            $0.again = result.again.map { AgainAsk(base: asked, again: $0) }
            $0.recopy = result.recopy ?? (result.changed ? nil : $0.recopy)
        }
        await finishDest(result.outcome, wasPending: dest.focus != nil)
    }

    /// A grey link that only waits for its one-time test: Integrations → Destinations, and the test starts.
    public func openTest(_ entry: DestEntry) {
        show(.integrations)
        if dest.test == .idle {
            startTest(entry)
        }
    }

    func sendCommand(to destID: String?) -> CpaCommand? {
        if let row = dest.focusedRow {
            return try? .destSend(row.ref, to: destID == row.dest ? nil : destID)
        }
        guard let entry = inbox.selectedEntry, let destID else {
            say(Toast(text: tr("先选一个去向：⌥ 加字母"), isError: true))
            return nil
        }
        return try? .filingSend(entry.id, to: destID)
    }

    /// ⌥D on a Waiting to send row: do not send it (its daily-note line stays).
    public func dropFocused() {
        guard let row = dest.focusedRow, let command = try? CpaCommand.destDrop(row.ref) else { return }
        Task {
            guard !inbox.busy else { return }
            inbox = inbox.with { $0.busy = true }
            await finishDest(await DestWorkflow.drop(command, client: services.client), wasPending: true)
        }
    }

    /// After a Waiting to send row went (sent or dropped), the next one waiting takes the focus.
    func finishDest(_ outcome: ActionOutcome, wasPending: Bool) async {
        inbox = inbox.with { $0.busy = false }
        say(outcome.toast)
        guard outcome.changed else { return }
        _ = await loadDest()
        if wasPending, dest.focus == nil, let next = dest.pending.first {
            selectPending(next.ref)
        }
        _ = await loadInbox()
        _ = await loadCounts()
    }

    // MARK: the Destinations card

    /// Add: first the app's own confirmation with cpa's note, then `cpa dest add --preset P --yes`.
    public func askAdd(_ preset: PresetEntry) {
        dest = dest.with { $0.adding = preset }
    }

    public func cancelAdd() {
        dest = dest.with {
            $0.adding = nil
            $0.addLink = ""
        }
    }

    /// The chat's link, when the preset takes one and it was given: every project goes to that chat.
    public func confirmAdd() {
        guard let preset = dest.adding else { return }
        let link = dest.addLink.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let command = try? CpaCommand.destAdd(preset: preset.preset, link: preset.takesMap && !link.isEmpty ? link : nil)
        else {
            dest = dest.with { $0.note = Toast(text: tr("这不像一个链接或 id"), isError: true) }
            return
        }
        cancelAdd()
        runCard(command, done: tr("已加上 {0}：接着送一条测试", preset.label))
    }

    public func setUsual(_ project: String, to destID: String?) {
        guard let command = try? CpaCommand.setUsual(project, to: destID) else { return }
        let label = destID.map { id in dest.dests.first { $0.id == id }?.label ?? id }
        runCard(command, done: label.map { tr("{0} 的常用去向：{1}", project, $0) } ?? tr("{0} 不再有常用去向", project))
    }

    func runCard(_ command: CpaCommand, done: String) {
        Task {
            dest = dest.with { $0.busy = true }
            let failed = await DestWorkflow.plain(command, client: services.client)
            dest = dest.with {
                $0.busy = false
                $0.note = Toast(text: failed ?? done, isError: failed != nil)
            }
            _ = await loadDest()
        }
    }

    // MARK: the one-time test

    public func startTest(_ entry: DestEntry) {
        startTest(DestTestTarget(entry))
    }

    func startTest(_ target: DestTestTarget) {
        dest = dest.with {
            $0.test = .sending(target)
            $0.testLink = ""
        }
        Task {
            let step = await DestWorkflow.test(target, client: services.client)
            dest = dest.with { $0.test = step }
        }
    }

    public func answerTest(yes: Bool) {
        let (step, command) = DestTestFlow.answer(dest.test, yes: yes)
        dest = dest.with { $0.test = step }
        guard let command else { return }
        Task {
            if let failed = await DestWorkflow.plain(command, client: services.client) {
                dest = dest.with { $0.test = .finished(Toast(text: failed, isError: true)) }
            }
            _ = await loadDest()
        }
    }

    /// The chat's link: `cpa dest map ID '*=LINK'`, then the test again.
    public func submitTestLink() {
        guard case .link(let target) = dest.test else { return }
        guard let command = try? CpaCommand.destMap(target.dest, link: dest.testLink) else {
            dest = dest.with { $0.note = Toast(text: tr("这不像一个链接或 id"), isError: true) }
            return
        }
        Task {
            if let failed = await DestWorkflow.plain(command, client: services.client) {
                dest = dest.with { $0.test = .finished(Toast(text: failed, isError: true)) }
                return
            }
            startTest(target)
        }
    }

    public func closeTest() {
        if case .finished(let toast) = dest.test {
            dest = dest.with { $0.note = toast }
        }
        dest = dest.with { $0.test = .idle }
    }
}
