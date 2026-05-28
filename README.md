# newnZip

`newnZip`은 네이티브 앱 중심 구조로 정리된 압축 프로그램 프로젝트입니다.

- `shared/`
  macOS와 Windows가 함께 사용하는 다국어 리소스와 설정 스키마
- `native/newnzip_engine/`
  공용 네이티브 압축 엔진
- `mac/NewnZipMac/`
  macOS용 Swift 앱
- `windows/NewnZipWin/`
  Windows용 네이티브 앱 스캐폴드

## 구조

```text
newnZip/
  shared/
    locales/
    config/
  native/
    newnzip_engine/
  mac/
    NewnZipMac/
  windows/
    NewnZipWin/
  scripts/
```

## 공용 자원

- `shared/locales/*.json`
  한국어, 영어, 일본어 다국어 원본 파일
- `shared/config/settings.schema.json`
  공용 설정 계약 파일

## macOS

mac 앱은 `mac/NewnZipMac` 아래에 있으며 SwiftUI를 사용합니다.

빌드:

```bash
cd mac/NewnZipMac
swift build
```

## Windows

Windows 폴더에는 네이티브 앱 스캐폴드가 있으며, `shared`의 다국어 및 설정 구조를 함께 사용합니다.

## 네이티브 엔진

공용 압축 엔진은 `native/newnzip_engine` 아래에 있습니다.

빌드:

```bash
cd native/newnzip_engine
make
```
