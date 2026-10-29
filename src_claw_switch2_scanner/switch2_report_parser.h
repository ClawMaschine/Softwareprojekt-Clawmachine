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

// --- Akkustand --------------------------------------------------------------
//
// Byte 0x01 jedes typspezifischen Reports (0x07 links, 0x08 rechts) ist ein
// Bitfeld mit der Energieversorgung. Es kommt ohne Zutun mit jedem Report, es
// braucht also keine Abfrage und kein Feature-Bit:
//
//   Bit 0     externe Spannung liegt an
//   Bit 1     laedt
//   Bit 2-5   Ladestufe 0-9
//   Bit 6-7   reserviert
//
// Quelle: hid_reports.md (ndeadly/switch2_controller_research), Report 0x07
// und 0x08 jeweils Offset 0x1. Dazu passt die eigene Messung des Projekts, die
// Byte 0x01 als unveraenderlich gesehen hat - ein Akkustand aendert sich selten.

static constexpr uint8_t JOYCON_POWER_INFO_BYTE = 0x01;

static constexpr uint8_t JOYCON_POWER_MASK_EXTERNAL_POWER = 0x01;
static constexpr uint8_t JOYCON_POWER_MASK_CHARGING       = 0x02;
static constexpr uint8_t JOYCON_POWER_BATTERY_LEVEL_SHIFT = 2;
static constexpr uint8_t JOYCON_POWER_BATTERY_LEVEL_MASK  = 0x0F;

// Der Controller meldet Zehntel. Hoechster Wert ist 9, der Ladestand erreicht
// also NIE 100 % - wer danach sucht, sucht umsonst.
static constexpr uint8_t JOYCON_POWER_PERCENT_PER_LEVEL = 10;
static constexpr uint8_t JOYCON_POWER_HIGHEST_LEVEL     = 9;

struct JoyConPowerState
{
  bool    hasExternalPower = false;
  bool    isCharging       = false;
  uint8_t batteryPercent   = 0;      // 0, 10, ... 90
  bool    isKnown          = false;  // false = Report zu kurz, Rest bedeutungslos

  bool equals(const JoyConPowerState &other) const
  {
    return isKnown == other.isKnown &&
           hasExternalPower == other.hasExternalPower &&
           isCharging == other.isCharging &&
           batteryPercent == other.batteryPercent;
  }
};

JoyConPowerState readPowerStateFromReport(const uint8_t *report, size_t length);

// --- Stickwerte -------------------------------------------------------------

// Zwei 12-Bit-Werte stecken in drei Bytes.
static constexpr size_t STICK_PACKED_BYTE_COUNT = 3;

// Die Vollauslenkung ist aus eigenen Messungen abgeleitet: gemessen wurden
// 1176 (rechts), 1196 (links), 1244 (oben), 1155 (unten) Zaehlwerte ab
// Ruhelage. 1150 liegt knapp darunter, damit jede Richtung sicher 100 %
// erreicht; darueber wird begrenzt.
static constexpr int16_t STICK_FULL_DEFLECTION_COUNTS = 1150;
static constexpr int8_t  STICK_DEADZONE_PERCENT       = 10;

// Theoretische Ruhelage eines 12-Bit-Sticks. Die echte liegt geraeteabhaengig
// daneben (gemessen: 0x860 und 0x788, also rund 100 Zaehlwerte), weshalb sie
// vermessen werden muss - aber eben nicht beliebig weit daneben.
static constexpr uint16_t STICK_THEORETICAL_CENTER = 0x800;

// Wie weit die vermessene Ruhelage hoechstens von der theoretischen abweichen
// darf. Wird waehrend der Lernphase am Stick gewackelt, kommt eine viel
// groessere Abweichung heraus (gemessen: 0xA39, also 569 Zaehlwerte daneben) -
// und die Achse faehrt danach im Ruhezustand dauerhaft los, weil der
// Scheinausschlag ueber der Richtungsschwelle liegt.
//
// 200 Zaehlwerte sind etwa 17 % Ausschlag: deutlich mehr als die gemessenen
// Geraeteabweichungen, aber deutlich unter der Richtungsschwelle von 30 %.
static constexpr int16_t STICK_CENTER_PLAUSIBLE_DEVIATION_COUNTS = 200;

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

// Das Tastenfeld ist zwei Byte breit (hid_reports.md: Offset 0x2, Groesse 0x2).
// Das zweite Byte traegt Home, C, SL und SR - fuer die Steuerung unbenutzt,
// aber es zaehlt als Bedienung.
static constexpr size_t  JOYCON_BUTTON_BYTE_COUNT    = 2;
static constexpr uint8_t JOYCON_MASK_DIRECTION_DOWN  = 0x01;
static constexpr uint8_t JOYCON_MASK_DIRECTION_RIGHT = 0x02;
static constexpr uint8_t JOYCON_MASK_DIRECTION_LEFT  = 0x04;
static constexpr uint8_t JOYCON_MASK_DIRECTION_UP    = 0x08;
static constexpr uint8_t JOYCON_MASK_SHOULDER_UPPER  = 0x10;  // L bzw. R
static constexpr uint8_t JOYCON_MASK_SHOULDER_LOWER  = 0x20;  // ZL bzw. ZR

// Ab welcher Auslenkung der Stick als Richtung gilt. Darunter passiert nichts.
// Darueber wird die Auslenkung auf 0-100 % umgerechnet (Schwelle = 0 %,
// Anschlag = 100 %), damit die Geschwindigkeit weich einsetzt.
static constexpr int8_t JOYCON_STICK_DIRECTION_THRESHOLD_PERCENT = 30;

