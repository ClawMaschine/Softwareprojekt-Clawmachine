// ESP-IDF-Einstiegspunkt für Bluepad32 + Arduino.
//
// Warum main.c nötig ist:
//   CONFIG_AUTOSTART_ARDUINO=n → Arduino startet NICHT automatisch.
//   Stattdessen muss app_main() den BTstack initialisieren, Bluepad32 einrichten
//   und dann btstack_run_loop_execute() aufrufen – dieser Loop läuft für immer
//   und ruft intern setup()/loop() aus main.cpp (Arduino-Sketch) auf.
//
// Reihenfolge beim Boot:
//   ESP-IDF → app_main() → btstack_init() → uni_init() → btstack_run_loop_execute()
//                                                              ↓
//                                                     Arduino setup() / loop()

#include "sdkconfig.h"
#include <stddef.h>
#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_stdio_esp32.h>
#include <arduino_platform.h>
#include <uni.h>

int app_main(void) {
    // BTstack-UART-Konsole aktivieren (deaktiviert wenn Bluepad32-USB-Konsole aktiv ist,
    // da beide sonst auf dieselbe UART-Schnittstelle zugreifen würden).
#ifndef CONFIG_ESP_CONSOLE_UART_NONE
#ifndef CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
    btstack_stdio_init();
#endif
#endif

    btstack_init();

    // Arduino als Bluepad32-Platform setzen: ermöglicht setup()/loop()-Aufruf aus dem BTstack-Loop.
    uni_platform_set_custom(get_arduino_platform());

    uni_init(0, NULL);

    // Blockiert ab hier – BTstack-Loop läuft dauerhaft und ruft Arduino-Callbacks auf.
    btstack_run_loop_execute();
    return 0;
}
