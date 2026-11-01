import CardputerCore
import Foundation

private let t0 = Date(timeIntervalSince1970: 1_000_000)
private let all: Set<RefreshJob> = [.status, .list, .projects, .stt, .features]

private func at(_ seconds: TimeInterval) -> Date {
    t0.addingTimeInterval(seconds)
}

/// Started at t0 and finished, with the given outcome.
private func ran(_ jobs: [RefreshJob], ok: Bool = true, from start: RefreshScheduler = RefreshScheduler()) -> RefreshScheduler {
    jobs.reduce(start) { $0.started($1, at: t0).finished($1, ok: ok) }
}

let refreshTests = Suite(name: "Refresh", cases: [
    test("visible: every wanted job is due at once; hidden: only cpa status, once a minute (the menu bar dot)") {
        let fresh = RefreshScheduler()
        let ranStatus = ran([.status])
        return [
            expectEqual(Set(fresh.due(now: t0, visible: true, wanted: all)), all),
            expectEqual(fresh.due(now: t0, visible: false, wanted: all), [.status]),
            expectEqual(ranStatus.due(now: at(59), visible: false, wanted: all), []),
            expectEqual(ranStatus.due(now: at(60), visible: false, wanted: all), [.status]),
            expectEqual(ran([.list]).due(now: at(3600), visible: false, wanted: [.list]), [], "no list while hidden"),
            expectEqual(fresh.due(now: t0, visible: true, wanted: [.status]), [.status], "only what the page wants"),
        ]
    },
    test("status and the list every 3 s, the stt status every 10 s, projects and features every 60 s") {
        let done = ran(Array(all))
        let due = { (seconds: TimeInterval) in Set(done.due(now: at(seconds), visible: true, wanted: all)) }
        return [
            expectEqual(due(2.9), []),
            expectEqual(due(3), [.status, .list]),
            expectEqual(due(10), [.status, .list, .stt]),
            expectEqual(due(59), [.status, .list, .stt]),
            expectEqual(due(60), all),
        ]
    },
    test("a job still running is skipped, never queued") {
        let running = RefreshScheduler().started(.status, at: t0)
        return [
            expectEqual(running.due(now: at(30), visible: true, wanted: [.status]), []),
            expect(running.isRunning(.status)),
            expectEqual(running.finished(.status, ok: true).due(now: at(30), visible: true, wanted: [.status]), [.status]),
        ]
    },
    test("after two failures in a row a job backs off to 15 s; a success resets it") {
        let once = ran([.status], ok: false)
        let twice = ran([.status], ok: false, from: once)
        let healed = ran([.status], ok: true, from: twice)
        return [
            expectEqual(once.interval(.status), 3),
            expectEqual(twice.interval(.status), 15),
            expectEqual(twice.due(now: at(14), visible: true, wanted: [.status]), []),
            expectEqual(twice.due(now: at(15), visible: true, wanted: [.status]), [.status]),
            expectEqual(twice.interval(.projects), 60, "a slower job keeps its own interval"),
            expectEqual(healed.interval(.status), 3),
        ]
    },
    test("expedite: due at once, after an action or when the window comes back") {
        let done = ran([.status, .list])
        let soon = done.expedite([.list])
        return [
            expectEqual(soon.due(now: at(1), visible: true, wanted: [.status, .list]), [.list]),
            expectEqual(soon.due(now: at(1), visible: false, wanted: [.status, .list]), [], "still nothing while hidden"),
        ]
    },
])
