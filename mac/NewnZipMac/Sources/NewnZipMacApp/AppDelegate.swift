import AppKit
import SwiftUI

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    private var mainWindow: NSWindow?
    private var hudWindows: [NSWindow] = []
    private var delayedMainWindow: DispatchWorkItem?
    private var receivedFileOpenEvent = false

    func applicationDidFinishLaunching(_ notification: Notification) {
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

        guard let intent = DropResolver.resolve(urls: urls) else {
            showMainWindow()
            return
        }

        let command: HUDCommand
        switch intent {
        case .compress(let items):
            command = HUDCommand(kind: .compress, urls: items)
        case .extract(let items):
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
        window.contentView = NSHostingView(
            rootView: HUDProgressView(command: command, terminatesWhenDone: terminatesWhenDone)
        )
        configureHUDWindow(window)
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        hudWindows.append(window)
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
