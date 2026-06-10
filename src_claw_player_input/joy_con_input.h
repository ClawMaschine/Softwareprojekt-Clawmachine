#pragma once

#include <Bluepad32.h>

struct JoyConInput {
    // D-Pad
    bool up_button    = false;
    bool down_button  = false;
    bool left_button  = false;
    bool right_button = false;

    // Face buttons
    bool a_button = false;
    bool b_button = false;
    bool x_button = false;
    bool y_button = false;

    // Shoulder / trigger buttons
    bool l1_button = false;
    bool r1_button = false;
    bool l2_button = false;
    bool r2_button = false;

    // Stick clicks
    bool left_stick_button  = false;
    bool right_stick_button = false;

    // Menu
    bool menu_button = false;

    // Analog axes (-512 to 511)
    int axis_x  = 0;
    int axis_y  = 0;
    int axis_rx = 0;
    int axis_ry = 0;

    void update(ControllerPtr controller);
};
