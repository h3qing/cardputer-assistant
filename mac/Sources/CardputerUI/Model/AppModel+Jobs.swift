// The refresh jobs: cpa status (and the notes dir it points to), the project table and this Mac's name,
// cpa-stt, and the lists. A result is assigned only when it differs, so the views do not redraw every 3 s.

import CardputerCore
import Foundation

/// What loadStatus reads from the notes dir, built off the main thread.
struct StatusExtras: Sendable {
    let notes: NotesDirectory?
    let transcriber: TranscriberClaim?
    let lastPull: String??

    static func read(status: CpaStatus?, stt: SttStatus?, home: String, chosen: String?,
                                 pull: Bool) -> StatusExtras {
        let notes = NotesDirectory.resolve(status: status, stt: stt, home: home, chosen: chosen)
        return StatusExtras(notes: notes, transcriber: notes?.transcriber(),
                            lastPull: pull ? .some(notes?.latestPull()) : .none)
    }
}

extension AppModel {
    /// How often the newest manifest is looked at while the window is open (the watcher clears it too).
    static let pullEvery: TimeInterval = 60

    func loadStatus() async -> Bool {
        guard cpaFound else { return true }
        do {
            let fresh = try await services.client.status()
            if fresh != status {
                status = fresh
            }
            statusError = nil
            answeredAt = now
        } catch {
            statusError = Feedback.failure(error)
        }
        await loadExtras()
        return statusError == nil
    }

    func loadExtras() async {
        let pull = lastPullAt.map { now.timeIntervalSince($0) >= Self.pullEvery } ?? true
        let (status, stt, home, chosen) = (status, stt, services.home, services.chosenNotes)
        let extras = await Task.detached {
            StatusExtras.read(status: status, stt: stt, home: home, chosen: chosen, pull: pull)
        }.value
        if extras.notes != notes {
            notes = extras.notes
        }
        if extras.transcriber != transcriber {
            transcriber = extras.transcriber
        }
        if case .some(let pulled) = extras.lastPull {
            lastPullAt = now
            if pulled != lastPull {
                lastPull = pulled
            }
        }
    }

    func loadProjects() async -> Bool {
        guard cpaFound else { return true }
        if features?.hostName == .available, let name = try? await services.client.hostName(), name != hostName {
            hostName = name
        }
        let listed = features?.projectList == .available
        guard let map = try? await ProjectMap.load(services.client, status: status, listed: listed) else { return false }
        if map != projects {
            projects = map
        }
        return true
    }

    func loadStt() async -> Bool {
        guard features?.stt == .available else {
            stt = nil
            return true
        }
        let fresh = try? await services.client.sttStatus()
        if fresh != stt {
            stt = fresh
        }
        return fresh != nil
    }

    func loadLists() async -> Bool {
        let window = visible.contains(.window)
        let inboxOK = window && section == .inbox ? await loadInbox() : (window ? await loadCounts() : true)
        let menuOK = visible.contains(.menu) ? await loadMenu() : true
        let recOK = window && section == .recordings ? await loadRecordings() : true
        let destOK = window && section == .inbox ? await loadDest() : true
        return inboxOK && menuOK && recOK && destOK
    }

    /// Only the numbers (cpa filing ls --json --limit 1), for the sidebar and the menu bar.
    func loadCounts() async -> Bool {
        guard cpaFound, features?.canFile == true else { return true }
        guard let list = try? await services.client.filingList(FilingQuery(limit: 1), now: now,
                                                                  calendar: services.calendar) else { return false }
        if list.counts != counts {
            setCounts(list.counts)
        }
        return true
    }

    /// The menu's five newest new entries, whatever the window's filters are; nothing in the read-only list.
    func loadMenu() async -> Bool {
        guard cpaFound, features?.canFile == true else {
            menuPage = nil
            return true
        }
        let loader = InboxLoader(client: services.client, notes: notes)
        guard let page = try? await loader.load(FilingQuery(limit: 5), canFile: true, now: now,
                                                calendar: services.calendar) else { return false }
        if page != menuPage {
            menuPage = page
        }
        if case .filing(let fresh) = page.source, fresh != counts {
            setCounts(fresh)
        }
        return true
    }

    /// Check again (⌘R): what cpa can do, then everything, now.
    public func recheck() {
        Task { await loadEverything() }
    }

    // MARK: choices

    /// Find cpa and cpa-stt again (Settings' Look again, or after choosing a path), then reload everything.
    public func rediscover(_ choices: Choices = .saved) {
        services = .live(choices.saving())
        features = nil
        Task { await loadEverything() }
    }
}
