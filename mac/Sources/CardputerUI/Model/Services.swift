// What the model needs from the outside, passed in so CardputerShots can draw every screen with fixture
// data: the cpa client, the clock, the paths the user chose, and the few system actions (copy, reveal in
// Finder, the open and save panels).

import AppKit
import CardputerCore
import Foundation
import UniformTypeIdentifiers

public struct AppServices: Sendable {
    public let client: CpaClient
    /// Where cpa and cpa-stt were found, to show in settings; nil when not found.
    public let cpaPath: String?
    public let sttPath: String?
    public let home: String
    /// Settings: a notes dir the user picked (the last choice, after notes_dir, cpa-stt and ~/CardputerNotes).
    public let chosenNotes: String?
    public let now: @Sendable () -> Date
    public let calendar: Calendar
    /// CPA_HOME of the app's environment, when set (the MCP snippet of any other client carries it).
    public let cpaHome: String?
    /// Looks for cpa again while it is missing (the app, not CardputerShots).
    public let discovers: Bool

    public init(client: CpaClient, cpaPath: String?, sttPath: String?, home: String, chosenNotes: String?,
                now: @escaping @Sendable () -> Date = { Date() }, calendar: Calendar = .current,
                cpaHome: String? = nil, discovers: Bool = false) {
        self.client = client
        self.cpaPath = cpaPath
        self.sttPath = sttPath
        self.home = home
        self.chosenNotes = chosenNotes
        self.now = now
        self.calendar = calendar
        self.cpaHome = cpaHome
        self.discovers = discovers
    }

    /// The real thing: cpa and cpa-stt found as docs/24 §5.2 says, the paths chosen in settings first.
    public static func live(_ choices: Choices = .saved) -> AppServices {
        let client = ProcessCpaClient.discover(chosenCpa: choices.cpa, chosenStt: choices.stt)
        let home = ProcessInfo.processInfo.environment["CPA_HOME"].flatMap { $0.isEmpty ? nil : $0 }
        return AppServices(client: client, cpaPath: client.cpa?.path, sttPath: client.stt?.path, home: NSHomeDirectory(),
                           chosenNotes: choices.notes, cpaHome: home, discovers: true)
    }
}

/// The paths chosen in settings, kept in this app's own defaults (never in cpa's files).
public struct Choices: Hashable, Sendable {
    public let cpa: String?
    public let stt: String?
    public let notes: String?

    static let keys = (cpa: "chosenCpa", stt: "chosenStt", notes: "chosenNotes")

    public static var saved: Choices {
        let defaults = UserDefaults.standard
        return Choices(cpa: defaults.string(forKey: keys.cpa), stt: defaults.string(forKey: keys.stt),
                       notes: defaults.string(forKey: keys.notes))
    }

    func saving() -> Choices {
        let defaults = UserDefaults.standard
        defaults.set(cpa, forKey: Self.keys.cpa)
        defaults.set(stt, forKey: Self.keys.stt)
        defaults.set(notes, forKey: Self.keys.notes)
        return self
    }
}

@MainActor
public protocol SystemActions {
    func copy(_ text: String)
    func reveal(_ url: URL)
    func chooseFile(_ message: String) async -> URL?
    func chooseFolder(_ message: String) async -> URL?
    func chooseDestination(_ name: String) async -> URL?
}

/// The Mac: the general pasteboard, Finder and the system panels.
public struct MacSystem: SystemActions {
    public init() {}

    public func copy(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }

    public func reveal(_ url: URL) {
        NSWorkspace.shared.activateFileViewerSelecting([url])
    }

    public func chooseFile(_ message: String) async -> URL? {
        let panel = NSOpenPanel()
        panel.message = message
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        return panel.runModal() == .OK ? panel.url : nil
    }

    public func chooseFolder(_ message: String) async -> URL? {
        let panel = NSOpenPanel()
        panel.message = message
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.canCreateDirectories = true
        return panel.runModal() == .OK ? panel.url : nil
    }

    public func chooseDestination(_ name: String) async -> URL? {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = name
        panel.canCreateDirectories = true
        // A name with an extension (Cardputer.zip, <id>.srt) keeps it: the panel offers only that type.
        let ext = (name as NSString).pathExtension
        if !ext.isEmpty, let type = UTType(filenameExtension: ext) {
            panel.allowedContentTypes = [type]
        }
        return panel.runModal() == .OK ? panel.url : nil
    }
}

/// For drawing screens: nothing leaves the app.
public struct StillSystem: SystemActions {
    public init() {}
    public func copy(_ text: String) {}
    public func reveal(_ url: URL) {}
    public func chooseFile(_ message: String) async -> URL? { nil }
    public func chooseFolder(_ message: String) async -> URL? { nil }
    public func chooseDestination(_ name: String) async -> URL? { nil }
}
