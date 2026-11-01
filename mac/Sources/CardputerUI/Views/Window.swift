// The main window: a sidebar (Inbox · Recordings · Projects · Device · Integrations · Settings) and the page,
// laid out by the app itself rather than a NavigationSplitView: on macOS 26 the split view turns a page's
// scroll view into the glass toolbar's scroll edge and can push a page under the title bar; a plain sidebar
// and page draw the same every time, and CardputerShots captures this very window offscreen. When cpa is not
// found the whole window is the cpa not found page.

import AppKit
import CardputerCore
import SwiftUI

public struct MainWindow: View {
    @Bindable var model: AppModel
    /// Reports whether the window can be seen (the app); off for an offscreen capture.
    let observes: Bool

    public init(model: AppModel, observes: Bool = true) {
        self.model = model
        self.observes = observes
    }

    public var body: some View {
        Group {
            if model.cpaFound {
                HStack(spacing: 0) {
                    SidebarView(model: model).frame(width: Theme.sidebarWidth).background(Theme.sidebar)
                    Rectangle().fill(Theme.line).frame(width: 1)
                    SectionView(model: model)
                }
            } else {
                CpaMissingView(model: model)
            }
        }
        .id(model.language)
        .frame(minWidth: Theme.minimumWindow.width, minHeight: Theme.minimumWindow.height)
        .background(Theme.bg)
        .preferredColorScheme(.dark)
        .tint(Theme.accent)
        .background {
            if observes {
                WindowObserver { visible in model.setVisible(.window, visible) }
            }
        }
    }
}

/// One page at its whole height, for CardputerShots (nothing cut off at the window's bottom).
public struct ShotPage: View {
    let model: AppModel

    public init(model: AppModel) {
        self.model = model
    }

    public var body: some View {
        SectionView(model: model).environment(\.colorScheme, .dark)
    }
}

struct SidebarView: View {
    @Bindable var model: AppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(spacing: 10) {
                MandoHeadView(scale: 3)
                VStack(alignment: .leading, spacing: 1) {
                    Text("Cardputer").font(Theme.bodyStrong).foregroundStyle(Theme.fg)
                    Text("Assistant").font(Theme.small).foregroundStyle(Theme.dim)
                }
            }
            .padding(.horizontal, 10)
            .padding(.bottom, Theme.grid * 2)
            ForEach(Section.allCases) { section in
                SidebarRow(section: section, title: title(section), badge: badge(section),
                           selected: model.section == section) { model.show(section) }
            }
            Spacer()
            SidebarStatus(menu: model.menu)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 14)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }

    func title(_ section: Section) -> String {
        section == .inbox && model.features?.canFile == false ? tr("没项目的录音") : section.title
    }

    func badge(_ section: Section) -> String? {
        section == .inbox ? model.waiting.map(String.init) : nil
    }
}

struct SidebarRow: View {
    let section: Section
    let title: String
    let badge: String?
    let selected: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 10) {
                Image(systemName: section.symbol)
                    .frame(width: 18)
                    .foregroundStyle(selected ? Theme.accent : Theme.dim)
                Text(title).font(selected ? Theme.bodyStrong : Theme.body)
                    .foregroundStyle(selected ? Theme.fg : Theme.ink2)
                Spacer(minLength: 4)
                if let badge {
                    Text(badge).font(Theme.smallStrong).foregroundStyle(Theme.onAccent)
                        .padding(.horizontal, 6).padding(.vertical, 1)
                        .background(Theme.accent, in: Capsule())
                }
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 7)
            .background(selected ? Theme.select : .clear, in: RoundedRectangle(cornerRadius: 7))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

/// The status in words at the bottom of the sidebar: the dot never speaks alone.
struct SidebarStatus: View {
    let menu: MenuSummary

    var body: some View {
        HStack(alignment: .top, spacing: 8) {
            StatusDot(light: menu.light).padding(.top, 4)
            Text(menu.headline).font(Theme.small).foregroundStyle(Theme.ink2)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(10)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Theme.card, in: RoundedRectangle(cornerRadius: 8))
    }
}

struct SectionView: View {
    @Bindable var model: AppModel

    var body: some View {
        ZStack(alignment: .top) {
            Theme.bg
            page
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    @ViewBuilder var page: some View {
        switch model.section {
        case .inbox: InboxView(model: model)
        case .recordings: RecordingsView(model: model)
        case .projects: ProjectsView(model: model)
        case .device: DeviceView(model: model)
        case .integrations: IntegrationsView(model: model)
        case .settings: SettingsView(model: model)
        }
    }
}

/// Tells the model whether the window can be seen (not closed, not minimised, not fully covered), and puts
/// the app in the Dock only while it is open.
struct WindowObserver: NSViewRepresentable {
    let changed: (Bool) -> Void

    func makeNSView(context: Context) -> NSView {
        let view = ObservingView()
        view.changed = changed
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {}

    final class ObservingView: NSView {
        var changed: ((Bool) -> Void)?
        private var tokens: [NSObjectProtocol] = []

        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            tokens.forEach(NotificationCenter.default.removeObserver)
            guard let window else { return changed?(false) ?? () }
            let center = NotificationCenter.default
            // Both arrive on the main queue (queue: .main).
            tokens = [
                center.addObserver(forName: NSWindow.didChangeOcclusionStateNotification, object: window, queue: .main) {
                    [weak self] _ in MainActor.assumeIsolated { self?.report() }
                },
                center.addObserver(forName: NSWindow.willCloseNotification, object: window, queue: .main) {
                    [weak self] _ in MainActor.assumeIsolated { self?.closed() }
                },
            ]
            NSApp.setActivationPolicy(.regular)
            report()
        }

        func report() {
            changed?(window?.occlusionState.contains(.visible) == true)
        }

        func closed() {
            changed?(false)
            NSApp.setActivationPolicy(.accessory)
        }
    }
}
