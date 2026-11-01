// Settings: local transcription (cpa-stt use / off / setup / service install), this Mac's name (cpa host
// name), and the paths chosen for cpa and the notes dir. Each button runs the one command it names.

import CardputerCore
import Foundation

extension AppModel {
    /// On: cpa-stt use, then its LaunchAgent once the self-test passed. Off: cpa-stt off.
    public func setLocalTranscription(_ on: Bool) {
        settings = settings.with { $0.busy = true }
        Task {
            let note = on ? await turnSttOn() : await runStt(.sttOff, done: tr("本地转写关了"))
            _ = await loadStt()
            settings = settings.with {
                $0.busy = false
                $0.note = note
                $0.useFailed = on && note.isError
            }
        }
    }

    func turnSttOn() async -> Toast {
        let used = await runStt(.sttUse, done: tr("本地转写开了"))
        guard !used.isError else { return used }
        _ = await loadStt()
        guard stt?.selftestOK != false else { return Toast(text: tr("自检没过：看下面的原因"), isError: true) }
        guard stt?.service != true else { return used }
        return await runStt(.sttServiceInstall, done: tr("本地转写开了，登录后自动运行"))
    }

    func runStt(_ command: CpaCommand, done: String) async -> Toast {
        do {
            _ = try await services.client.run(command)
            return Toast(text: done, isError: false)
        } catch {
            return Toast(text: Feedback.failure(error), isError: true)
        }
    }

    /// cpa host name --json -- NAME, then the name cpa says it has now.
    public func setHostName(_ name: String) {
        Task {
            do {
                let named = try FilingContract.host(try await services.client.run(try .hostName(name))).host
                hostName = named
                settings = settings.with { $0.note = Toast(text: tr("这台 Mac 叫“{0}”了", named ?? name), isError: false) }
            } catch {
                settings = settings.with { $0.note = Toast(text: Feedback.failure(error), isError: true) }
            }
        }
    }

    /// Choose a model file…: cpa-stt setup --yes PATH links the file (no copy, no download), then turns it on.
    public func chooseModel() {
        Task {
            guard let url = await system.chooseFile(tr("选一个本地的 whisper 模型文件（.bin）")) else { return }
            do {
                _ = try await services.client.run(try .sttSetup(model: url.path))
                setLocalTranscription(true)
            } catch {
                settings = settings.with { $0.note = Toast(text: Feedback.failure(error), isError: true) }
            }
        }
    }

    public func chooseCpa() {
        Task {
            guard let url = await system.chooseFile(tr("选 cpa 程序（通常在 ~/.local/bin/cpa）")) else { return }
            let saved = Choices.saved
            rediscover(Choices(cpa: url.path, stt: saved.stt, notes: saved.notes))
        }
    }

    public func chooseNotes() {
        Task {
            guard let url = await system.chooseFolder(tr("选笔记目录（里面有 recordings/）")) else { return }
            let saved = Choices.saved
            rediscover(Choices(cpa: saved.cpa, stt: saved.stt, notes: url.path))
        }
    }
}

extension AppModel {
    /// Language: saved in this app's defaults; the windows redraw in it at once.
    public func setLanguage(_ choice: LanguageChoice) {
        UserDefaults.standard.set(choice.rawValue, forKey: LanguageChoice.defaultsKey)
        applyLanguage(choice)
    }

    func applyLanguage(_ choice: LanguageChoice) {
        L10n.language = choice.resolved()
        languageChoice = choice
        language = L10n.language
    }
}

extension LanguageChoice {
    static let defaultsKey = "language"

    /// What Settings picked last, or follow the Mac.
    static var saved: LanguageChoice {
        UserDefaults.standard.string(forKey: defaultsKey).flatMap(LanguageChoice.init(rawValue:)) ?? .system
    }
}

extension SettingsState {
    func with(_ change: (inout SettingsState) -> Void) -> SettingsState {
        var copy = self
        change(&copy)
        return copy
    }
}
