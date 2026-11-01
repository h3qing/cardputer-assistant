// The notes dir (~/CardputerNotes), read only and only through its open files (recording contract v1):
// manifest.json, status.json, transcript.json, transcript.md, result.json, summary.md, seg-NNN.wav and
// recordings/.transcriber. A session dir is found by its id, must be recordings/<date>/<HHMM>-<id> with no
// link anywhere on the way (its real path is exactly root + that path), and files open with O_NOFOLLOW
// and a size cap. Nothing here ever writes.

import Foundation

public enum NotesError: Error, Hashable, Sendable {
    case outside, notRegular, tooLarge, unreadable
}

public enum OpenFile: Hashable, Sendable {
    case manifest, status, transcriptJSON, transcriptMarkdown, result, summary

    var name: String {
        switch self {
        case .manifest: return "manifest.json"
        case .status: return "status.json"
        case .transcriptJSON: return "transcript.json"
        case .transcriptMarkdown: return "transcript.md"
        case .result: return "result.json"
        case .summary: return "summary.md"
        }
    }

    /// result.json is at most 64 KiB by contract; the others are capped generously.
    var cap: Int {
        switch self {
        case .result: return 64 << 10
        case .status: return 4 << 10
        case .manifest, .summary: return 1 << 20
        case .transcriptJSON, .transcriptMarkdown: return 8 << 20
        }
    }
}

public struct NotesDirectory: Hashable, Sendable {
    /// The real path of the notes dir.
    public let root: URL

    public init?(path: String) {
        guard let real = ToolLocator.realPath(path), let info = ToolLocator.fileInfo(real),
              (info.st_mode & S_IFMT) == S_IFDIR else { return nil }
        self.root = URL(fileURLWithPath: real, isDirectory: true)
    }

    /// G1 (docs/24 §1.3): the folder chosen in settings, else cpa status's notes_dir, else the parent of
    /// cpa-stt's recordings, else ~/CardputerNotes; the first that is a folder.
    public static func resolve(status: CpaStatus?, stt: SttStatus?, home: String, chosen: String?) -> NotesDirectory? {
        let fromStt = stt?.recordings.map { ($0 as NSString).deletingLastPathComponent }
        let home = (home as NSString).appendingPathComponent("CardputerNotes")
        return [chosen, status?.notesDir, fromStt, home].compactMap { $0 }.lazy.compactMap(NotesDirectory.init).first
    }

    /// A session dir as `cpa rec ls --json` gives it (relative to the notes dir).
    public func session(dir relative: String) -> URL? {
        let parts = relative.split(separator: "/", omittingEmptySubsequences: false).map(String.init)
        guard parts.count == 3, parts[0] == "recordings", Patterns.isDate(parts[1]),
              Self.isSessionName(parts[2]) else { return nil }
        let path = root.appendingPathComponent(relative).path
        guard ToolLocator.realPath(path) == path, let info = ToolLocator.fileInfo(path),
              (info.st_mode & S_IFMT) == S_IFDIR else { return nil }
        return URL(fileURLWithPath: path, isDirectory: true)
    }

    /// The session dir of a recording id, found by scanning recordings/<date>/ (the contract has no path).
    public func session(id: String) -> URL? {
        session(id: id, near: nil, calendar: .current)
    }

    /// The same, looking first where cpad puts it (the start's date and HHMM in this Mac's time zone), then in
    /// that day's folder and the days beside it, and only then in every day.
    public func session(id: String, near start: Date?, calendar: Calendar) -> URL? {
        let upper = id.uppercased()
        guard Patterns.isMonoID(upper) else { return nil }
        if let start, let exact = session(dir: Self.folder(id: upper, at: start, calendar: calendar)) {
            return exact
        }
        let hinted = start.map { date in [0, -1, 1].map { FilingQuery.day(date.addingTimeInterval(Double($0) * 86400), calendar) } }
        let recordings = root.appendingPathComponent("recordings")
        let all = ((try? FileManager.default.contentsOfDirectory(atPath: recordings.path)) ?? [])
            .filter(Patterns.isDate).sorted(by: >)
        let days = (hinted ?? []).filter(all.contains) + all.filter { !(hinted ?? []).contains($0) }
        return days.lazy.compactMap { day in self.session(id: upper, inDay: day) }.first
    }

    func session(id upper: String, inDay day: String) -> URL? {
        let suffix = "-" + upper.lowercased()
        let folder = root.appendingPathComponent("recordings").appendingPathComponent(day).path
        let names = (try? FileManager.default.contentsOfDirectory(atPath: folder)) ?? []
        return names.filter { $0.hasSuffix(suffix) }.lazy.compactMap { session(dir: "recordings/\(day)/\($0)") }.first
    }

    /// recordings/<YYYY-MM-DD>/<HHMM>-<id lower> for a session that started at `start` (cpad's land.session_dir).
    public static func folder(id: String, at start: Date, calendar: Calendar) -> String {
        let parts = calendar.dateComponents([.hour, .minute], from: start)
        let time = String(format: "%02d%02d", parts.hour ?? 0, parts.minute ?? 0)
        return "recordings/\(FilingQuery.day(start, calendar))/\(time)-\(id.lowercased())"
    }

    /// One open file of a session dir: not a link, a regular file, at most its cap.
    public func read(_ file: OpenFile, in session: URL) throws -> Data {
        guard session.path.hasPrefix(root.path + "/") else { throw NotesError.outside }
        return try Self.readFile(session.appendingPathComponent(file.name).path, cap: file.cap)
    }

    /// The seg-NNN.wav files of a session, in order; links and other names are left out.
    public func audio(in session: URL) -> [URL] {
        guard session.path.hasPrefix(root.path + "/") else { return [] }
        let names = (try? FileManager.default.contentsOfDirectory(atPath: session.path)) ?? []
        return names.filter(Patterns.isSegmentName).sorted().map { session.appendingPathComponent($0) }.filter { url in
            guard let info = Self.linkInfo(url.path) else { return false }
            return (info.st_mode & S_IFMT) == S_IFREG
        }
    }

    /// recordings/.transcriber: which transcriber claims the recordings folder now.
    public func transcriber() -> TranscriberClaim? {
        let path = root.appendingPathComponent("recordings/.transcriber").path
        return (try? Self.readFile(path, cap: 4 << 10)).flatMap { try? JSONDecoder().decode(TranscriberClaim.self, from: $0) }
    }

    /// HHMM-<lower-case id>
    static func isSessionName(_ name: String) -> Bool {
        let head = name.prefix(5)
        let id = String(name.dropFirst(5))
        return head.count == 5 && head.prefix(4).allSatisfy(\.isASCII) && head.prefix(4).allSatisfy(\.isNumber)
            && head.last == "-" && id == id.lowercased() && Patterns.isMonoID(id.uppercased())
    }

    static func readFile(_ path: String, cap: Int) throws -> Data {
        let fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)
        guard fd >= 0 else { throw errno == ELOOP ? NotesError.notRegular : NotesError.unreadable }
        let handle = FileHandle(fileDescriptor: fd, closeOnDealloc: true)
        var info = stat()
        guard fstat(fd, &info) == 0, (info.st_mode & S_IFMT) == S_IFREG else { throw NotesError.notRegular }
        guard info.st_size <= cap else { throw NotesError.tooLarge }
        guard let data = try? handle.read(upToCount: cap + 1) ?? Data() else { throw NotesError.unreadable }
        guard data.count <= cap else { throw NotesError.tooLarge }
        return data
    }

    static func linkInfo(_ path: String) -> stat? {
        var info = stat()
        return lstat(path, &info) == 0 ? info : nil
    }
}
