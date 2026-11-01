// What Send to…, Waiting to send and the Destinations card show (docs/25 §5.7), as one plain value: every
// answer comes from a public `cpa dest … --json`; a change makes a new value.

import CardputerCore
import Foundation

public struct DestState: Hashable, Sendable {
    /// cpa dest ls --json
    public var catalog: DestCatalog = .empty
    /// cpa dest pending --json: Waiting to send, held rows first.
    public var pending: [PendingRow] = []
    /// cpa dest usual --json
    public var usual = UsualList(usual: [])
    /// cpa dest presets --installed --json, read when Integrations opens.
    public var presets: PresetList = .empty
    /// The Waiting to send row selected instead of an Inbox entry.
    public var focus: String?
    /// The one-time test's dialog.
    public var test: DestTestStep = .idle
    /// The bot's link the test asks for when nothing appeared.
    public var testLink = ""
    /// The preset waiting for the app's own confirmation before `cpa dest add --yes`.
    public var adding: PresetEntry?
    /// The chat's link the confirmation asks for, for a preset that takes one (cpa dest add --map).
    public var addLink = ""
    /// cpa answered E_ALREADY to this send: pressing it again sends it once more (--again).
    public var again: AgainAsk?
    /// The last text handed through the clipboard: Copy again sends it there once more.
    public var recopy: CpaCommand?
    /// The Destinations card's own line.
    public var note: Toast?
    public var busy = false

    public var focusedRow: PendingRow? {
        pending.first { $0.ref == focus }
    }

    /// The destinations Send to… can offer (on, and a kind this Mac hands over itself).
    public var dests: [DestEntry] {
        catalog.dests
    }

    /// The command a send runs: once more (--again) when cpa just said it was already sent there.
    func command(for asked: CpaCommand) -> CpaCommand {
        again.flatMap { $0.base == asked ? $0.again : nil } ?? asked
    }

    func with(_ change: (inout DestState) -> Void) -> DestState {
        var copy = self
        change(&copy)
        return copy
    }
}

/// A send cpa refused with E_ALREADY, and the same send with --again for the next press.
public struct AgainAsk: Hashable, Sendable {
    public let base: CpaCommand
    public let again: CpaCommand
}
