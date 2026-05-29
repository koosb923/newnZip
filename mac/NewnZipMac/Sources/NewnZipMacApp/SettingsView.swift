import SwiftUI

struct SettingsView: View {
    @StateObject private var settings = AppSettings.shared
    @StateObject private var defaultAppService = DefaultArchiveAppService.shared
    @StateObject private var finderIntegration = FinderIntegrationService.shared
    @Environment(\.dismiss) private var dismiss
    @State private var splitSizeText = "\(AppSettings.shared.defaultSplitSizeMB)"
    @State private var splitPartCountText = "\(AppSettings.shared.defaultSplitPartCount)"

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

            TextField(Localizer.shared.text("settings.default_split_size_mb"), text: $splitSizeText)
                .textFieldStyle(.roundedBorder)

            TextField(Localizer.shared.text("settings.default_split_part_count"), text: $splitPartCountText)
                .textFieldStyle(.roundedBorder)

            Picker(Localizer.shared.text("settings.output_conflict_policy"), selection: $settings.outputConflictPolicy) {
                ForEach(OutputConflictPolicy.allCases) { policy in
                    Text(Localizer.shared.text("settings.output_conflict_\(policy.rawValue)")).tag(policy)
                }
            }

            SecureField(Localizer.shared.text("settings.archive_password"), text: $settings.archivePassword)

            Toggle(Localizer.shared.text("settings.drag_overlay"), isOn: $settings.dragOverlayEnabled)

            Picker(Localizer.shared.text("settings.drag_overlay_dock_side"), selection: $settings.dragOverlayDockSide) {
                Text(Localizer.shared.text("settings.drag_overlay_dock_side_left")).tag(DragOverlayDockSide.left)
                Text(Localizer.shared.text("settings.drag_overlay_dock_side_right")).tag(DragOverlayDockSide.right)
            }

            Divider()

            Toggle(isOn: Binding(
                get: { defaultAppService.isDefaultArchiveApp },
                set: { enabled in
                    if enabled {
                        defaultAppService.setAsDefaultArchiveApp()
                    } else {
                        defaultAppService.unsetAsDefaultArchiveApp()
                    }
                }
            )) {
                VStack(alignment: .leading, spacing: 4) {
                    Text(Localizer.shared.text("settings.default_app"))
                    Text(defaultAppService.statusText)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            VStack(alignment: .leading, spacing: 8) {
                Text(Localizer.shared.text("settings.finder_service"))

                HStack {
                    Button(Localizer.shared.text("settings.finder_service_refresh")) {
                        finderIntegration.refreshServices()
                    }
                    Button(Localizer.shared.text("settings.finder_service_open")) {
                        finderIntegration.openServicesSettings()
                    }
                }

                Text(finderIntegration.statusText.isEmpty
                     ? Localizer.shared.text("settings.finder_service_hint")
                     : finderIntegration.statusText)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            HStack {
                Spacer()
                Button(Localizer.shared.text("settings.cancel")) {
                    dismiss()
                }
                Button(Localizer.shared.text("settings.save")) {
                    settings.defaultSplitSizeMB = max(1, Int(splitSizeText.trimmingCharacters(in: .whitespacesAndNewlines)) ?? 100)
                    settings.defaultSplitPartCount = max(2, Int(splitPartCountText.trimmingCharacters(in: .whitespacesAndNewlines)) ?? 4)
                    Localizer.shared.setLanguage(settings.language)
                    dismiss()
                }
            }
        }
        .padding(20)
        .frame(width: 420)
        .onAppear {
            defaultAppService.refresh()
        }
    }
}
