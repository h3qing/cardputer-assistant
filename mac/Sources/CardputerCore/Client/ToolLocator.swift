// Where cpa and cpa-stt are (docs/24 §5.2): the one chosen in settings first (so a cpa of a branch can be
// tried), then ~/.local/bin (an app opened from Finder has only the system dirs on its PATH), then PATH,
// then Homebrew's dirs. A candidate counts only when its real path is a regular, executable file owned by
// this user or root that neither group nor others can write; a chosen one that fails falls through.

import Foundation

public struct ToolLocation: Hashable, Sendable {
    /// As found: what is run, so cpa sees its own folder on PATH (cpa mcp setup prints it).
    public let path: String
    /// The checked file behind any links.
    public let realPath: String

    /// The folder `path` is in: the first entry of the child's PATH.
    public var directory: String {
        (path as NSString).deletingLastPathComponent
    }
}

public enum ToolLocator {
    static let fallbackDirectories = ["/opt/homebrew/bin", "/usr/local/bin"]

    public static func candidates(_ tool: Tool, home: String, pathVariable: String?, chosen: String? = nil) -> [String] {
        let pathDirs = (pathVariable ?? "").split(separator: ":").map(String.init).filter { $0.hasPrefix("/") }
        let dirs = [(home as NSString).appendingPathComponent(".local/bin")] + pathDirs + fallbackDirectories
        let all = (chosen.map { [$0] } ?? []) + dirs.map { ($0 as NSString).appendingPathComponent(tool.rawValue) }
        return all.enumerated().filter { index, path in !all.prefix(index).contains(path) }.map(\.element)
    }

    /// The first candidate that passes check(), or nil.
    public static func locate(_ tool: Tool, home: String, pathVariable: String?, chosen: String?) -> ToolLocation? {
        candidates(tool, home: home, pathVariable: pathVariable, chosen: chosen).lazy.compactMap(check).first
    }

    public static func check(_ path: String) -> ToolLocation? {
        guard let real = realPath(path), let info = fileInfo(real) else { return nil }
        let regular = (info.st_mode & S_IFMT) == S_IFREG
        let notShared = (info.st_mode & 0o022) == 0
        let owned = info.st_uid == getuid() || info.st_uid == 0
        guard regular, notShared, owned, access(real, X_OK) == 0 else { return nil }
        return ToolLocation(path: path, realPath: real)
    }

    static func realPath(_ path: String) -> String? {
        guard let resolved = realpath(path, nil) else { return nil }
        defer { free(resolved) }
        return String(cString: resolved)
    }

    static func fileInfo(_ path: String) -> stat? {
        var info = stat()
        return stat(path, &info) == 0 ? info : nil
    }
}

/// The environment a cpa or cpa-stt child gets: an allow-list, never the app's whole environment.
public enum ChildEnvironment {
    public static let passed = ["HOME", "USER", "LOGNAME", "LANG", "TMPDIR", "CPA_HOME"]
    public static let systemPath = "/usr/bin:/bin:/usr/sbin:/sbin"

    public static func make(parent: [String: String], toolDirectory: String) -> [String: String] {
        let kept = parent.filter { passed.contains($0.key) }
        return kept.merging(["PATH": toolDirectory + ":" + systemPath]) { _, path in path }
    }
}
