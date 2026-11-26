cmake_minimum_required(VERSION 3.8)
project(ros2_control_drive)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)

install(DIRECTORY description config launch DESTINATION share/${PROJECT_NAME})
ament_package()