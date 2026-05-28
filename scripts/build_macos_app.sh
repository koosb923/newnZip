#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_PATH="${1:-$HOME/Desktop/newnZip.app}"
TMP_DIR="${TMPDIR:-/tmp}/newnzip-macos-build"
SDK_PATH="${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk}"

rm -rf "$TMP_DIR"
mkdir -p "$TMP_DIR"

clang -std=c11 -O2 -Wall -Wextra -pedantic \
  -arch arm64 -arch x86_64 -mmacosx-version-min=10.13 \
  -o "$TMP_DIR/newnzip-engine" \
  "$ROOT_DIR/native/newnzip_engine/src/main.c" \
  "$ROOT_DIR/native/newnzip_engine/src/common.c" \
  "$ROOT_DIR/native/newnzip_engine/src/runtime.c" \
  "$ROOT_DIR/native/newnzip_engine/src/progress.c" \
  "$ROOT_DIR/native/newnzip_engine/src/zip_writer.c" \
  "$ROOT_DIR/native/newnzip_engine/src/zip_reader.c" \
  "$ROOT_DIR/native/newnzip_engine/src/benchmark.c" \
  "$ROOT_DIR/native/newnzip_engine/src/capabilities.c" \
  "$ROOT_DIR/native/newnzip_engine/src/archive_adapter.c" \
  -lz -lpthread -framework CoreFoundation

env SDKROOT="$SDK_PATH" CLANG_MODULE_CACHE_PATH="$TMP_DIR/clang-arm64" \
  swift build -c release --triple arm64-apple-macosx14.0 \
  --package-path "$ROOT_DIR/mac/NewnZipMac" \
  --scratch-path "$TMP_DIR/swift-arm64"

env SDKROOT="$SDK_PATH" CLANG_MODULE_CACHE_PATH="$TMP_DIR/clang-x86_64" \
  swift build -c release --triple x86_64-apple-macosx14.0 \
  --package-path "$ROOT_DIR/mac/NewnZipMac" \
  --scratch-path "$TMP_DIR/swift-x86_64"

lipo -create \
  "$TMP_DIR/swift-arm64/arm64-apple-macosx/release/NewnZipMac" \
  "$TMP_DIR/swift-x86_64/x86_64-apple-macosx/release/NewnZipMac" \
  -output "$TMP_DIR/NewnZipMac"

rm -rf "$APP_PATH"
mkdir -p \
  "$APP_PATH/Contents/MacOS" \
  "$APP_PATH/Contents/Frameworks/newnzip_engine" \
  "$APP_PATH/Contents/Frameworks/newnzip_engine/backends" \
  "$APP_PATH/Contents/Resources/locales"

cp "$TMP_DIR/NewnZipMac" "$APP_PATH/Contents/MacOS/NewnZipMac"
cp "$TMP_DIR/newnzip-engine" "$APP_PATH/Contents/Frameworks/newnzip_engine/newnzip-engine"
cp "$ROOT_DIR/mac/NewnZipMac/Sources/NewnZipMacApp/Resources/locales/"*.json "$APP_PATH/Contents/Resources/locales/"

cat > "$APP_PATH/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>
  <string>newnZip</string>
  <key>CFBundleDisplayName</key>
  <string>newnZip</string>
  <key>CFBundleIdentifier</key>
  <string>com.newntool.newnzip</string>
  <key>CFBundleVersion</key>
  <string>1</string>
  <key>CFBundleShortVersionString</key>
  <string>0.1</string>
  <key>CFBundleExecutable</key>
  <string>NewnZipMac</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>LSMinimumSystemVersion</key>
  <string>14.0</string>
  <key>CFBundleDocumentTypes</key>
  <array>
    <dict>
      <key>CFBundleTypeName</key>
      <string>Archive</string>
      <key>CFBundleTypeRole</key>
      <string>Viewer</string>
      <key>LSHandlerRank</key>
      <string>Owner</string>
      <key>LSItemContentTypes</key>
      <array>
        <string>public.zip-archive</string>
        <string>org.gnu.gnu-zip-archive</string>
        <string>public.tar-archive</string>
        <string>org.7-zip.7-zip-archive</string>
        <string>com.rarlab.rar-archive</string>
        <string>com.newntool.split-archive-volume</string>
      </array>
    </dict>
  </array>
  <key>UTImportedTypeDeclarations</key>
  <array>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.newntool.split-archive-volume</string>
      <key>UTTypeDescription</key>
      <string>Split Archive Volume</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>001</string>
          <string>z01</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>org.7-zip.7-zip-archive</string>
      <key>UTTypeDescription</key>
      <string>7-Zip Archive</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>7z</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.rarlab.rar-archive</string>
      <key>UTTypeDescription</key>
      <string>RAR Archive</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>rar</string>
        </array>
      </dict>
    </dict>
  </array>
</dict>
</plist>
PLIST

codesign --force --deep --sign - "$APP_PATH"

echo "Built $APP_PATH"
file "$APP_PATH/Contents/MacOS/NewnZipMac"
file "$APP_PATH/Contents/Frameworks/newnzip_engine/newnzip-engine"
