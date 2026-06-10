from dataclasses import dataclass

from ..mqtt import MQTTClient


@dataclass
class MQTTEsp:

    def __init__(self, mqtt_client: MQTTClient, mqtt_topic: str = "default"):
        self.mqtt_client = mqtt_client
        self.mqtt_topic = mqtt_topic

    def send_command(self, command: str):
        return self.mqtt_client.publish(self.mqtt_topic, command)
