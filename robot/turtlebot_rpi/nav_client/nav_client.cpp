#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <functional>
#include <atomic>
#include <mutex>
#include <cmath>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "tf2_ros/buffer.h"
#include "tf2_ros/transform_listener.h"
#include "tf2/LinearMath/Quaternion.h"
#include "tf2/LinearMath/Matrix3x3.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"

using NavigateToPose = nav2_msgs::action::NavigateToPose;
using GoalHandleNav  = rclcpp_action::ClientGoalHandle<NavigateToPose>;

// =====================
// 목적지 좌표 DB
// =====================
struct Pose {
    double x, y, z;
    double qx, qy, qz, qw;
};

static const std::map<std::string, Pose> DESTINATION_POSES = {
    {"전시관 A", { 1.348543508318109,   0.15897841725351927,  0.0,
                 0.0, 0.0,  0.6912196001481005,   0.722644770527747  }},
    {"전시관 B", { 2.7533017565522363, -0.2691128214396824,   0.0,
                 0.0, 0.0, -0.018383569948217927,  0.9998310078988144 }},
    {"전시관 C", { 1.422238562096601,  -0.7500966329032905,   0.0,
                 0.0, 0.0, -0.7196813653768039,    0.6943044953976455 }},
    {"화장실",  { 0.5007380441206662, -0.8003511481052001,   0.0,
                 0.0, 0.0,  0.999998277693159,     0.0018559662485194616 }},
    {"출입구", { 0.0, 0.0, 0.0,
                 0.0, 0.0,  0.0,                   1.0                }},
};

