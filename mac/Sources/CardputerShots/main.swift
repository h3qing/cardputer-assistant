// swift run CardputerShots [--out DIR] [--lang en|zh] [name-filter]: draws every screen of the app in its key
// states with made-up data (World, ShotClient), in English to DIR (default mac/out/shots) and in Chinese to
// DIR/zh: the real window captured offscreen
// (1200 x 800 at 2x) and each page at its whole height (NAME-full.png), the menu popover, the menu bar
// item and an overview sheet. `--live` draws the same screens from the real cpa of this Mac instead, read
// only (LiveShots). Exits non-zero when a shot could not be drawn or is one flat colour. Whether a screen is
// clear and easy to use is for a person to look at.

import AppKit
import CardputerCore
import CardputerUI
import Foundation
import SwiftUI

/// A scenario's own copy of Fixtures/notes; its newest manifest says it was pulled 4 minutes ago.
@MainActor
func notesCopy(fresh claim: Bool) throws -> String {
    let dir = FileManager.default.temporaryDirectory
        .appendingPathComponent("cardputer-shots-\(UUID().uuidString)/CardputerNotes")
    try FileManager.default.createDirectory(at: dir.deletingLastPathComponent(), withIntermediateDirectories: true)
    try FileManager.default.copyItem(at: Fixtures.root.appendingPathComponent("notes"), to: dir)
    let marker = dir.appendingPathComponent("recordings/.transcriber")
    if claim {
        let beat = ISO8601DateFormatter().string(from: Fixtures.now.addingTimeInterval(-40))
        let text = #"{"v":1,"app":"cpa-stt","pid":4242,"since":"2026-10-22T09:00:00+08:00","beat_at":"\#(beat)"}"#
        try Data(text.utf8).write(to: marker)
    } else {
        try FileManager.default.removeItem(at: marker)
    }
    try freshen(dir.appendingPathComponent("recordings/2026-09-16/0546-3qzk1m7-00042-0035/manifest.json"))
    return dir.path
}

func freshen(_ manifest: URL) throws {
    guard var object = try JSONSerialization.jsonObject(with: Data(contentsOf: manifest)) as? [String: Any] else { return }
    object["written_at"] = ISO8601DateFormatter().string(from: Fixtures.now.addingTimeInterval(-240))
    try JSONSerialization.data(withJSONObject: object).write(to: manifest)
}

@MainActor
func model(for scenario: Scenario) async throws -> AppModel {
    let notes = try notesCopy(fresh: scenario.world.claim)
    let world = scenario.world.with(notes: notes)
    // The copy is <temp>/CardputerNotes, so with HOME = <temp> it is found as ~/CardputerNotes.
    let home = (notes as NSString).deletingLastPathComponent
    let services = AppServices(client: ShotClient(world: world), cpaPath: world.cpa ? home + "/.local/bin/cpa" : nil,
                               sttPath: world.stt == .missing ? nil : home + "/.local/bin/cpa-stt",
                               home: home, chosenNotes: nil, now: { Fixtures.now }, calendar: Fixtures.calendar)
    let model = AppModel(services: services, system: StillSystem())
    await model.loadEverything(pages: scenario.output == .menu)
    await model.settle()
    await scenario.steps(model)
    await model.settle()
    return model
}

