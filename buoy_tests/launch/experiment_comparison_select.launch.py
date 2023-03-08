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

import launch
from launch.actions import DeclareLaunchArgument, Shutdown
from launch.actions import OpaqueFunction
from launch.conditions import IfCondition, UnlessCondition
from launch.substitutions import LaunchConfiguration

from launch_ros.actions import Node

from testing_utils import regenerate_models


def generate_launch_description():

    test_inputdata_exp = os.path.join(
        get_package_share_directory('buoy_tests'),
        'test_inputdata',
        'FrictionIDMovesWithBiasCurr.exp'
    )

    test_inputdata_tst = os.path.join(
        get_package_share_directory('buoy_tests'),
        'test_inputdata',
        'FrictionIDMovesWithBiasCurr.tst'
    )

    manual_comparison_arg = DeclareLaunchArgument(
        'manual', default_value='False',
        description='compare data manually'
    )

    # Test fixture
    gazebo_test_fixture = Node(
        package='buoy_tests',
        executable='experiment_comparison',
        output='both',
        parameters=[
            {'use_sim_time': True},
            {'inputdata_filename': test_inputdata_tst},
            {'manual_comparison': False}
        ],
        namespace='/experiment_comparison',
        condition=UnlessCondition(LaunchConfiguration('manual')),
        on_exit=Shutdown()
    )

    gazebo_test_fixture_manual = Node(
        package='buoy_tests',
        executable='experiment_comparison',
        output='both',
        parameters=[
            {'use_sim_time': True},
            {'inputdata_filename': test_inputdata_exp},
            {'manual_comparison': True}
        ],
        namespace='/experiment_comparison',
        prefix=['x-terminal-emulator -e'],
        shell=True,
        condition=IfCondition(LaunchConfiguration('manual')),
        on_exit=Shutdown()
    )

    bridge = Node(package='ros_gz_bridge',
                  executable='parameter_bridge',
                  arguments=['/clock@rosgraph_msgs/msg/Clock[gz.msgs.Clock'],
                  output='screen')

    nodes = [gazebo_test_fixture,
             gazebo_test_fixture_manual,
             bridge]
    sim_params = dict(inc_wave_spectrum='inc_wave_spectrum_type:None',
                      physics_rtf=11.0,
                      physics_step=0.001)

    return launch.LaunchDescription([
        manual_comparison_arg,
        OpaqueFunction(function=regenerate_models,
                       args=nodes,
                       kwargs=sim_params),
    ])
