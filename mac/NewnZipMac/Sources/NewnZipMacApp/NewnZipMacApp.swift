import SwiftUI

@main
struct NewnZipMacApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
        .windowResizability(.contentSize)
        Settings {
            SettingsView()
        }
    }
}
