from dataclasses import replace

try:
    from python_server.clawmachine.topics import MOTOR_CONTROLLER_COMMAND_TOPIC
    from python_server.configuration_loader import MotorSpeedConfiguration
except ModuleNotFoundError:
    from topics import MOTOR_CONTROLLER_COMMAND_TOPIC
    from configuration_loader import MotorSpeedConfiguration

# Rohe Feldnamen im Endstop-JSON (siehe src_claw_server/main.cpp) auf die
# Achsen-Buchstaben abgebildet, die hier und in claw_machine.py verwendet
# werden. *2-Endstops (x2/y2) werden aktuell nicht fuers Homing ausgewertet.
ENDSTOP_FIELD_BY_AXIS = {"X": "x1", "Y": "y1", "Z": "z1"}

# Reihenfolge, in der die Achsen bei start_homing_sequence() nacheinander
# gehomet werden (erst wenn eine Achse ihren Endstop erreicht hat, startet
# die naechste) — Geschwindigkeit, mit der dabei Richtung Endstop gefahren wird.
HOMING_SEQUENCE = ("X", "Y", "Z")
HOMING_SPEED = 30



class Moving:
    endstop_status = {
        "X": 1,  # 1 = nicht erreicht, 0 = erreicht
        "Y": 1,
        "Z": 1,
    }
    position = {
        "X": 0,  # Aktuelle Position der Achse (z.B. in Millimetern oder Schritten)
        "Y": 0,
        "Z": 0,
    }

    def __init__(self, mqtt_client, motor_speed_configuration: MotorSpeedConfiguration):
        self.mqtt_client = mqtt_client
        self.motor_speed_configuration = motor_speed_configuration
        self.homing_in_progress = {"X": False, "Y": False, "Z": False}

    def update_endstop_status(self, endstop_state: dict):
        # Wird von claw_machine.py bei jeder Nachricht auf ENDSTOP_TOPIC
        # aufgerufen (Rohstatus vom Endstop-Board, z.B. {"x1":0,"x2":0,...}).
        # Kein blockierendes Warten hier — paho-mqtt ruft on_message im
        # einzigen Netzwerk-Thread auf, eine blockierende Schleife wuerde
        # genau die Nachricht nie verarbeiten koennen, auf die sie wartet.
        for axis, field in ENDSTOP_FIELD_BY_AXIS.items():
            if field not in endstop_state:
                continue
            is_triggered = endstop_state[field] == 1
            self.endstop_status[axis] = 0 if is_triggered else 1
            if is_triggered and self.homing_in_progress.get(axis):
                self.__finish_homing(axis)

    def __finish_homing(self, axis):
        self.homing_in_progress[axis] = False
        self.position[axis] = 0
        self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, f"{axis}:0")
        print(f"[MOVING] Homing {axis} abgeschlossen")

        next_index = HOMING_SEQUENCE.index(axis) + 1
        if next_index < len(HOMING_SEQUENCE):
            self.home_axis(HOMING_SEQUENCE[next_index])

    def start_homing_sequence(self):
        self.home_axis(HOMING_SEQUENCE[0])

    def home_axis(self, axis):
        if axis not in self.position:
            print(f"[MOVING] Unbekannte Achse: {axis}")
            return False

        print(f"[MOVING] Starte Homing fuer Achse {axis}")
        self.homing_in_progress[axis] = True
        self.mqtt_client.publish(
            MOTOR_CONTROLLER_COMMAND_TOPIC, f"{axis}:{-HOMING_SPEED}"
        )
        return True

    def __endstop_reached(self, axis):
        return self.endstop_status.get(axis, 1) == 0

    def set_max_speed_percent(self, axis, percent: int):
        # Zur Laufzeit vom Webinterface eingestellt (xy_speed:/z_speed:), gilt bis
        # zum Neustart des Servers; danach wieder der Wert aus config.ini.
        percent = max(0, min(100, int(percent)))
        if axis in ("X", "Y"):
            self.motor_speed_configuration = replace(self.motor_speed_configuration, xy_percent=percent)
        elif axis == "Z":
            self.motor_speed_configuration = replace(self.motor_speed_configuration, z_percent=percent)

    def max_speed_percent(self, axis) -> int:
        # In config.ini ([motor_speed]) eingestellte Standard-Geschwindigkeit der
        # Achse in Prozent der maximalen Motordrehzahl. X und Y teilen sich einen Wert.
        if axis in ("X", "Y"):
            return self.motor_speed_configuration.xy_percent
        if axis == "Z":
            return self.motor_speed_configuration.z_percent
        return 0

    def scale_speed(self, axis, input_percent: float) -> int:
        # input_percent (-100..100, Vorzeichen = Richtung) prozentual auf die
        # Standard-Geschwindigkeit der Achse umrechnen.
        return round(self.max_speed_percent(axis) * input_percent / 100)

    def move_axis(self, axis, speed: int):
        # speed = Eingabe in Prozent (-100..100, Vorzeichen = Richtung), wird auf die
        # Standard-Geschwindigkeit der Achse skaliert.
        print(f"[MOVING] Bewegung der Achse {axis} mit Geschwindigkeit {speed}")
        if self.__endstop_reached(axis):
            print(f"[MOVING] Bewegung der Achse {axis} abgebrochen — Endstop erreicht")
            return False

        # Position wird hier erstmal nur grob mitgezaehlt (kein echtes
        # Weg-Zeit-Modell) — der Motor-Controller-ESP bekommt ausschliesslich
        # die Geschwindigkeit und macht selbst keine Positionsberechnung.
        if axis not in ("X", "Y", "Z"):
            print(f"[MOVING] Unbekannte Achse: {axis}")
            return False
        scaled_speed = self.scale_speed(axis, speed)
        self.position[axis] += scaled_speed
        self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, f"{axis}:{scaled_speed}")

        return True
