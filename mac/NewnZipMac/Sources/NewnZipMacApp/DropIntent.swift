import Foundation

enum DropIntent {
    case compress([URL])
    case extract([URL])
}

enum DropResolver {
    static func resolve(urls: [URL]) -> DropIntent? {
        let archiveExtensions = ["zip", "7z", "rar", "tar", "tgz", "gz", "bz2", "xz", "iso", "img", "cbz"]
        let archives = urls.filter { archiveExtensions.contains($0.pathExtension.lowercased()) }
        let regular = urls.filter { !archiveExtensions.contains($0.pathExtension.lowercased()) }

        if !archives.isEmpty && regular.isEmpty {
            return .extract(archives)
        }
        if !regular.isEmpty && archives.isEmpty {
            return .compress(regular)
        }
        return nil
    }
}
