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
cp "$ROOT_DIR/assets/newnzip-app-icon.icns" "$APP_PATH/Contents/Resources/newnzip-app-icon.icns"

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
  <key>CFBundleIconFile</key>
  <string>newnzip-app-icon</string>
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
        <string>com.newntool.archive.tgz</string>
        <string>com.newntool.archive.tbz2</string>
        <string>com.newntool.archive.txz</string>
        <string>com.newntool.archive.zstd</string>
        <string>com.newntool.archive.lz4</string>
        <string>com.newntool.archive.brotli</string>
        <string>com.newntool.archive.cab</string>
        <string>com.newntool.archive.iso</string>
        <string>com.newntool.archive.wim</string>
        <string>com.newntool.archive.arj</string>
        <string>com.newntool.archive.lzh</string>
        <string>com.newntool.archive.cpio</string>
        <string>com.newntool.archive.rpm</string>
        <string>com.newntool.archive.deb</string>
        <string>com.newntool.archive.img</string>
      </array>
      <key>CFBundleTypeExtensions</key>
      <array>
        <string>zip</string>
        <string>7z</string>
        <string>rar</string>
        <string>tar</string>
        <string>tgz</string>
        <string>gz</string>
        <string>bz2</string>
        <string>xz</string>
        <string>zst</string>
        <string>zstd</string>
        <string>lz4</string>
        <string>br</string>
        <string>brotli</string>
        <string>cab</string>
        <string>iso</string>
        <string>wim</string>
        <string>arj</string>
        <string>lzh</string>
        <string>lha</string>
        <string>cpio</string>
        <string>rpm</string>
        <string>deb</string>
        <string>img</string>
        <string>001</string>
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
      <string>com.newntool.archive.tgz</string>
      <key>UTTypeDescription</key>
      <string>Compressed Tar Archive</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>tgz</string>
          <string>tar.gz</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.newntool.archive.tbz2</string>
      <key>UTTypeDescription</key>
      <string>BZip2 Tar Archive</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>tbz2</string>
          <string>tar.bz2</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.newntool.archive.txz</string>
      <key>UTTypeDescription</key>
      <string>XZ Tar Archive</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>txz</string>
          <string>tar.xz</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.newntool.archive.zstd</string>
      <key>UTTypeDescription</key>
      <string>Zstandard Archive</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>zst</string>
          <string>zstd</string>
          <string>tar.zst</string>
          <string>tar.zstd</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.newntool.archive.lz4</string>
      <key>UTTypeDescription</key>
      <string>LZ4 Archive</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>lz4</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.newntool.archive.brotli</string>
      <key>UTTypeDescription</key>
      <string>Brotli Archive</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>br</string>
          <string>brotli</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.newntool.archive.cab</string>
      <key>UTTypeDescription</key>
      <string>Cabinet Archive</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>cab</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.newntool.archive.iso</string>
      <key>UTTypeDescription</key>
      <string>ISO Disk Image</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>iso</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.newntool.archive.wim</string>
      <key>UTTypeDescription</key>
      <string>Windows Imaging Archive</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>wim</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.newntool.archive.arj</string>
      <key>UTTypeDescription</key>
      <string>ARJ Archive</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>arj</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.newntool.archive.lzh</string>
      <key>UTTypeDescription</key>
      <string>LZH Archive</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>lzh</string>
          <string>lha</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.newntool.archive.cpio</string>
      <key>UTTypeDescription</key>
      <string>CPIO Archive</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>cpio</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.newntool.archive.rpm</string>
      <key>UTTypeDescription</key>
      <string>RPM Package</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>rpm</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.newntool.archive.deb</string>
      <key>UTTypeDescription</key>
      <string>Debian Package</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>deb</string>
        </array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.newntool.archive.img</string>
      <key>UTTypeDescription</key>
      <string>Disk Image</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>public.data</string>
        <string>public.archive</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>img</string>
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
