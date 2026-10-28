# Jazzy sub-6-DOF SVD patch v1

Minimal runtime distribution of the previously supplied patched Servo 2.12.4.
Upstream base: moveit/moveit2 commit 1ade0e9dcf50dbbbc3a984b995c786cf12736235.
Runtime C++ source and .hpp headers are unchanged from the working bundle.
This marker is required by the custom gamepad launch.

Removed: demos, demo launches/configs, tests, legacy .h forwarding headers,
and upstream documentation. CMake and package metadata match this distribution.
Keep this file and config/servo_parameters.yaml.

The original patched function passed 35 numerical checks. This minimal package
has been checked for missing local source/header references, but full ROS
compilation is not available in the preparation environment.
