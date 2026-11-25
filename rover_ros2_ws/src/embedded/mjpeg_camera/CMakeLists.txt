cmake_minimum_required(VERSION 3.8)
project(mjpeg_camera)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0)

add_library(mjpeg_camera SHARED
  src/pipeline.cpp
  src/mjpeg_camera.cpp)
target_compile_features(mjpeg_camera PUBLIC cxx_std_17)
target_link_libraries(mjpeg_camera
  rclcpp::rclcpp
  rclcpp_components::component
  ${sensor_msgs_TARGETS}
  PkgConfig::GST)

# Loadable into a component container, and runnable on its own as mjpeg_camera_node.
rclcpp_components_register_node(mjpeg_camera
  PLUGIN "mjpeg_camera::MjpegCamera"
  EXECUTABLE mjpeg_camera_node)

install(TARGETS mjpeg_camera
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_pipeline test/test_pipeline.cpp src/pipeline.cpp)
  target_include_directories(test_pipeline PRIVATE src)
endif()

ament_package()
