import CardputerCore
import Foundation

private let fm = FileManager.default

private let cal = Fixture.calendar
private let now = Fixture.now  // 2026-09-16 14:30 +08:00, the fixtures' day

private func mixed() throws -> FilingList {
    try FilingContract.list(try Fixture.data("filing-v1/vectors/valid/ls_mixed.json").vectorData())
}

private func fallback() throws -> [InboxEntry] {
    let (_, notes) = try notesCopy("fallback")
    return Fallback.entries(try CpaJSON.recordings(try Fixture.data("cpa/rec-ls.json")), notes: notes, cache: .empty).0
}

private func ids(_ entries: [InboxEntry]) -> [String] {
    entries.map { String($0.id.suffix(4)) }
}

let inboxTests = Suite(name: "Inbox", language: .zh, cases: [
    test("an entry from a filing row keeps its fields; the headline is the title, else the text") {
        let rows = try mixed().rows
        let handed = InboxEntry(rows[1])
        let orphan = InboxEntry(rows[2])
        let quiet = InboxEntry(rows[3])
        return [
            expectEqual(handed.filing, .handed),
            expectEqual(handed.headline, "报价"),
            expectEqual(handed.queue?.qid, "q12"),
            expectEqual(handed.env?.host, "公司"),
            expectEqual(orphan.asked, "acme"),
            expectEqual(orphan.headline, "便条 · 24s · 还没转写"),
            expectEqual(quiet.headline, "会议 · 24s · 转写失败"),
            expectEqual(InboxEntry(rows[4]).headline, "旧会议只有转写。"),
        ]
    },
    test("fallback: recordings of cpa rec ls without a project, not hidden, off the device") {
        let entries = try fallback()
        let byID = Dictionary(uniqueKeysWithValues: entries.map { (String($0.id.suffix(4)), $0) })
        return [
            expectEqual(ids(entries), ["0035", "0034", "0033", "0032", "0031", "0010", "0012", "0014", "0015", "0016"],
                        "cpa's order, newest first"),
            expect(entries.allSatisfy { $0.filing == nil && $0.project == nil }),
            expectEqual(byID["0033"]?.title, "Send Alice the quote"),
            expectEqual(byID["0033"]?.text, "To-do: send Alice the quote by Friday."),
            expectEqual(byID["0034"]?.text, "把 Mando 的图标换成新的 然后 再看一下菜单栏。"),
            expectEqual(byID["0035"]?.text, nil),
            expectEqual(entries.map(\.tx.raw), ["failed", "transcript", "done", "blocked", "processing", "waiting",
                                                "waiting", "waiting", "waiting", "stale"]),
            expectEqual(byID["0010"]?.at, RecordedAt(wallMs: 1_789_505_617_433, tzMin: nil, uncertain: false)),
            expectEqual(byID["0035"]?.kind?.known, .meeting),
        ]
    },
    test("fallback filters: date (the folder's day), mode and text, in the app") {
        let entries = try fallback()
        let filter = { (query: FilingQuery) in ids(Fallback.filter(entries, query, now: now, calendar: cal)) }
        return [
            expectEqual(filter(FilingQuery(dates: .today)).count, 9),
            expectEqual(filter(FilingQuery(dates: .lastWeek)), []),
            expectEqual(filter(FilingQuery(mode: .meeting)), ["0035", "0016"]),
            expectEqual(filter(FilingQuery(text: "ALICE")), ["0033"]),
            expectEqual(filter(FilingQuery(text: "菜单栏")), ["0034"]),
            expectEqual(filter(FilingQuery(limit: 3)), ["0035", "0034", "0033"], "the limit applies here too"),
        ]
    },
    test("words read once per recording state: a cache keyed by id, state and result") {
        let (dir, notes) = try notesCopy("cache")
        let rows = try CpaJSON.recordings(try Fixture.data("cpa/rec-ls.json"))
        let (first, cache) = Fallback.entries(rows, notes: notes, cache: .empty)
        try fm.removeItem(at: dir.appendingPathComponent("recordings/2026-09-16/0544-3qzk1m7-00042-0033/result.json"))
        let (again, kept) = Fallback.entries(rows, notes: notes, cache: cache)
        let changed = try JSONSerialization.jsonObject(with: try Fixture.data("cpa/rec-ls.json")) as? [[String: Any]] ?? []
        let edited = changed.map { row in
            row["sid"] as? String == "3QZK1M7-00042-0033" ? row.merging(["state": "transcript"]) { $1 } : row
        }
        let rows2 = try CpaJSON.recordings(try JSONSerialization.data(withJSONObject: edited))
        let (reread, _) = Fallback.entries(rows2, notes: notes, cache: kept)
        let title = { (entries: [InboxEntry]) in entries.first { $0.id.hasSuffix("0033") }?.title }
        return [
            expectEqual(title(first), "Send Alice the quote"),
            expectEqual(title(again), "Send Alice the quote", "the file is gone, the cache still has it"),
            expectEqual(title(reread), nil, "a new state reads the files again"),
            expectEqual(cache.count, kept.count),
            expectEqual(Headlines.empty.count, 0),
        ]
    },
    test("the loader asks cpa filing when it can, and keeps only other-Mac rows for that scope") {
        let query = FilingQuery(scope: .otherMac)
        let command = try CpaCommand.filingList(query, now: now, calendar: cal)
        let ls = CommandOutput(exitCode: 0, stdout: try Fixture.data("filing-v1/vectors/valid/ls_mixed.json").vectorData())
        let loader = InboxLoader(client: FakeCpaClient([command: .output(ls)]), notes: nil)
        let page = try await loader.load(query, canFile: true, now: now, calendar: cal)
        return [
            expectEqual(page.source, .filing(FilingCounts(new: 1, otherMac: 1, clipsWaiting: 0, onDevice: nil))),
            expectEqual(page.entries.map(\.id), ["3QZK1M7-00045-0001"]),
            expectEqual(page.nextCursor, "WyJmaWxpbmciLCIxNzkyNjIwMTIwMDAwLzNRWksxTTctMDAwNDAtMDAwMCJd"),
        ]
    },
    test("without cpa filing the loader reads cpa rec ls and the open files") {
        let (_, notes) = try notesCopy("loader")
        let rec = CommandOutput(exitCode: 0, stdout: try Fixture.data("cpa/rec-ls.json"))
        let loader = InboxLoader(client: FakeCpaClient([.recordings(hidden: false): .output(rec)]), notes: notes)
        let page = try await loader.load(FilingQuery(mode: .memo), canFile: false, now: now, calendar: cal)
        return [
            expectEqual(page.source, .fallback),
            expectEqual(page.entries.count, 8),
            expectEqual(page.nextCursor, nil),
            expectEqual(page.summary, "8 段没项目的录音（只读）"),
        ]
    },
    test("what each entry may do, and why not") {
        let rows = try mixed().rows
        let byID = Dictionary(uniqueKeysWithValues: try fallback().map { (String($0.id.suffix(4)), $0) })
        let memo = InboxEntry(rows[1])
        let fresh = InboxEntry(try FilingContract.row(Data(#"""
            {"id":"3QZK1M7-00042-0A20","kind":"memo","filing":"new","why":["unfiled"],"asked":null,
             "at":{"wall_ms":1,"tz_min":0,"uncertain":false},"dur_ms":1000,"device":null,"env":null,"prof":null,
             "project":null,"starred":false,"tx":"done","title":null,"text":"有字","intent":null,"clips":null,"queue":null}
            """#.utf8)))
        return [
            expectEqual(fresh.handBlocked, nil),
            expectEqual(memo.handBlocked, "已经交给 AI 了"),
            expectEqual(InboxEntry(rows[0]).handBlocked, "会议里有别人的话，不能交给 AI"),
            expectEqual(InboxEntry(rows[2]).handBlocked, "还没转写，先归档或等转写"),
            expectEqual(byID["0033"]?.handBlocked, "归档、交给 AI、看过了都要 cpa filing"),
            expectEqual(byID["0033"]?.fileBlocked, "归档、交给 AI、看过了都要 cpa filing"),
            expectEqual(memo.fileBlocked, nil),
            expect(InboxEntry(rows[2]).needsConfirmation),
            expect(!memo.needsConfirmation),
        ]
    },
    test("the line on top") {
        let counts = { (new: Int, other: Int, device: Int?) in
            InboxPage(source: .filing(FilingCounts(new: new, otherMac: other, clipsWaiting: 0, onDevice: device)),
                      entries: [], nextCursor: nil).summary
        }
        return [
            expectEqual(counts(16, 0, 2), "16 条待归档 · 设备上还有 2 段"),
            expectEqual(counts(3, 1, nil), "4 条待归档 · 其中别的电脑的项目 1 条"),
            expectEqual(counts(0, 0, 0), "都归好了"),
        ]
    },
    test("after an action the next new entry is selected") {
        let rows = try mixed().rows.map(InboxEntry.init)
        return [
            expectEqual(rows.nextNew(after: "3QZK1M7-00042-0A1C"), "3QZK1M7-00045-0001"),
            expectEqual(rows.nextNew(after: "3QZK1M7-00045-0001"), nil, "the only new one"),
            expectEqual(rows.nextNew(after: "3QZK1M7-00040-0000"), "3QZK1M7-00045-0001", "none after: the one before"),
        ]
    },
])
