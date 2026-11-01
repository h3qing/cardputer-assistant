// Device (docs/24 §2.3): one line in big words (linked · not linked · cpad is not running), the numbers (the
// failed ones offer Retry the failed ones), what every number 1-9 means (Empty when nothing), this Mac's
// name, and cpad's warnings. Never the device's own ids. Changes happen in the terminal; the commands are
// here to copy.

import CardputerCore
import SwiftUI

struct DeviceView: View {
    @Bindable var model: AppModel

    var body: some View {
        ThemedScroll {
            VStack(alignment: .leading, spacing: 16) {
                PageHeader(title: tr("设备"), subtitle: tr("Cardputer 和这台 Mac 之间的状态"))
                headline
                facts
                projectTable
                macCard
                warnings
            }
            .padding(.horizontal, 22)
            .padding(.vertical, 18)
        }
    }

    var light: StatusLight {
        model.menu.light
    }

    var headline: some View {
        HStack(spacing: 16) {
            MandoView(pose: light == .linked ? .review : (light == .down ? .think : .wait), scale: 1.25)
            VStack(alignment: .leading, spacing: 6) {
                HStack(spacing: 10) {
                    StatusDot(light: light, size: 12)
                    Text(model.status.map(Labels.deviceHeadline) ?? tr("读不到 cpa status"))
                        .font(.system(size: 24, weight: .bold)).foregroundStyle(Theme.fg)
                }
                Text(explain).font(Theme.body).foregroundStyle(Theme.ink2).fixedSize(horizontal: false, vertical: true)
                if model.status?.running == false {
                    CommandChip(command: "cpa service install", copy: model.copy).fixedSize()
                }
            }
            Spacer(minLength: 0)
        }
        .padding(Theme.inset)
        .background(Theme.card, in: RoundedRectangle(cornerRadius: Theme.radius))
    }

    var explain: String {
        switch light {
        case .linked: return tr("设备连着这台 Mac：录音会自动取回，交给 AI 的事会发到设备的看板上。")
        case .unlinked: return tr("设备现在没连这台 Mac。插上 USB 线就会自动连上；没连时录音先存在设备上。")
        case .down: return tr("cpad 是这台 Mac 上和设备说话的后台服务。它没在跑，录音就取不回来；这个 app 不启停它。")
        case .unknown: return tr("cpa status 一阵子没回答了，下面的数字可能是旧的。")
        }
    }

    var facts: some View {
        let items = DeviceFacts.facts(status: model.status, lastPull: model.lastPull, now: model.now,
                                      calendar: model.services.calendar)
        return LazyVGrid(columns: [GridItem(.adaptive(minimum: 170), spacing: 10)], spacing: 10) {
            ForEach(items, id: \.label) { fact in
                VStack(alignment: .leading, spacing: 4) {
                    Text(fact.label).font(Theme.small).foregroundStyle(Theme.dim)
                    HStack(spacing: 8) {
                        Text(fact.value).font(Theme.big).foregroundStyle(Theme.fg).lineLimit(1)
                        if fact.kind == .failed, fact.value != tr("0 段") {
                            RetryButton(model: model)
                        }
                    }
                }
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(Theme.card, in: RoundedRectangle(cornerRadius: 8))
            }
        }
    }

    var projectTable: some View {
        CardBox {
            HStack {
                SectionLabel(text: tr("1–9 是哪个项目（设备上按的数字）"))
                Spacer()
                CommandChip(command: "cpa project ls", copy: model.copy).fixedSize()
            }
            ForEach(model.projects.numbers) { row in
                HStack(spacing: 12) {
                    KeyCap(text: String(row.number), highlighted: row.slot != nil)
                    if let slot = row.slot {
                        Text(slot.name).font(Theme.bodyStrong).foregroundStyle(Theme.fg).frame(width: 160, alignment: .leading)
                        Text(slot.key).font(Theme.mono).foregroundStyle(Theme.ink2)
                        Spacer()
                        FolderPill(slot: slot)
                    } else {
                        Text(tr("空")).font(Theme.body).foregroundStyle(Theme.dim)
                        Spacer()
                    }
                }
                .padding(.vertical, 1)
            }
            Text(tr("0 是“没项目”。新建、改名、换号、连目录都在“项目”页。"))
                .font(Theme.small).foregroundStyle(Theme.dim)
        }
    }

    var macCard: some View {
        CardBox {
            SectionLabel(text: tr("这台 Mac"))
            HStack {
                Text(tr("名字")).font(Theme.body).foregroundStyle(Theme.ink2).frame(width: 80, alignment: .leading)
                Text(model.hostName ?? tr("还没起名")).font(Theme.bodyStrong).foregroundStyle(Theme.fg)
            }
            if model.features?.hostName != .available {
                Reason(text: tr("起名要 cpa host name（随 cpa filing 一起，还没合进 main）。"))
            } else if model.hostName == nil {
                Reason(text: tr("在设置里起个名字（家、公司）：每条录音会记下是哪台 Mac 取回的。"))
            }
            Reason(text: tr("不记网络：Wi-Fi 名字算行踪，不存。"))
        }
    }

    @ViewBuilder var warnings: some View {
        let lines = (model.status?.warnings ?? []).compactMap { DisplaySafe.excerpt($0, limit: 200) }
        if !lines.isEmpty || model.statusError != nil {
            CardBox {
                SectionLabel(text: tr("cpad 说"))
                ForEach(lines, id: \.self) { line in
                    Label(line, systemImage: "exclamationmark.triangle").font(Theme.body).foregroundStyle(Theme.warn)
                }
                if let error = model.statusError {
                    Label(error, systemImage: "xmark.octagon").font(Theme.body).foregroundStyle(Theme.error)
                }
            }
        }
    }
}
