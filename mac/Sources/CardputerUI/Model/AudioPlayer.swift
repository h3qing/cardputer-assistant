// Plays a recording's seg-NNN.wav files in order, one segment in memory at a time: each segment's length
// comes from its size (no reading), the one to play is read off the main thread through
// NotesDirectory.readAudio (inside the notes dir, no link, capped) and handed to AVAudioPlayer as bytes;
// the next is read when it ends. Nothing is ever written.

import AVFoundation
import CardputerCore
import Foundation
import Observation

@MainActor
@Observable
public final class AudioPlayer {
    public internal(set) var playingID: String?
    public internal(set) var failed: String?

    @ObservationIgnored var player: AVAudioPlayer?
    @ObservationIgnored var queue: [URL] = []
    @ObservationIgnored var index = 0
    @ObservationIgnored var notes: NotesDirectory?
    /// Changes with every play or stop: a segment read for an older one is dropped.
    @ObservationIgnored var token = UUID()
    @ObservationIgnored let finisher = Finisher()

    public init() {
        finisher.ended = { [weak self] ended in
            Task { @MainActor in self?.ended(ended) }
        }
    }

    public var isPlaying: Bool {
        playingID != nil
    }

    public func toggle(id: String, urls: [URL], notes: NotesDirectory?) {
        if playingID == id {
            stop()
        } else {
            play(id: id, urls: urls, notes: notes)
        }
    }

    /// From `seconds` into the recording, across segments.
    public func play(id: String, urls: [URL], notes: NotesDirectory?, from seconds: Double = 0) {
        stop()
        guard let notes else { return fail(tr("找不到笔记目录")) }
        let durations = urls.map { notes.segmentSeconds($0) ?? 0 }
        guard !urls.isEmpty else { return fail(tr("这段录音没有可以播放的音频")) }
        let start = Playback.locate(seconds, durations: durations)
        self.notes = notes
        queue = urls
        playingID = id
        failed = nil
        load(start.segment, at: start.offset)
    }

    public func stop() {
        token = UUID()
        player?.stop()
        player = nil
        queue = []
        playingID = nil
    }

    /// Reads segment `number` off the main thread, then plays it from `offset`.
    func load(_ number: Int, at offset: TimeInterval) {
        guard number < queue.count, let notes else { return stop() }
        let (url, current) = (queue[number], token)
        index = number
        Task {
            let data = await Task.detached { try? notes.readAudio(url) }.value
            guard current == token else { return }
            guard let data, let next = try? AVAudioPlayer(data: data) else {
                return number + 1 < queue.count ? load(number + 1, at: 0) : fail(tr("这段录音没有可以播放的音频"))
            }
            next.delegate = finisher
            next.currentTime = min(offset, max(next.duration - 0.05, 0))
            player = next
            next.play()
        }
    }

    /// A segment ended: play the next, unless this one was replaced in the meantime.
    func ended(_ ended: AVAudioPlayer) {
        guard ended === player else { return }
        load(index + 1, at: 0)
    }

    func fail(_ text: String) {
        token = UUID()
        failed = text
        playingID = nil
    }
}

/// AVAudioPlayer's delegate: says which player ended.
final class Finisher: NSObject, AVAudioPlayerDelegate, @unchecked Sendable {
    var ended: ((AVAudioPlayer) -> Void)?

    func audioPlayerDidFinishPlaying(_ player: AVAudioPlayer, successfully flag: Bool) {
        ended?(player)
    }
}
