// main.cpp
// 변경사항: gesture.py에 컬러 JPEG + 깊이 프레임 함께 전송
//           기존 TCP 9999 그림인식 로직 완전히 그대로 유지
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <librealsense2/rs.hpp>
#include <opencv2/opencv.hpp>

// =====================
// 소켓 전송/수신
// =====================
bool sendAll(int sock, const uint8_t* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        ssize_t n = send(sock, data + sent, size - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool recvAll(int sock, uint8_t* buf, size_t size) {
    size_t received = 0;
    while (received < size) {
        ssize_t n = recv(sock, buf + received, size - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

// =====================
// gesture.py 용 로컬 프레임 공유 (포트 9990)
// 패킷 포맷:
//   [jpeg_size(4)] [jpeg_data] [depth_size(4)] [depth_raw_uint16]
// depth_raw: 424x240 uint16, 단위 mm
// =====================
static int        g_gesture_sock = -1;
static std::mutex g_gesture_mutex;

void gestureServerThread() {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(9990);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    bind(srv, (sockaddr*)&addr, sizeof(addr));
    listen(srv, 1);
    std::cout << "[gesture] 로컬 프레임 서버 대기 중... (포트: 9990)" << std::endl;

    while (true) {
        int conn = accept(srv, nullptr, nullptr);
        if (conn < 0) continue;
        std::cout << "[gesture] gesture.py 연결됨" << std::endl;
        std::lock_guard<std::mutex> lock(g_gesture_mutex);
        if (g_gesture_sock >= 0) close(g_gesture_sock);
        g_gesture_sock = conn;
    }
}

void sendFrameToGesture(const std::vector<uint8_t>& jpeg,
                        const uint16_t* depth_data, size_t depth_bytes) {
    std::lock_guard<std::mutex> lock(g_gesture_mutex);
    if (g_gesture_sock < 0) return;

    uint32_t jpeg_size = htonl(jpeg.size());
    if (!sendAll(g_gesture_sock, (uint8_t*)&jpeg_size, 4) ||
        !sendAll(g_gesture_sock, jpeg.data(), jpeg.size())) {
        std::cout << "[gesture] gesture.py 연결 끊김" << std::endl;
        close(g_gesture_sock);
        g_gesture_sock = -1;
        return;
    }

    uint32_t d_size = htonl(depth_bytes);
    if (!sendAll(g_gesture_sock, (uint8_t*)&d_size, 4) ||
        !sendAll(g_gesture_sock, (const uint8_t*)depth_data, depth_bytes)) {
        std::cout << "[gesture] gesture.py 깊이 전송 실패" << std::endl;
        close(g_gesture_sock);
        g_gesture_sock = -1;
    }
}

// =====================
// main
// =====================
int main() {
    std::thread(gestureServerThread).detach();

    // ── 서버 연결 (그림인식) ──────────────────────
    const char* SERVER_IP   = "10.10.16.53";
    const int   SERVER_PORT = 9999;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "서버 연결 실패" << std::endl;
        return -1;
    }
    std::cout << "서버 연결 완료" << std::endl;

    // ── RealSense 초기화 ──────────────────────────
    rs2::pipeline pipeline;
    rs2::config   cfg;
    cfg.enable_stream(RS2_STREAM_COLOR, 424, 240, RS2_FORMAT_BGR8, 30);
    // cfg.enable_stream(RS2_STREAM_DEPTH, 424, 240, RS2_FORMAT_Z16,  30);
    cfg.enable_stream(RS2_STREAM_DEPTH, 480, 270, RS2_FORMAT_Z16, 30);
    pipeline.start(cfg);

    // 깊이를 컬러 프레임에 정렬
    rs2::align align_to_color(RS2_STREAM_COLOR);

    std::cout << "카메라 시작 완료" << std::endl;

    std::string last_artwork = "";
    int frame_count = 0;

    while (true) {
        rs2::frameset frames         = pipeline.wait_for_frames();
        auto          aligned_frames = align_to_color.process(frames);

        rs2::frame       color_frame = aligned_frames.get_color_frame();
        rs2::depth_frame depth_frame = aligned_frames.get_depth_frame();
        if (!color_frame || !depth_frame) continue;

        cv::Mat color_img(
            cv::Size(424, 240),
            CV_8UC3,
            (void*)color_frame.get_data(),
            cv::Mat::AUTO_STEP
        );

        frame_count++;

        // ── 매 3프레임마다 gesture.py 에 컬러+깊이 전달 ──
        if (frame_count % 3 == 0) {
            std::vector<uint8_t> gesture_jpeg;
            cv::imencode(".jpg", color_img, gesture_jpeg,
                        {cv::IMWRITE_JPEG_QUALITY, 70});

            const uint16_t* depth_data  = (const uint16_t*)depth_frame.get_data();
            size_t          depth_bytes = 424 * 240 * sizeof(uint16_t);

            sendFrameToGesture(gesture_jpeg, depth_data, depth_bytes);
        }

        // ── 매 10프레임마다 서버로 전송 (그림인식) ──────
        if (frame_count % 10 == 0) {
            std::vector<uint8_t> jpeg;
            cv::imencode(".jpg", color_img, jpeg,
                        {cv::IMWRITE_JPEG_QUALITY, 80});

            float dist = depth_frame.get_distance(212, 120);

            uint32_t size = htonl(jpeg.size());
            if (!sendAll(sock, (uint8_t*)&size, 4)) break;
            if (!sendAll(sock, jpeg.data(), jpeg.size())) break;

            uint32_t dist_net;
            memcpy(&dist_net, &dist, 4);
            dist_net = htonl(dist_net);
            if (!sendAll(sock, (uint8_t*)&dist_net, 4)) break;

            uint32_t result_size = 0;
            if (!recvAll(sock, (uint8_t*)&result_size, 4)) break;
            result_size = ntohl(result_size);

            std::vector<uint8_t> result_buf(result_size);
            if (!recvAll(sock, result_buf.data(), result_size)) break;
            std::string result(result_buf.begin(), result_buf.end());

            auto sep = result.find('|');
            std::string artwork_name = result.substr(0, sep);
            float score = std::stof(result.substr(sep + 1));

            if (artwork_name != last_artwork) {
                last_artwork = artwork_name;
                std::cout << "인식된 작품: " << artwork_name;
                if (score > 0.0f) {
                    std::cout << " (유사도: " << score << ")";
                    if (dist > 0.0f)
                        std::cout << " | 거리: " << dist << "m";
                }
                std::cout << std::endl;
            }
        }
    }

    close(sock);
    pipeline.stop();
    return 0;
}