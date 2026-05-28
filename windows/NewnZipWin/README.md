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
- 내장 `newnzip-engine.exe` 호출
- ZIP 자체 분할/결합 처리

## 빌드

WinUI XAML 컴파일러는 Windows 실행 파일이므로 Windows + Visual Studio 2026 또는 Windows App SDK가 설치된 Windows 환경에서 빌드해야 합니다.

```powershell
dotnet restore .\NewnZipWin.csproj
dotnet build .\NewnZipWin.csproj
```

macOS에서는 NuGet restore까지 가능하지만 `XamlCompiler.exe`를 실행할 수 없어 최종 빌드는 실패합니다.
