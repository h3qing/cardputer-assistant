// The Integrations page (docs/24 §2.4, docs/25 §5.7): first where spoken words go (Destinations: any app, one
// click, once cpa dest exists), then the ways any app reads Cardputer's data, for developers. Each card has a
// sentence, whether it works on this Mac now (or what comes first) and the exact text to copy, with this
// Mac's cpa path filled in. Nothing here is specific to any app: every client, this app included, uses the
// same commands. brief() puts it all into one Markdown text to paste into any app's coding agent.

import Foundation

public enum Readiness: Hashable, Sendable {
    case ready
    /// What comes first.
    case needs(String)
}

public enum CardKind: String, CaseIterable, Hashable, Sendable {
    case destinations, mcp, cli, files, transcriber, export
}

/// One command to copy, and whether this cpa runs it today.
public struct CopyCommand: Hashable, Sendable, Identifiable {
    public let text: String
    public let readiness: Readiness

    public var id: String {
        text
    }
}

public struct IntegrationCard: Hashable, Sendable, Identifiable {
    public let kind: CardKind
    public let title: String
    public let sentence: String
    public let readiness: Readiness
    public let commands: [CopyCommand]
    /// More to read on the card: a layout, a contract, the steps of a flow.
    public let lines: [String]

    public var id: CardKind {
        kind
    }

    /// The five ways to read Cardputer's data, grouped under For developers.
    public var forDevelopers: Bool {
        kind != .destinations
    }
}

public enum IntegrationCatalog {
    public static var opening: String { tr("Cardputer 的数据对所有 app 开放；这个 app 自己也只用下面这些接口。") }
    public static var developers: String { tr("给开发者：读 Cardputer 的数据") }
    public static var afterFiling: String { tr("装好 cpa filing 后") }
    public static var recordingContract: String { tr("契约：仓库的 protocol/recording/v1/README.md") }
    public static var cliRules: [String] { [
        tr("参数数组，不经 shell"), tr("每条命令有超时"), tr("正文走 stdin（--text -）"), tr("v 不是 1 就整份拒绝"), tr("不认识的字段忽略"),
    ] }

    /// A claim older than this is stale (cpa-stt claim.FRESH_S).
    public static let claimFresh: TimeInterval = 300

    /// `cpaPath`: where cpa was found; the commands to copy carry it (an app opened from Finder has no
    /// ~/.local/bin on its PATH).
    public static func cards(features: Capabilities?, transcriber: TranscriberClaim?, now: Date,
                             cpaPath: String?) -> [IntegrationCard] {
        let cpa = features?.cpa == .available ? Readiness.ready : .needs(tr("要先装 cpa"))
        let someone = isFresh(transcriber, now: now) || features?.stt == .available
        let stt = someone ? Readiness.ready : .needs(tr("要先装 cpa-stt 或别的转写程序"))
        return [
            destinations(features),
            IntegrationCard(kind: .mcp, title: tr("AI 客户端（MCP）"),
                            sentence: tr("任何支持 MCP 的 AI 客户端用 cpa mcp serve 读录音；默认只读，每次调用都记下来。"),
                            readiness: cpa, commands: [], lines: []),
            IntegrationCard(kind: .cli, title: tr("有界面的 app（命令行 + JSON）"),
                            sentence: tr("任何 app 都可以跑这几条命令读写；这个 app 就是例子。"),
                            readiness: cpa, commands: cliCommands(features: features, cpaPath: cpaPath),
                            lines: [tr("契约：仓库的 protocol/filing/v1/README.md（待归档）"),
                                    tr("契约：仓库的 protocol/dest-cli/v1/README.md（送到别的 app）")]),
            IntegrationCard(kind: .files, title: tr("文件"),
                            sentence: tr("录音和转写是普通文件，笔记软件、Finder、grep 不用写代码就能读。"),
                            readiness: .ready, commands: [], lines: fileLayout),
            IntegrationCard(kind: .transcriber, title: tr("转写程序"),
                            sentence: tr("任何本地转写程序守录音契约的“转写程序”角色就能接；同一时间只有一个认领 recordings/。"),
                            readiness: stt, commands: [CopyCommand(text: path(cpaPath, "cpa-stt") + " status --json", readiness: stt)],
                            lines: [recordingContract + tr("（转写程序一节）")]),
            IntegrationCard(kind: .export, title: tr("导出"),
                            sentence: tr("把录音、转写和归档状态导出到一个文件夹或 zip，可以不带音频。"),
                            readiness: cpa, commands: [], lines: []),
        ]
    }

