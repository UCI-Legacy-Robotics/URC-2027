cmake_minimum_required(VERSION 3.10)
project(camera_capture)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)
find_package(pano_interfaces REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgcodecs)

# ── Custom services ───────────────────────────────────────────────────────────


# ── Node executable ───────────────────────────────────────────────────────────
add_executable(camera_capture_node
  src/camera_capture_node.cpp
)

target_include_directories(camera_capture_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

ament_target_dependencies(camera_capture_node
  rclcpp
  sensor_msgs
  cv_bridge
  image_transport
  pano_interfaces
)

target_link_libraries(camera_capture_node
  ${OpenCV_LIBS}
)

install(TARGETS camera_capture_node
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()