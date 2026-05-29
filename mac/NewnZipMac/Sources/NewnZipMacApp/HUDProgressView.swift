import AppKit
import SwiftUI

struct HUDProgressView: View {
    let command: HUDCommand
    var terminatesWhenDone = true

    @StateObject private var settings = AppSettings.shared
    @State private var title = ""
    @State private var detail = ""
    @State private var progressValue: Double = 0
    @State private var isCompleted = false
    @State private var isFailed = false
    @State private var operationTask: Task<Void, Never>?

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 10) {
                ProgressView(value: progressValue)
                    .progressViewStyle(.linear)
                Text("\(Int(progressValue * 100))%")
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
                    .frame(width: 40, alignment: .trailing)
            }

            Text(title)
                .font(.headline)

            Text(detail)
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .truncationMode(.middle)

            HStack {
                Spacer()
                Button(Localizer.shared.text("simple.cancel")) {
                    cancel()
                }
                .disabled(isCompleted || isFailed)
            }
        }
        .padding(16)
        .frame(width: 360)
        .onAppear {
            start()
        }
    }

    private func start() {
        title = command.kind == .compress
            ? Localizer.shared.text("hud.compressing")
            : Localizer.shared.text("hud.extracting")
        detail = command.urls.first?.lastPathComponent ?? ""

        let currentFormat = settings.defaultFormat
        let currentZipMethod = settings.zipMethod
        let currentArchivePassword = settings.archivePassword.trimmingCharacters(in: .whitespacesAndNewlines)
        let currentConflictPolicy = resolvedConflictPolicy(
            format: currentFormat,
            splitSizeMB: 0
        )

        operationTask = Task.detached(priority: .userInitiated) {
            do {
                switch command.kind {
                case .compress:
                    _ = try await EngineBridge.compress(
                        urls: command.urls,
                        format: currentFormat,
                        zipMethod: currentZipMethod,
                        splitSizeMB: 0,
                        password: currentArchivePassword.isEmpty ? nil : currentArchivePassword,
                        conflictPolicy: currentConflictPolicy
                    ) { line in
                        Task { @MainActor in
                            handleEngineLine(line)
                        }
                    }
                case .extract:
                    _ = try await EngineBridge.extract(
                        urls: command.urls,
                        password: currentArchivePassword.isEmpty ? nil : currentArchivePassword,
                        conflictPolicy: currentConflictPolicy
                    ) { line in
                        Task { @MainActor in
                            handleEngineLine(line)
                        }
                    }
                }

                await MainActor.run {
                    title = Localizer.shared.text("hud.done")
                    progressValue = 1
                    isCompleted = true
                    closeSoon()
                }
            } catch is CancellationError {
                await MainActor.run {
                    title = Localizer.shared.text("simple.status_cancelled")
                    isFailed = true
                    closeSoon()
                }
            } catch {
                await MainActor.run {
                    title = Localizer.shared.text("simple.status_failed")
                    detail = error.localizedDescription
                    isFailed = true
                }
            }
        }
    }

    private func resolvedConflictPolicy(format: ArchiveFormat, splitSizeMB: Int) -> OutputConflictPolicy {
        guard settings.outputConflictPolicy == .ask else {
            return settings.outputConflictPolicy
        }

        let hasCollision = switch command.kind {
        case .compress:
            EngineBridge.outputCollisionExists(urls: command.urls, format: format, splitSizeMB: splitSizeMB)
        case .extract:
            EngineBridge.extractionCollisionExists(urls: command.urls)
        }
        guard hasCollision else {
            return .append
        }

        let alert = NSAlert()
        alert.messageText = Localizer.shared.text("conflict.ask_title")
        alert.informativeText = Localizer.shared.text("conflict.ask_message")
        alert.addButton(withTitle: Localizer.shared.text("conflict.append"))
        alert.addButton(withTitle: Localizer.shared.text("conflict.overwrite"))
        return alert.runModal() == .alertSecondButtonReturn ? .overwrite : .append
    }

    @MainActor
    private func handleEngineLine(_ line: String) {
        guard let progress = LogParser.parseProgressLine(line) else {
            return
        }
        progressValue = progress.fraction
        detail = progress.name
    }

    private func cancel() {
        operationTask?.cancel()
    }

    private func closeSoon() {
        Task {
            try? await Task.sleep(for: .seconds(1.0))
            await MainActor.run {
                if terminatesWhenDone {
                    NSApplication.shared.terminate(nil)
                } else {
                    NSApplication.shared.keyWindow?.close()
                }
            }
        }
    }

}