    public static var afterDest: String { tr("装好 cpa dest 后") }
    /// docs/25 §5.7: connecting an app is one click in a list, once cpa dest (R1) answers; until then the
    /// steps, grey.
    static func destinations(_ features: Capabilities?) -> IntegrationCard {
        IntegrationCard(
            kind: .destinations, title: tr("去向：说的话送到任何 app"),
            sentence: tr("接一个 app 只要在这里点一下：选一条便条按 ⌥ 加字母，字就进那个 app 的输入框；从不替你按发送。"),
            readiness: features?.canSend == true ? .ready : .needs(tr("要先有 cpa dest（R1）")), commands: [],
            lines: [tr("1. 这里列出这台 Mac 上能接的 app（cpa dest presets --installed）"),
                    tr("2. 选一个，选一个字母，点“加上”"),
                    tr("3. 点“送一条测试”，回答“出现了吗”“自动发出去了吗”"),
                    tr("有预填链接的 app 直接填进输入框；别的 app 把字放进剪贴板、调到前面，你按 ⌘V")])
    }

    static var fileLayout: [String] { [
        tr("recordings/<日期>/<时间>-<会话>/"),
        tr("  manifest.json · seg-NNN.wav（录音）"),
        tr("  transcript.json · transcript.md（逐句）"),
        tr("  result.json（memo.text 是便条全文）· summary.md（纪要）"),
        tr("recordings/.transcriber（谁在转写）"),
        recordingContract,
    ] }

    static func cliCommands(features: Capabilities?, cpaPath: String?) -> [CopyCommand] {
        let now = features?.cpa == .available ? Readiness.ready : .needs(tr("要先装 cpa"))
        let filing = features?.filing == .available ? now : .needs(afterFiling)
        let projects = features?.projectList == .available ? now : .needs(afterFiling)
        let dest = features?.canSend == true ? now : .needs(afterDest)
        let cpa = path(cpaPath, "cpa")
        return [
            ("status --json", now), ("rec ls --json", now), ("rec show SID --json", now),
            ("filing ls --json", filing), ("filing show ID --json", filing), ("filing file ID KEY --json", filing),
            ("project ls --json", projects), ("dest pending --json", dest), ("dest send REF --json", dest),
        ].map { CopyCommand(text: cpa + " " + $0.0, readiness: $0.1) }
    }

    /// cpa (or cpa-stt beside it) by the path it was found at; the bare name when it was not found.
    static func path(_ cpaPath: String?, _ tool: String) -> String {
        guard let cpaPath else { return tool }
        return ((cpaPath as NSString).deletingLastPathComponent as NSString).appendingPathComponent(tool)
    }

    /// Who claims recordings/ now, from recordings/.transcriber.
    public static func transcriberLine(_ claim: TranscriberClaim?, now: Date) -> String {
        guard let claim, let app = DisplaySafe.excerpt(claim.app, limit: 16) else { return tr("现在没有转写程序") }
        guard isFresh(claim, now: now) else {
            return tr("{0} 认领过，但 {1} 分钟没有心跳", app, Int(claimFresh / 60))
        }
        return tr("{0} 正在转写", app)
    }

    static func isFresh(_ claim: TranscriberClaim?, now: Date) -> Bool {
        guard let beat = claim?.beatAt.flatMap(Format.instant) else { return false }
        return now.timeIntervalSince(beat) <= claimFresh
    }
}
