// The menu bar item (docs/24 §3): Mando's head with a dot and the number waiting to be filed; the popover
// says the state in words, lists the five newest new recordings (a click opens the window on it) and opens
// the window. cpa not found is the whole window when cpa cannot be found.

import AppKit
import CardputerCore
import SwiftUI

public struct MenuBarLabel: View {
    let model: AppModel

    public init(model: AppModel) {
        self.model = model
    }

    public var body: some View {
        let menu = model.menu
        HStack(spacing: 3) {
            Image(nsImage: MenuIcon.image(menu.light, sparkle: model.sparkling))
            if let count = model.waiting {
                Text(String(count)).monospacedDigit()
            }
        }
    }
}

public enum MenuIcon {
    /// Mando's 11 x 9 head in its own colours (readable on a light and a dark menu bar), 2 pt a pixel, and a
    /// dot in the status colour beside it.
    /// `sparkle`: inbox zero, a four-pointed star in place of the dot for a few seconds.
    public static func image(_ light: StatusLight, sparkle: Bool = false) -> NSImage {
        let image = NSImage(size: NSSize(width: 31, height: 18), flipped: true) { _ in
            for (index, entry) in MandoSprites.head.enumerated() where entry != 0 {
                colour(entry).setFill()
                NSRect(x: CGFloat(index % 11) * 2, y: CGFloat(index / 11) * 2, width: 2, height: 2).fill()
            }
            if sparkle {
                drawSparkle()
                return true
            }
            let dot = NSBezierPath(ovalIn: NSRect(x: 24.75, y: 10.75, width: 5.5, height: 5.5))
            if light == .unknown {
                // Not sure: a ring, not a dot.
                nsColour(dotColour(light)).setStroke()
                dot.lineWidth = 1.5
                dot.stroke()
            } else {
                nsColour(dotColour(light)).setFill()
                NSBezierPath(ovalIn: NSRect(x: 24, y: 10, width: 7, height: 7)).fill()
            }
            return true
        }
        image.isTemplate = false
        return image
    }

    /// A four-pointed star where the dot sits, and a small one above it, in the warm yellow.
    static func drawSparkle() {
        nsColour(ThemeTokens.warn).setFill()
        star(center: NSPoint(x: 26.75, y: 12.5), radius: 4.5).fill()
        star(center: NSPoint(x: 28.5, y: 3.5), radius: 2.3).fill()
    }

    static func star(center: NSPoint, radius: CGFloat) -> NSBezierPath {
        let path = NSBezierPath()
        let waist = radius * 0.28
        for step in 0..<8 {
            let angle = CGFloat(step) * .pi / 4 - .pi / 2
            let length = step.isMultiple(of: 2) ? radius : waist
            let point = NSPoint(x: center.x + cos(angle) * length, y: center.y + sin(angle) * length)
            step == 0 ? path.move(to: point) : path.line(to: point)
        }
        path.close()
        return path
    }

    /// A head pixel: palette entry 0x20 + n is Mando's colour n + 1; 0x13 is the eyes' dark.
    static func colour(_ entry: UInt8) -> NSColor {
        let index = Int(entry) - 0x20
        let rgb = MandoSprites.colours.indices.contains(index) ? MandoSprites.colours[index] : ThemeTokens.onAccent
        return nsColour(rgb)
    }

    static func dotColour(_ light: StatusLight) -> UInt32 {
        switch light {
        case .linked: return ThemeTokens.ok
        case .unlinked, .unknown: return ThemeTokens.dim
        case .down: return ThemeTokens.error
        }
    }

    static func nsColour(_ rgb: UInt32) -> NSColor {
        NSColor(srgbRed: CGFloat((rgb >> 16) & 0xFF) / 255, green: CGFloat((rgb >> 8) & 0xFF) / 255,
                blue: CGFloat(rgb & 0xFF) / 255, alpha: 1)
    }
}

public struct MenuBarContent: View {
    @Bindable var model: AppModel
    @Environment(\.isSnapshot) private var snapshot
    let open: () -> Void
    let quit: () -> Void

    public init(model: AppModel, open: @escaping () -> Void, quit: @escaping () -> Void) {
        self.model = model
        self.open = open
        self.quit = quit
    }

