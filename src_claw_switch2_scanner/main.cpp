#include <Arduino.h>
#include <NimBLEDevice.h>

#include <string.h>

#include "claw_mqtt_connection.h"
#include "firmware_config.h"

// ============================================================================
// Stufe 1: Rohsignale eines Nintendo-Switch-2-Controllers abfangen.
//
// Diese Firmware trifft BEWUSST KEINE ANNAHME darueber, welches Bit zu welcher
// Taste gehoert. Sie verbindet sich mit dem Controller, abonniert dessen
// Input-Reports und gibt aus, WELCHE BYTES UND BITS sich aendern. Die
// Zuordnung "Bit -> physische Taste" entsteht durch Druecken und Ablesen und
// wird anschliessend in docs/switch2_joycon_mapping.md festgehalten.
//
// Aus der Reverse-Engineering-Doku uebernommen sind nur die Dinge, die sich
// nicht durch Druecken herausfinden lassen: UUIDs, Handles, das Prinzip der
// 12-Bit-Stickpackung und die Regel "kein SMP-Pairing".
//
// Quellen:
// - https://github.com/ndeadly/switch2_controller_research
//   (bluetooth_interface.md, hid_reports.md, commands.md)
// - https://gist.github.com/ndeadly/7d27aa63e2f653a902a2474dbcbc08b3
// - https://qiita.com/Tsukusim/items/5a88b76a2e8e0e69e412
//   (funktionierender NimBLE-Nachbau auf einem normalen ESP32)
//
// Warum nicht Bluepad32: Switch-2-Controller sprechen kein Bluetooth Classic
// HID mehr, sondern BLE mit einem proprietaeren GATT-Protokoll ohne HID over
// GATT. Bluepad32 unterstuetzt ausschliesslich BR/EDR-Nintendo-Geraete.
//
// WICHTIG: Es darf niemals SMP-Pairing initiiert werden. Laut Doku trennt der
// Controller die Verbindung sofort, wenn der Host das versucht. Deshalb
// setSecurityAuth(false, false, false) und kein secureConnection().
//
// Zusaetzlich sendet diese Firmware die Steuerbefehle des LINKEN Joy-Con per
// MQTT an den Python-Server - im selben Format, das der Server vom Control-
// Panel kennt, sodass dort keine neue Auswertelogik noetig ist. Das ist
// bewusst dieselbe Rolle, die src_claw_panel_test beim Panel hatte: eine
// Testfirmware, die auf das echte Topic sendet. Der Umbau nach
// src_claw_player_input kommt spaeter.
//
// Serielle Kommandos zur Laufzeit (kein Neuflashen noetig):
//   b = Tastennamen / rohe Bitmasken
//   d = kompletten Hexdump jedes Reports an/aus
//   r = Referenzwerte und Rauschliste zuruecksetzen (Controller ruhig halten)
//   s = Stick-Dekodierung an/aus
//   h = Hilfe
// ============================================================================

// --- Protokollkonstanten (bluetooth_interface.md) ---------------------------

static constexpr uint16_t NINTENDO_MANUFACTURER_ID  = 0x0553;
static constexpr uint16_t NINTENDO_VENDOR_ID        = 0x057E;
static constexpr uint16_t SWITCH2_LOWEST_PRODUCT_ID = 0x2060;

static constexpr size_t MANUFACTURER_DATA_MINIMUM_LENGTH  = 0x13;
static constexpr size_t MANUFACTURER_DATA_VENDOR_OFFSET   = 0x05;
static constexpr size_t MANUFACTURER_DATA_PRODUCT_OFFSET  = 0x07;
static constexpr size_t MANUFACTURER_DATA_WAKE_FLAG_OFFSET = 0x0B;
static constexpr size_t MANUFACTURER_DATA_HOST_ADDRESS_OFFSET = 0x0C;
static constexpr size_t MANUFACTURER_DATA_HOST_ADDRESS_LENGTH = 6;

// Service, unter dem alle Input-Reports haengen (GATT-Handles 0x0008-0x002a).
static const char *SWITCH2_INPUT_SERVICE_UUID = "ab7de9be-89fe-49ad-828f-118f09df7fd0";

// Input Report 0x05 auf Handle 0x000A - bei ALLEN Controllertypen vorhanden.
static const char *COMMON_INPUT_REPORT_UUID = "ab7de9be-89fe-49ad-828f-118f09df7fd2";

// --- Controllertyp-Erkennung ------------------------------------------------
//
// Nintendo stellt kein Typfeld bereit. Der Typ ergibt sich daraus, WELCHE
// Characteristic auf Handle 0x000E existiert - die UUID ist pro Controllertyp
// eindeutig. Genau deshalb koennen wir linken und rechten Joy-Con sauber
// auseinanderhalten, obwohl sie im Advertisement nahezu identisch aussehen.

enum class Switch2ControllerType
{
  Unknown,
  JoyConLeft,
  JoyConRight,
  ProController,
  GameCube
};

// --- Tastenbelegung ---------------------------------------------------------
//
// Die Belegung des RECHTEN Joy-Con ist vollstaendig selbst gemessen (siehe
// docs/mapping.txt): alle zwoelf Bits, jedes einzelne deckungsgleich mit
// ndeadlys hid_reports.md.
//
// Die Belegung des LINKEN Joy-Con stammt bisher nur aus der Dokumentation und
// ist noch nicht am Geraet nachgeprueft. Sie wird deshalb beim Verbinden als
// unbestaetigt gekennzeichnet.

struct Switch2ButtonMapping
{
  uint8_t     byteOffset;
  uint8_t     bitMask;
  const char *buttonName;
};

// Report 0x08 - gemessen am Geraet
static const Switch2ButtonMapping JOYCON_RIGHT_BUTTON_MAPPINGS[] = {
    {0x02, 0x01, "B"},
    {0x02, 0x02, "A"},
    {0x02, 0x04, "Y"},
    {0x02, 0x08, "X"},
    {0x02, 0x10, "R"},
    {0x02, 0x20, "ZR"},
    {0x02, 0x40, "Plus"},
    {0x02, 0x80, "Stick-Klick"},
    {0x03, 0x01, "Home"},
    {0x03, 0x10, "C"},
    {0x03, 0x40, "SR"},
    {0x03, 0x80, "SL"},
};

// Report 0x07 - aus hid_reports.md, noch nicht am Geraet geprueft
static const Switch2ButtonMapping JOYCON_LEFT_BUTTON_MAPPINGS[] = {
    {0x02, 0x01, "Unten"},
    {0x02, 0x02, "Rechts"},
    {0x02, 0x04, "Links"},
    {0x02, 0x08, "Oben"},
    {0x02, 0x10, "L"},
    {0x02, 0x20, "ZL"},
    {0x02, 0x40, "Minus"},
    {0x02, 0x80, "Stick-Klick"},
    {0x03, 0x01, "Capture"},
    {0x03, 0x40, "SR"},
    {0x03, 0x80, "SL"},
};

