import CardputerCore
import Foundation

private let fm = FileManager.default

/// An executable file (contents do not matter: the locator never runs it) with these permissions.
private func tool(_ dir: URL, _ name: String = "cpa", mode: Int = 0o755) throws -> URL {
    try fm.createDirectory(at: dir, withIntermediateDirectories: true)
    let url = dir.appendingPathComponent(name)
    try Data("#!fake\n".utf8).write(to: url)
    try fm.setAttributes([.posixPermissions: mode], ofItemAtPath: url.path)
    return url
}

private func request(_ executable: String, _ arguments: [String], stdin: Data? = nil, timeout: TimeInterval = 5,
                     cap: Int = 4 << 20, environment: [String: String] = [:]) -> RunRequest {
    RunRequest(executable: executable, arguments: arguments, stdin: stdin, environment: environment,
               timeout: timeout, outputCap: cap, workingDirectory: NSTemporaryDirectory())
}

private func text(_ data: Data) -> String {
    String(decoding: data, as: UTF8.self)
}

let locatorTests = Suite(name: "Locator", cases: [
    test("the one chosen in settings first; then ~/.local/bin, PATH and Homebrew's dirs") {
        let home = try Fixture.tempDir("home")
        let pathDir = home.appendingPathComponent("elsewhere")
        let local = try tool(home.appendingPathComponent(".local/bin"))
        let onPath = try tool(pathDir)
        let chosen = try tool(home.appendingPathComponent("chosen"))
        let first = ToolLocator.locate(.cpa, home: home.path, pathVariable: "relative:\(pathDir.path)", chosen: chosen.path)
        try fm.removeItem(at: chosen)
        let second = ToolLocator.locate(.cpa, home: home.path, pathVariable: pathDir.path, chosen: chosen.path)
        try fm.removeItem(at: local)
        let third = ToolLocator.locate(.cpa, home: home.path, pathVariable: pathDir.path, chosen: nil)
        try fm.removeItem(at: onPath)
        let none = ToolLocator.locate(.cpa, home: home.path, pathVariable: pathDir.path, chosen: chosen.path)
        return [
            expectEqual(first?.path, chosen.path, "the chosen one wins while it passes the checks"),
            expectEqual(second?.path, local.path, "a chosen one that is gone falls back to discovery"),
            expectEqual(third?.path, onPath.path),
            expectEqual(none, nil),
            expectEqual(ToolLocator.candidates(.cpaStt, home: "/h", pathVariable: "/a::b:/c", chosen: "/x/cpa-stt"),
                        ["/x/cpa-stt", "/h/.local/bin/cpa-stt", "/a/cpa-stt", "/c/cpa-stt", "/opt/homebrew/bin/cpa-stt",
                         "/usr/local/bin/cpa-stt"]),
        ]
    },
    test("a link is run as found and checked at its real path; its folder goes on the child's PATH") {
        let home = try Fixture.tempDir("link")
        let real = try tool(home.appendingPathComponent("uv/tools/cpad/bin"))
        let bin = home.appendingPathComponent(".local/bin")
        try fm.createDirectory(at: bin, withIntermediateDirectories: true)
        try fm.createSymbolicLink(at: bin.appendingPathComponent("cpa"), withDestinationURL: real)
        let found = ToolLocator.locate(.cpa, home: home.path, pathVariable: nil, chosen: nil)
        return [
            expectEqual(found?.path, bin.appendingPathComponent("cpa").path),
            expectEqual(found?.realPath, real.path),
            expectEqual(found?.directory, bin.path),
        ]
    },
    test("refused: writable by group or others, not executable, a folder, a dangling link") {
        let dir = try Fixture.tempDir("perm")
        let group = try tool(dir, "group", mode: 0o775)
        let other = try tool(dir, "other", mode: 0o757)
        let plain = try tool(dir, "plain", mode: 0o644)
        let folder = dir.appendingPathComponent("folder")
        try fm.createDirectory(at: folder, withIntermediateDirectories: true)
        let dangling = dir.appendingPathComponent("dangling")
        try fm.createSymbolicLink(atPath: dangling.path, withDestinationPath: dir.appendingPathComponent("gone").path)
        let mine = try tool(dir, "mine", mode: 0o700)
        return [group, other, plain, folder, dangling].map { url in
            expectEqual(ToolLocator.check(url.path), nil, url.lastPathComponent)
        } + [
            expectEqual(ToolLocator.check(mine.path)?.path, mine.path, "owned by me, 0700"),
            expectEqual(ToolLocator.check("/bin/cat")?.path, "/bin/cat", "owned by root"),
        ]
    },
    test("the child's environment: a short allow-list and a PATH that starts at cpa's folder") {
        let parent = ["HOME": "/Users/me", "USER": "me", "LOGNAME": "me", "LANG": "zh_CN.UTF-8",
                      "TMPDIR": "/tmp/x", "CPA_HOME": "/tmp/cpa", "PATH": "/evil", "ANTHROPIC_API_KEY": "k",
                      "DYLD_INSERT_LIBRARIES": "/x.dylib", "PYTHONPATH": "/x"]
        let child = ChildEnvironment.make(parent: parent, toolDirectory: "/Users/me/.local/bin")
        let without = ChildEnvironment.make(parent: ["HOME": "/Users/me"], toolDirectory: "/opt/homebrew/bin")
        return [
            expectEqual(child, ["HOME": "/Users/me", "USER": "me", "LOGNAME": "me", "LANG": "zh_CN.UTF-8",
                                "TMPDIR": "/tmp/x", "CPA_HOME": "/tmp/cpa",
                                "PATH": "/Users/me/.local/bin:/usr/bin:/bin:/usr/sbin:/sbin"]),
            expectEqual(without, ["HOME": "/Users/me", "PATH": "/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"]),
        ]
    },
])

