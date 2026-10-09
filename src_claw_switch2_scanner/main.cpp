#include <Arduino.h>
#include <NimBLEDevice.h>

#include <string.h>

#include "claw_mqtt_connection.h"
#include "firmware_config.h"

#include "switch2_command_channel.h"
#include "switch2_controller_types.h"
#include "switch2_hex_output.h"
#include "switch2_pairing_manager.h"
#include "switch2_protocol.h"
#include "switch2_report_parser.h"
#include "switch2_report_tracker.h"
#include "switch2_session.h"

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
// Der Befehlskanal (Handle 0x0014) wird beim Verbinden mit aufgeloest. Darueber
// laeuft die Testvibration: ein Druck auf A am rechten Joy-Con laesst ihn
// vibrieren. Das ist zugleich der Nachweis, dass ein ungekoppelter Host
// Schreibbefehle setzen darf - Voraussetzung fuer den Pairing-Handshake.
//
// Serielle Kommandos zur Laufzeit (kein Neuflashen noetig):
//   b = Tastennamen / rohe Bitmasken
//   d = kompletten Hexdump jedes Reports an/aus
//   r = Referenzwerte und Rauschliste zuruecksetzen (Controller ruhig halten)
//   s = Stick-Dekodierung an/aus
//   v = Testvibration am rechten Joy-Con ausloesen
//   p = Pairing-Handshake starten (siehe switch2_pairing.h)
//   h = Hilfe
// ============================================================================

static constexpr const char *JOYCON_CONTROL_TOPIC = "clawmachine/player_input/joycon";

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
//
// Je Controller ein eigener Zustand, damit beide zusammengefuehrt werden
// koennen: gleichzeitiges Druecken links und rechts ergibt einen gemeinsamen
// Befehl, statt dass sich beide gegenseitig ueberschreiben.
static JoyConControlState leftControlState;
static JoyConControlState rightControlState;
static volatile bool      hasPendingControlState = false;

static void storeControlState(Switch2ControllerType type, const JoyConControlState &state)
{
  if (type == Switch2ControllerType::JoyConLeft)
  {
    leftControlState = state;
  }
  else if (type == Switch2ControllerType::JoyConRight)
  {
    rightControlState = state;
  }
  else
  {
    return;
  }
  hasPendingControlState = true;
}

// Bei Verbindungsverlust muss der Zustand geloescht werden. Sonst bliebe eine
// gehaltene Richtung stehen und die Maschine fuehre weiter.
static void clearControlState(Switch2ControllerType type)
{
  if (type == Switch2ControllerType::JoyConLeft)
  {
    leftControlState = JoyConControlState();
  }
  else if (type == Switch2ControllerType::JoyConRight)
  {
    rightControlState = JoyConControlState();
  }
  else
  {
    return;
  }
  hasPendingControlState = true;
}

// --- Rueckfallebene: gemeinsamer Report 0x05 --------------------------------

// Zusaetzlich zum controllerspezifischen Report auch den gemeinsamen Report
// 0x05 abonnieren. War im Bring-up die Rueckfallebene fuer den Fall, dass der
// typspezifische Report keine Daten liefert. Das ist widerlegt - beide Joy-Cons
// liefern auf Handle 0x000E zuverlaessig. Bleibt als Schalter stehen, ist aber
// aus, weil das Abo nur den Datenverkehr und die Konsolenausgabe verdoppelt.
#define SUBSCRIBE_TO_COMMON_INPUT_REPORT 0

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

// Die Byte-Reihenfolge der Host-Adresse im Advertisement ist nicht dokumentiert
// und an einem gekoppelten Geraet auch nicht ablesbar, solange man dessen Host
// nicht kennt. Deshalb werden beide Richtungen geprueft - eine falsch herum
// verglichene Adresse waere sonst ein Fehler, der sich als "reagiert nie" tarnt.
static bool isHostAddressOurs(const uint8_t *advertisedHostAddress)
{
  const uint8_t *ownAddress = NimBLEDevice::getAddress().getVal();

  if (memcmp(advertisedHostAddress, ownAddress, MANUFACTURER_DATA_HOST_ADDRESS_LENGTH) == 0)
  {
    return true;
  }

  for (size_t i = 0; i < MANUFACTURER_DATA_HOST_ADDRESS_LENGTH; i++)
  {
    if (advertisedHostAddress[i] !=
        ownAddress[MANUFACTURER_DATA_HOST_ADDRESS_LENGTH - 1 - i])
    {
      return false;
    }
  }
  return true;
}

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

