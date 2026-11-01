// swift run CardputerCoreTests [substring]: runs every suite (or the tests whose name contains it).

import CardputerCore
import Foundation

let suites: [Suite] = [
    formatTests,
    contractTests,
    statusTests,
    commandTests,
    clientTests,
    featureTests,
    projectMapTests,
    projectsTests,
    l10nTests,
    englishTests,
    locatorTests,
    runnerTests,
    processClientTests,
    notesTests,
    inboxTests,
    rulesTests,
    refreshTests,
    keysTests,
    actionsTests,
    mcpTests,
    pagesTests,
    notesMoreTests,
    themeTests,
    labelsTests,
    suggestionTests,
    destContractTests,
    destCommandTests,
    destChoiceTests,
    destFlowTests,
    delightTests,
]

// Each suite runs in its own language (Suite.language): English, or Chinese where its expected words are.
let filter = CommandLine.arguments.dropFirst().first
exit(await Runner.run(suites, filter: filter))
