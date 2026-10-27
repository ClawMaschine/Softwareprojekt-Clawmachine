import math
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
        # Zuletzt an den Motor-Controller gesendete Geschwindigkeit je Achse — damit
        # eine Achse sofort gestoppt werden kann, wenn waehrend der Fahrt ihr Endstop
        # ausloest.
        self.commanded_speed = {"X": 0, "Y": 0, "Z": 0}

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
            elif is_triggered and self.commanded_speed[axis] < 0:
                # Achse faehrt gerade auf ihren Endstop zu: sofort stoppen
                print(f"[MOVING] Endstop {axis} ausgeloest — Achse gestoppt")
                self.__stop_axis_immediately(axis)

    def __send_axis_speed(self, axis, speed: int):
        self.commanded_speed[axis] = speed
        self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, f"{axis}:{speed}")

    def __stop_axis_immediately(self, axis):
        # Sofortstopp ohne Beschleunigungsrampe des Motor-Controllers (Endstop,
        # Homing). Normales Loslassen einer Taste laeuft weiter ueber "X:0" und
        # wird ueber die Rampe abgebremst.
        self.commanded_speed[axis] = 0
        self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, f"stop:{axis}")

    def __finish_homing(self, axis):
        self.homing_in_progress[axis] = False
        self.position[axis] = 0
        self.__stop_axis_immediately(axis)
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
        self.__send_axis_speed(axis, -HOMING_SPEED)
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

    def acceleration_percent_per_second(self, axis) -> int:
        if axis in ("X", "Y"):
            return self.motor_speed_configuration.xy_acceleration
        return self.motor_speed_configuration.z_acceleration

    def set_acceleration(self, axis, percent_per_second: int):
        # Beschleunigung in Prozentpunkten pro Sekunde; X und Y teilen sich einen Wert
        percent_per_second = max(0, int(percent_per_second))
        if axis in ("X", "Y"):
            self.motor_speed_configuration = replace(self.motor_speed_configuration, xy_acceleration=percent_per_second)
        elif axis == "Z":
            self.motor_speed_configuration = replace(self.motor_speed_configuration, z_acceleration=percent_per_second)

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
        max_speed_percent = self.max_speed_percent(axis)
        scaled_speed = round(max_speed_percent * input_percent / 100)
        if scaled_speed == 0:
            return 0
        # Sehr kleine Geschwindigkeiten laufen bei Schrittmotoren sichtbar ruckelnd;
        # deshalb nicht unter minimum_percent (aber nie ueber die Standard-Geschwindigkeit).
        minimum_speed = min(self.motor_speed_configuration.minimum_percent, max_speed_percent)
        return int(math.copysign(max(abs(scaled_speed), minimum_speed), scaled_speed))

    def move_axis(self, axis, speed: float):
        # Einziger Weg, auf dem Fahrbefehle zum Motor-Controller gehen (Panel, Joy-Con,
        # Webinterface, clawmachine/claw). speed = Eingabe in Prozent (-100..100,
        # Vorzeichen = Richtung), wird auf die Standard-Geschwindigkeit der Achse
        # skaliert. Negative Richtung = auf den Endstop der Achse zu (wie beim Homing).
        if axis not in ("X", "Y", "Z"):
            print(f"[MOVING] Unbekannte Achse: {axis}")
            return False
        if self.homing_in_progress.get(axis):
            # Das Homing hat Vorrang; Eingaben auf dieser Achse wuerden es stoeren.
            return False

        scaled_speed = self.scale_speed(axis, speed)
        if scaled_speed < 0 and self.__endstop_reached(axis):
            # Statt nur abzubrechen explizit 0 senden: sonst liefe die Achse mit der
            # vorigen Geschwindigkeit weiter.
            print(f"[MOVING] Bewegung der Achse {axis} blockiert — Endstop erreicht")
            if self.commanded_speed[axis] != 0:
                self.__stop_axis_immediately(axis)
            return True

        # Nur bei Aenderung senden (Panel/Joy-Con melden auch bei Aenderungen anderer Achsen)
        if scaled_speed == self.commanded_speed[axis]:
            return True
        print(f"[MOVING] Bewegung der Achse {axis} mit Geschwindigkeit {scaled_speed}")

        # Position wird hier erstmal nur grob mitgezaehlt (kein echtes
        # Weg-Zeit-Modell) — der Motor-Controller-ESP bekommt ausschliesslich
        # die Geschwindigkeit und macht selbst keine Positionsberechnung.
        self.position[axis] += scaled_speed
        self.__send_axis_speed(axis, scaled_speed)
        return True
