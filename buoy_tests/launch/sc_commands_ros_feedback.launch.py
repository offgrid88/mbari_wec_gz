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

import unittest

import launch
import launch.actions
from launch.actions import OpaqueFunction

from launch_ros.actions import Node

import launch_testing
import launch_testing.actions

from testing_utils import regenerate_models


PHYSICS_STEP = 0.01


def generate_test_description():

    # Test fixture
    gazebo_test_fixture = Node(
        package='buoy_tests',
        executable='sc_commands_ros_feedback',
        output='screen',
        parameters=[dict(physics_step=PHYSICS_STEP)],
        on_exit=launch.actions.Shutdown()
    )

    bridge = Node(package='ros_gz_bridge',
                  executable='parameter_bridge',
                  arguments=['/clock@rosgraph_msgs/msg/Clock[gz.msgs.Clock'],
                  output='screen')

    nodes = [gazebo_test_fixture,
             bridge]
    sim_params = dict(inc_wave_spectrum='inc_wave_spectrum_type:None',
                      physics_rtf=11.0,
                      physics_step=PHYSICS_STEP,
                      initial_piston_position=2.03,
                      initial_buoy_height=2.0)

    return launch.LaunchDescription([
        OpaqueFunction(function=regenerate_models,
                       args=nodes,
                       kwargs=sim_params),
        launch_testing.util.KeepAliveProc(),
        launch_testing.actions.ReadyToTest()
    ]), locals()


class SCCommandsROSTest(unittest.TestCase):

    def test_termination(self, gazebo_test_fixture, proc_info):
        proc_info.assertWaitForShutdown(process=gazebo_test_fixture, timeout=600)


@launch_testing.post_shutdown_test()
class SCCommandsROSTestAfterShutdown(unittest.TestCase):

    def test_exit_code(self, gazebo_test_fixture, proc_info):
        launch_testing.asserts.assertExitCodes(
            proc_info,
            [launch_testing.asserts.EXIT_OK],
            gazebo_test_fixture
        )
