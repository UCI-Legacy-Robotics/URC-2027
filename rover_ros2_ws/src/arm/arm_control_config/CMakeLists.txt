cmake_minimum_required(VERSION 3.8)
project(arm_control_config)

find_package(ament_cmake REQUIRED)
find_package(hardware_interface REQUIRED)
find_package(pluginlib REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(embedded_can REQUIRED)

add_library(arm_hw_interface SHARED src/arm_hw_interface.cpp)
target_compile_features(arm_hw_interface PUBLIC cxx_std_17)
ament_target_dependencies(arm_hw_interface
  hardware_interface pluginlib rclcpp rclcpp_lifecycle)
target_link_libraries(arm_hw_interface embedded_can::CanDevices)

# Generate and register the required plugin description in the build directory.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/arm_hw_interface.xml" [=[
<library path="arm_hw_interface">
  <class name="arm_control_config/ArmHardwareInterface"
         type="arm_control_config::ArmHardwareInterface"
         base_class_type="hardware_interface::SystemInterface">
    <description>ODrive CAN hardware interface for configured arm joints.</description>
  </class>
</library>
]=])
ament_index_register_resource("hardware_interface__pluginlib__plugin"
  CONTENT "share/${PROJECT_NAME}/arm_hw_interface.xml\n")

install(TARGETS arm_hw_interface
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/arm_hw_interface.xml"
  DESTINATION share/${PROJECT_NAME})
install(DIRECTORY config models DESTINATION share/${PROJECT_NAME})

ament_package()