// --- Kernstueck: was ein eintreffender Report nach sich zieht ---------------
//
// Diese Funktion verdrahtet nur. Sie ist bewusst die einzige Stelle, an der
// Diagnose (switch2_report_tracker), Auswertung (switch2_report_parser),
// Vibration und MQTT-Ausgabe zusammentreffen - die Module selbst kennen sich
// gegenseitig nicht.
//
// Sie laeuft im NimBLE-Host-Task, nicht im Arduino-Task. Deshalb wird hier
// nichts gesendet: Vibration und MQTT-Zustand werden nur vorgemerkt und im
// loop() abgearbeitet.

// Testvibration bei steigender Flanke der A-Taste. Bewusst VOR der Lernphase
// und nicht in der Diff-Auswertung: die laeuft erst nach der Lernphase und
// wuerde die Rueckmeldung die ersten hundert Reports lang verschlucken.
//
// Der gemeinsame Report 0x05 wird mit Typ Unknown abonniert und faellt damit
// von selbst heraus - sein Tastenlayout ist ein anderes.
static void requestVibrationOnAButtonPress(const ReportTracker &tracker,
                                           const uint8_t       *report,
                                           size_t               usableLength)
{
  if (tracker.controllerType != Switch2ControllerType::JoyConRight ||
      usableLength <= JOYCON_RIGHT_A_BUTTON_BYTE ||
      tracker.previousReportLength <= JOYCON_RIGHT_A_BUTTON_BYTE)
  {
    return;
  }

  const bool wasAButtonPressed =
      (tracker.previousReport[JOYCON_RIGHT_A_BUTTON_BYTE] & JOYCON_RIGHT_A_BUTTON_MASK) != 0;
  const bool isAButtonPressed =
      (report[JOYCON_RIGHT_A_BUTTON_BYTE] & JOYCON_RIGHT_A_BUTTON_MASK) != 0;

  if (isAButtonPressed && !wasAButtonPressed)
  {
    requestVibration(Switch2ControllerType::JoyConRight);
  }
}

static void handleIncomingReport(ReportTracker &tracker, const uint8_t *report, size_t length)
{
  const size_t usableLength = length > MAXIMUM_REPORT_LENGTH ? MAXIMUM_REPORT_LENGTH : length;
  tracker.receivedReportCount++;

  printReportHexDump(tracker, report, length, usableLength);

  if (captureFirstReport(tracker, report, length, usableLength))
  {
    return;  // kein Vorgaenger vorhanden, also nichts zu vergleichen
  }

  requestVibrationOnAButtonPress(tracker, report, usableLength);

  if (advanceNoiseLearning(tracker, report, usableLength))
  {
    return;  // Lernphase laeuft noch, der Report ist damit verbraucht
  }

  reportChangedBytes(tracker, report, usableLength);
  printStickDeflection(tracker, report, usableLength);

  // Beide Joy-Cons steuern die Maschine. Hier wird der Zustand lediglich
  // hinterlegt; publiziert wird im loop(), weil PubSubClient nicht threadsicher
  // ist und dieser Callback im NimBLE-Host-Task laeuft.
  storeControlState(tracker.controllerType,
                    readControlStateFromReport(report, usableLength,
                                               trackerStickCalibration(tracker)));

  rememberReport(tracker, report, usableLength);
}

// --- Steuerbefehle an den Python-Server -------------------------------------

