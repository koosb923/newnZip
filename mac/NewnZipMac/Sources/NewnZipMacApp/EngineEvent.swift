import Foundation

enum EngineOperationKind {
    case compress
    case extract
}

struct EngineProgress {
    let stage: String
    let completed: Int
    let total: Int
    let name: String

    var fraction: Double {
        guard total > 0 else { return 0.0 }
        return Double(completed) / Double(total)
    }
}

struct EngineResult {
    let summary: String
    let logLines: [String]
}
