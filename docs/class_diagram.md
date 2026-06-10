# Klassendiagramme

## Systemübersicht

```mermaid
graph LR
    ESP1["ESP32\nMotor Controller"]
    ESP2["ESP32\nControl Panel"]
    ESP3["ESP32\nPlayer Input"]
    BROKER["Mosquitto Broker\n(Docker :1883)"]
    SERVER["Python Server\n(Raspberry Pi)"]

    ESP1 <-->|MQTT| BROKER
    ESP2 <-->|MQTT| BROKER
    ESP3 <-->|MQTT| BROKER
    SERVER <-->|MQTT| BROKER
```

---

## Python Server

```mermaid
classDiagram
    class ClawMachine {
        +bool is_claw_open
        +str control_topic
        +DeviceRegistry device_registry
        +MQTTClient mqtt_client
        +MQTTEspController esp_controller
        +setup_message_handlers()
        +on_message(client, userdata, message)
        +open_claw()
        +close_claw()
        +main_loop()
    }

    class DeviceRegistry {
        +str device_added_topic
        +dict devices_by_name
        +register(device_name) EspDevice
        +get(name) EspDevice
        +extract_device_name(topic, payload) str
    }

    class EspDevice {
        +str name
        +float added_at_unix_seconds
        +bool is_online
        +EspDeviceMetadata metadata
        +on_message(topic, payload)
    }

    class EspDeviceMetadata {
        +int uptime_milliseconds
    }

    class MQTTEspController {
        +MQTTEsp steering_esp
        +MQTTEsp arm_esp
        +send_steering_command(command)
        +send_arm_command(command)
        +get_connected_esps() list
    }

    class MQTTEsp {
        +str mqtt_topic
        +MQTTClient mqtt_client
        +send_command(command)
    }

    class MQTTClient {
        +str client_id
        +str broker
        +int port
        +str username
        +str password
        +connect()
        +publish(topic, msg)
        +disconnect()
    }

    class MqttConfiguration {
        +str broker
        +int port
        +str topic
        +str device_added_topic
        +str client_id
        +float connect_timeout_seconds
        +str username
        +str password
    }

    ClawMachine "1" --> "1" DeviceRegistry
    ClawMachine "1" --> "1" MQTTClient
    ClawMachine "1" --> "1" MQTTEspController
    DeviceRegistry "1" --> "*" EspDevice
    EspDevice "1" --> "1" EspDeviceMetadata
    MQTTEspController "1" --> "2" MQTTEsp
    MQTTEsp "1" --> "1" MQTTClient
    ClawMachine ..> MqttConfiguration : lädt
```

---

## Firmware (ESP32)

```mermaid
classDiagram
    class ClawMqttConnection {
        -const char* wifiSsid
        -const char* wifiPassword
        -const char* mqttBrokerHost
        -uint16_t mqttBrokerPort
        -const char* mqttClientId
        -const char* mqttUsername
        -const char* mqttPassword
        -unsigned long reconnectIntervalMs
        -WiFiClient networkClient
        -PubSubClient mqttClient
        +begin()
        +maintainConnection()
        -ensureWifiConnected() bool
        -ensureMqttConnected() bool
    }

    class PubSubClient {
        +connect(id, user, pass, willTopic, ...) bool
        +publish(topic, payload, retain) bool
        +loop()
        +connected() bool
    }

    class WiFiClient

    ClawMqttConnection "1" --> "1" PubSubClient
    ClawMqttConnection "1" --> "1" WiFiClient
```

### MQTT Topics (Firmware → Broker)

| Topic | Payload | Beschreibung |
|---|---|---|
| `clawmachine/<id>/metadata/uptime` | Millisekunden | Uptime seit Boot, alle ~20ms |
| `clawmachine/<id>/status` | `online` / `offline` | LWT: offline bei Verbindungsabbruch |

### MQTT Topics (Server → Broker)

| Topic | Payload | Beschreibung |
|---|---|---|
| `clawmachine/claw` | `open` / `close` | Greifer steuern |
| `clawmachine/device/steering/command` | Befehl | Steuerung ESP |
| `clawmachine/device/arm/command` | Befehl | Arm ESP |
