import select
import sys
import termios
import time
import tty

try:
    from python_server.mqtt import MQTTClient
    from python_server.configuration_loader import load_mqtt_configuration
except ModuleNotFoundError:
    from mqtt import MQTTClient
    from configuration_loader import load_mqtt_configuration

DEVICE_NAME = "steering"
SPEED = 80
RELEASE_TIMEOUT_SECONDS = 0.3
POLL_INTERVAL_SECONDS = 0.05

# Taste halten zum Bewegen, loslassen (kein Nachdruecken > RELEASE_TIMEOUT_SECONDS) stoppt die Achse.
AXIS_KEYS = {
    "w": ("Y", SPEED),
    "s": ("Y", -SPEED),
    "a": ("X", -SPEED),
    "d": ("X", SPEED),
    "q": ("Z", SPEED),
    "e": ("Z", -SPEED),
}
CLAW_KEYS = {
    "o": "open",
    "c": "close",
}

LEGEND = """Steuerung:
  w/s  Y-Achse vor/zurueck
  a/d  X-Achse links/rechts
  q/e  Z-Achse (Seil) hoch/runter
  o/c  Klaue oeffnen/schliessen
  Taste halten zum Bewegen, loslassen zum Stoppen. Strg+C zum Beenden.
"""


class RawTerminal:
    def __enter__(self):
        self.fd = sys.stdin.fileno()
        self.original_settings = termios.tcgetattr(self.fd)
        tty.setcbreak(self.fd)
        return self

    def __exit__(self, *_args):
        termios.tcsetattr(self.fd, termios.TCSADRAIN, self.original_settings)

    def read_pending_keys(self):
        # Liest ALLE gerade im Puffer wartenden Zeichen, nicht nur eins — sonst
        # baut sich bei gehaltener Taste (OS-Auto-Repeat) ein Rueckstau auf, der
        # neue Tastendruecke erst nach Abarbeiten der alten registrieren wuerde.
        keys = []
        while select.select([sys.stdin], [], [], 0)[0]:
            keys.append(sys.stdin.read(1))
        return keys


def connect():
    mqtt_configuration = load_mqtt_configuration()
    mqtt_client = MQTTClient(
        client_id=DEVICE_NAME,
        broker=mqtt_configuration.broker,
        port=mqtt_configuration.port,
        connect_timeout_seconds=mqtt_configuration.connect_timeout_seconds,
        username=mqtt_configuration.username,
        password=mqtt_configuration.password,
    )
    mqtt_client.connect()
    mqtt_client.publish(mqtt_configuration.device_added_topic, DEVICE_NAME)
    return mqtt_client, mqtt_configuration.topic


def main():
    mqtt_client, control_topic = connect()
    print(LEGEND)

    active_axis_speed = {"X": 0, "Y": 0, "Z": 0}
    last_seen_at = {}

    with RawTerminal() as terminal:
        while True:
            keys = terminal.read_pending_keys()
            now = time.time()

            for key in keys:
                key = key.lower()
                if key in AXIS_KEYS:
                    axis, speed = AXIS_KEYS[key]
                    last_seen_at[axis] = now
                    if active_axis_speed[axis] != speed:
                        active_axis_speed[axis] = speed
                        mqtt_client.publish(control_topic, f"{axis}:{speed}")
                elif key in CLAW_KEYS:
                    mqtt_client.publish(control_topic, f"claw:{CLAW_KEYS[key]}")

            for axis in ("X", "Y", "Z"):
                if active_axis_speed[axis] != 0 and now - last_seen_at.get(axis, 0) > RELEASE_TIMEOUT_SECONDS:
                    active_axis_speed[axis] = 0
                    mqtt_client.publish(control_topic, f"{axis}:0")

            time.sleep(POLL_INTERVAL_SECONDS)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nSteering beendet.")
