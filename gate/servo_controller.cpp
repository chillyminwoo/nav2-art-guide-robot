#include "servo_controller.h"
#include <lgpio.h>
#include <iostream>
#include <unistd.h>

static int gh = -1;

bool servo_init() {
    gh = lgGpiochipOpen(0);
    if (gh < 0) {
        std::cerr << "[오류] lgpio 초기화 실패: " << gh << std::endl;
        return false;
    }
    lgGpioClaimOutput(gh, 0, SERVO_PIN, 0);
    std::cout << "[서보] 초기화 완료 (GPIO " << SERVO_PIN << ")" << std::endl;
    return true;
}

void servo_open() {
    std::cout << "[서보] 문 열기 (수직)" << std::endl;
    lgTxServo(gh, SERVO_PIN, SERVO_OPEN_US, 50, 0, 0);
    sleep(1);
}

void servo_close() {
    std::cout << "[서보] 문 닫기 (수평)" << std::endl;
    lgTxServo(gh, SERVO_PIN, SERVO_CLOSE_US, 50, 0, 0);
    sleep(1);
}

void servo_cleanup() {
    lgTxServo(gh, SERVO_PIN, 0, 50, 0, 0);  // PWM 정지
    lgGpiochipClose(gh);
}
