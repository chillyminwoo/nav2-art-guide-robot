# server.py (통합본 - qr_server 흡수)
import socket
import struct
import threading
import sqlite3
import numpy as np
import cv2
import torch
import torchvision.models as models
import torchvision.transforms as transforms
from PIL import Image
from collections import deque, Counter

# =====================
# 티켓 DB (SQLite) — qr_server 통합
# =====================
import os
DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tickets.db")

def init_db():
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute('''
        CREATE TABLE IF NOT EXISTS tickets (
            id         TEXT PRIMARY KEY,
            used       INTEGER DEFAULT 0,
            created_at TEXT DEFAULT (datetime('now', 'localtime')),
            used_at    TEXT
        )
    ''')
    conn.commit()
    conn.close()
    print(f"티켓 DB 초기화 완료: {DB_PATH}")

def verify_ticket(ticket_id: str) -> dict:
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("SELECT id, used FROM tickets WHERE id = ?", (ticket_id,))
    row = c.fetchone()
    if row is None:
        conn.close()
        return {"valid": False, "reason": "존재하지 않는 티켓"}
    if row[1] == 1:
        conn.close()
        return {"valid": False, "reason": "이미 사용된 티켓"}
    c.execute(
        "UPDATE tickets SET used = 1, used_at = datetime('now', 'localtime') WHERE id = ?",
        (ticket_id,)
    )
    conn.commit()
    conn.close()
    return {"valid": True, "reason": "인증 성공"}

# =====================
# 모델 및 DB 초기화
# =====================
device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
print(f"사용 디바이스: {device}")

model = models.mobilenet_v2(weights='IMAGENET1K_V1')
model.classifier = torch.nn.Identity()
model = model.to(device)
model.eval()

transform = transforms.Compose([
    transforms.Resize((224, 224)),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406],
                         [0.229, 0.224, 0.225]),
])

def extract_feature(img_bgr):
    img_rgb = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2RGB)
    pil_img = Image.fromarray(img_rgb)
    tensor = transform(pil_img).unsqueeze(0).to(device)
    with torch.no_grad():
        feature = model(tensor).squeeze().cpu().numpy()
    return feature

def cosine_similarity(a, b):
    return np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b))

# =====================
# 작품 DB
# =====================
artwork_list = {
    "진주 귀걸이 소녀": "/home/ubuntu/artwork_recognition/artwork_db/artwork4.png",
    "그랑드 자트":      "/home/ubuntu/artwork_recognition/artwork_db/artwork5.png",
    "세잔 정물화":      "/home/ubuntu/artwork_recognition/artwork_db/artwork6.png"
}

artwork_info = {
    "진주 귀걸이 소녀": {
        "작가": "요하네스 페르메이르", "연도": "1665년경",
        "장르": "인물화", "기법": "유화",
        "소장처": "마우리츠하이스 미술관, 네덜란드 헤이그",
        "설명": "바로크 시대의 거장 페르메이르의 대표작으로, "
                "정체불명의 소녀가 진주 귀걸이를 하고 "
                "관람자를 바라보는 모습을 담은 작품입니다. "
                "네덜란드의 모나리자라고도 불립니다."
    },
    "그랑드 자트": {
        "작가": "조르주 쇠라", "연도": "1884~1886년",
        "장르": "풍경화/인물화", "기법": "점묘법 (유화)",
        "소장처": "시카고 미술관, 미국",
        "설명": "수백만 개의 작은 점으로 그린 점묘법의 대표작으로, "
                "파리 근교 그랑드 자트 섬에서 일요일 오후를 "
                "즐기는 사람들을 묘사한 작품입니다."
    },
    "세잔 정물화": {
        "작가": "폴 세잔", "연도": "1895~1900년경",
        "장르": "정물화", "기법": "유화",
        "소장처": "오르세 미술관, 프랑스 파리",
        "설명": "근대 회화의 아버지라 불리는 세잔의 정물화로, "
                "사과와 오렌지를 독특한 원근법과 색채로 표현했습니다. "
                "입체파에 큰 영향을 준 작품입니다."
    }
}

print("작품 DB 구축 중...")
artwork_db = {}
for name, path in artwork_list.items():
    img = cv2.imread(path)
    if img is None:
        print(f"이미지 로드 실패: {path}")
        continue
    artwork_db[name] = extract_feature(img)
    print(f"DB 등록 완료: {name}")

# =====================
# 공통 수신/송신 함수
# =====================
def recv_all(sock, size):
    data = b''
    while len(data) < size:
        packet = sock.recv(size - len(data))
        if not packet:
            return None
        data += packet
    return data

def send_msg(sock, msg_str):
    msg_bytes = msg_str.encode('utf-8')
    sock.sendall(struct.pack('>I', len(msg_bytes)))
    sock.sendall(msg_bytes)

