import math
from dataclasses import replace

try:
    from python_server.clawmachine.topics import MOTOR_CONTROLLER_COMMAND_TOPIC
    from python_server.configuration_loader import (
        MotorSpeedConfiguration,
        PositioningConfiguration,
    )
except ModuleNotFoundError:
    from topics import MOTOR_CONTROLLER_COMMAND_TOPIC
    from configuration_loader import MotorSpeedConfiguration, PositioningConfiguration

# Rohe Feldnamen im Endstop-JSON (siehe src_claw_server/main.cpp) auf die
# Achsen-Buchstaben abgebildet, die hier und in claw_machine.py verwendet
# werden. *2-Endstops (x2/y2) werden aktuell nicht fuers Homing ausgewertet.
ENDSTOP_FIELD_BY_AXIS = {"X": "x1", "Y": "y1", "Z": "z1"}

# Reihenfolge, in der die Achsen bei start_homing_sequence() nacheinander
# gehomet werden (erst wenn eine Achse ihren Endstop erreicht hat, startet
# die naechste). Die Geschwindigkeit dabei steht in config.ini (homing_percent).
HOMING_SEQUENCE = ("X", "Y", "Z")


class Moving:
    endstop_status = {
        "X": 1,  # 1 = nicht erreicht, 0 = erreicht
        "Y": 1,
        "Z": 1,
    }

    def __init__(
        self,
        mqtt_client,
        motor_speed_configuration: MotorSpeedConfiguration,
        positioning_configuration: PositioningConfiguration,
    ):
        self.mqtt_client = mqtt_client
        self.motor_speed_configuration = motor_speed_configuration
        self.positioning_configuration = positioning_configuration
        # Position in Schritten, vom Motor-Controller gezaehlt (0 = Endstop, positiv = vom
        # Endstop weg). Sie gilt nur, solange is_referenced[axis] gesetzt ist: das passiert
        # beim Erreichen des Endstops und faellt weg, wenn der Motor-Controller neu startet.
        self.step_position = {"X": 0, "Y": 0, "Z": 0}
        self.is_referenced = {"X": False, "Y": False, "Z": False}
        # Ziel einer laufenden Zielfahrt je Achse (None = keine)
        self.goto_target = {"X": None, "Y": None, "Z": None}
        self.homing_in_progress = {"X": False, "Y": False, "Z": False}
        # Steht die Achse beim Homing-Start schon am Endstop, faehrt sie erst ein Stueck
        # weg (homing_backoff_steps) und danach wieder hin, damit der Endstop sauber ausloest.
        self.homing_backing_off = {"X": False, "Y": False, "Z": False}
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
            if is_triggered and self.homing_backing_off[axis]:
                # Der Endstop meldet noch, waehrend die Achse sich erst davon entfernt.
                continue
            if is_triggered and self.homing_in_progress.get(axis):
                self.__finish_homing(axis)
                continue
            if is_triggered and self.commanded_speed[axis] < 0:
                # Achse faehrt gerade auf ihren Endstop zu: sofort stoppen. Am Endstop ist
                # die Position per Definition 0, das gleicht auch verlorene Schritte aus.
                print(f"[MOVING] Endstop {axis} ausgeloest — Achse gestoppt")
                self.goto_target[axis] = None
                self.__stop_axis_immediately(axis)
                self.__zero_axis(axis)

    def __send_axis_speed(self, axis, speed: int):
        self.commanded_speed[axis] = speed
        self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, f"{axis}:{speed}")

    def __stop_axis_immediately(self, axis):
        # Sofortstopp ohne Beschleunigungsrampe des Motor-Controllers (Endstop,
        # Homing). Normales Loslassen einer Taste laeuft weiter ueber "X:0" und
        # wird ueber die Rampe abgebremst.
        self.commanded_speed[axis] = 0
        self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, f"stop:{axis}")

    def __zero_axis(self, axis):
        # Aktuelle Stellung ist Position 0: Motor-Controller und Server setzen sie gleichzeitig.
        self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, f"zero:{axis}")
        self.step_position[axis] = 0
        self.is_referenced[axis] = True

    def __finish_homing(self, axis):
        self.homing_in_progress[axis] = False
        self.__stop_axis_immediately(axis)
        self.__zero_axis(axis)
        print(f"[MOVING] Homing {axis} abgeschlossen")

        next_index = HOMING_SEQUENCE.index(axis) + 1
        if next_index < len(HOMING_SEQUENCE):
            self.home_axis(HOMING_SEQUENCE[next_index])

    def start_homing_sequence(self):
        # Auch als Neustart brauchbar (Webinterface-Taste): ein evtl.
        # laufendes Homing wird verworfen und alle Achsen halten an.
        for axis in HOMING_SEQUENCE:
            self.homing_in_progress[axis] = False
            self.homing_backing_off[axis] = False
            self.goto_target[axis] = None
            self.is_referenced[axis] = False
            if self.commanded_speed[axis] != 0:
                self.__stop_axis_immediately(axis)
        self.home_axis(HOMING_SEQUENCE[0])

    def home_axis(self, axis):
        if axis not in self.step_position:
            print(f"[MOVING] Unbekannte Achse: {axis}")
            return False

        print(f"[MOVING] Starte Homing fuer Achse {axis}")

        self.homing_in_progress[axis] = True
        if self.__endstop_reached(axis):
            # Das Endstop-Board meldet nur Aenderungen: steht die Achse schon am
            # Endstop, kommt keine Meldung mehr, die das Homing abschliessen wuerde.
            # Deshalb ein Stueck wegfahren und danach erneut auf den Endstop zu.
            self.__start_homing_backoff(axis)
            return True
        self.__send_axis_speed(axis, -self.motor_speed_configuration.homing_percent)
        return True

    def __start_homing_backoff(self, axis):
        backoff_steps = self.motor_speed_configuration.homing_backoff_steps
        homing_percent = self.motor_speed_configuration.homing_percent
        print(
            f"[MOVING] Achse {axis} steht am Endstop — fahre {backoff_steps} Schritte weg"
        )
        self.homing_backing_off[axis] = True
        # Stellung gilt kurz als 0, damit der Motor-Controller die Wegstrecke zaehlen kann;
        # referenziert ist die Achse erst, wenn der Endstop wieder erreicht wurde.
        self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, f"zero:{axis}")
        self.step_position[axis] = 0
        self.goto_target[axis] = backoff_steps
        self.commanded_speed[axis] = homing_percent
        self.mqtt_client.publish(
            MOTOR_CONTROLLER_COMMAND_TOPIC,
            f"goto:{axis}:{backoff_steps}:{homing_percent}",
        )

    def __finish_homing_backoff(self, axis):
        self.homing_backing_off[axis] = False
        if self.__endstop_reached(axis):
            # Endstop meldet nach dem Wegfahren immer noch "erreicht": Mechanik, Verkabelung
            # oder Richtung pruefen. Nicht blind gegen den Anschlag fahren.
            print(
                f"[MOVING] Homing {axis} abgebrochen — Endstop loest sich beim Wegfahren nicht"
            )
            self.homing_in_progress[axis] = False
            return
        print(
            f"[MOVING] Achse {axis} weggefahren — fahre zum Homing zurueck auf den Endstop"
        )
        self.__send_axis_speed(axis, -self.motor_speed_configuration.homing_percent)

    def __endstop_reached(self, axis):
        return self.endstop_status.get(axis, 1) == 0

    def set_max_speed_percent(self, axis, percent: int):
        # Zur Laufzeit vom Webinterface eingestellt (xy_speed:/z_speed:), gilt bis
        # zum Neustart des Servers; danach wieder der Wert aus config.ini.
        percent = max(0, min(100, int(percent)))
        if axis in ("X", "Y"):
            self.motor_speed_configuration = replace(
                self.motor_speed_configuration, xy_percent=percent
            )
        elif axis == "Z":
            self.motor_speed_configuration = replace(
                self.motor_speed_configuration, z_percent=percent
            )

    def acceleration_percent_per_second(self, axis) -> int:
        if axis in ("X", "Y"):
            return self.motor_speed_configuration.xy_acceleration
        return self.motor_speed_configuration.z_acceleration

    def set_acceleration(self, axis, percent_per_second: int):
        # Beschleunigung in Prozentpunkten pro Sekunde; X und Y teilen sich einen Wert
        percent_per_second = max(0, int(percent_per_second))
        if axis in ("X", "Y"):
            self.motor_speed_configuration = replace(
                self.motor_speed_configuration, xy_acceleration=percent_per_second
            )
        elif axis == "Z":
            self.motor_speed_configuration = replace(
                self.motor_speed_configuration, z_acceleration=percent_per_second
            )

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
        minimum_speed = min(
            self.motor_speed_configuration.minimum_percent, max_speed_percent
        )
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
        if self.goto_target[axis] is not None:
            if scaled_speed == 0:
                # Panel und Joy-Con melden bei jeder Aenderung alle Achsen, auch die
                # unberuehrten mit 0 — das darf eine Zielfahrt nicht abbrechen.
                return True
            # Echte Eingabe auf dieser Achse: Zielfahrt abbrechen, die Eingabe gilt sofort.
            print(f"[MOVING] Zielfahrt der Achse {axis} durch Eingabe abgebrochen")
            self.goto_target[axis] = None
            self.commanded_speed[axis] = 0
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

        self.__send_axis_speed(axis, scaled_speed)
        return True

    # --- Position und Zielfahrten ---------------------------------------------

    def update_position(self, position_state: dict):
        # Gemeldet vom Motor-Controller (clawmachine/motor_controller/position), der jeden
        # Schritt mitzaehlt. Kommt eine Zielfahrt am Ziel an, ist sie damit beendet.
        for axis in self.step_position:
            if axis not in position_state:
                continue
            try:
                self.step_position[axis] = int(position_state[axis])
            except (TypeError, ValueError):
                continue
            target = self.goto_target[axis]
            if target is not None and self.step_position[axis] == target:
                print(f"[MOVING] Achse {axis} an Position {target} angekommen")
                self.goto_target[axis] = None
                self.commanded_speed[axis] = 0
                if self.homing_backing_off[axis]:
                    self.__finish_homing_backoff(axis)

    def on_motor_controller_restarted(self):
        # Ein neu gestarteter Motor-Controller zaehlt wieder ab 0, egal wo die Achsen stehen.

        for axis in self.step_position:
            self.is_referenced[axis] = False
            self.homing_in_progress[axis] = False
            self.homing_backing_off[axis] = False
            self.goto_target[axis] = None
            self.commanded_speed[axis] = 0

    def go_to_position(self, targets: dict) -> bool:
        # targets: Achse -> Zielposition in Schritten. Es wird entweder alles gestartet
        # oder (bei einem ungueltigen Ziel) nichts, damit keine Achse allein losfaehrt.
        if any(self.homing_in_progress.values()):
            print("[MOVING] Zielfahrt abgelehnt — Homing laeuft")
            return False
        for axis, target in targets.items():
            if axis not in self.step_position:
                print(f"[MOVING] Zielfahrt abgelehnt — unbekannte Achse {axis}")
                return False
            if not self.is_referenced[axis]:
                print(
                    f"[MOVING] Zielfahrt abgelehnt — Achse {axis} nicht referenziert (erst Homing)"
                )
                return False
            travel_steps = self.positioning_configuration.travel_steps_by_axis[axis]
            if target < 0 or (travel_steps > 0 and target > travel_steps):
                print(
                    f"[MOVING] Zielfahrt abgelehnt — {axis}={target} ausserhalb 0..{travel_steps or 'unbegrenzt'}"
                )
                return False

        goto_percent = self.positioning_configuration.goto_percent
        for axis, target in targets.items():
            if target == self.step_position[axis]:
                continue
            direction = 1 if target > self.step_position[axis] else -1
            print(
                f"[MOVING] Fahre Achse {axis} von {self.step_position[axis]} zu {target}"
            )
            self.goto_target[axis] = target
            # Wie eine normale Fahrt vermerkt, damit der Endstop sie bei negativer
            # Richtung auch hier sofort stoppt.
            self.commanded_speed[axis] = direction * goto_percent
            self.mqtt_client.publish(
                MOTOR_CONTROLLER_COMMAND_TOPIC, f"goto:{axis}:{target}:{goto_percent}"
            )
        return True

    def cancel_goto(self):
        for axis, target in self.goto_target.items():
            if target is not None and not self.homing_backing_off[axis]:
                self.goto_target[axis] = None
                self.__stop_axis_immediately(axis)
