import CardputerCore
import Foundation

private func status(_ name: String) throws -> CpaStatus {
    try CpaJSON.status(try Fixture.data("cpa/\(name).json"))
}

private func edited(_ name: String, _ changes: [String: Any]) throws -> CpaStatus {
    let object = try JSONSerialization.jsonObject(with: try Fixture.data("cpa/\(name).json"))
    let fields = (object as? [String: Any] ?? [:]).merging(changes) { _, new in new }
    return try CpaJSON.status(try JSONSerialization.data(withJSONObject: fields))
}

let statusTests = Suite(name: "Status", language: .zh, cases: [
    test("cpa status --json of a running cpad") {
        let doc = try status("status-running")
        return [
            expect(doc.running),
            expectEqual(doc.version, "0.1.0"),
            expectEqual(doc.link?.known, .ready),
            expectEqual(doc.device?.fw, "cpa-fw/0.4.0"),
            expectEqual(doc.device?.pending, 0),
            expectEqual(doc.projects, [StatusProject(key: "acmeshop", slot: 1, ok: true),
                                       StatusProject(key: "cardputer", slot: 2, ok: true)]),
            expectEqual(doc.recordings?.waiting, 19),
            expectEqual(doc.recordings?.onDevice, 0),
            expectEqual(doc.queueOpen, 2),
            expectEqual(doc.warnings, []),
            expectEqual(doc.notesDir, nil),
        ]
    },
    test("the device's own ids (serial, dev, boot) are never kept") {
        let shown = String(reflecting: try status("status-running"))
        return ["02:00:5e:10:00:01", "02005e100001", "3QZK1M7-000B1"].map { secret in
            expect(!shown.contains(secret), "kept \(secret)")
        }
    },
    test("cpa status --json when cpad is not running") {
        let doc = try status("status-offline")
        return [
            expect(!doc.running),
            expectEqual(doc.link, nil),
            expectEqual(doc.recordings?.onDevice, nil),
            expectEqual(doc.recordings?.failed, 1),
            expectEqual(doc.projects.count, 2),
        ]
    },
    test("a later cpa: new fields read when there, unknown ones ignored") {
        let doc = try status("status-later")
        return [
            expectEqual(doc.api, 2),
            expectEqual(doc.link?.known, .paused),
            expectEqual(doc.pausedS, 42.5),
            expectEqual(doc.notesDir, "/Users/someone/CardputerNotes"),
            expectEqual(doc.hostLabel, "Office"),
            expectEqual(doc.warnings.count, 1),
        ]
    },
    test("a status that is not an object or has the wrong types is refused") {
        [
            expectThrows("array", matching: { $0 as? ContractError == .invalid(field: "(root)") }) {
                try CpaJSON.status(Data("[]".utf8))
            },
            expectThrows("running as text", matching: { $0 as? ContractError == .invalid(field: "running") }) {
                try CpaJSON.status(Data(#"{"running":"yes"}"#.utf8))
            },
        ]
    },
    test("the device headline in Chinese") {
        let running = try status("status-running")
        return [
            expectEqual(Labels.deviceHeadline(running), "设备连着 · 固件 v0.4.0"),
            expectEqual(Labels.deviceHeadline(try edited("status-running", ["link": "handshaking"])), "设备握手中"),
            expectEqual(Labels.deviceHeadline(try edited("status-running", ["link": "offline"])), "设备没连"),
            expectEqual(Labels.deviceHeadline(try status("status-later")), "设备暂停 43 秒"),
            expectEqual(Labels.deviceHeadline(try edited("status-running", ["link": "warp"])), "设备状态未知"),
            expectEqual(Labels.deviceHeadline(try status("status-offline")), "cpad 没在跑"),
        ]
    },
    test("the menu bar light: green linked, grey not, red when cpad is down or cpa failed") {
        [
            expectEqual(StatusLight.of(try status("status-running")), .linked),
            expectEqual(StatusLight.of(try edited("status-running", ["link": "offline"])), .unlinked),
            expectEqual(StatusLight.of(try status("status-later")), .unlinked),
            expectEqual(StatusLight.of(try status("status-offline")), .down),
            expectEqual(StatusLight.of(nil), .down),
        ]
    },
    test("cpa rec ls --json: every row and state") {
        let rows = try CpaJSON.recordings(try Fixture.data("cpa/rec-ls.json"))
        let byID = Dictionary(uniqueKeysWithValues: rows.map { ($0.sid, $0) })
        let onDevice = rows[0]
        return [
            expectEqual(rows.count, 16),
            expectEqual(onDevice.state.known, .onDevice),
            expectEqual(onDevice.dir, nil),
            expectEqual(onDevice.startedMs, nil),
            expect(onDevice.uncertain),
            expectEqual(byID["3QZK1M7-00042-0032"]?.reason, "model_missing"),
            expectEqual(byID["3QZK1M7-00042-0035"]?.result, RecResult(state: "failed", level: nil, errorCode: "E_STT")),
            expectEqual(byID["3QZK1M7-00042-0035"]?.mode?.known, .meeting),
            expectEqual(byID["3QZK1M7-00042-0036"]?.hidden, true),
            expectEqual(byID["3QZK1M7-00042-0037"]?.audioSwept, true),
            expectEqual(byID["3QZK1M7-00042-0037"]?.project, "acmeshop"),
            expectEqual(byID["3QZK1M7-00042-0033"]?.starred, true),
            expectEqual(byID["3QZK1M7-00042-0010"]?.dir, "recordings/2026-09-16/0453-3qzk1m7-00042-0010"),
            expectEqual(byID["3QZK1M7-00042-0010"]?.durMs, 33_408),
        ]
    },
    test("rec states in Chinese, unknown ones as Unknown") {
        [
            expectEqual(Labels.recState(Open(.archived)), "等转写"),
            expectEqual(Labels.recState(Open(.onDevice)), "在设备上"),
            expectEqual(Labels.recState(Open(raw: "later")), "未知"),
            expectEqual(Labels.blockedReason("model_missing"), "缺模型"),
            expectEqual(Labels.blockedReason("busy"), "别的转写程序在忙"),
            expectEqual(Labels.blockedReason("odd"), "未知"),
        ]
    },
])
