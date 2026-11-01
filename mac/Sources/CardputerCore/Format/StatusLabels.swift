// Chinese words for `cpa status` and `cpa rec ls`.

import Foundation

extension Labels {
    /// One line for the device page and the menu: Device linked · firmware v0.4.0 · Device not linked · cpad
    /// is not running …
    public static func deviceHeadline(_ status: CpaStatus) -> String {
        guard status.running else { return tr("cpad 没在跑") }
        switch status.link?.known {
        case .ready:
            return status.device?.fw.map { tr("设备连着 · 固件 ") + firmware($0) } ?? tr("设备连着")
        case .handshaking:
            return tr("设备握手中")
        case .offline:
            return tr("设备没连")
        case .paused:
            return tr("设备暂停 {0} 秒", Int((status.pausedS ?? 0).rounded(.up)))
        case nil:
            return tr("设备状态未知")
        }
    }

    /// The same line without the firmware, for the menu: Device linked · Device not linked · cpad is not
    /// running …
    public static func deviceShort(_ status: CpaStatus) -> String {
        status.running && status.link?.known == .ready ? tr("设备连着") : deviceHeadline(status)
    }

    /// "cpa-fw/0.4.0" → "v0.4.0"
    public static func firmware(_ fw: String) -> String {
        let version = fw.split(separator: "/").last.map(String.init) ?? fw
        return version.first?.isNumber == true ? "v" + version : version
    }

    public static func recState(_ state: Open<RecState>) -> String {
        switch state.known {
        case .onDevice: return tr("在设备上")
        case .pulling: return tr("拉取中")
        case .archived: return tr("等转写")
        case .processing: return tr("转写中")
        case .blocked: return tr("转写程序跑不了")
        case .transcript: return tr("只有转写")
        case .done: return tr("有纪要")
        case .failed: return tr("转写失败")
        case .stale: return tr("超时没转")
        case nil: return unknown
        }
    }

    /// Why the transcriber cannot run (status.json reason, rec ls reason).
    public static func blockedReason(_ reason: String) -> String {
        switch reason {
        case "busy": return tr("别的转写程序在忙")
        case "model_missing": return tr("缺模型")
        case "disabled": return tr("转写关着")
        case "engine": return tr("转写命令跑不起来")
        default: return unknown
        }
    }

    /// An MCP call's error in cpa mcp audit.
    public static func mcpError(_ code: String) -> String {
        code == "E_DISABLED" ? tr("被拒：写入没开") : (DisplaySafe.excerpt(code, limit: 40) ?? unknown)
    }

    /// result.json's error.code of a failed transcription.
    public static func resultError(_ code: String) -> String {
        switch code {
        case "E_STT": return tr("转写命令出错")
        case "E_AUDIO": return tr("音频读不了")
        case "E_LLM": return tr("总结出错")
        case "E_IO": return tr("读写文件出错")
        case "E_VERSION": return tr("录音格式版本不对")
        case "E_INVALID": return tr("录音文件不合格")
        default: return DisplaySafe.excerpt(code, limit: 24) ?? unknown
        }
    }
}
