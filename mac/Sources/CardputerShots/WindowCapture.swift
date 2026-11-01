// Draws the app's real window (MainWindow in an NSWindow with its title bar) offscreen and reads its pixels
// back with cacheDisplay: no screen recording permission, nothing shown on screen.

import AppKit
import CardputerUI
import SwiftUI

@MainActor
enum WindowCapture {
    static func image<V: View>(_ view: V, size: CGSize) -> CGImage? {
        _ = NSApplication.shared
        NSApp.setActivationPolicy(.prohibited)
        let window = NSWindow(contentRect: NSRect(origin: .zero, size: size),
                              styleMask: [.titled, .closable, .miniaturizable, .resizable],
                              backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false
        window.appearance = NSAppearance(named: .darkAqua)
        window.title = "Cardputer Assistant"
        window.contentView = NSHostingView(rootView: view.environment(\.colorScheme, .dark))
        window.setFrameOrigin(NSPoint(x: -30000, y: -30000))
        window.orderFrontRegardless()
        settle(window)
        defer { window.close() }
        guard let frame = window.contentView?.superview,
              let rep = frame.bitmapImageRepForCachingDisplay(in: frame.bounds) else { return nil }
        frame.cacheDisplay(in: frame.bounds, to: rep)
        return rep.cgImage
    }

    /// Lets SwiftUI lay out and draw: a few turns of the run loop.
    static func settle(_ window: NSWindow) {
        for _ in 0..<6 {
            window.contentView?.layoutSubtreeIfNeeded()
            window.displayIfNeeded()
            RunLoop.current.run(until: Date().addingTimeInterval(0.08))
        }
    }
}
