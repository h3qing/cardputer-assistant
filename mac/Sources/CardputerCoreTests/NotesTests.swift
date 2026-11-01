import CardputerCore
import Foundation

private let fm = FileManager.default
private let done = "recordings/2026-09-16/0544-3qzk1m7-00042-0033"
private let transcriptOnly = "recordings/2026-09-16/0545-3qzk1m7-00042-0034"
private let failed = "recordings/2026-09-16/0546-3qzk1m7-00042-0035"
private let bare = "recordings/2026-09-16/0453-3qzk1m7-00042-0010"

/// A private copy of Fixtures/notes, so a test can add links and big files to it.
func notesCopy(_ label: String) throws -> (URL, NotesDirectory) {
    let dir = try Fixture.tempDir(label).appendingPathComponent("CardputerNotes")
    try fm.copyItem(at: Fixture.url("notes"), to: dir)
    guard let notes = NotesDirectory(path: dir.path) else { throw NotesError.unreadable }
    return (dir, notes)
}

let notesTests = Suite(name: "Notes", cases: [
    test("the notes dir: the one chosen, else notes_dir of cpa status, else cpa-stt's recordings' parent, else ~/CardputerNotes") {
        let (dir, _) = try notesCopy("resolve")
        let home = dir.deletingLastPathComponent()
        let status = try CpaJSON.status(try JSONSerialization.data(withJSONObject: ["running": true, "notes_dir": dir.path]))
        let stt = try CpaJSON.stt(try JSONSerialization.data(
            withJSONObject: ["on": true, "recordings": dir.appendingPathComponent("recordings").path]))
        let empty = try Fixture.tempDir("nohome")
        return [
            expectEqual(NotesDirectory.resolve(status: status, stt: nil, home: "/nonexistent", chosen: nil)?.root.path, dir.path),
            expectEqual(NotesDirectory.resolve(status: nil, stt: stt, home: "/nonexistent", chosen: nil)?.root.path, dir.path),
            expectEqual(NotesDirectory.resolve(status: nil, stt: nil, home: home.path, chosen: nil)?.root.path, dir.path),
            expectEqual(NotesDirectory.resolve(status: nil, stt: nil, home: empty.path, chosen: dir.path)?.root.path, dir.path),
            expectEqual(NotesDirectory.resolve(status: nil, stt: nil, home: empty.path, chosen: nil), nil),
            expectEqual(NotesDirectory.resolve(status: status, stt: nil, home: home.path, chosen: empty.path)?.root.path,
                        empty.path, "the chosen folder wins"),
            expectEqual(NotesDirectory.resolve(status: status, stt: nil, home: home.path, chosen: "/nonexistent")?.root.path,
                        dir.path, "a chosen folder that is gone falls back"),
        ]
    },
    test("a recording's folder is looked for on its own day first, then everywhere") {
        let (dir, notes) = try notesCopy("byday")
        let started = Date(timeIntervalSince1970: 1_789_508_677.433)  // 2026-09-16 05:44 +08:00
        let wrongDay = started.addingTimeInterval(-3 * 86400)
        return [
            expectEqual(notes.session(id: "3QZK1M7-00042-0033", near: started, calendar: Fixture.calendar)?.path,
                        dir.appendingPathComponent(done).path),
            expectEqual(notes.session(id: "3QZK1M7-00042-0033", near: wrongDay, calendar: Fixture.calendar)?.path,
                        dir.appendingPathComponent(done).path, "a wrong day still finds it"),
            expectEqual(notes.session(id: "3QZK1M7-00042-0033", near: nil, calendar: Fixture.calendar)?.path,
                        dir.appendingPathComponent(done).path),
            expectEqual(NotesDirectory.folder(id: "3QZK1M7-00042-0033", at: started, calendar: Fixture.calendar), done),
        ]
    },
    test("status.json: why a transcriber is stuck on a recording") {
        let (dir, notes) = try notesCopy("status")
        let session = dir.appendingPathComponent(bare)
        let text = #"{"v":1,"session":"3QZK1M7-00042-0010","attempt":1,"state":"blocked","reason":"engine","by":"cpa-stt","at":"2026-09-16T05:00:00+08:00"}"#
        try Data(text.utf8).write(to: session.appendingPathComponent("status.json"))
        return [
            expectEqual(notes.status(in: session), SessionStatus(state: "blocked", reason: "engine", by: "cpa-stt")),
            expectEqual(notes.status(in: dir.appendingPathComponent(done)), nil),
        ]
    },
    test("a session dir must be recordings/<date>/<HHMM>-<id> inside the root, with no link on the way") {
        let (dir, notes) = try notesCopy("session")
        let outside = try Fixture.tempDir("outside")
        try fm.createSymbolicLink(atPath: dir.appendingPathComponent("recordings/2026-09-16/0600-3qzk1m7-00042-0099").path,
                                  withDestinationPath: outside.path)
        try fm.createSymbolicLink(atPath: dir.appendingPathComponent("recordings/2026-09-17").path,
                                  withDestinationPath: dir.appendingPathComponent("recordings/2026-09-16").path)
        return [
            expectEqual(notes.session(dir: done)?.path, dir.appendingPathComponent(done).path),
            expectEqual(notes.session(dir: "recordings/2026-09-16/../../../etc"), nil),
            expectEqual(notes.session(dir: "/etc"), nil),
            expectEqual(notes.session(dir: "recordings/x/0544-3qzk1m7-00042-0033"), nil),
            expectEqual(notes.session(dir: "recordings/2026-09-16/0544-3QZK1M7-00042-0033"), nil, "upper case"),
            expectEqual(notes.session(dir: "recordings/2026-09-16/0600-3qzk1m7-00042-0099"), nil, "linked out"),
            expectEqual(notes.session(dir: "recordings/2026-09-17/0544-3qzk1m7-00042-0033"), nil, "linked date"),
            expectEqual(notes.session(id: "3QZK1M7-00042-0033")?.path, dir.appendingPathComponent(done).path),
            expectEqual(notes.session(id: "3qzk1m7-00042-0034")?.path, dir.appendingPathComponent(transcriptOnly).path),
            expectEqual(notes.session(id: "3QZK1M7-00042-0099"), nil),
            expectEqual(notes.session(id: "../etc"), nil),
        ]
    },
    test("result.json and transcript.json, display_safe") {
        let (_, notes) = try notesCopy("read")
        let session = try notes.session(dir: done).unwrapped()
        let other = try notes.session(dir: transcriptOnly).unwrapped()
        let result = notes.result(in: session)
        return [
            expectEqual(result?.title, "Send Alice the quote"),
            expectEqual(result?.memo?.text, "To-do: send Alice the quote by Friday."),
            expectEqual(result?.memo?.intent.known, .todo),
            expectEqual(result?.level, "summary"),
            expectEqual(notes.transcript(in: session),
                        [TimedSentence(seconds: 0, text: "To-do: send Alice the quote by Friday."),
                         TimedSentence(seconds: 3.2, text: "Also ask about the delivery date.")]),
            expectEqual(notes.transcript(in: other)?.map(\.text), ["把 Mando 的图标换成新的", "然后 再看一下菜单栏。"]),
            expectEqual(notes.result(in: try notes.session(dir: failed).unwrapped())?.errorCode, "E_STT"),
            expectEqual(notes.transcript(in: try notes.session(dir: bare).unwrapped()), nil),
        ]
    },
    test("transcript.md when there is no transcript.json") {
        let (dir, notes) = try notesCopy("md")
        try fm.removeItem(at: dir.appendingPathComponent(done + "/transcript.json"))
        return [
            expectEqual(notes.transcript(in: try notes.session(dir: done).unwrapped())?.map(\.seconds), [0, 3]),
            expectEqual(TranscriptMarkdown.parse("- [12:34] 你好 ^t001234\n- [12:34] 再见 ^t001234-2\n- [1:02:03] 长 ^t010203\nnoise\n"),
                        [TimedSentence(seconds: 754, text: "你好"), TimedSentence(seconds: 754, text: "再见"),
                         TimedSentence(seconds: 3723, text: "长")]),
            expectEqual(TranscriptMarkdown.parse("（没听到）\n"), []),
        ]
    },
    test("refused: a linked file, a file too big, a result of a later v") {
        let (dir, notes) = try notesCopy("refuse")
        let session = dir.appendingPathComponent(done)
        let elsewhere = try Fixture.tempDir("elsewhere").appendingPathComponent("result.json")
        try fm.moveItem(at: session.appendingPathComponent("result.json"), to: elsewhere)
        try fm.createSymbolicLink(at: session.appendingPathComponent("result.json"), withDestinationURL: elsewhere)
        let big = dir.appendingPathComponent(transcriptOnly + "/result.json")
        try Data(count: 65 << 10).write(to: big)
        let later = dir.appendingPathComponent(failed + "/result.json")
        try Data(#"{"v":2,"session":"3QZK1M7-00042-0035","state":"done"}"#.utf8).write(to: later)
        return [
            expectEqual(notes.result(in: session), nil),
            expectThrows("link", matching: { $0 as? NotesError == .notRegular }) { try notes.read(.result, in: session) },
            expectThrows("big", matching: { $0 as? NotesError == .tooLarge }) {
                try notes.read(.result, in: dir.appendingPathComponent(transcriptOnly))
            },
            expectEqual(notes.result(in: dir.appendingPathComponent(failed)), nil),
        ]
    },
    test("audio segments: seg-NNN.wav files only, no links") {
        let (dir, notes) = try notesCopy("audio")
        let session = dir.appendingPathComponent(done)
        try fm.createSymbolicLink(atPath: session.appendingPathComponent("seg-001.wav").path, withDestinationPath: "/etc/hosts")
        try Data().write(to: session.appendingPathComponent("seg-x.wav"))
        return [expectEqual(notes.audio(in: session).map(\.lastPathComponent), ["seg-000.wav"])]
    },
    test("who claims recordings/ (.transcriber)") {
        let (_, notes) = try notesCopy("claim")
        return [expectEqual(notes.transcriber()?.app, "cpa-stt"), expectEqual(notes.transcriber()?.beatAt, "2026-09-16T05:49:00+08:00")]
    },
])
