// The app's state and its one refresh loop (docs/24 §5.1): while the window or the menu is visible it asks
// cpa what RefreshScheduler says is due; when nothing is visible it asks only cpa status once a minute and
// a notes-dir watcher updates the menu bar number. Every answer comes from a public command or an open
// file of the notes dir; files are read off the main thread.

import CardputerCore
import Foundation
import Observation

@MainActor
@Observable
public final class AppModel {
    public internal(set) var services: AppServices
    @ObservationIgnored let system: SystemActions

    public var section: Section = .inbox
    /// The language the windows are drawn in (docs/24 §2.7); a change redraws them.
    public internal(set) var language: Language = L10n.language
    /// The choice in Settings: follow the Mac, or one language.
    public internal(set) var languageChoice: LanguageChoice = .system
    public internal(set) var features: Capabilities?
    public internal(set) var status: CpaStatus?
    public internal(set) var statusError: String?
    public internal(set) var projects: ProjectMap = .empty
    /// This Mac's name (cpa host name --json), nil when it has none or this cpa cannot say.
    public internal(set) var hostName: String?
    public internal(set) var notes: NotesDirectory?
    public internal(set) var transcriber: TranscriberClaim?
    public internal(set) var stt: SttStatus?
    public internal(set) var cpaVersion: String?
    public internal(set) var lastPull: String?
    public internal(set) var counts: FilingCounts?
    public internal(set) var menuPage: InboxPage?
    public internal(set) var answeredAt: Date?
    /// Inbox zero: the menu bar sparkle is on (a few seconds, once a day) and today's recap for the empty
    /// Inbox.
    public internal(set) var sparkling = false
    public internal(set) var recap: TodayRecap?

    public var inbox = InboxState()
    public var recordings = RecordingsState()
    public var integrations = IntegrationsState()
    public var settings = SettingsState()
    public var projectsPane = ProjectsState()
    public var dest = DestState()
    public let player = AudioPlayer()

    @ObservationIgnored var scheduler = RefreshScheduler()
    @ObservationIgnored var visible: Set<Surface> = []
    @ObservationIgnored var loop: Task<Void, Never>?
    @ObservationIgnored var watcher: NotesWatcher?
    /// The read-only list's words, kept between loads.
    @ObservationIgnored var headlines = Headlines.empty
    @ObservationIgnored var recordingWords = Headlines.empty
    @ObservationIgnored var lastPullAt: Date?
    /// The window's undo manager: ⌘Z in the Edit menu takes filing steps back.
    @ObservationIgnored public weak var undoManager: UndoManager?
    /// The app keeps "celebrated today" in its own defaults; CardputerShots does not.
    @ObservationIgnored let remembers: Bool
    @ObservationIgnored var celebratedDay: String?

    /// `start`: load everything now and keep the loop running (the app); CardputerShots drives it itself.
    public init(services: AppServices, system: SystemActions, start: Bool = false) {
        self.services = services
        self.system = system
        self.remembers = start
        self.celebratedDay = start ? UserDefaults.standard.string(forKey: Self.celebratedKey) : nil
        if start {
            applyLanguage(LanguageChoice.saved)
            Task { [weak self] in
                await self?.loadEverything()
                self?.startLoop()
                self?.watchNotes()
            }
        }
    }

    /// cpa was found and starts; false shows the cpa not found page.
    public var cpaFound: Bool {
        guard services.cpaPath != nil else { return false }
        switch features?.cpa {
        case .missing?, .broken(.launchFailed)?: return false
        default: return true
        }
    }

    /// cpa status answered without an error in the last 90 s.
    var statusFresh: Bool {
        guard statusError == nil, let answeredAt else { return false }
        return now.timeIntervalSince(answeredAt) <= MenuSummary.freshFor
    }

    public var menu: MenuSummary {
        MenuSummary.make(status: status, page: menuPage ?? inbox.page, cpaFound: cpaFound, fresh: statusFresh)
    }

