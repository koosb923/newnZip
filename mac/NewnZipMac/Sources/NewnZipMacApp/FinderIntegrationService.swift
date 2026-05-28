import AppKit
import Foundation

@MainActor
final class FinderIntegrationService: ObservableObject {
    static let shared = FinderIntegrationService()

    @Published private(set) var statusText = ""

    private init() {}

    func refreshServices() {
        registerAppBundle()
        NSUpdateDynamicServices()
        run("/System/Library/CoreServices/pbs", arguments: ["-flush"])
        statusText = Localizer.shared.text("settings.finder_service_refreshed")
    }

    func openServicesSettings() {
        let candidates = [
            "x-apple.systempreferences:com.apple.Keyboard-Settings.extension?KeyboardShortcuts",
            "x-apple.systempreferences:com.apple.Keyboard-Settings.extension?Shortcuts",
            "x-apple.systempreferences:com.apple.preference.keyboard?KeyboardShortcuts",
            "x-apple.systempreferences:com.apple.preference.keyboard?Shortcuts",
            "x-apple.systempreferences:com.apple.preference.keyboard?shortcuts",
            "x-apple.systempreferences:com.apple.Keyboard-Settings.extension"
        ]
        for candidate in candidates {
            guard let url = URL(string: candidate), NSWorkspace.shared.open(url) else {
                continue
            }
            runServicesNavigationScript()
            return
        }
        runServicesNavigationScript()
    }

    private func runServicesNavigationScript() {
        let script = """
        tell application "System Settings"
          activate
        end tell
        delay 1.5
        tell application "System Events"
          tell process "System Settings"
            set frontmost to true
            delay 0.8
            repeat with shortcutLabel in {"Keyboard Shortcuts…", "Keyboard Shortcuts...", "Keyboard Shortcuts", "키보드 단축키…", "키보드 단축키...", "키보드 단축키", "キーボードショートカット…", "キーボードショートカット"}
              try
                click (first UI element of entire contents of window 1 whose name is shortcutLabel)
                delay 1.0
                exit repeat
              end try
            end repeat
            repeat with serviceLabel in {"Services", "서비스", "サービス"}
              try
                click (first UI element of entire contents of window 1 whose name is serviceLabel)
                delay 0.8
                exit repeat
              end try
            end repeat
            repeat with fileFolderLabel in {"Files and Folders", "파일 및 폴더", "ファイルとフォルダ"}
              try
                click (first UI element of entire contents of window 1 whose name is fileFolderLabel)
                exit repeat
              end try
            end repeat
          end tell
        end tell
        """

        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
        process.arguments = ["-e", script]
        try? process.run()
    }

    private func registerAppBundle() {
        let lsregister = "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
        run(lsregister, arguments: ["-f", Bundle.main.bundleURL.path])
    }

    private func run(_ executable: String, arguments: [String]) {
        guard FileManager.default.isExecutableFile(atPath: executable) else {
            return
        }

        let process = Process()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        try? process.run()
        process.waitUntilExit()
    }
}
