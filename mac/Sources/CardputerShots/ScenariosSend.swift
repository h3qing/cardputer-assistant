// The screens of sending from the Mac (docs/25 §5.7, R1-E): Send to… on a memo, Waiting to send and its ⌘↩,
// the line after a send, a destination still to be tested, and Integrations → Destinations with Add and the
// one-time test's three questions. World(dest: true) is a cpa with cpa dest (Fixtures/dest).

import CardputerCore
import CardputerUI
import Foundation

extension Scenarios {
    static let send: [Scenario] = [
        Scenario(name: "24-send-to", caption: "Send to…: a memo; link destinations first, ⌥X sends in one press",
                 world: World(dest: true, destPending: false)),
        Scenario(name: "25-send-pending",
                 caption: "Waiting to send: the project's usual destination preselected; ⌘↩ sends, ⌥D drops",
                 world: World(dest: true)) { model in
            model.selectPending("3QZK1M7-00042-0051")
        },
        Scenario(name: "26-send-sent-link", caption: "After a send: filled into the app; press Return there to send",
                 world: World(dest: true, destPending: false)) { model in
            await model.sendNow(to: model.dest.dests.first { $0.kind == "url" }?.id)
        },
        Scenario(name: "27-send-sent-clip",
                 caption: "After a send: the bot still has the previous draft, so it goes on the clipboard",
                 world: World(dest: true, destClip: true)) { model in
            model.selectPending("3QZK1M7-00042-0050")
            await model.sendNow(to: nil)
        },
        Scenario(name: "28-send-untested",
                 caption: "Send to…: an untested link destination is grey and asks for a test first",
                 world: World(dest: true, destList: "dest/ls-untested.json", destPending: false)),
        Scenario(name: "29-send-meeting", caption: "A meeting has no Send to…",
                 world: World(dest: true, destPending: false)) { model in
            model.select("3QZK1M7-00042-0039")
        },
        Scenario(name: "30-dest-card", caption: "Integrations → Destinations: added, usual per project, on this Mac",
                 output: .page, world: World(stt: .on, dest: true)) { model in
            model.show(.integrations)
            await model.loadEverything(pages: true)
        },
        Scenario(name: "31-dest-add",
                 caption: "Integrations → Destinations: confirm before Add (cpa's own description)", output: .page,
                 world: World(stt: .on, dest: true)) { model in
            await openCard(model)
            let added = Set(model.dest.dests.map(\.id))
            if let preset = model.dest.presets.presets.first(where: { !added.contains($0.destID) }) {
                model.askAdd(preset)
            }
        },
        Scenario(name: "32-dest-test-appeared", caption: "One-time test: did it appear?",
                 world: World(stt: .on, dest: true, destList: "dest/ls-untested.json")) { model in
            await test(model, answers: [])
        },
        Scenario(name: "33-dest-test-sent", caption: "One-time test: did it send by itself?",
                 world: World(stt: .on, dest: true, destList: "dest/ls-untested.json")) { model in
            await test(model, answers: [true])
        },
        Scenario(name: "34-dest-test-link",
                 caption: "One-time test: it did not appear; paste the bot's link and test again",
                 world: World(stt: .on, dest: true, destList: "dest/ls-untested.json")) { model in
            await test(model, answers: [false])
        },
        Scenario(name: "35-dest-test-done", caption: "One-time test: fills a draft only, noted",
                 world: World(stt: .on, dest: true, destList: "dest/ls-untested.json")) { model in
            await test(model, answers: [true, false])
        },
    ]

    static func openCard(_ model: AppModel) async {
        model.show(.integrations)
        await model.loadEverything(pages: true)
    }

    /// The test send, then each answer in turn.
    static func test(_ model: AppModel, answers: [Bool]) async {
        await openCard(model)
        guard let dest = model.dest.dests.first(where: { $0.kind == "url" }) else { return }
        model.startTest(dest)
        await settleTest(model)
        for yes in answers {
            model.answerTest(yes: yes)
            await settleTest(model)
        }
    }

    static func settleTest(_ model: AppModel) async {
        for _ in 0..<50 {
            if case .sending = model.dest.test {
                try? await Task.sleep(nanoseconds: 20_000_000)
            } else {
                return
            }
        }
    }
}
