// Every screen of docs/24 §2 in the states §8.2 asks for: each a World (what cpa answers) and the steps to
// get there (a page, a selection, a key, an action). The 1-9 project names and the words are made up.

import CardputerCore
import CardputerUI
import Foundation

/// What a scenario draws: the real window (and its page at full height), only the page at full height
/// (a tall page in several states), or the menu bar popover.
enum Output {
    case window, page, menu
}

struct Scenario {
    let name: String
    let caption: String
    var output = Output.window
    var world = World()
    var steps: @MainActor (AppModel) async -> Void = { _ in }
}

@MainActor
enum Scenarios {
    static let all: [Scenario] = inbox + pages + mcpTabs + menus + send

    static let inbox: [Scenario] = [
        Scenario(name: "01-inbox-many", caption: "Inbox: many rows; the selected one has words and audio"),
        Scenario(name: "02-inbox-no-text", caption: "Inbox: not transcribed yet, so Hand to AI is grey") { model in
            model.select("3QZK1M7-00042-0010")
        },
        Scenario(name: "03-inbox-other-mac",
                 caption: "Inbox: another Mac's project; after 1, Enter confirms") { model in
            model.select("3QZK1M7-00045-0001")
            await model.settle()
            model.press(.digit(1))
        },
        Scenario(name: "04-inbox-filed-toast", caption: "Inbox: the toast after filing with 3") { model in
            await model.act(.file(id: "3QZK1M7-00042-0033", project: "p3", other: false))
        },
        Scenario(name: "05-inbox-pick-project", caption: "Inbox: q without a project asks for one") { model in
            model.select("3QZK1M7-00042-0034")
            await model.settle()
            model.press(.letter("q"))
        },
        Scenario(name: "05b-inbox-suggestion",
                 caption: "Inbox: the words name “thesis”, so 3 Thesis is suggested; Enter approves") { model in
            model.select("3QZK1M7-00042-0041")
        },
        Scenario(name: "05c-inbox-failed", caption: "Inbox: transcription failed, says why, can transcribe again",
                 world: World(stt: .on)) { model in
            model.select("3QZK1M7-00042-0035")
        },
        Scenario(name: "06-inbox-handed", caption: "Inbox: handed to AI, reading back what was queued",
                 world: World(rows: Row.handed + Row.many)) { model in
            model.filter { _ in FilingQuery(scope: .handed) }
            await model.loadEverything()
        },
        Scenario(name: "07-inbox-all-filed",
                 caption: "Inbox: all filed ✓, today's recap (memos, minutes, projects, ★)",
                 world: World(rows: [], counts: ["new": 0, "other_mac": 0, "clips_waiting": 0, "on_device": 0])) { model in
            await model.loadRecap()
            model.celebrate()
        },
        Scenario(name: "08-inbox-fallback",
                 caption: "Basic view: no cpa filing; recordings without a project, read only",
                 world: World(filing: false, projectList: false)),
        Scenario(name: "09-inbox-cpad-down", caption: "Inbox: cpad is not running",
                 world: World(running: false, counts: ["new": 11, "other_mac": 1, "clips_waiting": 0, "on_device": nil])),
    ]

    static let pages: [Scenario] = [
        Scenario(name: "10-recordings",
                 caption: "Recordings: list and detail (summary, sentences, playback)") { model in
            model.show(.recordings)
            await model.loadEverything(pages: true)
            model.selectRecording("3QZK1M7-00042-0033")
        },
        Scenario(name: "10b-recordings-vlog",
                 caption: "Recordings: copy as vlog timecodes (★ and sentences), export SRT") { model in
            model.show(.recordings)
            await model.loadEverything(pages: true)
            model.selectRecording("3QZK1M7-00042-0033")
            await model.settle()
            model.copyVlogTimecodes()
        },
        Scenario(name: "11-device-linked", caption: "Device: linked; failed transcriptions can run again",
                 world: World(stt: .on)) {
            $0.show(.device)
        },
        Scenario(name: "11b-projects",
                 caption: "Projects: 1–9, some with a code folder, some only a group") { $0.show(.projects) },
        Scenario(name: "11c-projects-edit",
                 caption: "Projects: one being renamed, another waiting for a second click on Remove") { model in
            model.show(.projects)
            model.projectsPane.newName = "Website"
            model.startRename("p4")
            model.projectsPane.renameText = "Chores"
            model.removeProject("p5")
        },
        Scenario(name: "11d-projects-old-cpa", caption: "Projects: cpa too old to edit; says how to update",
                 world: World(projectEdit: false)) { $0.show(.projects) },
        Scenario(name: "12-device-offline", caption: "Device: not linked",
                 world: World(link: "offline")) { $0.show(.device) },
        Scenario(name: "13-device-cpad-down", caption: "Device: cpad is not running", world: World(running: false)) {
            $0.show(.device)
        },
        Scenario(name: "14-integrations-all", caption: "Integrations: everything works",
                 world: World(stt: .on)) { model in
            model.show(.integrations)
            await model.loadEverything(pages: true)
        },
        Scenario(name: "14b-integrations-cli",
                 caption: "Integrations: the command line card (absolute paths, whether each command works)",
                 output: .page, world: World(filing: false, projectList: false)) { model in
            model.show(.integrations)
            await model.loadEverything(pages: true)
        },
        Scenario(name: "15-integrations-missing", caption: "Integrations: cpa filing and cpa-stt missing",
                 world: World(filing: false, projectList: false, claim: false)) { model in
            model.show(.integrations)
            await model.loadEverything(pages: true)
        },
        Scenario(name: "16-settings-on", caption: "Settings: local transcription on",
                 world: World(stt: .on)) { $0.show(.settings) },
        Scenario(name: "17-settings-needs-model", caption: "Settings: local transcription off, model missing",
                 world: World(stt: .needsModel, claim: false)) { $0.show(.settings) },
        Scenario(name: "18-settings-no-stt", caption: "Settings: cpa-stt not installed",
                 world: World(filing: false, projectList: false)) {
            $0.show(.settings)
        },
        Scenario(name: "19-cpa-missing", caption: "cpa not found", world: World(cpa: false)),
    ]

    /// The MCP card of Integrations, one tab each: every client cpa knows and any other stdio client.
    static var mcpTabs: [Scenario] {
        (World.mcpClients + [McpClients.generic.id]).map { client in
            Scenario(name: "14-mcp-\(client)", caption: "Integrations: MCP · \(McpClients.title(client))",
                     output: .page, world: World(stt: .on)) { model in
                model.show(.integrations)
                await model.loadEverything(pages: true)
                model.integrations.client = client
            }
        }
    }

    static let menus: [Scenario] = [
        Scenario(name: "20-menu-waiting", caption: "Menu bar: rows waiting", output: .menu),
        Scenario(name: "21-menu-all-filed", caption: "Menu bar: all filed", output: .menu,
                 world: World(rows: [], counts: ["new": 0, "other_mac": 0, "clips_waiting": 0, "on_device": 0])),
        Scenario(name: "22-menu-cpad-down", caption: "Menu bar: cpad is not running", output: .menu,
                 world: World(running: false, counts: ["new": 11, "other_mac": 1, "clips_waiting": 0, "on_device": nil])),
    ]
}
