import AppKit
import ObjectiveC
import SwiftUI

struct ContentView: View {
    @StateObject private var settings = AppSettings.shared
    @State private var statusText = ""
    @State private var showingSettings = false
    @State private var isProcessing = false
    @State private var isCompleted = false
    @State private var progressValue: Double?
    @State private var operationTask: Task<Void, Never>?
    @State private var splitDialogMode: SplitInputMode = .sizeMB

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
                subtitle: Localizer.shared.text("simple.single_drop_subtitle", ["format": ArchiveFormat.zip.rawValue]),
                isProcessing: isProcessing,
                isCompleted: isCompleted,
                progressValue: progressValue,
                statusText: currentStatusText,
                onDropURLs: handleDrop(urls:action:),
                onTap: openSelectionPanel(action:),
                onCancel: cancelCurrentOperation
            )
            .frame(minWidth: 460, minHeight: 360)
        }
        .padding(16)
        .sheet(isPresented: $showingSettings) {
            SettingsView()
        }
        .onOpenURL { url in
            openHUD(for: DropResolver.resolve(urls: [url]), urls: [url])
        }
    }

    private var currentStatusText: String {
        if !statusText.isEmpty {
            return statusText
        }
        return Localizer.shared.text("simple.idle_status")
    }

    private func openSelectionPanel(action: DropPanelAction) {
        let urls = SelectionPanel.pickURLs()
        guard !urls.isEmpty else { return }
        handleDrop(urls: urls, action: action)
    }

    private func handleDrop(urls: [URL], action: DropPanelAction) {
        guard !isProcessing else { return }

        switch action {
        case .standard:
            handleStandardDrop(urls: urls)
        case .passwordCompress:
            guard let password = promptForPassword(defaultValue: settings.archivePassword) else {
                return
            }
            let format = passwordCompatibleFormat(for: settings.defaultFormat)
            startCompression(
                urls: urls,
                format: format,
                zipMethod: settings.zipMethod,
                splitSizeMB: 0,
                password: password
            )
        case .splitCompress:
            guard let splitRequest = promptForSplitRequest(urls: urls) else {
                return
            }
            startCompression(
                urls: urls,
                format: .zip,
                zipMethod: settings.zipMethod,
                splitSizeMB: splitRequest.splitSizeMB,
                password: nil
            )
        }
    }

    private func handleStandardDrop(urls: [URL]) {
        switch DropResolver.resolve(urls: urls) {
        case .compress(let items):
            startCompression(
                urls: items,
                format: .zip,
                zipMethod: settings.zipMethod,
                splitSizeMB: 0,
                password: nil
            )
        case .extract(let items):
            startExtraction(urls: items, password: nil)
        case .chooseForMultipleArchives(let items):
            switch promptForMultipleArchiveAction() {
            case .extractEach:
                startExtraction(urls: items, password: nil)
            case .compressTogether:
                startCompression(
                    urls: items,
                    format: .zip,
                    zipMethod: settings.zipMethod,
                    splitSizeMB: 0,
                    password: nil
                )
            case .cancel:
                return
            }
        }
    }

    private var normalizedPassword: String? {
        let value = settings.archivePassword.trimmingCharacters(in: .whitespacesAndNewlines)
        return value.isEmpty ? nil : value
    }

    private func startCompression(
        urls: [URL],
        format: ArchiveFormat,
        zipMethod: ZipMethod,
        splitSizeMB: Int,
        password: String?
    ) {
        guard !urls.isEmpty else { return }

        isProcessing = true
        isCompleted = false
        progressValue = 0.0

        let conflictPolicy = resolvedConflictPolicy(for: .compress(urls), format: format, splitSizeMB: splitSizeMB)
        statusText = splitSizeMB > 0
            ? "분할 압축 중..."
            : Localizer.shared.text("simple.status_compressing", ["count": "\(urls.count)", "format": format.rawValue])

        operationTask = Task.detached(priority: .userInitiated) {
            do {
                _ = try await EngineBridge.compress(
                    urls: urls,
                    format: format,
                    zipMethod: zipMethod,
                    splitSizeMB: splitSizeMB,
                    password: password,
                    conflictPolicy: conflictPolicy
                ) { line in
                    Task { @MainActor in
                        handleEngineLine(line)
                    }
                }

                await finishSuccess()
            } catch is CancellationError {
                await finishCancelled()
            } catch {
                await finishFailure(error)
            }
        }
    }

    private func startExtraction(urls: [URL], password: String?) {
        guard !urls.isEmpty else { return }

        isProcessing = true
        isCompleted = false
        progressValue = 0.0
        statusText = Localizer.shared.text("simple.status_extracting", ["count": "\(urls.count)"])

        let conflictPolicy = resolvedConflictPolicy(for: .extract(urls), format: settings.defaultFormat, splitSizeMB: 0)

        operationTask = Task.detached(priority: .userInitiated) {
            do {
                do {
                    _ = try await EngineBridge.extract(
                        urls: urls,
                        password: password,
                        conflictPolicy: conflictPolicy
                    ) { line in
                        Task { @MainActor in
                            handleEngineLine(line)
                        }
                    }
                } catch {
                    guard password == nil,
                          let retryPassword = await MainActor.run(body: {
                              promptForExtractionPassword(defaultValue: settings.archivePassword)
                          }) else {
                        throw error
                    }

                    _ = try await EngineBridge.extract(
                        urls: urls,
                        password: retryPassword,
                        conflictPolicy: conflictPolicy
                    ) { line in
                        Task { @MainActor in
                            handleEngineLine(line)
                        }
                    }
                }

                await finishSuccess()
            } catch is CancellationError {
                await finishCancelled()
            } catch {
                await finishFailure(error)
            }
        }
    }

    @MainActor
    private func finishSuccess() {
        statusText = Localizer.shared.text("simple.status_done")
        isProcessing = false
        progressValue = 1.0
        isCompleted = true
        operationTask = nil
        scheduleReset()
    }

    @MainActor
    private func finishCancelled() {
        statusText = Localizer.shared.text("simple.status_cancelled")
        isProcessing = false
        isCompleted = false
        progressValue = nil
        operationTask = nil
    }

    @MainActor
    private func finishFailure(_ error: Error) {
        statusText = error.localizedDescription
        isProcessing = false
        isCompleted = false
        progressValue = nil
        operationTask = nil
    }

    private func cancelCurrentOperation() {
        guard isProcessing else { return }
        statusText = Localizer.shared.text("simple.status_cancelling")
        operationTask?.cancel()
    }

    private func openHUD(for intent: DropIntent, urls: [URL]) {
        let command = switch intent {
        case .compress, .chooseForMultipleArchives: "--hud-compress"
        case .extract: "--hud-extract"
        }
        let configuration = NSWorkspace.OpenConfiguration()
        configuration.arguments = [command] + urls.map(\.path)
        NSWorkspace.shared.openApplication(at: Bundle.main.bundleURL, configuration: configuration)
    }

    private func resolvedConflictPolicy(for intent: DropIntent, format: ArchiveFormat, splitSizeMB: Int) -> OutputConflictPolicy {
        guard settings.outputConflictPolicy == .ask else {
            return settings.outputConflictPolicy
        }
        let hasCollision = switch intent {
        case .compress(let items), .chooseForMultipleArchives(let items):
            EngineBridge.outputCollisionExists(urls: items, format: format, splitSizeMB: splitSizeMB)
        case .extract(let items):
            EngineBridge.extractionCollisionExists(urls: items)
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

    private func passwordCompatibleFormat(for format: ArchiveFormat) -> ArchiveFormat {
        switch format {
        case .zip, .sevenZip:
            return format
        default:
            return .zip
        }
    }

    private func promptForPassword(defaultValue: String) -> String? {
        let alert = NSAlert()
        alert.messageText = "암호로 압축"
        alert.informativeText = "압축에 사용할 암호를 입력하세요."
        alert.addButton(withTitle: "압축")
        alert.addButton(withTitle: "취소")

        let accessory = PasswordAccessoryView(defaultValue: defaultValue)
        alert.accessoryView = accessory

        guard alert.runModal() == .alertFirstButtonReturn else {
            return nil
        }

        let password = accessory.currentValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !password.isEmpty else {
            return nil
        }
        settings.archivePassword = password
        return password
    }

    private func promptForExtractionPassword(defaultValue: String) -> String? {
        let alert = NSAlert()
        alert.messageText = "암호 압축 풀기"
        alert.informativeText = "압축을 풀기 위한 암호를 입력하세요."
        alert.addButton(withTitle: "압축 풀기")
        alert.addButton(withTitle: "취소")

        let accessory = PasswordAccessoryView(defaultValue: defaultValue)
        alert.accessoryView = accessory

        guard alert.runModal() == .alertFirstButtonReturn else {
            return nil
        }

        let password = accessory.currentValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !password.isEmpty else {
            return nil
        }
        settings.archivePassword = password
        return password
    }

    private func promptForMultipleArchiveAction() -> MultipleArchiveAction {
        let alert = NSAlert()
        alert.messageText = "여러 압축파일을 어떻게 처리할까요?"
        alert.informativeText = "각각 압축을 풀거나, 선택한 압축파일들을 다시 하나로 압축할 수 있습니다."
        alert.addButton(withTitle: "각각 압축 풀기")
        alert.addButton(withTitle: "하나로 압축하기")
        alert.addButton(withTitle: "취소")

        switch alert.runModal() {
        case .alertFirstButtonReturn:
            return .extractEach
        case .alertSecondButtonReturn:
            return .compressTogether
        default:
            return .cancel
        }
    }

    private func promptForSplitRequest(urls: [URL]) -> SplitRequest? {
        let alert = NSAlert()
        alert.messageText = "분할 압축"
        alert.informativeText = "용량 기준 또는 개수 기준으로 분할 압축 옵션을 선택하세요."
        alert.addButton(withTitle: "적용")
        alert.addButton(withTitle: "취소")

        let modePicker = NSPopUpButton(frame: NSRect(x: 0, y: 36, width: 260, height: 26), pullsDown: false)
        modePicker.addItems(withTitles: ["몇 MB씩 나누기", "몇 개로 나누기"])
        modePicker.selectItem(at: splitDialogMode == .sizeMB ? 0 : 1)

        let valueField = NSTextField(frame: NSRect(x: 0, y: 0, width: 260, height: 24))
        valueField.stringValue = splitDialogMode == .sizeMB
            ? "\(settings.defaultSplitSizeMB)"
            : "\(settings.defaultSplitPartCount)"
        modePicker.action = #selector(SplitModeTarget.modeChanged(_:))
        let target = SplitModeTarget { selectedIndex in
            splitDialogMode = selectedIndex == 0 ? .sizeMB : .partCount
            valueField.stringValue = selectedIndex == 0
                ? "\(settings.defaultSplitSizeMB)"
                : "\(settings.defaultSplitPartCount)"
        }
        modePicker.target = target

        let container = NSView(frame: NSRect(x: 0, y: 0, width: 260, height: 62))
        container.addSubview(modePicker)
        container.addSubview(valueField)
        objc_setAssociatedObject(container, "splitModeTarget", target, .OBJC_ASSOCIATION_RETAIN_NONATOMIC)
        alert.accessoryView = container

        guard alert.runModal() == .alertFirstButtonReturn else {
            return nil
        }

        let value = max(1, Int(valueField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)) ?? 0)
        if modePicker.indexOfSelectedItem == 0 {
            splitDialogMode = .sizeMB
            settings.defaultSplitSizeMB = value
            return SplitRequest(splitSizeMB: value)
        }

        splitDialogMode = .partCount
        settings.defaultSplitPartCount = max(2, value)
        let estimatedSplitSize = estimateSplitSizeMB(for: settings.defaultSplitPartCount, urls: urls)
        return SplitRequest(splitSizeMB: estimatedSplitSize)
    }

    private func estimateSplitSizeMB(for partCount: Int, urls: [URL]) -> Int {
        max(1, Int(ceil(Double(totalInputBytes(for: urls)) / Double(max(2, partCount)) / 1_048_576.0)))
    }

    private func totalInputBytes(for urls: [URL]) -> UInt64 {
        urls.reduce(0) { partialResult, url in
            partialResult + itemSize(url)
        }
    }

    private func itemSize(_ url: URL) -> UInt64 {
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: url.path, isDirectory: &isDirectory) else {
            return 0
        }
        if !isDirectory.boolValue {
            return (try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize).map { UInt64($0) } ?? 0
        }

        let enumerator = FileManager.default.enumerator(at: url, includingPropertiesForKeys: [.isRegularFileKey, .fileSizeKey])
        var total: UInt64 = 0
        while let fileURL = enumerator?.nextObject() as? URL {
            let values = try? fileURL.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey])
            if values?.isRegularFile == true {
                total += UInt64(values?.fileSize ?? 0)
            }
        }
        return total
    }
}

