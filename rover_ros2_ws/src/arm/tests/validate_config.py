#!/usr/bin/env python3
"""Validate the source bundle without starting ROS nodes (requires xacro and PyYAML)."""
import ast
from collections import Counter
import math
import os
from pathlib import Path
import tempfile
import xml.etree.ElementTree as ET

import xacro
import yaml

ROOT = Path(__file__).resolve().parents[1]


class UniqueLoader(yaml.SafeLoader):
    pass


def unique_mapping(loader, node, deep=False):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        assert key not in result, f'Duplicate YAML key: {key}'
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, unique_mapping)


def load(relative):
    return yaml.load((ROOT / relative).read_text(), Loader=UniqueLoader)


def group_joints(group, joints):
    result = {joint.get('name') for joint in group.findall('joint')}
    by_child = {joint.find('child').get('link'): joint for joint in joints.values()}
    for chain in group.findall('chain'):
        link = chain.get('tip_link')
        while link != chain.get('base_link'):
            joint = by_child[link]
            if joint.get('type') != 'fixed':
                result.add(joint.get('name'))
            link = joint.find('parent').get('link')
    return result


def validate(model):
    link_names = [link.get('name') for link in model.findall('link')]
    joint_names = [joint.get('name') for joint in model.findall('joint')]
    assert len(link_names) == len(set(link_names)), 'Duplicate link'
    assert len(joint_names) == len(set(joint_names)), 'Duplicate joint'
    joints = {joint.get('name'): joint for joint in model.findall('joint')}
    active = {name for name, joint in joints.items() if joint.get('type') != 'fixed'}
    children = [joint.find('child').get('link') for joint in joints.values()]
    assert len(children) == len(set(children)), 'Multiple parents'
    assert set(link_names) - set(children) == {'world'}, 'Expected one world root'
    seen = {'world'}
    while True:
        connected = {joint.find('child').get('link') for joint in joints.values()
                     if joint.find('parent').get('link') in seen}
        if connected <= seen:
            break
        seen |= connected
    assert seen == set(link_names), 'Disconnected or cyclic tree'
    for name in active:
        joint = joints[name]
        limits = joint.find('limit')
        assert float(limits.get('velocity')) > 0 and float(limits.get('effort')) > 0, name
        if joint.get('type') == 'revolute':
            assert math.isclose(float(limits.get('lower')), -math.pi, abs_tol=1e-9), name
            assert math.isclose(float(limits.get('upper')), math.pi, abs_tol=1e-9), name
        elif joint.get('type') == 'prismatic':
            assert float(limits.get('lower')) < float(limits.get('upper')), name
    for mesh in model.findall('.//mesh'):
        filename = mesh.get('filename')
        assert filename.startswith('package://'), filename
        assert (ROOT / filename.removeprefix('package://')).is_file(), filename
    system, = model.findall('ros2_control')
    controls = system.findall('joint')
    assert {joint.get('name') for joint in controls} == active and len(controls) == len(active) == 8
    initial = load('arm_control_config/config/initial_positions.yaml')['initial_positions']
    assert set(initial) == active
    for joint in controls:
        name = joint.get('name')
        assert [item.get('name') for item in joint.findall('command_interface')] == ['position']
        assert {item.get('name') for item in joint.findall('state_interface')} == {'position', 'velocity'}
        assert float(joint.find("state_interface[@name='position']/param").text) == initial[name]
        if joints[name].get('type') != 'continuous':
            limit = joints[name].find('limit')
            assert float(limit.get('lower')) <= initial[name] <= float(limit.get('upper'))

    controllers = load('arm_control_config/config/ros2_controllers.yaml')
    mapping = load('moveit_config/config/moveit_controllers.yaml')['moveit_simple_controller_manager']
    manager = controllers['controller_manager']['ros__parameters']
    limits = load('moveit_config/config/joint_limits.yaml')['joint_limits']
    assert set(limits) == active
    claims = []
    for name in mapping['controller_names']:
        configured = controllers[name]['ros__parameters']
        assert manager[name]['type'] == 'joint_trajectory_controller/JointTrajectoryController'
        assert configured['command_interfaces'] == ['position']
        assert set(configured['joints']) == set(mapping[name]['joints'])
        assert mapping[name]['type'] == 'FollowJointTrajectory'
        assert mapping[name]['action_ns'] == 'follow_joint_trajectory'
        claims += configured['joints']
    assert Counter(claims) == Counter({joint: 1 for joint in active}), 'Coverage or ownership mismatch'
    srdf = ET.parse(ROOT / 'moveit_config/config/arm.srdf').getroot()
    groups = {group.get('name'): group_joints(group, joints) for group in srdf.findall('group')}
    expected = {'arm': 'arm_controller', 'Gripper': 'gripper_controller', 'Solenoid': 'solenoid_controller'}
    assert set(groups) == set(expected)
    ompl = load('moveit_config/config/ompl_planning.yaml')
    for group, controller in expected.items():
        assert groups[group] == set(mapping[controller]['joints']), group
        assert ompl[group]['default_planner_config'] in ompl['planner_configs'], group
    for state in srdf.findall('group_state'):
        assert {joint.get('name') for joint in state.findall('joint')} == groups[state.get('group')]
        for joint in state.findall('joint'):
            name, value = joint.get('name'), float(joint.get('value'))
            if joints[name].get('type') != 'continuous':
                limit = joints[name].find('limit')
                assert float(limit.get('lower')) <= value <= float(limit.get('upper'))
    for collision in srdf.findall('disable_collisions'):
        assert {collision.get('link1'), collision.get('link2')} <= set(link_names)
    assert set(load('moveit_config/config/kinematics.yaml')) == {'arm'}
    for name, limit in limits.items():
        assert limit['has_velocity_limits'] and limit['has_acceleration_limits']
        assert 0 < limit['max_velocity'] <= float(joints[name].find('limit').get('velocity'))
        assert limit['max_acceleration'] > 0
    servo = load('arm_gamepad_teleop/config/servo.yaml')
    assert servo['move_group_name'] == 'arm'
    assert servo['command_out_topic'] == '/arm_controller/joint_trajectory'
    for file in (ROOT / 'arm_gamepad_teleop/config').glob('teleop_*.yaml'):
        values = load(file.relative_to(ROOT))['arm_teleop']['ros__parameters']
        assert set(values['joint_names']) == groups['arm'], file