let runnerTests = Suite(name: "Runner", cases: [
    test("arguments reach the program as they are: no shell in between") {
        let out = try await ProcessRunner.run(request("/bin/echo", ["a b", "$HOME", ";", "`id`", "*"]))
        return [expectEqual(text(out.stdout), "a b $HOME ; `id` *\n"), expectEqual(out.exitCode, 0)]
    },
    test("stdin carries the bytes given; without them stdin is closed") {
        let words = Data("报价 --filing hidden".utf8)
        let echoed = try await ProcessRunner.run(request("/bin/cat", [], stdin: words))
        let closed = try await ProcessRunner.run(request("/bin/cat", [], stdin: nil, timeout: 2))
        return [
            expectEqual(echoed.stdout, words),
            expectEqual(closed.stdout, Data()),
            expect(!closed.timedOut, "cat waited on an open stdin"),
        ]
    },
    test("exit codes and stderr are kept apart from stdout") {
        let failed = try await ProcessRunner.run(request("/usr/bin/false", []))
        let listing = try await ProcessRunner.run(request("/bin/ls", ["/nonexistent-cardputer"]))
        return [
            expectEqual(failed.exitCode, 1),
            expect(listing.exitCode != 0),
            expectEqual(listing.stdout, Data()),
            expect(!listing.stderr.isEmpty),
        ]
    },
    test("a timeout stops it: SIGTERM, then SIGKILL a second later") {
        let start = Date()
        let slept = try await ProcessRunner.run(request("/bin/sleep", ["30"], timeout: 0.3))
        let quick = Date().timeIntervalSince(start)
        let stubborn = Date()
        let ignoring = try await ProcessRunner.run(request(
            "/usr/bin/perl", ["-e", "$SIG{TERM} = 'IGNORE'; sleep 30"], timeout: 0.3))
        let killed = Date().timeIntervalSince(stubborn)
        return [
            expect(slept.timedOut), expect(quick < 2, "took \(quick) s"),
            expect(ignoring.timedOut), expect(killed >= 1.2 && killed < 4, "took \(killed) s"),
        ]
    },
    test("a child left holding stdout does not hold the run: 1 s after the program exits, what came is kept") {
        let start = Date()
        let out = try await ProcessRunner.run(request(
            "/usr/bin/perl", ["-e", "if (fork) { print \"hi\\n\"; exit 0 } sleep 6"], timeout: 4))
        let took = Date().timeIntervalSince(start)
        return [
            expectEqual(text(out.stdout), "hi\n"),
            expectEqual(out.exitCode, 0),
            expect(!out.timedOut, "the program exited before its timeout"),
            expect(took < 3, "took \(took) s"),
        ]
    },
    test("a timeout stops the program's whole process group, children too") {
        let out = try await ProcessRunner.run(request(
            "/usr/bin/perl", ["-e", "$| = 1; my $p = fork; print \"$p\\n\" if $p; sleep 30"], timeout: 0.5))
        let child = Int32(text(out.stdout).trimmingCharacters(in: .whitespacesAndNewlines)) ?? 0
        try await Task.sleep(nanoseconds: 300_000_000)
        return [
            expect(out.timedOut),
            expect(child > 0, "no child pid: \(text(out.stdout))"),
            expect(child > 0 && kill(child, 0) != 0, "child \(child) still runs"),
        ]
    },
    test("stdout is cut at the cap and the program is stopped") {
        let start = Date()
        let out = try await ProcessRunner.run(request("/usr/bin/yes", [], timeout: 10, cap: 1 << 20))
        return [
            expect(out.truncated),
            expectEqual(out.stdout.count, 1 << 20),
            expect(Date().timeIntervalSince(start) < 5, "yes ran on"),
        ]
    },
    test("the child gets exactly the environment given") {
        let out = try await ProcessRunner.run(request("/usr/bin/env", [], environment: ["HOME": "/h", "PATH": "/usr/bin"]))
        return [expectEqual(Set(text(out.stdout).split(separator: "\n").map(String.init)), ["HOME=/h", "PATH=/usr/bin"])]
    },
    test("a program that cannot start is an error") {
        [await expectThrowsAsync("missing") { try await ProcessRunner.run(request("/nonexistent/cpa", [])) }]
    },
])

