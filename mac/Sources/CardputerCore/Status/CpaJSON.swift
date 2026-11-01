// The JSON of the cpa commands outside the filing contract: `cpa status --json` (an object) and
// `cpa rec ls --json` (an array). A wrong type is refused with its path, like the contract's documents;
// one pass of JSONDecoder (a large archive's rec ls is megabytes).

import Foundation

public enum CpaJSON {
    public static func status(_ data: Data) throws -> CpaStatus {
        try decode(data)
    }

    public static func recordings(_ data: Data) throws -> [RecRow] {
        try decode(data)
    }

    /// `cpa-stt status --json`.
    public static func stt(_ data: Data) throws -> SttStatus {
        try decode(data)
    }

    static func decode<T: Decodable>(_ data: Data) throws -> T {
        do {
            return try JSONDecoder().decode(T.self, from: data)
        } catch let error as DecodingError {
            throw ContractError.invalid(field: FilingContract.path(of: error))
        }
    }
}
