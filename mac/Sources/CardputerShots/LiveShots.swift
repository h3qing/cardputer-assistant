// --live: the same screens drawn from this Mac's real cpa, cpa-stt and notes dir, so a person can see what
// the app will show before opening it. Read only: every command goes through ReadOnlyClient, which runs
// only the reading commands the app uses and refuses the rest before anything starts. The PNGs carry real
// words and stay where they are written.

import AppKit
import CardputerCore
import CardputerUI
import Foundation

/// Lets through only reads: status, rec ls, project ls, host name (no name after it), filing ls and show,
/// dest ls, pending, usual and presets, mcp tools, audit and setup (setup only prints), --version, cpa-stt status.
struct ReadOnlyClient: CpaClient {
    let inner: CpaClient

    func execute(_ command: CpaCommand) async throws -> CommandOutput {
        guard Self.allows(command) else { throw CpaError.failed(command.name + " (read-only)", exitCode: 126) }
        return try await inner.execute(command)
    }

    static func allows(_ command: CpaCommand) -> Bool {
        let words = command.arguments
        guard command.tool == .cpa else { return words == ["status", "--json"] }
        let exact: [[String]] = [["--version"], ["status", "--json"], ["host", "name", "--json"], ["project", "ls", "--json"],
                                 ["dest", "ls", "--json"], ["dest", "pending", "--json"], ["dest", "usual", "--json"],
                                 ["dest", "presets", "--installed", "--json"]]
        let prefixes: [[String]] = [["rec", "ls", "--json"], ["filing", "ls", "--json"], ["mcp", "tools"], ["mcp", "audit"],
                                    ["mcp", "setup"]]
        let show = words.count == 4 && words[0] == "filing" && words[1] == "show" && words[3] == "--json"
        return exact.contains(words) || prefixes.contains { words.starts(with: $0) } || show
    }
}

@MainActor
enum LiveShots {
    static func run(into out: URL) async -> Int32 {
        let found = ProcessCpaClient.discover()
        let home = ProcessInfo.processInfo.environment["CPA_HOME"].flatMap { $0.isEmpty ? nil : $0 }
        let services = AppServices(client: ReadOnlyClient(inner: found), cpaPath: found.cpa?.path, sttPath: found.stt?.path,
                                   home: NSHomeDirectory(), chosenNotes: nil, cpaHome: home)
        let model = AppModel(services: services, system: StillSystem())
        await model.loadEverything(pages: true)
        await model.settle()
        var failures: [String] = []
        let pages: [(String, Section)] = [("live-1-inbox", .inbox), ("live-2-recordings", .recordings),
                                          ("live-3-device", .device), ("live-4-integrations", .integrations),
                                          ("live-5-settings", .settings)]
        for (name, section) in pages {
            model.show(section)
            await model.settle()
            failures += attempt(name) { try draw(model, .window, name: name, into: out) }
        }
        for client in model.integrations.clients.map(\.id) + [McpClients.generic.id] {
            model.show(.integrations)
            model.integrations.client = client
            failures += attempt("live-4-mcp-\(client)") { try draw(model, .page, name: "live-4-mcp-\(client)", into: out) }
        }
        failures += attempt("live-6-menu") { try draw(model, .menu, name: "live-6-menu", into: out) }
        if let bar = Render.menuBar([model]) {
            try? Render.write(bar, to: out.appendingPathComponent("live-7-menubar.png"))
        }
        print("live shots in \(out.path), \(failures.count) failed")
        return failures.isEmpty ? 0 : 1
    }

    static func attempt(_ name: String, _ body: () throws -> CGImage) -> [String] {
        do {
            _ = try body()
            print("ok   \(name).png")
            return []
        } catch {
            print("FAIL \(name): \(error)")
            return ["\(name): \(error)"]
        }
    }
}
