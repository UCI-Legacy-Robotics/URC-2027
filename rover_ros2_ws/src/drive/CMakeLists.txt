cmake_minimum_required(VERSION 3.8)
project(drive_description)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)

add_executable(arcade arcade.cpp)
target_compile_features(arcade PUBLIC cxx_std_17)
ament_target_dependencies(arcade rclcpp geometry_msgs sensor_msgs)
install(TARGETS arcade DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY description config launch DESTINATION share/${PROJECT_NAME})
ament_package()