# =====================
# 인식 상태 플래그
# =====================
last_sent_artwork  = None
recognition_active = False
recognition_lock   = threading.Lock()
current_dest       = None

# =====================
# QR Pi 클라이언트 (포트 9995)
# 변경: 티켓 ID 수신 → 직접 DB 검증 → qr_main 응답 + Qt UI 중계
# =====================
qr_ui_clients = []
qr_ui_lock    = threading.Lock()

def send_to_qr_ui(msg_str):
    msg_bytes = msg_str.encode('utf-8')
    with qr_ui_lock:
        disconnected = []
        for client in qr_ui_clients:
            try:
                client.sendall(struct.pack('>I', len(msg_bytes)))
                client.sendall(msg_bytes)
            except Exception:
                disconnected.append(client)
        for c in disconnected:
            qr_ui_clients.remove(c)

def qr_result_handler(conn, addr):
    """
    QR Pi로부터 티켓 ID 수신
    → 직접 DB 검증
    → qr_main에 AUTH_OK/FAIL 응답
    → Qt UI로 동일 결과 중계
    """
    print(f"QR Pi 연결됨: {addr}")
    try:
        while True:
            size_data = recv_all(conn, 4)
            if not size_data:
                break
            msg_size = struct.unpack('>I', size_data)[0]
            msg_data = recv_all(conn, msg_size)
            if not msg_data:
                break

            ticket_id = msg_data.decode('utf-8')
            print(f"티켓 수신: {ticket_id[:8]}...")

            result   = verify_ticket(ticket_id)
            auth_msg = "AUTH_OK" if result["valid"] else "AUTH_FAIL"
            print(f"인증 결과: {auth_msg} ({result['reason']})")

            # qr_main으로 결과 응답
            send_msg(conn, auth_msg)

            # Qt UI로 중계
            send_to_qr_ui(auth_msg)

    except Exception as e:
        print(f"QR Pi 핸들러 오류: {e}")
    finally:
        conn.close()
        print("QR Pi 연결 종료")

def qr_result_server():
    """포트 9995: QR Pi 전용 수신 서버"""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('0.0.0.0', 9995))
    srv.listen(5)
    print("QR 결과 서버 대기 중... (포트: 9995)")
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=qr_result_handler, args=(conn, addr), daemon=True).start()

def qr_ui_server():
    """포트 9994: Qt UI Pi가 연결해서 AUTH 결과를 대기하는 서버"""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('0.0.0.0', 9994))
    srv.listen(5)
    print("QR UI 서버 대기 중... (포트: 9994)")
    while True:
        conn, addr = srv.accept()
        print(f"Qt UI QR 수신 연결됨: {addr}")
        with qr_ui_lock:
            qr_ui_clients.append(conn)
        def keep_alive(c):
            try:
                while True:
                    data = c.recv(1)
                    if not data:
                        break
            except Exception:
                pass
            finally:
                with qr_ui_lock:
                    if c in qr_ui_clients:
                        qr_ui_clients.remove(c)
                c.close()
                print("Qt UI QR 연결 종료")
        threading.Thread(target=keep_alive, args=(conn,), daemon=True).start()

# =====================
# Pi4 #1 Nav 클라이언트 (포트 9996)
# =====================
pi1_nav_conn = None
pi1_nav_lock = threading.Lock()

def send_goal_to_pi1(dest_name):
    with pi1_nav_lock:
        if pi1_nav_conn is None:
            print(f"Pi4 #1 미연결 - 목적지 전송 실패: {dest_name}")
            return
        try:
            send_msg(pi1_nav_conn, dest_name)
            print(f"Pi4 #1으로 목적지 전송: {dest_name}")
        except Exception as e:
            print(f"Pi4 #1 전송 오류: {e}")

def pi1_nav_handler(conn, addr):
    global pi1_nav_conn, recognition_active, last_sent_artwork
    print(f"Pi4 #1 Nav 연결됨: {addr}")
    with pi1_nav_lock:
        pi1_nav_conn = conn
    try:
        while True:
            size_data = recv_all(conn, 4)
            if not size_data:
                break
            msg_size = struct.unpack('>I', size_data)[0]
            msg_data = recv_all(conn, msg_size)
            if not msg_data:
                break
            msg = msg_data.decode('utf-8')
            print(f"Pi4 #1 수신: {msg}")
            if msg.startswith("ARRIVED"):
                # "ARRIVED:목적지명" 또는 구버전 "ARRIVED" 모두 처리
                if ":" in msg:
                    arrived_dest = msg.split(":", 1)[1]
                else:
                    arrived_dest = current_dest  # 구버전 fallback
                artwork_dests = {"전시관 A", "전시관 B", "전시관 C"}
                with recognition_lock:
                    if arrived_dest in artwork_dests:
                        recognition_active = True
                        last_sent_artwork   = None
                        print("작품 인식 활성화")
                    else:
                        recognition_active = False
                        print("작품 인식 비활성화 (작품 목적지 아님)")
                send_to_pi2_ui(f"ARRIVED:{arrived_dest}")
    except Exception as e:
        print(f"Pi4 #1 Nav 오류: {e}")
    finally:
        with pi1_nav_lock:
            pi1_nav_conn = None
        conn.close()
        print("Pi4 #1 Nav 연결 종료")

