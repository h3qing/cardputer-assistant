// Which number 1-9 is which project: the same slots this Mac gives the device, from `cpa project ls --json`;
// before that command exists, the keys of `cpa status --json` stand in for the names.

public struct ProjectSlot: Hashable, Sendable, Identifiable {
    /// 1-9
    public let slot: Int
    public let key: String
    /// display_safe
    public let name: String
    /// false when the project's linked folder is gone.
    public let ok: Bool
    /// A code folder is linked: only then can the device start an agent session for it.
    public let linked: Bool
    /// The linked folder's last path component (never a path).
    public let folder: String?

    public var id: String {
        key
    }

    /// What the button says: "3 acmeshop".
    public var label: String {
        "\(slot) \(name)"
    }

    public init(slot: Int, key: String, name: String, ok: Bool, linked: Bool = true, folder: String? = nil) {
        self.slot = slot
        self.key = key
        self.name = name
        self.ok = ok
        self.linked = linked
        self.folder = folder
    }
}

public struct ProjectMap: Hashable, Sendable {
    /// By slot, at most one per number.
    public let slots: [ProjectSlot]

    public static let empty = ProjectMap(slots: [])

    init(slots: [ProjectSlot]) {
        let valid = slots.filter { (1...9).contains($0.slot) && Patterns.isProjectKey($0.key) }
        let firsts = valid.enumerated().filter { index, item in
            !valid.prefix(index).contains { $0.slot == item.slot }
        }
        self.slots = firsts.map(\.element).sorted { $0.slot < $1.slot }
    }

    public init(_ list: ProjectList) {
        self.init(slots: list.projects.map { entry in
            let name = DisplaySafe.excerpt(entry.name, limit: 24) ?? entry.key
            return ProjectSlot(slot: entry.slot, key: entry.key, name: name, ok: entry.ok, linked: entry.linked,
                               folder: entry.folder.flatMap { DisplaySafe.excerpt($0, limit: 64) })
        })
    }

    public init(_ status: CpaStatus) {
        self.init(slots: status.projects.map { ProjectSlot(slot: $0.slot, key: $0.key, name: $0.key, ok: $0.ok) })
    }

    public func slot(_ number: Int) -> ProjectSlot? {
        slots.first { $0.slot == number }
    }

    public func project(key: String) -> ProjectSlot? {
        slots.first { $0.key == key }
    }

    /// "2 Cardputer" for this Mac's key; another Mac's key as it is.
    public func label(key: String) -> String {
        project(key: key)?.label ?? key
    }

    /// The lowest number without a project, nil when all nine are taken.
    public var firstFree: Int? {
        (1...9).first { slot($0) == nil }
    }

    /// Every number 1-9 in order, each with its project or nil (Empty).
    public var numbers: [NumberRow] {
        (1...9).map { NumberRow(number: $0, slot: slot($0)) }
    }

    /// From `cpa project ls --json` when this cpa has it (`listed`, from the feature probe), else from `status`.
    public static func load(_ client: CpaClient, status: CpaStatus?, listed: Bool) async throws -> ProjectMap {
        guard listed else { return status.map(ProjectMap.init) ?? .empty }
        return ProjectMap(try await client.projects())
    }
}

/// One of the nine numbers the device shows, with its project or none.
public struct NumberRow: Hashable, Sendable, Identifiable {
    public let number: Int
    public let slot: ProjectSlot?

    public var id: Int {
        number
    }
}
