// Projects (docs/24 §2.6, docs/25 §8): projects are named groups this Mac makes; the device files voice under
// them by number 1-9 and it stays in this app until you hand it on yourself. Each button runs one
// `cpa project … --json`; its answer is the list after the change, shown at once.

import CardputerCore
import Foundation

extension AppModel {
    /// Add: the typed name on `slot`, or on the first free number.
    public func addProject(slot: Int? = nil) {
        let name = projectsPane.newName
        editProjects({ try .projectAdd(name: name, slot: slot) }, done: { tr("加上了“{0}”", name.trimmingCharacters(in: .whitespaces)) }) {
            $0.newName = ""
        }
    }

    public func startRename(_ key: String) {
        let name = projects.project(key: key)?.name ?? ""
        projectsPane = projectsPane.with {
            $0.renaming = key
            $0.renameText = name
            $0.confirmRemove = nil
        }
    }

    public func cancelRename() {
        projectsPane = projectsPane.with { $0.renaming = nil }
    }

    public func confirmRename() {
        guard let key = projectsPane.renaming else { return }
        let name = projectsPane.renameText
        editProjects({ try .projectRename(key, name: name) }, done: { tr("改好了") }) { $0.renaming = nil }
    }

    /// The project already on `slot` takes this one's number (cpa project move swaps).
    public func moveProject(_ key: String, to slot: Int) {
        guard (1...9).contains(slot), projects.project(key: key)?.slot != slot else { return }
        editProjects({ try .projectMove(key, to: slot) }, done: { tr("换到 {0} 了", slot) })
    }

    /// Link folder…: a folder chosen in the system panel; hooks and agent sessions then work for this project.
    public func linkFolder(_ key: String) {
        Task {
            guard let url = await system.chooseFolder(tr("选这个项目的代码目录（可以不连：不连也能归档语音）")) else { return }
            editProjects({ try .projectFolder(key, path: url.path) }, done: { tr("连上了 {0}", url.lastPathComponent) })
        }
    }

    public func unlinkFolder(_ key: String) {
        editProjects({ try .projectFolder(key, path: nil) }, done: { tr("断开了目录") })
    }

    /// Remove asks once more; the second click removes it. Its recordings keep the key.
    public func removeProject(_ key: String) {
        guard projectsPane.confirmRemove == key else {
            projectsPane = projectsPane.with { $0.confirmRemove = key }
            return
        }
        editProjects({ try .projectRemove(key) }, done: { tr("删掉了（录音还在，仍记着原来的项目）") }) { $0.confirmRemove = nil }
    }

    public func cancelRemove() {
        projectsPane = projectsPane.with { $0.confirmRemove = nil }
    }

    func editProjects(_ build: @escaping () throws -> CpaCommand, done: @escaping () -> String,
                      after: @escaping (inout ProjectsState) -> Void = { _ in }) {
        let command: CpaCommand
        do {
            command = try build()
        } catch {
            projectsPane = projectsPane.with { $0.toast = Toast(text: Feedback.failure(error), isError: true) }
            return
        }
        projectsPane = projectsPane.with { $0.busy = true }
        Task {
            await runProjectEdit(command, done: done(), after: after)
        }
    }

    func runProjectEdit(_ command: CpaCommand, done: String, after: (inout ProjectsState) -> Void) async {
        do {
            let list = try await services.client.editProjects(command)
            projects = ProjectMap(list)
            projectsPane = projectsPane.with {
                after(&$0)
                $0.busy = false
                $0.toast = Toast(text: done, isError: false)
            }
        } catch {
            projectsPane = projectsPane.with {
                $0.busy = false
                $0.toast = Toast(text: Feedback.failure(error), isError: true)
            }
        }
    }
}
