// Settings (docs/24 §2.5): the local transcription switch (cpa-stt use / off, model file, its LaunchAgent),
// the language (docs/24 §2.7: follow the Mac, English or Chinese), this Mac's name (cpa host name --json;
// grey on a cpa without it), cpad's state with the command to copy (the app never starts or stops it), and
// where cpa and the notes dir were found.

import CardputerCore
import SwiftUI

struct SettingsView: View {
    @Bindable var model: AppModel

    var body: some View {
        ThemedScroll {
            VStack(alignment: .leading, spacing: 16) {
                PageHeader(title: tr("设置"), subtitle: model.cpaVersion.map { tr("{0} · 这个 app 只跑你点的 cpa 命令", $0) })
                SttCard(model: model)
                languageCard
                macName
                cpadCard
                pathsCard
                if let note = model.settings.note {
                    ToastView(toast: note)
                }
            }
            .padding(.horizontal, 22)
            .padding(.vertical, 18)
        }
    }

    var languageCard: some View {
        CardBox {
            SectionLabel(text: tr("语言 · Language"))
            HStack(spacing: 8) {
                ForEach(LanguageChoice.allCases, id: \.self) { choice in
                    Button(choice.title()) { model.setLanguage(choice) }
                        .buttonStyle(ThemedButtonStyle(selected: model.languageChoice == choice))
                }
            }
            Reason(text: tr("跟随 Mac：Mac 的第一个语言是中文就用中文，否则用 English。"))
        }
    }

    var macName: some View {
        CardBox {
            SectionLabel(text: tr("这台 Mac 的名字"))
            HStack(spacing: 8) {
                ForEach([tr("家"), tr("公司")], id: \.self) { name in
                    Button(name) { model.setHostName(name) }
                        .buttonStyle(ThemedButtonStyle(selected: model.hostName == name))
                }
                Text(model.hostName.map { tr("现在：{0}", $0) } ?? tr("还没起名")).font(Theme.body).foregroundStyle(Theme.ink2)
            }
            .disabled(model.features?.hostName != .available)
            if model.features?.hostName != .available {
                Reason(text: tr("起名要 cpa host name（随 cpa filing 一起，还没合进 main）：起了名字，每条录音会记下是哪台 Mac 取回的。"))
            } else {
                Reason(text: tr("起了名字，每条录音会记下是哪台 Mac 取回的；名字最多 8 个汉字。"))
            }
        }
    }

    var cpadCard: some View {
        CardBox {
            SectionLabel(text: tr("cpad 服务"))
            HStack(spacing: 10) {
                StatusDot(light: model.status?.running == true ? .linked : .down)
                Text(model.status?.running == true ? tr("在跑") : tr("没在跑")).font(Theme.bodyStrong).foregroundStyle(Theme.fg)
                if let version = model.status?.version {
                    Text(tr("版本 {0}", version)).font(Theme.body).foregroundStyle(Theme.ink2)
                }
                Spacer()
                CommandChip(command: "cpa service install", copy: model.copy).fixedSize()
            }
            Reason(text: tr("没跑或太旧时，在终端跑上面这条。app 不启停 cpad：重启会打断正在取回的录音。"))
        }
    }

    func shown(_ path: String?) -> String? {
        path.map { displayPath($0, home: model.services.home) }
    }

    var pathsCard: some View {
        CardBox {
            SectionLabel(text: tr("在哪里找到的"))
            PathRow(label: "cpa", path: shown(model.services.cpaPath) ?? tr("没找到"), choose: model.chooseCpa)
            PathRow(label: "cpa-stt", path: shown(model.services.sttPath) ?? tr("没装"), choose: nil)
            PathRow(label: tr("笔记目录"), path: shown(model.notes?.root.path) ?? tr("没找到"), choose: model.chooseNotes)
            HStack {
                Text(tr("按顺序找：你选的、~/.local/bin、PATH、/opt/homebrew/bin、/usr/local/bin。"))
                    .font(Theme.small).foregroundStyle(Theme.dim)
                Spacer()
                Button { model.rediscover() } label: { Label(tr("重新查找"), systemImage: "arrow.clockwise") }
                    .buttonStyle(ThemedButtonStyle(compact: true))
            }
        }
    }
}