let processClientTests = Suite(name: "ProcessClient", cases: [
    test("no cpa found: notFound, nothing runs") {
        let client = ProcessCpaClient(cpa: nil, stt: nil, environment: [:])
        return [
            await expectThrowsAsync("cpa", matching: { $0 as? CpaError == .notFound(.cpa) }) { try await client.status() },
            await expectThrowsAsync("stt", matching: { $0 as? CpaError == .notFound(.cpaStt) }) { try await client.sttStatus() },
        ]
    },
    test("discover finds cpa and cpa-stt from HOME and PATH of the given environment") {
        let home = try Fixture.tempDir("discover")
        let cpa = try tool(home.appendingPathComponent(".local/bin"))
        let client = ProcessCpaClient.discover(environment: ["HOME": home.path, "PATH": "/nonexistent"])
        return [expectEqual(client.cpa?.path, cpa.path), expectEqual(client.stt, nil)]
    },
    test("runs the located program with the command's arguments") {
        let echo = try ToolLocator.check("/bin/echo").unwrapped()
        let client = ProcessCpaClient(cpa: echo, stt: nil, environment: ["HOME": NSHomeDirectory()])
        let out = try await client.execute(.status)
        return [expectEqual(out.exitCode, 0), expectEqual(text(out.stdout), "status --json\n")]
    },
    test("runaway output and a timeout become typed errors") {
        let yes = try ToolLocator.check("/usr/bin/yes").unwrapped()
        let client = ProcessCpaClient(cpa: yes, stt: nil, environment: [:])
        let slow = RawOutput(exitCode: 15, stdout: Data(), stderr: Data(), timedOut: true, truncated: false)
        return [
            await expectThrowsAsync("yes", matching: { $0 as? CpaError == .outputTooLarge("cpa status") }) {
                try await client.status()
            },
            expectThrows("timeout", matching: { $0 as? CpaError == .timedOut("cpa status") }) {
                try ProcessCpaClient.output(slow, of: .status)
            },
        ]
    },
])

private struct Missing: Error {}

extension Optional {
    func unwrapped() throws -> Wrapped {
        guard let value = self else { throw Missing() }
        return value
    }
}
