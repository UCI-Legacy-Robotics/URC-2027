from setuptools import find_packages, setup

setup(
    name="science_sequence",
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages", ["resource/science_sequence"]),
        ("share/science_sequence", ["package.xml"]),
    ],
    install_requires=["setuptools"],
    python_requires=">=3.10",
    zip_safe=True,
    maintainer="Legacy Robotics",
    maintainer_email="TODO@todo.com",
    description="Bradford and NPK science sequence controller",
    license="BSD-3-Clause",
    entry_points={"console_scripts": [
        "sequencer = science_sequence.node:main",
    ]},
)
