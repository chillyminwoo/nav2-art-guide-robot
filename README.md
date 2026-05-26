# 박물관 안내 로봇 시스템

QR 입장 인증부터 Nav2 자율 이동, 작품 인식, 손 제스처 제어까지 통합한 박물관 안내 로봇 프로젝트.  
5개 컴포넌트가 TCP 소켓을 통해 중앙 서버와 연결되는 분산 구조로 동작한다.

---

## 시스템 구성

```
[입구 RPi]           [서버 PC]               [TurtleBot RPi]         [UI RPi]
 gate_client ─9995─► server.py ◄─9996─── nav_client (ROS2)    Qt6 UI ◄─9998─┐
                      │     │             artwork_recognition           │     │
                      │     └─9994──────────────────────────────────────►    │
                      │                  gesture.py ◄─(9990)◄─artwork_recog │
                      │                       │                              │
                      ◄─9993─────────────────┘                              │
                      └─9997◄──────────────────────────────────────── Qt6 UI┘
                      └─9999◄─── artwork_recognition
```

### 하드웨어 구성

| 기기 | 역할 |
|------|------|
| 서버 PC | 중앙 허브, 작품 인식 AI |
| 입구 RPi | QR 스캔, 서보 게이트 제어 |
| TurtleBot RPi | Nav2 자율 이동, RealSense 카메라, 손 제스처 인식 |
| UI RPi | 800×480 터치스크린 Qt6 UI |

### 전체 동작 흐름

1. 관리자가 `issue_ticket.py`로 UUID QR 티켓 발급
2. 관람객이 입구 QR 리더기에 티켓 제시 → `gate_client`가 서버로 전송 → DB 검증 → 서보 게이트 열림
3. 로봇 터치스크린(Qt UI)에서 목적지 선택
4. `nav_client`가 Nav2 액션 서버로 목표 좌표 전달 → 자율 이동
5. 전시관 도착 시 RealSense 카메라가 작품 인식 (MobileNetV2 + 코사인 유사도) → UI에 정보 표시 + TTS 안내
6. 손 펼치기 → 로봇이 관람객 위치로 이동 / 주먹 → 긴급 정지

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
├── robot/
│   ├── turtlebot_rpi/                  # TurtleBot 연결 RPi
│   │   ├── artwork_recognition/        # RealSense 카메라 클라이언트
│   │   │   ├── CMakeLists.txt
│   │   │   └── main.cpp                # 10f마다 서버 전송 / 3f마다 gesture.py 전송
│   │   └── nav_client/                 # ROS2 네비게이션 + 제스처
│   │       ├── CMakeLists.txt
│   │       ├── package.xml
│   │       ├── nav_client.cpp          # Nav2 액션 클라이언트, TF 좌표 변환
│   │       └── gesture.py              # MediaPipe 손 제스처 → CALL/ESTOP 전송
│   │
│   └── ui_rpi/                         # 터치스크린 전용 RPi
│       └── ui/
│           ├── CMakeLists.txt
│           ├── main.cpp
│           ├── mainwindow.cpp/h        # QStackedWidget 페이지 관리
│           └── pages/
│               ├── qrpage              # QR 인증 결과 수신 (9994), TTS 상태 관리
│               ├── standbypage         # 터치 감지
│               ├── destinationpage     # 목적지 5개 버튼 그리드
│               ├── navigatingpage      # 이동 중 프로그레스 바 + ARRIVED 수신
│               ├── artworkpage         # 작품 정보 표시 + gTTS/mpg123
│               └── navigationmanager  # 목적지 전송 / ARRIVED 수신 (9997)
│
└── .gitignore
```

---

## 통신 프로토콜

모든 메시지는 `[4바이트 빅엔디언 길이] + [UTF-8 페이로드]` 형식.

| 포트 | 방향 | 내용 |
|------|------|------|
| 9990 | artwork_recognition → gesture.py | JPEG + uint16 깊이 raw (로컬) |
| 9993 | gesture.py → server | `ESTOP` / `CALL:x,z` |
| 9994 | server → Qt UI | `AUTH_OK` / `AUTH_FAIL` |
| 9995 | gate_client → server | UUID 티켓 ID |
| 9996 | server ↔ nav_client | 목적지명 / `ARRIVED:목적지명` |
| 9997 | Qt UI → server | 목적지명 / `ESTOP` |
| 9998 | server → Qt UI | `작품명\|작가\|연도\|장르\|기법\|소장처\|설명` |
| 9999 | artwork_recognition → server | JPEG + float 깊이값 |

---

## 기술 스택

| 분류 | 사용 기술 |
|------|----------|
| 플랫폼 | Raspberry Pi (입구 × 1, TurtleBot × 1, UI × 1) |
| OS / 미들웨어 | Ubuntu 22.04, ROS2 Humble |
| 자율 이동 | Nav2 (NavigateToPose 액션), TurtleBot3 |
| 센서 | Intel RealSense D435 (컬러 + 깊이) |
| 작품 인식 | MobileNetV2 (ImageNet pretrained), 코사인 유사도 |
| 제스처 인식 | MediaPipe Hands, 룰베이스 분류기 |
| UI | Qt6 (Core / Widgets / Network), gTTS + mpg123 |
| QR / 서보 | OpenCV QRCodeDetector, lgpio (MG996R) |
| DB | SQLite3 (일회용 UUID 티켓) |
| 통신 | TCP 소켓 (길이-접두 프레임) |
| 빌드 | CMake 3.16+, colcon (ROS2 패키지) |

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
cd gate && mkdir build && cd build
cmake .. && make

# artwork_recognition (TurtleBot RPi)
cd robot/turtlebot_rpi/artwork_recognition && mkdir build && cd build
cmake .. && make

# nav_client (TurtleBot RPi, ROS2 워크스페이스)
cd ~/nav_ws
colcon build
source install/setup.bash

# UI (UI RPi)
cd robot/ui_rpi/ui && mkdir build && cd build
cmake .. && make
```

### 실행 순서

```bash
# 1. 서버 PC
python3 server/server.py

# 2. 입구 RPi
./gate/build/gate_client

# 3. TurtleBot RPi (각 터미널)
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
