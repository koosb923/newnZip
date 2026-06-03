# newnZip

`newnZip`은 macOS와 Windows에서 기본 압축 도구처럼 가볍게 동작하도록 만든 네이티브 압축 프로그램 프로젝트입니다.

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

mac 앱은 `mac/NewnZipMac` 아래에 있으며 SwiftUI와 AppKit 런치 흐름을 함께 사용합니다.

현재 범위:

- 앱을 직접 실행하면 메인 창을 표시
- 압축파일 더블클릭, Finder Quick Action, 앱 아이콘 드롭은 작은 HUD 진행률 창으로 처리
- HUD는 우측 상단에 통일해서 표시하고 완료 후 자동 종료
- 압축/해제 중 취소 지원
- 같은 이름 결과 처리 옵션 지원: 사본으로 추가, 덮어쓰기, 항상 물어보기
- 기본값은 사본으로 추가
- Finder Quick Action 메뉴:
  - `newnZip으로 압축하기`
  - `newnZip으로 압축 풀기`

빌드:

```bash
./scripts/build_macos_app.sh ~/Desktop/newnZip.app ~/Desktop/newnZip.dmg
open ~/Desktop/newnZip.dmg
./scripts/register_macos.sh /Applications/newnZip.app
```

DMG를 열면 `newnZip.app`과 `Applications` 바로가기가 함께 표시됩니다. 앱을 `Applications`로 옮긴 뒤 등록 스크립트를 실행합니다.

## Windows

Windows 앱은 `windows/NewnZipWin` 아래에 있으며 WinUI 3 기반입니다.

현재 범위:

- 일반 실행 시 메인 창 표시
- 우클릭/파일 연결 실행 시 작은 HUD 진행률 창으로 처리
- HUD에서 진행률, 현재 파일명, 취소 버튼 제공
- 압축/해제 결과는 같은 위치에 생성
- 같은 이름 결과 처리 옵션 지원: 사본으로 추가, 덮어쓰기, 항상 물어보기
- Windows 최종 XAML 빌드는 Windows 환경에서 검증 필요

## 네이티브 엔진

공용 압축 엔진은 `native/newnzip_engine` 아래에 있습니다.

빌드:

```bash
cd native/newnzip_engine
make
```
