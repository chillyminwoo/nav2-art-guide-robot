# Art Museum Guide Robot System

QR 티켓 인증 · 자율주행 · 작품 인식 · 제스처 제어 통합 박물관 안내 로봇 플랫폼

- **수행 기간**: 2026.04.17 ~ 2026.04.27
- **기술 스택**: TurtleBot3 / ROS2 Nav2 / Qt6 / MobileNetV2 / MediaPipe / RealSense D435 / TCP Socket

---

## 시스템 구성

```
[입구 RPi (QR Gate)]         [서버 PC]                [TurtleBot RPi]
 웹캠 + 서보모터               Python 멀티스레드          nav_client.cpp (ROS2)
 gate_client ──9995──────►  server.py  ◄──9996──────  artwork_recognition
                             │       │                 gesture.py
                             │       └──9993◄──────── gesture.py
                             │
                    9994────►│◄───9997
                             │────9998►
                             │◄───9999─────────────── artwork_recognition
                             │
                       [UI RPi]
                        Qt6 터치스크린 UI
```

### 하드웨어 구성

| 기기 | 역할 |
|------|------|
| 서버 PC | 중앙 허브, MobileNetV2 작품 인식, SQLite 티켓 검증 |
| 입구 RPi | QR 스캔, 서보모터 게이트 제어 |
| TurtleBot RPi | Nav2 자율주행, RealSense D435 카메라, MediaPipe 제스처 인식 |
| UI RPi | 800×480 Qt6 터치스크린, TTS 재생 |

### 전체 동작 흐름

1. 관리자가 `issue_ticket.py`로 UUID QR 티켓 발급
2. 관람객이 입구 QR 리더기에 티켓 제시 → SQLite 검증 → 서보 게이트 열림 + UI 전환
3. 터치스크린에서 목적지 선택 → Nav2 자율주행 이동
4. 전시관 도착 시 MobileNetV2로 작품 인식 → UI 표시 + TTS 안내
5. 손 펼치기로 로봇 호출 / 주먹으로 긴급 정지

---

## 디렉토리 구조

```
museum-robot/
├── server/                             # 중앙 허브 서버 (PC)
│   ├── server.py                       # TCP 8포트 허브 + MobileNetV2 작품 인식
│   ├── database.py                     # SQLite 티켓 DB
│   └── issue_ticket.py                 # QR 티켓 발급 스크립트
│
├── gate/                               # QR 입장 게이트 (입구 RPi)
│   ├── CMakeLists.txt
│   ├── qr_main.cpp                     # 메인 루프 (QR 스캔 → 서버 전송 → 서보 제어)
│   ├── qr_scanner.cpp/h                # OpenCV QRCodeDetector, V4L2
│   └── servo_controller.cpp/h         # lgpio GPIO18, MG996R 서보
│
└── robot/
    ├── turtlebot_rpi/                  # TurtleBot 연결 RPi
    │   ├── artwork_recognition/
    │   │   ├── CMakeLists.txt
    │   │   └── main.cpp                # 10f마다 서버 전송 / 3f마다 gesture.py 전송
    │   └── nav_client/
    │       ├── CMakeLists.txt
    │       ├── package.xml
    │       ├── nav_client.cpp          # Nav2 액션 클라이언트, TF 좌표 변환
    │       └── gesture.py             # MediaPipe 손 제스처 → CALL/ESTOP 전송
    │
    └── ui_rpi/                         # 터치스크린 전용 RPi
        └── ui/
            ├── CMakeLists.txt
            ├── main.cpp
            ├── mainwindow.cpp/h
            └── pages/
                ├── qrpage             # QR 인증 결과 수신 (9994), TTS 상태 관리
                ├── standbypage        # 터치 감지
                ├── destinationpage    # 목적지 5개 버튼 그리드
                ├── navigatingpage     # 이동 중 프로그레스 바 + ARRIVED 수신
                ├── artworkpage        # 작품 정보 표시 + gTTS/mpg123
                └── navigationmanager  # 목적지 전송 / ARRIVED 수신 (9997)
```

---

## 주요 기능

### QR 티켓 인증
OpenCV `QRCodeDetector` + V4L2로 QR 스캔, UUID 티켓 ID를 서버로 전송해 SQLite DB에서 일회용 검증. 인증 결과를 입구 서보모터와 UI RPi에 동시 중계. 한 번 사용된 티켓은 재사용 불가.

### 자율주행 목적지 이동
Qt UI에서 전시관 A/B/C, 화장실, 출입구 선택 → `nav_client.cpp`가 하드코딩된 좌표 맵에서 x/y/quaternion 조회 후 `NavigateToPose` Action Goal 발행. 도착 시 `ARRIVED:목적지명`을 서버로 역전송.

