import SwiftUI

struct SettingsView: View {
    @StateObject private var settings = AppSettings.shared
    @StateObject private var defaultAppService = DefaultArchiveAppService.shared
    @StateObject private var finderIntegration = FinderIntegrationService.shared
    @Environment(\.dismiss) private var dismiss

    var onClose: (() -> Void)? = nil

    @State private var selectedTab: SettingsTab = .general
    @State private var splitSizeText = "\(AppSettings.shared.defaultSplitSizeMB)"
    @State private var splitPartCountText = "\(AppSettings.shared.defaultSplitPartCount)"
    @State private var draftPassword = AppSettings.shared.archivePassword
    @State private var revealsPassword = false

    var body: some View {
        VStack(spacing: 0) {
            VStack(spacing: 14) {
                HStack(spacing: 10) {
                    ForEach(SettingsTab.allCases, id: \.self) { tab in
                        settingsTabButton(for: tab)
                    }
                }
                .padding(14)

                Group {
                    switch selectedTab {
                    case .general:
                        generalTab
                    case .compression:
                        compressionTab
                    case .overlay:
                        overlayTab
                    case .integration:
                        integrationTab
                    }
                }
            }
            .padding(.horizontal, 20)
            .padding(.top, 18)

            Divider()
                .padding(.top, 12)

            HStack {
                Spacer()
                Button(Localizer.shared.text("settings.cancel")) {
                    closeView()
                }
                Button(Localizer.shared.text("settings.save")) {
                    saveAndDismiss()
                }
                .keyboardShortcut(.defaultAction)
            }
            .padding(20)
        }
        .frame(width: 720, height: 560)
        .onAppear {
            defaultAppService.refresh()
        }
    }

