cmake_minimum_required(VERSION 3.16)
project(nav_client)

set(CMAKE_CXX_STANDARD 17)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav2_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)

add_executable(nav_client nav_client.cpp)
ament_target_dependencies(nav_client
    rclcpp
    rclcpp_action
    geometry_msgs
    nav2_msgs
    tf2
    tf2_ros
    tf2_geometry_msgs
)

install(TARGETS nav_client
    DESTINATION lib/${PROJECT_NAME}
)

ament_package()