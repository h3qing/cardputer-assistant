// The app speaks English or Chinese (docs/24 §2.7). Every UI string is written in Chinese in the code as
// tr("…{0}…", value): the Chinese text is the key and the Chinese table; EnglishTable holds the English one.
// {0}, {1}, … stand for the values in order. The language follows the Mac's preferred languages (Chinese when
// the first one is Chinese, else English) unless Settings picked one; each test suite sets it.

import Foundation

public enum Language: String, CaseIterable, Hashable, Sendable {
    case zh, en

    /// The first preferred language decides: zh-Hans, zh-Hant, zh-CN … are Chinese, anything else English.
    public static func preferred(_ languages: [String] = Locale.preferredLanguages) -> Language {
        languages.first.map { $0.lowercased().hasPrefix("zh") } == true ? .zh : .en
    }
}

/// Settings' choice: follow the Mac, or one language.
public enum LanguageChoice: String, CaseIterable, Hashable, Sendable {
    case system, zh, en

    public func resolved(_ languages: [String] = Locale.preferredLanguages) -> Language {
        switch self {
        case .system: return Language.preferred(languages)
        case .zh: return .zh
        case .en: return .en
        }
    }

    /// The button in Settings: Follow the Mac says which language it picked now; the others are named in
    /// their own language.
    public func title(_ languages: [String] = Locale.preferredLanguages) -> String {
        switch self {
        case .system: return tr("跟随 Mac（现在：{0}）", resolved(languages) == .zh ? tr("中文") : "English")
        case .zh: return tr("中文")
        case .en: return "English"
        }
    }
}

/// The commands the app shows for installing or updating cpa: one place, so every hint says the same.
public enum Setup {
    /// Run in the Cardputer Assistant checkout; the same command Settings and the Inbox show.
    public static let updateCpa = "uv tool install --force ./daemon"
}

public enum L10n {
    /// Set once at start and from Settings; read everywhere. The app redraws its windows after a change.
    nonisolated(unsafe) public static var language: Language = .preferred()

    /// Every key and its English text (for the tests that check the table is whole).
    public static var englishTable: [String: String] {
        EnglishTable.strings
    }

    /// The text of `key` in `language`, with {n} replaced by the n-th value.
    public static func text(_ key: String, _ args: [Any], in language: Language) -> String {
        let pattern = language == .en ? (EnglishTable.strings[key] ?? key) : key
        return args.enumerated().reduce(pattern) { text, item in
            text.replacingOccurrences(of: "{\(item.offset)}", with: String(describing: item.element))
        }
    }
}

/// A UI string in the current language (see L10n).
public func tr(_ key: String, _ args: Any...) -> String {
    L10n.text(key, args, in: L10n.language)
}

/// A UI string in `language`: for a place that must redraw when the language changes (the app's menus).
public func tr(in language: Language, _ key: String, _ args: Any...) -> String {
    L10n.text(key, args, in: language)
}
