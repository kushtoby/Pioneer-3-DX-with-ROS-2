from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory

import os


def generate_launch_description():
    # LiDAR (SLLIDAR A1)
    sllidar_share_dir = get_package_share_directory('sllidar_ros2')
    sllidar_launch_path = os.path.join(
        sllidar_share_dir,
        'launch',
        'sllidar_a1_launch.py'
    )

    lidar_launch = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(sllidar_launch_path)
    )

    # Pioneer base controller node
    base_controller = Node(
        package='pioneer3',
        executable='base_controller',
        name='pioneer_base',
        output='screen',
        # pass through the ARIA argument for the serial port
        arguments=['-robotPort', '/dev/ttyS0'],
    )

    bag_recorder = Node(
        package='pioneer3',
        executable='bag_recorder',
        name='bag_recorder',
        output='screen',
        parameters=[{
            'laptop_user': 'kush',
            'laptop_ip': '192.168.1.8',
            'laptop_dir': '~/bags',
            'robot_dir': '/home/easel/bags',
            'scan_topic': '/scan',
        }]
    )

    return LaunchDescription([
        lidar_launch,
        base_controller,
        bag_recorder,
    ])
