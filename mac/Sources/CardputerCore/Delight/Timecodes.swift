// Vlog timecodes: a recording's ★ marks (manifest.json) and transcript lines as chapter lines,
// "00:01:23 ★ words", and the transcript as an SRT. Pure; the words are already display_safe.

import Foundation

public enum Timecodes {
    /// HH:MM:SS, whole seconds down; never below zero.
    public static func clock(_ seconds: Double) -> String {
        let whole = Int(max(seconds, 0))
        return String(format: "%02d:%02d:%02d", whole / 3600, whole / 60 % 60, whole % 60)
    }

    /// HH:MM:SS,mmm as SRT writes it.
    public static func srtClock(_ seconds: Double) -> String {
        let ms = Int((max(seconds, 0) * 1000).rounded())
        return clock(Double(ms / 1000)) + String(format: ",%03d", ms % 1000)
    }

    /// Chapter lines. With ★: 00:00:00 first, then only the starred lines (a mark before the first line or
    /// without a transcript is a bare "★" line), as YouTube's chapters want. Without ★: every line.
    public static func chapters(marks: [Double], sentences: [TimedSentence]) -> String {
        let timed = timedLines(sentences)
        guard !marks.isEmpty else {
            return timed.map { clock($0.start) + " " + $0.text }.joined(separator: "\n")
        }
        let first = timed.first?.start ?? .infinity
        let loose = marks.filter { $0 < first }.map { (start: $0, line: clock($0) + " ★") }
        let starredLines = timed.filter { starred($0, marks) }
        let picked = (loose + starredLines.map { (start: $0.start, line: clock($0.start) + " ★ " + $0.text) })
            .sorted { $0.start < $1.start }
        guard let top = picked.first, Int(top.start) > 0 else { return picked.map(\.line).joined(separator: "\n") }
        let opening = timed.first.flatMap { starred($0, marks) ? nil : $0.text }
        let zero = clock(0) + (opening.map { " " + $0 } ?? "")
        return ([zero] + picked.map(\.line)).joined(separator: "\n")
    }

    /// The transcript as SRT cues: each ends where the next begins, and lasts at least a second; the last ends
    /// at `duration` when that is after it, else four seconds on. A starred line keeps its ★.
    public static func srt(sentences: [TimedSentence], marks: [Double], duration: Double?) -> String {
        timedLines(sentences).enumerated().map { index, item in
            let next = item.end.map { max($0, item.start + 1) }
            let end = next ?? duration.flatMap { $0 > item.start ? $0 : nil } ?? item.start + 4
            let text = (starred(item, marks) ? "★ " : "") + item.text
            return "\(index + 1)\n\(srtClock(item.start)) --> \(srtClock(end))\n\(text)\n"
        }.joined(separator: "\n")
    }

    struct Line {
        let start: Double
        let end: Double?
        let text: String
    }

    /// The lines with a time and words, in order, newlines as spaces; lines at the same time are one line;
    /// each line ends where the next one starts.
    static func timedLines(_ sentences: [TimedSentence]) -> [Line] {
        let timed = sentences.compactMap { sentence -> (start: Double, text: String)? in
            let text = sentence.text.components(separatedBy: .newlines)
                .map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }.joined(separator: " ")
            guard let start = sentence.seconds, !text.isEmpty else { return nil }
            return (start, text)
        }
        let merged = Dictionary(grouping: timed, by: \.start).map { start, group in
            (start: start, text: group.map(\.text).joined(separator: " "))
        }.sorted { $0.start < $1.start }
        return merged.enumerated().map { index, item in
            Line(start: item.start, end: index + 1 < merged.count ? merged[index + 1].start : nil, text: item.text)
        }
    }

    static func starred(_ line: Line, _ marks: [Double]) -> Bool {
        marks.contains { $0 >= line.start && $0 < (line.end ?? .infinity) }
    }
}
