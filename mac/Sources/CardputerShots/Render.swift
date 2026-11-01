// Draws a view to a PNG with SwiftUI's ImageRenderer (no screen recording permission), checks that it is
// not one flat colour, and lays every shot out on one overview sheet.

import AppKit
import CardputerCore
import CardputerUI
import ImageIO
import SwiftUI
import UniformTypeIdentifiers

@MainActor
enum Render {
    static let scale: CGFloat = 2

    static func image<V: View>(_ view: V, width: CGFloat, height: CGFloat?) -> CGImage? {
        let framed = view
            .environment(\.isSnapshot, true)
            .environment(\.colorScheme, .dark)
            .frame(width: width, height: height)
        let renderer = ImageRenderer(content: framed)
        renderer.scale = scale
        renderer.proposedSize = ProposedViewSize(width: width, height: height)
        return renderer.cgImage
    }

    static func write(_ image: CGImage, to url: URL) throws {
        guard let destination = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil)
        else { throw ShotError.write(url.lastPathComponent) }
        CGImageDestinationAddImage(destination, image, nil)
        guard CGImageDestinationFinalize(destination) else { throw ShotError.write(url.lastPathComponent) }
    }

    /// Fewer than 8 colours over every 5th pixel: nothing was drawn.
    static func isFlat(_ image: CGImage) -> Bool {
        let width = image.width
        let height = image.height
        var pixels = [UInt8](repeating: 0, count: width * height * 4)
        let drawn = pixels.withUnsafeMutableBytes { buffer -> Bool in
            guard let context = CGContext(data: buffer.baseAddress, width: width, height: height, bitsPerComponent: 8,
                                          bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                                          bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return false }
            context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
            return true
        }
        guard drawn else { return true }
        let colours = Set(stride(from: 0, to: width * height, by: 5).map { index -> UInt32 in
            let at = index * 4
            return UInt32(pixels[at]) << 16 | UInt32(pixels[at + 1]) << 8 | UInt32(pixels[at + 2])
        })
        return colours.count < 8
    }

    /// The menu bar item as MenuBarExtra shows it (MenuBarLabel: Mando, the dot and the number) for each
    /// model, on a light and a dark bar, six times larger; then the four dots alone.
    static func menuBar(_ models: [AppModel]) -> CGImage? {
        let row = { (scheme: ColorScheme, bar: Color) in
            HStack(spacing: 22) {
                ForEach(Array(models.enumerated()), id: \.offset) { _, model in
                    MenuBarLabel(model: model).font(.system(size: 13, weight: .medium))
                }
                ForEach([StatusLight.linked, .unlinked, .down, .unknown], id: \.self) { light in
                    Image(nsImage: MenuIcon.image(light))
                }
                Image(nsImage: MenuIcon.image(.linked, sparkle: true))
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 4)
            .background(bar)
            .environment(\.colorScheme, scheme)
        }
        let view = VStack(spacing: 0) {
            row(.light, Color(white: 0.93))
            row(.dark, Color(white: 0.15))
        }
        let renderer = ImageRenderer(content: view)
        renderer.scale = 6
        return renderer.cgImage
    }

    static func overview(_ shots: [(caption: String, image: CGImage)]) -> CGImage? {
        let columns = Array(repeating: GridItem(.fixed(380), spacing: 20), count: 4)
        let sheet = LazyVGrid(columns: columns, alignment: .leading, spacing: 20) {
            ForEach(Array(shots.enumerated()), id: \.offset) { _, shot in
                VStack(alignment: .leading, spacing: 6) {
                    Image(decorative: shot.image, scale: 1).resizable().aspectRatio(contentMode: .fit)
                        .clipShape(RoundedRectangle(cornerRadius: 6))
                        .frame(width: 380, height: 254)
                    Text(shot.caption).font(.system(size: 12, weight: .medium)).foregroundStyle(.white)
                }
            }
        }
        .padding(24)
        .background(Color.black)
        return image(sheet, width: 1640, height: nil)
    }
}

enum ShotError: Error {
    case write(String)
    case render(String)
    case flat(String)
}
