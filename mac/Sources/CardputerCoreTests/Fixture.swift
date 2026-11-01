// Fixtures live in mac/Fixtures, found from this file's place in the source tree (swift run).

import Foundation

enum Fixture {
    static let root = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        .appendingPathComponent("Fixtures")

    static func url(_ path: String) -> URL {
        root.appendingPathComponent(path)
    }

    static func data(_ path: String) throws -> Data {
        try Data(contentsOf: url(path))
    }

    /// A fresh, empty directory of its own under the system temp dir.
    static func tempDir(_ label: String) throws -> URL {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("cardputer-tests-\(label)-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        guard let real = realpath(dir.path, nil) else { return dir }
        defer { free(real) }
        return URL(fileURLWithPath: String(cString: real))
    }

    /// A fixed calendar and clock for time formatting: UTC+8, weeks starting on Monday.
    static let calendar: Calendar = {
        var cal = Calendar(identifier: .gregorian)
        cal.timeZone = TimeZone(secondsFromGMT: 8 * 3600)!
        cal.firstWeekday = 2
        return cal
    }()

    /// 2026-09-16 14:30:00 +08:00, a Wednesday.
    static let now = Date(timeIntervalSince1970: 1_789_540_200)
}
