import Foundation

enum DropIntent {
    case compress([URL])
    case extract([URL])
    case chooseForMultipleArchives([URL])
}

enum DropResolver {
    private static let archiveExtensions = Set([
        "zip", "7z", "rar", "tar", "tgz", "gz", "bz2", "xz", "iso", "img", "cbz",
        "001", "z01", "zst", "zstd", "lz4", "br", "brotli", "cab", "wim", "arj",
        "lzh", "lha", "cpio", "rpm", "deb"
    ])

    static func isArchive(_ url: URL) -> Bool {
        let lowercasedPath = url.lastPathComponent.lowercased()
        if lowercasedPath.hasSuffix(".zip.001") || lowercasedPath.hasSuffix(".7z.001") {
            return true
        }
        return archiveExtensions.contains(url.pathExtension.lowercased())
    }

    static func resolve(urls: [URL]) -> DropIntent {
        let archives = urls.filter(isArchive)
        let regular = urls.filter { !isArchive($0) }

        if !archives.isEmpty && regular.isEmpty {
            return archives.count > 1 ? .chooseForMultipleArchives(archives) : .extract(archives)
        }
        if !regular.isEmpty && archives.isEmpty {
            return .compress(regular)
        }
        return .compress(urls)
    }
}
