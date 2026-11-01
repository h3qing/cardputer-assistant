// More open files of the notes dir, read the same careful way: a segment's bytes for playback, summary.md as
// plain lines (a link shows its words, an image its alt text; nothing is loaded) and the newest manifest's
// written_at (Last pulled).

import Foundation

extension NotesDirectory {
    /// A WAV segment is at most this big (a long meeting segment at 16 kHz mono is far smaller).
    public static let audioCap = 64 << 20

    /// The bytes of one seg-NNN.wav of a session dir inside the root; no link, at most audioCap.
    public func readAudio(_ url: URL) throws -> Data {
        let session = url.deletingLastPathComponent().path
        let prefix = root.path + "/"
        // The session dir must pass session(dir:): recordings/<date>/<HHMM>-<id>, no link on the way.
        let inside = session.hasPrefix(prefix) && self.session(dir: String(session.dropFirst(prefix.count))) != nil
        guard inside, Patterns.isSegmentName(url.lastPathComponent) else { throw NotesError.outside }
        return try Self.readFile(url.path, cap: Self.audioCap)
    }

    /// A segment's length in seconds from its size, without reading it: 16 kHz mono 16-bit (the recording
    /// contract's audio) after a 44-byte header. nil when it is not a plain file of a session inside the root.
    public func segmentSeconds(_ url: URL) -> Double? {
        let session = url.deletingLastPathComponent().path
        let prefix = root.path + "/"
        guard session.hasPrefix(prefix), self.session(dir: String(session.dropFirst(prefix.count))) != nil,
              Patterns.isSegmentName(url.lastPathComponent), let info = Self.linkInfo(url.path),
              (info.st_mode & S_IFMT) == S_IFREG else { return nil }
        return Double(max(Int64(info.st_size) - 44, 0)) / 32_000
    }

    /// summary.md as lines to show; nil when there is none.
    public func summary(in session: URL) -> [String]? {
        guard let data = try? read(.summary, in: session) else { return nil }
        let lines = String(decoding: data, as: UTF8.self).components(separatedBy: "\n").map(MarkdownText.plain)
        return MarkdownText.collapse(lines)
    }

    /// written_at of the newest session's manifest.json, as written; only the newest day is looked at.
    public func latestPull() -> String? {
        let recordings = root.appendingPathComponent("recordings")
        let days = ((try? FileManager.default.contentsOfDirectory(atPath: recordings.path)) ?? []).filter(Patterns.isDate)
        guard let day = days.max() else { return nil }
        let names = ((try? FileManager.default.contentsOfDirectory(atPath: recordings.appendingPathComponent(day).path)) ?? [])
            .filter(Self.isSessionName).sorted(by: >)
        return names.lazy.compactMap { manifestStamp("recordings/\(day)/\($0)") }.first
    }

    func manifestStamp(_ relative: String) -> String? {
        struct Manifest: Decodable {
            let writtenAt: String?

            enum CodingKeys: String, CodingKey {
                case writtenAt = "written_at"
            }
        }
        guard let session = session(dir: relative), let manifest: Manifest = versioned(.manifest, in: session) else {
            return nil
        }
        return manifest.writtenAt.flatMap { DisplaySafe.excerpt($0, limit: 40) }
    }
}

/// Markdown shown as text: [words](url) → words, ![alt](url) → alt; display_safe; blank runs become one.
public enum MarkdownText {
    public static func plain(_ line: String) -> String {
        let unlinked = replacing(#"!?\[([^\]]*)\]\([^)]*\)"#, in: line, with: "$1")
        return DisplaySafe.clean(unlinked).trimmingCharacters(in: .whitespaces)
    }

    static func replacing(_ pattern: String, in text: String, with template: String) -> String {
        guard let regex = try? NSRegularExpression(pattern: pattern) else { return text }
        let range = NSRange(text.startIndex..., in: text)
        return regex.stringByReplacingMatches(in: text, range: range, withTemplate: template)
    }

    /// No blank line at either end, never two in a row.
    static func collapse(_ lines: [String]) -> [String] {
        let kept = lines.enumerated().filter { index, line in
            !line.isEmpty || (index > 0 && !lines[index - 1].isEmpty)
        }.map(\.element)
        let trimmed = kept.drop { $0.isEmpty }.reversed().drop { $0.isEmpty }.reversed()
        return Array(trimmed)
    }
}
