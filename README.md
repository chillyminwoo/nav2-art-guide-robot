# Art Museum Guide Robot System

---

# 1. 주제

**미술관 안내로봇 시스템 (Museum Guide Robot System)**
QR 티켓 인증 · 자율주행 · 작품 인식 · 제스처 제어 통합 플랫폼
TurtleBot / ROS2 Nav2 / Qt UI / MobileNetV2 / MediaPipe / TCP

---

## 📌 프로젝트 개요
 
- 📅 **수행 기간**: 2026.04.17 ~ 2026.04.27
- 🛠 **사용 기술**:
  - TurtleBot / ROS2 Nav2
  - Raspberry Pi 4 (×2) / Python 3
  - C++17 / Qt6 Dashboard
  - MobileNetV2 (PyTorch) 작품 인식
  - MediaPipe Hands 제스처 인식
  - RealSense D435 RGB+깊이 카메라
  - SQLite3 / WiFi TCP 통신
- 🎯 **주요 기능**:
  - OpenCV QR 인증 → 서보모터 출입문 자동 개방
  - Qt 목적지 선택 → Nav2 자율주행 이동
  - MobileNetV2 + 코사인 유사도 기반 미술작품 자동 인식 및 TTS 안내
  - MediaPipe 제스처(주먹/손 펼치기)로 로봇 즉시 정지 및 호출
  - 카메라 1대로 제스처·작품 인식 포트 분리 이중 활용
  - 관리자 Qt 대시보드 실시간 제어 및 TTS 작품 설명 출력
 
---

# 2. 개발 배경 및 목표

최근 문화예술 관람객이 꾸준히 증가하면서 박물관·미술관에서는 인력 부족과 안내원 과로 문제가 대두되고 있다.
기존 안내원이 구석에 상주하며 관람객을 응대하는 방식의 한계를 극복하기 위해, 길 안내와 작품 설명을 자동으로 수행하는 안내로봇 시스템을 개발하였다.

| 핵심 기능 | 구현 방법 |
| --- | --- |
| QR 티켓 인증 | OpenCV QRCodeDetector → TCP → SQLite 검증 → 서보모터 출입문 개방 |
| 자율주행 목적지 이동 | Qt 목적지 선택 → TCP → Nav2 Action → TurtleBot |
| 미술작품 인식 | MobileNetV2 특징 벡터 + 코사인 유사도 → TTS 작품 설명 |
| 제스처 제어 | MediaPipe CNN 손 인식 → RealSense D435 깊이 → ESTOP / CALL 명령 |
| 이동 중 제스처만 활성 | 카메라 1대로 제스처/작품 인식 분리 (포트 이중화) |

관리자 / 관람객 양방향 인터랙션 : Qt 대시보드로 목적지 선택, TTS로 작품 안내, 제스처로 로봇 즉시 제어

---

# 3. 시스템 구성도

<p align="center">
  <img src="https://github.com/1213ES/Art-Museum-Guide-Robot---ROS2/raw/main/images/flow1.png" width="700"/>
</p>

<p align="center">
  <img src="https://github.com/1213ES/Art-Museum-Guide-Robot---ROS2/raw/main/images/flow2.png" width="700"/>
</p>

```
[QR Pi (RPi4 #1)]
  웹캠           OpenCV QRCodeDetector
  서보모터       출입문 개방 제어
        |
        | TCP 9995 (티켓 ID 전송)
        ↓
[Server PC]
  Python 멀티스레드 서버
  SQLite3          tickets.db 티켓 검증
  MobileNetV2      작품 특징 벡터 DB
  코사인 유사도    프레임 안정화 (deque 5개)
        |
        | TCP 9994 (AUTH_OK / AUTH_FAIL)
        | TCP 9996 (목적지 문자열 / ESTOP / CALL:x,z)
        | TCP 9998 (작품 정보 파이프 구분)
        ↓
[Qt UI Pi (RPi4 #2)]
  Qt C++  QRPage / DestinationPage / NavigatingPage / ArtworkPage
  TTS     QProcess → espeak/piper
        |
        | TCP 9997 (목적지 선택 / ESTOP)
        ↑
[TurtleBot + RealSense D435]
  nav_client.cpp   ROS2 ↔ TCP 브릿지 (포트 9996)
  Nav2             NavigateToPose Action
  main.cpp         카메라 프레임 분배 (로컬 9990 / 서버 9999)
  gesture.py       MediaPipe 손 인식 → 서버 9993
        |
        | TCP 9999 (JPEG + 깊이 4byte)  → 서버 작품 인식
        | TCP 9993 (ESTOP / CALL:x,z)  → 서버 → nav_client
```

