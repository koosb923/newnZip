# newnzip-engine

`newnZip`에서 공용으로 사용하는 네이티브 압축 엔진 프로토타입입니다.

현재 범위:

- `list`: ZIP 압축 파일 내부 항목 목록 조회
- `create`: Deflate 압축 방식으로 ZIP 파일 생성
- `extract`: 일반적인 ZIP 파일 해제

이 엔진은 완전한 독립 엔진으로 가기 위한 첫 단계이며, 우선 ZIP부터 자체 처리하는 것을 목표로 두고 있습니다.

## 빌드

```bash
cd native/newnzip_engine
make
```

## 사용 예시

```bash
./bin/newnzip-engine list archive.zip
./bin/newnzip-engine create output.zip /path/to/file /path/to/folder
./bin/newnzip-engine extract archive.zip /destination/folder
```

## 참고

- `zlib`를 사용해 Deflate 압축과 해제를 수행합니다.
- 표준 ZIP 중앙 디렉터리 레코드를 기록합니다.
- 현재 구현은 일반 파일과 폴더를 기준으로 동작합니다.
