class InputSourceControl:
    # Haelt fest, welche physischen Eingabequellen (Panel, Joy-Con, ...)
    # aktuell Steuerbefehle an die Motoren weiterreichen duerfen. Nur im
    # Speicher — setzt sich bei jedem Serverstart auf "alle erlaubt"
    # zurueck, keine Persistenz ueber einen Neustart hinaus.
    def __init__(self, source_names):
        self.allowed = {name: True for name in source_names}

    def is_allowed(self, name: str) -> bool:
        return self.allowed.get(name, True)

    def set_allowed(self, name: str, is_allowed: bool) -> bool:
        if name not in self.allowed:
            return False
        self.allowed[name] = is_allowed
        return True

    def to_dict(self) -> dict:
        return dict(self.allowed)
