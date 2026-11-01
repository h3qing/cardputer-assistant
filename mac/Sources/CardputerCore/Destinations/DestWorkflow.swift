// What a Send to… click, ⌘↩, ⌥ + a letter or a Destinations card button does: one cpa dest command, then a
// short line. A send that ran but failed prints a `sent` document and exits 1: it is said, and the row stays
// in Waiting to send. A refused one prints the dest-cli error and wrote nothing. Sends have no undo (a draft
// cannot be taken back).

import Foundation

extension CpaClient {
    public func destPending() async throws -> PendingList {
        try await destDecoded(.destPending, DestContract.pending)
    }

    public func destUsual() async throws -> UsualList {
        try await destDecoded(.destUsual, DestContract.usual)
    }

    public func destCatalog() async throws -> DestCatalog {
        try await decoded(.destList, DestCatalog.decode)
    }

    public func destPresets() async throws -> PresetList {
        try await decoded(.destPresets, PresetList.decode)
    }

    /// A send's document, also when it failed (exit 1 with a `sent` document).
    public func destSent(_ command: CpaCommand) async throws -> DestSent {
        let output = try await execute(command)
        if let doc = try? DestContract.sent(output.stdout) {
            return doc
        }
        return try DestContract.sent(try output.destChecked(command.name))
    }

    /// A dest-cli document from a command that exited 0; else the dest-cli error, else the usual reasons.
    func destDecoded<T>(_ command: CpaCommand, _ decode: (Data) throws -> T) async throws -> T {
        let data = try await execute(command).destChecked(command.name)
        do {
            return try decode(data)
        } catch let error as ContractError {
            throw CpaError.badOutput(error)
        }
    }
}

extension CommandOutput {
    func destChecked(_ name: String) throws -> Data {
        if exitCode != 0, let failure = try? DestContract.failure(stdout) {
            throw CpaError.refusedDest(failure)
        }
        return try checked(name)
    }
}

/// What a send did: the line to say, and what the next press may run.
public struct DestSendOutcome: Hashable, Sendable {
    public let outcome: ActionOutcome
    /// Refused with E_ALREADY: the same send with --again, for the next press of the same key (send again).
    public let again: CpaCommand?
    /// Handed through the clipboard: the same send with --again, for Copy again.
    public let recopy: CpaCommand?

    public var toast: Toast {
        outcome.toast
    }

    public var changed: Bool {
        outcome.changed
    }

    public var undo: UndoStep? {
        outcome.undo
    }
}

public enum DestWorkflow {
    public static func send(_ command: CpaCommand, client: CpaClient) async -> DestSendOutcome {
        do {
            let doc = try await client.destSent(command)
            let outcome = ActionOutcome(toast: DestFeedback.sent(doc), undo: nil, select: nil, changed: true)
            let clip = doc.outcome.known == .handed && doc.via?.known == .clip
            return DestSendOutcome(outcome: outcome, again: nil, recopy: clip ? CpaCommand.sendAgain(doc) : nil)
        } catch CpaError.timedOut {
            let unsure = ActionOutcome(toast: Toast(text: Feedback.unsure, isError: true), undo: nil, select: nil,
                                       changed: true)
            return DestSendOutcome(outcome: unsure, again: nil, recopy: nil)
        } catch CpaError.refusedDest(let failure) where failure.code == "E_ALREADY" {
            let ask = ActionOutcome(toast: Toast(text: DestFeedback.refusal(failure), isError: false), undo: nil,
                                    select: nil, changed: false)
            return DestSendOutcome(outcome: ask, again: command.again(), recopy: nil)
        } catch {
            return DestSendOutcome(outcome: .failed(Feedback.failure(error)), again: nil, recopy: nil)
        }
    }

    public static func drop(_ command: CpaCommand, client: CpaClient) async -> ActionOutcome {
        do {
            _ = try await client.destDecoded(command, DestContract.dropped)
            return ActionOutcome(toast: Toast(text: DestFeedback.dropped, isError: false), undo: nil, select: nil,
                                 changed: true)
        } catch {
            return .failed(Feedback.failure(error))
        }
    }

    /// The test send: the first question, or why it did not go.
    public static func test(_ target: DestTestTarget, client: CpaClient) async -> DestTestStep {
        do {
            let tested = try await client.destDecoded(try .destTest(target.dest), DestContract.tested)
            return DestTestFlow.afterSend(tested, target: target)
        } catch {
            return .finished(Toast(text: Feedback.failure(error), isError: true))
        }
    }

    /// A command whose output is one line for a person (add, verify, map): nil when it worked, else why not.
    public static func plain(_ command: CpaCommand, client: CpaClient) async -> String? {
        do {
            _ = try await client.execute(command).destChecked(command.name)
            return nil
        } catch {
            return Feedback.failure(error)
        }
    }
}
