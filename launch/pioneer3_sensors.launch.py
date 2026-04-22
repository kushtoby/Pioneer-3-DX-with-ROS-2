from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    # Args
    use_oak = LaunchConfiguration('use_oak')
    use_rear = LaunchConfiguration('use_rear')
    rear_device = LaunchConfiguration('rear_device')
    # rear_width = LaunchConfiguration('rear_width')
    # rear_height = LaunchConfiguration('rear_height')
    rear_format = LaunchConfiguration('rear_format')

    declare = [
        DeclareLaunchArgument('use_oak', default_value='true'),
        DeclareLaunchArgument('use_rear', default_value='true'),
	DeclareLaunchArgument(
	    'rear_device',
	    default_value='/dev/v4l/by-id/usb-046d_HD_Pro_Webcam_C920_41F47D8F-video-index0'
	),
	# DeclareLaunchArgument('rear_width', default_value='640'),
        # DeclareLaunchArgument('rear_height', default_value='480'),
        DeclareLaunchArgument('rear_format', default_value='YUYV'),
    ]


    # Front camera (OAK) via launch include
    depthai_share = get_package_share_directory('depthai_ros_driver')
    depthai_launch_path = os.path.join(depthai_share, 'launch', 'camera.launch.py')
    oak_launch = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(depthai_launch_path),
        condition=IfCondition(use_oak),
    )

    # Rear camera (USB webcam) via v4l2_camera node
    rear_cam = Node(
        package='v4l2_camera',
        executable='v4l2_camera_node',
        name='rear_camera',
        output='screen',
        condition=IfCondition(use_rear),
	parameters=[
            {'video_device': rear_device},
            {'image_size': [320, 240]},
            {'pixel_format': rear_format},
        ],
        remappings=[
            ('image_raw', '/rear/image_raw'),
            ('camera_info', '/rear/camera_info'),
        ],
    )

    return LaunchDescription(declare + [
        oak_launch,
        rear_cam,
    ])
