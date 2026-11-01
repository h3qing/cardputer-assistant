// The developer cards of Integrations: MCP (a tab per client cpa knows, plus any other stdio client; where it
// goes as a caption and only the paste text copied), the command line (cpa's absolute path, each command with
// whether this cpa has it), the files, the transcriber and export.

import CardputerCore
import SwiftUI

struct McpCardBody: View {
    @Bindable var model: AppModel

    var state: IntegrationsState {
        model.integrations
    }

    var tabs: [McpClientEntry] {
        state.clients + [McpClients.generic]
    }

    var selected: String {
        state.client ?? tabs.first?.id ?? McpClients.generic.id
    }

    var body: some View {
        FlowLayout(spacing: 6, lineSpacing: 6) {
            ForEach(tabs) { client in
                Chip(text: client.label, on: selected == client.id) {
                    model.integrations = state.with { $0.client = client.id }
                }
            }
        }
        if selected == McpClients.generic.id {
            GenericClient(model: model)
        } else if let snippet = state.snippets[selected] {
            SnippetView(snippet: snippet, copy: model.copy)
            Text(tr("由 cpa mcp setup {0} 生成，填好了这台 Mac 的 cpa 路径。", selected))
                .font(Theme.small).foregroundStyle(Theme.dim)
        } else {
            Reason(text: state.loaded ? tr("cpa mcp setup 没有回答") : tr("正在问 cpa mcp setup…"))
        }
        ToolsAndCalls(model: model)
    }
}

/// Where the snippet goes, then the text to paste (only that is copied).
struct SnippetView: View {
    let snippet: McpSnippet
    let copy: (String) -> Void

    var body: some View {
        if let place = snippet.where {
            Label(tr("贴到：") + place, systemImage: "arrow.down.doc").font(Theme.small).foregroundStyle(Theme.ink2)
                .fixedSize(horizontal: false, vertical: true)
        }
        SnippetBox(text: snippet.body, copy: copy)
        if snippet.isJSON {
            Text(tr("已有 mcpServers 时只把 cardputer 这一项并进去。")).font(Theme.small).foregroundStyle(Theme.dim)
        }
    }
}

/// Any other MCP client: what every stdio client asks for, and the usual JSON.
struct GenericClient: View {
    @Bindable var model: AppModel

    var body: some View {
        let cpa = model.services.cpaPath ?? "cpa"
        VStack(alignment: .leading, spacing: 3) {
            fact(tr("传输"), tr("stdio（本机进程）"))
            fact(tr("命令"), cpa)
            fact(tr("参数"), "mcp serve")
            if let home = model.services.cpaHome {
                fact(tr("环境变量"), "CPA_HOME=" + home)
            }
        }
        SnippetView(snippet: McpJSON.generic(cpaPath: cpa, cpaHome: model.services.cpaHome), copy: model.copy)
    }

    func fact(_ label: String, _ value: String) -> some View {
        HStack(spacing: 10) {
            Text(label).font(Theme.small).foregroundStyle(Theme.dim).frame(width: 64, alignment: .leading)
            Text(value).font(Theme.monoSmall).foregroundStyle(Theme.fg).textSelection(.enabled)
        }
    }
}

struct ToolsAndCalls: View {
    @Bindable var model: AppModel

    var body: some View {
        let state = model.integrations
        if !state.tools.isEmpty {
            let writes = state.tools.filter(\.write).map(\.name)
            Label(tr("{0} 个工具，{1} 个只读", state.tools.count, state.tools.count - writes.count)
                  + (writes.isEmpty ? "" : tr("；会写的：{0}（每次要你在客户端里批准）", writes.joined(separator: tr("、")))),
                  systemImage: "wrench.and.screwdriver")
                .font(Theme.small).foregroundStyle(Theme.ink2)
        }
        SectionLabel(text: tr("最近的调用（cpa mcp audit；不记内容）"))
        if state.calls.isEmpty {
            Text(tr("还没有 AI 客户端调用过。")).font(Theme.small).foregroundStyle(Theme.dim)
        }
        ForEach(Array(state.calls.suffix(3).reversed().enumerated()), id: \.offset) { _, call in
            HStack(spacing: 10) {
                Text(call.at.flatMap(Format.instant).map { Format.relative($0, now: model.now, calendar: model.services.calendar) } ?? "—")
                    .font(Theme.small).foregroundStyle(Theme.dim).frame(width: 80, alignment: .leading)
                Text(call.client ?? "—").font(Theme.small).foregroundStyle(Theme.ink2).frame(width: 130, alignment: .leading)
                Text(call.what).font(Theme.monoSmall).foregroundStyle(Theme.fg)
                if let error = call.error {
                    Pill(text: Labels.mcpError(error), tone: .error)
                }
            }
        }
    }

}

struct CliCardBody: View {
    @Bindable var model: AppModel
    let card: IntegrationCard

