# TCP/IP Tester (C++17 / Windows)

별도 프로세스로 실행한 두 GUI에서 TCP 메시지를 양방향으로 주고받고, 실제 바이트와 해석한 값을 비교하는 학습용 프로그램입니다.

## 바로 실행

최신 이미지 지원 실행 파일: **`build/image/Release/TcpIpTester.exe`**
디버깅 솔루션: **`build/image/TcpIpTester.sln`**, Debug / x64 / 시작 프로젝트 `TcpIpTester`.
이전 `build/Release`, `build/final` 실행 파일은 이미지 기능이 없는 예전 버전일 수 있습니다.

### C++ ↔ Python 이미지 전송

1. 위 C++ EXE를 실행하고 `127.0.0.1:9001`에서 **서버 대기**를 누릅니다.
2. 프로젝트 폴더의 **`launch_python.cmd`**를 더블클릭합니다. 현재 PC의 준비된 Python을 우선 사용하며, 없으면 `py -3`를 사용합니다.
3. Python 창에서 같은 IP·포트로 **클라이언트 연결**을 누릅니다. 서버 대기는 한쪽에서만 누릅니다. 기존 프로그램이 포트를 쓰면 양쪽 포트를 9002 등으로 변경하세요.
4. 두 창 모두 기본 사진은 프로젝트의 `000028.JPG`입니다. 어느 쪽에서든 **사진 전송**을 누르면 반대편에 미리보기와 너비·높이·채널·stride·바이트 수가 표시됩니다.
5. **사진 선택**으로 다른 JPG/PNG/BMP 등의 파일을 고른 뒤 전송하세요. 파일을 다시 읽고 메타데이터를 구성하므로 크기와 채널이 바뀌어도 동작합니다.
6. **수신 PNG 저장**으로 마지막 수신 이미지를 저장할 수 있습니다. 미리보기만 축소하며 전송 및 저장 데이터는 전체 해상도입니다.

Python을 직접 설치한 다른 PC에서는 다음과 같이 실행합니다. Tkinter가 포함된 Python 3.10 이상을 사용하세요.

```powershell
py -3 -m pip install -r python/requirements.txt
py -3 python/image_peer.py
```

C++ 서버/Python 클라이언트뿐 아니라 Python 서버/C++ 클라이언트도 가능합니다. C++↔C++ 및 Python↔Python도 같은 프로토콜을 사용합니다. Python GUI는 이미지 송신을 제공하며 C++에서 보내는 기존 문자열/숫자도 수신·ACK합니다. Python 서버는 연결 종료 후 다시 **서버 대기**를 누릅니다.

### 기존 문자열/숫자 시험

1. 프로그램을 실행하고 **서버 대기**를 누릅니다. 기본값은 `127.0.0.1:9001`입니다.
2. **상대 창 열기**를 누르면 같은 EXE가 **독립 프로세스**로 실행됩니다.
3. 새 창에서 **클라이언트 연결**을 누릅니다.
4. 어느 창에서든 값을 입력하고 **전송**을 누릅니다. 반대편 `RX FRAME`의 `value`와 송신 측 `TX QUEUED`의 `value`를 비교하세요.
5. 송신 측의 `ACK 확인 seq=...`가 나오면 상대 프로그램이 같은 순번의 프레임을 정상 해석했습니다.

문자열 기본값 그대로 시험한 다음, 종류를 Int32로 바꿔 `-42`, Float64로 바꿔 `3.141592653589793`을 보내보세요. 타입을 바꿀 때 입력값도 해당 타입에 맞게 바꿉니다.

- **분할 전송**: 100ms마다 최대 3바이트씩 `send`합니다. 헤더와 본문이 나뉘어 도착해도 전체 프레임을 모아 한 번만 해석합니다.
- **묶음 전송**: 서로 다른 순번의 같은 값 3개를 하나의 송신 큐 항목으로 보냅니다. 한 번의 `recv`로 여러 프레임이 들어와도 각각 해석합니다.
- **로그 저장**: 현재 화면의 로그를 UTF-8 파일로 저장합니다. 로그는 약 40만 글자 이후 자동 정리됩니다.
- **중지**: 연결과 서버 대기를 종료합니다. 서버는 상대가 끊으면 다시 접속을 받을 수 있습니다.

