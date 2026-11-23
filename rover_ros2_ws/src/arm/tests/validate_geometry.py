#!/usr/bin/env python3
"""Optional mesh checks: pip install trimesh python-fcl numpy scipy PyYAML.

Checks sampled configurations using the supplied SRDF collision exemptions.
This is not a replacement for live MoveIt/Gazebo testing or swept-volume checking.
"""
from pathlib import Path
import xml.etree.ElementTree as ET

import numpy as np
import trimesh
from trimesh.collision import CollisionManager
from trimesh.transformations import euler_matrix, rotation_matrix, translation_matrix
import yaml

ROOT = Path(__file__).resolve().parents[1]


def origin(element):
    if element is None:
        return np.eye(4)
    result = euler_matrix(*map(float, element.get('rpy', '0 0 0').split()))
    result[:3, 3] = list(map(float, element.get('xyz', '0 0 0').split()))
    return result


def main():
    model = ET.parse(ROOT / 'arm/description/arm.urdf').getroot()
    semantic = ET.parse(ROOT / 'moveit_config/config/arm.srdf').getroot()
    excluded = {frozenset((pair.get('link1'), pair.get('link2')))
                for pair in semantic.findall('disable_collisions')}
    manager, meshes = CollisionManager(), {}
    for link in model.findall('link'):
        name = link.get('name')
        inertia = link.find('inertial/inertia')
        if inertia is not None:
            matrix = np.array([[float(inertia.get('i' + a + b if a <= b else 'i' + b + a))
                                for b in 'xyz'] for a in 'xyz'])
            assert np.all(np.linalg.eigvalsh(matrix) > 0), f'{name}: invalid inertia'
        for collision in link.findall('collision'):
            source = collision.find('geometry/mesh')
            mesh = trimesh.load(str(ROOT / source.get('filename').removeprefix('package://')), force='mesh')
            mesh.apply_scale(list(map(float, source.get('scale', '1 1 1').split())))
            mesh.apply_transform(origin(collision.find('origin')))
            meshes[name] = mesh
            manager.add_object(name, mesh)

    def check(values):
        transforms, remaining = {'world': np.eye(4)}, model.findall('joint')
        while remaining:
            progressed = False
            for joint in remaining[:]:
                parent, child = joint.find('parent').get('link'), joint.find('child').get('link')
                if parent not in transforms:
                    continue
                transform = origin(joint.find('origin'))
                if joint.get('type') != 'fixed':
                    axis = np.array(list(map(float, joint.find('axis').get('xyz').split())))
                    axis /= np.linalg.norm(axis)
                    value = values[joint.get('name')]
                    transform = transform @ (translation_matrix(axis * value) if joint.get('type') == 'prismatic'
                                              else rotation_matrix(value, axis))
                transforms[child] = transforms[parent] @ transform
                remaining.remove(joint)
                progressed = True
            assert progressed, 'Disconnected or cyclic tree'
        for name, mesh in meshes.items():
            manager.set_transform(name, transforms[name])
            assert trimesh.transform_points(mesh.vertices, transforms[name])[:, 2].min() >= -0.0001, name
        _, pairs = manager.in_collision_internal(return_names=True)
        return {frozenset(pair) for pair in pairs} - excluded

    initial = yaml.safe_load((ROOT / 'arm_control_config/config/initial_positions.yaml').read_text())['initial_positions']
    assert not check(initial), 'Initial self-collision'
    count = 1
    for state in semantic.findall('group_state'):
        target = {**initial, **{joint.get('name'): float(joint.get('value')) for joint in state.findall('joint')}}
        for alpha in np.linspace(0, 1, 11):
            positions = {name: value + alpha * (target[name] - value) for name, value in initial.items()}
            assert not check(positions), f'Collision in {state.get("name")} at {alpha}'
            count += 1
    combined = {**initial, 'claw_to_left_grip': 0.03, 'claw_to_right_grip': 0.03, 'grip_to_solenoid': 0.015}
    assert not check(combined), 'Combined gripper/solenoid state collides'
    assert not check({**initial, 'base_mount_to_upper_base': 0.05}), 'Smoke-test arm target collides'
    full_stroke = {**initial, 'claw_to_left_grip': 0.05, 'claw_to_right_grip': 0.05}
    assert frozenset(('left_gripper', 'right_gripper')) in check(full_stroke), 'Recheck the documented full-stroke issue'
    print(f'PASS: positive inertias and {count + 2} sampled initial/named/test configurations without unexpected collisions.')
    print('CONFIRMED: both finger joints at 0.05 m collide; this is deliberately not a named target.')


if __name__ == '__main__':
    main()
