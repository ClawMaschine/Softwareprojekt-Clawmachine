#include "panel_input.h"
#include "firmware_config.h"

void PanelInput::begin()
{
    pinMode(CLAW_PANEL_PIN_UP,      INPUT_PULLUP);
    pinMode(CLAW_PANEL_PIN_DOWN,    INPUT_PULLUP);
    pinMode(CLAW_PANEL_PIN_LEFT,    INPUT_PULLUP);
    pinMode(CLAW_PANEL_PIN_RIGHT,   INPUT_PULLUP);
    pinMode(CLAW_PANEL_PIN_GRAB,    INPUT_PULLUP);
    pinMode(CLAW_PANEL_PIN_RELEASE, INPUT_PULLUP);
}

void PanelInput::read()
{
    // LOW = gedrückt (Pull-Up)
    up_button      = digitalRead(CLAW_PANEL_PIN_UP)      == LOW;
    down_button    = digitalRead(CLAW_PANEL_PIN_DOWN)    == LOW;
    left_button    = digitalRead(CLAW_PANEL_PIN_LEFT)    == LOW;
    right_button   = digitalRead(CLAW_PANEL_PIN_RIGHT)   == LOW;
    grab_button    = digitalRead(CLAW_PANEL_PIN_GRAB)    == LOW;
    release_button = digitalRead(CLAW_PANEL_PIN_RELEASE) == LOW;
}

bool PanelInput::isValid() const
{
    if (left_button && right_button) return false;
    if (up_button   && down_button)  return false;
    return true;
}
