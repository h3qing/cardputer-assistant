// While no window or menu is visible, FSEvents watches <notes>/recordings (read only; never $CPA_HOME) and,
// 2 s after a burst of changes, asks for the new numbers once (docs/24 §5.1). File events, so the
// transcriber's heartbeat (recordings/.transcriber, every 60 s) can be told apart and ignored.

import CoreServices
import Foundation

final class NotesWatcher {
    private let onChange: () -> Void
    private var stream: FSEventStreamRef?

    init?(path: String, onChange: @escaping () -> Void) {
        self.onChange = onChange
        var context = FSEventStreamContext(version: 0, info: nil, retain: nil, release: nil, copyDescription: nil)
        context.info = Unmanaged.passUnretained(self).toOpaque()
        let callback: FSEventStreamCallback = { _, info, count, paths, _, _ in
            guard let info else { return }
            let names = unsafeBitCast(paths, to: NSArray.self) as? [String] ?? []
            guard NotesWatcher.matters(names.prefix(count)) else { return }
            Unmanaged<NotesWatcher>.fromOpaque(info).takeUnretainedValue().onChange()
        }
        let flags = FSEventStreamCreateFlags(kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagUseCFTypes)
        guard let stream = FSEventStreamCreate(nil, callback, &context, [path] as CFArray,
                                               FSEventStreamEventId(kFSEventStreamEventIdSinceNow), 2.0, flags) else {
            return nil
        }
        self.stream = stream
        FSEventStreamSetDispatchQueue(stream, DispatchQueue.main)
        FSEventStreamStart(stream)
    }

    /// Anything but a dot file: the claim's heartbeat and every writer's temporary files (the rename that
    /// follows names the real file).
    static func matters<S: Sequence>(_ paths: S) -> Bool where S.Element == String {
        paths.contains { !(($0 as NSString).lastPathComponent.hasPrefix(".")) }
    }

    deinit {
        guard let stream else { return }
        FSEventStreamStop(stream)
        FSEventStreamInvalidate(stream)
        FSEventStreamRelease(stream)
    }
}
