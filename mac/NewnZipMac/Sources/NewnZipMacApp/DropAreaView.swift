import SwiftUI
import UniformTypeIdentifiers

struct DropAreaView: View {
    let title: String
    let subtitle: String
    let isProcessing: Bool
    let isCompleted: Bool
    let progressValue: Double?
    let statusText: String
    let onDropURLs: ([URL]) -> Void
    let onTap: () -> Void
    let onCancel: () -> Void

    var body: some View {
        RoundedRectangle(cornerRadius: 14)
            .fill(Color(nsColor: .textBackgroundColor))
            .overlay {
                VStack(spacing: 12) {
                    if isProcessing {
                        Text("\(Int((progressValue ?? 0.0) * 100))%")
                            .font(.system(size: 64, weight: .bold))
                            .monospacedDigit()
                        Text(statusText)
                            .font(.callout)
                            .foregroundStyle(.secondary)
                            .multilineTextAlignment(.center)
                            .padding(.horizontal, 20)
                        Button(role: .cancel) {
                            onCancel()
                        } label: {
                            Label(Localizer.shared.text("button.cancel_operation"), systemImage: "xmark.circle")
                        }
                        .keyboardShortcut(.cancelAction)
                    } else if isCompleted {
                        Image(systemName: "checkmark.circle")
                            .font(.system(size: 54))
                            .foregroundStyle(.green)
                        Text(statusText)
                            .font(.title3.weight(.semibold))
                    } else {
                        Image(systemName: "square.and.arrow.down.on.square")
                            .font(.system(size: 48))
                            .foregroundStyle(.secondary)
                        Text(title)
                            .font(.title2.weight(.semibold))
                        Text(subtitle)
                            .foregroundStyle(.secondary)
                            .multilineTextAlignment(.center)
                            .padding(.horizontal, 20)
                    }
                }
                .padding(24)
            }
            .overlay(
                RoundedRectangle(cornerRadius: 14)
                    .stroke(Color(nsColor: .separatorColor), lineWidth: 1)
            )
            .opacity(isProcessing ? 0.82 : 1.0)
            .contentShape(RoundedRectangle(cornerRadius: 14))
            .onTapGesture {
                guard !isProcessing else { return }
                onTap()
            }
            .dropDestination(for: URL.self) { urls, _ in
                guard !isProcessing else { return false }
                onDropURLs(urls)
                return true
            }
    }
}