#define ARRAY_ELEMENT_COUNT(array) (sizeof(array) / sizeof((array)[0]))

// --- Steuerbelegung des linken Joy-Con --------------------------------------
//
// Dieselben Bitmasken wie in JOYCON_LEFT_BUTTON_MAPPINGS, hier nur unter dem
// Namen, den die Clawmachine-Achse traegt. Steuerkreuz auf die horizontale
// Ebene, L und ZL auf die Hoehe.

static constexpr uint8_t JOYCON_LEFT_BUTTON_BYTE      = 0x02;
static constexpr uint8_t JOYCON_LEFT_MASK_DPAD_DOWN   = 0x01;
static constexpr uint8_t JOYCON_LEFT_MASK_DPAD_RIGHT  = 0x02;
static constexpr uint8_t JOYCON_LEFT_MASK_DPAD_LEFT   = 0x04;
static constexpr uint8_t JOYCON_LEFT_MASK_DPAD_UP     = 0x08;
static constexpr uint8_t JOYCON_LEFT_MASK_SHOULDER_L  = 0x10;
static constexpr uint8_t JOYCON_LEFT_MASK_SHOULDER_ZL = 0x20;

static constexpr const char *JOYCON_CONTROL_TOPIC = "clawmachine/player_input/joycon";

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

static JoyConControlState readControlStateFromLeftJoyCon(const uint8_t *report, size_t length)
{
  JoyConControlState state;
  if (length <= JOYCON_LEFT_BUTTON_BYTE)
  {
    return state;
  }

  const uint8_t buttons = report[JOYCON_LEFT_BUTTON_BYTE];

  state.rightButton = (buttons & JOYCON_LEFT_MASK_DPAD_RIGHT) != 0;
  state.leftButton  = (buttons & JOYCON_LEFT_MASK_DPAD_LEFT) != 0;
  // Steuerkreuz oben schiebt den Greifer von der Bedienperson weg.
  state.backButton  = (buttons & JOYCON_LEFT_MASK_DPAD_UP) != 0;
  state.frontButton = (buttons & JOYCON_LEFT_MASK_DPAD_DOWN) != 0;
  state.upButton    = (buttons & JOYCON_LEFT_MASK_SHOULDER_L) != 0;
  state.downButton  = (buttons & JOYCON_LEFT_MASK_SHOULDER_ZL) != 0;

  return state;
}

ClawMqttConnection mqttConnection(
    CLAW_CLIENT_WIFI_SSID,
    CLAW_CLIENT_WIFI_PASSWORD,
    CLAW_MQTT_BROKER_HOST,
    CLAW_MQTT_BROKER_PORT,
    "claw_switch2_scanner",
    CLAW_MQTT_USER_USERNAME,
    CLAW_MQTT_USER_PASSWORD,
    CLAW_CONNECTION_RETRY_INTERVAL_MS);

// Der Report-Callback laeuft im NimBLE-Host-Task, maintainConnection() im
// Arduino-Task. PubSubClient ist nicht threadsicher, deshalb wird im Callback
// nur der Zustand hinterlegt und im loop() publiziert - dasselbe Muster wie
// bei pendingConnectAddress.
static JoyConControlState pendingControlState;
static volatile bool      hasPendingControlState = false;

struct Switch2ReportDescription
{
  Switch2ControllerType type;
  const char *characteristicUuid;
  const char *controllerName;
  const char *reportName;
  uint8_t firstStickOffset;  // 0xFF = kein Stick an bekannter Position
  uint8_t secondStickOffset;
  const Switch2ButtonMapping *buttonMappings;  // nullptr = keine Belegung bekannt
  size_t buttonMappingCount;
  bool isButtonMappingVerified;
};

static constexpr uint8_t NO_STICK_OFFSET = 0xFF;

static const Switch2ReportDescription CONTROLLER_SPECIFIC_REPORTS[] = {
    {Switch2ControllerType::JoyConLeft, "cc1bbbb5-7354-4d32-a716-a81cb241a32a",
     "Joy-Con 2 (L)", "Report 0x07", 0x05, NO_STICK_OFFSET,
     JOYCON_LEFT_BUTTON_MAPPINGS, ARRAY_ELEMENT_COUNT(JOYCON_LEFT_BUTTON_MAPPINGS), false},
    {Switch2ControllerType::JoyConRight, "d5a9e01e-2ffc-4cca-b20c-8b67142bf442",
     "Joy-Con 2 (R)", "Report 0x08", 0x05, NO_STICK_OFFSET,
     JOYCON_RIGHT_BUTTON_MAPPINGS, ARRAY_ELEMENT_COUNT(JOYCON_RIGHT_BUTTON_MAPPINGS), true},
    {Switch2ControllerType::ProController, "7492866c-ec3e-4619-8258-32755ffcc0f8",
     "Pro Controller 2", "Report 0x09", 0x05, 0x08,
     nullptr, 0, false},
    {Switch2ControllerType::GameCube, "8261cba1-9435-420c-84d6-f0c75a2c8e4d",
     "NSO GameCube Controller", "Report 0x0A", 0x05, NO_STICK_OFFSET,
     nullptr, 0, false},
};

static constexpr size_t CONTROLLER_SPECIFIC_REPORT_COUNT =
    sizeof(CONTROLLER_SPECIFIC_REPORTS) / sizeof(CONTROLLER_SPECIFIC_REPORTS[0]);

// Report 0x05 fuehrt beide Sticks in einem gemeinsamen Feld (hid_reports.md).
static constexpr uint8_t COMMON_REPORT_LEFT_STICK_OFFSET  = 0x0A;
static constexpr uint8_t COMMON_REPORT_RIGHT_STICK_OFFSET = 0x0D;

// --- Laufzeitschalter -------------------------------------------------------

// Zusaetzlich zum controllerspezifischen Report auch den gemeinsamen Report
// 0x05 abonnieren. War im Bring-up die Rueckfallebene fuer den Fall, dass der
// typspezifische Report keine Daten liefert. Das ist widerlegt - beide Joy-Cons
// liefern auf Handle 0x000E zuverlaessig. Bleibt als Schalter stehen, ist aber
// aus, weil das Abo nur den Datenverkehr und die Konsolenausgabe verdoppelt.
#define SUBSCRIBE_TO_COMMON_INPUT_REPORT 0

