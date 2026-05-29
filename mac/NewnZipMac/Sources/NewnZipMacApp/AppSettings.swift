import Foundation

enum ArchiveFormat: String, CaseIterable, Identifiable {
    case zip = "zip"
    case sevenZip = "7z"
    case tar = "tar"
    case tarGz = "tar.gz"
    case tarBz2 = "tar.bz2"
    case tarXz = "tar.xz"
    case tarZstd = "tar.zstd"
    case zstd = "zstd"
    case lz4 = "lz4"
    case brotli = "brotli"
    case wim = "wim"

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

enum ZipMethod: String, CaseIterable, Identifiable {
    case auto = "auto"
    case deflate = "deflate"
    case store = "store"

    var id: String { rawValue }
}

enum DragOverlayDockSide: String, CaseIterable, Identifiable {
    case left = "left"
    case right = "right"

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

    @Published var zipMethod: ZipMethod {
        didSet { UserDefaults.standard.set(zipMethod.rawValue, forKey: "zip_method") }
    }

    @Published var dragOverlayEnabled: Bool {
        didSet {
            UserDefaults.standard.set(dragOverlayEnabled, forKey: "drag_overlay_enabled")
            NotificationCenter.default.post(name: .dragOverlaySettingChanged, object: nil)
        }
    }

    @Published var dragOverlayDockSide: DragOverlayDockSide {
        didSet {
            UserDefaults.standard.set(dragOverlayDockSide.rawValue, forKey: "drag_overlay_dock_side")
            NotificationCenter.default.post(name: .dragOverlaySettingChanged, object: nil)
        }
    }

    @Published var archivePassword: String {
        didSet { UserDefaults.standard.set(archivePassword, forKey: "archive_password") }
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
        let savedZipMethod = UserDefaults.standard.string(forKey: "zip_method")
        self.zipMethod = ZipMethod(rawValue: savedZipMethod ?? "auto") ?? .auto
        self.dragOverlayEnabled = UserDefaults.standard.object(forKey: "drag_overlay_enabled") as? Bool ?? true
        let savedDragOverlayDockSide = UserDefaults.standard.string(forKey: "drag_overlay_dock_side")
        self.dragOverlayDockSide = DragOverlayDockSide(rawValue: savedDragOverlayDockSide ?? "left") ?? .left
        self.archivePassword = UserDefaults.standard.string(forKey: "archive_password") ?? ""

        let savedLanguage = UserDefaults.standard.string(forKey: "language")
        let resolvedLanguage = AppLanguage(rawValue: savedLanguage ?? "system") ?? .system
        self.language = resolvedLanguage
        Localizer.shared.setLanguage(resolvedLanguage)
    }
}

extension Notification.Name {
    static let dragOverlaySettingChanged = Notification.Name("NewnZipDragOverlaySettingChanged")
}
