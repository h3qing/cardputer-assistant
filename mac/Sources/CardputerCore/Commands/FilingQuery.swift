// The inbox filters (docs/24 §2.1, docs/23 §5.1), all buttons, mapped one to one onto `cpa filing ls`
// arguments: the app never decides filing states itself.

import Foundation

/// The state buttons: Inbox · Other Mac's projects · Handed to AI · Done · No words · All.
public enum InboxScope: String, CaseIterable, Hashable, Sendable {
    case pending, otherMac, handed, done, noText, all
}

/// The date buttons: All · Today · This week · Last week (the recording folder's date; weeks start on the
/// calendar's first weekday).
public enum DateRange: String, CaseIterable, Hashable, Sendable {
    case all, today, thisWeek, lastWeek
}

public struct FilingQuery: Hashable, Sendable {
    public let scope: InboxScope
    public let dates: DateRange
    /// This Mac's project keys, any of them; empty for any project.
    public let projects: [String]
    /// Only recordings without a project (--none); not with `projects`.
    public let withoutProject: Bool
    public let mode: RecordingKind?
    /// A row's 12-hex device id.
    public let device: String?
    /// Recorded while linked to this Mac (here) or not known to be (unknown).
    public let link: MacLink?
    /// Search words (literal, any case), 1-200 characters, sent through stdin.
    public let text: String?
    public let limit: Int?
    /// The previous page's next_cursor, with the same filters.
    public let cursor: String?

    public init(scope: InboxScope = .pending, dates: DateRange = .all, projects: [String] = [],
                withoutProject: Bool = false, mode: RecordingKind? = nil, device: String? = nil,
                link: MacLink? = nil, text: String? = nil, limit: Int? = nil, cursor: String? = nil) {
        self.scope = scope
        self.dates = dates
        self.projects = projects
        self.withoutProject = withoutProject
        self.mode = mode
        self.device = device
        self.link = link
        self.text = text
        self.limit = limit
        self.cursor = cursor
    }

    /// `cpa filing ls` can only add the other-Mac group (--orphans), so its scope keeps those rows alone.
    public func keeps(_ row: FilingRow) -> Bool {
        scope != .otherMac || row.isOtherMac
    }

    /// The search words as sent, or nil when there are none.
    var searchWords: String? {
        let words = text?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        return words.isEmpty ? nil : words
    }

    func scopeArguments() -> [String] {
        switch scope {
        case .pending: return ["--filing", "new"]
        case .otherMac: return ["--filing", "new", "--orphans"]
        case .handed: return ["--filing", "handed"]
        case .done: return ["--filing", "done"]
        case .noText: return ["--filing", "all", "--tx", "failed", "--tx", "stale"]
        case .all: return ["--filing", "all"]
        }
    }

    func filterArguments() throws -> [String] {
        if let device, !Patterns.isDeviceID(device) {
            throw CommandError.invalid(argument: "device")
        }
        if withoutProject, !projects.isEmpty {
            throw CommandError.invalid(argument: "none")
        }
        let keys = try projects.flatMap { ["--project", try CpaCommand.checkedKey($0)] }
        return (device.map { ["--device", $0] } ?? [])
            + (link.map { ["--link", $0.rawValue] } ?? [])
            + (mode.map { ["--mode", $0.rawValue] } ?? [])
            + keys + (withoutProject ? ["--none"] : [])
    }

    func pageArguments() throws -> [String] {
        if let limit, !(1...1000).contains(limit) {
            throw CommandError.invalid(argument: "limit")
        }
        if let cursor, !Patterns.isCursor(cursor) {
            throw CommandError.invalid(argument: "cursor")
        }
        if let words = searchWords, words.unicodeScalars.count > 200 {
            throw CommandError.invalid(argument: "text")
        }
        return (limit.map { ["--limit", String($0)] } ?? []) + (cursor.map { ["--cursor", $0] } ?? [])
            + (searchWords == nil ? [] : ["--text", "-"])
    }

    /// --from / --to for the date buttons, as YYYY-MM-DD in the calendar's time zone.
    func dateArguments(now: Date, calendar: Calendar) -> [String] {
        guard let (from, to) = dayRange(now: now, calendar: calendar) else { return [] }
        return ["--from", Self.day(from, calendar), "--to", Self.day(to, calendar)]
    }

    func dayRange(now: Date, calendar: Calendar) -> (Date, Date)? {
        guard let week = calendar.dateInterval(of: .weekOfYear, for: now) else { return nil }
        switch dates {
        case .all:
            return nil
        case .today:
            return (now, now)
        case .thisWeek:
            return (week.start, now)
        case .lastWeek:
            let lastDay = week.start.addingTimeInterval(-1)
            return calendar.dateInterval(of: .weekOfYear, for: lastDay).map { ($0.start, lastDay) }
        }
    }

    static func day(_ date: Date, _ calendar: Calendar) -> String {
        let parts = calendar.dateComponents([.year, .month, .day], from: date)
        return String(format: "%04d-%02d-%02d", parts.year ?? 0, parts.month ?? 0, parts.day ?? 0)
    }
}

extension CpaCommand {
    /// `cpa filing ls --json` with these filters; the search words, if any, are the stdin.
    public static func filingList(_ query: FilingQuery, now: Date, calendar: Calendar) throws -> CpaCommand {
        let arguments = ["filing", "ls", "--json"] + query.scopeArguments() + (try query.filterArguments())
            + query.dateArguments(now: now, calendar: calendar) + (try query.pageArguments())
        return CpaCommand(.cpa, arguments, stdin: query.searchWords.map { Data($0.utf8) })
    }
}
