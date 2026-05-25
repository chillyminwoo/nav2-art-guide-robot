#pragma once

#define SERVO_PIN      18

// MG996R 기준 (수평=0도, 수직=90도)

#define SERVO_CLOSE_US 700   // 수평
#define SERVO_OPEN_US  1700  // 수직

bool servo_init();
void servo_open();
void servo_close();
void servo_cleanup();
