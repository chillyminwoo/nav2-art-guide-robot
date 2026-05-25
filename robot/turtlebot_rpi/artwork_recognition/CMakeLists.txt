cmake_minimum_required(VERSION 3.10)
project(artwork_recognition)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenCV REQUIRED)
find_package(realsense2 REQUIRED)

add_executable(artwork_recognition main.cpp)

target_link_libraries(artwork_recognition
    ${OpenCV_LIBS}
    realsense2::realsense2
)

