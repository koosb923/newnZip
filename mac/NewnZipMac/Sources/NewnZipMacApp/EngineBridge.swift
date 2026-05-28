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

    private static var backendDirectoryURL: URL? {
        let bundled = Bundle.main.bundleURL.appendingPathComponent("Contents/Frameworks/newnzip_engine/backends")
        if FileManager.default.fileExists(atPath: bundled.path) {
            return bundled
        }

        let current = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
        let local = current
            .appendingPathComponent("../../native/newnzip_engine/backends")
            .standardizedFileURL
        return FileManager.default.fileExists(atPath: local.path) ? local : nil
    }

    static func outputCollisionExists(urls: [URL], format: ArchiveFormat, splitSizeMB: Int) -> Bool {
        guard let first = urls.first else {
            return false
        }
        let archiveName = urls.count == 1 ? first.deletingPathExtension().lastPathComponent : "newnzip-bundle"
        let requestedOutput = first.deletingLastPathComponent().appendingPathComponent("\(archiveName).\(format.rawValue)")
        let collisionURL = splitSizeMB > 0
            ? URL(fileURLWithPath: requestedOutput.path + ".001")
            : requestedOutput
        return FileManager.default.fileExists(atPath: collisionURL.path)
    }

    static func compress(
        urls: [URL],
        format: ArchiveFormat,
        zipMethod: ZipMethod = .auto,
        splitSizeMB: Int = 0,
        conflictPolicy: OutputConflictPolicy = .append,
        onLine: @escaping @Sendable (String) -> Void
    ) async throws -> EngineResult {
        guard let first = urls.first else {
            return EngineResult(summary: "입력 항목이 없습니다.", logLines: [])
        }
        let archiveName = urls.count == 1 ? first.deletingPathExtension().lastPathComponent : "newnzip-bundle"
        let requestedOutput = first.deletingLastPathComponent().appendingPathComponent("\(archiveName).\(format.rawValue)")
        let output = resolvedOutputURL(for: requestedOutput, splitSizeMB: splitSizeMB, conflictPolicy: conflictPolicy)

        guard let engineURL else {
            let summary = "newnzip-engine을 찾지 못했습니다."
            onLine(summary)
            return EngineResult(summary: summary, logLines: [summary])
        }

        if splitSizeMB > 0 && format != .zip {
            let summary = "분할 압축은 현재 zip 형식만 지원합니다."
            onLine(summary)
            return EngineResult(summary: summary, logLines: [summary])
        }

        var args = ["create", "--format=\(format.rawValue)"]
        if format == .zip {
            args.append("--method=\(zipMethod.rawValue)")
        }
        if splitSizeMB > 0 {
            args.append("--split=\(splitSizeMB)m")
        }
        args.append(output.path)
        args.append(contentsOf: urls.map(\.path))
        return try await run(executable: engineURL, arguments: args, onLine: onLine)
    }

    static func extract(urls: [URL], onLine: @escaping @Sendable (String) -> Void) async throws -> EngineResult {
        guard let first = urls.first else {
            return EngineResult(summary: "입력 항목이 없습니다.", logLines: [])
        }
        let output = first.deletingLastPathComponent().appendingPathComponent(extractionDirectoryName(for: first))

        if let engineURL {
            let args = ["extract", first.path, output.path]
            return try await run(executable: engineURL, arguments: args, onLine: onLine)
        }

        let summary = "newnzip-engine을 찾지 못했습니다."
        onLine(summary)
        return EngineResult(summary: summary, logLines: [summary])
    }

    private static func extractionDirectoryName(for url: URL) -> String {
        let name = url.lastPathComponent
        if name.hasSuffix(".zip.001") || name.hasSuffix(".7z.001") {
            return url.deletingPathExtension().deletingPathExtension().lastPathComponent
        }
        if name.hasSuffix(".tar.gz") || name.hasSuffix(".tar.bz2") ||
            name.hasSuffix(".tar.xz") || name.hasSuffix(".tar.zstd") ||
            name.hasSuffix(".tar.zst") {
            return url.deletingPathExtension().deletingPathExtension().lastPathComponent
        }
        if url.pathExtension.lowercased() == "001" {
            return url.deletingPathExtension().lastPathComponent
        }
        return url.deletingPathExtension().lastPathComponent
    }

    private static func isSplitArchiveStart(_ url: URL) -> Bool {
        let name = url.lastPathComponent.lowercased()
        return name.hasSuffix(".zip.001")
            || name.hasSuffix(".7z.001")
            || url.pathExtension.lowercased() == "001"
    }

    private static func resolvedOutputURL(
        for requestedOutput: URL,
        splitSizeMB: Int,
        conflictPolicy: OutputConflictPolicy
    ) -> URL {
        let fileManager = FileManager.default
        let collisionURL = splitSizeMB > 0
            ? URL(fileURLWithPath: requestedOutput.path + ".001")
            : requestedOutput

        if conflictPolicy == .overwrite {
            if splitSizeMB > 0 {
                removeSplitOutputs(for: requestedOutput)
            } else {
                try? fileManager.removeItem(at: requestedOutput)
            }
            return requestedOutput
        }

        guard fileManager.fileExists(atPath: collisionURL.path) else {
            return requestedOutput
        }

        let directory = requestedOutput.deletingLastPathComponent()
        let baseName = requestedOutput.deletingPathExtension().lastPathComponent
        let pathExtension = requestedOutput.pathExtension
        for index in 2...9999 {
            let candidate = directory.appendingPathComponent("\(baseName) \(index)").appendingPathExtension(pathExtension)
            let candidateCollision = splitSizeMB > 0
                ? URL(fileURLWithPath: candidate.path + ".001")
                : candidate
            if !fileManager.fileExists(atPath: candidateCollision.path) {
                return candidate
            }
        }
        return requestedOutput
    }

    private static func splitFile(source: URL, destinationBase: URL, splitSizeMB: Int) throws {
        let chunkSize = max(1, splitSizeMB) * 1024 * 1024
        let input = try FileHandle(forReadingFrom: source)
        defer { try? input.close() }

        var index = 1
        while true {
            let data = try input.read(upToCount: chunkSize) ?? Data()
            if data.isEmpty {
                break
            }
            let partURL = URL(fileURLWithPath: "\(destinationBase.path).\(String(format: "%03d", index))")
            try data.write(to: partURL, options: .atomic)
            index += 1
        }
    }

    private static func joinSplitFiles(start: URL, destination: URL) throws {
        let output = FileManager.default.createFile(atPath: destination.path, contents: nil)
        if !output {
            throw NSError(domain: "NewnZipMac", code: 1, userInfo: [NSLocalizedDescriptionKey: "임시 파일을 만들지 못했습니다."])
        }
        let writer = try FileHandle(forWritingTo: destination)
        defer { try? writer.close() }

        let startPath = start.path
        let basePath = startPath.hasSuffix(".001") ? String(startPath.dropLast(4)) : start.deletingPathExtension().path
        for index in 1...9999 {
            let partURL = URL(fileURLWithPath: "\(basePath).\(String(format: "%03d", index))")
            if !FileManager.default.fileExists(atPath: partURL.path) {
                break
            }
            let reader = try FileHandle(forReadingFrom: partURL)
            defer { try? reader.close() }
            while true {
                let data = try reader.read(upToCount: 1024 * 1024) ?? Data()
                if data.isEmpty {
                    break
                }
                try writer.write(contentsOf: data)
            }
        }
    }

    private static func removeSplitOutputs(for destinationBase: URL) {
        for index in 1...9999 {
            let partURL = URL(fileURLWithPath: "\(destinationBase.path).\(String(format: "%03d", index))")
            if !FileManager.default.fileExists(atPath: partURL.path) {
                break
            }
            try? FileManager.default.removeItem(at: partURL)
        }
    }

    private static func run(
        executable: URL,
        arguments: [String],
        onLine: @escaping @Sendable (String) -> Void
    ) async throws -> EngineResult {
        let collector = OutputCollector(onLine: onLine)
        let processBox = ProcessBox()

        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                let process = Process()
                process.executableURL = executable
                process.arguments = arguments
                if let backendDirectoryURL {
                    var environment = ProcessInfo.processInfo.environment
                    environment["NEWNZIP_BACKEND_DIR"] = backendDirectoryURL.path
                    process.environment = environment
                }
                processBox.process = process

                let output = Pipe()
                process.standardOutput = output
                process.standardError = output

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
                    processBox.process = nil

                    if processBox.isCancelled {
                        continuation.resume(throwing: CancellationError())
                        return
                    }

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
                    if Task.isCancelled {
                        process.terminate()
                    }
                } catch {
                    output.fileHandleForReading.readabilityHandler = nil
                    processBox.process = nil
                    continuation.resume(throwing: error)
                }
            }
        } onCancel: {
            processBox.cancel()
        }
    }
}

private final class ProcessBox: @unchecked Sendable {
    private let lock = NSLock()
    private var currentProcess: Process?
    private var cancelled = false

    var process: Process? {
        get {
            lock.lock()
            defer { lock.unlock() }
            return currentProcess
        }
        set {
            lock.lock()
            currentProcess = newValue
            lock.unlock()
        }
    }

    var isCancelled: Bool {
        lock.lock()
        defer { lock.unlock() }
        return cancelled
    }

    func cancel() {
        lock.lock()
        cancelled = true
        let process = currentProcess
        lock.unlock()
        process?.terminate()
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
