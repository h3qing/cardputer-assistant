// The few building blocks every screen uses, all drawn by SwiftUI itself (so a screen renders the same in
// the window and in CardputerShots): cards, pills, key caps, buttons, the switch, the status dot.

import CardputerCore
import SwiftUI

/// A solid card with a lighter 1 pt top edge (docs/19: cards must not vanish on the dark ground).
struct CardBox<Content: View>: View {
    var padding: CGFloat = Theme.inset
    @ViewBuilder let content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.grid) {
            content
        }
        .padding(padding)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Theme.card, in: RoundedRectangle(cornerRadius: Theme.radius))
        .overlay(alignment: .top) {
            RoundedRectangle(cornerRadius: Theme.radius).stroke(Theme.cardRim.opacity(0.7), lineWidth: 1)
                .mask(alignment: .top) { Rectangle().frame(height: 3) }
        }
    }
}

struct Pill: View {
    let text: String
    var tone: Tone = .neutral
    var symbol: String?

    var body: some View {
        HStack(spacing: 4) {
            if let symbol {
                Image(systemName: symbol).font(.system(size: 10, weight: .semibold))
            }
            Text(text).font(Theme.smallStrong).lineLimit(1)
        }
        .foregroundStyle(tone.foreground)
        .padding(.horizontal, 7)
        .padding(.vertical, 3)
        .background(tone.background, in: Capsule())
        .fixedSize()
    }
}

/// A key cap: what to press ("3", "⌘Z", "Space").
struct KeyCap: View {
    let text: String
    var highlighted = false

    var body: some View {
        Text(text)
            .font(.system(size: 11, weight: .semibold, design: .rounded))
            .foregroundStyle(highlighted ? Theme.onAccent : Theme.ink2)
            .padding(.horizontal, 5)
            .frame(minWidth: 18, minHeight: 18)
            .background(highlighted ? Theme.accent2 : Theme.cap, in: RoundedRectangle(cornerRadius: 4))
            .fixedSize()
    }
}

enum ButtonKind {
    case primary, secondary, quiet, danger
}

/// Buttons drawn in the theme; grey and still readable when disabled.
struct ThemedButtonStyle: ButtonStyle {
    var kind: ButtonKind = .secondary
    var selected = false
    var compact = false
    @Environment(\.isEnabled) private var enabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(compact ? Theme.smallStrong : Theme.bodyStrong)
            .lineLimit(1)
            .foregroundStyle(foreground)
            .padding(.horizontal, compact ? 8 : 12)
            .padding(.vertical, compact ? 4 : 7)
            .background(background(pressed: configuration.isPressed), in: RoundedRectangle(cornerRadius: 7))
            .overlay(RoundedRectangle(cornerRadius: 7).stroke(selected ? Theme.accent : .clear, lineWidth: 1.5))
            .opacity(enabled ? 1 : 0.45)
            .contentShape(Rectangle())
    }

    var foreground: Color {
        switch kind {
        case .primary: return Theme.onAccent
        case .danger: return Theme.error
        case .secondary: return Theme.fg
        case .quiet: return Theme.ink2
        }
    }

    func background(pressed: Bool) -> Color {
        let base: Color
        switch kind {
        case .primary: base = Theme.accent
        case .secondary, .danger: base = selected ? Theme.select : Theme.cap
        case .quiet: base = .clear
        }
        return pressed ? base.opacity(0.7) : base
    }
}

/// A filter chip: on (selected) or off.
struct Chip: View {
    let text: String
    let on: Bool
    var badge: String?
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 4) {
                Text(text)
                if let badge {
                    Text(badge).foregroundStyle(on ? Theme.onAccent.opacity(0.75) : Theme.dim)
                }
            }
            .font(Theme.small.weight(on ? .semibold : .regular))
            .foregroundStyle(on ? Theme.onAccent : Theme.ink2)
            .padding(.horizontal, 9)
            .padding(.vertical, 4)
            .background(on ? Theme.accent : Theme.raised, in: Capsule())
        }
        .buttonStyle(.plain)
        .fixedSize()
    }
}

/// The on / off switch, drawn (the system switch is not drawn by ImageRenderer).
struct ThemedSwitchStyle: ToggleStyle {
    @Environment(\.isEnabled) private var enabled

    func makeBody(configuration: Configuration) -> some View {
        Button { configuration.isOn.toggle() } label: {
            HStack(spacing: Theme.grid) {
                configuration.label
                Spacer(minLength: Theme.grid)
                ZStack(alignment: configuration.isOn ? .trailing : .leading) {
                    Capsule().fill(configuration.isOn ? Theme.ok : Theme.line).frame(width: 40, height: 22)
                    Circle().fill(enabled ? Theme.fg : Theme.dim).frame(width: 18, height: 18).padding(2)
                }
                .opacity(enabled ? 1 : 0.5)
            }
        }
        .buttonStyle(.plain)
    }
}

