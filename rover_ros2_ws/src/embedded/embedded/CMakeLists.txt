cmake_minimum_required(VERSION 3.8)
project(embedded)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(Boost REQUIRED COMPONENTS thread)

include_directories(include)

# 1. CORE LIBRARY (SocketCAN Manager & Abstract Device Infrastructure)
add_library(can_manager_core SHARED
  src/core/can_device.cpp
  src/core/can_manager.cpp
)
target_link_libraries(can_manager_core
  PUBLIC
    Boost::thread
)

# 2. CONCRETE DEVICE DRIVERS (Motor Implementations)
add_library(can_devices SHARED
  src/devices/my_motor_device.cpp
)
target_link_libraries(can_devices
  PUBLIC
    can_manager_core
)

# INSTALLATION AND EXPORTS
install(
  TARGETS can_manager_core can_devices
  EXPORT export_embedded
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(
  DIRECTORY include/
  DESTINATION include
)

ament_export_targets(export_embedded HAS_LIBRARY_TARGETS)
ament_export_include_directories(include)
ament_export_dependencies(Boost)

ament_package()