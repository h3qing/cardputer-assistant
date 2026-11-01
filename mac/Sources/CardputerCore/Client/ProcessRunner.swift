// Runs one program through Process with an argument array: never a shell. stdin is closed unless bytes are
// given; stdout is read up to a cap (past it the program is stopped); stderr is captured and kept apart.
// At the timeout the program's whole process group gets SIGTERM, and SIGKILL a second later. The timeout
// is a hard limit: once the program has exited, its output gets at most a second more to end (a child it
// left behind may hold the pipe open), and what came is returned.

import Foundation

public struct RunRequest: Hashable, Sendable {
    public let executable: String
    public let arguments: [String]
    public let stdin: Data?
    public let environment: [String: String]
    public let timeout: TimeInterval
    public let outputCap: Int
    public let workingDirectory: String

    public init(executable: String, arguments: [String], stdin: Data?, environment: [String: String],
                timeout: TimeInterval, outputCap: Int, workingDirectory: String) {
        self.executable = executable
        self.arguments = arguments
        self.stdin = stdin
        self.environment = environment
        self.timeout = timeout
        self.outputCap = outputCap
        self.workingDirectory = workingDirectory
    }
}

public struct RawOutput: Hashable, Sendable {
    public let exitCode: Int32
    public let stdout: Data
    /// Captured so the pipe never fills; never shown or logged.
    public let stderr: Data
    public let timedOut: Bool
    /// stdout went past the cap; what is kept is its first `outputCap` bytes.
    public let truncated: Bool

    public init(exitCode: Int32, stdout: Data, stderr: Data, timedOut: Bool, truncated: Bool) {
        self.exitCode = exitCode
        self.stdout = stdout
        self.stderr = stderr
        self.timedOut = timedOut
        self.truncated = truncated
    }
}

public enum ProcessRunner {
    static let stderrCap = 64 << 10
    /// How long the output may stay open after the program exited.
    static let grace: TimeInterval = 1

    /// Throws only when the program cannot be started.
    public static func run(_ request: RunRequest) async throws -> RawOutput {
        try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                continuation.resume(with: Result { try runBlocking(request) })
            }
        }
    }

    static func runBlocking(_ request: RunRequest) throws -> RawOutput {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: request.executable)
        process.arguments = request.arguments
        process.environment = request.environment
        process.currentDirectoryURL = URL(fileURLWithPath: request.workingDirectory)
        let output = Pipe()
        let errors = Pipe()
        let input = request.stdin.map { _ in Pipe() }
        process.standardOutput = output
        process.standardError = errors
        process.standardInput = input ?? FileHandle.nullDevice
        try process.run()
        let stopper = Stopper(process)
        stopper.arm(after: request.timeout)
        feed(input, request.stdin)
        let stderr = Collector(errors.fileHandleForReading, cap: stderrCap, onOverflow: {}).drainInBackground()
        let stdout = Collector(output.fileHandleForReading, cap: request.outputCap, onOverflow: stopper.stop)
            .drainInBackground()
        process.waitUntilExit()
        stopper.disarm()
        _ = stdout.wait(seconds: grace)
        _ = stderr.wait(seconds: 0.1)
        return RawOutput(exitCode: process.terminationStatus, stdout: stdout.data, stderr: stderr.data,
                         timedOut: stopper.timedOut, truncated: stdout.overflowed)
    }

    /// Writes stdin on its own thread and closes it; a program that exits early cannot raise SIGPIPE here.
    static func feed(_ pipe: Pipe?, _ data: Data?) {
        guard let pipe, let data else { return }
        let handle = pipe.fileHandleForWriting
        _ = fcntl(handle.fileDescriptor, F_SETNOSIGPIPE, 1)
        DispatchQueue.global(qos: .userInitiated).async {
            try? handle.write(contentsOf: data)
            try? handle.close()
        }
    }
}

/// Stops a process at its timeout or on demand: SIGTERM to its process group (Process makes the child a
/// group leader) and to itself, then SIGKILL a second later. The timeout counts only while it runs.
final class Stopper: @unchecked Sendable {
    private let process: Process
    private let lock = NSLock()
    private var fired = false
    private var timer: DispatchWorkItem?

    init(_ process: Process) {
        self.process = process
    }

    var timedOut: Bool {
        lock.withLock { fired }
    }

    func arm(after seconds: TimeInterval) {
        let work = DispatchWorkItem { [self] in
            guard process.isRunning else { return }
            lock.withLock { fired = true }
            stop()
        }
        lock.withLock { timer = work }
        DispatchQueue.global().asyncAfter(deadline: .now() + seconds, execute: work)
    }

    func disarm() {
        lock.withLock { timer?.cancel() }
    }

    func stop() {
        let pid = process.processIdentifier
        guard pid > 0 else { return }
        Self.signal(pid, SIGTERM)
        DispatchQueue.global().asyncAfter(deadline: .now() + 1) {
            Self.signal(pid, SIGKILL)
        }
    }

    /// The group first (children left behind), then the program itself if it is not a group leader.
    static func signal(_ pid: pid_t, _ number: Int32) {
        kill(-pid, number)
        kill(pid, number)
    }
}

/// Reads a pipe to its end, keeping at most `cap` bytes; calls onOverflow once when more arrive.
final class Collector: @unchecked Sendable {
    private let handle: FileHandle
    private let cap: Int
    private let onOverflow: () -> Void
    private let lock = NSLock()
    private let done = DispatchSemaphore(value: 0)
    private var kept = Data()
    private var over = false

    init(_ handle: FileHandle, cap: Int, onOverflow: @escaping () -> Void) {
        self.handle = handle
        self.cap = cap
        self.onOverflow = onOverflow
    }

    var data: Data {
        lock.withLock { kept }
    }

    var overflowed: Bool {
        lock.withLock { over }
    }

    /// Reads on the calling thread until the end of the pipe, each chunk as soon as it comes (read(2), not
    /// FileHandle.read(upToCount:), which waits for the whole count).
    func drain() {
        var buffer = [UInt8](repeating: 0, count: 64 << 10)
        let fd = handle.fileDescriptor
        while true {
            let count = buffer.withUnsafeMutableBytes { read(fd, $0.baseAddress, $0.count) }
            if count > 0 {
                take(Data(buffer[0..<count]))
            } else if count == 0 || errno != EINTR {
                break
            }
        }
        done.signal()
    }

    /// Reads on a thread of its own (a pipe held open by a stray child blocks only that thread).
    func drainInBackground() -> Collector {
        Thread { self.drain() }.start()
        return self
    }

    /// True when the pipe ended within `seconds`.
    func wait(seconds: TimeInterval) -> Bool {
        done.wait(timeout: .now() + seconds) == .success
    }

    private func take(_ chunk: Data) {
        let first = lock.withLock { () -> Bool in
            let room = max(0, cap - kept.count)
            kept.append(chunk.prefix(room))
            let firstOverflow = chunk.count > room && !over
            over = over || chunk.count > room
            return firstOverflow
        }
        if first {
            onOverflow()
        }
    }
}
