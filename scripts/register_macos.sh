#!/bin/bash
set -euo pipefail

APP_PATH="${1:-/Applications/newnZip.app}"
SET_DEFAULT="${2:-}"

if [ ! -d "$APP_PATH" ]; then
  echo "App bundle not found: $APP_PATH"
  exit 1
fi

LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
"$LSREGISTER" -u "$APP_PATH" >/dev/null 2>&1 || true
"$LSREGISTER" -f "$APP_PATH"
echo "Launch Services refreshed for $APP_PATH"

SERVICES_DIR="$HOME/Library/Services"
mkdir -p "$SERVICES_DIR"

write_workflow() {
  local workflow_path="$1"
  local command_string="$2"
  local workflow_id="$3"

  rm -rf "$workflow_path"
  mkdir -p "$workflow_path/Contents/Resources"
  local workflow_name
  workflow_name="$(basename "$workflow_path" .workflow)"
  python3 - "$workflow_path/Contents/Info.plist" "$workflow_name" "$workflow_id" <<'PY'
import plistlib
import sys

output_path, workflow_name, workflow_id = sys.argv[1:4]
info = {
    "CFBundleIdentifier": f"com.newntool.newnzip.{workflow_id}",
    "CFBundleName": workflow_name,
    "CFBundleDisplayName": workflow_name,
    "CFBundleShortVersionString": "1.0",
    "CFBundleVersion": "1",
    "NSServices": [
        {
            "NSMenuItem": {"default": workflow_name},
            "NSMessage": "runWorkflowAsService",
            "NSSendFileTypes": ["public.item"],
        }
    ],
}

with open(output_path, "wb") as f:
    plistlib.dump(info, f)
PY

  python3 - "$workflow_path/Contents/Resources/document.wflow" "$command_string" <<'PY'
import plistlib
import sys

output_path = sys.argv[1]
command = sys.argv[2]
workflow = {
    "AMApplicationBuild": "521",
    "AMApplicationVersion": "2.10",
    "AMDocumentVersion": "2",
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
        "serviceApplicationBundleID": "com.apple.finder",
        "inputTypeIdentifier": "com.apple.Automator.fileSystemObject",
        "outputTypeIdentifier": "com.apple.Automator.nothing",
        "presentationMode": 15,
        "processesInput": True,
        "serviceInputTypeIdentifier": "com.apple.Automator.fileSystemObject",
        "serviceOutputTypeIdentifier": "com.apple.Automator.nothing",
        "serviceProcessesInput": True,
        "workflowTypeIdentifier": "com.apple.Automator.servicesMenu",
    },
}

with open(output_path, "wb") as f:
    plistlib.dump(workflow, f)
PY
  cp "$workflow_path/Contents/Resources/document.wflow" "$workflow_path/Contents/document.wflow"
}

COMPRESS_COMMAND=$(cat <<EOF
LOG="/tmp/newnzip-quick-action.log"
echo "\$(date '+%F %T') compress invoked argc=\$#" >> "\$LOG"
if [ "\$#" -eq 0 ]; then
  ITEMS=()
  while IFS= read -r item; do
    [ -n "\$item" ] && ITEMS+=("\$item")
  done
  set -- "\${ITEMS[@]}"
  echo "\$(date '+%F %T') compress stdin argc=\$#" >> "\$LOG"
fi
APP_PATH="$APP_PATH"
ENGINE="\$APP_PATH/Contents/Frameworks/newnzip_engine/newnzip-engine"
if [ ! -x "\$ENGINE" ]; then
  echo "\$(date '+%F %T') missing engine: \$ENGINE" >> "\$LOG"
  osascript -e 'display alert "newnZip 엔진을 찾을 수 없습니다."'
  exit 1
fi
if [ "\$#" -lt 1 ]; then
  echo "\$(date '+%F %T') no input paths" >> "\$LOG"
  osascript -e 'display alert "선택된 파일이나 폴더를 받지 못했습니다."'
  exit 0
fi
FIRST="\$1"
echo "\$(date '+%F %T') first=\$FIRST" >> "\$LOG"
PARENT=\$(dirname "\$FIRST")
NAME=\$(basename "\$FIRST")
BASE="\${NAME%.*}"
if [ -d "\$FIRST" ]; then
  BASE="\$NAME"
fi
OUTPUT="\$PARENT/\$BASE.zip"
if [ -e "\$OUTPUT" ]; then
  INDEX=2
  while [ -e "\$PARENT/\$BASE \$INDEX.zip" ]; do
    INDEX=\$((INDEX + 1))
  done
  OUTPUT="\$PARENT/\$BASE \$INDEX.zip"
fi
echo "\$(date '+%F %T') output=\$OUTPUT" >> "\$LOG"
if "\$ENGINE" create "\$OUTPUT" "\$@" >> "\$LOG" 2>&1; then
  /usr/bin/open -R "\$OUTPUT"
  osascript -e "display notification \"\$OUTPUT\" with title \"newnZip 압축 완료\""
else
  echo "\$(date '+%F %T') compress failed status=\$?" >> "\$LOG"
  osascript -e 'display alert "newnZip 압축에 실패했습니다."'
  exit 1
fi
EOF
)

