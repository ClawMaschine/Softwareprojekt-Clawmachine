from turtle import position
from mqtt import MQTTClient



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
        this.mqtt_client = mqtt_client
        this.mqtt_client.subscribe("clawmachine/motor_controller/")

    
    def __endstop_reached(self, axis):
        return self.endstop_status.get(axis, 1) == 0
    
    
    
    def move_axis(self, axis, direction):
        if self.__endstop_reached(axis):
            print(f"[MOVING] Bewegung der Achse {axis} in Richtung {direction} abgebrochen — Endstop erreicht")
            return False
        # Hier würde die Logik zum Bewegen der Achse implementiert werden
        print(f"[MOVING] Achse {axis} bewegt sich in Richtung {direction}")
        return True