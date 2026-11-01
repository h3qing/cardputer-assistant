// The words for the contracts' states. A value this build does not know reads Unknown.

public enum Labels {
    public static var unknown: String { tr("未知") }
    public static func kind(_ kind: Open<RecordingKind>) -> String {
        switch kind.known {
        case .memo: return tr("便条")
        case .meeting: return tr("会议")
        case nil: return unknown
        }
    }

    public static func transcription(_ tx: Open<Transcription>) -> String {
        switch tx.known {
        case .pulling: return tr("拉取中")
        case .waiting: return tr("等转写")
        case .processing: return tr("转写中")
        case .blocked: return tr("转写程序跑不了")
        case .transcript: return tr("只有转写")
        case .done: return tr("有纪要")
        case .failed: return tr("转写失败")
        case .stale: return tr("超时没转")
        case nil: return unknown
        }
    }

    public static func filing(_ state: FilingState) -> String {
        switch state {
        case .new: return tr("待归档")
        case .filed: return tr("已归档")
        case .handed: return tr("已交给 AI")
        case .done: return tr("看过了")
        case .hidden: return tr("藏起来了")
        }
    }

    /// Whether it was recorded while linked to this Mac (env.link).
    public static func macLink(_ link: Open<MacLink>) -> String {
        switch link.known {
        case .here: return tr("录音时连着这台 Mac")
        case .unknown: return tr("录音时没连电脑")
        case nil: return unknown
        }
    }

    public static func intent(_ intent: Open<MemoIntent>) -> String {
        switch intent.known {
        case .note: return tr("笔记")
        case .todo: return tr("待办")
        case nil: return unknown
        }
    }
}