EXTRACT_COMMAND=$(cat <<EOF
LOG="/tmp/newnzip-quick-action.log"
echo "\$(date '+%F %T') extract invoked argc=\$#" >> "\$LOG"
if [ "\$#" -eq 0 ]; then
  ITEMS=()
  while IFS= read -r item; do
    [ -n "\$item" ] && ITEMS+=("\$item")
  done
  set -- "\${ITEMS[@]}"
  echo "\$(date '+%F %T') extract stdin argc=\$#" >> "\$LOG"
fi
APP_PATH="$APP_PATH"
ENGINE="\$APP_PATH/Contents/Frameworks/newnzip_engine/newnzip-engine"
SEVENZIP=""
for candidate in /opt/homebrew/bin/7zz /usr/local/bin/7zz /opt/homebrew/bin/7z /usr/local/bin/7z /usr/bin/7zz /usr/bin/7z; do
  if [ -x "\$candidate" ]; then
    SEVENZIP="\$candidate"
    break
  fi
done
if [ "\$#" -lt 1 ]; then
  echo "\$(date '+%F %T') no input archives" >> "\$LOG"
  osascript -e 'display alert "선택된 압축파일을 받지 못했습니다."'
  exit 0
fi
for item in "\$@"; do
  echo "\$(date '+%F %T') archive=\$item" >> "\$LOG"
  DIR=\$(dirname "\$item")
  NAME=\$(basename "\$item")
  LOWER=\$(printf "%s" "\$NAME" | tr '[:upper:]' '[:lower:]')
  BASE="\${NAME%.*}"
  if [[ "\$LOWER" == *.zip.001 || "\$LOWER" == *.7z.001 || "\$LOWER" == *.tar.gz || "\$LOWER" == *.tar.bz2 || "\$LOWER" == *.tar.xz ]]; then
    BASE="\${BASE%.*}"
  fi
  DEST="\$DIR/\$BASE"
  if [ -e "\$DEST" ]; then
    INDEX=2
    while [ -e "\$DIR/\$BASE \$INDEX" ]; do
      INDEX=\$((INDEX + 1))
    done
    DEST="\$DIR/\$BASE \$INDEX"
  fi
  echo "\$(date '+%F %T') destination=\$DEST" >> "\$LOG"
  if [[ "\$LOWER" == *.zip && -x "\$ENGINE" ]]; then
    "\$ENGINE" extract "\$item" "\$DEST" >> "\$LOG" 2>&1 || {
      echo "\$(date '+%F %T') engine extract failed status=\$?" >> "\$LOG"
      osascript -e 'display alert "newnZip 압축 해제에 실패했습니다."'
      exit 1
    }
  elif [ -n "\$SEVENZIP" ]; then
    "\$SEVENZIP" x "\$item" "-o\$DEST" -y >> "\$LOG" 2>&1 || {
      echo "\$(date '+%F %T') 7z extract failed status=\$?" >> "\$LOG"
      osascript -e 'display alert "newnZip 압축 해제에 실패했습니다."'
      exit 1
    }
  else
    echo "\$(date '+%F %T') no extractor for \$item" >> "\$LOG"
    osascript -e 'display alert "이 압축 파일을 풀려면 7zz 또는 7z가 필요합니다."'
    exit 1
  fi
  /usr/bin/open "\$DEST"
done
osascript -e 'display notification "선택한 항목 처리가 끝났습니다." with title "newnZip 압축 풀기 완료"'
EOF
)

write_workflow "$SERVICES_DIR/newnZip으로 압축하기.workflow" "$COMPRESS_COMMAND" "compress"
write_workflow "$SERVICES_DIR/newnZip으로 압축 풀기.workflow" "$EXTRACT_COMMAND" "extract"
echo "Finder Quick Actions installed in $SERVICES_DIR"

if [ -x /System/Library/CoreServices/pbs ]; then
  /System/Library/CoreServices/pbs -flush
  echo "Services cache flushed"
fi

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
] as CFArray

for type in types as! [String] {
    LSSetDefaultRoleHandlerForContentType(type as CFString, .all, identifier as CFString)
}
SWIFT
  echo "Default archive handlers pointed to $APP_PATH"
fi
