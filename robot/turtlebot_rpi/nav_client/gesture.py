#!/usr/bin/env python3
"""
gesture.py  [Robot Pi에서 실행]

main.cpp 로컬소켓(9990)에서 컬러 JPEG + 깊이 프레임 수신
→ MediaPipe CNN 으로 손동작 인식
→ 서버(9993)로 명령 전송

  ✋ 손 펼치기 → CALL:x,z  → 사용자 위치로 로봇 이동
  ✊ 주먹 쥐기 → ESTOP     → 로봇 정지
"""

import cv2
import mediapipe as mp
import numpy as np
import socket
import struct
import time
from collections import deque, Counter
from enum import Enum, auto


# ─────────────────────────────────────────────
# 설정
# ─────────────────────────────────────────────
FRAME_SERVER_IP   = "127.0.0.1"
FRAME_SERVER_PORT = 9990

SERVER_IP   = "10.10.16.53"
SERVER_PORT = 9993

HISTORY_LEN      = 6
STABLE_THRESHOLD = 0.60
COOLDOWN_SEC     = 1.5

# RealSense D435 424x240 컬러 스트림 내부 파라미터 (근사값)
# 실제값과 다를 경우 약간의 오차 발생 (방향성에는 영향 없음)
FX = 305.0
FY = 305.0
CX = 212.0
CY = 120.0

DEPTH_WIDTH  = 424
DEPTH_HEIGHT = 240

# 사람 앞 정지 거리 (m) - 이 거리만큼 앞에서 멈춤
STOP_BEFORE  = 0.3


# ─────────────────────────────────────────────
# 제스처 클래스
# ─────────────────────────────────────────────
class Gesture(Enum):
    NONE      = auto()
    OPEN_HAND = auto()   # 손 펼치기 → 사용자 위치로 이동
    FIST      = auto()   # 주먹      → 정지


# ─────────────────────────────────────────────
# 소켓 유틸
# ─────────────────────────────────────────────
def recv_all(sock, size):
    data = b''
    while len(data) < size:
        packet = sock.recv(size - len(data))
        if not packet:
            return None
        data += packet
    return data

def send_msg(sock, msg: str) -> bool:
    try:
        data = msg.encode('utf-8')
        sock.sendall(struct.pack('>I', len(data)))
        sock.sendall(data)
        return True
    except Exception:
        return False


# ─────────────────────────────────────────────
# 랜드마크 기반 제스처 분류기 (MediaPipe CNN 위에 룰베이스)
# ─────────────────────────────────────────────
class LandmarkGestureClassifier:
    TIP_IDS = [4, 8, 12, 16, 20]
    PIP_IDS = [3, 6, 10, 14, 18]
    MCP_IDS = [2, 5,  9, 13, 17]

    def __call__(self, landmarks) -> Gesture:
        if not landmarks:
            return Gesture.NONE

        lm = landmarks
        mid_mcp_y = lm[9][1]
        wrist_y   = lm[0][1]
        hand_dir  = -1 if mid_mcp_y < wrist_y else 1

        extended = []
        for tip, pip, mcp in zip(self.TIP_IDS[1:], self.PIP_IDS[1:], self.MCP_IDS[1:]):
            tip_y = lm[tip][1]
            pip_y = lm[pip][1]
            if hand_dir == -1:
                extended.append(tip_y < pip_y - 0.03)
            else:
                extended.append(tip_y > pip_y + 0.03)

        thumb_tip_x = lm[4][0]
        thumb_ip_x  = lm[3][0]
        thumb_mcp_x = lm[2][0]
        extended.insert(0, abs(thumb_tip_x - thumb_mcp_x) > abs(thumb_ip_x - thumb_mcp_x) * 1.15)

        n_ext = sum(extended)
        if n_ext >= 4:
            return Gesture.OPEN_HAND
        elif n_ext <= 1:
            return Gesture.FIST
        return Gesture.NONE


# ─────────────────────────────────────────────
# 3D 좌표 계산
# 손바닥 중심(landmark 9) 픽셀 위치에서 깊이 조회
# → 카메라 좌표계 (X: 오른쪽, Y: 아래, Z: 앞)
# ─────────────────────────────────────────────
def get_user_3d(landmarks, depth_arr: np.ndarray):
    """
    returns (x_cam, z_cam) in meters
    x_cam: 카메라 기준 좌우 (오른쪽 +)
    z_cam: 카메라 기준 앞뒤 (앞 +)
    """
    # 손바닥 중심 = landmark 9 (중지 MCP)
    u = int(landmarks[9][0] * DEPTH_WIDTH)
    v = int(landmarks[9][1] * DEPTH_HEIGHT)

    # 경계 클램프
    u = max(5, min(DEPTH_WIDTH  - 6, u))
    v = max(5, min(DEPTH_HEIGHT - 6, v))

    # 10x10 영역 중앙값으로 노이즈 제거
    roi = depth_arr[v-5:v+5, u-5:u+5].flatten()
    roi = roi[roi > 0]  # 유효한 깊이값만
    if len(roi) == 0:
        return None, None

    d = float(np.median(roi)) / 1000.0  # mm → m

    if d <= 0.1 or d > 5.0:  # 너무 가깝거나 멀면 무효
        return None, None

    x_cam = (u - CX) * d / FX
    z_cam = d

    return x_cam, z_cam


