cmake_minimum_required(VERSION 3.8)

project(cv_pose)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(OpenCV REQUIRED)

set(ONNXRUNTIME_ROOT "/opt/onnxruntime")

find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    PATHS "${ONNXRUNTIME_ROOT}/include"
    REQUIRED
)

find_library(ONNXRUNTIME_LIBRARY onnxruntime
    PATHS "${ONNXRUNTIME_ROOT}/lib"
    REQUIRED
)

add_executable(
    pose_node
    src/pose_node.cpp
)

ament_target_dependencies(
    pose_node
    rclcpp
    sensor_msgs
    cv_bridge
)

target_include_directories(
    pose_node PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${ONNXRUNTIME_INCLUDE_DIR}
)

target_link_libraries(
    pose_node
    ${OpenCV_LIBS}
    ${ONNXRUNTIME_LIBRARY}
)

set_target_properties(pose_node PROPERTIES
    BUILD_RPATH "/opt/onnxruntime/lib"
    INSTALL_RPATH "/opt/onnxruntime/lib"
)

install(
    TARGETS pose_node
    DESTINATION lib/${PROJECT_NAME}
)

install(
    DIRECTORY models/
    DESTINATION share/${PROJECT_NAME}/models
)

ament_package()