---

# 3-1. 하드웨어 정리
<p align="center">
  <img src="https://github.com/1213ES/Art-Museum-Guide-Robot---ROS2/raw/main/images/hardware_front.png" width="250"/>
  &nbsp;&nbsp;
  <img src="https://github.com/1213ES/Art-Museum-Guide-Robot---ROS2/raw/main/images/hardware_backt.png" width="250"/>
</p>


## 🔧 부품 목록

| 부품 | 역할 |
| --- | --- |
| TurtleBot | 자율주행 플랫폼, ROS2/Nav2 |
| RealSense D435 | 컬러(424×240) + 깊이 스트림 → 제스처·작품 인식 |
| Raspberry Pi 4 #1 | QR 스캔 + 서보모터 출입문 제어 |
| Raspberry Pi 4 #2 | Qt UI 실행 + TTS 재생 |
| Server PC | Python 서버, MobileNetV2, SQLite |
| 웹캠 (QR Pi) | OpenCV QR 코드 인식 |
| 서보모터 | 출입문 개방/폐쇄 물리 제어 |

---

# 4. 기술 정리

## 하드웨어

| 부품 | 역할 |
| --- | --- |
| TurtleBot | 바퀴 주행 + ROS2 Nav2 자율항법 |
| RealSense D435 | RGB + 깊이 동시 스트림 → 제스처·작품 인식 |
| Raspberry Pi 4 (×2) | #1 QR/서보, #2 Qt UI/TTS |
| 서보모터 | 티켓 인증 성공 시 출입문 물리 개방 |

## 소프트웨어

| 항목 | 내용 |
| --- | --- |
| 서버 | Python 3 멀티스레드 (threading) |
| Qt UI | C++17 + Qt6 — QRPage / DestinationPage / NavigatingPage / ArtworkPage |
| ROS2 Nav 브릿지 | C++17 nav_client.cpp — TCP ↔ Nav2 Action |
| 작품 인식 | MobileNetV2 (PyTorch) + 코사인 유사도 — 임계값 0.58 / 5프레임 안정화 |
| 제스처 인식 | MediaPipe Hands CNN — 6프레임 중 60% 이상 동일 시 확정 |
| 티켓 DB | SQLite3 — tickets.db (id / used / created_at / used_at) |
| TTS | QProcess → espeak/piper (Qt UI Pi에서 작품 설명 음성 출력) |

## 통신

| 구간 | 방식 | 세부 |
| --- | --- | --- |
| QR Pi → Server | WiFi TCP 9995 | 티켓 ID 전송 |
| Server → Qt UI Pi | WiFi TCP 9994 | AUTH_OK / AUTH_FAIL |
| Qt UI Pi → Server | WiFi TCP 9997 | 목적지 문자열 / ESTOP |
| Server → TurtleBot | WiFi TCP 9996 | 목적지 / ESTOP / CALL:x,z |
| TurtleBot → Server | WiFi TCP 9999 | JPEG 프레임 + 깊이값 |
| gesture.py → Server | WiFi TCP 9993 | ESTOP / CALL:x,z |
| Server → Qt UI Pi | WiFi TCP 9998 | 작품 정보 파이프 구분 문자열 |
| main.cpp → gesture.py | 로컬 TCP 9990 | 컬러 JPEG + 깊이 프레임 |
| TurtleBot ↔ Nav2 | ROS2 Action | NavigateToPose |

---

# 5. 주요 기능

## QR 티켓 인증

