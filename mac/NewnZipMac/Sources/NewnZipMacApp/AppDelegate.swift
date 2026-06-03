import AppKit
import SwiftUI

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    private var mainWindow: NSWindow?
    private var hudWindows: [NSWindow] = []
    private var dropOverlayController: DropOverlayController?
    private var delayedMainWindow: DispatchWorkItem?
    private var receivedFileOpenEvent = false
    private var settingsObserver: NSObjectProtocol?

    func applicationDidFinishLaunching(_ notification: Notification) {
        if installInApplicationsIfNeeded() {
            return
        }

        if let command = HUDCommand.parse(arguments: Array(CommandLine.arguments.dropFirst())) {
            showHUD(command: command, terminatesWhenDone: true)
            return
        }

        let workItem = DispatchWorkItem { [weak self] in
            guard let self, !self.receivedFileOpenEvent else {
                return
            }
            self.showMainWindow()
        }
        delayedMainWindow = workItem
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.2, execute: workItem)
        observeSettings()
        updateDropOverlayState()
    }

    func application(_ application: NSApplication, open urls: [URL]) {
        handleOpen(urls: urls)
    }

    func application(_ sender: NSApplication, openFile filename: String) -> Bool {
        handleOpen(urls: [URL(fileURLWithPath: filename)])
        return true
    }

    func application(_ sender: NSApplication, openFiles filenames: [String]) {
        handleOpen(urls: filenames.map { URL(fileURLWithPath: $0) })
        sender.reply(toOpenOrPrint: .success)
    }

    private func handleOpen(urls: [URL]) {
        receivedFileOpenEvent = true
        delayedMainWindow?.cancel()

        let intent = DropResolver.resolve(urls: urls)

        let command: HUDCommand
        switch intent {
        case .compress(let items):
            command = HUDCommand(kind: .compress, urls: items)
        case .extract(let items):
            command = HUDCommand(kind: .extract, urls: items)
        case .chooseForMultipleArchives(let items):
            command = HUDCommand(kind: .extract, urls: items)
        }
        showHUD(command: command, terminatesWhenDone: mainWindow == nil)
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if !flag {
            showMainWindow()
        }
        return true
    }

    private func installInApplicationsIfNeeded() -> Bool {
        let currentURL = Bundle.main.bundleURL.standardizedFileURL
        let applicationsURL = URL(fileURLWithPath: "/Applications", isDirectory: true).standardizedFileURL
        let installedURL = applicationsURL.appendingPathComponent("newnZip.app", isDirectory: true)

        guard currentURL.pathExtension == "app",
              currentURL.deletingLastPathComponent().standardizedFileURL.path != applicationsURL.path,
              !CommandLine.arguments.contains("--newnzip-skip-install-prompt")
        else {
            return false
        }

        let alert = NSAlert()
        alert.messageText = "newnZip을 Applications 폴더로 이동할까요?"
        alert.informativeText = "앱을 Applications 폴더에 설치한 뒤 새 위치에서 다시 실행합니다."
        alert.addButton(withTitle: "이동하고 실행")
        alert.addButton(withTitle: "그냥 실행")

        guard alert.runModal() == .alertFirstButtonReturn else {
            return false
        }

        do {
            try installCurrentApp(from: currentURL, to: installedURL)
            relaunchInstalledApp(at: installedURL)
            NSApp.terminate(nil)
            return true
        } catch {
            let failure = NSAlert(error: error)
            failure.messageText = "Applications 폴더로 이동하지 못했습니다"
            failure.informativeText = "권한을 확인한 뒤 다시 시도하거나 앱을 직접 Applications 폴더로 옮겨주세요."
            failure.runModal()
            return false
        }
    }

    private func installCurrentApp(from sourceURL: URL, to destinationURL: URL) throws {
        let fileManager = FileManager.default

        if fileManager.fileExists(atPath: destinationURL.path) {
            try fileManager.removeItem(at: destinationURL)
        }

        do {
            try fileManager.moveItem(at: sourceURL, to: destinationURL)
        } catch {
            try fileManager.copyItem(at: sourceURL, to: destinationURL)
            try fileManager.removeItem(at: sourceURL)
        }
    }

    private func relaunchInstalledApp(at appURL: URL) {
        let configuration = NSWorkspace.OpenConfiguration()
        configuration.arguments = Array(CommandLine.arguments.dropFirst()) + ["--newnzip-skip-install-prompt"]
        NSWorkspace.shared.openApplication(at: appURL, configuration: configuration)
    }

    private func showMainWindow() {
        if let mainWindow {
            mainWindow.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            return
        }

        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 492, height: 392),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "newnZip"
        window.isReleasedWhenClosed = false
        window.delegate = self
        window.contentView = NSHostingView(rootView: ContentView())
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        mainWindow = window
    }

    private func showHUD(command: HUDCommand, terminatesWhenDone: Bool) {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 360, height: 150),
            styleMask: [.titled, .closable],
            backing: .buffered,
            defer: false
        )
        window.title = "newnZip"
        window.isReleasedWhenClosed = false
        window.delegate = self
        window.contentView = NSHostingView(
            rootView: HUDProgressView(command: command, terminatesWhenDone: terminatesWhenDone)
        )
        configureHUDWindow(window)
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        hudWindows.append(window)
    }

    func windowWillClose(_ notification: Notification) {
        guard let window = notification.object as? NSWindow else {
            return
        }

        if window === mainWindow {
            mainWindow = nil
        }
        hudWindows.removeAll { $0 === window }
    }

    private func startDropOverlay() {
        guard dropOverlayController == nil else {
            return
        }
        let controller = DropOverlayController { [weak self] urls in
            self?.handleOpen(urls: urls)
        }
        controller.start()
        dropOverlayController = controller
    }

    private func stopDropOverlay() {
        dropOverlayController?.stop()
        dropOverlayController = nil
    }

    private func updateDropOverlayState() {
        if AppSettings.shared.dragOverlayEnabled {
            startDropOverlay()
        } else {
            stopDropOverlay()
        }
    }

    private func observeSettings() {
        guard settingsObserver == nil else {
            return
        }
        settingsObserver = NotificationCenter.default.addObserver(
            forName: .dragOverlaySettingChanged,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            Task { @MainActor in
                self?.updateDropOverlayState()
            }
        }
    }

    private func configureHUDWindow(_ window: NSWindow) {
        window.level = .floating
        window.styleMask.remove(.resizable)
        window.styleMask.remove(.miniaturizable)
        window.titleVisibility = .hidden
        window.titlebarAppearsTransparent = true
        window.isMovableByWindowBackground = true

        let screen = NSScreen.main ?? NSScreen.screens.first
        guard let visibleFrame = screen?.visibleFrame else {
            return
        }
        let frame = window.frame
        let origin = CGPoint(
            x: visibleFrame.maxX - frame.width - 20,
            y: visibleFrame.maxY - frame.height - 20
        )
        window.setFrameOrigin(origin)
    }
}
