#include "panel_input.h"
#include "firmware_config.h"

// 12-Bit-ADC des ESP32
static constexpr int ANALOG_READ_MAXIMUM = 4095;

void PanelInput::begin()
{
    pinMode(CLAW_PANEL_PIN_UP,      INPUT_PULLUP);
    pinMode(CLAW_PANEL_PIN_DOWN,    INPUT_PULLUP);
    pinMode(CLAW_PANEL_PIN_LEFT,    INPUT_PULLUP);
    pinMode(CLAW_PANEL_PIN_RIGHT,   INPUT_PULLUP);
    pinMode(CLAW_PANEL_PIN_FRONT,   INPUT_PULLUP);
    pinMode(CLAW_PANEL_PIN_BACK,    INPUT_PULLUP);
    pinMode(CLAW_PANEL_PIN_POTENTIOMETER, INPUT);
}

void PanelInput::read()
{
    // LOW = gedrückt (Pull-Up)
    up_button      = digitalRead(CLAW_PANEL_PIN_UP)      == LOW;
    down_button    = digitalRead(CLAW_PANEL_PIN_DOWN)    == LOW;
    left_button    = digitalRead(CLAW_PANEL_PIN_LEFT)    == LOW;
    right_button   = digitalRead(CLAW_PANEL_PIN_RIGHT)   == LOW;
    front_button   = digitalRead(CLAW_PANEL_PIN_FRONT)   == LOW;
    back_button    = digitalRead(CLAW_PANEL_PIN_BACK)    == LOW;
    // Poti als Klauen-Stellung in Prozent (0-100) statt als roher ADC-Wert (0-4095).
    // Ganze Prozent daempfen zugleich das ADC-Rauschen, das sonst bei jedem Lesen
    // eine neue MQTT-Nachricht ausloesen wuerde.
    const int potentiometer_value = analogRead(CLAW_PANEL_PIN_POTENTIOMETER);
    claw = constrain(map(potentiometer_value, 0, ANALOG_READ_MAXIMUM, 0, 100), 0, 100);
}

bool PanelInput::isValid() const
{
    if (left_button && right_button) return false;
    if (up_button   && down_button)  return false;
    return true;
}
