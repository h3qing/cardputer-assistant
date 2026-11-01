// What went wrong, in the app's language, said where it happened and saying what to do (docs/24 §2.1,
// feedback). A failed command names its tool (cpa or cpa-stt) and, for the few commands where it is known,
// the likely cause.

extension Labels {
    public static func failure(_ error: CpaError) -> String {
        switch error {
        case .notFound(let tool): return tr("没找到 {0}", tool.rawValue)
        case .launchFailed(let tool): return tr("{0} 启动失败", tool.rawValue)
        case .timedOut(let name): return tr("{0} 没有及时回答", tool(of: name))
        case .outputTooLarge(let name): return tr("{0} 的输出太大，没有读", tool(of: name))
        case .unsupported: return tr("这个 cpa 还没有这个命令：先更新 cpa")
        case .failed(let name, let code):
            let said = tr("{0} 出错了（退出码 {1}）", tool(of: name), code)
            return hint(for: name).map { said + tr("：") + $0 } ?? said
        case .badOutput: return tr("cpa 的输出看不懂：app 和 cpa 的版本可能对不上")
        case .refused(let failure): return refusal(failure)
        case .refusedDest(let failure): return DestFeedback.refusal(failure)
        }
    }

    /// "cpa-stt use" → "cpa-stt".
    static func tool(of name: String) -> String {
        name.split(separator: " ").first.map(String.init) ?? "cpa"
    }

    /// The usual cause of a command's failure, where one is known (exit 2 is a bad value or path, G6).
    static func hint(for name: String) -> String? {
        switch name {
        case "cpa export": return tr("目标要是新的，不能在 iCloud 同步的文件夹（桌面、文稿、iCloud 云盘）或笔记目录里")
        case "cpa-stt use": return tr("多半是缺模型，先选模型文件")
        case "cpa host name": return tr("名字最多 24 字节（8 个汉字）")
        default: return nil
        }
    }

    static func refusal(_ failure: FilingFailure) -> String {
        switch (failure.code, failure.field) {
        case (.badArg, "other"): return tr("这是别的电脑的项目，要先确认")
        case (.badArg, "id"): return tr("会议里有别人的话，不能交给 AI")
        case (.badArg, "name"): return tr("名字不行：1–24 字节（最多 8 个汉字），不以 - 开头")
        case (.badArg, "folder"): return tr("这个目录不在了，或者不能用")
        case (.badArg, "slot"): return tr("号要是 1–9")
        case (.badArg, _): return tr("参数不对")
        case (.unknownID, _): return tr("找不到这段录音")
        case (.unknownProject, _): return tr("这台 Mac 没有这个项目")
        case (.noText, _): return tr("还没转写，先归档或等转写")
        case (.already, "queue"): return tr("交给 AI 的那一项还在排队或已经开始了")
        case (.already, "slot"): return tr("这个号已经有项目了，或者 9 个号都满了")
        case (.already, "key"): return tr("这个 key 已经有项目在用")
        case (.already, _): return tr("这段录音现在的状态不能这样做")
        case (.unavailable, "cpad"): return tr("cpad 比 cpa 老：在终端跑 cpa service install")
        case (.unavailable, "projects"): return tr("读不了项目表")
        case (.unavailable, _): return tr("cpa 暂时用不了")
        }
    }
}
