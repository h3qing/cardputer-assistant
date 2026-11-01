// What each screen shows, as plain values: a change makes a new value and the model swaps it in whole.

import CardputerCore
import Foundation

public enum Section: String, CaseIterable, Identifiable, Sendable {
    case inbox, recordings, projects, device, integrations, settings

    public var id: String {
        rawValue
    }

    public var menuTitle: String {
        title
    }

    var title: String {
        switch self {
        case .inbox: return tr("待归档")
        case .recordings: return tr("录音")
        case .projects: return tr("项目")
        case .device: return tr("设备")
        case .integrations: return tr("集成")
        case .settings: return tr("设置")
        }
    }

    var symbol: String {
        switch self {
        case .inbox: return "tray.full"
        case .recordings: return "waveform"
        case .projects: return "square.grid.3x3"
        case .device: return "keyboard"
        case .integrations: return "puzzlepiece.extension"
        case .settings: return "gearshape"
        }
    }

    /// ⌘1 … ⌘6
    public var shortcut: Character {
        Character(String((Section.allCases.firstIndex(of: self) ?? 0) + 1))
    }
}

/// Where the app is looked at: polling runs only while one of them is visible.
public enum Surface: Hashable, Sendable {
    case window, menu
}

/// One recording's full text, transcript and audio, for the detail pane.
public struct EntryDetail: Hashable, Sendable {
    public let id: String
    public let text: String?
    public let sentences: [TimedSentence]
    public let todos: [Todo]
    public let summary: [String]
    public let audio: [URL]
    public let queueText: String?
    /// The queue item's id (q12) when it was handed to AI.
    public let queueID: String?
    public let session: URL?
    /// Why the transcription stopped (status.json's reason, result.json's error), in words.
    public let stopped: String?
    /// The ★ marks of manifest.json, in seconds (a recording's detail only).
    public var marks: [Double] = []
    /// manifest.json's duration_ms in seconds (a recording's detail only): where the last SRT cue ends.
    public var length: Double?
}

public struct InboxState: Hashable, Sendable {
    public var query = FilingQuery()
    public var search = ""
    public var page: InboxPage?
    public var error: String?
    public var selected: String?
    public var detail: EntryDetail?
    public var pending: KeyPending = .none
    public var undo: UndoStack = .empty
    public var toast: Toast?
    /// The id waiting for the second click on Hide.
    public var confirmHide: String?
    public var busy = false
    /// Bumped by every filter change and action: a load started before it is dropped.
    public var generation = 0
    /// The last list of everything waiting had a recording whose transcription failed: No words shows.
    public var stuckSeen = false

    public var entries: [InboxEntry] {
        page?.entries ?? []
    }

    public var selectedEntry: InboxEntry? {
        entries.first { $0.id == selected }
    }

    var isFallback: Bool {
        page?.source == .fallback
    }
}

public struct RecordingsState: Hashable, Sendable {
    public var rows: [RecRow] = []
    /// Each row's words, by id.
    public var words: [String: Headline] = [:]
    public var showHidden = false
    public var selected: String?
    public var detail: EntryDetail?
    public var error: String?
    public var toast: Toast?
    public var generation = 0

    public var selectedRow: RecRow? {
        rows.first { $0.sid == selected }
    }
}

public struct IntegrationsState: Hashable, Sendable {
    /// The MCP clients cpa can print a snippet for (its list, or its help's choices).
    public var clients: [McpClientEntry] = []
    /// The selected tab: a client's id, or McpClients.generic.id.
    public var client: String?
    public var snippets: [String: McpSnippet] = [:]
    public var tools: [McpTool] = []
    public var calls: [McpCall] = []
    public var error: String?
    public var exportZip = false
    public var exportAudio = true
    public var exportNote: Toast?
    public var loaded = false
}

/// Projects (docs/24 §2.6): the new name being typed, the row being renamed, a removal waiting for its second
/// click.
public struct ProjectsState: Hashable, Sendable {
    public var newName = ""
    /// The key being renamed, and the name typed so far.
    public var renaming: String?
    public var renameText = ""
    /// The key waiting for the second click on Remove.
    public var confirmRemove: String?
    public var toast: Toast?
    public var busy = false

    func with(_ change: (inout ProjectsState) -> Void) -> ProjectsState {
        var copy = self
        change(&copy)
        return copy
    }
}

public struct SettingsState: Hashable, Sendable {
    public var note: Toast?
    public var busy = false
    /// The last cpa-stt use failed: offer Choose a model file… whatever cpa-stt status says (G9).
    public var useFailed = false
}
