cmake_minimum_required(VERSION 3.8)
project(arm)

find_package(ament_cmake REQUIRED)

install(
  DIRECTORY
    launch
    description
    config
    meshes
  DESTINATION share/${PROJECT_NAME}
)

# Install saved RViz configurations if the package has a separate rviz folder.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/rviz")
  install(DIRECTORY rviz DESTINATION share/${PROJECT_NAME})
endif()

ament_package()
