// The MCP clients `cpa mcp setup` prints a snippet for. The list comes from cpa: `cpa mcp setup --list
// --json` (gap G8), else the choices its own help lists ({a,b,c}); each tab is named after its id, so no
// client is named in this app's code. "generic" is any other stdio client.

import Foundation

public struct McpClientEntry: Hashable, Sendable, Identifiable {
    public let id: String
    public let label: String

    public init(id: String, label: String? = nil) {
        self.id = id
        self.label = label.flatMap { DisplaySafe.excerpt($0, limit: 24) } ?? McpClients.title(id)
    }
}

public enum McpClients {
    /// Any other client that runs a local stdio server.
    public static var generic: McpClientEntry { McpClientEntry(id: "generic", label: tr("其他 MCP 客户端")) }

    /// ^[a-z0-9][a-z0-9-]{0,31}$
    public static func isID(_ text: String) -> Bool {
        guard let first = text.first, (1...32).contains(text.count), first != "-" else { return false }
        return text.allSatisfy { ($0.isASCII && ($0.isLowercase || $0.isNumber)) || $0 == "-" }
    }

    /// "alpha-one" → "Alpha One".
    public static func title(_ id: String) -> String {
        id.split(separator: "-").map { $0.prefix(1).uppercased() + $0.dropFirst() }.joined(separator: " ")
    }

    /// The ids of argparse's `{a,b,c}` in `cpa mcp setup --help`; empty when there is none.
    public static func choices(help: String) -> [String] {
        guard let open = help.firstIndex(of: "{"), let close = help[open...].firstIndex(of: "}") else { return [] }
        let ids = help[help.index(after: open)..<close].split(separator: ",").map(String.init)
        return ids.allSatisfy(isID) ? ids : []
    }

    /// `cpa mcp setup --list --json`: an array of ids, or of {id, label}. Entries that are neither are skipped.
    public static func parse(_ data: Data) throws -> [McpClientEntry] {
        let items: [ListedClient] = try CpaJSON.decode(data)
        return items.compactMap { item in isID(item.id) ? McpClientEntry(id: item.id, label: item.label) : nil }
    }
}

struct ListedClient: Decodable {
    let id: String
    let label: String?

    init(from decoder: Decoder) throws {
        if let id = try? decoder.singleValueContainer().decode(String.self) {
            self.id = id
            self.label = nil
            return
        }
        let box = try decoder.container(keyedBy: CodingKeys.self)
        self.id = try box.decode(String.self, forKey: .id)
        self.label = try box.decodeIfPresent(String.self, forKey: .label)
    }

    enum CodingKeys: String, CodingKey {
        case id, label
    }
}
