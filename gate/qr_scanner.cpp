#include "qr_scanner.h"
#include <opencv2/opencv.hpp>
#include <iostream>

std::string scan_qr() {
    // cv::VideoCapture cap(0);
    // if (!cap.isOpened()) {
    //     std::cerr << "[오류] 카메라 열기 실패" << std::endl;
    //     return "";
    // }

    cv::VideoCapture cap(0, cv::CAP_V4L2);  // GStreamer 대신 V4L2 직접 사용
    if (!cap.isOpened()) {
        std::cerr << "[오류] 카메라 열기 실패" << std::endl;
        return "";
    }
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));

    cv::QRCodeDetector detector;
    cv::Mat frame;
    std::vector<cv::Point> points;

    std::cout << "[대기] QR 코드를 카메라에 보여주세요..." << std::endl;

    while (true) {
        cap >> frame;
        if (frame.empty()) continue;

        try {
            std::string data;
            bool detected = detector.detect(frame, points);
            if (detected && !points.empty()) {
                data = detector.decode(frame, points);
            }

            if (!data.empty()) {
                std::cout << "[QR 인식] " << data << std::endl;
                cap.release();
                return data;  // ticket_id 반환
            }
        } catch (const cv::Exception &) {
            continue;
        }
    }

    cap.release();
    return "";
}