다른 PC로 시험할 때는 서버의 IPv4를 해당 PC의 LAN 주소 또는 `0.0.0.0`으로 설정하고, 클라이언트에는 서버 PC의 실제 LAN 주소를 입력합니다. 기본 `127.0.0.1`은 같은 PC 전용입니다. 포트가 이미 사용 중이면 양쪽에서 다른 포트로 바꾸세요. 외부 PC 접속에는 해당 포트를 허용하는 방화벽 설정이 필요할 수 있습니다.

## 선택한 기술

- C++17 + CMake + Visual Studio 2022 MSVC
- Win32 네이티브 GUI + Winsock2 비동기 TCP
- 별도 다운로드 라이브러리 없음. Windows 전용입니다.
- C++ 이미지 입출력: Windows WIC. Python GUI: Tkinter, 네트워크: 표준 socket, 이미지: Pillow.

모든 회사에서 가장 많이 쓰는 단일 라이브러리는 없습니다. Windows 기본 API를 직접 익히고 즉시 실행할 수 있도록 이 구성을 선택했습니다. 크로스 플랫폼 데스크톱에서는 Qt Widgets/Qt Network, 대규모 네트워크 엔진에서는 Boost.Asio/IOCP 같은 구성을 고려할 수 있습니다. 이 작은 GUI는 윈도우 메시지와 통합되는 `WSAAsyncSelect`를 사용합니다. 최신 Windows SDK는 이 오래된 API 대신 `WSAEventSelect`를 권장하는 경고를 내므로, 사용 목적을 명시하고 해당 경고만 비활성화했습니다. 대량 접속용 서버 설계는 아닙니다.

## 빌드 및 검증

Visual Studio 2022의 **C++를 사용한 데스크톱 개발**, Windows SDK, CMake가 필요합니다.

```powershell
cmake -S . -B build/image -G "Visual Studio 17 2022" -A x64
cmake --build build/image --config Debug
cmake --build build/image --config Release
ctest --test-dir build/image -C Release --output-on-failure
.\build\image\Release\TcpIpTester.exe
```

프로토콜 단위 테스트는 Windows 외 환경에서도 CMake로 빌드할 수 있습니다. GUI만 Windows 전용입니다. 다른 PC에서 MSVC 런타임 오류가 나오면 Visual C++ 2015–2022 x64 재배포 패키지가 필요합니다.

## 프로토콜 v1

전체 명세와 HEX 예시는 [PROTOCOL.md](PROTOCOL.md)를 참고하세요.

| 오프셋 | 바이트 수 | 의미 |
|---|---:|---|
| 0 | 4 | Magic: ASCII `TCPT` |
| 4 | 2 | 버전: 1 |
| 6 | 2 | 종류: 문자열=1, Int32=2, Float64=3, ACK=4, Image=5 |
| 8 | 4 | 순번: uint32 |
| 12 | 4 | 본문 바이트 길이: uint32 |
| 16 | 가변 | 본문 |

## 코드 읽는 순서

1. `src/protocol.h`: `put/get` 바이트 순서 → `encode` 프레임 생성 → `Decoder::feed` 수신 누적 및 프레임 분리.
2. `src/main.cpp`: `sendValue` 입력 직렬화 → `enqueue/flush` 비동기 송신 → `readPeer` 수신 및 ACK → `describe` 화면 표시.
3. `tests/protocol_tests.cpp`: 고정 HEX 정답, 모든 분할 위치, 1바이트씩 수신, 100개 합쳐진 프레임, 잘못된 헤더, UTF-8, 최대 길이 검증.
4. `tests/tcp_integration_tests.cpp`: 별도 클라이언트 프로세스를 실행해 실제 loopback TCP로 4가지 본문을 분할/묶음 전송하고 echo와 ACK를 검증합니다. GUI 이벤트 처리를 직접 검증하는 테스트는 아닙니다.

