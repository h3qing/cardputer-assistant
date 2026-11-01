// The real client: finds cpa and cpa-stt (ToolLocator) and runs each command through ProcessRunner with
// the allow-listed environment. stderr is never passed on.

import Foundation

public struct ProcessCpaClient: CpaClient {
    public let cpa: ToolLocation?
    public let stt: ToolLocation?
    /// The app's own environment; only ChildEnvironment.passed reaches a child.
    public let environment: [String: String]

    public init(cpa: ToolLocation?, stt: ToolLocation?, environment: [String: String]) {
        self.cpa = cpa
        self.stt = stt
        self.environment = environment
    }

    /// Finds both tools from the HOME and PATH of `environment` (and the paths chosen in settings).
    public static func discover(environment: [String: String] = ProcessInfo.processInfo.environment,
                                chosenCpa: String? = nil, chosenStt: String? = nil) -> ProcessCpaClient {
        let home = environment["HOME"] ?? NSHomeDirectory()
        let path = environment["PATH"]
        return ProcessCpaClient(cpa: ToolLocator.locate(.cpa, home: home, pathVariable: path, chosen: chosenCpa),
                                stt: ToolLocator.locate(.cpaStt, home: home, pathVariable: path, chosen: chosenStt),
                                environment: environment)
    }

    public func execute(_ command: CpaCommand) async throws -> CommandOutput {
        guard let location = command.tool == .cpa ? cpa : stt else {
            throw CpaError.notFound(command.tool)
        }
        let request = RunRequest(
            executable: location.path, arguments: command.arguments, stdin: command.stdin,
            environment: ChildEnvironment.make(parent: environment, toolDirectory: location.directory),
            timeout: command.timeout, outputCap: command.outputCap,
            workingDirectory: environment["HOME"] ?? "/")
        let raw: RawOutput
        do {
            raw = try await ProcessRunner.run(request)
        } catch {
            throw CpaError.launchFailed(command.tool)
        }
        return try Self.output(raw, of: command)
    }

    /// A run that timed out or overflowed is an error; anything else is its exit code and stdout.
    public static func output(_ raw: RawOutput, of command: CpaCommand) throws -> CommandOutput {
        if raw.timedOut {
            throw CpaError.timedOut(command.name)
        }
        if raw.truncated {
            throw CpaError.outputTooLarge(command.name)
        }
        return CommandOutput(exitCode: raw.exitCode, stdout: raw.stdout)
    }
}
