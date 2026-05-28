import SwiftUI

struct SettingsView: View {
    @StateObject private var settings = AppSettings.shared
    @Environment(\.dismiss) private var dismiss
    @State private var splitSizeText = "\(AppSettings.shared.splitSizeMB)"

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Picker(Localizer.shared.text("settings.language"), selection: $settings.language) {
                ForEach(AppLanguage.allCases) { language in
                    Text(language.displayName).tag(language)
                }
            }

            Picker(Localizer.shared.text("settings.default_format"), selection: $settings.defaultFormat) {
                ForEach(ArchiveFormat.allCases) { format in
                    Text(format.rawValue.uppercased()).tag(format)
                }
            }

            Picker(Localizer.shared.text("settings.zip_method"), selection: $settings.zipMethod) {
                ForEach(ZipMethod.allCases) { method in
                    Text(Localizer.shared.text("settings.zip_method_\(method.rawValue)")).tag(method)
                }
            }

            TextField(Localizer.shared.text("settings.split_size_mb"), text: $splitSizeText)
                .textFieldStyle(.roundedBorder)

            Picker(Localizer.shared.text("settings.output_conflict_policy"), selection: $settings.outputConflictPolicy) {
                ForEach(OutputConflictPolicy.allCases) { policy in
                    Text(Localizer.shared.text("settings.output_conflict_\(policy.rawValue)")).tag(policy)
                }
            }

            HStack {
                Spacer()
                Button(Localizer.shared.text("settings.cancel")) {
                    dismiss()
                }
                Button(Localizer.shared.text("settings.save")) {
                    settings.splitSizeMB = max(0, Int(splitSizeText.trimmingCharacters(in: .whitespacesAndNewlines)) ?? 0)
                    Localizer.shared.setLanguage(settings.language)
                    dismiss()
                }
            }
        }
        .padding(20)
        .frame(width: 360)
    }
}
