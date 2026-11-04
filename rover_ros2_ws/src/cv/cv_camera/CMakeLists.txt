cmake_minimum_required(VERSION 3.8)

project(cv_camera)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(OpenCV REQUIRED)

add_executable(
    webcam_node
    src/webcam_node.cpp
)

ament_target_dependencies(
    webcam_node
    rclcpp
    sensor_msgs
    cv_bridge
)

target_include_directories(
    webcam_node PRIVATE
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(
    webcam_node
    ${OpenCV_LIBS}
)

install(
    TARGETS webcam_node
    DESTINATION lib/${PROJECT_NAME}
)

ament_package()