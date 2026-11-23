from moveit_configs_utils import MoveItConfigsBuilder
from moveit_configs_utils.launches import generate_setup_assistant_launch


def generate_launch_description():
    config = (MoveItConfigsBuilder('arm', package_name='moveit_config')
              .planning_pipelines(pipelines=['ompl'], default_planning_pipeline='ompl')
              .to_moveit_configs())
    return generate_setup_assistant_launch(config)