private enum MultipleArchiveAction {
    case extractEach
    case compressTogether
    case cancel
}

private struct SplitRequest {
    let splitSizeMB: Int
}

@MainActor
final class PasswordAccessoryView: NSView {
    private let secureField = NSSecureTextField(frame: .zero)
    private let plainField = NSTextField(frame: .zero)
    private let toggleButton = NSButton(frame: .zero)

    var currentValue: String {
        secureField.isHidden ? plainField.stringValue : secureField.stringValue
    }

    init(defaultValue: String) {
        super.init(frame: NSRect(x: 0, y: 0, width: 300, height: 24))

        secureField.frame = NSRect(x: 0, y: 0, width: 260, height: 24)
        secureField.stringValue = defaultValue

        plainField.frame = secureField.frame
        plainField.stringValue = defaultValue
        plainField.isHidden = true

        toggleButton.frame = NSRect(x: 268, y: 1, width: 24, height: 22)
        toggleButton.bezelStyle = .shadowlessSquare
        toggleButton.isBordered = false
        toggleButton.image = NSImage(systemSymbolName: "eye", accessibilityDescription: "암호 보기")
        toggleButton.target = self
        toggleButton.action = #selector(toggleVisibility)

        addSubview(secureField)
        addSubview(plainField)
        addSubview(toggleButton)
    }

    @objc
    private func toggleVisibility() {
        if secureField.isHidden {
            secureField.stringValue = plainField.stringValue
        } else {
            plainField.stringValue = secureField.stringValue
        }
        secureField.isHidden.toggle()
        plainField.isHidden.toggle()
        toggleButton.image = NSImage(
            systemSymbolName: secureField.isHidden ? "eye.slash" : "eye",
            accessibilityDescription: secureField.isHidden ? "암호 숨기기" : "암호 보기"
        )
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }
}

private enum SplitInputMode {
    case sizeMB
    case partCount
}

@MainActor
private final class SplitModeTarget: NSObject {
    private let handler: (Int) -> Void

    init(handler: @escaping (Int) -> Void) {
        self.handler = handler
    }

    @objc
    func modeChanged(_ sender: NSPopUpButton) {
        handler(sender.indexOfSelectedItem)
    }
}
