class WebInterfacePresence:
    # Verfolgt Online/Offline-Status der Webinterface-Browser-Clients separat
    # von der ESP32-DeviceRegistry. Browser-Tabs sind UI-Clients, keine
    # Hardware, die überwacht werden muss — sie sollen deshalb nie in der
    # Geräteliste (clawmachine/web_interface/devices) auftauchen, bekommen
    # aber trotzdem ihren eigenen Online/Offline-Status mitgeteilt (siehe
    # claw_machine.py on_message()).
    def __init__(self):
        self.is_online = False

    def set_online(self, is_online: bool):
        self.is_online = is_online
