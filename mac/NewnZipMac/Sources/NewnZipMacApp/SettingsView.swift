import SwiftUI

struct SettingsView: View {
    @StateObject private var settings = AppSettings.shared
    @Environment(\.dismiss) private var dismiss

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

            HStack {
                Spacer()
                Button(Localizer.shared.text("settings.cancel")) {
                    dismiss()
                }
                Button(Localizer.shared.text("settings.save")) {
                    Localizer.shared.setLanguage(settings.language)
                    dismiss()
                }
            }
        }
        .padding(20)
        .frame(width: 360)
    }
}
