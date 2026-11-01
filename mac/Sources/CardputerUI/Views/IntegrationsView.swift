// Integrations (docs/24 §2.4, docs/25 §5.7): "any app can connect" made into buttons. First Destinations
// (send what you say to any app; one click per app, live once cpa dest answers: DestinationsCard), then, for
// developers, one card per public way to read Cardputer's data with whether it works here now and the exact
// text to copy, and Copy the connection guide: all of it as Markdown for any app's coding agent. The MCP tabs
// and snippets come from `cpa mcp setup`; the app never edits another app's config and names no app in its
// code.

import CardputerCore
import SwiftUI

struct IntegrationsView: View {
    @Bindable var model: AppModel

    var cards: [IntegrationCard] {
        IntegrationCatalog.cards(features: model.features, transcriber: model.transcriber, now: model.now,
                                 cpaPath: model.services.cpaPath)
    }

    var body: some View {
        ThemedScroll {
            VStack(alignment: .leading, spacing: 16) {
                PageHeader(title: tr("集成"), subtitle: IntegrationCatalog.opening)
                ForEach(cards.filter { !$0.forDevelopers }) { card in
                    CardBox {
                        CardTitle(card: card)
                        Text(card.sentence).font(Theme.body).foregroundStyle(Theme.ink2).fixedSize(horizontal: false, vertical: true)
                        if model.features?.canSend == true {
                            DestinationsCardBody(model: model)
                        } else {
                            DestinationSteps(lines: card.lines)
                        }
                    }
                }
                DevelopersHeader(model: model)
                IntegrationOverview(cards: cards.filter(\.forDevelopers))
                ForEach(cards.filter(\.forDevelopers)) { card in
                    CardBox {
                        CardTitle(card: card)
                        Text(card.sentence).font(Theme.body).foregroundStyle(Theme.ink2)
                            .fixedSize(horizontal: false, vertical: true)
                        content(card)
                    }
                }
            }
            .padding(.horizontal, 22)
            .padding(.vertical, 18)
        }
    }

    @ViewBuilder func content(_ card: IntegrationCard) -> some View {
        switch card.kind {
        case .mcp: McpCardBody(model: model)
        case .cli: CliCardBody(model: model, card: card)
        case .files: FilesCardBody(model: model, card: card)
        case .transcriber: TranscriberCardBody(model: model, card: card)
        case .export: ExportCardBody(model: model)
        case .destinations: EmptyView()
        }
    }
}

/// The steps of the one-click flow, grey until cpa dest exists.
struct DestinationSteps: View {
    let lines: [String]

    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            ForEach(lines, id: \.self) { line in
                Text(line).font(Theme.body).foregroundStyle(Theme.dim).fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Theme.bg.opacity(0.6), in: RoundedRectangle(cornerRadius: 8))
    }
}

/// For developers and the brief to copy.
struct DevelopersHeader: View {
    @Bindable var model: AppModel
    @State private var copied = false

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            VStack(alignment: .leading, spacing: 3) {
                Text(IntegrationCatalog.developers).font(Theme.title).foregroundStyle(Theme.fg)
                Text(tr("命令、规矩、契约和 MCP 设置，贴给任何 app 的编程助手，它就照这些接。"))
                    .font(Theme.small).foregroundStyle(Theme.dim)
            }
            Spacer()
            Button {
                model.copyBrief()
                copied = true
                Task {
                    try? await Task.sleep(nanoseconds: 1_500_000_000)
                    copied = false
                }
            } label: {
                Label(copied ? tr("已复制") : tr("复制接入说明"), systemImage: copied ? "checkmark" : "doc.on.doc")
            }
            .buttonStyle(ThemedButtonStyle(kind: .primary))
        }
        .padding(.top, 6)
    }
}

/// All five ways in at a glance: what each is for and whether it works on this Mac now.
struct IntegrationOverview: View {
    let cards: [IntegrationCard]

    var body: some View {
        HStack(spacing: 10) {
            ForEach(cards) { card in
                VStack(alignment: .leading, spacing: 6) {
                    HStack(spacing: 6) {
                        Image(systemName: CardTitle.symbol(card.kind)).foregroundStyle(Theme.accent)
                        Text(Self.name(card.kind)).font(Theme.bodyStrong).foregroundStyle(Theme.fg).lineLimit(1)
                    }
                    switch card.readiness {
                    case .ready: Label(tr("能用"), systemImage: "checkmark.circle.fill").foregroundStyle(Theme.ok)
                    case .needs(let what):
                        Label(what, systemImage: "arrow.down.circle").foregroundStyle(Theme.warn).lineLimit(1)
                    }
                }
                .font(Theme.small)
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(Theme.card, in: RoundedRectangle(cornerRadius: 8))
            }
        }
    }

    static func name(_ kind: CardKind) -> String {
        switch kind {
        case .destinations: return tr("去向")
        case .mcp: return tr("AI 客户端")
        case .cli: return tr("命令行 + JSON")
        case .files: return tr("文件")
        case .transcriber: return tr("转写程序")
        case .export: return tr("导出")
        }
    }
}

struct CardTitle: View {
    let card: IntegrationCard

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: Self.symbol(card.kind)).foregroundStyle(Theme.accent).frame(width: 20)
            Text(card.title).font(Theme.title).foregroundStyle(Theme.fg)
            Spacer()
            switch card.readiness {
            case .ready: Pill(text: tr("能用"), tone: .ok, symbol: "checkmark")
            case .needs(let what): Pill(text: what, tone: .warn, symbol: "arrow.down.circle")
            }
        }
    }

    static func symbol(_ kind: CardKind) -> String {
        switch kind {
        case .destinations: return "paperplane"
        case .mcp: return "sparkles"
        case .cli: return "terminal"
        case .files: return "folder"
        case .transcriber: return "waveform.badge.mic"
        case .export: return "square.and.arrow.up"
        }
    }
}