static bool shouldPrintFullHexDump  = false;
static bool shouldPrintStickValues  = true;
// true = Klartextnamen, false = rohe Bytes und Bitmasken (zum Nachmessen)
static bool shouldPrintButtonNames  = true;

// --- Report-Verfolgung ------------------------------------------------------
//
// Pro abonnierter Characteristic wird der letzte Report aufgehoben und mit dem
// naechsten verglichen. Bytes, die sich im Ruhezustand staendig aendern
// (Zaehler, Motion-Daten, Stick-Jitter), werden in einer Lernphase automatisch
// als Rauschen markiert und danach aus der Diff-Ausgabe ausgeblendet - sonst
// waere die Konsole unbrauchbar. Der Vollhexdump ('d') zeigt trotzdem alles.

static constexpr size_t   MAXIMUM_REPORT_LENGTH        = 64;
static constexpr uint16_t NOISE_LEARNING_SAMPLE_COUNT  = 100;
static constexpr uint8_t  NOISE_CHANGE_PERCENT_THRESHOLD = 25;

// Stick-Auswertung. Die Vollauslenkung ist aus eigenen Messungen abgeleitet:
// gemessen wurden 1176 (rechts), 1196 (links), 1244 (oben), 1155 (unten)
// Zaehlwerte ab Ruhelage. 1150 liegt knapp darunter, damit jede Richtung
// sicher 100 % erreicht; darueber wird begrenzt.
static constexpr int16_t STICK_FULL_DEFLECTION_COUNTS = 1150;
static constexpr int8_t  STICK_DEADZONE_PERCENT       = 10;
static constexpr int8_t  STICK_PRINT_STEP_PERCENT     = 5;

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

struct ReportTracker
{
  const char *controllerName;
  const char *reportName;
  Switch2ControllerType controllerType;
  uint8_t firstStickOffset;
  uint8_t secondStickOffset;

  const Switch2ButtonMapping *buttonMappings;
  size_t                      buttonMappingCount;

  uint8_t previousReport[MAXIMUM_REPORT_LENGTH];
  size_t  previousReportLength;
  bool    hasPreviousReport;

  uint16_t learningSampleCount;
  uint16_t byteChangeCount[MAXIMUM_REPORT_LENGTH];
  bool     isNoisyByte[MAXIMUM_REPORT_LENGTH];
  bool     isNoiseLearned;

  // Mittellage wird waehrend derselben Lernphase mitgemittelt, in der auch das
  // Rauschen bestimmt wird. Die Ruhelage liegt geraeteabhaengig deutlich neben
  // den theoretischen 0x800 (gemessen: 0x860 / 0x788) und darf deshalb nicht
  // angenommen werden.
  uint32_t      stickCenterSumX;
  uint32_t      stickCenterSumY;
  uint16_t      stickCenterSampleCount;
  StickPosition stickCenter;
  bool          hasStickCenter;

  StickDeflection lastPrintedDeflection;
  bool            hasPrintedDeflection;

  uint32_t receivedReportCount;
};

static void resetReportTracker(ReportTracker &tracker)
{
  tracker.previousReportLength   = 0;
  tracker.hasPreviousReport      = false;
  tracker.learningSampleCount    = 0;
  tracker.isNoiseLearned         = false;
  tracker.stickCenterSumX        = 0;
  tracker.stickCenterSumY        = 0;
  tracker.stickCenterSampleCount = 0;
  tracker.hasStickCenter         = false;
  tracker.hasPrintedDeflection   = false;
  tracker.receivedReportCount    = 0;
  memset(tracker.byteChangeCount, 0, sizeof(tracker.byteChangeCount));
  memset(tracker.isNoisyByte, 0, sizeof(tracker.isNoisyByte));
}

// --- Verbindungsverwaltung --------------------------------------------------

static constexpr size_t MAXIMUM_CONTROLLER_SESSIONS = 3;

struct ControllerSession
{
  bool                  isInUse;
  NimBLEAddress         address;
  Switch2ControllerType type;
  ReportTracker         specificReportTracker;
  ReportTracker         commonReportTracker;
};

static ControllerSession controllerSessions[MAXIMUM_CONTROLLER_SESSIONS];

// --- Backoff und Log-Drosselung ---------------------------------------------
//
// Ohne Backoff wuerde nach jedem Fehlschlag sofort neu verbunden. Das ueber-
// flutet nicht nur den UART, sondern loest laut Praxisberichten genau den
// Cooldown aus, der den Controller minutenlang gar nicht mehr reagieren
// laesst. Ebenso wird jedes Advertisement desselben Geraets nur noch alle paar
// Sekunden geloggt.

static constexpr size_t   MAXIMUM_TRACKED_DEVICES         = 4;
static constexpr uint32_t CONNECT_RETRY_BASE_DELAY_MS     = 5000;
static constexpr uint32_t CONNECT_RETRY_MAXIMUM_DELAY_MS  = 30000;
static constexpr uint32_t SCAN_LOG_INTERVAL_MS            = 5000;

struct DeviceRecord
{
  bool          isInUse;
  NimBLEAddress address;
  uint8_t       failedConnectCount;
  uint32_t      nextConnectAllowedAtMs;
  uint32_t      lastScanLogAtMs;
  bool          hasLoggedOnce;
};

static DeviceRecord deviceRecords[MAXIMUM_TRACKED_DEVICES];

static DeviceRecord *findOrCreateDeviceRecord(const NimBLEAddress &address)
{
  for (size_t i = 0; i < MAXIMUM_TRACKED_DEVICES; i++)
  {
    if (deviceRecords[i].isInUse && deviceRecords[i].address == address)
    {
      return &deviceRecords[i];
    }
  }
  for (size_t i = 0; i < MAXIMUM_TRACKED_DEVICES; i++)
  {
    if (!deviceRecords[i].isInUse)
    {
      deviceRecords[i].isInUse                = true;
      deviceRecords[i].address                = address;
      deviceRecords[i].failedConnectCount     = 0;
      deviceRecords[i].nextConnectAllowedAtMs = 0;
      deviceRecords[i].lastScanLogAtMs        = 0;
      deviceRecords[i].hasLoggedOnce          = false;
      return &deviceRecords[i];
    }
  }
  return nullptr;
}

// Wird im NimBLE-Host-Task gesetzt und im Arduino-Task ausgewertet. Ein
// einzelner Slot genuegt: Controller senden ihre Advertisements wiederholt,
// ein verpasster Fund kommt im naechsten Intervall erneut.
static NimBLEAddress   pendingConnectAddress;
static volatile bool   hasPendingConnectAddress = false;

