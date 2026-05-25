cmake_minimum_required(VERSION 3.16)
project(museum_robot_ui)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

add_executable(museum_robot_ui
    main.cpp
    mainwindow.cpp
    mainwindow.h
    pages/qrpage.cpp
    pages/qrpage.h
    pages/standbypage.cpp
    pages/standbypage.h
    pages/destinationpage.cpp
    pages/destinationpage.h
    pages/navigatingpage.cpp
    pages/navigatingpage.h
    pages/artworkpage.cpp
    pages/artworkpage.h
    pages/navigationmanager.cpp
    pages/navigationmanager.h
)

target_link_libraries(museum_robot_ui
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)