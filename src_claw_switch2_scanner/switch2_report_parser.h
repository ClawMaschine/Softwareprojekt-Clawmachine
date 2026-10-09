#pragma once

#include <stddef.h>
#include <stdint.h>

#include "switch2_controller_types.h"

// ============================================================================
// Auswertung eines rohen Input-Reports.
//
// Dieses Modul rechnet nur: Bytes rein, Zahlen und Tastenzustaende raus. Es
// kennt kein Bluetooth, kein MQTT und keine Konsolenausgabe und ist deshalb
// das einzige Modul, das sich ohne Hardware nachrechnen laesst.
// ============================================================================

// --- Stickwerte -------------------------------------------------------------

// Zwei 12-Bit-Werte stecken in drei Bytes.
static constexpr size_t STICK_PACKED_BYTE_COUNT = 3;

// Die Vollauslenkung ist aus eigenen Messungen abgeleitet: gemessen wurden
// 1176 (rechts), 1196 (links), 1244 (oben), 1155 (unten) Zaehlwerte ab
// Ruhelage. 1150 liegt knapp darunter, damit jede Richtung sicher 100 %
// erreicht; darueber wird begrenzt.
static constexpr int16_t STICK_FULL_DEFLECTION_COUNTS = 1150;
static constexpr int8_t  STICK_DEADZONE_PERCENT       = 10;

struct StickPosition
{
  uint16_t x;
  uint16_t y;
};

// Vorzeichenbehaftete Auslenkung in Prozent. Positiv = rechts bzw. oben.
// Die Achsenrichtung ist am Geraet gemessen: steigendes X = rechts,
// steigendes Y = oben.
struct StickDeflection
{
  int8_t horizontalPercent;
  int8_t verticalPercent;
};

// Wo im Report der Stick liegt und wo seine Ruhelage gemessen wurde. Die
// Ruhelage liegt geraeteabhaengig deutlich neben den theoretischen 0x800
// (gemessen: 0x860 / 0x788) und darf deshalb nicht angenommen werden - ohne
// hasCenter ist keine Auswertung moeglich.
struct StickCalibration
{
  uint8_t       offset;  // NO_STICK_OFFSET = kein Stick an bekannter Position
  StickPosition center;
  bool          hasCenter;
};

// Sticks sind als zwei 12-Bit-Werte in drei Bytes gepackt, Ruhelage 0x800.
// Gleiches Schema wie bei den Switch-1-Joy-Cons.
StickPosition unpackPackedStickValues(const uint8_t *bytes);

// Rohwert einer Achse in vorzeichenbehaftete Prozent umrechnen, begrenzt auf
// -100..+100. Positiv = rechts bzw. oben.
int8_t convertAxisToPercent(uint16_t rawValue, uint16_t centerValue);

bool isInsideDeadzone(int8_t percent);

// Liest den kalibrierten Stick aus dem Report. false = Stick unbekannt, nicht
// kalibriert, oder der Report ist zu kurz; deflection bleibt dann unberuehrt.
bool readStickDeflection(const uint8_t         *report,
                         size_t                 length,
                         const StickCalibration &stick,
                         StickDeflection       &deflection);

// --- Steuerbelegung beider Joy-Cons -----------------------------------------
//
// Beide Controller nutzen DIESELBEN Bitmasken - Nintendo hat die Tasten
// identisch angeordnet. Das ist an beiden Geraeten gemessen:
//
//   Maske   linker Joy-Con        rechter Joy-Con        Lage am Geraet
//   0x01    Steuerkreuz Unten     B                      unten
//   0x02    Steuerkreuz Rechts    A                      rechts
//   0x04    Steuerkreuz Links     Y                      links
//   0x08    Steuerkreuz Oben      X                      oben
//   0x10    L                     R                      obere Schulter
//   0x20    ZL                    ZR                     hintere Schulter
//
// Deshalb genuegt eine Zuordnungsfunktion fuer beide Seiten.

static constexpr uint8_t JOYCON_BUTTON_BYTE          = 0x02;
static constexpr uint8_t JOYCON_MASK_DIRECTION_DOWN  = 0x01;
static constexpr uint8_t JOYCON_MASK_DIRECTION_RIGHT = 0x02;
static constexpr uint8_t JOYCON_MASK_DIRECTION_LEFT  = 0x04;
static constexpr uint8_t JOYCON_MASK_DIRECTION_UP    = 0x08;
static constexpr uint8_t JOYCON_MASK_SHOULDER_UPPER  = 0x10;  // L bzw. R
static constexpr uint8_t JOYCON_MASK_SHOULDER_LOWER  = 0x20;  // ZL bzw. ZR

// Ab welcher Auslenkung der Stick als Richtung gilt. Darunter passiert nichts.
static constexpr int8_t JOYCON_STICK_DIRECTION_THRESHOLD_PERCENT = 30;

// Feldnamen und Bedeutung sind bewusst identisch zu PanelInput, damit der
// Python-Server dieselbe Auswertung nutzen kann (siehe on_control_command in
// python_server/clawmachine/claw_machine.py).
struct JoyConControlState
{
  bool upButton    = false;
  bool downButton  = false;
  bool leftButton  = false;
  bool rightButton = false;
  bool frontButton = false;
  bool backButton  = false;

  // Gegensaetzliche Richtungen derselben Achse gleichzeitig ergeben keinen
  // sinnvollen Motorbefehl. Spiegelt PanelInput::isValid().
  bool isValid() const
  {
    if (leftButton && rightButton) return false;
    if (frontButton && backButton) return false;
    if (upButton && downButton)    return false;
    return true;
  }

  bool equals(const JoyConControlState &other) const
  {
    return upButton == other.upButton && downButton == other.downButton &&
           leftButton == other.leftButton && rightButton == other.rightButton &&
           frontButton == other.frontButton && backButton == other.backButton;
  }
};

// Tasten und Stick eines Joy-Con in Steuerbefehle uebersetzen. Der Stick wirkt
// wie ein zusaetzliches Steuerkreuz: ab der Schwelle gilt seine Richtung als
// gedrueckt, damit er ohne Aenderung in das an/aus-Format des Panels passt.
//
// Waagerecht stimmen Tastenname und Bewegung ueberein: links am Controller
// bewegt den Greifer nach links. Die senkrechte Achse ist dagegen bewusst
// gespiegelt, Richtung oben bewegt den Greifer nach hinten. Stick und Tasten
// folgen derselben Konvention, damit sich beides gleich anfuehlt.
JoyConControlState readControlStateFromReport(const uint8_t          *report,
                                              size_t                  length,
                                              const StickCalibration &stick);
