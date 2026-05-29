import SwiftUI

enum DropPanelAction {
    case standard
    case passwordCompress
    case splitCompress
}

struct DropAreaView: View {
    let title: String
    let subtitle: String
    let isProcessing: Bool
    let isCompleted: Bool
    let progressValue: Double?
    let statusText: String
    let onDropURLs: ([URL], DropPanelAction) -> Void
    let onTap: (DropPanelAction) -> Void
    let onCancel: () -> Void

    var body: some View {
        Group {
            if isProcessing || isCompleted {
                primarySurface {
                    stateContent
                }
            } else {
                VStack(spacing: 12) {
                    primarySurface {
                        idlePrimaryContent
                    }
                    .frame(maxHeight: .infinity)
                    .dropDestination(for: URL.self) { urls, _ in
                        onDropURLs(urls, .standard)
                        return true
                    }
                    .onTapGesture {
                        onTap(.standard)
                    }

                    HStack(spacing: 12) {
                        secondarySurface(
                            title: "암호로 압축",
                            systemImage: "lock.fill",
                            action: .passwordCompress
                        )
                        secondarySurface(
                            title: "분할 압축",
                            systemImage: "square.split.2x1.fill",
                            action: .splitCompress
                        )
                    }
                    .frame(height: 110)
                }
            }
        }
    }

    private var stateContent: some View {
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
            } else {
                Image(systemName: "checkmark.circle")
                    .font(.system(size: 54))
                    .foregroundStyle(.green)
                Text(statusText)
                    .font(.title3.weight(.semibold))
            }
        }
        .padding(24)
    }

    private var idlePrimaryContent: some View {
        VStack(spacing: 12) {
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
        .padding(24)
    }

    private func secondarySurface(title: String, systemImage: String, action: DropPanelAction) -> some View {
        primarySurface {
            VStack(spacing: 8) {
                Image(systemName: systemImage)
                    .font(.system(size: 24, weight: .semibold))
                    .foregroundStyle(.secondary)
                Text(title)
                    .font(.headline)
                    .multilineTextAlignment(.center)
                Text("드래그 또는 클릭")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .padding(18)
        }
        .dropDestination(for: URL.self) { urls, _ in
            onDropURLs(urls, action)
            return true
        }
        .onTapGesture {
            onTap(action)
        }
    }

    private func primarySurface<Content: View>(@ViewBuilder content: () -> Content) -> some View {
        RoundedRectangle(cornerRadius: 14)
            .fill(Color(nsColor: .textBackgroundColor))
            .overlay { content() }
            .overlay(
                RoundedRectangle(cornerRadius: 14)
                    .stroke(Color(nsColor: .separatorColor), lineWidth: 1)
            )
            .opacity(isProcessing ? 0.82 : 1.0)
            .contentShape(RoundedRectangle(cornerRadius: 14))
    }
}
