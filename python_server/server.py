import threading

try:
    from python_server.clawmachine.claw_machine import ClawMachine
    from python_server.webserver import run_webserver
except ModuleNotFoundError:
    from clawmachine.claw_machine import ClawMachine
    from webserver import run_webserver


def run():
    # ClawMachine() blockiert für immer in ihrer eigenen main_loop() (das MQTT-
    # Netzwerk-IO läuft davon unabhängig schon in einem paho-Hintergrund-Thread,
    # siehe MQTTClient.connect()/loop_start()) — läuft deshalb in einem eigenen
    # Thread, damit der Hauptthread für den Kamera-Webserver frei bleibt.
    claw_machine_thread = threading.Thread(target=ClawMachine, daemon=True)
    claw_machine_thread.start()

    run_webserver()


if __name__ == "__main__":
    run()