    /// The number on the menu bar, the sidebar, the page header and the Inbox button: new rows of this Mac
    /// and of the other-Mac group.
    public var waiting: Int? {
        guard let counts, features?.canFile == true else { return nil }
        return counts.waiting > 0 ? counts.waiting : nil
    }

    var now: Date {
        services.now()
    }

    // MARK: visibility and the loop

    public func setVisible(_ surface: Surface, _ on: Bool) {
        visible = on ? visible.union([surface]) : visible.subtracting([surface])
        if visible.isEmpty {
            watchNotes()
        } else {
            watcher = nil
            scheduler = scheduler.expedite([.status, .list])
        }
        startLoop()
    }

    func startLoop() {
        guard loop == nil else { return }
        loop = Task { [weak self] in
            while !Task.isCancelled {
                self?.tick()
                try? await Task.sleep(nanoseconds: 1_000_000_000)
            }
        }
    }

    func tick() {
        let started = now
        let jobs = scheduler.due(now: started, visible: !visible.isEmpty, wanted: wantedJobs())
        for job in jobs {
            scheduler = scheduler.started(job, at: started)
            Task {
                let ok = await run(job)
                scheduler = scheduler.finished(job, ok: ok)
            }
        }
    }

    func wantedJobs() -> Set<RefreshJob> {
        let pages: Set<Section> = [.settings, .integrations, .device]
        let sttShown = visible.contains(.window) && pages.contains(section)
        let always: Set<RefreshJob> = [.features, .status, .projects, .list]
        return sttShown ? always.union([.stt]) : always
    }

    /// Ask again now (after an action, a filter change or a page switch).
    func refreshSoon(_ jobs: Set<RefreshJob> = [.list]) {
        scheduler = scheduler.expedite(jobs)
        tick()
    }

    public func show(_ next: Section) {
        section = next
        refreshSoon([.list, .stt])
        if next == .integrations, integrations.clients.isEmpty {
            Task { await loadIntegrations() }
        }
        if next == .integrations {
            Task {
                await loadPresets()
                _ = await loadDest()
            }
        }
    }

    /// FSEvents on <notes>/recordings while nothing is visible; a burst of changes asks for the numbers once.
    func watchNotes() {
        guard visible.isEmpty else { return }
        let dir = notes ?? NotesDirectory.resolve(status: nil, stt: nil, home: services.home, chosen: services.chosenNotes)
        guard let path = dir?.root.appendingPathComponent("recordings").path else { return }
        watcher = NotesWatcher(path: path) { [weak self] in
            Task { @MainActor in
                self?.lastPullAt = nil
                _ = await self?.loadCounts()
            }
        }
    }

    // MARK: jobs

    func run(_ job: RefreshJob) async -> Bool {
        switch job {
        case .features: return await loadFeatures()
        case .status: return await loadStatus()
        case .projects: return await loadProjects()
        case .stt: return await loadStt()
        case .list: return await loadLists()
        }
    }

    /// Everything once, in order: at start, on Check again, and for CardputerShots.
    public func loadEverything(pages: Bool = false) async {
        _ = await loadFeatures()
        _ = await loadStatus()
        _ = await loadProjects()
        _ = await loadStt()
        _ = await loadInbox()
        _ = await loadCounts()
        _ = await loadDest()
        if pages {
            _ = await loadMenu()
            _ = await loadRecordings()
            await loadIntegrations()
            await loadPresets()
        }
    }

    /// While cpa is missing, look for it again: it may have been installed since.
    func loadFeatures() async -> Bool {
        if !cpaFound, features != nil, services.discovers {
            let found = AppServices.live(.saved)
            if found.cpaPath != nil {
                services = found
            }
        }
        let found = await FeatureProbe.probe(services.client)
        features = found
        cpaVersion = found.cpa == .available ? try? await services.client.version() : nil
        if case .broken = found.cpa {
            return false
        }
        return true
    }
}
