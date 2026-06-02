from python_server.mqtt import MQTTClient

from ..configuration_loader import MqttConfiguration, load_mqtt_configuration
from ..esp.mqtt_esp_controller import MQTTEspController

load_mqtt_configuration()
mqtt_configuration = load_mqtt_configuration()
mqtt_client = MQTTClient(
    client_id=mqtt_configuration.client_id,
    broker=mqtt_configuration.broker,
    port=mqtt_configuration.port,
    connect_timeout_seconds=mqtt_configuration.connect_timeout_seconds,
)
mqtt_client.connect()
mqtt_client.publish(mqtt_configuration.device_added_topic, "Emulated ESP")
