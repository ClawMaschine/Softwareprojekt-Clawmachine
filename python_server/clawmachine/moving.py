MOTOR_CONTROLLER_COMMAND_TOPIC = "clawmachine/motor_controller/motor/command"


class Moving:
    endstop_status = {
        "X": 1,  # 1 = nicht erreicht, 0 = erreicht
        "Y": 1,
    }
    position = {
        "X": 0,  # Aktuelle Position der Achse (z.B. in Millimetern oder Schritten)
        "Y": 0,
    }

    def __init__(self, mqtt_client):
        self.mqtt_client = mqtt_client
        



    def home_axis(self, axis):
        print(f"[MOVING] Achse {axis} wird gehome")
        # Homing-Bewegung selbst macht aktuell die Firmware (siehe homing() in
        # claw_motor_controller.cpp) — hier wird nur die serverseitig
        # mitgezaehlte Position zurueckgesetzt, sobald der Endstop erreicht ist.
        if axis == "X":
            self.position["X"] = 0
        elif axis == "Y":
            self.position["Y"] = 0
        else:
            print(f"[MOVING] Unbekannte Achse: {axis}")
            return False
        return True

    def __endstop_reached(self, axis):
        return self.endstop_status.get(axis, 1) == 0

    def move_axis(self, axis, speed: int):
        print(f"[MOVING] Bewegung der Achse {axis} mit Geschwindigkeit {speed}")
        if self.__endstop_reached(axis):
            print(f"[MOVING] Bewegung der Achse {axis} abgebrochen — Endstop erreicht")
            return False

        # Position wird hier erstmal nur grob mitgezaehlt (kein echtes
        # Weg-Zeit-Modell) — der Motor-Controller-ESP bekommt ausschliesslich
        # die Geschwindigkeit und macht selbst keine Positionsberechnung.
        if axis == "X":
            self.position["X"] += speed
        elif axis == "Y":
            self.position["Y"] += speed
        else:
            print(f"[MOVING] Unbekannte Achse: {axis}")
            return False

        self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, f"{axis}:{speed}")
        return True