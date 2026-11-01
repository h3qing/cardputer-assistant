// ImageRenderer draws only what SwiftUI draws itself: a ScrollView's content, a text field or a list come
// out blank. Under `isSnapshot` (set by CardputerShots) the few such places draw a still copy instead:
// the scroll content clipped from the top, the search field as its words. Everything else is the same view.

import SwiftUI

private struct SnapshotKey: EnvironmentKey {
    static let defaultValue = false
}

extension EnvironmentValues {
    public var isSnapshot: Bool {
        get { self[SnapshotKey.self] }
        set { self[SnapshotKey.self] = newValue }
    }
}

/// A vertical ScrollView in the app; its content from the top, clipped, in a snapshot.
struct ThemedScroll<Content: View>: View {
    @Environment(\.isSnapshot) private var snapshot
    @ViewBuilder let content: Content

    var body: some View {
        if snapshot {
            // The content at its own height, its top at the top; whatever does not fit is cut off.
            VStack(spacing: 0) {
                content
            }
            .fixedSize(horizontal: false, vertical: true)
            .frame(minWidth: 0, maxWidth: .infinity, minHeight: 0, maxHeight: .infinity, alignment: .top)
            .clipped()
        } else {
            ScrollView(.vertical) {
                content
            }
            .scrollIndicators(.automatic)
        }
    }
}

/// The search box: a text field in the app, its words (or the prompt) in a snapshot.
struct SearchField: View {
    @Environment(\.isSnapshot) private var snapshot
    let prompt: String
    @Binding var text: String
    var focus: FocusState<Bool>.Binding
    let submit: () -> Void

    var body: some View {
        HStack(spacing: 6) {
            Image(systemName: "magnifyingglass").foregroundStyle(Theme.dim)
            if snapshot {
                Text(text.isEmpty ? prompt : text).foregroundStyle(text.isEmpty ? Theme.dim : Theme.fg)
                Spacer(minLength: 0)
            } else {
                TextField(prompt, text: $text)
                    .textFieldStyle(.plain)
                    .foregroundStyle(Theme.fg)
                    .focused(focus)
                    .onSubmit(submit)
            }
            KeyCap(text: "/")
        }
        .font(Theme.body)
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
        .background(Theme.raised, in: RoundedRectangle(cornerRadius: 7))
    }
}

/// A rounded text field in the app; in a snapshot its words (or the prompt) in the same box.
struct BoxField: View {
    @Environment(\.isSnapshot) private var snapshot
    let prompt: String
    @Binding var text: String
    let submit: () -> Void

    var body: some View {
        if snapshot {
            Text(text.isEmpty ? prompt : text)
                .font(Theme.body)
                .foregroundStyle(text.isEmpty ? Theme.dim : Theme.fg)
                .lineLimit(1)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.horizontal, 7)
                .padding(.vertical, 4)
                .background(Theme.raised, in: RoundedRectangle(cornerRadius: 5))
        } else {
            TextField(prompt, text: $text)
                .textFieldStyle(.roundedBorder)
                .onSubmit(submit)
        }
    }
}
