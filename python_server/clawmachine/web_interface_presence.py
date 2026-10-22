class WebInterfacePresence:
    # Verfolgt Online/Offline-Status der Webinterface-Browser-Clients separat
    # von der ESP32-DeviceRegistry. Browser-Tabs sind UI-Clients, keine
    # Hardware, die überwacht werden muss — sie sollen deshalb nie in der
    # Geräteliste (clawmachine/web_interface/devices) auftauchen, bekommen
    # aber trotzdem ihren eigenen Online/Offline-Status mitgeteilt (siehe
    # claw_machine.py on_message()).
    #
    # Jeder Browser-Tab bekommt frontendseitig eine eigene zufällige
    # vierstellige ID (siehe webInterfaceId in mqtt.service.ts) und damit
    # einen eigenen Status-Topic — es können also mehrere gleichzeitig
    # online sein, deshalb ein Set statt eines einzelnen Bools.
    def __init__(self):
        self.online_ids: set[str] = set()

    def set_online(self, web_interface_id: str, is_online: bool):
        if is_online:
            self.online_ids.add(web_interface_id)
        else:
            self.online_ids.discard(web_interface_id)

    @property
    def is_any_online(self) -> bool:
        return len(self.online_ids) > 0
