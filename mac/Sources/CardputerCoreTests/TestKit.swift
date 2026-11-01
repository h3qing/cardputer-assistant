// A tiny in-house test runner: XCTest and swift-testing are not part of the Command Line Tools.
// A test returns the checks it made as values; the runner prints each failed one with its place and
// exits non-zero when any failed.

import CardputerCore
import Foundation

struct Check {
    let passed: Bool
    let message: String
    let location: String
}

struct TestCase {
    let name: String
    let body: () async throws -> [Check]
}

struct Suite {
    let name: String
    /// The language the suite's expected words are in: the runner sets it before each test. English unless a
    /// suite checks the Chinese.
    let language: Language
    let cases: [TestCase]

    init(name: String, language: Language = .en, cases: [TestCase]) {
        self.name = name
        self.language = language
        self.cases = cases
    }
}

/// Runs `body` with the app in `language`, then back to the language the suite set.
func inLanguage<T>(_ language: Language, _ body: () throws -> T) rethrows -> T {
    let before = L10n.language
    L10n.language = language
    defer { L10n.language = before }
    return try body()
}

func test(_ name: String, _ body: @escaping () async throws -> [Check]) -> TestCase {
    TestCase(name: name, body: body)
}

func expect(
    _ condition: Bool, _ message: @autoclosure () -> String = "expected true",
    file: StaticString = #fileID, line: UInt = #line
) -> Check {
    Check(passed: condition, message: condition ? "" : message(), location: "\(file):\(line)")
}

func expectEqual<T: Equatable>(
    _ got: T, _ want: T, _ note: String = "", file: StaticString = #fileID, line: UInt = #line
) -> Check {
    let ok = got == want
    let text = ok ? "" : "\(note.isEmpty ? "" : note + ": ")got \(String(reflecting: got)), want \(String(reflecting: want))"
    return Check(passed: ok, message: text, location: "\(file):\(line)")
}

func expectThrows<T>(
    _ note: String = "", file: StaticString = #fileID, line: UInt = #line,
    matching match: (Error) -> Bool = { _ in true }, _ body: () throws -> T
) -> Check {
    do {
        let value = try body()
        return Check(passed: false, message: "\(note): no error, got \(value)", location: "\(file):\(line)")
    } catch {
        let ok = match(error)
        return Check(passed: ok, message: ok ? "" : "\(note): unexpected error \(error)", location: "\(file):\(line)")
    }
}

func expectThrowsAsync<T>(
    _ note: String = "", file: StaticString = #fileID, line: UInt = #line,
    matching match: (Error) -> Bool = { _ in true }, _ body: () async throws -> T
) async -> Check {
    do {
        let value = try await body()
        return Check(passed: false, message: "\(note): no error, got \(value)", location: "\(file):\(line)")
    } catch {
        let ok = match(error)
        return Check(passed: ok, message: ok ? "" : "\(note): unexpected error \(error)", location: "\(file):\(line)")
    }
}

struct Outcome {
    let name: String
    let failures: [String]
}

enum Runner {
    static func run(_ suites: [Suite], filter: String?) async -> Int32 {
        let selected = suites.flatMap { suite in suite.cases.map { (suite, $0) } }
            .filter { suite, item in filter.map { "\(suite.name).\(item.name)".contains($0) } ?? true }
        let outcomes = await sequentially(selected[...])
        let failed = outcomes.filter { !$0.failures.isEmpty }
        for item in failed {
            print("FAIL \(item.name)")
            item.failures.forEach { print("     \($0)") }
        }
        print("\(outcomes.count) tests, \(outcomes.count - failed.count) passed, \(failed.count) failed")
        return failed.isEmpty && !outcomes.isEmpty ? 0 : 1
    }

    private static func sequentially(_ items: ArraySlice<(Suite, TestCase)>) async -> [Outcome] {
        guard let (suite, item) = items.first else { return [] }
        let first = await outcome(of: item, in: suite)
        return [first] + (await sequentially(items.dropFirst()))
    }

    private static func outcome(of item: TestCase, in suite: Suite) async -> Outcome {
        let name = "\(suite.name).\(item.name)"
        L10n.language = suite.language
        do {
            let checks = try await item.body()
            let failures = checks.filter { !$0.passed }.map { "\($0.location): \($0.message)" }
            return Outcome(name: name, failures: checks.isEmpty ? ["no checks made"] : failures)
        } catch {
            return Outcome(name: name, failures: ["threw \(error)"])
        }
    }
}
