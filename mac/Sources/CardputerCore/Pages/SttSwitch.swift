// The local transcription switch in Settings (docs/24 §2.5, D9): on = cpa-stt use, off = cpa-stt off. Grey,
// with the reason, when cpa-stt is not installed or another transcriber holds a fresh claim on recordings/
// (only one may transcribe at a time, docs/22 §4.10). Off with a missing model, or never turned on (no config
// yet, so cpa-stt cannot say whether a model is linked, G9), offers Choose a model file….

import Foundation

public enum SttSwitch: Hashable, Sendable {
    /// Grey: cpa-stt is not there (or does not answer).
    case unavailable(String)
    /// Grey: another transcriber holds recordings/.
    case taken(String)
    case off(model: ModelState)
    case on

    public enum ModelState: Hashable, Sendable {
        case ok
        /// A path of the config is gone, or the self-test said model_missing.
        case missing
        /// Never turned on: no config yet.
        case firstTime
    }

    public static var notInstalled: String { tr("装好 cpa-stt 才能本地转写") }
    public static let ownName = "cpa-stt"

    public static func of(features: Capabilities?, stt: SttStatus?, claim: TranscriberClaim?,
                          now: Date) -> SttSwitch {
        guard features?.stt == .available, let stt else { return .unavailable(notInstalled) }
        if stt.on {
            return .on
        }
        if let app = claim?.app, app != ownName, IntegrationCatalog.isFresh(claim, now: now) {
            let name = DisplaySafe.excerpt(app, limit: 16) ?? Labels.unknown
            return .taken(tr("{0} 正在转写，两个只能开一个", name))
        }
        if !stt.missing.isEmpty || stt.blocked == "model_missing" {
            return .off(model: .missing)
        }
        return .off(model: stt.selftestOK == nil ? .firstTime : .ok)
    }

    /// Choose a model file… shows.
    public var needsModel: Bool {
        if case .off(let model) = self {
            return model != .ok
        }
        return false
    }

    public var isOn: Bool {
        self == .on
    }

    public var isEnabled: Bool {
        switch self {
        case .on, .off: return true
        case .unavailable, .taken: return false
        }
    }

    /// The line under a grey switch.
    public var reason: String? {
        switch self {
        case .unavailable(let text), .taken(let text): return text
        case .off(.missing): return tr("缺模型：先选一个模型文件")
        case .off(.firstTime): return tr("第一次开：先选一个模型文件（whisper 的 .bin）")
        case .off(.ok): return nil
        case .on: return nil
        }
    }
}
