import CardputerCore
import Foundation

/// tools/theme/tokens.py: ("Name", 0xRRGGBB) pairs of TOKENS.
private func repoTokens() throws -> [String: UInt32] {
    let path = Fixture.root.deletingLastPathComponent().deletingLastPathComponent()
        .appendingPathComponent("tools/theme/tokens.py")
    let text = try String(contentsOf: path, encoding: .utf8)
    let regex = try NSRegularExpression(pattern: #"\("([A-Za-z0-9]+)", 0x([0-9A-Fa-f]{6})\)"#)
    let matches = regex.matches(in: text, range: NSRange(text.startIndex..., in: text))
    return Dictionary(matches.compactMap { match -> (String, UInt32)? in
        guard let name = Range(match.range(at: 1), in: text), let hex = Range(match.range(at: 2), in: text),
              let value = UInt32(text[hex], radix: 16) else { return nil }
        return (String(text[name]), value)
    }, uniquingKeysWith: { first, _ in first })
}

let themeTests = Suite(name: "Theme", cases: [
    test("the Mac app's colour tokens are the device's: every one equals tools/theme/tokens.py") {
        let repo = try repoTokens()
        return [expect(ThemeTokens.all.count >= 20, "\(ThemeTokens.all.count) tokens")]
            + ThemeTokens.all.sorted { $0.key < $1.key }.map { name, value in
                expectEqual(repo[name], value, name)
            }
    },
])
