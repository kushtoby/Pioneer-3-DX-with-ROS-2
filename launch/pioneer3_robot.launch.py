from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
import os

def generate_launch_description():
    this_dir = os.path.dirname(__file__)

    use_oak  = LaunchConfiguration('use_oak')
    use_rear = LaunchConfiguration('use_rear')

    bringup = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(os.path.join(this_dir, 'pioneer3_bringup.launch.py'))
    )

    sensors = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(os.path.join(this_dir, 'pioneer3_sensors.launch.py')),
        launch_arguments={
            'use_oak': use_oak,
            'use_rear': use_rear,
        }.items()
    )

    return LaunchDescription([
        DeclareLaunchArgument('use_oak', default_value='true'),
        DeclareLaunchArgument('use_rear', default_value='true'),
        bringup,
        sensors,
    ])