- QR Pi(RPi4 #1) 웹캠에서 `cv2.QRCodeDetector`로 QR 코드 감지
- 감지된 티켓 ID를 TCP 4바이트 길이 헤더 + 본문 포맷으로 서버(9995)에 전송
- 서버에서 `tickets.db` SQLite 조회 → 미사용 티켓이면 `used=1` 업데이트 후 `AUTH_OK` 응답
- 이미 사용됐거나 존재하지 않으면 `AUTH_FAIL` 응답
- 인증 결과는 QR Pi(서보모터 개방)와 Qt UI Pi(화면 전환) 양쪽으로 동시 중계

## 자율주행 목적지 이동

- Qt UI에서 전시관 A/B/C, 화장실, 출입구 중 선택 → 서버(9997) 전송
- 서버가 목적지 문자열을 nav_client.cpp(9996)로 relay
- `nav_client.cpp`에서 하드코딩된 `DESTINATION_POSES` 맵에서 x/y/quaternion 좌표 조회 → `NavigateToPose` Action Goal 발행
- 도착 시 `ARRIVED:목적지명`을 서버로 역전송 → 서버가 Qt UI Pi로 알림 + 작품 전시관이면 작품 인식 활성화

## 미술작품 인식

- MobileNetV2 마지막 분류기 레이어를 `Identity()`로 교체해 1280차원 특징 벡터 추출
- 서버 시작 시 작품 DB 이미지(진주 귀걸이 소녀 / 그랑드 자트 / 세잔 정물화) 벡터를 사전 계산해 메모리에 보관
- TurtleBot 카메라(9999)로 수신한 프레임을 동일한 방식으로 벡터화 → 코사인 유사도 비교
- 코사인 유사도 0.58 미만이면 "없음", 이상이면 `deque(maxlen=5)` 안정화 후 최빈 작품명 확정
- 확정된 작품명을 Qt UI Pi(9998)로 전송 → TTS(작가, 연도, 기법, 설명 등 읽기)
- **이동 중에는 `recognition_active = False`** — 목적지 도착(`ARRIVED`) 시에만 활성화

## 제스처 제어

- RealSense D435 컬러 + 깊이 프레임을 `main.cpp`가 로컬 소켓(9990)으로 `gesture.py`에 전달
- `gesture.py`가 MediaPipe Hands CNN으로 21개 손 마디 x/y/z 좌표 추출
- 손가락 끝 > 손가락 중간 → 핀 것으로 판별; 5개 전부 피면 OPEN_HAND(보자기), 아니면 FIST(주먹)
- 6프레임 중 60% 이상 동일 제스처 확인 후 명령 확정
- **주먹** → `ESTOP` 전송(9993) → 서버 → nav_client `cancelGoal()`
- **손 펼치기** → 깊이값으로 사용자 3D 위치 계산 → `CALL:x_cam,z_cam` 전송(9993) → nav_client `sendGoalToUser()` (TF 변환으로 map 좌표 변환 후 Nav2 Goal 발행)

## 카메라 1대 이중 활용

- 이동 중에는 제스처 인식만 동작 (gesture.py가 프레임 소비)
- 목적지 도착 후에는 작품 인식만 동작 (서버 9999로 프레임 전송)
- 두 모드가 포트를 분리해 충돌 없이 전환

---

# 6. 데이터 저장 — 상세

## 티켓 DB (tickets.db)

```sql
CREATE TABLE IF NOT EXISTS tickets (
    id         TEXT PRIMARY KEY,
    used       INTEGER DEFAULT 0,
    created_at TEXT DEFAULT (datetime('now', 'localtime')),
    used_at    TEXT
);
```

| 항목 | 내용 |
| --- | --- |
| 발권 | `issue_ticket.py` 로 UUID 티켓 ID 사전 생성 후 DB 삽입 |
| 검증 | `used=0` 확인 → `used=1` + `used_at` 기록 (원자적 처리) |
| 이미 사용됨 | `"이미 사용된 티켓"` 응답, DB 변경 없음 |
| 없는 티켓 | `"존재하지 않는 티켓"` 응답 |

## 작품 정보 DB (in-memory)

```python
artwork_info = {
    "진주 귀걸이 소녀": { 작가, 연도, 장르, 기법, 소장처, 설명 },
    "그랑드 자트":      { ... },
    "세잔 정물화":      { ... }
}
```

- 작품 이미지 경로 → MobileNetV2 특징 벡터로 변환 → `artwork_db` dict에 보관
- TTS 전송 포맷 : `작품명|작가|연도|장르|기법|소장처|설명` (파이프 구분)

---

# 7. 통신 상세

## 공통 프레임 포맷

```
[4byte 빅엔디안 uint32 길이] + [UTF-8 메시지 본문]
```

모든 TCP 채널(9993~9998)은 동일한 길이 헤더 + 본문 포맷 사용

## QR 티켓 채널

```
방향 : QR Pi → Server (9995)
내용 : 티켓 ID 문자열

방향 : Server → Qt UI Pi (9994)
내용 : "AUTH_OK" 또는 "AUTH_FAIL"
Qt UI Pi는 연결 후 대기 상태 유지 (keep-alive recv)
```

## 목적지/Nav 채널

```
방향 : Qt UI Pi → Server (9997)
내용 : "전시관 A" / "전시관 B" / "전시관 C" / "화장실" / "출입구" / "ESTOP"

방향 : Server → TurtleBot nav_client (9996)
내용 : 목적지 문자열 / "ESTOP" / "CALL:x_cam,z_cam"

방향 : nav_client → Server (역방향, 동일 소켓)
내용 : "ARRIVED:목적지명"

목적지 좌표 DB (nav_client.cpp 하드코딩)
  전시관 A : x=1.348, y=0.158, qz=0.691, qw=0.722
  전시관 B : x=2.753, y=-0.269, qz=-0.018, qw=0.999
  전시관 C : x=1.422, y=-0.750, qz=-0.719, qw=0.694
  화장실   : x=0.500, y=-0.800, qz=0.999, qw=0.001
  출입구   : x=0.0,   y=0.0,   qz=0.0,   qw=1.0
```

## 카메라/작품 인식 채널

```
방향 : TurtleBot → Server (9999)
프레임 형식 : [4byte 빅엔디안 JPEG 크기] + [JPEG 바이너리] + [4byte float 깊이값]

처리 흐름
  JPEG 수신 → OpenCV 디코드 → MobileNetV2 추론 → 코사인 유사도
  → deque(maxlen=5) 안정화 → recognition_active 확인
  → 작품 확정 시 Pi4 #2 (9998)로 작품 정보 전송
  → 인식 결과(작품명|유사도) 역전송
```

## 제스처 채널

```
방향 : gesture.py → Server (9993)
내용 : "ESTOP"          주먹 → 즉시 정지
       "CALL:x,z"       손 펼치기 → 사용자 위치로 이동

서버 처리
  ESTOP  → send_goal_to_pi1("ESTOP") → nav_client cancelGoal()
  CALL:  → send_goal_to_pi1("CALL:x,z") → nav_client sendGoalToUser()

nav_client.cpp sendGoalToUser() 흐름
  TF lookupTransform("map", "base_link") → 현재 로봇 자세 획득
  카메라 좌표 → base_link 변환 (x_bl=z_cam, y_bl=-x_cam)
  base_link → map 변환 (회전 행렬 적용)
  사용자 방향 yaw = atan2(-x_cam, z_cam)
  NavigateToPose Goal 발행
```

---

# 8. Qt UI 화면 구성
<p>
  <img src="https://github.com/1213ES/Art-Museum-Guide-Robot---ROS2/raw/main/images/qt_ui.png" width="650"/>
</p>

| 페이지 | 파일 | 역할 |
| --- | --- | --- |
| StandbyPage | standbypage.cpp | 대기 화면, QR 스캔 유도 |
| QRPage | qrpage.cpp | QR 인증 결과 표시 (AUTH_OK/FAIL), TTS 재생 |
| DestinationPage | destinationpage.cpp | 목적지 버튼 선택 → 서버 9997 전송 |
| NavigatingPage | navigatingpage.cpp | 이동 중 표시, ESTOP 버튼 |
| ArtworkPage | artworkpage.cpp | 작품 이미지 + 정보 표시, TTS 재생 |

페이지 전환 흐름

```
StandbyPage → (QR 스캔) → QRPage
QRPage → (AUTH_OK) → DestinationPage
DestinationPage → (선택) → NavigatingPage
NavigatingPage → (ARRIVED) → ArtworkPage
ArtworkPage → (완료) → DestinationPage (반복)
```

---

# 9. 트러블슈팅

| 문제 | 원인 | 해결 |
| --- | --- | --- |
| ROS2 ↔ TCP 통신 효율 | ROS2 토픽으로 서버-로봇 1:1 통신 시도 → 오버헤드 발생 | 1:1 통신은 TCP, ROS2는 Nav2 Action 전용으로 역할 분리 |
| 목적지 도착 오차 | Nav2 기본 허용 반경이 넓어 작품 정면 정확 정지 불가 | 목표 좌표 주변 허용 반경 조정 + x/y 보정값 반복 튜닝 |
| 이동 중 작품 오인식 | 카메라가 이동 중에도 작품을 인식해 잘못된 TTS 출력 | `recognition_active` 플래그로 도착 후에만 작품 인식 활성화 |

---

# 10. 발전 가능성

- **LLM 기반 대화형 안내** : GPT/Claude API 연동으로 자연어 질의응답 지원
- **실시간 지도 갱신** : SLAM으로 전시관 변경 시 자동 맵 업데이트
- **클라우드 티켓 발권** : 온라인 예매 → QR 자동 발급 파이프라인 연동
- **3D 조각품 인식** : 깊이 카메라 포인트 클라우드 + 3D 특징 벡터 추출로 조각품 대응
- **다중 로봇 확장** : 서버 DeviceState 구조 도입으로 복수 안내로봇 동시 운영