# ─────────────────────────────────────────────
# 메인
# ─────────────────────────────────────────────
def main():
    classifier  = LandmarkGestureClassifier()
    history     = deque(maxlen=HISTORY_LEN)
    last_sent   = Gesture.NONE
    last_sent_t = 0.0

    mp_hands = mp.solutions.hands
    hands = mp_hands.Hands(
        static_image_mode=False,
        max_num_hands=1,
        min_detection_confidence=0.75,
        min_tracking_confidence=0.65,
        model_complexity=1,
    )

    # 서버 소켓 연결
    server_sock = None
    def connect_server():
        nonlocal server_sock
        while True:
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.connect((SERVER_IP, SERVER_PORT))
                server_sock = s
                print(f"[gesture] 서버 연결 완료: {SERVER_IP}:{SERVER_PORT}")
                return
            except Exception as e:
                print(f"[gesture] 서버 연결 실패 ({e}), 3초 후 재시도...")
                time.sleep(3)

    connect_server()

    # main.cpp 프레임 수신 루프
    while True:
        try:
            frame_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            frame_sock.connect((FRAME_SERVER_IP, FRAME_SERVER_PORT))
            print("[gesture] main.cpp 프레임 수신 시작")
        except Exception as e:
            print(f"[gesture] main.cpp 연결 실패 ({e}), 3초 후 재시도...")
            time.sleep(3)
            continue

        try:
            while True:
                # ── 컬러 JPEG 수신 ────────────────────
                size_data = recv_all(frame_sock, 4)
                if not size_data: break
                jpeg_size = struct.unpack('>I', size_data)[0]
                jpeg_data = recv_all(frame_sock, jpeg_size)
                if not jpeg_data: break

                # ── 깊이 프레임 수신 ──────────────────
                d_size_data = recv_all(frame_sock, 4)
                if not d_size_data: break
                d_size    = struct.unpack('>I', d_size_data)[0]
                depth_raw = recv_all(frame_sock, d_size)
                if not depth_raw: break

                # 디코딩
                np_arr    = np.frombuffer(jpeg_data, dtype=np.uint8)
                frame     = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)
                depth_arr = np.frombuffer(depth_raw, dtype=np.uint16).reshape(DEPTH_HEIGHT, DEPTH_WIDTH)
                if frame is None: continue

                # ── MediaPipe 추론 ────────────────────
                rgb     = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                results = hands.process(rgb)

                if results.multi_hand_landmarks:
                    lm_list = [(p.x, p.y, p.z)
                               for p in results.multi_hand_landmarks[0].landmark]
                    gesture = classifier(lm_list)
                else:
                    gesture = Gesture.NONE
                    lm_list = None

                history.append(gesture)

                # ── 안정화 ────────────────────────────
                if len(history) < HISTORY_LEN // 2:
                    continue
                counts = Counter(history)
                top, cnt = counts.most_common(1)[0]
                if cnt / len(history) < STABLE_THRESHOLD:
                    continue
                stable = top
                if stable == Gesture.NONE:
                    continue

                # ── 쿨다운 ────────────────────────────
                now = time.time()
                if stable == last_sent and (now - last_sent_t) < COOLDOWN_SEC:
                    continue

                # ── 명령 결정 ─────────────────────────
                cmd = None

                if stable == Gesture.FIST:
                    cmd = "ESTOP"

                elif stable == Gesture.OPEN_HAND and lm_list is not None:
                    x_cam, z_cam = get_user_3d(lm_list, depth_arr)
                    if x_cam is not None:
                        # 사람 앞 STOP_BEFORE 미터 앞 목표 좌표
                        target_z = z_cam - STOP_BEFORE
                        if target_z > 0.1:
                            cmd = f"CALL:{x_cam:.3f},{target_z:.3f}"
                        else:
                            # 너무 가까우면 그냥 정지
                            cmd = "ESTOP"

                if cmd is None:
                    continue

                # ── 전송 ─────────────────────────────
                if server_sock and send_msg(server_sock, cmd):
                    if stable == Gesture.FIST:
                        print(f"[gesture] ✊ 주먹 → ESTOP 전송")
                    else:
                        print(f"[gesture] ✋ 손 펼치기 → 사용자 위치로 이동: {cmd}")
                    last_sent   = stable
                    last_sent_t = now
                else:
                    print("[gesture] 서버 전송 실패, 재연결 시도")
                    connect_server()

        except Exception as e:
            print(f"[gesture] 프레임 수신 오류: {e}")
        finally:
            frame_sock.close()
            print("[gesture] main.cpp 연결 끊김, 재연결 시도...")
            time.sleep(3)


if __name__ == "__main__":
    main()
