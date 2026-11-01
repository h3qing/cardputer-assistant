// The Mando companion look on the Mac (docs/24 §6, docs/19 §2.1): dark, warm, one accent (caramel) for the
// main button, focus and the current project; system fonts (SF, PingFang); an 8 pt grid, cards with a 16 pt
// inset and a 10 pt radius. Colours come from ThemeTokens, which a test keeps equal to the device's.

import CardputerCore
import SwiftUI

extension Color {
    init(rgb: UInt32) {
        self.init(.sRGB, red: Double((rgb >> 16) & 0xFF) / 255, green: Double((rgb >> 8) & 0xFF) / 255,
                  blue: Double(rgb & 0xFF) / 255, opacity: 1)
    }
}

public enum Theme {
    static let bg = Color(rgb: ThemeTokens.bg)
    static let sidebar = Color(rgb: ThemeTokens.sheet)
    static let card = Color(rgb: ThemeTokens.card)
    static let raised = Color(rgb: ThemeTokens.raised)
    static let line = Color(rgb: ThemeTokens.line)
    static let cap = Color(rgb: ThemeTokens.cap)
    static let rim = Color(rgb: ThemeTokens.rim)
    static let cardRim = Color(rgb: ThemeTokens.cardRim)
    static let select = Color(rgb: ThemeTokens.select)
    static let fg = Color(rgb: ThemeTokens.fg)
    static let ink2 = Color(rgb: ThemeTokens.ink2)
    static let dim = Color(rgb: ThemeTokens.dim)
    static let accent = Color(rgb: ThemeTokens.accent)
    static let accent2 = Color(rgb: ThemeTokens.accent2)
    static let onAccent = Color(rgb: ThemeTokens.onAccent)
    static let live = Color(rgb: ThemeTokens.live)
    static let ok = Color(rgb: ThemeTokens.ok)
    static let warn = Color(rgb: ThemeTokens.warn)
    static let error = Color(rgb: ThemeTokens.error)
    static let okBg = Color(rgb: ThemeTokens.okBg)
    static let warnBg = Color(rgb: ThemeTokens.warnBg)
    static let recBg = Color(rgb: ThemeTokens.recBg)
    static let infoBg = Color(rgb: ThemeTokens.infoBg)

    static let body = Font.system(size: 13)
    static let bodyStrong = Font.system(size: 13, weight: .semibold)
    static let small = Font.system(size: 11)
    static let smallStrong = Font.system(size: 11, weight: .semibold)
    static let title = Font.system(size: 17, weight: .semibold)
    static let pageTitle = Font.system(size: 22, weight: .bold)
    static let big = Font.system(size: 15, weight: .medium)
    static let mono = Font.system(size: 12, design: .monospaced)
    static let monoSmall = Font.system(size: 11, design: .monospaced)

    static let grid: CGFloat = 8
    static let inset: CGFloat = 16
    static let radius: CGFloat = 10
    static let smallRadius: CGFloat = 6

    public static let windowSize = CGSize(width: 1200, height: 800)
    /// A page beside the sidebar in a window of windowSize.
    public static let pageWidth: CGFloat = 987
    public static let minimumWindow = CGSize(width: 960, height: 600)
    static let sidebarWidth: CGFloat = 212
    static let listWidth: CGFloat = 380
    static let menuWidth: CGFloat = 340
}

/// The four tones of a pill or a line: words always come with them, never colour alone.
enum Tone: Hashable {
    case neutral, ok, warn, error, info

    var foreground: Color {
        switch self {
        case .neutral: return Theme.ink2
        case .ok: return Theme.ok
        case .warn: return Theme.warn
        case .error: return Theme.error
        case .info: return Theme.accent
        }
    }

    var background: Color {
        switch self {
        case .neutral: return Theme.raised
        case .ok: return Theme.okBg
        case .warn: return Theme.warnBg
        case .error: return Theme.recBg
        case .info: return Theme.infoBg
        }
    }
}

extension Tone {
    static func of(_ tx: Open<Transcription>) -> Tone {
        switch tx.known {
        case .done, .transcript: return .ok
        case .pulling, .waiting, .processing: return .neutral
        case .blocked, .stale: return .warn
        case .failed: return .error
        case nil: return .neutral
        }
    }

    static func symbol(_ tx: Open<Transcription>) -> String {
        switch tx.known {
        case .done: return "text.badge.checkmark"
        case .transcript: return "text.alignleft"
        case .pulling: return "arrow.down.circle"
        case .waiting: return "hourglass"
        case .processing: return "waveform"
        case .blocked: return "pause.circle"
        case .stale: return "clock.badge.exclamationmark"
        case .failed: return "exclamationmark.triangle"
        case nil: return "questionmark.circle"
        }
    }
}

extension StatusLight {
    var color: Color {
        switch self {
        case .linked: return Theme.ok
        case .unlinked, .unknown: return Theme.dim
        case .down: return Theme.error
        }
    }
}