// =====================
// 공통 소켓 함수
// =====================
bool recvAll(int sock, uint8_t *buf, size_t size) {
    size_t received = 0;
    while (received < size) {
        ssize_t n = recv(sock, buf + received, size - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

bool sendMsg(int sock, const std::string &msg) {
    if (sock < 0) return false;
    uint32_t size = htonl(static_cast<uint32_t>(msg.size()));
    std::vector<uint8_t> buf(4 + msg.size());
    memcpy(buf.data(), &size, 4);
    memcpy(buf.data() + 4, msg.data(), msg.size());
    ssize_t n = send(sock, buf.data(), buf.size(), 0);
    return n == static_cast<ssize_t>(buf.size());
}

// =====================
// Nav2 클라이언트 노드
// =====================
class NavClientNode : public rclcpp::Node {
public:
    NavClientNode() : Node("nav_client_node") {
        action_client_ = rclcpp_action::create_client<NavigateToPose>(
            this, "navigate_to_pose");

        // TF 초기화 (사용자 호출 기능에 사용)
        tf_buffer_   = std::make_shared<tf2_ros::Buffer>(get_clock());
        tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

        RCLCPP_INFO(get_logger(), "Nav2 액션 클라이언트 초기화 완료");
    }

    bool isNavigating() const { return goal_active_.load(); }

    void cancelGoal() {
        std::lock_guard<std::mutex> lock(goal_mutex_);
        if (current_goal_handle_) {
            RCLCPP_WARN(get_logger(), "기존 Goal 취소 중...");
            action_client_->async_cancel_goal(current_goal_handle_);
            current_goal_handle_ = nullptr;
        }
        goal_active_ = false;
    }

    // ── 목적지 이름으로 Goal 발행 (기존) ───────────
    void sendGoal(const std::string &dest_name,
                  std::function<void(bool)> on_done) {
        auto it = DESTINATION_POSES.find(dest_name);
        if (it == DESTINATION_POSES.end()) {
            RCLCPP_ERROR(get_logger(), "알 수 없는 목적지: %s", dest_name.c_str());
            on_done(false);
            return;
        }
        if (!action_client_->wait_for_action_server(std::chrono::seconds(5))) {
            RCLCPP_ERROR(get_logger(), "Nav2 액션 서버 연결 실패");
            on_done(false);
            return;
        }

        const Pose &pose = it->second;
        auto goal_msg = NavigateToPose::Goal();
        goal_msg.pose.header.frame_id        = "map";
        goal_msg.pose.header.stamp           = get_clock()->now();
        goal_msg.pose.pose.position.x        = pose.x;
        goal_msg.pose.pose.position.y        = pose.y;
        goal_msg.pose.pose.position.z        = pose.z;
        goal_msg.pose.pose.orientation.x     = pose.qx;
        goal_msg.pose.pose.orientation.y     = pose.qy;
        goal_msg.pose.pose.orientation.z     = pose.qz;
        goal_msg.pose.pose.orientation.w     = pose.qw;

        _sendGoalMsg(goal_msg, dest_name, on_done);
    }

    // ── 사용자 위치로 Goal 발행 (신규) ─────────────
    // x_cam: 카메라 기준 좌우 (오른쪽 +, 미터)
    // z_cam: 카메라 기준 전방 거리 (미터, 이미 STOP_BEFORE 차감된 값)
    void sendGoalToUser(double x_cam, double z_cam,
                        std::function<void(bool)> on_done) {
        if (!action_client_->wait_for_action_server(std::chrono::seconds(5))) {
            RCLCPP_ERROR(get_logger(), "Nav2 액션 서버 연결 실패");
            on_done(false);
            return;
        }

        // ── 현재 로봇 자세 조회 (map → base_link) ──
        geometry_msgs::msg::TransformStamped tf_stamped;
        try {
            tf_stamped = tf_buffer_->lookupTransform(
                "map", "base_link", tf2::TimePointZero);
        } catch (tf2::TransformException &ex) {
            RCLCPP_ERROR(get_logger(), "TF 조회 실패: %s", ex.what());
            on_done(false);
            return;
        }

        double rx = tf_stamped.transform.translation.x;
        double ry = tf_stamped.transform.translation.y;

        tf2::Quaternion q;
        tf2::fromMsg(tf_stamped.transform.rotation, q);
        tf2::Matrix3x3 m(q);
        double roll, pitch, ryaw;
        m.getRPY(roll, pitch, ryaw);

        // ── 카메라 좌표 → base_link 좌표 변환 ──────
        // 카메라: X=오른쪽, Y=아래, Z=앞
        // base_link: X=앞,   Y=왼쪽, Z=위
        double x_bl =  z_cam;   // 전방
        double y_bl = -x_cam;   // 좌측

        // ── base_link → map 좌표 변환 ──────────────
        double x_map = rx + x_bl * std::cos(ryaw) - y_bl * std::sin(ryaw);
        double y_map = ry + x_bl * std::sin(ryaw) + y_bl * std::cos(ryaw);

        // 사용자를 바라보는 방향으로 yaw 설정
        double target_yaw = ryaw + std::atan2(-x_cam, z_cam);

        tf2::Quaternion target_q;
        target_q.setRPY(0.0, 0.0, target_yaw);

        RCLCPP_INFO(get_logger(),
            "사용자 호출 Goal → map(%.2f, %.2f) yaw=%.2f",
            x_map, y_map, target_yaw);

        auto goal_msg = NavigateToPose::Goal();
        goal_msg.pose.header.frame_id    = "map";
        goal_msg.pose.header.stamp       = get_clock()->now();
        goal_msg.pose.pose.position.x    = x_map;
        goal_msg.pose.pose.position.y    = y_map;
        goal_msg.pose.pose.orientation   = tf2::toMsg(target_q);

        _sendGoalMsg(goal_msg, "사용자위치", on_done);
    }

private:
    // ── 공통 Goal 발행 ──────────────────────────────
    void _sendGoalMsg(const NavigateToPose::Goal &goal_msg,
                      const std::string &label,
                      std::function<void(bool)> on_done) {
        goal_active_ = true;

        auto opts = rclcpp_action::Client<NavigateToPose>::SendGoalOptions();

        opts.goal_response_callback =
            [this, label](const GoalHandleNav::SharedPtr &gh) {
                if (!gh) {
                    RCLCPP_ERROR(get_logger(), "Goal 거절됨 [%s]", label.c_str());
                    goal_active_ = false;
                } else {
                    std::lock_guard<std::mutex> lock(goal_mutex_);
                    current_goal_handle_ = gh;
                    RCLCPP_INFO(get_logger(), "Goal 수락 [%s]", label.c_str());
                }
            };

        opts.result_callback =
            [this, on_done, label](const GoalHandleNav::WrappedResult &result) {
                { std::lock_guard<std::mutex> lock(goal_mutex_);
                  current_goal_handle_ = nullptr; }
                goal_active_ = false;

                switch (result.code) {
                case rclcpp_action::ResultCode::SUCCEEDED:
                    RCLCPP_INFO(get_logger(), "목적지 도착! [%s]", label.c_str());
                    on_done(true);
                    break;
                case rclcpp_action::ResultCode::CANCELED:
                    RCLCPP_WARN(get_logger(), "Goal 취소 [%s]", label.c_str());
                    on_done(false);
                    break;
                default:
                    RCLCPP_ERROR(get_logger(), "Goal 실패 [%s]", label.c_str());
                    on_done(false);
                    break;
                }
            };

        action_client_->async_send_goal(goal_msg, opts);
    }

    rclcpp_action::Client<NavigateToPose>::SharedPtr action_client_;
    GoalHandleNav::SharedPtr                          current_goal_handle_;
    std::mutex                                        goal_mutex_;
    std::atomic<bool>                                 goal_active_{false};

    std::shared_ptr<tf2_ros::Buffer>           tf_buffer_;
    std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
};

// =====================
// 메인
// =====================
int main(int argc, char *argv[]) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<NavClientNode>();

    std::thread spin_thread([&node]() { rclcpp::spin(node); });

    const char *SERVER_IP   = "10.10.16.53";
    const int   SERVER_PORT = 9996;

    std::atomic<int> current_sock{-1};
    std::mutex       sock_mutex;

    while (rclcpp::ok()) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port   = htons(SERVER_PORT);
        inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

        if (connect(sock, reinterpret_cast<sockaddr*>(&server_addr),
                    sizeof(server_addr)) < 0) {
            RCLCPP_ERROR(node->get_logger(), "서버 연결 실패. 3초 후 재연결...");
            close(sock);
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }
        { std::lock_guard<std::mutex> lock(sock_mutex); current_sock = sock; }
        RCLCPP_INFO(node->get_logger(), "서버 연결 완료: %s:%d", SERVER_IP, SERVER_PORT);

        while (rclcpp::ok()) {
            uint32_t msg_size = 0;
            if (!recvAll(sock, reinterpret_cast<uint8_t*>(&msg_size), 4)) break;
            msg_size = ntohl(msg_size);

            std::string dest(msg_size, '\0');
            if (!recvAll(sock, reinterpret_cast<uint8_t*>(dest.data()), msg_size)) break;

            RCLCPP_INFO(node->get_logger(), "수신: %s", dest.c_str());

            // ── 긴급 정지 ────────────────────────────
            if (dest == "ESTOP") {
                RCLCPP_WARN(node->get_logger(), "긴급 정지! Goal 취소");
                node->cancelGoal();
                continue;
            }

            // ── 사용자 위치 호출 (제스처) ─────────────
            // 포맷: "CALL:x_cam,z_cam"
            if (dest.substr(0, 5) == "CALL:") {
                std::string coords = dest.substr(5);
                size_t comma = coords.find(',');
                if (comma == std::string::npos) continue;

                double x_cam = std::stod(coords.substr(0, comma));
                double z_cam = std::stod(coords.substr(comma + 1));

                if (node->isNavigating()) {
                    node->cancelGoal();
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }

                int captured_sock = sock;
                node->sendGoalToUser(x_cam, z_cam,
                    [&node, &sock_mutex, &current_sock, captured_sock](bool success) {
                        if (!success) return;
                        std::lock_guard<std::mutex> lock(sock_mutex);
                        int active_sock = current_sock.load();
                        if (active_sock < 0) return;
                        sendMsg(active_sock, "ARRIVED:사용자위치");
                    });
                continue;
            }

            // ── 일반 목적지 이동 ──────────────────────
            if (node->isNavigating()) {
                RCLCPP_WARN(node->get_logger(), "이전 Goal 취소 후 새 Goal 발행");
                node->cancelGoal();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            int captured_sock = sock;
            node->sendGoal(dest,
                [&node, &sock_mutex, &current_sock, captured_sock, dest](bool success) {
                    if (!success) return;
                    std::lock_guard<std::mutex> lock(sock_mutex);
                    int active_sock = current_sock.load();
                    if (active_sock < 0) return;
                    RCLCPP_INFO(node->get_logger(), "도착 → ARRIVED:%s 전송", dest.c_str());
                    sendMsg(active_sock, "ARRIVED:" + dest);
                });
        }

        RCLCPP_WARN(node->get_logger(), "서버 연결 끊김. 3초 후 재연결...");
        { std::lock_guard<std::mutex> lock(sock_mutex); current_sock = -1; }
        close(sock);
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    spin_thread.join();
    rclcpp::shutdown();
    return 0;
}
