import AppKit
import CoreServices
import Foundation

@MainActor
final class DefaultArchiveAppService: ObservableObject {
    static let shared = DefaultArchiveAppService()

    @Published private(set) var isDefaultArchiveApp = false
    @Published private(set) var statusText = ""

    private let archiveTypes = [
        "public.zip-archive",
        "org.gnu.gnu-zip-archive",
        "public.tar-archive",
        "org.7-zip.7-zip-archive",
        "com.rarlab.rar-archive",
        "com.newntool.split-archive-volume",
        "com.newntool.archive.tgz",
        "com.newntool.archive.tbz2",
        "com.newntool.archive.txz",
        "com.newntool.archive.zstd",
        "com.newntool.archive.lz4",
        "com.newntool.archive.brotli",
        "com.newntool.archive.cab",
        "com.newntool.archive.iso",
        "com.newntool.archive.wim",
        "com.newntool.archive.arj",
        "com.newntool.archive.lzh",
        "com.newntool.archive.cpio",
        "com.newntool.archive.rpm",
        "com.newntool.archive.deb",
        "com.newntool.archive.img"
    ]

    private var bundleIdentifier: String {
        Bundle.main.bundleIdentifier ?? "com.newntool.newnzip"
    }

    private var archiveUtilityBundleIdentifier: String {
        let archiveUtilityURL = URL(fileURLWithPath: "/System/Library/CoreServices/Applications/Archive Utility.app")
        return Bundle(url: archiveUtilityURL)?.bundleIdentifier ?? "com.apple.archiveutility"
    }

    private init() {
        refresh()
    }

    func refresh() {
        isDefaultArchiveApp = archiveTypes.allSatisfy { type in
            guard let handler = LSCopyDefaultRoleHandlerForContentType(type as CFString, .all)?.takeRetainedValue() as String? else {
                return false
            }
            return handler == bundleIdentifier
        }
        statusText = isDefaultArchiveApp
            ? Localizer.shared.text("settings.default_app_active")
            : Localizer.shared.text("settings.default_app_inactive")
    }

    func setAsDefaultArchiveApp() {
        for type in archiveTypes {
            LSSetDefaultRoleHandlerForContentType(type as CFString, .all, bundleIdentifier as CFString)
        }
        refresh()
    }

    func unsetAsDefaultArchiveApp() {
        for type in archiveTypes {
            LSSetDefaultRoleHandlerForContentType(type as CFString, .all, archiveUtilityBundleIdentifier as CFString)
        }
        refresh()
    }
}
