cmake_minimum_required(VERSION 3.8)

project(cv_classifier)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(cv_interfaces REQUIRED)

set(ONNXRUNTIME_ROOT "/opt/onnxruntime")

find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    PATHS "${ONNXRUNTIME_ROOT}/include"
    REQUIRED
)

find_library(ONNXRUNTIME_LIBRARY onnxruntime
    PATHS "${ONNXRUNTIME_ROOT}/lib"
    REQUIRED
)

# C++ preprocessing lives next to preprocess.py in cv_gesture so the two stay side by side
set(PREPROCESS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cv_gesture/cv_gesture")

add_library(
    gesture_preprocess STATIC
    ${PREPROCESS_DIR}/preprocess.cpp
)

target_include_directories(
    gesture_preprocess PUBLIC
    ${PREPROCESS_DIR}
)

add_executable(
    classifier_node
    src/classifier_node.cpp
)

ament_target_dependencies(
    classifier_node
    rclcpp
    ament_index_cpp
    cv_interfaces
)

target_include_directories(
    classifier_node PRIVATE
    ${ONNXRUNTIME_INCLUDE_DIR}
)

target_link_libraries(
    classifier_node
    gesture_preprocess
    ${ONNXRUNTIME_LIBRARY}
)

set_target_properties(classifier_node PROPERTIES
    BUILD_RPATH "/opt/onnxruntime/lib"
    INSTALL_RPATH "/opt/onnxruntime/lib"
)

install(
    TARGETS classifier_node
    DESTINATION lib/${PROJECT_NAME}
)

install(
    DIRECTORY models/
    DESTINATION share/${PROJECT_NAME}/models
)

if(BUILD_TESTING)
    find_package(ament_cmake_gtest REQUIRED)

    # checks preprocess.cpp gives the same numbers as preprocess.py
    # (cases made by cv_gesture/test/gen_parity_cases.py)
    ament_add_gtest(
        test_preprocess_parity
        test/test_preprocess_parity.cpp
    )

    target_link_libraries(
        test_preprocess_parity
        gesture_preprocess
    )

    target_compile_definitions(
        test_preprocess_parity PRIVATE
        PARITY_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../cv_gesture/test/data/parity_cases.txt"
    )
endif()

ament_package()
