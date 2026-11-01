// Recordings, Integrations and Settings: the lists and files they show and the few commands their buttons
// run. Writes are only ever one public cpa or cpa-stt command, the one the button names.

import CardputerCore
import Foundation

extension AppModel {
    // MARK: Recordings

    func loadRecordings() async -> Bool {
        guard cpaFound else { return true }
        let generation = recordings.generation
        do {
            let rows = try await services.client.recordings(hidden: recordings.showHidden)
            let (cache, notes) = (recordingWords, notes)
            let (words, kept) = await Task.detached { cache.reading(rows, notes: notes) }.value
            recordingWords = kept
            guard generation == recordings.generation else { return true }
            applyRecordings(rows, words: words)
            return true
        } catch {
            guard generation == recordings.generation else { return true }
            recordings = recordings.with { $0.error = Feedback.failure(error) }
            return false
        }
    }

    func applyRecordings(_ rows: [RecRow], words: [String: Headline]) {
        guard rows != recordings.rows || words != recordings.words || recordings.error != nil else { return }
        let kept = rows.contains { $0.sid == recordings.selected } ? recordings.selected : nil
        let selected = kept ?? rows.first { $0.dir != nil }?.sid
        let changed = selected != recordings.detail?.id
        recordings = recordings.with {
            $0.rows = rows
            $0.words = words
            $0.error = nil
            $0.selected = selected
        }
        if changed {
            Task { await loadRecordingDetail() }
        }
    }

    public func selectRecording(_ sid: String) {
        recordings = recordings.with { $0.selected = sid }
        Task { await loadRecordingDetail() }
    }

    func loadRecordingDetail() async {
        guard let row = recordings.selectedRow else {
            recordings = recordings.with { $0.detail = nil }
            return
        }
        let notes = notes
        let detail = await Task.detached { DetailReader.recording(row, notes: notes) }.value
        guard recordings.selected == row.sid, detail != recordings.detail else { return }
        recordings = recordings.with { $0.detail = detail }
    }

    public func setShowHidden(_ on: Bool) {
        recordings = recordings.with {
            $0.showHidden = on
            $0.generation += 1
        }
        Task { _ = await loadRecordings() }
    }

    /// cpa rec unhide SID
    public func unhide(_ sid: String) {
        Task {
            do {
                _ = try await services.client.run(try .unhide(sid))
                recordings = recordings.with { $0.toast = Toast(text: tr("已重新显示"), isError: false) }
            } catch {
                recordings = recordings.with { $0.toast = Toast(text: Feedback.failure(error), isError: true) }
            }
            recordings = recordings.with { $0.generation += 1 }
            _ = await loadRecordings()
        }
    }

    public func reveal(_ url: URL?) {
        guard let url else { return }
        system.reveal(url)
    }

    public func playRecording(from seconds: Double = 0) {
        guard let detail = recordings.detail else { return }
        if seconds == 0, player.playingID == detail.id {
            player.toggle(id: detail.id, urls: detail.audio, notes: notes)
        } else {
            player.play(id: detail.id, urls: detail.audio, notes: notes, from: seconds)
        }
    }

    // MARK: Integrations

    /// The MCP clients cpa knows (its list, or its help's choices), each one's snippet, the tools, the calls.
    func loadIntegrations() async {
        guard cpaFound else { return }
        let clients = await services.client.mcpClients()
        var snippets: [String: McpSnippet] = [:]
        for client in clients {
            snippets[client.id] = try? await services.client.mcpSetup(client.id)
        }
        let tools = try? await services.client.mcpTools()
        let calls = (try? await services.client.mcpAudit(last: 20)) ?? []
        let found = snippets
        integrations = integrations.with {
            $0.clients = clients
            $0.client = $0.client ?? clients.first?.id ?? McpClients.generic.id
            $0.snippets = found
            $0.tools = tools ?? []
            $0.calls = calls
            $0.error = tools == nil ? tr("cpa mcp tools 没有回答") : nil
            $0.loaded = true
        }
    }

    public func copy(_ text: String) {
        system.copy(text)
    }

    /// Copy the connection guide: the Integrations page as Markdown, for any app's coding agent.
    public func copyBrief() {
        let notesPath = notes.map { displayPath($0.root.path, home: services.home) }
        copy(IntegrationCatalog.brief(features: features, cpaPath: services.cpaPath, cpaHome: services.cpaHome,
                                      notes: notesPath))
    }

    /// cpa export DEST [--zip] [--no-audio], to a place chosen in the save panel.
    public func export() {
        let zip = integrations.exportZip
        let audio = integrations.exportAudio
        Task {
            guard let url = await system.chooseDestination(zip ? "Cardputer.zip" : tr("Cardputer 导出")) else { return }
            integrations = integrations.with { $0.exportNote = Toast(text: tr("正在导出…"), isError: false) }
            do {
                _ = try await services.client.run(try .export(to: url.path, zip: zip, audio: audio))
                integrations = integrations.with { $0.exportNote = Toast(text: tr("已导出到 {0}", url.path), isError: false) }
            } catch {
                integrations = integrations.with { $0.exportNote = Toast(text: Feedback.failure(error), isError: true) }
            }
        }
    }
}

extension RecordingsState {
    func with(_ change: (inout RecordingsState) -> Void) -> RecordingsState {
        var copy = self
        change(&copy)
        return copy
    }
}

extension IntegrationsState {
    func with(_ change: (inout IntegrationsState) -> Void) -> IntegrationsState {
        var copy = self
        change(&copy)
        return copy
    }
}
