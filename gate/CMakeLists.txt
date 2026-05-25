cmake_minimum_required(VERSION 3.16)
project(gate_client)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenCV REQUIRED)

add_executable(gate_client
    qr_main.cpp
    qr_scanner.cpp
    servo_controller.cpp
)

target_include_directories(gate_client PRIVATE ${OpenCV_INCLUDE_DIRS})

target_link_libraries(gate_client
    ${OpenCV_LIBS}
    curl
    lgpio
    pthread
)
