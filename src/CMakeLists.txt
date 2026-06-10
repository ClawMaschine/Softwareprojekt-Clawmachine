# ESP-IDF Main-Component für claw_player_input.
# Wird nur von framework=espidf genutzt – Arduino-Envs (claw_1, claw_3, claw_server) ignorieren diese Datei.
#
# REQUIRES: Teilt ESP-IDF mit, dass bluepad32/arduino/btstack als Komponenten eingebunden werden.
#           Ohne diese Zeile findet der Compiler <Bluepad32.h>, <Arduino.h> etc. nicht.
# INCLUDE_DIRS: Macht firmware_config.h (include/) und claw_mqtt_connection.h (lib/) sichtbar.
# SRCS-Glob: Kompiliert alle .c/.cpp aus src_claw_player_input/ und lib/claw_mqtt_connection/.
#
# PubSubClient: PlatformIO's LDF lädt lib_deps herunter, gibt die Pfade aber nicht an den
# ESP-IDF CMake-Build weiter. Deshalb werden PubSubClient-Quellen und -Headers hier explizit eingebunden.
# Der Glob funktioniert weil PlatformIO lib_deps vor dem CMake-Build herunterlädt.

file(GLOB player_input_srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/../src_claw_player_input/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src_claw_player_input/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../lib/claw_mqtt_connection/*.cpp")

file(GLOB pubsub_srcs
    "${CMAKE_SOURCE_DIR}/.pio/libdeps/claw_player_input/PubSubClient/src/*.cpp")

idf_component_register(
    SRCS ${player_input_srcs} ${pubsub_srcs}
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../lib/claw_mqtt_connection"
        "${CMAKE_SOURCE_DIR}/.pio/libdeps/claw_player_input/PubSubClient/src"
    REQUIRES "bluepad32" "bluepad32_arduino" "arduino" "btstack"
)