static ControllerSession *findSessionByAddress(const NimBLEAddress &address)
{
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    if (controllerSessions[i].isInUse && controllerSessions[i].address == address)
    {
      return &controllerSessions[i];
    }
  }
  return nullptr;
}

static ControllerSession *claimFreeSession(const NimBLEAddress &address)
{
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    if (!controllerSessions[i].isInUse)
    {
      controllerSessions[i].isInUse = true;
      controllerSessions[i].address = address;
      controllerSessions[i].type    = Switch2ControllerType::Unknown;
      resetReportTracker(controllerSessions[i].specificReportTracker);
      resetReportTracker(controllerSessions[i].commonReportTracker);
      return &controllerSessions[i];
    }
  }
  return nullptr;
}

// --- Hilfsfunktionen --------------------------------------------------------

static void printHexBytes(const uint8_t *bytes, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    Serial.printf("%02X", bytes[i]);
    if ((i % 8) == 7 && i + 1 < length)
    {
      Serial.print(' ');
    }
  }
}

// Sticks sind als zwei 12-Bit-Werte in drei Bytes gepackt, Ruhelage 0x800.
// Gleiches Schema wie bei den Switch-1-Joy-Cons.
static StickPosition unpackPackedStickValues(const uint8_t *bytes)
{
  StickPosition stick;
  stick.x = static_cast<uint16_t>(bytes[0] | ((bytes[1] & 0x0F) << 8));
  stick.y = static_cast<uint16_t>((bytes[1] >> 4) | (bytes[2] << 4));
  return stick;
}

// Die drei Bytes eines Sticks gehoeren nicht in die Diff-Ausgabe: sie werden
// separat als [STICK] dekodiert. Der Rauschfilter allein reicht dafuer nicht,
// weil das dritte Byte (obere Y-Bits) im Ruhezustand stabil bleibt und sich
// erst bei Bewegung aendert - es sahe sonst wie ein Tastendruck aus.
static bool isPartOfKnownStickField(const ReportTracker &tracker, size_t byteIndex)
{
  if (tracker.firstStickOffset != NO_STICK_OFFSET &&
      byteIndex >= tracker.firstStickOffset &&
      byteIndex < static_cast<size_t>(tracker.firstStickOffset) + 3)
  {
    return true;
  }
  if (tracker.secondStickOffset != NO_STICK_OFFSET &&
      byteIndex >= tracker.secondStickOffset &&
      byteIndex < static_cast<size_t>(tracker.secondStickOffset) + 3)
  {
    return true;
  }
  return false;
}

// Rohwert einer Achse in vorzeichenbehaftete Prozent umrechnen, begrenzt auf
// -100..+100. Positiv = rechts bzw. oben.
static int8_t convertAxisToPercent(uint16_t rawValue, uint16_t centerValue)
{
  const int32_t deflection = static_cast<int32_t>(rawValue) - static_cast<int32_t>(centerValue);
  int32_t percent = (deflection * 100) / STICK_FULL_DEFLECTION_COUNTS;
  if (percent > 100)
  {
    percent = 100;
  }
  if (percent < -100)
  {
    percent = -100;
  }
  return static_cast<int8_t>(percent);
}

static bool isInsideDeadzone(int8_t percent)
{
  return percent > -STICK_DEADZONE_PERCENT && percent < STICK_DEADZONE_PERCENT;
}

static void printLearnedNoiseBytes(const ReportTracker &tracker)
{
  Serial.printf("[BASE]  %s / %s: Referenz gesetzt. Ignorierte Rausch-Bytes:",
                tracker.controllerName, tracker.reportName);

  bool hasAnyNoisyByte = false;
  for (size_t i = 0; i < tracker.previousReportLength; i++)
  {
    if (tracker.isNoisyByte[i])
    {
      Serial.printf(" 0x%02X", static_cast<unsigned>(i));
      hasAnyNoisyByte = true;
    }
  }
  if (!hasAnyNoisyByte)
  {
    Serial.print(" keine");
  }
  Serial.println();
  Serial.printf("[BASE]  %s: Stick-Mitte gemessen bei x=0x%03X y=0x%03X\n",
                tracker.controllerName, tracker.stickCenter.x, tracker.stickCenter.y);
  Serial.println("[BASE]  Bitte jetzt Tasten einzeln druecken.");
}

// Stick als Richtung und prozentuale Auslenkung ausgeben. In der Mittellage
// wird bewusst gar nichts ausgegeben.
static void printStickDeflection(ReportTracker &tracker, const uint8_t *report, size_t length)
{
  if (!shouldPrintStickValues || tracker.firstStickOffset == NO_STICK_OFFSET ||
      !tracker.hasStickCenter)
  {
    return;
  }
  if (static_cast<size_t>(tracker.firstStickOffset) + 3 > length)
  {
    return;
  }

  const StickPosition raw = unpackPackedStickValues(&report[tracker.firstStickOffset]);

  StickDeflection deflection;
  deflection.horizontalPercent = convertAxisToPercent(raw.x, tracker.stickCenter.x);
  deflection.verticalPercent   = convertAxisToPercent(raw.y, tracker.stickCenter.y);

  const bool isHorizontalIdle = isInsideDeadzone(deflection.horizontalPercent);
  const bool isVerticalIdle   = isInsideDeadzone(deflection.verticalPercent);

  if (isHorizontalIdle && isVerticalIdle)
  {
    // Mittellage: nichts ausgeben. Merker zuruecksetzen, damit die naechste
    // Auslenkung sofort wieder eine Zeile erzeugt.
    tracker.hasPrintedDeflection = false;
    return;
  }

  // Drosselung: bei ~30 Reports/s wuerde sonst jede Zitterbewegung eine Zeile
  // erzeugen. Neu ausgegeben wird erst ab einer spuerbaren Aenderung.
  if (tracker.hasPrintedDeflection)
  {
    const int16_t horizontalChange =
        abs(deflection.horizontalPercent - tracker.lastPrintedDeflection.horizontalPercent);
    const int16_t verticalChange =
        abs(deflection.verticalPercent - tracker.lastPrintedDeflection.verticalPercent);

    if (horizontalChange < STICK_PRINT_STEP_PERCENT && verticalChange < STICK_PRINT_STEP_PERCENT)
    {
      return;
    }
  }

  Serial.printf("[STICK] %s:", tracker.controllerName);
  if (!isHorizontalIdle)
  {
    Serial.printf(" %s %d%%",
                  deflection.horizontalPercent > 0 ? "rechts" : "links",
                  abs(deflection.horizontalPercent));
  }
  if (!isVerticalIdle)
  {
    Serial.printf(" %s %d%%",
                  deflection.verticalPercent > 0 ? "oben" : "unten",
                  abs(deflection.verticalPercent));
  }
  Serial.println();

  tracker.lastPrintedDeflection = deflection;
  tracker.hasPrintedDeflection  = true;
}

