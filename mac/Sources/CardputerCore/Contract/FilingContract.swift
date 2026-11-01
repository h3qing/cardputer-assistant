// Reads the documents of protocol/filing/v1 the way check_filing.validate does: not an object → E_INVALID
// (root); v not the integer 1 → E_VERSION, nothing else read; otherwise the schema and the rules, with the
// path of the first fault (rows[0].text). Unknown fields are ignored; unknown values of the extensible
// enums are kept (Open). One pass of JSONDecoder per document.

import Foundation

public enum ContractError: Error, Hashable, Sendable {
    case version
    case invalid(field: String)

    public var code: String {
        switch self {
        case .version: return "E_VERSION"
        case .invalid: return "E_INVALID"
        }
    }

    public var field: String {
        switch self {
        case .version: return "v"
        case .invalid(let field): return field
        }
    }
}

public enum FilingContract {
    public static func list(_ data: Data) throws -> FilingList {
        let doc: FilingList = try decode(data, versioned: true)
        try FilingRules.list(doc)
        return doc
    }

    public static func item(_ data: Data) throws -> FilingItem {
        let doc: FilingItem = try decode(data, versioned: true)
        try FilingRules.item(doc)
        return doc
    }

    public static func result(_ data: Data) throws -> FilingResult {
        let doc: FilingResult = try decode(data, versioned: true, ok: true)
        try FilingRules.result(doc)
        return doc
    }

    public static func failure(_ data: Data) throws -> FilingFailure {
        let doc: FilingFailure = try decode(data, versioned: true, ok: false)
        try FilingRules.failure(doc)
        return doc
    }

    public static func projects(_ data: Data) throws -> ProjectList {
        let doc: ProjectList = try decode(data, versioned: true)
        try FilingRules.projects(doc)
        return doc
    }

    /// `cpa host name --json`: this Mac's name, nil when it has none.
    public static func host(_ data: Data) throws -> HostName {
        let doc: HostName = try decode(data, versioned: true)
        try FilingRules.host(doc)
        return doc
    }

    /// One line of the export's data/filing.jsonl: a row on its own, without v.
    public static func row(_ data: Data) throws -> FilingRow {
        let doc: FilingRow = try decode(data, versioned: false)
        try FilingRules.row(doc, at: "")
        return doc
    }

    /// `ok`: the value the document's ok must have (result: true, error: false), nil when it has none.
    static func decode<T: Decodable>(_ data: Data, versioned: Bool, ok: Bool? = nil) throws -> T {
        let decoder = JSONDecoder()
        decoder.userInfo[Envelope<T>.rulesKey] = EnvelopeRules(versioned: versioned, ok: ok)
        do {
            return try decoder.decode(Envelope<T>.self, from: data).doc
        } catch let error as ContractError {
            throw error
        } catch let error as DecodingError {
            throw ContractError.invalid(field: path(of: error))
        }
    }

    /// rows[0].text from a decoding error's coding path (and the missing key, if that was the fault).
    static func path(of error: DecodingError) -> String {
        switch error {
        case .keyNotFound(let key, let context):
            return render(context.codingPath + [key])
        case .typeMismatch(_, let context), .valueNotFound(_, let context), .dataCorrupted(let context):
            return render(context.codingPath)
        @unknown default:
            return "(root)"
        }
    }

    static func render(_ keys: [CodingKey]) -> String {
        let text = keys.reduce("") { path, key in
            if let index = key.intValue {
                return path + "[\(index)]"
            }
            return path.isEmpty ? key.stringValue : path + "." + key.stringValue
        }
        return text.isEmpty ? "(root)" : text
    }
}

struct EnvelopeRules: Sendable {
    let versioned: Bool
    let ok: Bool?
}

/// A contract document read in one pass: an object (else (root)), v the integer 1 (else E_VERSION, nothing
/// else read), ok the expected boolean (else ok), then the document's own fields.
struct Envelope<T: Decodable>: Decodable {
    static var rulesKey: CodingUserInfoKey {
        CodingUserInfoKey(rawValue: "cardputer.contract.rules")!
    }

    let doc: T

    init(from decoder: Decoder) throws {
        let fields = try decoder.container(keyedBy: AnyKey.self)
        let rules = decoder.userInfo[Self.rulesKey] as? EnvelopeRules ?? EnvelopeRules(versioned: false, ok: nil)
        if rules.versioned, (try? fields.decode(VersionOne.self, forKey: AnyKey("v"))) == nil {
            throw ContractError.version
        }
        if let want = rules.ok, (try? fields.decode(Bool.self, forKey: AnyKey("ok"))) != want {
            throw ContractError.invalid(field: "ok")
        }
        self.doc = try T(from: decoder)
    }
}

/// The integer 1: not "1", not true, not 1.5.
struct VersionOne: Decodable {
    init(from decoder: Decoder) throws {
        let value = try decoder.singleValueContainer()
        if (try? value.decode(Bool.self)) != nil {
            throw ContractError.version
        }
        guard try value.decode(Int.self) == 1 else { throw ContractError.version }
    }
}

struct AnyKey: CodingKey {
    let stringValue: String
    let intValue: Int? = nil

    init(_ name: String) {
        self.stringValue = name
    }

    init(stringValue: String) {
        self.stringValue = stringValue
    }

    init?(intValue: Int) {
        nil
    }
}

/// A document of a recording contract file (manifest.json, result.json, transcript.json): v the integer 1,
/// read in one pass; nil when it is not.
enum VersionedFile {
    static func decode<T: Decodable>(_ data: Data) -> T? {
        try? FilingContract.decode(data, versioned: true)
    }
}