struct PathRow: View {
    let label: String
    let path: String
    let choose: (() -> Void)?

    var body: some View {
        HStack(spacing: 10) {
            Text(label).font(Theme.body).foregroundStyle(Theme.ink2).frame(width: 80, alignment: .leading)
            Text(path).font(Theme.mono).foregroundStyle(Theme.fg).lineLimit(1).truncationMode(.middle)
            Spacer()
            if let choose {
                Button(tr("选择…"), action: choose).buttonStyle(ThemedButtonStyle(kind: .quiet, compact: true))
            }
        }
    }
}

struct SttCard: View {
    @Bindable var model: AppModel

    var state: SttSwitch {
        SttSwitch.of(features: model.features, stt: model.stt, claim: model.transcriber, now: model.now)
    }

    var body: some View {
        CardBox {
            Toggle(isOn: Binding(get: { state.isOn }, set: { model.setLocalTranscription($0) })) {
                VStack(alignment: .leading, spacing: 3) {
                    Text(tr("本地转写")).font(Theme.title).foregroundStyle(Theme.fg)
                    Text(tr("录音只在这台 Mac 上转成文字，不联网（cpa-stt）。同一时间只能有一个转写程序。"))
                        .font(Theme.body).foregroundStyle(Theme.ink2)
                }
            }
            .toggleStyle(ThemedSwitchStyle())
            .disabled(!state.isEnabled || model.settings.busy)
            if let reason = state.reason {
                Reason(text: reason)
            }
            if state.needsModel || (model.settings.useFailed && !state.isOn) {
                Button { model.chooseModel() } label: { Label(tr("选模型文件…"), systemImage: "doc.badge.plus") }
                    .buttonStyle(ThemedButtonStyle(kind: .primary))
            }
            if case .unavailable = state {
                HStack(spacing: 8) {
                    Text(tr("在仓库目录里运行：")).font(Theme.small).foregroundStyle(Theme.dim)
                    CommandChip(command: Setup.updateCpa, copy: model.copy).fixedSize()
                }
            }
            if let stt = model.stt {
                SttFacts(stt: stt, now: model.now, calendar: model.services.calendar)
            }
        }
    }
}

struct SttFacts: View {
    let stt: SttStatus
    let now: Date
    let calendar: Calendar

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            row(tr("转写命令"), stt.command ?? tr("没设"))
            row(tr("沙箱"), stt.sandbox == true ? tr("开着（不联网，只写自己的文件）") : tr("没开"))
            row(tr("自检"), selftest)
            row(tr("登录后自动运行"), stt.service == true ? tr("装好了") : tr("还没装"))
            if let blocked = stt.blocked {
                row(tr("跑不了"), Labels.blockedReason(blocked))
            }
            if let recent = stt.recent.last {
                row(tr("最近一段"), tr("{0} 的录音，用了 {1}", Format.duration(ms: recent.durMs), Format.duration(ms: recent.tookMs)))
            }
        }
        .padding(.top, 4)
    }

    var selftest: String {
        switch stt.selftestOK {
        case true?:
            let when = stt.selftestAt.flatMap(Format.instant).map { tr("（{0}）", Format.relative($0, now: now, calendar: calendar)) } ?? ""
            return tr("过了") + when
        case false?: return tr("没过")
        case nil: return tr("还没跑")
        }
    }

    func row(_ label: String, _ value: String) -> some View {
        HStack(spacing: 10) {
            Text(label).font(Theme.small).foregroundStyle(Theme.dim).frame(width: 100, alignment: .leading)
            Text(value).font(Theme.small).foregroundStyle(Theme.ink2)
        }
    }
}
