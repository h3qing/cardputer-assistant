// What the installed cpa and cpa-stt can do, found by running each probe and reading the exit code
// (argparse exits 2 for a command it does not know) and the JSON (docs/24 §4). A probe that times out or
// prints what the contract does not describe is broken, not missing: the app says so instead of guessing.

public enum FeatureState: Hashable, Sendable {
    case available
    case missing
    case broken(CpaError)
}

public struct Capabilities: Hashable, Sendable {
    /// cpa itself (cpa --version).
    public let cpa: FeatureState
    /// cpa filing ls/show/file/done/hide/reopen/hand --json (docs/23 P1).
    public let filing: FeatureState
    /// cpa project ls --json; without it the 1-9 names are the keys of cpa status.
    public let projectList: FeatureState
    /// cpa host name --json (G2, docs/23 P1-B).
    public let hostName: FeatureState
    /// cpa-stt status --json (local transcription).
    public let stt: FeatureState
    /// cpa dest pending/send/drop --json (docs/25 R1): sending memos from the Mac to other apps.
    public let dest: FeatureState
    /// cpa project add|rename|move|folder|rm --json (docs/25 §8): the Projects pane edits projects.
    public let projectEdit: FeatureState

    public init(cpa: FeatureState, filing: FeatureState, projectList: FeatureState, hostName: FeatureState,
                stt: FeatureState, dest: FeatureState = .missing, projectEdit: FeatureState = .missing) {
        self.cpa = cpa
        self.filing = filing
        self.projectList = projectList
        self.hostName = hostName
        self.stt = stt
        self.dest = dest
        self.projectEdit = projectEdit
    }

    /// The inbox can file; otherwise it is the read-only list of recordings without a project.
    public var canFile: Bool {
        filing == .available
    }

    /// Send to…, Waiting to send and the Destinations card work.
    public var canSend: Bool {
        dest == .available
    }
}

public enum FeatureProbe {
    public static let filingProbe = CpaCommand(.cpa, ["filing", "ls", "--json", "--limit", "1"])
    public static let hostProbe = CpaCommand.hostShow
    public static let destProbe = CpaCommand.destPending

    /// Every probe reads exit 2 as "this cpa does not have the command" (argparse); nothing else does.
    public static func probe(_ client: CpaClient) async -> Capabilities {
        let cpa = await state { _ = try await client.run(.version, exit2MeansMissing: true) }
        let stt = await state { _ = try await client.decoded(.sttStatus, CpaJSON.stt, exit2MeansMissing: true) }
        guard cpa == .available else {
            return Capabilities(cpa: cpa, filing: .missing, projectList: .missing, hostName: .missing, stt: stt)
        }
        async let filing = state { _ = try await client.decoded(filingProbe, FilingContract.list, exit2MeansMissing: true) }
        async let projects = state { _ = try await client.decoded(.projects, FilingContract.projects, exit2MeansMissing: true) }
        async let host = state { _ = try await client.decoded(hostProbe, FilingContract.host, exit2MeansMissing: true) }
        async let dest = state { _ = try await client.decoded(destProbe, DestContract.pending, exit2MeansMissing: true) }
        async let edit = state {
            _ = try await client.decoded(.projectsProbe, FilingContract.projects, exit2MeansMissing: true)
        }
        return Capabilities(cpa: cpa, filing: await filing, projectList: await projects, hostName: await host, stt: stt,
                            dest: await dest, projectEdit: await edit)
    }

    /// available when the probe answered; missing when the tool or the command is not there; else broken.
    static func state(_ probe: () async throws -> Void) async -> FeatureState {
        do {
            try await probe()
            return .available
        } catch CpaError.notFound, CpaError.unsupported {
            return .missing
        } catch let error as CpaError {
            return .broken(error)
        } catch {
            return .broken(.launchFailed(.cpa))
        }
    }
}
