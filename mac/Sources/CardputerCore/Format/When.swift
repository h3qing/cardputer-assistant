// A row's time as one line ("3 min ago · 14:27", "~" when the device was unsure), the contracts' ISO times,
// and the words of the filter buttons and the row's Mac.

import Foundation

extension Format {
    public static func when(_ at: RecordedAt, now: Date, calendar: Calendar = .current) -> String {
        guard let wallMs = at.wallMs else { return tr("时间未知") }
        let date = Date(timeIntervalSince1970: TimeInterval(wallMs) / 1000)
        let clock = clock(wallMs: wallMs, tzMin: at.tzMin, uncertain: at.uncertain, calendar: calendar)
        let said = relative(date, now: now, calendar: calendar)
        // Yesterday HH:mm already says the clock.
        return said.hasPrefix(tr("昨天")) && !at.uncertain ? said : said + " · " + clock
    }

    /// An ISO 8601 time with an offset, with or without fractional seconds.
    public static func instant(_ text: String) -> Date? {
        let fractional = ISO8601DateFormatter()
        fractional.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return fractional.date(from: text) ?? ISO8601DateFormatter().date(from: text)
    }
}

extension Labels {
    public static func scope(_ scope: InboxScope) -> String {
        switch scope {
        case .pending: return tr("待归档")
        case .otherMac: return tr("别的电脑的项目")
        case .handed: return tr("已交给 AI")
        case .done: return tr("看过了")
        case .noText: return tr("没转出文字")
        case .all: return tr("全部")
        }
    }

    public static func dates(_ range: DateRange) -> String {
        switch range {
        case .all: return tr("全部日期")
        case .today: return tr("今天")
        case .thisWeek: return tr("本周")
        case .lastWeek: return tr("上周")
        }
    }

    /// The Mac that pulled it (env.host): This Mac only when the name is this Mac's (`thisMac`, from cpa host
    /// name); a stamp without a name is Unnamed Mac (any Mac); nil before the stamp existed.
    public static func mac(_ env: MacStamp?, thisMac: String?) -> String? {
        guard let env else { return nil }
        guard let host = DisplaySafe.excerpt(env.host, limit: 24) else { return tr("没起名的 Mac") }
        return host == thisMac ? tr("这台 Mac") : host
    }

    /// The Mac and the link in one line: Office pulled · linked while recording.
    public static func pulled(_ env: MacStamp?, thisMac: String?) -> String? {
        guard let mac = mac(env, thisMac: thisMac) else { return nil }
        switch env?.link?.known {
        case .here: return mac + tr(" 取回 · 录音时连着它")
        case .unknown: return mac + tr(" 取回 · 录音时没连")
        case nil: return mac + tr(" 取回")
        }
    }

    /// A recording without words: Memo at 13:25, Meeting at ~09:05, Recording, time unknown.
    public static func untitled(kind: Open<RecordingKind>?, at: RecordedAt, calendar: Calendar) -> String {
        let what = kind.map(Labels.kind).map { $0 == unknown ? tr("录音") : $0 } ?? tr("录音")
        let clock = Format.clock(wallMs: at.wallMs, tzMin: at.tzMin, uncertain: at.uncertain, calendar: calendar)
        return at.wallMs == nil ? tr("时间未知的{0}", what) : tr("{0} 的{1}", clock, what)
    }
}
