class Steering:
    
    def __init__(self, mqtt_client):
        self.mqtt_client = mqtt_client
        self.current_steering_state = {
            "up": 0,
            "down": 0,
            "left": 0,
            "right": 0,
            "front": 0,
            "back": 0,
            "claw": 0
        }
