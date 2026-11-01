// The Inbox with nothing waiting: Mando (hopping while the menu bar sparkles, once a day), All filed ✓ and
// today's recap — memos, minutes, projects, ★ — counted on this Mac from cpa rec ls. Without memos today, the
// hint. The recap is read again when the day turns (checked each minute), and one of another day is never
// shown.

import CardputerCore
import SwiftUI

struct InboxZeroView: View {
    @Bindable var model: AppModel

    var body: some View {
        TimelineView(.periodic(from: .now, by: 60)) { _ in
            let today = InboxZero.day(model.now, calendar: model.services.calendar)
            content(today: today)
                .task(id: today) { await model.loadRecap() }
        }
    }

    func content(today: String) -> some View {
        VStack(spacing: Theme.grid * 2) {
            HoppingMando(hopping: model.sparkling)
            Text(tr("都归好了 ✓")).font(Theme.title).foregroundStyle(Theme.fg)
            if let recap = model.recap, recap.day == today, recap.anything {
                Text(tr("今天")).font(Theme.small).foregroundStyle(Theme.dim)
                HStack(spacing: 10) {
                    RecapTile(value: recap.memos, label: tr("语音便条"))
                    RecapTile(value: recap.minutes, label: tr("分钟"))
                    RecapTile(value: recap.projects, label: tr("项目"))
                    RecapTile(value: recap.stars, label: "★")
                }
            } else {
                Text(tr("新的录音取回以后会出现在这里。按住设备上的 G0 说一段试试。"))
                    .font(Theme.body).foregroundStyle(Theme.ink2).multilineTextAlignment(.center).frame(maxWidth: 420)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(Theme.inset * 2)
    }
}

/// Mando's hop pose; while `hopping`, three small hops that land where they started (a still pose in a
/// snapshot, and with Reduce Motion).
struct HoppingMando: View {
    let hopping: Bool
    @Environment(\.isSnapshot) private var snapshot
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var up = false

    var body: some View {
        MandoView(pose: .hop, scale: MandoPose.hop.frame.height > 50 ? 2.5 : 3)
            .offset(y: up ? -10 : 0)
            .task(id: hopping) { await hop() }
    }

    /// Each hop goes up and comes down; cancelled (the sparkle ended, the view left) it lands at once.
    func hop() async {
        defer { up = false }
        guard hopping, !snapshot, !reduceMotion else { return }
        for _ in 0..<3 {
            withAnimation(.easeOut(duration: 0.22)) { up = true }
            try? await Task.sleep(nanoseconds: 220_000_000)
            withAnimation(.easeIn(duration: 0.22)) { up = false }
            try? await Task.sleep(nanoseconds: 300_000_000)
            if Task.isCancelled { return }
        }
    }
}

/// One number of the recap, big, with its word under it.
struct RecapTile: View {
    let value: Int
    let label: String

    var body: some View {
        VStack(spacing: 4) {
            Text(String(value)).font(.system(size: 26, weight: .semibold, design: .rounded)).monospacedDigit()
                .foregroundStyle(Theme.accent)
            Text(label).font(Theme.small).foregroundStyle(Theme.ink2).lineLimit(1)
        }
        .frame(width: 92)
        .padding(.vertical, 12)
        .background(Theme.card, in: RoundedRectangle(cornerRadius: Theme.radius))
        .overlay(RoundedRectangle(cornerRadius: Theme.radius).stroke(Theme.line, lineWidth: 1))
    }
}
