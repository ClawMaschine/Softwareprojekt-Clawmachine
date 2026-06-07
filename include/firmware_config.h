#pragma once

#ifndef CLAW_CLIENT_WIFI_SSID
#define CLAW_CLIENT_WIFI_SSID "TP-LINK_9688"
#endif

#ifndef CLAW_CLIENT_WIFI_PASSWORD
#define CLAW_CLIENT_WIFI_PASSWORD "88103198"
#endif

#ifndef CLAW_SERVER_TCP_PORT
#define CLAW_SERVER_TCP_PORT 3333
#endif

#ifndef CLAW_CONNECTION_RETRY_INTERVAL_MS
#define CLAW_CONNECTION_RETRY_INTERVAL_MS 2000
#endif

#ifndef CLAW_MQTT_BROKER_HOST
#define CLAW_MQTT_BROKER_HOST "192.168.0.103"
#endif

#ifndef CLAW_MQTT_BROKER_PORT
#define CLAW_MQTT_BROKER_PORT 1883
#endif

#ifndef CLAW_MQTT_USERNAME
#define CLAW_MQTT_USERNAME "clawmachine"
#endif

#ifndef CLAW_MQTT_PASSWORD
#define CLAW_MQTT_PASSWORD "claw_secret"
#endif

#ifndef CLAW_CONTROL_PANEL_CLIENT_ID
#define CLAW_CONTROL_PANEL_CLIENT_ID "control_panel"
#endif

#ifndef CLAW_MOTOR_CONTROLLER_CLIENT_ID
#define CLAW_MOTOR_CONTROLLER_CLIENT_ID "motor_controller"
#endif

// Hardware panel button GPIO pins — an tatsächliche Verdrahtung anpassen
#ifndef CLAW_PANEL_PIN_UP
#define CLAW_PANEL_PIN_UP      32
#endif
#ifndef CLAW_PANEL_PIN_DOWN
#define CLAW_PANEL_PIN_DOWN    33
#endif
#ifndef CLAW_PANEL_PIN_LEFT
#define CLAW_PANEL_PIN_LEFT    25
#endif
#ifndef CLAW_PANEL_PIN_RIGHT
#define CLAW_PANEL_PIN_RIGHT   26
#endif
#ifndef CLAW_PANEL_PIN_GRAB
#define CLAW_PANEL_PIN_GRAB    27
#endif
#ifndef CLAW_PANEL_PIN_RELEASE
#define CLAW_PANEL_PIN_RELEASE 14
#endif
