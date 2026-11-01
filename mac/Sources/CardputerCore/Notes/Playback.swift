// Where a time in a recording falls among its segments: the segment and the time into it. Pure.

import Foundation

public enum Playback {
    public struct Position: Hashable, Sendable {
        public let segment: Int
        public let offset: TimeInterval
    }

    /// The segment that holds `seconds` and the time into it; past the end, near the end of the last one.
    public static func locate(_ seconds: Double, durations: [TimeInterval]) -> Position {
        var left = max(seconds, 0)
        for (index, duration) in durations.enumerated() {
            if left < duration || index == durations.count - 1 {
                return Position(segment: index, offset: min(left, max(duration - 0.05, 0)))
            }
            left -= duration
        }
        return Position(segment: 0, offset: 0)
    }
}
