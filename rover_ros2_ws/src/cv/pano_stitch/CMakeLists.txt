cmake_minimum_required(VERSION 3.8)
project(pano_stitch)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── ROS2 dependencies ─────────────────────────────────────────────────────────
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)

# ── OpenCV ────────────────────────────────────────────────────────────────────
find_package(OpenCV REQUIRED
  COMPONENTS
    core
    highgui
    imgproc
    imgcodecs
    videoio
    features2d
    calib3d
    flann
    xfeatures2d   # SIFT lives here in opencv_contrib
)

# ── Node executable ───────────────────────────────────────────────────────────
add_executable(pano_stitch_node
  src/pano_stitch_node.cpp
  src/stitcher.cpp
)

target_include_directories(pano_stitch_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>
)

ament_target_dependencies(pano_stitch_node
  rclcpp
  std_msgs
  std_srvs
  sensor_msgs
  cv_bridge
  image_transport
)

target_link_libraries(pano_stitch_node
  ${OpenCV_LIBS}
)

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS pano_stitch_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include
)

# ── Testing ───────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()