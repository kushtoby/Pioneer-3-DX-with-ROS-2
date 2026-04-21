from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
import os


def generate_launch_description():
    this_dir = os.path.dirname(__file__)

    bringup = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(os.path.join(this_dir, 'pioneer3_bringup.launch.py'))
    )

    sensors = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(os.path.join(this_dir, 'pioneer3_sensors.launch.py'))
    )

    return LaunchDescription([
        bringup,
        sensors,
    ])
