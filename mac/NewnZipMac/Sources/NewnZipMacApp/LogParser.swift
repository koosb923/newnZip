import Foundation

enum LogParser {
    static func parseProgressLine(_ line: String) -> EngineProgress? {
        let parts = line.split(separator: "\t", omittingEmptySubsequences: false)
        guard parts.count >= 5, parts[0] == "NEWNZIP_PROGRESS" else {
            return nil
        }

        guard let completed = Int(parts[2]), let total = Int(parts[3]) else {
            return nil
        }

        return EngineProgress(
            stage: String(parts[1]),
            completed: completed,
            total: total,
            name: String(parts[4])
        )
    }
}
