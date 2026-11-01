// The numbers of the Device page (docs/24 §2.3), from `cpa status --json` and the newest manifest's
// written_at. Never the device's own ids (serial, dev: its factory MAC address; boot). While the device is
// not linked, the count on it is what it had when last seen.

import Foundation

public struct Fact: Hashable, Sendable {
    public enum Kind: Hashable, Sendable {
        case plain
        /// Failed transcriptions: the tile offers Retry the failed ones.
        case failed
    }

    public let label: String
    public let value: String
    public let kind: Kind

    public init(label: String, value: String, kind: Kind = .plain) {
        self.label = label
        self.value = value
        self.kind = kind
    }
}

public enum DeviceFacts {
    public static func facts(status: CpaStatus?, lastPull: String?, now: Date, calendar: Calendar) -> [Fact] {
        let counts = status?.recordings
        let onDevice = counts?.onDevice.map { tr("{0} 段", $0) } ?? tr("—（cpad 没在跑）")
        let when = { (text: String?) in
            text.flatMap(Format.instant).map { Format.relative($0, now: now, calendar: calendar) } ?? "—"
        }
        let rows: [(String, String?, Fact.Kind)] = [
            (onDeviceLabel(status), onDevice, .plain),
            (tr("等转写"), counts?.waiting.map { tr("{0} 段", $0) }, .plain),
            (tr("转写失败"), counts?.failed.map { tr("{0} 段", $0) }, .failed),
            (tr("待发给设备"), status?.device?.pending.map { tr("{0} 条", $0) }, .plain),
            (tr("最近取回"), when(lastPull), .plain),
            (tr("最近转写"), when(counts?.lastResultAt), .plain),
            (tr("存储"), status?.device?.store.map(store), .plain),
            (tr("cpad 版本"), status?.version.flatMap { DisplaySafe.excerpt($0, limit: 24) }, .plain),
        ]
        return rows.compactMap { label, value, kind in value.map { Fact(label: label, value: $0, kind: kind) } }
    }

    /// Still on the device while linked; Last seen on the device while not.
    static func onDeviceLabel(_ status: CpaStatus?) -> String {
        status?.running == true && status?.link?.known == .ready ? tr("设备上还有") : tr("上次看到设备上有")
    }

    /// "2 still on the device" (or "2 on the device when last seen"); nil when there is nothing on it or cpad
    /// does not say.
    public static func onDevice(_ status: CpaStatus) -> String? {
        guard let count = status.recordings?.onDevice, count > 0 else { return nil }
        let linked = status.running == true && status.link?.known == .ready
        return linked ? tr("设备上还有 {0} 段", count) : tr("上次看到设备上有 {0} 段", count)
    }

    static func store(_ value: String) -> String {
        switch value {
        case "sd": return tr("SD 卡")
        case "flash", "nvs": return tr("设备内存")
        default: return DisplaySafe.excerpt(value, limit: 16) ?? Labels.unknown
        }
    }
}
