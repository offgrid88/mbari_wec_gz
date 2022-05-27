# Copyright 2022 Open Source Robotics Foundation, Inc. and Monterey Bay Aquarium Research Institute
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os

from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node


def generate_launch_description():
    
    pkg_ros_ign_gazebo = get_package_share_directory('ros_ign_gazebo')
    pkg_buoy_gazebo = get_package_share_directory('buoy_gazebo')
    sdf_file = os.path.join(pkg_buoy_gazebo, 'worlds', 'test_mbari_wec_model.sdf')

    # pkg_buoy_description = get_package_share_directory('buoy_description')
    # sdf_file = os.path.join(pkg_buoy_description, 'models', 'mbari_wec', 'model.sdf')
    
    with open(sdf_file, 'r') as infp:
        robot_desc = infp.read()

    gazebo = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(pkg_ros_ign_gazebo, 'launch', 'ign_gazebo.launch.py'),
        ),
        launch_arguments={'ign_args': '-r empty.sdf'}.items(),

    )

    return LaunchDescription([
        gazebo,
        Node(
            package='ros_ign_gazebo',
            executable='create',
            arguments=[
                '-name', 'test_mbari_wec_model',
                '-topic', 'robot_description',
            ],
            output='screen',
        ),
        # TODO(quarkytale): Launch a bridge to forward tf and joint states to ros2
        Node(
            package='ros_ign_bridge',
            executable='parameter_bridge',
            arguments=[
                # Clock (IGN -> ROS2)
                '/clock@rosgraph_msgs/msg/Clock[ignition.msgs.Clock',
                # Joint states (IGN -> ROS2)
                '/world/empty/model/test_mbari_wec_model/joint_state@sensor_msgs/msg/JointState[ignition.msgs.Model',
            ],
            remappings=[
                ('/world/empty/model/test_mbari_wec_model/joint_state', 'joint_states'),
            ],
            output='screen'
        ),
        # Get the parser plugin convert sdf to urdf using robot_description topic
        Node(
            package='robot_state_publisher',
            executable='robot_state_publisher',
            name='robot_state_publisher',
            output='both',
            parameters=[
                {'use_sim_time': False},
                {'robot_description': robot_desc},
            ],
        ),
        # Launch rviz
        Node(
            package='rviz2',
            executable='rviz2'
        )
    ])