// --- Kernstueck: Report vergleichen und Aenderungen ausgeben ----------------

static void handleIncomingReport(ReportTracker &tracker, const uint8_t *report, size_t length)
{
  const size_t usableLength = length > MAXIMUM_REPORT_LENGTH ? MAXIMUM_REPORT_LENGTH : length;
  tracker.receivedReportCount++;

  if (shouldPrintFullHexDump)
  {
    Serial.printf("[RAW]   %s / %s (%u Bytes): ",
                  tracker.controllerName, tracker.reportName, static_cast<unsigned>(length));
    printHexBytes(report, usableLength);
    Serial.println();
  }

  if (!tracker.hasPreviousReport)
  {
    memcpy(tracker.previousReport, report, usableLength);
    tracker.previousReportLength = usableLength;
    tracker.hasPreviousReport    = true;
    Serial.printf("[BASE]  %s / %s: erster Report empfangen (%u Bytes), lerne Rauschen...\n",
                  tracker.controllerName, tracker.reportName, static_cast<unsigned>(length));
    return;
  }

  // Lernphase: zaehlen, welche Bytes sich im Ruhezustand staendig aendern.
  if (!tracker.isNoiseLearned)
  {
    for (size_t i = 0; i < usableLength && i < tracker.previousReportLength; i++)
    {
      if (report[i] != tracker.previousReport[i])
      {
        tracker.byteChangeCount[i]++;
      }
    }

    // Dieselben Stichproben liefern die Stick-Mittellage. Der Controller liegt
    // in dieser Phase ruhig, also ist der Mittelwert genau die Ruhelage.
    if (tracker.firstStickOffset != NO_STICK_OFFSET &&
        static_cast<size_t>(tracker.firstStickOffset) + 3 <= usableLength)
    {
      const StickPosition raw = unpackPackedStickValues(&report[tracker.firstStickOffset]);
      tracker.stickCenterSumX += raw.x;
      tracker.stickCenterSumY += raw.y;
      tracker.stickCenterSampleCount++;
    }

    tracker.learningSampleCount++;
    memcpy(tracker.previousReport, report, usableLength);
    tracker.previousReportLength = usableLength;

    if (tracker.learningSampleCount >= NOISE_LEARNING_SAMPLE_COUNT)
    {
      for (size_t i = 0; i < tracker.previousReportLength; i++)
      {
        const uint32_t changePercent =
            (static_cast<uint32_t>(tracker.byteChangeCount[i]) * 100) / tracker.learningSampleCount;
        tracker.isNoisyByte[i] = changePercent >= NOISE_CHANGE_PERCENT_THRESHOLD;
      }

      if (tracker.stickCenterSampleCount > 0)
      {
        tracker.stickCenter.x =
            static_cast<uint16_t>(tracker.stickCenterSumX / tracker.stickCenterSampleCount);
        tracker.stickCenter.y =
            static_cast<uint16_t>(tracker.stickCenterSumY / tracker.stickCenterSampleCount);
        tracker.hasStickCenter = true;
      }

      tracker.isNoiseLearned = true;
      printLearnedNoiseBytes(tracker);
    }
    return;
  }

  // Auswertephase: nur noch die nicht-rauschenden Bytes melden.
  for (size_t i = 0; i < usableLength && i < tracker.previousReportLength; i++)
  {
    const uint8_t previousValue = tracker.previousReport[i];
    const uint8_t currentValue  = report[i];

    if (currentValue == previousValue || tracker.isNoisyByte[i] ||
        isPartOfKnownStickField(tracker, i))
    {
      continue;
    }

    uint8_t changedBits = static_cast<uint8_t>(previousValue ^ currentValue);

    // Bekannte Bits als Klartextnamen melden und aus der Restmaske entfernen.
    if (shouldPrintButtonNames && tracker.buttonMappings != nullptr)
    {
      for (size_t m = 0; m < tracker.buttonMappingCount; m++)
      {
        const Switch2ButtonMapping &mapping = tracker.buttonMappings[m];
        if (mapping.byteOffset != i || (changedBits & mapping.bitMask) == 0)
        {
          continue;
        }

        Serial.printf("[BTN]   %s: %s %s\n",
                      tracker.controllerName,
                      mapping.buttonName,
                      (currentValue & mapping.bitMask) != 0 ? "gedrueckt" : "losgelassen");
        changedBits = static_cast<uint8_t>(changedBits & ~mapping.bitMask);
      }
    }

    if (changedBits == 0)
    {
      continue;
    }

    // Was uebrig bleibt, ist entweder nicht zugeordnet oder die Klartextausgabe
    // ist abgeschaltet. In beiden Faellen roh melden - ein unbekanntes Signal
    // darf nicht stillschweigend verschwinden.
    const uint8_t setBits     = static_cast<uint8_t>(changedBits & currentValue);
    const uint8_t clearedBits = static_cast<uint8_t>(changedBits & previousValue);

    Serial.printf("[DIFF]  %s / %s: byte 0x%02X: 0x%02X -> 0x%02X",
                  tracker.controllerName, tracker.reportName,
                  static_cast<unsigned>(i), previousValue, currentValue);
    if (setBits != 0)
    {
      Serial.printf("   bits gesetzt: 0x%02X", setBits);
    }
    if (clearedBits != 0)
    {
      Serial.printf("   bits geloescht: 0x%02X", clearedBits);
    }
    Serial.println();
  }

  printStickDeflection(tracker, report, usableLength);

  // Nur der linke Joy-Con steuert die Maschine. Hier wird der Zustand lediglich
  // hinterlegt; publiziert wird im loop(), weil PubSubClient nicht threadsicher
  // ist und dieser Callback im NimBLE-Host-Task laeuft.
  if (tracker.controllerType == Switch2ControllerType::JoyConLeft)
  {
    pendingControlState    = readControlStateFromLeftJoyCon(report, usableLength);
    hasPendingControlState = true;
  }

  memcpy(tracker.previousReport, report, usableLength);
  tracker.previousReportLength = usableLength;
}

// --- Steuerbefehle an den Python-Server -------------------------------------

