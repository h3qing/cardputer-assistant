// The app's only door to Cardputer data that is not a file: run one public command, get its exit code and
// stdout. Everything typed is built on execute(), so the Process-backed client and the fake behave the same.

import Foundation

public struct CommandOutput: Hashable, Sendable {
    public let exitCode: Int32
    public let stdout: Data

    public init(exitCode: Int32, stdout: Data) {
        self.exitCode = exitCode
        self.stdout = stdout
    }
}

/// Why a command gave no usable answer. Never carries stderr, argv or any text of a recording.
public enum CpaError: Error, Hashable, Sendable {
    /// cpa (or cpa-stt) was not found, or failed the ownership and permission checks.
    case notFound(Tool)
    case launchFailed(Tool)
    /// Stopped after its timeout; the command's name ("cpa filing ls").
    case timedOut(String)
    /// stdout went past the 4 MiB cap.
    case outputTooLarge(String)
    /// Exit 2 without a contract document, read by a feature probe: this cpa does not know the command.
    case unsupported(String)
    /// Any other non-zero exit without a contract document.
    case failed(String, exitCode: Int32)
    /// The filing contract's error document: a fixed code and the part at fault.
    case refused(FilingFailure)
    /// The dest-cli contract's error document (protocol/dest-cli/v1): an open code and the part at fault.
    case refusedDest(DestFailure)
    /// stdout was not the JSON the contract describes.
    case badOutput(ContractError)
}

public protocol CpaClient: Sendable {
    /// Runs one command to the end and returns its exit code and stdout. Throws only when it could not:
    /// not found, launch failed, timed out, output too large.
    func execute(_ command: CpaCommand) async throws -> CommandOutput
}

extension CommandOutput {
    /// stdout of a zero exit; otherwise the typed reason. Exit 2 is also how cpa and cpa-stt report a bad
    /// value, a config or an OSError, so only a feature probe reads it as a command this cpa lacks.
    public func checked(_ name: String, exit2MeansMissing: Bool = false) throws -> Data {
        if exitCode == 0 {
            return stdout
        }
        if let failure = try? FilingContract.failure(stdout) {
            throw CpaError.refused(failure)
        }
        let missing = exitCode == 2 && exit2MeansMissing
        throw missing ? CpaError.unsupported(name) : CpaError.failed(name, exitCode: exitCode)
    }
}

extension CpaClient {
    /// stdout of a command that exited 0.
    public func run(_ command: CpaCommand, exit2MeansMissing: Bool = false) async throws -> Data {
        try await execute(command).checked(command.name, exit2MeansMissing: exit2MeansMissing)
    }

    public func status() async throws -> CpaStatus {
        try await decoded(.status, CpaJSON.status)
    }

    public func recordings(hidden: Bool) async throws -> [RecRow] {
        try await decoded(.recordings(hidden: hidden), CpaJSON.recordings)
    }

    public func projects() async throws -> ProjectList {
        try await decoded(.projects, FilingContract.projects)
    }

    public func filingList(_ query: FilingQuery, now: Date = Date(), calendar: Calendar = .current) async throws -> FilingList {
        try await decoded(try .filingList(query, now: now, calendar: calendar), FilingContract.list)
    }

    public func filingShow(_ id: String) async throws -> FilingItem {
        try await decoded(try .filingShow(id), FilingContract.item)
    }

    /// A filing write (file, done, hide, reopen, hand, or an undo): the result document.
    public func perform(_ command: CpaCommand) async throws -> FilingResult {
        try await decoded(command, FilingContract.result)
    }

    public func sttStatus() async throws -> SttStatus {
        try await decoded(.sttStatus, CpaJSON.stt)
    }

    /// `cpa --version`, one line, display_safe.
    public func version() async throws -> String {
        let text = String(decoding: try await run(.version), as: UTF8.self)
        return DisplaySafe.excerpt(text, limit: 80) ?? ""
    }

    /// This Mac's name, nil when it has none.
    public func hostName() async throws -> String? {
        try await decoded(.hostShow, FilingContract.host).host
    }

    func decoded<T>(_ command: CpaCommand, _ decode: (Data) throws -> T,
                    exit2MeansMissing: Bool = false) async throws -> T {
        let data = try await run(command, exit2MeansMissing: exit2MeansMissing)
        do {
            return try decode(data)
        } catch let error as ContractError {
            throw CpaError.badOutput(error)
        }
    }
}

/// A client with canned answers, for tests and for drawing the screens with fixture data. A command it
/// has no answer for exits 2 with nothing on stdout, like a cpa that does not know the command.
public struct FakeCpaClient: CpaClient {
    public enum Reply: Hashable, Sendable {
        case output(CommandOutput)
        case failure(CpaError)
    }

    public let replies: [CpaCommand: Reply]
    public let fallback: Reply

    public init(_ replies: [CpaCommand: Reply], fallback: Reply = .output(CommandOutput(exitCode: 2, stdout: Data()))) {
        self.replies = replies
        self.fallback = fallback
    }

    public func execute(_ command: CpaCommand) async throws -> CommandOutput {
        switch replies[command] ?? fallback {
        case .output(let output): return output
        case .failure(let error): throw error
        }
    }
}
