// qr_main.cpp (통합본)
// 변경: HTTP POST 제거 → TCP 하나로 server.py와 통신
//   - 티켓 ID 전송
//   - AUTH_OK / AUTH_FAIL 수신
//   - 서보 제어
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "qr_scanner.h"
#include "servo_controller.h"

#define SERVER_IP        "10.10.16.53"
#define SERVER_PORT      9995   // server.py QR 수신 포트 (티켓ID 전송 + AUTH 결과 수신)
#define DOOR_OPEN_SEC    3

// ── 공통 소켓 함수 ──────────────────────────
static bool recvAll(int sock, uint8_t *buf, size_t size) {
    size_t received = 0;
    while (received < size) {
        ssize_t n = recv(sock, buf + received, size - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

static bool sendAll(int sock, const uint8_t *buf, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        ssize_t n = send(sock, buf + sent, size - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

static bool sendMsg(int sock, const std::string &msg) {
    uint32_t size = htonl(static_cast<uint32_t>(msg.size()));
    if (!sendAll(sock, reinterpret_cast<const uint8_t*>(&size), 4)) return false;
    if (!sendAll(sock, reinterpret_cast<const uint8_t*>(msg.data()), msg.size())) return false;
    return true;
}

static std::string recvMsg(int sock) {
    uint32_t size = 0;
    if (!recvAll(sock, reinterpret_cast<uint8_t*>(&size), 4)) return "";
    size = ntohl(size);

    std::string msg(size, '\0');
    if (!recvAll(sock, reinterpret_cast<uint8_t*>(msg.data()), size)) return "";
    return msg;
}

// ── server.py(9995)에 연결 ───────────────────
static int connectToServer() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    std::cout << "[서버] server.py 연결 완료" << std::endl;
    return sock;
}

int main() {
    std::cout << "=== 박물관 입장 게이트 ===" << std::endl;

    if (!servo_init()) {
        std::cerr << "[오류] 서보 초기화 실패. 종료합니다." << std::endl;
        return 1;
    }
    servo_close();  // 시작 시 문 닫힌 상태

    int sock = -1;

    while (true) {
        // server.py 연결이 끊겼으면 재연결
        if (sock < 0) {
            std::cout << "[서버] 연결 시도..." << std::endl;
            sock = connectToServer();
            if (sock < 0) {
                std::cerr << "[서버] 연결 실패. 3초 후 재시도..." << std::endl;
                sleep(3);
                continue;
            }
        }

        // QR 스캔 → ticket_id 획득
        std::string ticket_id = scan_qr();
        if (ticket_id.empty()) {
            std::cerr << "[QR] 스캔 실패. 재시도..." << std::endl;
            continue;
        }

        // server.py로 ticket_id 전송
        if (!sendMsg(sock, ticket_id)) {
            std::cerr << "[서버] ticket_id 전송 실패. 재연결 예정." << std::endl;
            close(sock);
            sock = -1;
            continue;
        }

        // server.py로부터 AUTH_OK / AUTH_FAIL 수신
        std::string auth_result = recvMsg(sock);
        if (auth_result.empty()) {
            std::cerr << "[서버] 응답 수신 실패. 재연결 예정." << std::endl;
            close(sock);
            sock = -1;
            continue;
        }

        std::cout << "[인증 결과] " << auth_result << std::endl;

        if (auth_result == "AUTH_OK") {
            std::cout << "[결과] 인증 성공 → 서보 열림" << std::endl;
            servo_open();
            sleep(DOOR_OPEN_SEC);
            servo_close();
            std::cout << "[결과] 문 닫힘. 다음 입장 대기 중..." << std::endl;
        } else {
            std::cout << "[결과] 인증 실패." << std::endl;
            sleep(1);
        }
    }

    if (sock >= 0) close(sock);
    servo_cleanup();
    return 0;
}
