import SwiftUI

struct ContentView: View {
    @StateObject private var settings = AppSettings.shared
    @State private var statusText = ""
    @State private var showingSettings = false
    @State private var isProcessing = false
    @State private var isCompleted = false
    @State private var progressValue: Double?

    var body: some View {
        VStack(spacing: 12) {
            HStack {
                Spacer()
                Button {
                    showingSettings = true
                } label: {
                    Image(systemName: "gearshape")
                }
                .buttonStyle(.borderless)
            }

            DropAreaView(
                title: Localizer.shared.text("simple.single_drop_title"),
                subtitle: Localizer.shared.text("simple.single_drop_subtitle", ["format": settings.defaultFormat.rawValue]),
                isProcessing: isProcessing,
                isCompleted: isCompleted,
                progressValue: progressValue,
                statusText: currentStatusText,
                onDropURLs: handleDrop(urls:),
                onTap: openSelectionPanel
            )
            .frame(minWidth: 460, minHeight: 360)
        }
        .padding(16)
        .sheet(isPresented: $showingSettings) {
            SettingsView()
        }
    }

    private var currentStatusText: String {
        if !statusText.isEmpty {
            return statusText
        }
        return Localizer.shared.text("simple.idle_status")
    }

    private func openSelectionPanel() {
        let urls = SelectionPanel.pickURLs()
        guard !urls.isEmpty else { return }
        handleDrop(urls: urls)
    }

    private func handleDrop(urls: [URL]) {
        guard !isProcessing else { return }

        guard let intent = DropResolver.resolve(urls: urls) else {
            statusText = Localizer.shared.text("simple.mixed_drop_error")
            isCompleted = false
            progressValue = nil
            return
        }

        isProcessing = true
        isCompleted = false
        progressValue = 0.0

        let currentFormat = settings.defaultFormat
        switch intent {
        case .compress(let items):
            statusText = Localizer.shared.text("simple.status_compressing", ["count": "\(items.count)", "format": currentFormat.rawValue])
        case .extract(let items):
            statusText = Localizer.shared.text("simple.status_extracting", ["count": "\(items.count)"])
        }

        Task.detached(priority: .userInitiated) {
            do {
                switch intent {
                case .compress(let items):
                    _ = try await EngineBridge.compress(urls: items, format: currentFormat) { line in
                        Task { @MainActor in
                            handleEngineLine(line)
                        }
                    }
                case .extract(let items):
                    _ = try await EngineBridge.extract(urls: items) { line in
                        Task { @MainActor in
                            handleEngineLine(line)
                        }
                    }
                }

                await MainActor.run {
                    statusText = Localizer.shared.text("simple.status_done")
                    isProcessing = false
                    progressValue = 1.0
                    isCompleted = true
                    scheduleReset()
                }
            } catch {
                await MainActor.run {
                    statusText = Localizer.shared.text("simple.status_failed")
                    isProcessing = false
                    isCompleted = false
                    progressValue = nil
                }
            }
        }
    }

    @MainActor
    private func handleEngineLine(_ line: String) {
        if let progress = LogParser.parseProgressLine(line) {
            progressValue = progress.fraction

            if progress.stage == "compress" {
                statusText = Localizer.shared.text("simple.progress_compress", [
                    "completed": "\(progress.completed)",
                    "total": "\(progress.total)"
                ])
            } else if progress.stage == "extract" {
                statusText = Localizer.shared.text("simple.progress_extract", [
                    "completed": "\(progress.completed)",
                    "total": "\(progress.total)"
                ])
            }
        }
    }

    @MainActor
    private func scheduleReset() {
        Task {
            try? await Task.sleep(for: .seconds(1.2))
            if !isProcessing {
                isCompleted = false
                progressValue = nil
                statusText = ""
            }
        }
    }
}
