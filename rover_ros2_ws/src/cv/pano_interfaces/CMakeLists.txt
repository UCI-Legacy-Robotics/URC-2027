cmake_minimum_required(VERSION 3.10)
project(pano_interfaces)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(std_msgs REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "srv/StitchFolder.srv"
  "srv/SaveFrame.srv"
  DEPENDENCIES std_msgs
)

ament_package()