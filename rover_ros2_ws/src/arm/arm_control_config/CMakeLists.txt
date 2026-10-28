cmake_minimum_required(VERSION 3.22)
project(arm_control_config)

find_package(ament_cmake REQUIRED)
find_package(moveit_servo REQUIRED)
find_package(control_msgs REQUIRED)

ament_package()


install(
  DIRECTORY models config
  DESTINATION share/${PROJECT_NAME}
  )


install(DIRECTORY config DESTINATION share/${PROJECT_NAME})

