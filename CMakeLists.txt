cmake_minimum_required(VERSION 3.8)
project(pioneer3)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclpy REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(pioneer_msgs REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(std_srvs REQUIRED)

# ARIA
include_directories(/usr/local/Aria/include)
link_directories(/usr/local/Aria/lib)

# base_controller
add_executable(base_controller src/base_controller.cpp)
ament_target_dependencies(base_controller
  rclcpp
  geometry_msgs
  pioneer_msgs
  std_srvs
)
target_link_libraries(base_controller
  Aria
  pthread
)

# bag_recorder
add_executable(bag_recorder src/bag_recorder.cpp)
ament_target_dependencies(bag_recorder
  rclcpp
  std_srvs
)

install(TARGETS
  base_controller
  bag_recorder
  DESTINATION lib/${PROJECT_NAME}
)

# Python side
ament_python_install_package(${PROJECT_NAME})
install(PROGRAMS
  scripts/lidar_node.py
  DESTINATION lib/${PROJECT_NAME}
)

# Launch files + config
install(DIRECTORY
  launch
  config
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
