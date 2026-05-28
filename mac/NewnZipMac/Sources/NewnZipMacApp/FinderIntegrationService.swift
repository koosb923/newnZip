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
        if let url = URL(string: "x-apple.systempreferences:com.apple.Keyboard-Settings.extension") {
            NSWorkspace.shared.open(url)
        }
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
