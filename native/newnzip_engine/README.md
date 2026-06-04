# newnzip-engine

`newnZip`에서 공용으로 사용하는 네이티브 압축 엔진 프로토타입입니다.

현재 범위:

- `list`: ZIP 압축 파일 내부 항목 목록 조회
- `create`: ZIP, 암호 ZIP(ZipCrypto), 분할 ZIP, TAR, TAR.GZ, TAR.BZ2, TAR.XZ 생성
- `extract`: ZIP, 암호 ZIP(ZipCrypto), ZIP 분할 볼륨, TAR 계열, GZ/BZ2 stream, libarchive가 읽을 수 있는 일부 컨테이너 해제
- `capabilities`: 현재 엔진/어댑터 지원 목록을 JSON으로 출력

이 엔진은 네이티브 ZIP 구현과 외부/번들 backend adapter를 함께 쓰는 구조입니다.

## 빌드

```bash
cd native/newnzip_engine
make
```

## 사용 예시

```bash
./bin/newnzip-engine list archive.zip
./bin/newnzip-engine create output.zip /path/to/file /path/to/folder
./bin/newnzip-engine create --method=auto output.zip /path/to/folder
./bin/newnzip-engine create --method=store output.zip /path/to/folder
./bin/newnzip-engine create --password=0923 output.zip /path/to/folder
./bin/newnzip-engine create --password=0923 --zip-encryption=zipcrypto output.zip /path/to/folder
./bin/newnzip-engine create --format=tar.gz output.tar.gz /path/to/folder
./bin/newnzip-engine create --format=zip --password=0923 --split=100m output.zip /path/to/folder
./bin/newnzip-engine extract archive.zip /destination/folder
./bin/newnzip-engine extract --password=0923 secure.zip /destination/folder
./bin/newnzip-engine extract archive.zip.001 /destination/folder
./bin/newnzip-engine capabilities
```

## Adapter backends

기본 macOS 환경에서는 `bsdtar`, `gzip`, `bzip2`를 사용합니다. ZIP/암호 ZIP/분할 ZIP은 네이티브 경로로 처리합니다. 다음 형식은 실행파일이 설치되어 있거나 앱에 번들되어 있어야 동작합니다.

- `7z`, `rar`, `arj`, `lzh`, `zpaq`: `7zz` 또는 `7z`
- `zstd`, `tar.zstd`: `zstd`
- `lz4`: `lz4`
- `brotli`: `brotli`
- `wim` 생성: `wimlib-imagex`

앱에 backend를 번들할 때는 실행파일을 `Contents/Frameworks/newnzip_engine/backends` 또는 Windows의 `newnzip_engine/backends`에 넣습니다. CLI에서는 `NEWNZIP_BACKEND_DIR=/path/to/backends`로 같은 경로를 지정할 수 있습니다.

## 참고

- `zlib`를 사용해 Deflate 압축과 해제를 수행합니다.
- `ZipCrypto` 기반 ZIP 암호화/복호화를 네이티브 경로로 처리합니다.
- `AES ZIP`은 다음 단계 구현 대상으로 옵션 골격이 준비되어 있습니다.
- 표준 ZIP 중앙 디렉터리 레코드를 기록합니다.
- ZIP은 일반 파일, 폴더, symlink metadata, 분할 볼륨을 처리합니다.
- ZIP64를 사용해 4GB 초과 크기/오프셋, 65535개 초과 항목, 큰 중앙 디렉터리를 처리합니다.
- ZIP 파일명은 UTF-8 플래그와 NFC 정규화를 사용해 macOS/Windows 왕복 시 한글 자모 분리를 줄입니다.
- `.DS_Store`, `._*`, `__MACOSX`, `.Spotlight-V100`, `.Trashes`, `.fseventsd` 같은 macOS 메타 파일은 압축 대상에서 제외합니다.
