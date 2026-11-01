// The words after a send, a drop or a refusal (docs/25 §5.7): "Filled into <app> · press Return in <app> to
// send" for 5 s; "On the clipboard · press ⌘V in <app> within 2 minutes" stays until it is closed (the
// clipboard clears itself after 2 minutes) and offers Copy again. Every code cpa dest can answer has words
// and a next step; the app's name is the label cpa printed; a code this build does not know is shown as it
// is.

public enum DestFeedback {
    public static func sent(_ doc: DestSent) -> Toast {
        let label = doc.label
        switch doc.outcome.known {
        case .handed? where doc.via?.known == .url:
            return Toast(text: tr("已填进 {0} · 到 {1} 里按回车发出", label, label), isError: false)
        case .handed?:
            return Toast(text: tr("在剪贴板 · 2 分钟内到 {0} 按 ⌘V", label) + clipWhy(doc), isError: false, sticky: true)
        case .delivered?:
            return Toast(text: tr("已送到 {0}", label), isError: false)
        case .failed?, nil:
            let code = code(doc.code ?? doc.outcome.raw, label: label)
            return Toast(text: tr("没送到 {0}：", label) + code + tr("；还在等你送里"), isError: true)
        }
    }

    static func clipWhy(_ doc: DestSent) -> String {
        switch doc.why?.known {
        case .draftBusy?: return tr("（{0} 里还有上一条草稿）", doc.label)
        case .tooLong?: return tr("（太长，链接放不下）")
        case nil: return doc.why.map { tr("（{0}）", $0.raw) } ?? ""
        }
    }

    /// A failed delivery's code, in words, with what to do next.
    public static func code(_ code: String, label: String = tr("那个 app")) -> String {
        switch code {
        case "E_DEST_MISSING": return tr("{0} 不在这台 Mac 上了", label)
        case "E_HANDLER_CHANGED": return tr("处理链接的 app 换了或签名变了：到 集成 → 去向 重新加一次")
        case "E_CLIP_PLAIN": return tr("剪贴板写不了：再按一次 ⌘↩ 试试")
        case "E_DRAFT_BUSY": return tr("{0} 里可能还有上一条草稿：打开 剪贴板 去向，或 30 分钟后再送", label)
        case "E_TOO_LONG": return tr("太长，链接放不下：打开 剪贴板 去向就会改用剪贴板")
        case "E_DEST_TIMEOUT": return tr("{0} 没响应：打开 {1} 后再按 ⌘↩", label, label)
        case "E_INTERRUPTED": return tr("上次没送完：再按 ⌘↩ 重送")
        case "E_BAD_TEXT": return tr("没有能送的字")
        case "E_DEST_IO": return tr("写不进去：在终端跑 cpa dest ls 看看")
        case "E_DEST_PATH": return tr("那个文件的位置不能写：在终端跑 cpa dest ls 看看")
        case "E_INTERNAL": return tr("cpa 出错了：在终端跑 cpa dest pending 看看")
        default: return code
        }
    }

    /// A refused command (nothing was written): what to do.
    public static func refusal(_ failure: DestFailure) -> String {
        switch failure.code {
        case "E_NEEDS_DEVICE": return tr("这个去向可能会自己发出去：先到 集成 → 去向 送一条测试，测过才能从 Mac 送")
        case "E_NO_TEXT": return tr("还没有字：转写开着吗？（cpa-stt use）")
        case "E_TOO_LONG": return tr("太长了：这个去向放不下")
        case "E_BUSY": return tr("这一条正在送")
        case "E_ALREADY": return tr("已经送过去了：再按一次就再送一次")
        case "E_DEST_OFF": return tr("这个去向关着或还没允许离开这台 Mac")
        case "E_OTHER_PROFILE": return tr("这个去向属于另一个档案")
        case "E_UNKNOWN_DEST": return tr("没有这个去向")
        case "E_UNKNOWN_ID": return tr("等你送里没有这一条了")
        case "E_UNKNOWN_PROJECT": return tr("这台 Mac 没有这个项目")
        case "E_UNAVAILABLE": return tr("去向表用不了：在终端跑 cpa dest ls 看看")
        case "E_BAD_ARG": return tr("参数不对")
        default: return tr("没送：") + failure.code
        }
    }

    public static var dropped: String { tr("不送了") }
}