def pi1_nav_server():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('0.0.0.0', 9996))
    srv.listen(1)
    print("Pi4 #1 Nav 서버 대기 중... (포트: 9996)")
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=pi1_nav_handler, args=(conn, addr), daemon=True).start()

# =====================
# Pi4 #2 UI 클라이언트 (포트 9997)
# =====================
pi2_ui_clients = []
pi2_ui_lock    = threading.Lock()

def send_to_pi2_ui(msg_str):
    msg_bytes = msg_str.encode('utf-8')
    with pi2_ui_lock:
        disconnected = []
        for client in pi2_ui_clients:
            try:
                client.sendall(struct.pack('>I', len(msg_bytes)))
                client.sendall(msg_bytes)
            except Exception:
                disconnected.append(client)
        for c in disconnected:
            pi2_ui_clients.remove(c)

def pi2_ui_handler(conn, addr):
    global recognition_active, last_sent_artwork, current_dest
    print(f"Pi4 #2 UI 연결됨: {addr}")
    with pi2_ui_lock:
        pi2_ui_clients.append(conn)
    try:
        while True:
            size_data = recv_all(conn, 4)
            if not size_data:
                break
            msg_size = struct.unpack('>I', size_data)[0]
            msg_data = recv_all(conn, msg_size)
            if not msg_data:
                break
            dest = msg_data.decode('utf-8')
            print(f"Pi4 #2 목적지 수신: {dest}")

            # 긴급 정지 처리
            if dest == "ESTOP":
                print("긴급 정지 수신! Nav 클라이언트로 전달")
                with recognition_lock:
                    recognition_active = False
                    last_sent_artwork  = None
                send_goal_to_pi1("ESTOP")
                continue

            with recognition_lock:
                current_dest       = dest
                recognition_active = False
                last_sent_artwork  = None
            print("작품 인식 비활성화 (이동 중)")
            send_goal_to_pi1(dest)
    except Exception as e:
        print(f"Pi4 #2 UI 오류: {e}")
    finally:
        with pi2_ui_lock:
            if conn in pi2_ui_clients:
                pi2_ui_clients.remove(conn)
        conn.close()
        print("Pi4 #2 UI 연결 종료")

def pi2_ui_server():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('0.0.0.0', 9997))
    srv.listen(5)
    print("Pi4 #2 UI 서버 대기 중... (포트: 9997)")
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=pi2_ui_handler, args=(conn, addr), daemon=True).start()

# =====================
# Pi4 #2 작품정보 클라이언트 (포트 9998)
# =====================
pi2_info_clients = []
pi2_info_lock    = threading.Lock()

def send_to_pi2_info(artwork_name):
    if artwork_name not in artwork_info:
        return
    info = artwork_info[artwork_name]
    msg = "|".join([artwork_name, info["작가"], info["연도"],
                    info["장르"], info["기법"], info["소장처"], info["설명"]])
    msg_bytes = msg.encode('utf-8')
    with pi2_info_lock:
        disconnected = []
        for client in pi2_info_clients:
            try:
                client.sendall(struct.pack('>I', len(msg_bytes)))
                client.sendall(msg_bytes)
            except Exception:
                disconnected.append(client)
        for c in disconnected:
            pi2_info_clients.remove(c)

def pi2_info_handler(conn, addr):
    print(f"Pi4 #2 Info 연결됨: {addr}")
    with pi2_info_lock:
        pi2_info_clients.append(conn)
    try:
        while True:
            data = conn.recv(1)
            if not data:
                break
    except Exception:
        pass
    finally:
        with pi2_info_lock:
            if conn in pi2_info_clients:
                pi2_info_clients.remove(conn)
        conn.close()

def pi2_info_server():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('0.0.0.0', 9998))
    srv.listen(5)
    print("Pi4 #2 Info 서버 대기 중... (포트: 9998)")
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=pi2_info_handler, args=(conn, addr), daemon=True).start()

