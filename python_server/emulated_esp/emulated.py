import time

try:
    from python_server.mqtt import MQTTClient
    from python_server.configuration_loader import load_mqtt_configuration
    from python_server.clawmachine.topics import device_topic
except ModuleNotFoundError:
    from mqtt import MQTTClient
    from configuration_loader import load_mqtt_configuration
    from clawmachine.topics import device_topic

EMULATED_ESP_NAME = "emulated_esp"

mqtt_configuration = load_mqtt_configuration()
mqtt_client = MQTTClient(
    client_id=EMULATED_ESP_NAME,
    broker=mqtt_configuration.broker,
    port=mqtt_configuration.port,
    connect_timeout_seconds=mqtt_configuration.connect_timeout_seconds,
    username=mqtt_configuration.username,
    password=mqtt_configuration.password,
)
mqtt_client.connect()

uptime_topic = device_topic(EMULATED_ESP_NAME, "metadata", "uptime")
status_topic = device_topic(EMULATED_ESP_NAME, "status")
started_at = time.time()
mqtt_client.publish(status_topic, "online")
while True:
    uptime_ms = int((time.time() - started_at) * 1000)
    mqtt_client.publish(uptime_topic, str(uptime_ms))
    time.sleep(1)
