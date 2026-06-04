import AppKit
import SwiftUI

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    private var mainWindow: NSWindow?
    private var settingsWindow: NSWindow?
    private var hudWindows: [NSWindow] = []
    private var dropOverlayController: DropOverlayController?
    private var delayedMainWindow: DispatchWorkItem?
    private var receivedFileOpenEvent = false
    private var settingsObserver: NSObjectProtocol?
    private var statusItem: NSStatusItem?
    private var allowsActualTermination = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        observeSettings()
        configureStatusItem()
        applyBackgroundActivationPolicyIfNeeded()
        updateStatusItemVisibility()
        updateDropOverlayState()
        FinderIntegrationService.shared.refreshServices()

        if let hudArguments = hudLaunchArgumentsIfPresent(),
           installInApplicationsIfNeeded(arguments: hudArguments) {
            return
        }

        if let command = HUDCommand.parse(arguments: Array(CommandLine.arguments.dropFirst())) {
            showHUD(command: command, terminatesWhenDone: false)
            return
        }

        let workItem = DispatchWorkItem { [weak self] in
            guard let self, !self.receivedFileOpenEvent else {
                return
            }
            if self.installInApplicationsIfNeeded() {
                return
            }
            self.showMainWindow()
        }
        delayedMainWindow = workItem
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.2, execute: workItem)
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
        let joinedPaths = urls.map { $0.path }.joined(separator: " | ")
        NewnZipDebugLog.write("handleOpen urls=\(joinedPaths)")
        receivedFileOpenEvent = true
        delayedMainWindow?.cancel()

        if installInApplicationsIfNeeded(openURLs: urls) {
            NewnZipDebugLog.write("handleOpen rerouted to Applications install")
            return
        }

        let intent = DropResolver.resolve(urls: urls)
        NewnZipDebugLog.write("handleOpen intent=\(debugDescription(for: intent))")

        let command: HUDCommand
        switch intent {
        case .compress(let items):
            command = HUDCommand(kind: .compress, urls: items)
        case .extract(let items):
            command = HUDCommand(kind: .extract, urls: items)
        case .chooseForMultipleArchives(let items):
            command = HUDCommand(kind: .extract, urls: items)
        }
        showHUD(command: command, terminatesWhenDone: false)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        false
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        if allowsActualTermination || !AppSettings.shared.dragOverlayEnabled {
            return .terminateNow
        }

        let alert = NSAlert()
        alert.messageText = Localizer.shared.text("quit.prompt_title")
        alert.informativeText = Localizer.shared.text("quit.prompt_body")
        alert.addButton(withTitle: Localizer.shared.text("quit.prompt_keep_running"))
        alert.addButton(withTitle: Localizer.shared.text("quit.prompt_quit"))
        alert.addButton(withTitle: Localizer.shared.text("quit.prompt_cancel"))

        switch alert.runModal() {
        case .alertFirstButtonReturn:
            hideAllWindows()
            NSApp.hide(nil)
            return .terminateCancel
        case .alertSecondButtonReturn:
            return .terminateNow
        default:
            return .terminateCancel
        }
    }

    private func debugDescription(for intent: DropIntent) -> String {
        switch intent {
        case .compress(let items):
            return "compress(\(items.count))"
        case .extract(let items):
            return "extract(\(items.count))"
        case .chooseForMultipleArchives(let items):
            return "chooseForMultipleArchives(\(items.count))"
        }
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if !flag {
            showMainWindow()
        }
        return true
    }

    private func installInApplicationsIfNeeded(openURLs: [URL] = [], arguments: [String]? = nil) -> Bool {
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
            relaunchInstalledApp(at: installedURL, openURLs: openURLs, arguments: arguments)
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

    private func relaunchInstalledApp(at appURL: URL, openURLs: [URL], arguments: [String]?) {
        let configuration = NSWorkspace.OpenConfiguration()
        let relaunchArguments = arguments ?? Array(CommandLine.arguments.dropFirst())
        configuration.arguments = relaunchArguments + ["--newnzip-skip-install-prompt"]

        if !openURLs.isEmpty {
            NSWorkspace.shared.open(openURLs, withApplicationAt: appURL, configuration: configuration) { _, error in
                if let error {
                    Task { @MainActor in
                        NSApp.presentError(error)
                    }
                }
            }
            return
        }

        NSWorkspace.shared.openApplication(at: appURL, configuration: configuration)
    }

    private func hudLaunchArgumentsIfPresent() -> [String]? {
        let arguments = Array(CommandLine.arguments.dropFirst())
        return HUDCommand.parse(arguments: arguments) == nil ? nil : arguments
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
        NSApp.setActivationPolicy(.regular)
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        mainWindow = window
    }

    private func showSettingsWindow() {
        if let settingsWindow {
            settingsWindow.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            return
        }

        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 720, height: 560),
            styleMask: [.titled, .closable],
            backing: .buffered,
            defer: false
        )
        window.title = Localizer.shared.text("menu.settings")
        window.isReleasedWhenClosed = false
        window.delegate = self
        window.center()
        window.contentView = NSHostingView(
            rootView: SettingsView(onClose: { [weak self] in
                self?.closeSettingsWindow()
            })
        )
        NSApp.setActivationPolicy(.regular)
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        settingsWindow = window
    }

    private func closeSettingsWindow() {
        settingsWindow?.close()
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
        if window === settingsWindow {
            settingsWindow = nil
        }
        hudWindows.removeAll { $0 === window }
        applyBackgroundActivationPolicyIfNeeded()
    }

    @objc
    private func showMainWindowFromStatusItem(_ sender: Any?) {
        showMainWindow()
    }

    @objc
    private func showSettingsFromStatusItem(_ sender: Any?) {
        showSettingsWindow()
    }

    @objc
    private func quitCompletelyFromStatusItem(_ sender: Any?) {
        allowsActualTermination = true
        NSApp.terminate(sender)
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
        updateStatusItemVisibility()
        applyBackgroundActivationPolicyIfNeeded()
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

    private func configureStatusItem() {
        guard statusItem == nil else { return }

        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        if let button = item.button {
            button.image = statusBarIconImage()
            button.imagePosition = .imageOnly
        }

        let menu = NSMenu()
        menu.addItem(NSMenuItem(
            title: Localizer.shared.text("menu.status_open"),
            action: #selector(showMainWindowFromStatusItem(_:)),
            keyEquivalent: ""
        ))
        menu.addItem(NSMenuItem(
            title: Localizer.shared.text("menu.settings"),
            action: #selector(showSettingsFromStatusItem(_:)),
            keyEquivalent: ""
        ))
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(
            title: Localizer.shared.text("menu.status_quit"),
            action: #selector(quitCompletelyFromStatusItem(_:)),
            keyEquivalent: ""
        ))
        menu.items.forEach { $0.target = self }
        item.menu = menu
        statusItem = item
    }

    private func updateStatusItemVisibility() {
        if shouldUseMenuBarBackgroundMode {
            configureStatusItem()
        } else if let statusItem {
            NSStatusBar.system.removeStatusItem(statusItem)
            self.statusItem = nil
        }
    }

    private func hideAllWindows() {
        mainWindow?.orderOut(nil)
        hudWindows.forEach { $0.orderOut(nil) }
        applyBackgroundActivationPolicyIfNeeded()
    }

    private func applyBackgroundActivationPolicyIfNeeded() {
        let hasVisibleMainWindow = mainWindow?.isVisible == true
        let hasVisibleHUD = hudWindows.contains { $0.isVisible }
        if hasVisibleMainWindow || hasVisibleHUD {
            NSApp.setActivationPolicy(.regular)
        } else if shouldUseMenuBarBackgroundMode {
            NSApp.setActivationPolicy(.accessory)
        } else {
            NSApp.setActivationPolicy(.regular)
        }
    }

    private var shouldUseMenuBarBackgroundMode: Bool {
        AppSettings.shared.dragOverlayEnabled && AppSettings.shared.backgroundDisplayMode == .menuBar
    }

    private func statusBarIconImage() -> NSImage? {
        guard let iconPath = Bundle.main.path(forResource: "newnzip-mac-app-icon", ofType: "icns"),
              let image = NSImage(contentsOfFile: iconPath) else {
            return NSImage(systemSymbolName: "archivebox.fill", accessibilityDescription: "newnZip")
        }
        image.size = NSSize(width: 18, height: 18)
        image.isTemplate = false
        return image
    }
}
