from typing import Optional

CLAWMACHINE_TOPIC_PREFIX = "clawmachine"
WEB_INTERFACE_NAME = "web_interface"


def build_topic(*parts: str) -> str:
    return "/".join((CLAWMACHINE_TOPIC_PREFIX, *parts))


def device_topic(device_name: str, *sub_parts: str) -> str:
    # device_topic("claw_endstop", "status") -> clawmachine/claw_endstop/status
    # device_topic("+", "status")            -> Wildcard fürs Subscribe
    return build_topic(device_name, *sub_parts)


def web_interface_topic(*sub_parts: str) -> str:
    return build_topic(WEB_INTERFACE_NAME, *sub_parts)


def parse_device_name(topic: str, *sub_parts: str) -> Optional[str]:
    # Gegenstück zu device_topic(): liefert den Gerätenamen, wenn das Topic
    # genau clawmachine/<name>/<sub_parts...> entspricht, sonst None.
    topic_parts = topic.split("/")
    if len(topic_parts) != 2 + len(sub_parts):
        return None
    if topic_parts[0] != CLAWMACHINE_TOPIC_PREFIX:
        return None
    if tuple(topic_parts[2:]) != sub_parts:
        return None
    return topic_parts[1]


# Haupt-Steuertopic des Servers (Befehle mit X:/Y:/Z:/claw:-Präfix)
CONTROL_TOPIC = build_topic("claw")

# Server -> Motor-Controller
MOTOR_CONTROLLER_COMMAND_TOPIC = device_topic(
    "claw_motor_controller", "motor", "command"
)
MOTOR_CONTROLLER_ERROR_LOG_TOPIC = device_topic("claw_motor_controller", "error_log")
# JSON-Settings-Update (z.B. Beschleunigung), getrennt von den Bewegungsbefehlen
MOTOR_CONTROLLER_SETTINGS_TOPIC = device_topic("claw_motor_controller", "settings")
# Motor-Controller -> Server: gezaehlte Schrittpositionen {"X":..,"Y":..,"Z":..}
MOTOR_CONTROLLER_POSITION_TOPIC = device_topic("claw_motor_controller", "position")

# Eingabegeräte -> Server
PLAYER_INPUT_PANEL_TOPIC = device_topic("player_input", "panel")
PLAYER_INPUT_JOYCON_TOPIC = device_topic("player_input", "joycon")
ENDSTOP_TOPIC = device_topic("endstop", "state")

# Webinterface <-> Server (Anfrage/Antwort)
WEB_INTERFACE_COMMAND_TOPIC = web_interface_topic("command")
WEB_INTERFACE_ENDSTOP_TOPIC = web_interface_topic("endstop")
WEB_INTERFACE_DEVICE_LIST_TOPIC = web_interface_topic("devices")
WEB_INTERFACE_DEVICE_LIST_REQUEST_TOPIC = web_interface_topic("devices", "request")
WEB_INTERFACE_INPUT_SOURCES_TOPIC = web_interface_topic("input_sources")
WEB_INTERFACE_INPUT_SOURCES_REQUEST_TOPIC = web_interface_topic(
    "input_sources", "request"
)
WEB_INTERFACE_INPUT_SOURCES_SET_TOPIC = web_interface_topic("input_sources", "set")
WEB_INTERFACE_SPEED_SETTINGS_TOPIC = web_interface_topic("speed_settings")
WEB_INTERFACE_SPEED_SETTINGS_REQUEST_TOPIC = web_interface_topic(
    "speed_settings", "request"
)
WEB_INTERFACE_START_HOMING_TOPIC = web_interface_topic("start_homing")
WEB_INTERFACE_POSITION_TOPIC = web_interface_topic("position")
WEB_INTERFACE_POSITION_REQUEST_TOPIC = web_interface_topic("position", "request")