def main():
    packages = {ET.parse(file).getroot().findtext('name'): file.parent for file in ROOT.glob('*/package.xml')}
    for file in ROOT.rglob('*.yaml'):
        load(file.relative_to(ROOT))
    for file in ROOT.rglob('*.py'):
        ast.parse(file.read_text(), filename=str(file))
    dependencies = {}
    for name, path in packages.items():
        manifest = ET.parse(path / 'package.xml').getroot()
        dependencies[name] = {element.text for element in manifest if 'depend' in element.tag} & packages.keys()

    def visit(name, stack):
        assert name not in stack, f'Package dependency cycle: {stack + [name]}'
        for dependency in dependencies[name]:
            visit(dependency, stack + [name])

    for name in packages:
        visit(name, [])
    # Use a temporary ament index so $(find ...) resolves the source bundle, not another workspace.
    with tempfile.TemporaryDirectory(prefix='arm-validation-') as directory:
        prefix = Path(directory)
        index = prefix / 'share/ament_index/resource_index/packages'
        index.mkdir(parents=True)
        for name, path in packages.items():
            (index / name).touch()
            (prefix / 'share' / name).symlink_to(path, target_is_directory=True)
        os.environ['AMENT_PREFIX_PATH'] = directory + os.pathsep + os.environ.get('AMENT_PREFIX_PATH', '')
        for backend in ('mock', 'gazebo'):
            xml = xacro.process_file(str(ROOT / 'moveit_config/config/arm.urdf.xacro'),
                                     mappings={'use_gazebo': str(backend == 'gazebo').lower()}).toxml()
            model = ET.fromstring(xml)
            validate(model)
            expected = 'gz_ros2_control/GazeboSimSystem' if backend == 'gazebo' else 'mock_components/GenericSystem'
            assert model.findtext('ros2_control/hardware/plugin') == expected
            assert len(model.findall('gazebo/plugin')) == (1 if backend == 'gazebo' else 0)
            print(f'PASS: {backend} Xacro, URDF tree, meshes, limits, all eight interfaces, three groups/controllers.')
    print('PASS: YAML uniqueness, Python syntax, package dependency graph, and existing gamepad/Servo mappings.')


if __name__ == '__main__':
    main()
