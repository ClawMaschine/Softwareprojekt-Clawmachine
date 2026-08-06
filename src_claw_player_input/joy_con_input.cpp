#include "joy_con_input.h"

void JoyConInput::update(ControllerPtr controller)
{
    if (!controller || !controller->isConnected()) {
        return;
    }

    const uint8_t dpad = controller->dpad();

    up_button    = (dpad & DPAD_UP)    != 0;
    down_button  = (dpad & DPAD_DOWN)  != 0;
    left_button  = (dpad & DPAD_LEFT)  != 0;
    right_button = (dpad & DPAD_RIGHT) != 0;

    a_button = controller->a();
    b_button = controller->b();
    x_button = controller->x();
    y_button = controller->y();

    l1_button = controller->l1();
    r1_button = controller->r1();
    l2_button = controller->l2();
    r2_button = controller->r2();

    left_stick_button  = controller->thumbL();
    right_stick_button = controller->thumbR();

    menu_button = controller->miscStart();

    axis_x  = controller->axisX();
    axis_y  = controller->axisY();
    axis_rx = controller->axisRX();
    axis_ry = controller->axisRY();
}