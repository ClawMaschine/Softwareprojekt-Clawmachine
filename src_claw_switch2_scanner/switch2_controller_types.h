#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Controllertypen und ihre geraetespezifischen Eigenschaften.
//
// Nintendo stellt kein Typfeld bereit. Der Typ ergibt sich daraus, WELCHE
// Characteristic auf Handle 0x000E existiert - die UUID ist pro Controllertyp
// eindeutig. Genau deshalb koennen wir linken und rechten Joy-Con sauber
// auseinanderhalten, obwohl sie im Advertisement nahezu identisch aussehen.
//
// Die Tastennamen-Tabellen selbst liegen bewusst in switch2_controller_types.cpp
// und sind von aussen nicht sichtbar: sie werden ausschliesslich ueber die
// Eintraege in CONTROLLER_SPECIFIC_REPORTS erreicht.
// ============================================================================

enum class Switch2ControllerType
{
  Unknown,
  JoyConLeft,
  JoyConRight,
  ProController,
  GameCube
};

// Ein Bit in einem Report und der Name der Taste, die es bedeutet. Reine
// Anzeigeinformation - an diesen Eintraegen haengt kein Verhalten.
struct Switch2ButtonMapping
{
  uint8_t     byteOffset;
  uint8_t     bitMask;
  const char *buttonName;
};

static constexpr uint8_t NO_STICK_OFFSET = 0xFF;

// Alles, was pro Controllertyp unterschiedlich ist: die vier UUIDs, die Lage
// der Sticks im Report und die Tastenbelegung.
struct Switch2ReportDescription
{
  Switch2ControllerType type;
  const char *characteristicUuid;
  const char *commandCharacteristicUuid;    // Handle 0x0016, Rumble + Befehle
  const char *vibrationCharacteristicUuid;  // Handle 0x0012, nur rohe Rumble-Daten
  const char *controllerName;
  const char *reportName;
  uint8_t firstStickOffset;  // NO_STICK_OFFSET = kein Stick an bekannter Position
  uint8_t secondStickOffset;
  const Switch2ButtonMapping *buttonMappings;  // nullptr = keine Belegung bekannt
  size_t buttonMappingCount;
  bool isButtonMappingVerified;
};

extern const Switch2ReportDescription CONTROLLER_SPECIFIC_REPORTS[];
extern const size_t                   CONTROLLER_SPECIFIC_REPORT_COUNT;

// Die A-Taste des rechten Joy-Con loest die Testvibration aus. Byte und Maske
// sind deckungsgleich mit dem Eintrag in der Tastentabelle des rechten
// Joy-Con und dort am Geraet gemessen. Bewusst eigene Konstanten statt eines
// Tabellenzugriffs: die Tabelle beschreibt Anzeigenamen, hier haengt
// Verhalten dran.
static constexpr uint8_t JOYCON_RIGHT_A_BUTTON_BYTE = 0x02;
static constexpr uint8_t JOYCON_RIGHT_A_BUTTON_MASK = 0x02;
