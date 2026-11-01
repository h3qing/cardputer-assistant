// Times and durations as the app shows them, in Chinese. Pure: the clock and calendar come in.

import Foundation

public enum Format {
    /// Like `cpa rec ls`: "51s" and "4m59s" under 10 minutes (memos are short), whole minutes above; "-" unknown.
    public static func duration(ms: Int?) -> String {
        guard let ms else { return "-" }
        let seconds = (ms + 500) / 1000
        if seconds < 60 {
            return "\(seconds)s"
        }
        if seconds < 600 {
            return "\(seconds / 60)m" + String(format: "%02ds", seconds % 60)
        }
        return "\((ms + 30_000) / 60_000)m"
    }

    /// just now · N min ago · N h ago (same day) · Yesterday HH:mm · N days ago (a week) · month and day ·
    /// month, day and year.
    public static func relative(_ date: Date, now: Date, calendar: Calendar = .current) -> String {
        let seconds = now.timeIntervalSince(date)
        if seconds < 60 {
            return tr("刚刚")
        }
        if seconds < 3600 {
            return tr("{0} 分钟前", Int(seconds / 60))
        }
        if calendar.isDate(date, inSameDayAs: now) {
            return tr("{0} 小时前", Int(seconds / 3600))
        }
        let days = dayDistance(from: date, to: now, calendar: calendar)
        if days == 1 {
            return tr("昨天 ") + hourMinute(date, calendar: calendar)
        }
        if days < 7 {
            return tr("{0} 天前", days)
        }
        return longDate(date, now: now, calendar: calendar)
    }

    /// HH:mm at the recording's own UTC offset (tz_min), else the calendar's; "~" when the device was unsure.
    public static func clock(wallMs: Int64?, tzMin: Int?, uncertain: Bool, calendar: Calendar = .current) -> String {
        guard let wallMs else { return tr("时间未知") }
        let zone = tzMin.flatMap { TimeZone(secondsFromGMT: $0 * 60) } ?? calendar.timeZone
        let date = Date(timeIntervalSince1970: TimeInterval(wallMs) / 1000)
        return (uncertain ? "~" : "") + hourMinute(date, calendar: calendar.with(zone))
    }

    /// A day header: Today · Yesterday · the month and day (and the year in another year).
    public static func day(_ date: Date, now: Date, calendar: Calendar = .current) -> String {
        switch dayDistance(from: date, to: now, calendar: calendar) {
        case 0: return tr("今天")
        case 1: return tr("昨天")
        default: return longDate(date, now: now, calendar: calendar)
        }
    }

    static func dayDistance(from date: Date, to now: Date, calendar: Calendar) -> Int {
        let start = calendar.startOfDay(for: date)
        let end = calendar.startOfDay(for: now)
        return calendar.dateComponents([.day], from: start, to: end).day ?? 0
    }

    static func hourMinute(_ date: Date, calendar: Calendar) -> String {
        let parts = calendar.dateComponents([.hour, .minute], from: date)
        return String(format: "%02d:%02d", parts.hour ?? 0, parts.minute ?? 0)
    }

    /// English month names: "Sep 16", never 9/16 (which reads as a fraction or a score).
    static let monthNames = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]

    static func longDate(_ date: Date, now: Date, calendar: Calendar) -> String {
        let parts = calendar.dateComponents([.year, .month, .day], from: date)
        let month = parts.month ?? 0
        let monthDay = L10n.language == .en && (1...12).contains(month)
            ? "\(monthNames[month - 1]) \(parts.day ?? 0)" : tr("{0}月{1}日", month, parts.day ?? 0)
        let sameYear = parts.year == calendar.component(.year, from: now)
        return sameYear ? monthDay : tr("{0}年{1}", parts.year ?? 0, monthDay)
    }
}

extension Calendar {
    /// A copy of this calendar in another time zone.
    func with(_ zone: TimeZone) -> Calendar {
        var copy = self
        copy.timeZone = zone
        return copy
    }
}
