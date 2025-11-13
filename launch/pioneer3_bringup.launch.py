from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        # Pioneer base driver (from pioneer_ros2)
        Node(
            package="pioneer_aria",
            executable="pioneer_aria_node",  # check this name with `ros2 pkg executables pioneer_aria`
            name="pioneer_base",
            parameters=[{
                "port": "/dev/ttyS0",   # or the correct serial/TCP config for your robot
                "baud": 9600,
            }],
            output="screen",
        ),

        # C++ base controller in this package
        Node(
            package="pioneer3",
            executable="base_controller",
            name="base_controller",
            output="screen",
        ),

        # Python lidar node in this package
        Node(
            package="pioneer3",
            executable="lidar_node.py",  # because we installed it as a script
            name="lidar_node",
            output="screen",
        ),
    ])
