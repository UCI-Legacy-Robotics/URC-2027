cmake_minimum_required(VERSION 3.8)
project(video_stream_manager)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)

add_library(stream_manager SHARED
  src/pipeline.cpp
  src/stream_manager.cpp)
target_compile_features(stream_manager PUBLIC cxx_std_17)
target_link_libraries(stream_manager
  rclcpp::rclcpp
  rclcpp_components::component
  ${sensor_msgs_TARGETS}
  PkgConfig::GST)

# Loadable into a component container, and runnable on its own as stream_manager_node.
rclcpp_components_register_node(stream_manager
  PLUGIN "video_stream_manager::StreamManager"
  EXECUTABLE stream_manager_node)

install(TARGETS stream_manager
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_pipeline test/test_pipeline.cpp src/pipeline.cpp)
  target_include_directories(test_pipeline PRIVATE src)
endif()

ament_package()
