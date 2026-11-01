// swift-tools-version:5.9
// Cardputer Assistant for Mac (docs/24): a plain client of the public interfaces (cpa … --json, the notes
// dir's open files, cpa-stt). Built with the Command Line Tools only: swift build; tests: swift run
// CardputerCoreTests; screens: swift run CardputerShots --out out/shots; the .app: scripts/build-app.sh.
import PackageDescription

let package = Package(
    name: "CardputerAssistant",
    platforms: [.macOS(.v14)],
    products: [
        .library(name: "CardputerCore", targets: ["CardputerCore"]),
        .executable(name: "CardputerAssistant", targets: ["CardputerAssistant"]),
    ],
    targets: [
        .target(name: "CardputerCore", path: "Sources/CardputerCore"),
        .target(name: "CardputerUI", dependencies: ["CardputerCore"], path: "Sources/CardputerUI"),
        .executableTarget(
            name: "CardputerAssistant",
            dependencies: ["CardputerUI"],
            path: "Sources/CardputerAssistant"
        ),
        .executableTarget(
            name: "CardputerShots",
            dependencies: ["CardputerUI"],
            path: "Sources/CardputerShots"
        ),
        .executableTarget(
            name: "CardputerCoreTests",
            dependencies: ["CardputerCore"],
            path: "Sources/CardputerCoreTests"
        ),
    ]
)
