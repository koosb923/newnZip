#!/bin/bash
set -euo pipefail

APP_PATH="${1:-/Applications/newnZip.app}"
SET_DEFAULT="${2:-}"

if [ ! -d "$APP_PATH" ]; then
  echo "App bundle not found: $APP_PATH"
  exit 1
fi

/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$APP_PATH"
echo "Launch Services refreshed for $APP_PATH"

SERVICES_DIR="$HOME/Library/Services"
mkdir -p "$SERVICES_DIR"

write_workflow() {
  local workflow_path="$1"
  local command_string="$2"

  rm -rf "$workflow_path"
  mkdir -p "$workflow_path/Contents"
  /usr/libexec/PlistBuddy -c "Clear dict" \
    -c "Add :CFBundleIdentifier string com.newntool.newnzip.$(basename "$workflow_path" .workflow)" \
    -c "Add :CFBundleName string $(basename "$workflow_path" .workflow)" \
    -c "Add :CFBundlePackageType string APPL" \
    "$workflow_path/Contents/Info.plist" >/dev/null

  python3 - "$workflow_path/Contents/document.wflow" "$command_string" <<'PY'
import plistlib
import sys

output_path = sys.argv[1]
command = sys.argv[2]
workflow = {
    "AMApplicationBuild": "521",
    "AMApplicationVersion": "2.10",
    "actions": [
        {
            "action": {
                "AMAccepts": {
                    "Container": "List",
                    "Optional": False,
                    "Types": ["com.apple.cocoa.path"],
                },
                "AMActionVersion": "2.0.3",
                "AMApplication": ["Automator"],
                "AMParameterProperties": {
                    "COMMAND_STRING": {},
                    "CheckedForUserDefaultShell": {},
                    "inputMethod": {},
                    "shell": {},
                },
                "AMProvides": {
                    "Container": "List",
                    "Types": ["com.apple.cocoa.string"],
                },
                "ActionBundlePath": "/System/Library/Automator/Run Shell Script.action",
                "ActionName": "Run Shell Script",
                "ActionParameters": {
                    "COMMAND_STRING": command,
                    "CheckedForUserDefaultShell": True,
                    "inputMethod": 1,
                    "shell": "/bin/bash",
                },
                "BundleIdentifier": "com.apple.RunShellScript",
                "CFBundleVersion": "2.0.3",
                "CanShowSelectedItemsWhenRun": False,
                "CanShowWhenRun": True,
                "Category": ["AMCategoryUtilities"],
                "Class Name": "RunShellScriptAction",
                "InputUUID": "11111111-1111-1111-1111-111111111111",
                "Keywords": ["Shell", "Script", "Command", "Run"],
                "OutputUUID": "22222222-2222-2222-2222-222222222222",
                "UUID": "33333333-3333-3333-3333-333333333333",
                "UnlocalizedApplications": ["Automator"],
                "arguments": {
                    "0": {
                        "default value": "",
                        "name": "COMMAND_STRING",
                        "required": "0",
                        "type": "0",
                        "uuid": "0",
                    },
                    "1": {
                        "default value": "0",
                        "name": "inputMethod",
                        "required": "0",
                        "type": "0",
                        "uuid": "1",
                    },
                    "2": {
                        "default value": "/bin/sh",
                        "name": "shell",
                        "required": "0",
                        "type": "0",
                        "uuid": "2",
                    },
                },
                "isViewVisible": True,
                "location": "309.000000:336.000000",
                "nibPath": "/System/Library/Automator/Run Shell Script.action/Contents/Resources/Base.lproj/main.nib",
            },
            "isViewVisible": True,
        }
    ],
    "connectors": {},
    "workflowMetaData": {
        "applicationBundleIDsByPath": {},
        "applicationPaths": [],
        "inputTypeIdentifier": "com.apple.Automator.fileSystemObject",
        "outputTypeIdentifier": "com.apple.Automator.nothing",
        "presentationMode": 15,
        "processesInput": True,
        "serviceInputTypeIdentifier": "com.apple.Automator.fileSystemObject",
        "serviceOutputTypeIdentifier": "com.apple.Automator.nothing",
        "serviceProcessesInput": True,
    },
}

with open(output_path, "wb") as f:
    plistlib.dump(workflow, f)
PY
}

