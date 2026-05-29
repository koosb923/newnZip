import AppKit
import Foundation

@MainActor
final class FinderIntegrationService: ObservableObject {
    static let shared = FinderIntegrationService()

    @Published private(set) var statusText = ""

    private init() {}

    func refreshServices() {
        do {
            try installQuickActions()
            registerAppBundle()
            NSUpdateDynamicServices()
            run("/System/Library/CoreServices/pbs", arguments: ["-flush"])
            statusText = Localizer.shared.text("settings.finder_service_refreshed")
        } catch {
            statusText = error.localizedDescription
        }
    }

    func openServicesSettings() {
        let candidates = [
            "x-apple.systempreferences:com.apple.Keyboard-Settings.extension?KeyboardShortcuts",
            "x-apple.systempreferences:com.apple.Keyboard-Settings.extension?Shortcuts",
            "x-apple.systempreferences:com.apple.preference.keyboard?KeyboardShortcuts",
            "x-apple.systempreferences:com.apple.preference.keyboard?Shortcuts",
            "x-apple.systempreferences:com.apple.preference.keyboard?shortcuts",
            "x-apple.systempreferences:com.apple.Keyboard-Settings.extension"
        ]
        for candidate in candidates {
            guard let url = URL(string: candidate), NSWorkspace.shared.open(url) else {
                continue
            }
            runServicesNavigationScript()
            return
        }
        runServicesNavigationScript()
    }

    private func runServicesNavigationScript() {
        let script = """
        tell application "System Settings"
          activate
        end tell
        delay 1.5
        tell application "System Events"
          tell process "System Settings"
            set frontmost to true
            delay 0.8
            repeat with shortcutLabel in {"Keyboard Shortcuts…", "Keyboard Shortcuts...", "Keyboard Shortcuts", "키보드 단축키…", "키보드 단축키...", "키보드 단축키", "キーボードショートカット…", "キーボードショートカット"}
              try
                click (first UI element of entire contents of window 1 whose name is shortcutLabel)
                delay 1.0
                exit repeat
              end try
            end repeat
            repeat with serviceLabel in {"Services", "서비스", "サービス"}
              try
                click (first UI element of entire contents of window 1 whose name is serviceLabel)
                delay 0.8
                exit repeat
              end try
            end repeat
            repeat with fileFolderLabel in {"Files and Folders", "파일 및 폴더", "ファイルとフォルダ"}
              try
                click (first UI element of entire contents of window 1 whose name is fileFolderLabel)
                exit repeat
              end try
            end repeat
          end tell
        end tell
        """

        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
        process.arguments = ["-e", script]
        try? process.run()
    }

    private func installQuickActions() throws {
        let servicesDirectory = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Services", isDirectory: true)
        try FileManager.default.createDirectory(at: servicesDirectory, withIntermediateDirectories: true)

        let appPath = Bundle.main.bundleURL.path
        let compressWorkflow = servicesDirectory.appendingPathComponent("newnZip으로 압축하기.workflow", isDirectory: true)
        let extractWorkflow = servicesDirectory.appendingPathComponent("newnZip으로 압축 풀기.workflow", isDirectory: true)

        try writeWorkflow(
            at: compressWorkflow,
            name: "newnZip으로 압축하기",
            workflowID: "compress",
            command: commandString(appPath: appPath, mode: .compress)
        )
        try writeWorkflow(
            at: extractWorkflow,
            name: "newnZip으로 압축 풀기",
            workflowID: "extract",
            command: commandString(appPath: appPath, mode: .extract)
        )
    }

