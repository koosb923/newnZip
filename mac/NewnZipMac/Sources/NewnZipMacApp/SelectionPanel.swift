import AppKit
import Foundation

enum SelectionPanel {
    @MainActor
    static func pickURLs() -> [URL] {
        let panel = NSOpenPanel()
        panel.title = Localizer.shared.text("picker.title")
        panel.message = Localizer.shared.text("picker.message")
        panel.prompt = Localizer.shared.text("picker.confirm")
        panel.allowsMultipleSelection = true
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        panel.resolvesAliases = true
        panel.canCreateDirectories = false

        return panel.runModal() == .OK ? panel.urls : []
    }
}