/// The status dot; a ring when cpa status has not answered for a while (not sure).
struct StatusDot: View {
    let light: StatusLight
    var size: CGFloat = 9

    var body: some View {
        if light == .unknown {
            Circle().stroke(light.color, lineWidth: 1.5).frame(width: size - 1, height: size - 1).frame(width: size, height: size)
        } else {
            Circle().fill(light.color).frame(width: size, height: size)
        }
    }
}

/// A segmented choice drawn by SwiftUI itself (so it renders the same in the window and in CardputerShots).
struct Segmented<Value: Hashable>: View {
    let items: [(value: Value, title: String, badge: String?)]
    let selected: Value
    let pick: (Value) -> Void

    var body: some View {
        HStack(spacing: 2) {
            ForEach(Array(items.enumerated()), id: \.offset) { _, item in
                Button { pick(item.value) } label: { segment(item.title, badge: item.badge, on: item.value == selected) }
                    .buttonStyle(.plain)
            }
        }
        .padding(2)
        .background(Theme.raised, in: Capsule())
        .fixedSize()
    }

    func segment(_ title: String, badge: String?, on: Bool) -> some View {
        HStack(spacing: 4) {
            Text(title)
            if let badge {
                Text(badge).foregroundStyle(on ? Theme.onAccent.opacity(0.75) : Theme.dim)
            }
        }
        .font(Theme.small.weight(on ? .semibold : .regular))
        .foregroundStyle(on ? Theme.onAccent : Theme.ink2)
        .padding(.horizontal, 10)
        .padding(.vertical, 4)
        .background(on ? Theme.accent : .clear, in: Capsule())
        .contentShape(Capsule())
    }
}

/// A filter as a menu: "Date ▾" (or what is chosen) on a chip; the choices in a native menu. In a snapshot
/// the chip alone.
struct FilterMenu<Items: View>: View {
    @Environment(\.isSnapshot) private var snapshot
    let title: String
    let on: Bool
    @ViewBuilder let items: Items

    var body: some View {
        if snapshot {
            chip
        } else {
            Menu { items } label: { chip }
                .menuStyle(.button)
                .buttonStyle(.plain)
                .menuIndicator(.hidden)
                .fixedSize()
        }
    }

    var chip: some View {
        HStack(spacing: 3) {
            Text(title)
            Image(systemName: "chevron.down").font(.system(size: 8, weight: .bold))
        }
        .font(Theme.small.weight(on ? .semibold : .regular))
        .foregroundStyle(on ? Theme.onAccent : Theme.ink2)
        .padding(.horizontal, 9)
        .padding(.vertical, 4)
        .background(on ? Theme.accent : Theme.raised, in: Capsule())
        .fixedSize()
    }
}

/// A grey line under a control saying why it is grey.
struct Reason: View {
    let text: String

    var body: some View {
        Label(text, systemImage: "info.circle")
            .font(Theme.small)
            .foregroundStyle(Theme.dim)
            .labelStyle(.titleAndIcon)
    }
}

struct SectionLabel: View {
    let text: String

    var body: some View {
        Text(text).font(Theme.smallStrong).foregroundStyle(Theme.dim).textCase(nil)
    }
}

/// Lays its children out left to right and wraps to a new line when the width runs out.
struct FlowLayout: Layout {
    var spacing: CGFloat = 6
    var lineSpacing: CGFloat = 8

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let width = proposal.width ?? .infinity
        let frames = place(subviews, width: width)
        let height = frames.map(\.maxY).max() ?? 0
        let used = frames.map(\.maxX).max() ?? 0
        return CGSize(width: proposal.width ?? used, height: height)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        let frames = place(subviews, width: bounds.width)
        for (subview, frame) in zip(subviews, frames) {
            subview.place(at: CGPoint(x: bounds.minX + frame.minX, y: bounds.minY + frame.minY),
                          proposal: ProposedViewSize(frame.size))
        }
    }

    /// Each child's frame: on the current line if it fits, else at the start of the next.
    func place(_ subviews: Subviews, width: CGFloat) -> [CGRect] {
        let sizes = subviews.map { $0.sizeThatFits(.unspecified) }
        return sizes.reduce(into: (frames: [CGRect](), x: CGFloat(0), y: CGFloat(0), line: CGFloat(0))) { state, size in
            let wraps = state.x > 0 && state.x + size.width > width
            let x = wraps ? 0 : state.x
            let y = wraps ? state.y + state.line + lineSpacing : state.y
            state.frames.append(CGRect(origin: CGPoint(x: x, y: y), size: size))
            state.line = wraps ? size.height : max(state.line, size.height)
            state.x = x + size.width + spacing
            state.y = y
        }.frames
    }
}
