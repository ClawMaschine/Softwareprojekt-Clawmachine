from dataclasses import dataclass

from .mqtt_esp import MQTTEsp
from ..mqtt import MQTTClient


@dataclass
class MQTTEspController:
    steering_esp: MQTTEsp
    arm_esp: MQTTEsp

    def __init__(
        self,
        mqtt_client: MQTTClient,
        steering_topic: str = "clawmachine/device/steering/command",
        arm_topic: str = "clawmachine/device/arm/command",
    ):
        self.steering_esp = MQTTEsp(mqtt_client, steering_topic)
        self.arm_esp = MQTTEsp(mqtt_client, arm_topic)

    def get_connected_esps(self):
        return [self.steering_esp, self.arm_esp]

    def send_steering_command(self, command: str):
        return self.steering_esp.send_command(command)

    def send_arm_command(self, command: str):
        return self.arm_esp.send_command(command)


mqtt_esp_controller = MQTTEspController
