# Windows 빌드 최종 검증 체크리스트

이 문서는 실제 Windows 빌드 환경에서 `NewnZipWin`을 마감하기 전에 확인해야 하는 항목을 정리합니다.

## 1. 환경 준비

- Windows 11 또는 지원 대상 Windows 버전
- Visual Studio 2026 또는 그에 준하는 Windows App SDK / WinUI 3 빌드 환경
- .NET 8 SDK 설치 확인
- `dotnet --info` 정상 출력 확인
- `newnzip-engine.exe`와 `newnzip_engine/backends` 포함 빌드 산출 경로 확인

## 2. 빌드 검증

- `dotnet restore .\windows\NewnZipWin\NewnZipWin.csproj`
- `dotnet build .\windows\NewnZipWin\NewnZipWin.csproj`
- Release 빌드 확인
- 배포 폴더에 아래 파일 존재 확인
  - `NewnZipWin.exe`
  - `newnzip-engine.exe` 또는 `newnzip_engine\newnzip-engine.exe`
  - 필요 backend 폴더

## 3. 기본 압축/해제

- 일반 파일 1개 압축
- 폴더 1개 압축
- 여러 파일 동시 압축
- 일반 ZIP 해제
- 분할 ZIP 해제
- 결과 위치와 파일명 충돌 정책 확인

## 4. 드래그 UI

- 메인 드롭 영역 기본 압축
- `암호로 압축` 드롭 영역
- `분할 압축` 드롭 영역
- 여러 압축파일 드롭 시 분기 동작
- 압축파일 + 일반 파일 혼합 드롭 시 전부 압축 처리 확인

## 5. 암호 ZIP 검증

- 암호 ZIP 생성
- 생성된 ZIP을 `newnZip`으로 해제
- 맞는 비밀번호로 정상 해제
- 틀린 비밀번호로 실패
- 틀린 비밀번호일 때 깨진 파일/빈 파일이 남지 않는지 확인
- 가능하면 외부 도구(7-Zip 등)로도 생성물 호환성 확인

## 6. 분할 ZIP 검증

- `몇 MB씩` 분할 생성
- `몇 개로` 분할 생성
- `.001`, `.002` 등 다중 파트 생성 확인
- 분할 ZIP 해제 후 원본 크기/해시 비교
- 암호 + 분할 ZIP 조합 확인

## 7. Explorer 연동

- 파일 우클릭 `newnZip으로 압축하기`
- 압축파일 우클릭 `newnZip으로 압축 풀기`
- 디렉터리 우클릭 동작
- Windows 11 `더 많은 옵션 표시` 아래 노출 여부
- 파일 연결로 ZIP 더블클릭 시 해제 흐름 확인

## 8. 설정 검증

- 기본 분할 용량 저장
- 기본 분할 개수 저장
- 암호 저장 및 다시 열기 반영
- 비밀번호 reveal 버튼 동작
- 오버레이 on/off
- 오버레이 좌/우 도킹

## 9. 안정성/예외

- 취소 버튼 동작
- 긴 경로/한글 파일명
- 읽기 전용 파일
- 큰 파일 압축/해제
- 백엔드 누락 시 오류 문구
- 엔진 실패 시 사용자 메시지

## 10. 마감 전 확인

- 디버그 로그/테스트 코드 제거 여부
- 아이콘/메타데이터 반영
- 서명 또는 배포 방식 확인
- 릴리스 노트에 `ZIP 암호화는 네이티브 엔진 기반` 반영
