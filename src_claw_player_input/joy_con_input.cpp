#include "joy_con_input.h"

void JoyConInput::update(ControllerPtr controller)
{
    if (!controller || !controller->isConnected()) {
        return;
    }

    const uint16_t buttons = controller->buttons();
    const uint8_t  dpad    = controller->dpad();

    up_button    = (dpad & DPAD_UP)    != 0;
    down_button  = (dpad & DPAD_DOWN)  != 0;
    left_button  = (dpad & DPAD_LEFT)  != 0;
    right_button = (dpad & DPAD_RIGHT) != 0;

    a_button = (buttons & BUTTON_A) != 0;
    b_button = (buttons & BUTTON_B) != 0;
    x_button = (buttons & BUTTON_X) != 0;
    y_button = (buttons & BUTTON_Y) != 0;

    l1_button = (buttons & BUTTON_L1) != 0;
    r1_button = (buttons & BUTTON_R1) != 0;
    l2_button = (buttons & BUTTON_L2) != 0;
    r2_button = (buttons & BUTTON_R2) != 0;

    left_stick_button  = (buttons & BUTTON_THUMB_L) != 0;
    right_stick_button = (buttons & BUTTON_THUMB_R) != 0;

    menu_button = (buttons & BUTTON_MENU) != 0;

    axis_x  = controller->axisX();
    axis_y  = controller->axisY();
    axis_rx = controller->axisRX();
    axis_ry = controller->axisRY();
}
