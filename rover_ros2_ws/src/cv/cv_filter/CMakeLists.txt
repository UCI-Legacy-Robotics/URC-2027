cmake_minimum_required(VERSION 3.8)

project(cv_filter)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(cv_interfaces REQUIRED)

add_executable(
    filter_node
    src/filter_node.cpp
)

ament_target_dependencies(
    filter_node
    rclcpp
    cv_interfaces
)

install(
    TARGETS filter_node
    DESTINATION lib/${PROJECT_NAME}
)

ament_package()
