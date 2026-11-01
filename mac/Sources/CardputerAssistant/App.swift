// Cardputer Assistant for Mac (docs/24): one window (Inbox · Recordings · Projects · Device · Integrations ·
// Settings) and a menu bar item. A plain client of the public interfaces: cpa … --json, the notes dir's open
// files and cpa-stt.

import AppKit
import CardputerCore
import CardputerUI
import SwiftUI

@main
struct CardputerAssistantApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @State private var model = AppModel(services: .live(), system: MacSystem(), start: true)

    var body: some Scene {
        Window("Cardputer Assistant", id: "main") {
            MainWindow(model: model)
        }
        .defaultSize(width: Theme.windowSize.width, height: Theme.windowSize.height)
        .windowResizability(.contentMinSize)
        .commands { AppCommands(model: model) }

        MenuBarExtra {
            MenuBarOpener(model: model)
        } label: {
            MenuBarLabel(model: model)
        }
        .menuBarExtraStyle(.window)
    }
}

/// The popover, with a way to open the window from it.
struct MenuBarOpener: View {
    let model: AppModel
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        MenuBarContent(model: model, open: {
            openWindow(id: "main")
            NSApp.activate(ignoringOtherApps: true)
        }, quit: { NSApp.terminate(nil) })
    }
}

/// ⌘1-⌘6 switch pages; ⌘R asks cpa again. ⌘Z and ⇧⌘Z stay the system's: a filing step is registered with the
/// window's undo manager, so Undo names it (Undo “File under 3 Thesis”) and is grey when there is nothing to
/// undo.
struct AppCommands: Commands {
    let model: AppModel

    var body: some Commands {
        CommandMenu(tr(in: model.language, "前往")) {
            ForEach(CardputerUI.Section.allCases) { section in
                Button(section.menuTitle) { model.show(section) }
                    .keyboardShortcut(KeyEquivalent(section.shortcut))
            }
            Divider()
            Button(tr(in: model.language, "重新检查")) { model.recheck() }.keyboardShortcut("r")
        }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    /// Closing the window leaves the menu bar item running.
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        false
    }
}