static void publishPendingControlState()
{
  if (!hasPendingControlState)
  {
    return;
  }

  const JoyConControlState state = pendingControlState;
  hasPendingControlState         = false;

  if (!state.isValid())
  {
    return;
  }

  // Nur bei Zustandswechsel senden. Bei rund 30 Reports pro Sekunde waere
  // zyklisches Publizieren eine Flut - und es entlastet die Antenne, die sich
  // WiFi und BLE teilen.
  static JoyConControlState lastPublishedState;
  static bool               hasPublishedOnce = false;

  if (hasPublishedOnce && state.equals(lastPublishedState))
  {
    return;
  }

  char payload[160];
  snprintf(payload, sizeof(payload),
           "{\"up\":%d,\"down\":%d,\"left\":%d,\"right\":%d,\"front\":%d,\"back\":%d}",
           state.upButton, state.downButton,
           state.leftButton, state.rightButton,
           state.frontButton, state.backButton);

  mqttConnection.publish(JOYCON_CONTROL_TOPIC, payload);
  Serial.printf("[MQTT]  %s -> %s\n", JOYCON_CONTROL_TOPIC, payload);

  lastPublishedState = state;
  hasPublishedOnce   = true;
}

// --- GATT-Baum ausgeben -----------------------------------------------------
//
// Pflicht, kein Luxus: laut Praxisberichten weichen die Characteristic-UUIDs
// zwischen Exemplaren ab, und bei Pro Controllern mit Headset-Firmware
// verschieben sich die Standard-Handles um +8.

static void printGattTree(NimBLEClient *client)
{
  const std::vector<NimBLERemoteService *> &services = client->getServices(true);

  Serial.printf("[GATT]  %u Services gefunden\n", static_cast<unsigned>(services.size()));

  for (NimBLERemoteService *service : services)
  {
    Serial.printf("[GATT]  Service %s  (0x%04X-0x%04X)\n",
                  service->getUUID().toString().c_str(),
                  service->getStartHandle(),
                  service->getEndHandle());

    for (NimBLERemoteCharacteristic *characteristic : service->getCharacteristics(true))
    {
      Serial.printf("[GATT]    Char %s  handle=0x%04X  ",
                    characteristic->getUUID().toString().c_str(),
                    characteristic->getHandle());
      if (characteristic->canRead())            Serial.print("READ ");
      if (characteristic->canWrite())           Serial.print("WRITE ");
      if (characteristic->canWriteNoResponse()) Serial.print("WRITE_NR ");
      if (characteristic->canNotify())          Serial.print("NOTIFY ");
      if (characteristic->canIndicate())        Serial.print("INDICATE ");
      Serial.println();
    }
  }
}

// --- Abonnieren -------------------------------------------------------------

static bool subscribeToReport(NimBLERemoteService         *service,
                              const char                  *characteristicUuid,
                              ReportTracker               &tracker,
                              const char                  *controllerName,
                              const char                  *reportName,
                              Switch2ControllerType        controllerType,
                              uint8_t                      firstStickOffset,
                              uint8_t                      secondStickOffset,
                              const Switch2ButtonMapping  *buttonMappings,
                              size_t                       buttonMappingCount)
{
  NimBLERemoteCharacteristic *characteristic = service->getCharacteristic(characteristicUuid);
  if (characteristic == nullptr)
  {
    return false;
  }
  if (!characteristic->canNotify())
  {
    Serial.printf("[SUB]   %s: Characteristic %s kann keine Notifications\n",
                  reportName, characteristicUuid);
    return false;
  }

  tracker.controllerName     = controllerName;
  tracker.reportName         = reportName;
  tracker.controllerType     = controllerType;
  tracker.firstStickOffset   = firstStickOffset;
  tracker.secondStickOffset  = secondStickOffset;
  tracker.buttonMappings     = buttonMappings;
  tracker.buttonMappingCount = buttonMappingCount;
  resetReportTracker(tracker);

  ReportTracker *trackerPointer = &tracker;
  const bool didSubscribe = characteristic->subscribe(
      true,
      [trackerPointer](NimBLERemoteCharacteristic *, uint8_t *data, size_t length, bool)
      {
        handleIncomingReport(*trackerPointer, data, length);
      });

  if (!didSubscribe)
  {
    Serial.printf("[SUB]   %s: subscribe() fehlgeschlagen\n", reportName);
    return false;
  }

  Serial.printf("[SUB]   Abonniert: %s auf handle 0x%04X\n",
                reportName, characteristic->getHandle());
  return true;
}

// --- Verbindungsaufbau ------------------------------------------------------

class Switch2ClientCallbacks : public NimBLEClientCallbacks
{
  void onConnect(NimBLEClient *client) override
  {
    Serial.printf("[CONN]  Verbunden mit %s\n", client->getPeerAddress().toString().c_str());
  }

  // Aufraeumen und Backoff passieren im loop()-Pfad, sobald connect() false
  // zurueckgibt. Hier wird nur geloggt, sonst wuerde beides doppelt laufen.
  void onConnectFail(NimBLEClient *client, int reason) override
  {
    Serial.printf("[CONN]  Verbindung zu %s fehlgeschlagen (reason=%d%s)\n",
                  client->getPeerAddress().toString().c_str(), reason,
                  reason == 574 ? " = HCI 0x3E, Verbindungsaufbau abgelehnt" : "");
  }

  void onDisconnect(NimBLEClient *client, int reason) override
  {
    Serial.printf("[CONN]  Getrennt von %s (reason=%d), scanne erneut...\n",
                  client->getPeerAddress().toString().c_str(), reason);

    ControllerSession *session = findSessionByAddress(client->getPeerAddress());
    if (session != nullptr)
    {
      session->isInUse = false;
    }
    NimBLEDevice::getScan()->start(0, false, true);
  }
};

static Switch2ClientCallbacks clientCallbacks;

