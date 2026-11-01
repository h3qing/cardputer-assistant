// What `cpa mcp tools --json`, `cpa mcp audit --json` and `cpa mcp setup CLIENT` print (daemon mcp/cli.py),
// for the Integrations page: the tools (read or write), the last calls (which client, which tool; never
// content) and the snippet to paste into a client. The app shows them; it never writes another app's config.

import Foundation

public struct McpTool: Hashable, Sendable {
    public let name: String
    public let title: String
    /// A write tool: needs the client's approval each time.
    public let write: Bool
}

public struct McpCall: Hashable, Sendable {
    public let at: String?
    /// The client's own name, when it said one.
    public let client: String?
    /// The tool called, or the event (start, stop) or the method.
    public let what: String
    public let error: String?
}

struct ToolListing: Decodable {
    struct Annotations: Decodable {
        let readOnlyHint: Bool?
    }

    let name: String
    let title: String?
    let annotations: Annotations?
}

struct AuditLine: Decodable {
    struct Client: Decodable {
        let name: String?
    }

    let at: String?
    let client: Client?
    let event: String?
    let method: String?
    let tool: String?
    let error: String?
}

public enum McpJSON {
    public static func tools(_ data: Data) throws -> [McpTool] {
        let listings: [ToolListing] = try CpaJSON.decode(data)
        return listings.map { listing in
            McpTool(name: DisplaySafe.excerpt(listing.name, limit: 40) ?? "?",
                    title: DisplaySafe.excerpt(listing.title, limit: 80) ?? "",
                    write: listing.annotations?.readOnlyHint == false)
        }
    }

    /// One record per line, oldest first; a line that is not a record is skipped.
    public static func audit(_ data: Data) -> [McpCall] {
        String(decoding: data, as: UTF8.self).split(separator: "\n").compactMap { line in
            guard let record = try? JSONDecoder().decode(AuditLine.self, from: Data(line.utf8)) else { return nil }
            let what = record.tool ?? record.event ?? record.method ?? "?"
            return McpCall(at: record.at, client: DisplaySafe.excerpt(record.client?.name, limit: 40),
                           what: DisplaySafe.excerpt(what, limit: 40) ?? "?",
                           error: DisplaySafe.excerpt(record.error, limit: 80))
        }
    }

    /// `cpa mcp setup CLIENT` prints where it goes on its first line ("# … paste into …", "Add to …:") and
    /// what to paste below it. Each line display_safe, trailing blank lines dropped.
    public static func snippet(_ data: Data) -> McpSnippet {
        let lines = String(decoding: data, as: UTF8.self).split(separator: "\n", omittingEmptySubsequences: false)
            .map { DisplaySafe.clean(String($0)) }
        let kept = Array(lines.reversed().drop { $0.trimmingCharacters(in: .whitespaces).isEmpty }.reversed())
        guard let first = kept.first, isInstruction(first) else {
            return McpSnippet(where: nil, body: kept.joined(separator: "\n"))
        }
        let bare = first.hasPrefix("Add to ") ? String(first.dropFirst(7)) : first
        let place = bare.drop { $0 == "#" || $0 == " " }
        let trimmed = place.hasSuffix(":") ? String(place.dropLast()) : String(place)
        return McpSnippet(where: trimmed, body: kept.dropFirst().joined(separator: "\n"))
    }

    /// A first line that says where, not what: a comment or "Add to …".
    static func isInstruction(_ line: String) -> Bool {
        line.hasPrefix("#") || line.hasPrefix("Add to ")
    }

    /// Any other stdio MCP client (until `cpa mcp setup generic` exists, G8): the usual mcpServers shape.
    public static func generic(cpaPath: String, cpaHome: String?) -> McpSnippet {
        let env = cpaHome.map { ",\n      \"env\": {\"CPA_HOME\": \(quoted($0))}" } ?? ""
        let body = """
            {
              "mcpServers": {
                "cardputer": {
                  "type": "stdio",
                  "command": \(quoted(cpaPath)),
                  "args": ["mcp", "serve"]\(env)
                }
              }
            }
            """
        return McpSnippet(where: tr("客户端的 MCP 设置（多半是一个 JSON 文件的 mcpServers）"), body: body)
    }

    /// A JSON string literal, escaped by JSONSerialization.
    static func quoted(_ text: String) -> String {
        let data = (try? JSONSerialization.data(withJSONObject: [text], options: [.withoutEscapingSlashes])) ?? Data("[\"\"]".utf8)
        return String(String(decoding: data, as: UTF8.self).dropFirst().dropLast())
    }
}

/// What `cpa mcp setup CLIENT` says: where it goes, and the text to paste there.
public struct McpSnippet: Hashable, Sendable {
    public let `where`: String?
    public let body: String

    public init(where: String?, body: String) {
        self.where = `where`
        self.body = body
    }

    /// A JSON config (mcpServers): paste only the cardputer entry into one that has others.
    public var isJSON: Bool {
        body.hasPrefix("{")
    }
}

extension CpaClient {
    public func mcpTools() async throws -> [McpTool] {
        try await decoded(.mcpTools, McpJSON.tools)
    }

    public func mcpAudit(last: Int) async throws -> [McpCall] {
        McpJSON.audit(try await run(try .mcpAudit(last: last)))
    }

    public func mcpSetup(_ client: String) async throws -> McpSnippet {
        McpJSON.snippet(try await run(try .mcpSetup(client)))
    }

    /// The clients cpa can print a snippet for: its list (G8), else the choices of its help; empty when
    /// neither answers.
    public func mcpClients() async -> [McpClientEntry] {
        if let listed = try? McpClients.parse(try await run(.mcpSetupList)), !listed.isEmpty {
            return listed
        }
        let help = (try? await run(.mcpSetupHelp)).map { String(decoding: $0, as: UTF8.self) } ?? ""
        return McpClients.choices(help: help).map { McpClientEntry(id: $0) }
    }
}
