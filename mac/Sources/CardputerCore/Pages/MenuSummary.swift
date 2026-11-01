// The menu bar item (docs/24 §3): a dot (green linked, grey not linked, red cpad down or cpa failing, grey
// "not sure" when cpa status has not answered for a while), the number waiting to be filed, a first line in
// words (never colour alone) and the five newest new entries.

import Foundation

public struct MenuSummary: Hashable, Sendable {
    public let light: StatusLight
    /// New entries to file, for the menu bar; nil shows no number (none, or the read-only list).
    public let count: Int?
    public let headline: String
    public let recent: [InboxEntry]

    /// `fresh`: cpa status answered without error in the last 90 s; otherwise the dot says it is not sure.
    public static func make(status: CpaStatus?, page: InboxPage?, cpaFound: Bool, fresh: Bool) -> MenuSummary {
        let sure = cpaFound && (fresh || status == nil)
        let light = cpaFound ? (sure ? StatusLight.of(status) : .unknown) : .down
        let headline = sure || !cpaFound ? headline(status, cpaFound: cpaFound) : tr("不确定：cpa status 一阵子没回答了")
        return MenuSummary(light: light, count: count(page), headline: headline, recent: recent(page))
    }

    public static let freshFor: TimeInterval = 90

    static func count(_ page: InboxPage?) -> Int? {
        guard case .filing(let counts)? = page?.source else { return nil }
        return counts.waiting > 0 ? counts.waiting : nil
    }

    static func headline(_ status: CpaStatus?, cpaFound: Bool) -> String {
        guard cpaFound else { return tr("没找到 cpa") }
        guard let status else { return tr("读不到 cpa status") }
        return Labels.deviceShort(status) + (DeviceFacts.onDevice(status).map { " · " + $0 } ?? "")
    }

    static func recent(_ page: InboxPage?) -> [InboxEntry] {
        guard case .filing? = page?.source else { return [] }
        return Array(page?.entries.filter { $0.filing == .new }.prefix(5) ?? [])
    }
}