    private var generalTab: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                settingsGroup(
                    title: Localizer.shared.text("settings.section_general"),
                    subtitle: Localizer.shared.text("settings.section_general_hint")
                ) {
                    labeledRow(Localizer.shared.text("settings.language")) {
                        Picker("", selection: $settings.language) {
                            ForEach(AppLanguage.allCases) { language in
                                Text(language.displayName).tag(language)
                            }
                        }
                        .labelsHidden()
                        .frame(maxWidth: 220)
                    }

                    labeledRow(Localizer.shared.text("settings.output_conflict_policy")) {
                        Picker("", selection: $settings.outputConflictPolicy) {
                            ForEach(OutputConflictPolicy.allCases) { policy in
                                Text(Localizer.shared.text("settings.output_conflict_\(policy.rawValue)")).tag(policy)
                            }
                        }
                        .labelsHidden()
                        .frame(maxWidth: 220)
                    }
                }
            }
            .padding(.vertical, 12)
        }
    }

    private var compressionTab: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                settingsGroup(
                    title: Localizer.shared.text("settings.section_compression"),
                    subtitle: Localizer.shared.text("settings.section_compression_hint")
                ) {
                    labeledRow(Localizer.shared.text("settings.default_format")) {
                        Picker("", selection: $settings.defaultFormat) {
                            ForEach(ArchiveFormat.allCases) { format in
                                Text(format.rawValue.uppercased()).tag(format)
                            }
                        }
                        .labelsHidden()
                        .frame(maxWidth: 220)
                    }

                    labeledRow(Localizer.shared.text("settings.zip_method")) {
                        Picker("", selection: $settings.zipMethod) {
                            ForEach(ZipMethod.allCases) { method in
                                Text(Localizer.shared.text("settings.zip_method_\(method.rawValue)")).tag(method)
                            }
                        }
                        .labelsHidden()
                        .frame(maxWidth: 220)
                    }
                }

                settingsGroup(
                    title: Localizer.shared.text("settings.section_split"),
                    subtitle: Localizer.shared.text("settings.section_split_hint")
                ) {
                    labeledRow(Localizer.shared.text("settings.default_split_size_mb")) {
                        TextField(Localizer.shared.text("settings.default_split_size_mb"), text: $splitSizeText)
                            .textFieldStyle(.roundedBorder)
                            .frame(maxWidth: 160)
                    }

                    labeledRow(Localizer.shared.text("settings.default_split_part_count")) {
                        TextField(Localizer.shared.text("settings.default_split_part_count"), text: $splitPartCountText)
                            .textFieldStyle(.roundedBorder)
                            .frame(maxWidth: 160)
                    }
                }

                settingsGroup(
                    title: Localizer.shared.text("settings.section_password"),
                    subtitle: Localizer.shared.text("settings.section_password_hint")
                ) {
                    HStack(alignment: .center, spacing: 10) {
                        Group {
                            if revealsPassword {
                                TextField(Localizer.shared.text("settings.archive_password"), text: $draftPassword)
                            } else {
                                SecureField(Localizer.shared.text("settings.archive_password"), text: $draftPassword)
                            }
                        }
                        .textFieldStyle(.roundedBorder)

                        Button {
                            revealsPassword.toggle()
                        } label: {
                            Image(systemName: revealsPassword ? "eye.slash" : "eye")
                                .frame(width: 20, height: 20)
                        }
                        .buttonStyle(.borderless)
                        .help(Localizer.shared.text(revealsPassword ? "settings.password_hide" : "settings.password_show"))
                    }
                }
            }
            .padding(.vertical, 12)
        }
    }

    private var overlayTab: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                settingsGroup(
                    title: Localizer.shared.text("settings.section_overlay"),
                    subtitle: Localizer.shared.text("settings.section_overlay_hint")
                ) {
                    Toggle(Localizer.shared.text("settings.drag_overlay"), isOn: $settings.dragOverlayEnabled)

                    labeledRow(Localizer.shared.text("settings.background_display_mode")) {
                        Picker("", selection: $settings.backgroundDisplayMode) {
                            Text(Localizer.shared.text("settings.background_display_mode_dock")).tag(BackgroundDisplayMode.dock)
                            Text(Localizer.shared.text("settings.background_display_mode_menu_bar")).tag(BackgroundDisplayMode.menuBar)
                        }
                        .labelsHidden()
                        .frame(maxWidth: 180)
                    }

                    labeledRow(Localizer.shared.text("settings.drag_overlay_dock_side")) {
                        Picker("", selection: $settings.dragOverlayDockSide) {
                            Text(Localizer.shared.text("settings.drag_overlay_dock_side_left")).tag(DragOverlayDockSide.left)
                            Text(Localizer.shared.text("settings.drag_overlay_dock_side_right")).tag(DragOverlayDockSide.right)
                        }
                        .labelsHidden()
                        .frame(maxWidth: 180)
                    }
                }
            }
            .padding(.vertical, 12)
        }
    }

    private var integrationTab: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                settingsGroup(
                    title: Localizer.shared.text("settings.section_integration"),
                    subtitle: Localizer.shared.text("settings.section_integration_hint")
                ) {
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
                }

                settingsGroup(
                    title: Localizer.shared.text("settings.finder_service"),
                    subtitle: Localizer.shared.text("settings.finder_service_hint")
                ) {
                    HStack {
                        Button(Localizer.shared.text("settings.finder_service_refresh")) {
                            finderIntegration.refreshServices()
                        }
                        Button(Localizer.shared.text("settings.finder_service_open")) {
                            finderIntegration.openServicesSettings()
                        }
                    }

                    if !finderIntegration.statusText.isEmpty {
                        Text(finderIntegration.statusText)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
            .padding(.vertical, 12)
        }
    }

    private func settingsGroup<Content: View>(
        title: String,
        subtitle: String,
        @ViewBuilder content: () -> Content
    ) -> some View {
        GroupBox {
            VStack(alignment: .leading, spacing: 14) {
                content()
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.top, 4)
        } label: {
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .font(.headline)
                Text(subtitle)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private func labeledRow<Content: View>(_ title: String, @ViewBuilder content: () -> Content) -> some View {
        HStack(alignment: .center, spacing: 16) {
            Text(title)
                .frame(width: 170, alignment: .leading)
            content()
            Spacer(minLength: 0)
        }
    }

    private func settingsTabButton(for tab: SettingsTab) -> some View {
        let isSelected = selectedTab == tab
        let title = Localizer.shared.text(tab.titleKey)

        return Button {
            selectedTab = tab
        } label: {
            VStack(spacing: 8) {
                Image(systemName: tab.systemImage)
                    .font(.system(size: 24, weight: .medium))
                Text(title)
                    .font(.system(size: 13, weight: .medium))
            }
            .foregroundStyle(isSelected ? Color.accentColor : Color.secondary)
            .frame(maxWidth: .infinity, minHeight: 78)
            .padding(.vertical, 12)
            .background(
                RoundedRectangle(cornerRadius: 14)
                    .fill(isSelected ? Color.accentColor.opacity(0.12) : Color.clear)
            )
            .contentShape(RoundedRectangle(cornerRadius: 14))
        }
        .buttonStyle(.plain)
        .frame(maxWidth: .infinity)
    }

    private func saveAndDismiss() {
        settings.defaultSplitSizeMB = max(1, Int(splitSizeText.trimmingCharacters(in: .whitespacesAndNewlines)) ?? 100)
        settings.defaultSplitPartCount = max(2, Int(splitPartCountText.trimmingCharacters(in: .whitespacesAndNewlines)) ?? 4)
        settings.archivePassword = draftPassword
        Localizer.shared.setLanguage(settings.language)
        closeView()
    }

    private func closeView() {
        if let onClose {
            onClose()
        } else {
            dismiss()
        }
    }
}

private enum SettingsTab: Hashable, CaseIterable {
    case general
    case compression
    case overlay
    case integration

    var titleKey: String {
        switch self {
        case .general:
            return "settings.tab_general"
        case .compression:
            return "settings.tab_compression"
        case .overlay:
            return "settings.tab_overlay"
        case .integration:
            return "settings.tab_integration"
        }
    }

    var systemImage: String {
        switch self {
        case .general:
            return "gearshape"
        case .compression:
            return "archivebox"
        case .overlay:
            return "rectangle.lefthalf.inset.filled.arrow.left"
        case .integration:
            return "slider.horizontal.3"
        }
    }
}
