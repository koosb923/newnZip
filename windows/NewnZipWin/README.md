# NewnZipWin

`newnZip`의 Windows 네이티브 WinUI 3 앱입니다.

## 스택

- UI: WinUI 3 + Windows App SDK
- 런타임: .NET 8 Windows Desktop
- 공용 자원: `../../shared/locales`, `../../shared/config`
- 엔진 브리지: mac 앱과 같은 CLI 계약으로 공용 네이티브 압축 엔진 호출

## 현재 범위

- 일반 실행 시 WinUI 3 메인 창 표시
- 우클릭/파일 연결 실행 인자 처리
  - `--compress <파일-또는-폴더>...`
  - `--extract <압축파일>...`
  - `--split-compress <MB> <파일-또는-폴더>...`
- 우클릭/파일 연결 명령은 창을 띄우지 않고 백그라운드로 실행
- 압축 완료 시 Explorer에서 생성된 압축파일 선택
- 압축 해제 완료 시 Explorer에서 해제 폴더 열기
- 내장 `newnzip-engine.exe` 호출
- ZIP 자체 분할/결합 처리

## 빌드

WinUI XAML 컴파일러는 Windows 실행 파일이므로 Windows + Visual Studio 2026 또는 Windows App SDK가 설치된 Windows 환경에서 빌드해야 합니다.

```powershell
dotnet restore .\NewnZipWin.csproj
dotnet build .\NewnZipWin.csproj
```

macOS에서는 NuGet restore까지 가능하지만 `XamlCompiler.exe`를 실행할 수 없어 최종 빌드는 실패합니다.

## Windows 우클릭 메뉴 등록

빌드한 `NewnZipWin.exe` 경로를 넣어 현재 사용자 레지스트리에 등록합니다. 관리자 권한은 보통 필요 없습니다.

```powershell
pwsh -ExecutionPolicy Bypass -File ..\..\scripts\register_windows.ps1 -ExePath "C:\Path\To\NewnZipWin.exe" -SetDefault
```

기본 우클릭 메뉴는 두 개만 등록됩니다.

- `newnZip으로 압축하기`
- `newnZip으로 압축 풀기`

분할 압축 메뉴까지 별도로 보이게 하려면 명시적으로 켭니다.

```powershell
pwsh -ExecutionPolicy Bypass -File ..\..\scripts\register_windows.ps1 -ExePath "C:\Path\To\NewnZipWin.exe" -SetDefault -IncludeSplitMenu
```

Windows 11의 새 우클릭 메뉴에서는 레거시 레지스트리 메뉴가 `더 많은 옵션 표시` 아래에 표시될 수 있습니다. 새 우클릭 최상단 메뉴에 바로 붙이려면 이후 MSIX/sparse package 기반 Explorer command extension 작업이 필요합니다.
