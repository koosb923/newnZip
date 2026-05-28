import Foundation

@MainActor
final class Localizer {
    static let shared = Localizer()

    private var messages: [String: String] = [:]
    private(set) var language: AppLanguage = .system

    private init() {
        setLanguage(.system)
    }

    func setLanguage(_ language: AppLanguage) {
        self.language = language
        let resolved = resolvedLanguageCode(for: language)
        messages = loadMessages(languageCode: resolved)
    }

    func text(_ key: String, _ values: [String: String] = [:]) -> String {
        var template = messages[key] ?? key
        for (name, value) in values {
            template = template.replacingOccurrences(of: "{\(name)}", with: value)
        }
        return template
    }

    private func resolvedLanguageCode(for language: AppLanguage) -> String {
        switch language {
        case .system:
            let preferred = Locale.preferredLanguages.first ?? "en"
            let code = preferred.split(separator: "-").first.map(String.init) ?? "en"
            return ["ko", "en", "ja"].contains(code) ? code : "en"
        case .ko, .en, .ja:
            return language.rawValue
        }
    }

    private func loadMessages(languageCode: String) -> [String: String] {
        var merged: [String: String] = [:]
        for code in ["en", languageCode] {
            if let url = resourceURL(for: code),
               let data = try? Data(contentsOf: url),
               let json = try? JSONSerialization.jsonObject(with: data) as? [String: String] {
                merged.merge(json) { _, new in new }
            }
        }
        return merged
    }

    private func resourceURL(for languageCode: String) -> URL? {
        let appBundleResource = Bundle.main.url(forResource: languageCode, withExtension: "json", subdirectory: "locales")
        if appBundleResource != nil {
            return appBundleResource
        }

        let bundled = Bundle.module.url(forResource: languageCode, withExtension: "json", subdirectory: "Resources/locales")
        if bundled != nil {
            return bundled
        }

        let current = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
        let shared = current
            .appendingPathComponent("../../shared/locales", isDirectory: true)
            .standardizedFileURL
            .appendingPathComponent("\(languageCode).json")
        return FileManager.default.fileExists(atPath: shared.path) ? shared : nil
    }
}
