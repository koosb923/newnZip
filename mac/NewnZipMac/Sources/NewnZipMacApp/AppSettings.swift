import Foundation

enum ArchiveFormat: String, CaseIterable, Identifiable {
    case zip = "zip"
    case sevenZip = "7z"
    case tarGz = "tar.gz"

    var id: String { rawValue }
}

enum AppLanguage: String, CaseIterable, Identifiable {
    case system = "system"
    case ko = "ko"
    case en = "en"
    case ja = "ja"

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .system: return "System"
        case .ko: return "한국어"
        case .en: return "English"
        case .ja: return "日本語"
        }
    }
}

@MainActor
final class AppSettings: ObservableObject {
    static let shared = AppSettings()

    @Published var defaultFormat: ArchiveFormat {
        didSet { UserDefaults.standard.set(defaultFormat.rawValue, forKey: "default_format") }
    }

    @Published var language: AppLanguage {
        didSet {
            UserDefaults.standard.set(language.rawValue, forKey: "language")
            Localizer.shared.setLanguage(language)
        }
    }

    private init() {
        let savedFormat = UserDefaults.standard.string(forKey: "default_format")
        self.defaultFormat = ArchiveFormat(rawValue: savedFormat ?? "zip") ?? .zip

        let savedLanguage = UserDefaults.standard.string(forKey: "language")
        let resolvedLanguage = AppLanguage(rawValue: savedLanguage ?? "system") ?? .system
        self.language = resolvedLanguage
        Localizer.shared.setLanguage(resolvedLanguage)
    }
}
