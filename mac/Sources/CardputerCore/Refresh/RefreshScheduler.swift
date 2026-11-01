// When to ask cpa again (docs/24 §5.1): while a window or the menu is visible, status and the page's list
// every 3 s, cpa-stt every 10 s, the project table and the feature probe every 60 s; while nothing is
// visible, only cpa status once a minute (one quick call keeps the menu bar dot true). A job still running
// is skipped, never queued; two failures in a row back a job off to 15 s. Pure: the clock comes in.

import Foundation

public enum RefreshJob: String, CaseIterable, Hashable, Sendable {
    /// cpa status --json
    case status
    /// The current page's list (cpa filing ls or cpa rec ls).
    case list
    /// cpa project ls --json
    case projects
    /// cpa-stt status --json, while settings or integrations are open.
    case stt
    /// FeatureProbe: what this cpa and cpa-stt can do.
    case features

    public var every: TimeInterval {
        switch self {
        case .status, .list: return 3
        case .stt: return 10
        case .projects, .features: return 60
        }
    }
}

public struct RefreshScheduler: Hashable, Sendable {
    public static let backoff: TimeInterval = 15
    public static let failuresBeforeBackoff = 2
    /// While nothing is visible: only these, this often.
    public static let hiddenJobs: Set<RefreshJob> = [.status]
    public static let hiddenEvery: TimeInterval = 60

    let lastStart: [RefreshJob: Date]
    let running: Set<RefreshJob>
    let failures: [RefreshJob: Int]

    public init() {
        self.init(lastStart: [:], running: [], failures: [:])
    }

    init(lastStart: [RefreshJob: Date], running: Set<RefreshJob>, failures: [RefreshJob: Int]) {
        self.lastStart = lastStart
        self.running = running
        self.failures = failures
    }

    /// The jobs to start now, in RefreshJob order; while nothing is visible, only cpa status, once a minute.
    public func due(now: Date, visible: Bool, wanted: Set<RefreshJob>) -> [RefreshJob] {
        let allowed = visible ? wanted : wanted.intersection(Self.hiddenJobs)
        return RefreshJob.allCases.filter { job in
            guard allowed.contains(job), !running.contains(job) else { return false }
            let every = visible ? interval(job) : max(interval(job), Self.hiddenEvery)
            return lastStart[job].map { now.timeIntervalSince($0) >= every } ?? true
        }
    }

    /// The job's interval now: its own, or the back-off after failures in a row (never shorter).
    public func interval(_ job: RefreshJob) -> TimeInterval {
        (failures[job] ?? 0) >= Self.failuresBeforeBackoff ? max(job.every, Self.backoff) : job.every
    }

    public func isRunning(_ job: RefreshJob) -> Bool {
        running.contains(job)
    }

    public func started(_ job: RefreshJob, at now: Date) -> RefreshScheduler {
        RefreshScheduler(lastStart: lastStart.merging([job: now]) { _, new in new },
                         running: running.union([job]), failures: failures)
    }

    public func finished(_ job: RefreshJob, ok: Bool) -> RefreshScheduler {
        let count = ok ? 0 : (failures[job] ?? 0) + 1
        return RefreshScheduler(lastStart: lastStart, running: running.subtracting([job]),
                                failures: failures.merging([job: count]) { _, new in new })
    }

    /// Due at once (after an action, when the window comes back); a running job still finishes first.
    public func expedite(_ jobs: Set<RefreshJob>) -> RefreshScheduler {
        RefreshScheduler(lastStart: lastStart.filter { !jobs.contains($0.key) }, running: running, failures: failures)
    }
}
