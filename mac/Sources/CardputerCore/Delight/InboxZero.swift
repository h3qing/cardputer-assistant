// Inbox zero: when the last waiting recording is filed, Mando's dot becomes a sparkle for a moment and the
// empty Inbox shows today's recap. Quiet, local, at most once a day. Pure: the clock and calendar come in.

import Foundation

/// Today's voice memos, from `cpa rec ls --json`: how many, their minutes, the projects they went to, the ★.
public struct TodayRecap: Hashable, Sendable {
    /// The day it counts (InboxZero.day): a recap of another day is not shown as today's.
    public let day: String
    public let memos: Int
    public let minutes: Int
    public let projects: Int
    public let stars: Int

    public init(day: String, memos: Int, minutes: Int, projects: Int, stars: Int) {
        self.day = day
        self.memos = memos
        self.minutes = minutes
        self.projects = projects
        self.stars = stars
    }

    /// Something to show: at least one memo today.
    public var anything: Bool {
        memos > 0
    }

    /// Memos (not meetings) started today by a sure clock, not hidden; minutes rounded, a short one counts as 1.
    public static func make(_ rows: [RecRow], now: Date, calendar: Calendar) -> TodayRecap {
        let today = rows.filter { row in
            guard row.mode?.known == .memo, !row.uncertain, !row.hidden, let started = row.startedMs else { return false }
            return calendar.isDate(Date(timeIntervalSince1970: TimeInterval(started) / 1000), inSameDayAs: now)
        }
        let ms = today.reduce(0) { $0 + max($1.durMs ?? 0, 0) }
        let minutes = ms > 0 ? max((ms + 30_000) / 60_000, 1) : 0
        let projects = Set(today.compactMap { $0.project }.filter { !$0.isEmpty }).count
        return TodayRecap(day: InboxZero.day(now, calendar: calendar), memos: today.count, minutes: minutes,
                          projects: projects, stars: today.reduce(0) { $0 + $1.marks })
    }
}

public enum InboxZero {
    /// How long the menu bar sparkle stays.
    public static let sparkleFor: TimeInterval = 3

    /// The day as yyyy-MM-dd in `calendar`: the key of "already celebrated today".
    public static func day(_ date: Date, calendar: Calendar) -> String {
        let parts = calendar.dateComponents([.year, .month, .day], from: date)
        return String(format: "%04d-%02d-%02d", parts.year ?? 0, parts.month ?? 0, parts.day ?? 0)
    }

    /// From some waiting to none (both known), and not yet celebrated today.
    public static func celebrates(from old: Int?, to new: Int?, lastDay: String?, today: String) -> Bool {
        guard let old, let new else { return false }
        return old > 0 && new == 0 && lastDay != today
    }
}
