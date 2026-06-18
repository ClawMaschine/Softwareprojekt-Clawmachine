import json
import time

try:
    from python_server.mqtt import MQTTClient
    from python_server.configuration_loader import load_mqtt_configuration
except ModuleNotFoundError:
    from mqtt import MQTTClient
    from configuration_loader import load_mqtt_configuration

EMULATED_ESP_NAME = "emulated_esp"

# Discovery-Descriptor: meldet den emulierten ESP als PlayerInputEsp mit einem
# Joy-Con an. Der Server erzeugt daraus die passenden ESP-/Hardware-Objekte.
ESP_DESCRIPTOR = {
    "name": EMULATED_ESP_NAME,
    "type": "player_input",
    "hardware": [
        {"name": "joycon_r", "type": "joycon"},
    ],
}

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
mqtt_client.publish(mqtt_configuration.device_added_topic, json.dumps(ESP_DESCRIPTOR))

uptime_topic = f"clawmachine/{EMULATED_ESP_NAME}/metadata/uptime"
internal_topic = f"clawmachine/{EMULATED_ESP_NAME}/internal"
# Demo-Eingaben, die der emulierte Joy-Con der Reihe nach an den Server schickt.
emulated_player_inputs = ["left", "right", "up", "down", "open", "close"]
started_at = time.time()
loop_iteration = 0

while True:
    uptime_ms = int((time.time() - started_at) * 1000)
    mqtt_client.publish(uptime_topic, str(uptime_ms))

    simulated_input = emulated_player_inputs[loop_iteration % len(emulated_player_inputs)]
    mqtt_client.publish(internal_topic, simulated_input)
    loop_iteration += 1

    time.sleep(1)