### 미술작품 인식
MobileNetV2 마지막 분류기를 `Identity()`로 교체해 1280차원 특징 벡터 추출. 서버 시작 시 작품 3점(진주 귀걸이 소녀 / 그랑드 자트 / 세잔 정물화) 벡터를 사전 계산. 수신 프레임과 코사인 유사도 비교(임계값 0.58) 후 `deque(maxlen=5)` 최빈값으로 안정화. **이동 중에는 `recognition_active=False`** — 도착 후에만 활성화.

### 손 제스처 제어
`main.cpp`가 로컬 소켓(9990)으로 JPEG + uint16 깊이 raw를 `gesture.py`에 전달. MediaPipe Hands로 21개 랜드마크 추출 후 손가락 펼침 수로 분류. 6프레임 중 60% 이상 동일 제스처 + 1.5초 쿨다운 후 명령 확정.
- **주먹** → `ESTOP` → `cancelGoal()`
- **손 펼치기** → landmark 9 깊이로 3D 좌표 계산 → `CALL:x,z` → TF 변환 후 Nav2 Goal

### 카메라 1대 이중 활용
카메라 1대로 작품 인식(서버 9999)과 제스처 인식(로컬 9990)을 포트 분리로 충돌 없이 운용. 이동 중에는 제스처만, 도착 후에는 작품 인식만 활성화.

---

## 통신 프로토콜

모든 메시지: `[4바이트 빅엔디언 uint32 길이] + [UTF-8 페이로드]`

| 포트 | 방향 | 내용 |
|------|------|------|
| 9990 | artwork_recognition → gesture.py | JPEG + uint16 깊이 raw (로컬) |
| 9993 | gesture.py → server | `ESTOP` / `CALL:x,z` |
| 9994 | server → Qt UI | `AUTH_OK` / `AUTH_FAIL` |
| 9995 | gate_client → server | UUID 티켓 ID |
| 9996 | server ↔ nav_client | 목적지명 / `ARRIVED:목적지명` / `ESTOP` / `CALL:x,z` |
| 9997 | Qt UI → server | 목적지명 / `ESTOP` |
| 9998 | server → Qt UI | `작품명\|작가\|연도\|장르\|기법\|소장처\|설명` |
| 9999 | artwork_recognition → server | JPEG + float 깊이값 |

---

## 트러블슈팅

| 문제 | 원인 | 해결 |
|------|------|------|
| ROS2 ↔ TCP 통신 오버헤드 | ROS2 토픽으로 서버-로봇 직접 통신 시도 | 1:1 통신은 TCP, ROS2는 Nav2 Action 전용으로 역할 분리 |
| 목적지 도착 정확도 | Nav2 기본 허용 반경이 넓어 작품 정면 정지 불가 | 허용 반경 조정 + 좌표 반복 튜닝 |
| 이동 중 작품 오인식 | 이동 중에도 카메라가 작품 인식해 잘못된 TTS 출력 | `recognition_active` 플래그로 도착 후에만 활성화 |

---

## 빌드 및 실행

### 의존성

```bash
# 서버 PC
pip install torch torchvision opencv-python pillow qrcode

# TurtleBot RPi
pip install mediapipe opencv-python

# 입구 RPi
sudo apt install liblgpio-dev
```

### 빌드

```bash
# gate (입구 RPi)
cd gate && mkdir build && cd build && cmake .. && make

# artwork_recognition (TurtleBot RPi)
cd robot/turtlebot_rpi/artwork_recognition && mkdir build && cd build && cmake .. && make

# nav_client (TurtleBot RPi)
cd ~/nav_ws && colcon build && source install/setup.bash

# UI (UI RPi)
cd robot/ui_rpi/ui && mkdir build && cd build && cmake .. && make
```

### 실행 순서

```bash
# 1. 서버 PC
python3 server/server.py

# 2. 입구 RPi
./gate/build/gate_client

# 3. TurtleBot RPi
ros2 launch turtlebot3_navigation2 navigation2.launch.py
./robot/turtlebot_rpi/artwork_recognition/build/artwork_recognition
python3 robot/turtlebot_rpi/nav_client/gesture.py
ros2 run nav_client nav_client

# 4. UI RPi
./robot/ui_rpi/ui/build/museum_robot_ui
```

### 티켓 발급

```bash
python3 server/issue_ticket.py
# tickets/<UUID>.png 생성 → 인쇄 후 관람객에게 배포
```

---

## 발전 가능성

- **LLM 기반 대화형 안내**: GPT/Claude API 연동으로 자연어 질의응답 지원
- **실시간 지도 갱신**: SLAM으로 전시관 변경 시 자동 맵 업데이트
- **클라우드 티켓 발권**: 온라인 예매 → QR 자동 발급 파이프라인 연동
- **3D 조각품 인식**: 포인트 클라우드 + 3D 특징 벡터로 조각품 대응
- **다중 로봇 확장**: 서버 DeviceState 구조 도입으로 복수 안내로봇 동시 운영
