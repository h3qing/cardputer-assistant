// docs/24 §1.2 and §8.1, pinned by reading the sources: no network, no shell, no private socket or state
// files, no serial ports, no app-specific code, one place that starts processes, small files.

import Foundation

private let sources = Fixture.root.deletingLastPathComponent().appendingPathComponent("Sources")

private func swiftFiles() throws -> [(name: String, text: String)] {
    let paths = FileManager.default.enumerator(atPath: sources.path)?.compactMap { $0 as? String } ?? []
    return try paths.filter { $0.hasSuffix(".swift") && !$0.hasSuffix("RulesTests.swift") }.sorted().map { path in
        (path, try String(contentsOf: sources.appendingPathComponent(path), encoding: .utf8))
    }
}

/// Each needle is split so this file never matches itself.
private let forbidden = [
    "URLSess" + "ion", "import " + "Network", "NWConn" + "ection", "CFNet" + "work", "cpad." + "sock",
    "Application Support/" + "CardputerAssistant", "/bin/" + "sh", "/bin/" + "zsh", "/bin/" + "bash",
    "/dev/" + "cu.", "/dev/" + "tty.", "launch" + "ctl", "events." + "jsonl",
]

/// Apps, in any case (docs/25 §2.1): no app is named in this app's code, its comments or its tests. The
/// MCP client tabs come from cpa. (The editor is checked as a capitalised word: a page cursor is not an app.)
private let appNames = ["whisper" + "woof", "gr" + "ok", "co" + "dex", "obsi" + "dian", "chat" + "gpt", "clau" + "de",
                        "noti" + "on"]
private let appWords = ["Cur" + "sor"]

private func containsWord(_ text: String, _ word: String) -> Bool {
    text.components(separatedBy: CharacterSet.alphanumerics.inverted).contains(word)
}

let rulesTests = Suite(name: "Rules", cases: [
    test("no network, shell, private socket, state files, serial port or app-specific code in Sources") {
        try swiftFiles().flatMap { file in
            forbidden.map { needle in expect(!file.text.contains(needle), "\(file.name) contains \(needle)") }
        }
    },
    test("no app is named anywhere in Sources, in any case") {
        try swiftFiles().flatMap { file in
            let lower = file.text.lowercased()
            return appNames.map { needle in expect(!lower.contains(needle), "\(file.name) names \(needle)") }
                + appWords.map { word in expect(!containsWord(file.text, word), "\(file.name) names \(word)") }
        }
    },
    test("processes start in ProcessRunner.swift only") {
        try swiftFiles().map { file in
            let starts = file.text.contains("Process" + "()")
            return expect(!starts || file.name.hasSuffix("ProcessRunner.swift"), "\(file.name) starts a process")
        }
    },
    test("files stay small: at most 400 lines") {
        try swiftFiles().map { file in
            let lines = file.text.split(separator: "\n", omittingEmptySubsequences: false).count
            return expect(lines <= 400, "\(file.name): \(lines) lines")
        }
    },
])
