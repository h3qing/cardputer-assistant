// The Mando companion colour tokens (docs/19 §2.1), the same values the device draws with: a test compares
// every one with tools/theme/tokens.py. The Mac app's Theme reads its colours from here.

public enum ThemeTokens {
    public static let bg: UInt32 = 0x120E0B
    public static let fg: UInt32 = 0xF3EDE6
    public static let dim: UInt32 = 0x96897B
    public static let accent: UInt32 = 0xE2A867
    public static let ok: UInt32 = 0x72C291
    public static let warn: UInt32 = 0xDCC15A
    public static let error: UInt32 = 0xF4675B
    public static let select: UInt32 = 0x463525
    public static let sheet: UInt32 = 0x1A1512
    public static let card: UInt32 = 0x221C18
    public static let raised: UInt32 = 0x2C241E
    public static let line: UInt32 = 0x3A2F27
    public static let cap: UInt32 = 0x43372D
    public static let rim: UInt32 = 0x5A4E44
    public static let cardRim: UInt32 = 0x4A3E35
    public static let ink2: UInt32 = 0xBFB2A6
    public static let accent2: UInt32 = 0xC48A4C
    public static let onAccent: UInt32 = 0x1D130B
    public static let live: UInt32 = 0x926D45
    public static let okBg: UInt32 = 0x272F25
    public static let warnBg: UInt32 = 0x372F1D
    public static let recBg: UInt32 = 0x3A2520
    public static let infoBg: UInt32 = 0x36291C

    /// By the names of tools/theme/tokens.py.
    public static let all: [String: UInt32] = [
        "Bg": bg, "Fg": fg, "Dim": dim, "Accent": accent, "Ok": ok, "Warn": warn, "Error": error, "Select": select,
        "Sheet": sheet, "Card": card, "Raised": raised, "Line": line, "Cap": cap, "Rim": rim, "CardRim": cardRim,
        "Ink2": ink2, "Accent2": accent2, "OnAccent": onAccent, "Live": live, "OkBg": okBg, "WarnBg": warnBg,
        "RecBg": recBg, "InfoBg": infoBg,
    ]
}