5. `src/image_io.h`: `load` 파일 디코딩 → `save` PNG 저장 → `thumbnail` 미리보기.
6. `python/protocol.py`: 동일한 바이트 규약의 Python 구현. `python/image_peer.py`: GUI와 소켓 작업 스레드.

이미지 디버깅 중단점: C++ `sendImage` → `imaging::load` → `wire::imagePayload` → `wire::encode` → `flush`. 수신은 `readPeer` → `Decoder::feed` → `imageInfo` → `showImage`. Python은 `load_image`, `Decoder.feed`, `image_info`, `to_image`를 확인하세요.

자동 검증: `tests/test_image_interop.py`가 실제 C++ 프로세스와 Python 소켓을 연결하여 원본 JPG, 크기가 다른 L/RGB/RGBA PNG, 1×1, 이진 흑백 PNG, 흑백 JPEG를 검사합니다. 송수신 메타데이터, 정확한 왕복 픽셀 바이트, C++ PNG 저장 후 재로드, 분할/묶음 수신을 확인합니다. JPEG는 서로 다른 디코더의 미세한 픽셀 차이가 가능하므로 각각 디코딩한 결과를 기준으로 전송 정확성을 검사합니다. GUI 자동 테스트를 대신하는 것은 아닙니다.

CMake가 Pillow 포함 Python을 찾으면 `image_python_cpp` 테스트를 등록합니다. 필요하면 구성 명령에 `-DPython3_EXECUTABLE="Python 실행 파일 절대 경로"`를 추가하세요. 직접 실행: `py -3 tests/test_image_interop.py build/image/Release/image_bridge.exe`.

## 동작 범위

1:1 시험용이며 IPv4 주소를 직접 입력합니다. 인증·암호화·자동 재접속·메시지 재전송은 구현하지 않았습니다. `send` 성공은 상대 해석 완료를 뜻하지 않으므로 ACK를 따로 표시합니다. ACK 대기 120초는 송신 준비/큐 대기를 포함합니다. ACK 시간은 정확한 네트워크 RTT가 아닙니다. Python 소켓 쓰기는 15초 제한이 있으며 실패 시 불완전 프레임 재사용을 막기 위해 연결을 종료합니다.

일반 수신 본문은 최대 64 KiB, 이미지 픽셀은 최대 64 MiB, 가로·세로는 각 16384 이하입니다. C++ 송신 큐는 이미지 1개 최대 크기 + 약 1 MiB로 제한합니다. Python은 한 번에 이미지 1개를 송신합니다. 잘못된 크기/채널/stride/길이는 거부합니다. 기존 분할/묶음 버튼은 문자열·숫자 전용입니다.

전송 채널은 디코딩 후의 **GRAY8 / RGB8 / RGBA8** 기준입니다. 1비트 흑백은 GRAY8로, 팔레트/CMYK는 RGB 또는 RGBA로 변환합니다. 고비트 깊이(16비트·float 등) 원본의 정밀도 보존은 지원하지 않으므로 8비트로 명시적으로 변환한 입력을 사용하세요. 멀티프레임 파일은 첫 프레임만 처리합니다. EXIF 자동 회전/ICC 색상 관리/원본 압축 파일 및 메타데이터 보존은 하지 않습니다. RGBA의 알파 바이트는 전송·PNG 저장에 보존되며 C++ 미리보기는 RGB만 표시합니다. C++의 파일 디코딩/PNG 저장은 UI 스레드에서 하므로 큰 파일에서는 잠깐 멈출 수 있습니다.

## 참고 문서

- [Microsoft WSAAsyncSelect](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsaasyncselect)
- [Qt TCP 소켓: stream-oriented 특성](https://doc.qt.io/qt-6.8/qtcpsocket.html)
