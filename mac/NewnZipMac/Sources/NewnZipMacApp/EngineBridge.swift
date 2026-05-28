import Foundation

struct EngineBridge {
    private static var engineURL: URL? {
        let bundled = Bundle.main.bundleURL.appendingPathComponent("Contents/Frameworks/newnzip_engine/newnzip-engine")
        if FileManager.default.fileExists(atPath: bundled.path) {
            return bundled
        }

        let current = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
        let local = current
            .appendingPathComponent("../../native/newnzip_engine/bin/newnzip-engine")
            .standardizedFileURL
        return FileManager.default.fileExists(atPath: local.path) ? local : nil
    }

    static func compress(urls: [URL], format: ArchiveFormat, onLine: @escaping @Sendable (String) -> Void) async throws -> EngineResult {
        guard let first = urls.first else {
            return EngineResult(summary: "입력 항목이 없습니다.", logLines: [])
        }
        let archiveName = urls.count == 1 ? first.deletingPathExtension().lastPathComponent : "newnzip-bundle"
        let output = first.deletingLastPathComponent().appendingPathComponent("\(archiveName).\(format.rawValue)")

        if format == .zip, let engineURL {
            let args = ["create", output.path] + urls.map(\.path)
            return try await run(executable: engineURL, arguments: args, onLine: onLine)
        }

        let summary = "\(format.rawValue) 형식 압축은 아직 구현 중입니다: \(output.lastPathComponent)"
        onLine(summary)
        return EngineResult(summary: summary, logLines: [summary])
    }

    static func extract(urls: [URL], onLine: @escaping @Sendable (String) -> Void) async throws -> EngineResult {
        guard let engineURL, let first = urls.first else {
            return EngineResult(summary: "입력 항목이 없습니다.", logLines: [])
        }
        let output = first.deletingLastPathComponent().appendingPathComponent(first.deletingPathExtension().lastPathComponent)
        let args = ["extract", first.path, output.path]
        return try await run(executable: engineURL, arguments: args, onLine: onLine)
    }

    private static func run(
        executable: URL,
        arguments: [String],
        onLine: @escaping @Sendable (String) -> Void
    ) async throws -> EngineResult {
        let collector = OutputCollector(onLine: onLine)
        let process = Process()
        process.executableURL = executable
        process.arguments = arguments

        let output = Pipe()
        process.standardOutput = output
        process.standardError = output

        return try await withCheckedThrowingContinuation { continuation in
            output.fileHandleForReading.readabilityHandler = { handle in
                let data = handle.availableData
                if data.isEmpty {
                    return
                }
                collector.append(data: data)
            }

            process.terminationHandler = { finishedProcess in
                output.fileHandleForReading.readabilityHandler = nil
                collector.finish()

                let lines = collector.lines
                let summary = lines.last(where: { LogParser.parseProgressLine($0) == nil && !$0.isEmpty }) ?? ""

                if finishedProcess.terminationStatus == 0 {
                    continuation.resume(returning: EngineResult(summary: summary, logLines: lines))
                    return
                }

                continuation.resume(throwing: NSError(
                    domain: "NewnZipMac",
                    code: Int(finishedProcess.terminationStatus),
                    userInfo: [NSLocalizedDescriptionKey: summary.isEmpty ? "엔진 실행에 실패했습니다." : summary]
                ))
            }

            do {
                try process.run()
            } catch {
                output.fileHandleForReading.readabilityHandler = nil
                continuation.resume(throwing: error)
            }
        }
    }
}

private final class OutputCollector: @unchecked Sendable {
    private let lock = NSLock()
    private let onLine: @Sendable (String) -> Void
    private var buffer = Data()
    private(set) var lines: [String] = []

    init(onLine: @escaping @Sendable (String) -> Void) {
        self.onLine = onLine
    }

    func append(data: Data) {
        lock.lock()
        buffer.append(data)

        while let newlineRange = buffer.firstRange(of: Data([0x0A])) {
            let lineData = buffer.subdata(in: 0..<newlineRange.lowerBound)
            buffer.removeSubrange(0...newlineRange.lowerBound)
            let line = String(data: lineData, encoding: .utf8)?
                .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            if !line.isEmpty {
                lines.append(line)
                onLine(line)
            }
        }
        lock.unlock()
    }

    func finish() {
        lock.lock()
        if !buffer.isEmpty {
            let line = String(data: buffer, encoding: .utf8)?
                .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            if !line.isEmpty {
                lines.append(line)
                onLine(line)
            }
            buffer.removeAll(keepingCapacity: false)
        }
        lock.unlock()
    }
}
