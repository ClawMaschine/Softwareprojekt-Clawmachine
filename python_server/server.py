from time import sleep

try:
    from .config import load_mqtt_configuration
    from .mqtt import MQTTClient
except ImportError:
    from config import load_mqtt_configuration
    from mqtt import MQTTClient


class ClawMachine:

    def __init__(self):
        self.position = (0, 0)
        mqtt_configuration = load_mqtt_configuration()
        self.mqtt_client = MQTTClient(
            client_id=mqtt_configuration.client_id,
            broker=mqtt_configuration.broker,
            port=mqtt_configuration.port,
            connect_timeout_seconds=mqtt_configuration.connect_timeout_seconds,
        )
        self.mqtt_client.connect()
        sleep(0.2)
        self.mqtt_client.publish(mqtt_configuration.topic, "open")

    def move_to(self, x, y):
        self.position = (x, y)
        print(f"Moved to position: {self.position}")

    def open_claw(self):
        self.is_claw_open = True
        print("Claw opened")

    def close_claw(self):
        self.is_claw_open = False
        print("Claw closed")


def run():
    return ClawMachine()


if __name__ == "__main__":
    run()