    public var body: some View {
        let menu = model.menu
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 10) {
                MandoHeadView(scale: 2.5)
                VStack(alignment: .leading, spacing: 2) {
                    HStack(spacing: 6) {
                        StatusDot(light: menu.light)
                        Text(menu.headline).font(Theme.bodyStrong).foregroundStyle(Theme.fg).lineLimit(2)
                    }
                    Text(countLine).font(Theme.small).foregroundStyle(Theme.ink2)
                }
            }
            if !menu.recent.isEmpty {
                VStack(spacing: 4) {
                    ForEach(menu.recent) { entry in
                        MenuRow(entry: entry, now: model.now, calendar: model.services.calendar) {
                            model.show(.inbox)
                            model.select(entry.id)
                            open()
                        }
                    }
                }
            } else if model.cpaFound, model.features?.canFile == true {
                HStack(spacing: 10) {
                    MandoView(pose: .hop, scale: 1)
                    Text(tr("都归好了")).font(Theme.body).foregroundStyle(Theme.ink2)
                }
            }
            Rectangle().fill(Theme.line).frame(height: 1)
            HStack {
                Button(action: open) { Label(tr("打开 Cardputer Assistant"), systemImage: "macwindow") }
                    .buttonStyle(ThemedButtonStyle(kind: .primary, compact: true))
                Spacer()
                Button(tr("退出"), action: quit).buttonStyle(ThemedButtonStyle(kind: .quiet, compact: true))
            }
        }
        .id(model.language)
        .padding(14)
        .frame(width: Theme.menuWidth)
        .background(Theme.bg)
        .environment(\.colorScheme, .dark)
        .onAppear { if !snapshot { model.setVisible(.menu, true) } }
        .onDisappear { if !snapshot { model.setVisible(.menu, false) } }
    }

    var countLine: String {
        guard model.cpaFound else { return tr("装好 cpa 才能用") }
        guard model.features?.canFile == true else { return tr("只读：cpa filing 还没合进 main") }
        return model.waiting.map { tr("{0} 条待归档", $0) } ?? tr("没有待归档的")
    }
}

struct MenuRow: View {
    let entry: InboxEntry
    let now: Date
    let calendar: Calendar
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(alignment: .top, spacing: 8) {
                Image(systemName: Tone.symbol(entry.tx)).foregroundStyle(Tone.of(entry.tx).foreground).frame(width: 16)
                VStack(alignment: .leading, spacing: 2) {
                    Text(entry.title(calendar: calendar)).font(Theme.body).foregroundStyle(entry.hasWords ? Theme.fg : Theme.dim)
                        .lineLimit(1)
                    Text(Format.when(entry.at, now: now, calendar: calendar)).font(Theme.small).foregroundStyle(Theme.dim)
                }
                Spacer(minLength: 0)
            }
            .padding(8)
            .background(Theme.card, in: RoundedRectangle(cornerRadius: 7))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

struct CpaMissingView: View {
    @Bindable var model: AppModel

    var body: some View {
        EmptyState(pose: .think, title: tr("没找到 cpa"),
                   detail: tr("这个 app 只通过 cpa 读写 Cardputer 的数据。照下面做一遍，再点重新查找。")) {
            CardBox {
                step(1, tr("在仓库目录里装好 cpa"), command: "uv tool install ./daemon")
                step(2, tr("装好后台服务 cpad"), command: "cpa service install")
                step(3, tr("回来点重新查找"), command: nil)
                Text(tr("找过：~/.local/bin/cpa、PATH 里的、/opt/homebrew/bin、/usr/local/bin，和你选的。"))
                    .font(Theme.small).foregroundStyle(Theme.dim).fixedSize(horizontal: false, vertical: true)
                HStack(spacing: 8) {
                    Button { model.rediscover() } label: { Label(tr("重新查找"), systemImage: "arrow.clockwise") }
                        .buttonStyle(ThemedButtonStyle(kind: .primary))
                    Button(tr("选择 cpa…")) { model.chooseCpa() }
                        .buttonStyle(ThemedButtonStyle(kind: .quiet))
                }
            }
            .frame(maxWidth: 520)
        }
        .background(Theme.bg)
    }

    func step(_ number: Int, _ text: String, command: String?) -> some View {
        HStack(spacing: 10) {
            KeyCap(text: String(number), highlighted: true)
            Text(text).font(Theme.body).foregroundStyle(Theme.fg)
            Spacer(minLength: 8)
            if let command {
                CommandChip(command: command, copy: model.copy).fixedSize()
            }
        }
    }
}
