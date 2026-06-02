from dataclasses import dataclass


@dataclass
class ClawESP:
    name: str
    last_command: str = None

    def __init__(self, name):
        self.name = name

    def got_command(self, command):
        self.last_command = command
        print(f"ESP {self.name} got command: {command}")