    private func writeWorkflow(at workflowURL: URL, name: String, workflowID: String, command: String) throws {
        let fileManager = FileManager.default
        if fileManager.fileExists(atPath: workflowURL.path) {
            try fileManager.removeItem(at: workflowURL)
        }

        let contentsURL = workflowURL.appendingPathComponent("Contents", isDirectory: true)
        let resourcesURL = contentsURL.appendingPathComponent("Resources", isDirectory: true)
        try fileManager.createDirectory(at: resourcesURL, withIntermediateDirectories: true)

        let info: [String: Any] = [
            "CFBundleIdentifier": "com.newntool.newnzip.\(workflowID)",
            "CFBundleName": name,
            "CFBundleDisplayName": name,
            "CFBundleShortVersionString": "1.0",
            "CFBundleVersion": "1",
            "NSServices": [[
                "NSMenuItem": ["default": name],
                "NSMessage": "runWorkflowAsService",
                "NSSendFileTypes": ["public.item"]
            ]]
        ]

        let workflow: [String: Any] = [
            "AMApplicationBuild": "521",
            "AMApplicationVersion": "2.10",
            "AMDocumentVersion": "2",
            "actions": [[
                "action": [
                    "AMAccepts": [
                        "Container": "List",
                        "Optional": false,
                        "Types": ["com.apple.cocoa.path"]
                    ],
                    "AMActionVersion": "2.0.3",
                    "AMApplication": ["Automator"],
                    "AMParameterProperties": [
                        "COMMAND_STRING": [:],
                        "CheckedForUserDefaultShell": [:],
                        "inputMethod": [:],
                        "shell": [:]
                    ],
                    "AMProvides": [
                        "Container": "List",
                        "Types": ["com.apple.cocoa.string"]
                    ],
                    "ActionBundlePath": "/System/Library/Automator/Run Shell Script.action",
                    "ActionName": "Run Shell Script",
                    "ActionParameters": [
                        "COMMAND_STRING": command,
                        "CheckedForUserDefaultShell": true,
                        "inputMethod": 1,
                        "shell": "/bin/bash"
                    ],
                    "BundleIdentifier": "com.apple.RunShellScript",
                    "CFBundleVersion": "2.0.3",
                    "CanShowSelectedItemsWhenRun": false,
                    "CanShowWhenRun": true,
                    "Category": ["AMCategoryUtilities"],
                    "Class Name": "RunShellScriptAction",
                    "InputUUID": "11111111-1111-1111-1111-111111111111",
                    "Keywords": ["Shell", "Script", "Command", "Run"],
                    "OutputUUID": "22222222-2222-2222-2222-222222222222",
                    "UUID": "33333333-3333-3333-3333-333333333333",
                    "UnlocalizedApplications": ["Automator"],
                    "arguments": [
                        "0": [
                            "default value": "",
                            "name": "COMMAND_STRING",
                            "required": "0",
                            "type": "0",
                            "uuid": "0"
                        ],
                        "1": [
                            "default value": "0",
                            "name": "inputMethod",
                            "required": "0",
                            "type": "0",
                            "uuid": "1"
                        ],
                        "2": [
                            "default value": "/bin/sh",
                            "name": "shell",
                            "required": "0",
                            "type": "0",
                            "uuid": "2"
                        ]
                    ],
                    "isViewVisible": true,
                    "location": "309.000000:336.000000",
                    "nibPath": "/System/Library/Automator/Run Shell Script.action/Contents/Resources/Base.lproj/main.nib"
                ],
                "isViewVisible": true
            ]],
            "connectors": [:],
            "workflowMetaData": [
                "applicationBundleIDsByPath": [:],
                "applicationPaths": [],
                "serviceApplicationBundleID": "com.apple.finder",
                "inputTypeIdentifier": "com.apple.Automator.fileSystemObject",
                "outputTypeIdentifier": "com.apple.Automator.nothing",
                "presentationMode": 15,
                "processesInput": true,
                "serviceInputTypeIdentifier": "com.apple.Automator.fileSystemObject",
                "serviceOutputTypeIdentifier": "com.apple.Automator.nothing",
                "serviceProcessesInput": true,
                "workflowTypeIdentifier": "com.apple.Automator.servicesMenu"
            ]
        ]

        try writePlist(info, to: contentsURL.appendingPathComponent("Info.plist"))
        let documentURL = resourcesURL.appendingPathComponent("document.wflow")
        try writePlist(workflow, to: documentURL)
        try fileManager.copyItem(at: documentURL, to: contentsURL.appendingPathComponent("document.wflow"))
    }

    private func writePlist(_ dictionary: [String: Any], to url: URL) throws {
        let data = try PropertyListSerialization.data(fromPropertyList: dictionary, format: .xml, options: 0)
        try data.write(to: url)
    }

    private func commandString(appPath: String, mode: WorkflowMode) -> String {
        let quotedAppPath = shellQuoted(appPath)
        let modeName = mode == .compress ? "compress" : "extract"
        let hudArgument = mode == .compress ? "--hud-compress" : "--hud-extract"
        return """
        LOG="/tmp/newnzip-quick-action.log"
        echo "$(date '+%F %T') \(modeName) invoked argc=$#" >> "$LOG"
        if [ "$#" -eq 0 ]; then
          ITEMS=()
          while IFS= read -r item; do
            [ -n "$item" ] && ITEMS+=("$item")
          done
          set -- "${ITEMS[@]}"
          echo "$(date '+%F %T') \(modeName) stdin argc=$#" >> "$LOG"
        fi
        APP_PATH=\(quotedAppPath)
        ENGINE="$APP_PATH/Contents/Frameworks/newnzip_engine/newnzip-engine"
        if [ ! -x "$ENGINE" ]; then
          echo "$(date '+%F %T') missing engine: $ENGINE" >> "$LOG"
          osascript -e 'display alert "newnZip 엔진을 찾을 수 없습니다."'
          exit 1
        fi
        if [ "$#" -lt 1 ]; then
          echo "$(date '+%F %T') no input paths" >> "$LOG"
          osascript -e 'display alert "선택된 파일이나 폴더를 받지 못했습니다."'
          exit 0
        fi
        open -n -a "$APP_PATH" --args \(hudArgument) "$@"
        """
    }

    private func shellQuoted(_ value: String) -> String {
        "'\(value.replacingOccurrences(of: "'", with: "'\\''"))'"
    }

    private func registerAppBundle() {
        let lsregister = "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
        run(lsregister, arguments: ["-u", Bundle.main.bundleURL.path])
        run(lsregister, arguments: ["-f", Bundle.main.bundleURL.path])
    }

    private func run(_ executable: String, arguments: [String]) {
        guard FileManager.default.isExecutableFile(atPath: executable) else {
            return
        }

        let process = Process()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        try? process.run()
        process.waitUntilExit()
    }
}

private enum WorkflowMode {
    case compress
    case extract
}
