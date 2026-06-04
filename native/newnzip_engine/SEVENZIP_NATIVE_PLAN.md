# 7Z Native Plan

This document tracks the staged move from the current `7zz/7z` adapter path to a native 7z engine inside `newnzip-engine`.

## Current State

- `7z` create: adapter via bundled or installed `7zz/7z`
- `7z` extract: adapter via bundled or installed `7zz/7z`
- `rar` extract: adapter via bundled or installed `7zz/7z`
- `rar` create: intentionally out of scope

## Scope

### In Scope

- native `7z` list/create/extract
- header parsing
- `LZMA` / `LZMA2` codec path
- AES-256 7z password support
- multivolume awareness if format value is high enough

### Out of Scope

- native `rar` create
- proprietary `rar` compression implementation

## Phase 1: Container Reader

- parse 7z signature header
- validate start header CRC
- parse next header streams
- enumerate folders, coders, packed streams, and file items
- expose `7z list` first before extract

## Phase 2: Extract Path

- implement packed stream reader
- implement folder pipeline execution
- add `LZMA` decode
- add `LZMA2` decode
- materialize files, directories, and timestamps
- keep `rar` extraction on adapter path

## Phase 3: Create Path

- implement file item table writer
- implement packed streams for single-folder create
- add solid/non-solid decisions
- add password + header encryption

## RAR Direction

- keep `rar` on extract-only path
- continue using `7zz/7z` adapter for reliability
- improve product-level messaging around:
  - backend missing
  - wrong password
  - unsupported rar variant

## Practical Order

1. native `7z list`
2. native `7z` extract without password
3. native `7z` extract with password
4. native `7z` create without password
5. native `7z` create with password
6. multivolume and advanced solid tuning