static void connectToController(const NimBLEAddress &address)
{
  if (findSessionByAddress(address) != nullptr)
  {
    return;
  }

  ControllerSession *session = claimFreeSession(address);
  if (session == nullptr)
  {
    Serial.println("[CONN]  Keine freie Session mehr - Verbindung uebersprungen");
    return;
  }

  Serial.printf("[CONN]  Verbinde mit %s ...\n", address.toString().c_str());

  NimBLEClient *client = NimBLEDevice::createClient(address);
  if (client == nullptr)
  {
    Serial.println("[CONN]  createClient() fehlgeschlagen");
    session->isInUse = false;
    return;
  }

  client->setClientCallbacks(&clientCallbacks, false);
  // Nur bei Trennung selbst loeschen. Bei Verbindungsfehler NICHT - sonst
  // waere der Client bereits weg, wenn wir gleich getLastError() abfragen.
  client->setSelfDelete(true, false);
  client->setConnectTimeout(10000);
  client->setConnectRetries(1);
  // 15-30 ms Verbindungsintervall. Die Konsole nutzt 5 ms, das liegt unter dem
  // BLE-Minimum von 7,5 ms und ist mit ESP32 nicht erreichbar.
  client->setConnectionParams(12, 24, 0, 400);

  // Letzter Parameter erzwingt den MTU-Exchange. Ohne ihn bliebe die MTU bei
  // 23 und Notifications waeren auf 20 Bytes gekuerzt - die Reports sind bis
  // zu 63 Bytes lang.
  if (!client->connect(address, true, false, true))
  {
    Serial.printf("[CONN]  connect() fehlgeschlagen (lastError=%d)\n", client->getLastError());
    session->isInUse = false;
    NimBLEDevice::deleteClient(client);

    DeviceRecord *record = findOrCreateDeviceRecord(address);
    if (record != nullptr)
    {
      if (record->failedConnectCount < 255)
      {
        record->failedConnectCount++;
      }
      uint32_t retryDelayMs = CONNECT_RETRY_BASE_DELAY_MS * record->failedConnectCount;
      if (retryDelayMs > CONNECT_RETRY_MAXIMUM_DELAY_MS)
      {
        retryDelayMs = CONNECT_RETRY_MAXIMUM_DELAY_MS;
      }
      record->nextConnectAllowedAtMs = millis() + retryDelayMs;

      Serial.printf("[CONN]  Naechster Versuch fruehestens in %u s (Fehlversuch %u).\n",
                    static_cast<unsigned>(retryDelayMs / 1000),
                    static_cast<unsigned>(record->failedConnectCount));
      Serial.println("[CONN]  Sync-Taste erneut lang druecken hebt die Wartezeit sofort auf.");
    }

    NimBLEDevice::getScan()->start(0, false, true);
    return;
  }

  Serial.printf("[CONN]  Verbunden. MTU=%u\n", client->getMTU());

  DeviceRecord *record = findOrCreateDeviceRecord(address);
  if (record != nullptr)
  {
    record->failedConnectCount     = 0;
    record->nextConnectAllowedAtMs = 0;
  }

  printGattTree(client);

  NimBLERemoteService *inputService = client->getService(SWITCH2_INPUT_SERVICE_UUID);
  if (inputService == nullptr)
  {
    Serial.printf("[TYPE]  Service %s nicht gefunden - kein Switch-2-Controller?\n",
                  SWITCH2_INPUT_SERVICE_UUID);
    client->disconnect();
    return;
  }

  // Typerkennung: die erste existierende der vier UUIDs bestimmt Typ UND Parser.
  const Switch2ReportDescription *matchedReport = nullptr;
  for (size_t i = 0; i < CONTROLLER_SPECIFIC_REPORT_COUNT; i++)
  {
    if (inputService->getCharacteristic(CONTROLLER_SPECIFIC_REPORTS[i].characteristicUuid) != nullptr)
    {
      matchedReport = &CONTROLLER_SPECIFIC_REPORTS[i];
      break;
    }
  }

  if (matchedReport != nullptr)
  {
    session->type = matchedReport->type;
    Serial.printf("[TYPE]  Erkannt: %s  -> %s\n",
                  matchedReport->controllerName, matchedReport->reportName);

    if (matchedReport->buttonMappings != nullptr && !matchedReport->isButtonMappingVerified)
    {
      Serial.printf("[TYPE]  ACHTUNG: Tastenbelegung fuer %s stammt aus der Doku und ist\n",
                    matchedReport->controllerName);
      Serial.println("[TYPE]  noch nicht am Geraet nachgeprueft. Namen koennen falsch sein.");
      Serial.println("[TYPE]  Mit 'b' auf rohe Bitmasken umschalten zum Nachmessen.");
    }

    subscribeToReport(inputService,
                      matchedReport->characteristicUuid,
                      session->specificReportTracker,
                      matchedReport->controllerName,
                      matchedReport->reportName,
                      matchedReport->type,
                      matchedReport->firstStickOffset,
                      matchedReport->secondStickOffset,
                      matchedReport->buttonMappings,
                      matchedReport->buttonMappingCount);
  }
  else
  {
    Serial.println("[TYPE]  Kein bekannter typspezifischer Report gefunden.");
    Serial.println("[TYPE]  Bitte den GATT-Dump oben mit hid_reports.md abgleichen.");
  }

#if SUBSCRIBE_TO_COMMON_INPUT_REPORT
  subscribeToReport(inputService,
                    COMMON_INPUT_REPORT_UUID,
                    session->commonReportTracker,
                    matchedReport != nullptr ? matchedReport->controllerName : "Unbekannt",
                    "Report 0x05",
                    Switch2ControllerType::Unknown,
                    COMMON_REPORT_LEFT_STICK_OFFSET,
                    COMMON_REPORT_RIGHT_STICK_OFFSET,
                    nullptr,  // Report 0x05 hat ein eigenes Button-Layout, nicht gemessen
                    0);
#endif

  // Weiterscannen, damit der zweite Joy-Con gefunden wird.
  NimBLEDevice::getScan()->start(0, false, true);
}

// --- Scannen ----------------------------------------------------------------