    var body: some View {
        if let path = model.services.cpaPath {
            HStack(spacing: 8) {
                Text(tr("cpa 在")).font(Theme.body).foregroundStyle(Theme.ink2)
                CommandChip(command: path, shown: shown(path), copy: model.copy).fixedSize()
            }
            Text(tr("从 Finder 打开的 app 的 PATH 里没有它：用绝对路径。复制的命令里就是绝对路径。"))
                .font(Theme.small).foregroundStyle(Theme.dim)
        }
        VStack(alignment: .leading, spacing: 6) {
            ForEach(card.commands) { command in
                HStack(spacing: 8) {
                    CommandChip(command: command.text, shown: shown(command.text), copy: model.copy)
                    if case .needs(let what) = command.readiness {
                        Pill(text: what, tone: .warn, symbol: "arrow.down.circle")
                    }
                }
            }
        }
        Text(tr("规矩：") + IntegrationCatalog.cliRules.joined(separator: " · ") + tr("。"))
            .font(Theme.small).foregroundStyle(Theme.dim).fixedSize(horizontal: false, vertical: true)
        ForEach(card.lines, id: \.self) { line in
            Text(line).font(Theme.small).foregroundStyle(Theme.dim)
        }
    }
}

extension CliCardBody {
    /// The home folder as ~, for reading; the copy keeps the absolute path.
    func shown(_ text: String) -> String {
        displayPath(text, home: model.services.home)
    }
}

struct FilesCardBody: View {
    @Bindable var model: AppModel
    let card: IntegrationCard

    var body: some View {
        HStack(spacing: 10) {
            Text(model.notes.map { displayPath($0.root.path, home: model.services.home) } ?? tr("没找到笔记目录"))
                .font(Theme.mono).foregroundStyle(Theme.fg)
                .lineLimit(1).truncationMode(.middle)
            Spacer()
            Button { model.reveal(model.notes?.root) } label: { Label(tr("在 Finder 中显示"), systemImage: "folder") }
                .buttonStyle(ThemedButtonStyle(compact: true))
                .disabled(model.notes == nil)
        }
        VStack(alignment: .leading, spacing: 2) {
            ForEach(card.lines, id: \.self) { line in
                Text(line).font(line.hasPrefix(tr("契约")) ? Theme.small : Theme.monoSmall)
                    .foregroundStyle(line.hasPrefix(tr("契约")) ? Theme.dim : Theme.ink2)
            }
        }
    }
}

struct TranscriberCardBody: View {
    @Bindable var model: AppModel
    let card: IntegrationCard

    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: "person.wave.2").foregroundStyle(Theme.ink2)
            Text(IntegrationCatalog.transcriberLine(model.transcriber, now: model.now))
                .font(Theme.bodyStrong).foregroundStyle(Theme.fg)
            Text(tr("（recordings/.transcriber）")).font(Theme.small).foregroundStyle(Theme.dim)
        }
        ForEach(card.commands) { command in
            CommandChip(command: command.text, shown: displayPath(command.text, home: model.services.home), copy: model.copy)
        }
        ForEach(card.lines, id: \.self) { line in
            Text(line).font(Theme.small).foregroundStyle(Theme.dim)
        }
    }
}

struct ExportCardBody: View {
    @Bindable var model: AppModel

    var state: IntegrationsState {
        model.integrations
    }

    var body: some View {
        HStack(spacing: 16) {
            Toggle(isOn: Binding(get: { state.exportZip }, set: { on in model.integrations = state.with { $0.exportZip = on } })) {
                Text(tr("打成 zip")).font(Theme.body).foregroundStyle(Theme.ink2)
            }
            .toggleStyle(ThemedSwitchStyle()).frame(width: 130)
            Toggle(isOn: Binding(get: { state.exportAudio }, set: { on in model.integrations = state.with { $0.exportAudio = on } })) {
                Text(tr("带音频")).font(Theme.body).foregroundStyle(Theme.ink2)
            }
            .toggleStyle(ThemedSwitchStyle()).frame(width: 120)
            Spacer()
            Button { model.export() } label: { Label(tr("导出到…"), systemImage: "square.and.arrow.up") }
                .buttonStyle(ThemedButtonStyle(kind: .primary))
                .disabled(model.features?.cpa != .available)
        }
        Text(command).font(Theme.monoSmall).foregroundStyle(Theme.dim)
        Text(tr("目标要是新的；不能在 iCloud 同步的文件夹（桌面、文稿、iCloud 云盘）或笔记目录里。"))
            .font(Theme.small).foregroundStyle(Theme.dim)
        if let note = state.exportNote {
            Label(note.text, systemImage: note.isError ? "exclamationmark.circle" : "checkmark.circle")
                .font(Theme.small).foregroundStyle(note.isError ? Theme.error : Theme.ok)
        }
    }

    var command: String {
        (["cpa export"] + (state.exportZip ? ["--zip"] : []) + (state.exportAudio ? [] : ["--no-audio"]) + [tr("目标")])
            .joined(separator: " ")
    }
}
