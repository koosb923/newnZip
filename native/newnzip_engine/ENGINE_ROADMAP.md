# newnZip Engine Roadmap

This document tracks the long-term compression engine target. The engine should grow as a layered system rather than a single monolithic implementation.

## Architecture

- `native-zip`: built-in ZIP reader/writer for common ZIP workflows, app bundles, symlinks, progress, cancellation-friendly process behavior, and split ZIP volumes.
- `codec-adapter`: adapters for proven external or vendored codec libraries when a format is too large or risky to reimplement directly.
- `archive-router`: chooses the backend from requested format, compression method, encryption, split mode, and available codecs.
- `capability-registry`: reports support for create, extract, list, encryption, split, solid, multithread, and password features.

## Create Targets

| Format | Backend plan | Notes |
| --- | --- | --- |
| ZIP | native first, adapter fallback | Add method selection: Deflate, Deflate64, LZMA, BZip2, PPMd. |
| 7Z | 7zz/7z adapter | Needs solid compression, LZMA/LZMA2, PPMd, split volumes, encryption. |
| TAR.GZ | bsdtar/libarchive adapter | Tar container plus gzip stream. |
| TAR.BZ2 | bsdtar/libarchive adapter | bzip2 codec required. |
| TAR.XZ | bsdtar/libarchive adapter | xz/lzma codec required. |
| TAR.ZSTD | bsdtar/libarchive + zstd adapter | zstd codec required. |
| ZSTD | zstd stream adapter | Single-file zstd stream. |
| WIM | wimlib-imagex adapter | WIM is a filesystem image format, not just compression. |
| SFX(EXE) | adapter/package builder | Requires Windows executable stub plus archive payload. |

## Extract Targets

| Format family | Backend plan | Notes |
| --- | --- | --- |
| ZIP, Deflate64, LZMA, BZip2, PPMd | native + adapter | Native ZIP should handle common ZIP; rare methods can use adapter until implemented. |
| 7Z, RAR, ARJ, LZH, ZPAQ | 7zz/7z adapter | Complex archive formats; backend must be installed or bundled. |
| TAR, GZ, BZ2, XZ, ZSTD, LZ4, LZ5, Brotli, LZMA, Z, CPIO | adapter | TAR.*, GZ, BZ2, XZ, ZSTD, LZ4, Brotli, and CPIO routes are wired; some require bundled codecs. |
| ALZ, EGG | adapter/proprietary module | EGG encryption target: ZipCrypto, AES-128/256, LEA-128/256. |
| CAB, ISO, WIM, UDF, IMG | bsdtar/libarchive adapter | Disk/image/container formats where libarchive supports the container. |
| RPM, DEB, MSI, NSIS, ASAR | adapter | RPM/DEB use libarchive; MSI/NSIS/ASAR still need specific adapters. |
| KZ, Lizard | adapter | Specialized codecs. |

## ZIP Method Targets

- Deflate: currently implemented for create/extract.
- Store: currently implemented for symlinks and stored entries.
- Deflate64: adapter first, native later.
- LZMA: adapter first.
- BZip2: adapter first.
- PPMd: adapter first.

## Split Volume Targets

- ZIP: native split create/join path in app layer; engine-level split commands should be added.
- RAR: adapter only.
- 7Z: adapter only.
- EGG/ALZ: adapter/proprietary module.

## CLI Contract Target

```text
newnzip-engine capabilities
newnzip-engine create --format=zip --method=deflate --split=100m --threads=auto output.zip inputs...
newnzip-engine create --format=7z --solid --password=... output.7z inputs...
newnzip-engine extract --password=... archive destination
newnzip-engine list archive
```

## Phases

1. Capability registry and router skeleton.
2. Expand ZIP create options: method, split, overwrite policy, symlink preservation.
3. Add adapter backend interface and probe available codecs. TAR.*, GZ, BZ2, XZ, ZSTD, LZ4, Brotli, 7Z/RAR family, CAB/ISO/WIM family routes are wired first.
4. Add broad extraction support through adapters.
5. Add create support for TAR.*, 7Z, ZSTD, WIM, SFX.
6. Add EGG/ALZ encryption support module.
7. Move high-value adapter features into native code only when it improves reliability or distribution.
