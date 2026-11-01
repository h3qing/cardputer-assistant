// `cpa host name --json -- NAME` (docs/23 P1-B, gap G2): this Mac's name, stamped on every recording it
// pulls. The name is text the user chose, so it goes after `--`, trimmed, one display_safe line of 1-24
// UTF-8 bytes (cpa counts bytes: 8 Chinese characters), never starting with '-'.

import Foundation

extension CpaCommand {
    public static let hostNameBytes = 24

    public static func hostName(_ name: String) throws -> CpaCommand {
        let clean = name.trimmingCharacters(in: .whitespaces)
        let fits = (1...hostNameBytes).contains(clean.utf8.count)
        guard fits, DisplaySafe.isSafe(clean), !clean.hasPrefix("-") else {
            throw CommandError.invalid(argument: "name")
        }
        return CpaCommand(.cpa, ["host", "name", "--json", "--", clean])
    }
}
