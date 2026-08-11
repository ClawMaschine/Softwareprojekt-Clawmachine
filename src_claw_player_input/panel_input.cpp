#include "panel_input.h"
#include "firmware_config.h"

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
    // Poti statt Taster: unterhalb der Schwelle greifen, oberhalb loslassen
    const int potentiometer_value = analogRead(CLAW_PANEL_PIN_POTENTIOMETER);
    grab_button    = potentiometer_value <  CLAW_PANEL_POTENTIOMETER_THRESHOLD;
    release_button = potentiometer_value >= CLAW_PANEL_POTENTIOMETER_THRESHOLD;
}

bool PanelInput::isValid() const
{
    if (left_button && right_button) return false;
    if (up_button   && down_button)  return false;
    return true;
}