# =====================
# 제스처 제어 (포트 9993) ← 신규 추가
# Robot Pi의 gesture_control_node.py 로부터 ESTOP / RESUME 수신
# =====================
def gesture_handler(conn, addr):
    """
    ESTOP  → Nav2 Goal 취소 + 작품 인식 중단
    RESUME → current_dest 로 Nav2 Goal 재전송 (주먹 풀고 손 펴면 재출발)
    """
    global recognition_active, last_sent_artwork
    print(f"[Gesture] 클라이언트 연결됨: {addr}")
    try:
        while True:
            size_data = recv_all(conn, 4)
            if not size_data:
                break
            msg_size = struct.unpack('>I', size_data)[0]
            msg_data = recv_all(conn, msg_size)
            if not msg_data:
                break

            cmd = msg_data.decode('utf-8').strip()
            print(f"[Gesture] 명령 수신: {cmd}")

            if cmd == "ESTOP":
                with recognition_lock:
                    recognition_active = False
                    last_sent_artwork  = None
                send_goal_to_pi1("ESTOP")
                print("[Gesture] ESTOP → Nav 취소 완료")

            elif cmd.startswith("CALL:"):
                # 사용자 위치 호출: "CALL:x_cam,z_cam"
                # nav_client 에서 TF로 맵 좌표 변환 후 Nav2 Goal 발행
                with recognition_lock:
                    recognition_active = False
                    last_sent_artwork  = None
                send_goal_to_pi1(cmd)
                print(f"[Gesture] 사용자 호출 → nav_client 전달: {cmd}")

            else:
                print(f"[Gesture] 알 수 없는 명령: {cmd}")

    except Exception as e:
        print(f"[Gesture] 핸들러 오류: {e}")
    finally:
        conn.close()
        print(f"[Gesture] 클라이언트 연결 종료: {addr}")

def gesture_server():
    """포트 9993: Robot Pi gesture_control_node.py 전용"""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('0.0.0.0', 9993))
    srv.listen(5)
    print("제스처 서버 대기 중... (포트: 9993)")
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=gesture_handler, args=(conn, addr), daemon=True).start()

# =====================
# 서버 시작
# =====================
init_db()  # 티켓 DB 초기화

threading.Thread(target=qr_result_server, daemon=True).start()  # 9995
threading.Thread(target=qr_ui_server,     daemon=True).start()  # 9994
threading.Thread(target=pi1_nav_server,   daemon=True).start()  # 9996
threading.Thread(target=pi2_ui_server,    daemon=True).start()  # 9997
threading.Thread(target=pi2_info_server,  daemon=True).start()  # 9998
threading.Thread(target=gesture_server,   daemon=True).start()  # 9993 ← 신규

# =====================
# Pi4 #1 카메라 소켓 서버 (포트 9999)
# =====================
camera_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
camera_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
camera_sock.bind(('0.0.0.0', 9999))
camera_sock.listen(1)
print("Pi4 #1 카메라 서버 대기 중... (포트: 9999)")

while True:
    conn, addr = camera_sock.accept()
    print(f"Pi4 #1 카메라 연결됨: {addr}")
    history = deque(maxlen=5)

    try:
        while True:
            size_data = recv_all(conn, 4)
            if not size_data:
                break
            frame_size = struct.unpack('>I', size_data)[0]

            frame_data = recv_all(conn, frame_size)
            if not frame_data:
                break

            dist_data = recv_all(conn, 4)
            if not dist_data:
                break
            dist_net = struct.unpack('>I', dist_data)[0]
            dist     = struct.unpack('f', struct.pack('I', dist_net))[0]

            np_arr = np.frombuffer(frame_data, dtype=np.uint8)
            frame  = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)
            if frame is None:
                continue

            feature    = extract_feature(frame)
            best_name  = "없음"
            best_score = 0.0

            for name, db_feature in artwork_db.items():
                score = cosine_similarity(feature, db_feature)
                if score > best_score:
                    best_score = score
                    best_name  = name

            if best_score < 0.58:
                best_name = "없음"

            history.append(best_name)
            stable_name = Counter(history).most_common(1)[0][0]

            if dist > 0.0:
                print(f"거리: {dist:.2f}m")
            print(f"안정화 결과: {stable_name} (유사도: {best_score:.4f})")

            with recognition_lock:
                active = recognition_active
                last   = last_sent_artwork

            if active and stable_name != "없음" and stable_name != last:
                with recognition_lock:
                    last_sent_artwork = stable_name
                send_to_pi2_info(stable_name)
                print(f"Pi4 #2로 작품 정보 전송: {stable_name}")

            result       = f"{stable_name}|{best_score:.4f}"
            result_bytes = result.encode('utf-8')
            conn.sendall(struct.pack('>I', len(result_bytes)))
            conn.sendall(result_bytes)

    except Exception as e:
        print(f"카메라 연결 오류: {e}")
    finally:
        conn.close()
        print("Pi4 #1 카메라 연결 종료")