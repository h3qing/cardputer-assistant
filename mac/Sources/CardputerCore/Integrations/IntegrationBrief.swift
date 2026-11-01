// "Copy the connection guide": the Integrations page as one Markdown text to paste into any app's coding
// agent. With it an app connects the way this one does: the absolute cpa path, the commands and the rules,
// the contracts, the stdio MCP server and the notes dir's files.

import Foundation

extension IntegrationCatalog {
    public static func brief(features: Capabilities?, cpaPath: String?, cpaHome: String?, notes: String?) -> String {
        let cpa = cpaPath ?? "cpa"
        let parts: [[String]] = [
            [tr("# 接入 Cardputer"), "",
             tr("Cardputer 的数据只经下面这些公开接口给出。照着做，你的 app 和 Cardputer Assistant for Mac 用的是同一套。"), ""],
            cliSection(features: features, cpa: cpa, cpaPath: cpaPath),
            mcpSection(cpa: cpa, cpaHome: cpaHome),
            filesSection(notes: notes),
        ]
        return parts.joined().joined(separator: "\n") + "\n"
    }

    static func cliSection(features: Capabilities?, cpa: String, cpaPath: String?) -> [String] {
        let commands = cliCommands(features: features, cpaPath: cpaPath).map { command -> String in
            switch command.readiness {
            case .ready: return "- `\(command.text)`"
            case .needs(let what): return tr("- `{0}`（{1}）", command.text, what)
            }
        }
        let rules = tr("规矩：") + cliRules.joined(separator: tr("；")) + tr("；退出码非 0 时读 stdout 里的错误文档（`ok:false`）。")
        let head = [tr("## 命令行 + JSON"), "", tr("cpa 在 `{0}`。从 Finder 打开的 app 的 PATH 里没有它：用这个绝对路径。", cpa), ""]
        let tail = ["", rules,
                    tr("契约：仓库的 protocol/filing/v1/README.md（待归档）、protocol/recording/v1/README.md（文件和转写程序）。"), ""]
        return head + commands + tail
    }

    static func mcpSection(cpa: String, cpaHome: String?) -> [String] {
        let body = McpJSON.generic(cpaPath: cpa, cpaHome: cpaHome).body
        return [tr("## MCP（stdio，默认只读）"), "", tr("命令 `{0}`，参数 `mcp serve`。已有 mcpServers 时只把 cardputer 这一项并进去：", cpa),
                "", "```json", body, "```", ""]
    }

    static func filesSection(notes: String?) -> [String] {
        let layout = fileLayout.map { "    " + $0 }
        return [tr("## 文件（只读）"), "", tr("笔记目录 `{0}`：", notes ?? "~/CardputerNotes"), ""] + layout
            + ["", tr("写只有一种：跑 cpa 的命令。不要碰 cpad 的私有文件和 socket。")]
    }
}
