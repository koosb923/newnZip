import Foundation

enum HUDCommandKind {
    case compress
    case extract
}

struct HUDCommand {
    let kind: HUDCommandKind
    let urls: [URL]

    static func parse(arguments: [String]) -> HUDCommand? {
        guard let command = arguments.first else {
            return nil
        }

        let kind: HUDCommandKind
        switch command {
        case "--hud-compress":
            kind = .compress
        case "--hud-extract":
            kind = .extract
        default:
            return nil
        }

        let urls = arguments.dropFirst().map { URL(fileURLWithPath: $0) }
        guard !urls.isEmpty else {
            return nil
        }
        return HUDCommand(kind: kind, urls: urls)
    }
}