/// The drawings of one model: the window (and its page at full height), the page alone, or the popover.
@MainActor
func draw(_ model: AppModel, _ output: Output, name: String, into dir: URL) throws -> CGImage {
    var first: CGImage?
    let keep = { (suffix: String, image: CGImage?) throws in
        guard let image else { throw ShotError.render(name + suffix) }
        guard !Render.isFlat(image) else { throw ShotError.flat(name + suffix) }
        try Render.write(image, to: dir.appendingPathComponent(name + suffix + ".png"))
        first = first ?? image
    }
    switch output {
    case .window:
        // The real window offscreen; where no window can be made (a session without a display), the same view
        // through ImageRenderer.
        let captured = WindowCapture.image(MainWindow(model: model, observes: false), size: Theme.windowSize)
        try keep("", captured.flatMap { Render.isFlat($0) ? nil : $0 }
            ?? Render.image(MainWindow(model: model, observes: false), width: Theme.windowSize.width,
                            height: Theme.windowSize.height))
        if model.cpaFound {
            try keep("-full", Render.image(ShotPage(model: model), width: Theme.pageWidth, height: nil))
        }
    case .page:
        try keep("", Render.image(ShotPage(model: model), width: Theme.pageWidth, height: nil))
    case .menu:
        try keep("", Render.image(MenuBarContent(model: model, open: {}, quit: {}), width: 340, height: nil))
    }
    guard let first else { throw ShotError.render(name) }
    return first
}

@MainActor
func main() async -> Int32 {
    let args = Array(CommandLine.arguments.dropFirst())
    let outIndex = args.firstIndex(of: "--out")
    let langIndex = args.firstIndex(of: "--lang")
    let out = outIndex.map { URL(fileURLWithPath: args[$0 + 1]) }
        ?? Fixtures.root.deletingLastPathComponent().appendingPathComponent("out/shots")
    let values = [outIndex, langIndex].compactMap { $0.map { $0 + 1 } }
    let filter = args.enumerated().first { index, arg in !arg.hasPrefix("--") && !values.contains(index) }?.element
    if args.contains("--live") {
        try? FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)
        return await LiveShots.run(into: out)
    }
    // docs/24 §2.7: English into DIR, Chinese into DIR/zh (--lang en or zh for one of them).
    let wanted = langIndex.flatMap { Language(rawValue: args[$0 + 1]) }.map { [$0] } ?? [.en, .zh]
    var failed = 0
    for language in wanted {
        L10n.language = language
        failed += await drawAll(into: language == .en ? out : out.appendingPathComponent("zh"), filter: filter)
    }
    return failed == 0 ? 0 : 1
}

/// Every scenario (or those whose name has `filter`), the menu bar item and the overview sheet; the failures.
@MainActor
func drawAll(into out: URL, filter: String?) async -> Int {
    try? FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)
    var shots: [(caption: String, image: CGImage)] = []
    var failures: [String] = []
    for scenario in Scenarios.all where filter.map(scenario.name.contains) ?? true {
        do {
            let image = try draw(try await model(for: scenario), scenario.output, name: scenario.name, into: out)
            shots.append((scenario.caption, image))
            print("ok   \(scenario.name).png")
        } catch {
            failures.append("\(scenario.name): \(error)")
            print("FAIL \(scenario.name): \(error)")
        }
    }
    if filter == nil || "23-menubar".contains(filter ?? "") {
        failures += await menuBar(into: out)
    }
    if filter == nil, let sheet = Render.overview(shots) {
        try? Render.write(sheet, to: out.appendingPathComponent("00-overview.png"))
        print("ok   00-overview.png")
    }
    print("\(shots.count) shots in \(out.path), \(failures.count) failed")
    return failures.count
}

/// The menu bar item as MenuBarExtra shows it (MenuBarLabel: Mando, the dot, the number), in each state.
@MainActor
func menuBar(into out: URL) async -> [String] {
    var models: [AppModel] = []
    for world in [World(), World(link: "offline"), World(running: false), World(rows: [], counts: ["new": 0, "other_mac": 0, "clips_waiting": 0, "on_device": 0])] {
        if let model = try? await model(for: Scenario(name: "23", caption: "", world: world)) {
            models.append(model)
        }
    }
    guard let image = Render.menuBar(models), !Render.isFlat(image) else { return ["23-menubar: not drawn"] }
    guard (try? Render.write(image, to: out.appendingPathComponent("23-menubar.png"))) != nil else { return ["23-menubar"] }
    print("ok   23-menubar.png")
    return []
}

extension World {
    func with(notes: String) -> World {
        var copy = self
        copy.notesPath = notes
        return copy
    }
}

exit(await main())
