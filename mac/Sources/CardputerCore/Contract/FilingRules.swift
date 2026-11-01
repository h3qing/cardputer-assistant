// The parts of protocol/filing/v1 that types alone do not hold: patterns, lengths, ranges and the two
// conditional rules (a handed row carries its queue item; only a new row asks for another Mac's key).
// Each failure names the path of the first fault, as check_filing does.

enum FilingRules {
    static func check(_ condition: Bool, _ path: String) throws {
        if !condition {
            throw ContractError.invalid(field: path)
        }
    }

    static func join(_ base: String, _ key: String) -> String {
        base.isEmpty ? key : base + "." + key
    }

    static func list(_ doc: FilingList) throws {
        let counts = [doc.counts.new, doc.counts.otherMac, doc.counts.clipsWaiting, doc.counts.onDevice ?? 0]
        try check(counts.allSatisfy { $0 >= 0 }, "counts")
        try check(doc.nextCursor.map(Patterns.isCursor) ?? true, "next_cursor")
        try check(doc.rows.count <= 1000, "rows")
        for (index, row) in doc.rows.enumerated() {
            try FilingRules.row(row, at: "rows[\(index)]")
        }
    }

    static func row(_ row: FilingRow, at base: String) throws {
        let at = { (key: String) in join(base, key) }
        try check(Patterns.isMonoID(row.id), at("id"))
        try check(Patterns.isToken(row.kind.raw), at("kind"))
        try check(row.asked == nil || row.filing == .new, at("filing"))
        try check(row.why.count <= 8, at("why"))
        for (index, reason) in row.why.enumerated() {
            try check(Patterns.isToken(reason.raw), at("why[\(index)]"))
        }
        try check(row.asked.map(Patterns.isProjectKey) ?? true, at("asked"))
        try recordedAt(row.at, at: at("at"))
        try check((row.durMs ?? 0) >= 0, at("dur_ms"))
        try check(row.device.map(Patterns.isDeviceID) ?? true, at("device"))
        try env(row.env, at: at("env"))
        try check(row.project.map(Patterns.isProjectKey) ?? true, at("project"))
        try check(Patterns.isToken(row.tx.raw), at("tx"))
        try check(row.title.map { Patterns.isSafeText($0, 1...80) } ?? true, at("title"))
        try check(row.text.map { Patterns.isSafeText($0, 1...280) } ?? true, at("text"))
        try check(row.intent.map { Patterns.isToken($0.raw) } ?? true, at("intent"))
        try queue(row.queue, at: at("queue"))
        try check(row.filing != .handed || row.queue != nil, at("queue"))
    }

    static func recordedAt(_ value: RecordedAt, at base: String) throws {
        try check((value.wallMs ?? 0) >= 0, join(base, "wall_ms"))
        try check((-1439...1439).contains(value.tzMin ?? 0), join(base, "tz_min"))
    }

    static func env(_ value: MacStamp?, at base: String) throws {
        guard let value else { return }
        try check(value.host.map { Patterns.isSafeText($0, 1...24) } ?? true, join(base, "host"))
        try check(value.link.map { Patterns.isToken($0.raw) } ?? true, join(base, "link"))
    }

    static func queue(_ value: QueueItem?, at base: String) throws {
        guard let value else { return }
        try check(Patterns.isQueueID(value.qid), join(base, "qid"))
        try check(Patterns.isToken(value.intent.raw), join(base, "intent"))
    }

    static func item(_ doc: FilingItem) throws {
        try row(doc.row, at: "row")
        try check(doc.text.map { Patterns.isSafeText($0, 1...2000) } ?? true, "text")
        for (index, sentence) in (doc.sentences ?? []).enumerated() {
            try check((sentence.seconds ?? 0) >= 0, "sentences[\(index)].t_s")
            try check(DisplaySafe.isSafe(sentence.text), "sentences[\(index)].text")
        }
        try check(doc.todos.count <= 20, "todos")
        for (index, todo) in doc.todos.enumerated() {
            try self.todo(todo, at: "todos[\(index)]")
        }
        try check(doc.audio.count <= 10000, "audio")
        for (index, name) in doc.audio.enumerated() {
            try check(Patterns.isSegmentName(name), "audio[\(index)]")
        }
        try check(doc.fw.map { Patterns.isSafeText($0, 1...64) } ?? true, "fw")
        try check(doc.queueText.map { Patterns.isSafeText($0, 1...500) } ?? true, "queue_text")
    }

    static func todo(_ todo: Todo, at base: String) throws {
        try check(todo.n >= 1, join(base, "n"))
        try check(Patterns.isSafeText(todo.text, 1...200), join(base, "text"))
        try check(todo.owner.map { Patterns.isSafeText($0, 1...40) } ?? true, join(base, "owner"))
        try check(todo.due.map { Patterns.isSafeText($0, 1...40) } ?? true, join(base, "due"))
        try check((todo.seconds ?? 0) >= 0, join(base, "t_s"))
    }

    static func result(_ doc: FilingResult) throws {
        try check(Patterns.isMonoID(doc.id), "id")
        try check(doc.changed || doc.undo == nil, "undo")
        if let undo = doc.undo {
            try check(Patterns.isToken(undo.op.raw), "undo.op")
            try check(Patterns.isMonoID(undo.id), "undo.id")
            try check(undo.project.map(Patterns.isProjectKey) ?? true, "undo.args.project")
        }
        try queue(doc.queue, at: "queue")
    }

    static func failure(_ doc: FilingFailure) throws {
        try check(doc.field.map(Patterns.isToken) ?? true, "field")
    }

    static func host(_ doc: HostName) throws {
        try check(doc.host.map { Patterns.isSafeText($0, 1...24) } ?? true, "host")
    }

    static func projects(_ doc: ProjectList) throws {
        try check(doc.projects.count <= 9, "projects")
        for (index, entry) in doc.projects.enumerated() {
            let base = "projects[\(index)]"
            try check(Patterns.isProjectKey(entry.key), join(base, "key"))
            try check(Patterns.isSafeText(entry.name, 1...24), join(base, "name"))
            try check((1...9).contains(entry.slot), join(base, "slot"))
            try check(entry.folder.map { Patterns.isSafeText($0, 1...64) && !$0.contains("/") } ?? true, join(base, "folder"))
        }
    }
}
