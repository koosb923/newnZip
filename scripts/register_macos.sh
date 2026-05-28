#!/bin/bash
set -euo pipefail

APP_PATH="${1:-/Applications/newnZip.app}"

if [ ! -d "$APP_PATH" ]; then
  echo "App bundle not found: $APP_PATH"
  exit 1
fi

/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$APP_PATH"
echo "Launch Services refreshed for $APP_PATH"