class Switch2ScanCallbacks : public NimBLEScanCallbacks
{
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override
  {
    const std::string manufacturerData = advertisedDevice->getManufacturerData();
    if (manufacturerData.size() < 2)
    {
      return;
    }

    const uint8_t *data = reinterpret_cast<const uint8_t *>(manufacturerData.data());
    const uint16_t manufacturerId = static_cast<uint16_t>(data[0] | (data[1] << 8));
    if (manufacturerId != NINTENDO_MANUFACTURER_ID)
    {
      return; // Kein Nintendo-Geraet - sonst waere die Konsole voller Fremdgeraete
    }

    const NimBLEAddress address = advertisedDevice->getAddress();
    DeviceRecord *record = findOrCreateDeviceRecord(address);
    if (record == nullptr)
    {
      return;
    }

    // Advertisements kommen mehrmals pro Sekunde. Ungedrosselt laeuft der
    // UART-Puffer ueber und die Zeilen verschachteln sich ineinander.
    const uint32_t nowMs = millis();
    const bool shouldLog =
        !record->hasLoggedOnce || (nowMs - record->lastScanLogAtMs) >= SCAN_LOG_INTERVAL_MS;

    if (manufacturerData.size() < MANUFACTURER_DATA_MINIMUM_LENGTH)
    {
      if (shouldLog)
      {
        record->lastScanLogAtMs = nowMs;
        record->hasLoggedOnce   = true;
        Serial.printf("[SCAN]  Nintendo-Geraet %s: MfgData zu kurz (%u Bytes)\n",
                      address.toString().c_str(),
                      static_cast<unsigned>(manufacturerData.size()));
      }
      return;
    }

    const uint16_t vendorId = static_cast<uint16_t>(
        data[MANUFACTURER_DATA_VENDOR_OFFSET] | (data[MANUFACTURER_DATA_VENDOR_OFFSET + 1] << 8));
    const uint16_t productId = static_cast<uint16_t>(
        data[MANUFACTURER_DATA_PRODUCT_OFFSET] | (data[MANUFACTURER_DATA_PRODUCT_OFFSET + 1] << 8));

    bool hasHostAddress = false;
    for (size_t i = 0; i < MANUFACTURER_DATA_HOST_ADDRESS_LENGTH; i++)
    {
      if (data[MANUFACTURER_DATA_HOST_ADDRESS_OFFSET + i] != 0x00)
      {
        hasHostAddress = true;
        break;
      }
    }
    const bool isWakeAdvertisement = data[MANUFACTURER_DATA_WAKE_FLAG_OFFSET] == 0x81;
    const bool isPairingMode       = !hasHostAddress && !isWakeAdvertisement;

    if (shouldLog)
    {
      record->lastScanLogAtMs = nowMs;
      record->hasLoggedOnce   = true;

      Serial.printf("[SCAN]  Nintendo-Geraet %s  RSSI=%d\n",
                    address.toString().c_str(), advertisedDevice->getRSSI());
      Serial.print("[SCAN]    MfgData: ");
      printHexBytes(data, manufacturerData.size());
      Serial.println();
      Serial.printf("[SCAN]    VendorID=0x%04X ProductID=0x%04X  -> %s\n",
                    vendorId, productId,
                    isWakeAdvertisement
                        ? "Wake-Advertisement"
                        : (hasHostAddress ? "Reconnection-Advertisement (auf anderen Host gekoppelt)"
                                          : "Standard-Advertisement (Pairing-Modus)"));

      if (!isPairingMode)
      {
        Serial.println("[SCAN]    -> Sync-Taste LANG gedrueckt halten, bis die LEDs laufen.");
        Serial.println("[SCAN]       Kurzer Tastendruck weckt ihn nur fuer seinen alten Host.");
      }
    }

    if (vendorId != NINTENDO_VENDOR_ID || productId < SWITCH2_LOWEST_PRODUCT_ID)
    {
      return;
    }

    // NUR im Pairing-Modus verbinden. Ein Reconnection- oder Wake-Advertisement
    // richtet sich an den bereits gekoppelten Host und wuerde uns zwangslaeufig
    // abgewiesen. Jeder solche Fehlversuch zaehlt auf den dokumentierten
    // Cooldown ein, der den Controller danach minutenlang gar nicht mehr
    // reagieren laesst - deshalb erst gar nicht versuchen.
    if (!isPairingMode)
    {
      return;
    }

    // Frischer Sync-Tastendruck ist eine bewusste Nutzeraktion: Backoff aus
    // frueheren Fehlversuchen verfaellt damit.
    if (record->failedConnectCount > 0)
    {
      record->failedConnectCount     = 0;
      record->nextConnectAllowedAtMs = 0;
      Serial.println("[SCAN]    -> Pairing-Modus erkannt, Wartezeit aufgehoben.");
    }

    if (findSessionByAddress(address) != nullptr || hasPendingConnectAddress)
    {
      return;
    }
    if (record->nextConnectAllowedAtMs != 0 &&
        static_cast<int32_t>(nowMs - record->nextConnectAllowedAtMs) < 0)
    {
      return; // Backoff laeuft noch
    }

    pendingConnectAddress    = address;
    hasPendingConnectAddress = true;
    NimBLEDevice::getScan()->stop();
  }
};

static Switch2ScanCallbacks scanCallbacks;

// --- Serielle Kommandos -----------------------------------------------------

static void printHelp()
{
  Serial.println("[HELP]  b = Tastennamen / rohe Bitmasken  | d = Vollhexdump an/aus");
  Serial.println("[HELP]  s = Stick-Ausgabe an/aus          | r = Referenz+Kalibrierung neu");
  Serial.println("[HELP]  h = diese Hilfe");
}

static void resetAllTrackers()
{
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    if (controllerSessions[i].isInUse)
    {
      resetReportTracker(controllerSessions[i].specificReportTracker);
      resetReportTracker(controllerSessions[i].commonReportTracker);
    }
  }
  Serial.println("[BASE]  Zurueckgesetzt. Controller ruhig liegen lassen bis 'Referenz gesetzt'");
  Serial.println("[BASE]  - in dieser Phase wird auch die Stick-Mitte neu vermessen.");
}

static void handleSerialCommands()
{
  while (Serial.available() > 0)
  {
    const int command = Serial.read();
    switch (command)
    {
      case 'b':
        shouldPrintButtonNames = !shouldPrintButtonNames;
        Serial.printf("[CMD]   Tastenausgabe: %s\n",
                      shouldPrintButtonNames ? "Klartextnamen" : "rohe Bitmasken");
        break;
      case 'd':
        shouldPrintFullHexDump = !shouldPrintFullHexDump;
        Serial.printf("[CMD]   Vollhexdump: %s\n", shouldPrintFullHexDump ? "an" : "aus");
        break;
      case 'r':
        resetAllTrackers();
        break;
      case 's':
        shouldPrintStickValues = !shouldPrintStickValues;
        Serial.printf("[CMD]   Stick-Ausgabe: %s\n", shouldPrintStickValues ? "an" : "aus");
        break;
      case 'h':
        printHelp();
        break;
      default:
        break;
    }
  }
}

// --- Setup / Loop -----------------------------------------------------------

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=== Switch-2-Controller Rohsignal-Scanner ===");
  printHelp();

  Serial.printf("[MQTT]  Broker %s:%d, Topic %s\n",
                CLAW_MQTT_BROKER_HOST, CLAW_MQTT_BROKER_PORT, JOYCON_CONTROL_TOPIC);
  mqttConnection.begin();

  NimBLEDevice::init("");
  // Kein Bonding, kein MITM, kein Secure Connections. Der Controller trennt
  // die Verbindung, sobald der Host SMP-Pairing initiiert.
  NimBLEDevice::setSecurityAuth(false, false, false);
  NimBLEDevice::setMTU(247);

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(80);
  scan->start(0, false, true);

  Serial.println("[SCAN]  Scanne... Joy-Con mit gedrueckter Sync-Taste in Pairing-Modus bringen.");
}

void loop()
{
  handleSerialCommands();
  mqttConnection.maintainConnection();
  publishPendingControlState();

  if (hasPendingConnectAddress)
  {
    const NimBLEAddress address = pendingConnectAddress;
    hasPendingConnectAddress    = false;
    connectToController(address);
  }

  delay(10);
}