static void publishPendingControlState()
{
  if (!hasPendingControlState)
  {
    return;
  }

  hasPendingControlState = false;

  // Beide Controller verodern: was auf einer der beiden Seiten gedrueckt ist,
  // gilt. So funktioniert gleichzeitiges Bedienen sinnvoll.
  JoyConControlState state;
  state.upButton    = leftControlState.upButton    || rightControlState.upButton;
  state.downButton  = leftControlState.downButton  || rightControlState.downButton;
  state.leftButton  = leftControlState.leftButton  || rightControlState.leftButton;
  state.rightButton = leftControlState.rightButton || rightControlState.rightButton;
  state.frontButton = leftControlState.frontButton || rightControlState.frontButton;
  state.backButton  = leftControlState.backButton  || rightControlState.backButton;

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

  configureReportTracker(tracker, controllerName, reportName, controllerType,
                         firstStickOffset, secondStickOffset,
                         buttonMappings, buttonMappingCount);

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

// --- Befehlskanal -----------------------------------------------------------
//
// Antworten kommen im selben Headerformat zurueck wie die Anfrage, nur mit
// Richtungsbyte 0x01. Vorerst werden sie nur protokolliert: fuer die Vibration
// ist das der Nachweis, dass der Befehl angenommen wurde. Der Pairing-
// Handshake wertet dieselben Antworten spaeter inhaltlich aus.

static void subscribeToCommandResponse(NimBLERemoteService *service,
                                       ControllerSession   *session,
                                       const char          *controllerName)
{
  NimBLERemoteCharacteristic *characteristic =
      service->getCharacteristic(SWITCH2_COMMAND_RESPONSE_UUID);
  if (characteristic == nullptr || !characteristic->canNotify())
  {
    Serial.println("[CMD]   Keine Befehlsantwort-Characteristic - Antworten bleiben unsichtbar.");
    return;
  }

  // controllerName zeigt auf ein String-Literal aus CONTROLLER_SPECIFIC_REPORTS
  // und lebt damit laenger als die Verbindung. Kopieren waere unnoetig.
  // session zeigt in den statischen Sitzungspool und bleibt ebenfalls gueltig.
  const bool didSubscribe = characteristic->subscribe(
      true,
      [session, controllerName](NimBLERemoteCharacteristic *, uint8_t *data, size_t length, bool)
      {
        const size_t usableLength = length > MAXIMUM_REPORT_LENGTH ? MAXIMUM_REPORT_LENGTH : length;

        Serial.printf("[CMD-RSP] %s (%u Bytes): ", controllerName, static_cast<unsigned>(length));
        printHexBytes(data, usableLength);
        Serial.println();

        // Nur ablegen und weiterreichen - ausgewertet wird im loop().
        memcpy(session->commandResponse, data, usableLength);
        session->commandResponseLength = usableLength;
        session->hasCommandResponse    = true;
      });

  if (!didSubscribe)
  {
    Serial.println("[CMD]   subscribe() auf die Befehlsantwort fehlgeschlagen.");
    return;
  }

  Serial.printf("[CMD]   Befehlsantwort abonniert auf handle 0x%04X\n",
                characteristic->getHandle());
}

// Beide Schreibkanaele einmalig aufloesen und in der Session merken. Der
// Report-Callback darf spaeter nicht selbst im GATT-Baum suchen - er laeuft im
// NimBLE-Host-Task.
static void resolveCommandChannel(NimBLERemoteService            *service,
                                  ControllerSession              *session,
                                  const Switch2ReportDescription *reportDescription)
{
  if (reportDescription == nullptr)
  {
    // Ohne erkannten Typ sind beide UUIDs unbekannt - sie sind typabhaengig.
    Serial.println("[CMD]   Controllertyp unbekannt - kein Befehlskanal.");
    return;
  }

  session->commandCharacteristic =
      service->getCharacteristic(reportDescription->commandCharacteristicUuid);
  if (session->commandCharacteristic == nullptr)
  {
    Serial.println("[CMD]   Befehls-Characteristic 0x0016 nicht gefunden - keine Vibration.");
  }
  else
  {
    Serial.printf("[CMD]   Befehlskanal auf handle 0x%04X bereit\n",
                  session->commandCharacteristic->getHandle());
  }

  session->vibrationCharacteristic =
      service->getCharacteristic(reportDescription->vibrationCharacteristicUuid);
  if (session->vibrationCharacteristic != nullptr)
  {
    Serial.printf("[CMD]   Vibrationskanal auf handle 0x%04X bereit\n",
                  session->vibrationCharacteristic->getHandle());
  }

  subscribeToCommandResponse(service, session, reportDescription->controllerName);
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
      // Zuerst die Steuerung stillsetzen: ohne das bliebe eine gehaltene
      // Richtung stehen und die Maschine fuehre nach dem Abriss weiter.
      clearControlState(session->type);
      releaseSession(*session);
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

  // Nur wenn zu dieser Adresse ein LTK vorliegt, passiert hier etwas. Ohne
  // gespeicherte Kopplung bleibt die Verbindung offen wie bisher - ein
  // Verschluesselungsversuch auf gut Glueck wuerde die Trennung ausloesen.
  applyStoredPairing(client, address);

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

  // Der Befehlskanal haengt am selben Service wie die Input-Reports.
  resolveCommandChannel(inputService, session, matchedReport);

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
    const bool isWakeAdvertisement =
        data[MANUFACTURER_DATA_WAKE_FLAG_OFFSET] == MANUFACTURER_DATA_WAKE_FLAG_VALUE;
    const bool isPairingMode       = !hasHostAddress && !isWakeAdvertisement;

    // Steht in der Host-Adresse unsere eigene, dann sucht der Controller uns
    // und nicht seine alte Konsole. Genau dieser Fall soll den Sync-
    // Tastendruck ueberfluessig machen.
    const bool isAddressedToUs =
        hasHostAddress && isHostAddressOurs(&data[MANUFACTURER_DATA_HOST_ADDRESS_OFFSET]);

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

      if (hasHostAddress)
      {
        const uint8_t *hostAddress = &data[MANUFACTURER_DATA_HOST_ADDRESS_OFFSET];
        Serial.printf("[SCAN]    Gekoppelt an Host %02x:%02x:%02x:%02x:%02x:%02x%s\n",
                      hostAddress[5], hostAddress[4], hostAddress[3],
                      hostAddress[2], hostAddress[1], hostAddress[0],
                      isAddressedToUs ? "  (das sind WIR)" : "  (fremder Host)");
      }

      if (isAddressedToUs)
      {
        Serial.println("[SCAN]    -> Wir sind der gekoppelte Host, wir duerfen ran.");
      }
      else if (!isPairingMode)
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
    if (!isPairingMode && !isAddressedToUs)
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
  Serial.println("[HELP]  v = Testvibration am rechten Joy-Con (wie ein Druck auf A)");
  Serial.println("[HELP]  p = Pairing starten (Controller merkt sich diesen ESP32!)");
  Serial.println("[HELP]  h = diese Hilfe");
}

static void resetAllTrackers()
{
  resetAllSessionTrackers();
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
        reportDiagnostics.shouldPrintButtonNames = !reportDiagnostics.shouldPrintButtonNames;
        Serial.printf("[CMD]   Tastenausgabe: %s\n",
                      reportDiagnostics.shouldPrintButtonNames ? "Klartextnamen"
                                                                : "rohe Bitmasken");
        break;
      case 'd':
        reportDiagnostics.shouldPrintFullHexDump = !reportDiagnostics.shouldPrintFullHexDump;
        Serial.printf("[CMD]   Vollhexdump: %s\n",
                      reportDiagnostics.shouldPrintFullHexDump ? "an" : "aus");
        break;
      case 'r':
        resetAllTrackers();
        break;
      case 's':
        reportDiagnostics.shouldPrintStickValues = !reportDiagnostics.shouldPrintStickValues;
        Serial.printf("[CMD]   Stick-Ausgabe: %s\n",
                      reportDiagnostics.shouldPrintStickValues ? "an" : "aus");
        break;
      case 'v':
        // Derselbe Weg wie beim Tastendruck: gesendet wird gleich im loop().
        requestVibration(Switch2ControllerType::JoyConRight);
        Serial.println("[CMD]   Testvibration angefordert.");
        break;
      case 'p':
        // Bewusst nur auf ausdruecklichen Wunsch: ein Joy-Con hat genau einen
        // Host. Nach dem Pairing verbindet er sich nicht mehr mit der Switch,
        // bis er dort neu gekoppelt wird.
        startPairingForConnectedControllers();
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
  // Ohne die eigene Adresse laesst sich nicht beurteilen, ob ein
  // Reconnection-Advertisement uns meint oder die Konsole.
  Serial.printf("[BLE]   Eigene Adresse: %s\n",
                NimBLEDevice::getAddress().toString().c_str());
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
  sendPendingVibration();
  advancePairingSessions();

  if (hasPendingConnectAddress)
  {
    const NimBLEAddress address = pendingConnectAddress;
    hasPendingConnectAddress    = false;
    connectToController(address);
  }

  delay(10);
}