COMPRESS_COMMAND=$(cat <<EOF
APP_PATH="$APP_PATH"
ENGINE="\$APP_PATH/Contents/Frameworks/newnzip_engine/newnzip-engine"
if [ ! -x "\$ENGINE" ]; then
  osascript -e 'display alert "newnZip 엔진을 찾을 수 없습니다."'
  exit 1
fi
if [ "\$#" -lt 1 ]; then
  exit 0
fi
FIRST="\$1"
PARENT=\$(dirname "\$FIRST")
NAME=\$(basename "\$FIRST")
BASE="\${NAME%.*}"
if [ -d "\$FIRST" ]; then
  BASE="\$NAME"
fi
OUTPUT="\$PARENT/\$BASE.zip"
"\$ENGINE" create "\$OUTPUT" "\$@"
osascript -e "display notification \"\$OUTPUT\" with title \"newnZip 압축 완료\""
EOF
)

EXTRACT_COMMAND=$(cat <<EOF
APP_PATH="$APP_PATH"
ENGINE="\$APP_PATH/Contents/Frameworks/newnzip_engine/newnzip-engine"
SEVENZIP=""
for candidate in /opt/homebrew/bin/7zz /usr/local/bin/7zz /opt/homebrew/bin/7z /usr/local/bin/7z /usr/bin/7zz /usr/bin/7z; do
  if [ -x "\$candidate" ]; then
    SEVENZIP="\$candidate"
    break
  fi
done
for item in "\$@"; do
  DIR=\$(dirname "\$item")
  NAME=\$(basename "\$item")
  LOWER=\$(printf "%s" "\$NAME" | tr '[:upper:]' '[:lower:]')
  BASE="\${NAME%.*}"
  if [[ "\$LOWER" == *.zip.001 || "\$LOWER" == *.7z.001 || "\$LOWER" == *.tar.gz || "\$LOWER" == *.tar.bz2 || "\$LOWER" == *.tar.xz ]]; then
    BASE="\${BASE%.*}"
  fi
  DEST="\$DIR/\$BASE"
  if [[ "\$LOWER" == *.zip && -x "\$ENGINE" ]]; then
    "\$ENGINE" extract "\$item" "\$DEST"
  elif [ -n "\$SEVENZIP" ]; then
    "\$SEVENZIP" x "\$item" "-o\$DEST" -y
  else
    osascript -e 'display alert "이 압축 파일을 풀려면 7zz 또는 7z가 필요합니다."'
    exit 1
  fi
done
osascript -e 'display notification "선택한 항목 처리가 끝났습니다." with title "newnZip 압축 풀기 완료"'
EOF
)

write_workflow "$SERVICES_DIR/newnZip으로 압축하기.workflow" "$COMPRESS_COMMAND"
write_workflow "$SERVICES_DIR/newnZip으로 압축 풀기.workflow" "$EXTRACT_COMMAND"
echo "Finder Quick Actions installed in $SERVICES_DIR"

if [ "$SET_DEFAULT" = "--set-default" ]; then
  /usr/bin/swift - "$APP_PATH" <<'SWIFT'
import CoreServices
import Foundation

let bundleURL = URL(fileURLWithPath: CommandLine.arguments[1])
guard let bundle = Bundle(url: bundleURL),
      let identifier = bundle.bundleIdentifier else {
    exit(1)
}

let types = [
    "public.zip-archive",
    "org.gnu.gnu-zip-archive",
    "public.tar-archive",
    "org.7-zip.7-zip-archive",
    "com.rarlab.rar-archive",
    "com.newntool.split-archive-volume"
] as CFArray

for type in types as! [String] {
    LSSetDefaultRoleHandlerForContentType(type as CFString, .all, identifier as CFString)
}
SWIFT
  echo "Default archive handlers pointed to $APP_PATH"
fi
