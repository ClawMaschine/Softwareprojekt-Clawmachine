import time

from paho.mqtt import client as mqtt_client


class MQTTClient:
    def __init__(
        self,
        client_id,
        broker,
        port,
        connect_timeout_seconds,
        username=None,
        password=None,
    ):
        self.client_id = client_id
        self.username = username
        self.password = password
        self.broker = broker
        self.port = int(port)
        self.connect_timeout_seconds = float(connect_timeout_seconds)
        self.client = None
        self.is_connected = False

    def connect(self):
        def on_connect(client, userdata, flags, rc):
            if rc == 0:
                print("Connected to MQTT Broker!")
                self.is_connected = True
            else:
                print("Failed to connect, return code %d\n", rc)
                self.is_connected = False

        self.client = mqtt_client.Client(self.client_id)
        if self.username is not None:
            self.client.username_pw_set(self.username, self.password)
        self.client.on_connect = on_connect
        self.client.connect(self.broker, self.port)
        self.client.loop_start()

        connect_wait_started_at = time.time()
        while (
            not self.is_connected
            and (time.time() - connect_wait_started_at) < self.connect_timeout_seconds
        ):
            time.sleep(0.1)

        if not self.is_connected:
            raise TimeoutError(
                f"MQTT connection to {self.broker}:{self.port} timed out after {self.connect_timeout_seconds} seconds"
            )

        return self.client

    def publish(self, topic, msg):
        if self.client is None:
            raise RuntimeError("MQTT client is not connected. Call connect() first.")

        result = self.client.publish(topic, msg)
        status = result[0]
        if status == 0:
            print(f"Sent `{msg}` to topic `{topic}`")
            return True

        print(f"Failed to send message to topic {topic}")
        return False

    def disconnect(self):
        if self.client is None:
            return

        self.client.loop_stop()
        self.client.disconnect()
        self.is_connected = False
