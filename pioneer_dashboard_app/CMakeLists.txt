cmake_minimum_required(VERSION 3.8)
project(pioneer_dashboard_app)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(cv_bridge REQUIRED)

# FORCE system OpenCV4 (Ubuntu 24.04)
set(OpenCV_DIR "/usr/lib/x86_64-linux-gnu/cmake/opencv4")
find_package(OpenCV REQUIRED COMPONENTS core imgproc)

find_package(std_srvs REQUIRED)

set(CMAKE_AUTOMOC ON)
find_package(Qt5 REQUIRED COMPONENTS Widgets)

add_executable(pioneer_dashboard_app
  src/main.cpp
  src/dashboard_window.cpp
  src/lidar_widget.cpp
  include/pioneer_dashboard_app/dashboard_window.hpp
  include/pioneer_dashboard_app/lidar_widget.hpp
)

target_include_directories(pioneer_dashboard_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

ament_target_dependencies(pioneer_dashboard_app
  rclcpp
  sensor_msgs
  geometry_msgs
  cv_bridge
  std_srvs
)

target_link_libraries(pioneer_dashboard_app
  Qt5::Widgets
  opencv_core
  opencv_imgproc
)

install(TARGETS pioneer_dashboard_app
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
