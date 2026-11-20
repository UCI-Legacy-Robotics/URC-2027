cmake_minimum_required(VERSION 3.16)
project(embedded_can)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(Threads REQUIRED)
find_package(rclcpp REQUIRED)

# ==============================================================================
# 1. CORE LIBRARY (SocketCAN Manager, Base Device, & Polymorphic TxMessages)
# ==============================================================================
add_library(CanManagerCore SHARED
  src/core/CanDevice.cpp
  src/core/CanManager.cpp
  src/messages/ImmediateMessage.cpp
  src/messages/PeriodicMessage.cpp
  src/messages/ServiceMessage.cpp
)

target_compile_features(CanManagerCore PUBLIC cxx_std_17)

target_include_directories(CanManagerCore
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(CanManagerCore
  PUBLIC
    Threads::Threads
)

# ==============================================================================
# 2. CONCRETE DEVICE DRIVERS (Inherits C++17, include paths, & Threads)
# ==============================================================================
add_library(CanDevices SHARED
  src/devices/ODriveDevice.cpp
  src/devices/YellowJacketDevice.cpp
)

target_link_libraries(CanDevices
  PUBLIC
    CanManagerCore
)

# ==============================================================================
# 3. OPTIONAL STANDALONE TEST EXECUTABLE
# ==============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dev/mock_device.cpp")
  add_executable(mock_device dev/mock_device.cpp)
  target_link_libraries(mock_device PRIVATE CanDevices)
  install(TARGETS mock_device DESTINATION lib/${PROJECT_NAME})
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dev/test_control.cpp")
  add_executable(test_control dev/test_control.cpp)
  target_link_libraries(test_control PRIVATE CanDevices)
  install(TARGETS test_control DESTINATION lib/${PROJECT_NAME})
endif()

# ==============================================================================
# 4. INSTALLATION & AMENT EXPORTS
# ==============================================================================
install(
  TARGETS CanManagerCore CanDevices
  EXPORT export_embedded_pkg
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(
  DIRECTORY include/
  DESTINATION include
)

ament_export_targets(export_embedded_pkg HAS_LIBRARY_TARGET)
ament_export_include_directories(include)
ament_export_dependencies(Threads)

ament_package()