#pragma once

#include <Arduino.h>

struct PanelInput {
    bool up_button      = false;
    bool down_button    = false;
    bool left_button    = false;
    bool right_button   = false;
    bool grab_button    = false;
    bool release_button = false;

    void begin();
    void read();

    // Gibt false zurück wenn Links+Rechts oder Hoch+Runter gleichzeitig gedrückt sind
    bool isValid() const;
};
