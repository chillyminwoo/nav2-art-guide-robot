#pragma once
#include <string>

// QR 코드를 스캔하여 ticket_id 문자열 반환
// 인식 실패 시 빈 문자열 반환
std::string scan_qr();
