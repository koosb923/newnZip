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

enum OutputConflictPolicy: String, CaseIterable, Identifiable {
    case append = "append"
    case overwrite = "overwrite"
    case ask = "ask"

    var id: String { rawValue }
}

@MainActor
final class AppSettings: ObservableObject {
    static let shared = AppSettings()

    @Published var defaultFormat: ArchiveFormat {
        didSet { UserDefaults.standard.set(defaultFormat.rawValue, forKey: "default_format") }
    }

    @Published var splitSizeMB: Int {
        didSet { UserDefaults.standard.set(splitSizeMB, forKey: "split_size_mb") }
    }

    @Published var outputConflictPolicy: OutputConflictPolicy {
        didSet { UserDefaults.standard.set(outputConflictPolicy.rawValue, forKey: "output_conflict_policy") }
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
        self.splitSizeMB = UserDefaults.standard.integer(forKey: "split_size_mb")
        let savedConflictPolicy = UserDefaults.standard.string(forKey: "output_conflict_policy")
        self.outputConflictPolicy = OutputConflictPolicy(rawValue: savedConflictPolicy ?? "append") ?? .append

        let savedLanguage = UserDefaults.standard.string(forKey: "language")
        let resolvedLanguage = AppLanguage(rawValue: savedLanguage ?? "system") ?? .system
        self.language = resolvedLanguage
        Localizer.shared.setLanguage(resolvedLanguage)
    }
}