// Eine einmal erkannte Stickrichtung bleibt aktiv, bis die Auslenkung unter
// diese (niedrigere) Schwelle faellt. Ohne diese Hysterese wechselt der Wert an
// der Schwelle staendig zwischen 0 und klein, und die Achse zuckt.
static constexpr int8_t JOYCON_STICK_RELEASE_THRESHOLD_PERCENT = 24;

// Werte werden auf Vielfache davon gerundet. 1 = volle Aufloesung; die Anzahl
// der MQTT-Nachrichten begrenzt das Sendemodul (joycon_control_output), nicht
// die Rundung hier.
static constexpr uint8_t JOYCON_PERCENT_STEP = 1;

// Tasten zaehlen als voll gedrueckt.
static constexpr uint8_t JOYCON_BUTTON_PRESSED_PERCENT = 100;

// Feldnamen entsprechen PanelInput, der Python-Server wertet beide gemeinsam
// aus (siehe on_control_command in python_server/clawmachine/claw_machine.py).
// Anders als beim Panel (0/1) steht pro Richtung ein Wert von 0 bis 100
// (Prozent): Tasten 100, beim Stick die umgerechnete Auslenkung.
struct JoyConControlState
{
  uint8_t upPercent    = 0;
  uint8_t downPercent  = 0;
  uint8_t leftPercent  = 0;
  uint8_t rightPercent = 0;
  uint8_t frontPercent = 0;
  uint8_t backPercent  = 0;
  // Klaue langsam auf/zu, solange die Taste gehalten wird (0 oder 100)
  uint8_t clawOpenPercent  = 0;
  uint8_t clawClosePercent = 0;

  // Gegensaetzliche Richtungen derselben Achse gleichzeitig ergeben keinen
  // sinnvollen Motorbefehl. Spiegelt PanelInput::isValid().
  bool isValid() const
  {
    if (leftPercent > 0 && rightPercent > 0) return false;
    if (frontPercent > 0 && backPercent > 0) return false;
    if (upPercent > 0 && downPercent > 0)    return false;
    if (clawOpenPercent > 0 && clawClosePercent > 0) return false;
    return true;
  }

  // true, wenn mindestens eine Richtung von/auf 0 wechselt (Start/Stopp).
  bool hasStartOrStopComparedTo(const JoyConControlState &other) const
  {
    return (upPercent == 0) != (other.upPercent == 0) ||
           (downPercent == 0) != (other.downPercent == 0) ||
           (leftPercent == 0) != (other.leftPercent == 0) ||
           (rightPercent == 0) != (other.rightPercent == 0) ||
           (frontPercent == 0) != (other.frontPercent == 0) ||
           (backPercent == 0) != (other.backPercent == 0) ||
           (clawOpenPercent == 0) != (other.clawOpenPercent == 0) ||
           (clawClosePercent == 0) != (other.clawClosePercent == 0);
  }

  // Groesste Aenderung einer einzelnen Richtung gegenueber other (in
  // Prozentpunkten).
  uint8_t largestChangeComparedTo(const JoyConControlState &other) const;

  // Nichts gedrueckt, nichts ausgelenkt.
  bool isNeutral() const
  {
    return upPercent == 0 && downPercent == 0 && leftPercent == 0 && rightPercent == 0 &&
           frontPercent == 0 && backPercent == 0 &&
           clawOpenPercent == 0 && clawClosePercent == 0;
  }

  bool equals(const JoyConControlState &other) const
  {
    return upPercent == other.upPercent && downPercent == other.downPercent &&
           leftPercent == other.leftPercent && rightPercent == other.rightPercent &&
           frontPercent == other.frontPercent && backPercent == other.backPercent &&
           clawOpenPercent == other.clawOpenPercent && clawClosePercent == other.clawClosePercent;
  }
};

// Tasten und Stick eines Joy-Con in Steuerbefehle uebersetzen.
//
// Welche Taste was bedeutet, haengt am Controllertyp - anders als die
// Bitmasken, die auf beiden Seiten gleich liegen:
//
//   linker Joy-Con:   Steuerkreuz und Stick fahren die Flaeche (X/Y),
//                     Schultertasten oeffnen die Klaue
//   rechter Joy-Con:  nur der Stick fahrt senkrecht (Z), die Tasten A/B/X/Y
//                     bleiben unbelegt, Schultertasten schliessen die Klaue
//
// Stick und Steuerkreuz wirken zusammen: je Richtung gilt der hoehere Wert.
//
// X und Y sind bewusst gespiegelt: Richtung rechts am Controller liefert
// "left", Richtung links liefert "right", und oben bewegt den Greifer nach
// hinten. Stick und Tasten folgen derselben Konvention, damit sich beides
// gleich anfuehlt.
//
// Z ist gegenueber den Namen vertauscht: Der Server rechnet z = up - down, und
// ein positiver Z-Wert senkt die Klaue ab (Endstop z1 sitzt oben). Das Panel
// ist genauso ueber getauschte Pins (CLAW_PANEL_PIN_UP/DOWN) angepasst.
// Deshalb hebt der Stick nach oben die Klaue ("down" = negativer Z-Wert) und
// nach unten senkt er sie ("up").
//
// previousState ist der zuletzt ermittelte Zustand DIESES Controllers und
// liefert die Hysterese: war eine Richtung schon aktiv, gilt fuer sie die
// niedrigere Freigabeschwelle.
JoyConControlState readControlStateFromReport(Switch2ControllerType     controllerType,
                                              const uint8_t            *report,
                                              size_t                    length,
                                              const StickCalibration   &stick,
                                              const JoyConControlState &previousState);
