#include <Arduino.h>
#include <NimBLEDevice.h>

// Fuer die gespeicherte Kopplung wird der NimBLE-Host direkt angesprochen:
// die Arduino-Huelle bietet kein Einspielen eines fremd ausgehandelten LTK.
#include "nimble/nimble/host/include/host/ble_gap.h"
#include "nimble/nimble/host/include/host/ble_store.h"

#include <string.h>

#include <Preferences.h>

#include "claw_mqtt_connection.h"
#include "firmware_config.h"
#include "switch2_pairing.h"
#include "switch2_controller_types.h"
#include "switch2_report_parser.h"
#include "switch2_protocol.h"

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

static constexpr uint16_t NOISE_LEARNING_SAMPLE_COUNT  = 100;
static constexpr uint8_t  NOISE_CHANGE_PERCENT_THRESHOLD = 25;

// Drosselung der [STICK]-Ausgabe: erst ab dieser Aenderung in Prozent wird
// eine neue Zeile erzeugt.
static constexpr int8_t STICK_PRINT_STEP_PERCENT = 5;

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

// Der Parser soll den Tracker nicht kennen muessen. Deshalb wird die
// Kalibrierung des ersten Sticks hier zusammengestellt und als Wert
// uebergeben - der Parser bleibt damit ohne Abhaengigkeit nach oben.
static StickCalibration trackerStickCalibration(const ReportTracker &tracker)
{
  StickCalibration stick;
  stick.offset    = tracker.firstStickOffset;
  stick.center    = tracker.stickCenter;
  stick.hasCenter = tracker.hasStickCenter;
  return stick;
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
  // Beim Verbinden aufgeloest, nullptr wenn der Controller sie nicht anbietet.
  // Gehoeren dem NimBLEClient und werden mit ihm freigegeben - deshalb beim
  // Trennen nur die Zeiger loeschen, niemals selbst freigeben.
  NimBLERemoteCharacteristic *commandCharacteristic;
  NimBLERemoteCharacteristic *vibrationCharacteristic;

  // Befehlsantworten treffen im NimBLE-Host-Task ein und werden hier abgelegt,
  // damit Auswertung und AES im Arduino-Task laufen. Ein Slot genuegt: der
  // Controller beantwortet immer erst die laufende Anfrage.
  uint8_t       commandResponse[MAXIMUM_REPORT_LENGTH];
  size_t        commandResponseLength;
  volatile bool hasCommandResponse;

  Switch2PairingSession pairing;
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
      controllerSessions[i].commandCharacteristic   = nullptr;
      controllerSessions[i].vibrationCharacteristic = nullptr;
      controllerSessions[i].commandResponseLength   = 0;
      controllerSessions[i].hasCommandResponse      = false;
      controllerSessions[i].pairing.step            = PairingStep::Idle;
      resetReportTracker(controllerSessions[i].specificReportTracker);
      resetReportTracker(controllerSessions[i].commonReportTracker);
      return &controllerSessions[i];
    }
  }
  return nullptr;
}

static ControllerSession *findSessionByType(Switch2ControllerType type)
{
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    if (controllerSessions[i].isInUse && controllerSessions[i].type == type)
    {
      return &controllerSessions[i];
    }
  }
  return nullptr;
}

// --- Befehle senden ---------------------------------------------------------
//
// Ein Befehl besteht aus drei Teilen, genau wie die Konsole ihn schickt:
//   17 Byte Praefix (Report-ID + HD-Rumble, hier Null)
//    8 Byte Befehlsheader
//    n Byte Befehlsdaten
//
// Die Characteristic kann nur WRITE_NR - eine Antwort anzufordern wuerde
// fehlschlagen. Die Bestaetigung kommt stattdessen als [CMD-RSP]-Notification.
static bool sendControllerCommand(ControllerSession *session,
                                  uint8_t            commandId,
                                  uint8_t            subcommandId,
                                  const uint8_t     *data,
                                  size_t             dataLength)
{
  if (session == nullptr || session->commandCharacteristic == nullptr)
  {
    return false;
  }

  if (dataLength > MAXIMUM_COMMAND_DATA_LENGTH)
  {
    Serial.println("[CMD]   Befehlsdaten zu lang - nicht gesendet.");
    return false;
  }

  uint8_t packet[COMMAND_RUMBLE_PREFIX_LENGTH + COMMAND_HEADER_LENGTH +
                 MAXIMUM_COMMAND_DATA_LENGTH] = {0};

  uint8_t *header = &packet[COMMAND_RUMBLE_PREFIX_LENGTH];
  header[0] = commandId;
  header[1] = COMMAND_DIRECTION_HOST_TO_DEVICE;
  header[2] = COMMAND_TRANSPORT_BLUETOOTH;
  header[3] = subcommandId;
  header[4] = 0x00;
  header[5] = static_cast<uint8_t>(dataLength);
  header[6] = 0x00;
  header[7] = 0x00;

  if (data != nullptr && dataLength > 0)
  {
    memcpy(&header[COMMAND_HEADER_LENGTH], data, dataLength);
  }

  const size_t packetLength = COMMAND_RUMBLE_PREFIX_LENGTH + COMMAND_HEADER_LENGTH + dataLength;
  return session->commandCharacteristic->writeValue(packet, packetLength, false);
}

// --- Vibration --------------------------------------------------------------
//
// Vordefinierte Muster laufen als Befehl 0x0A/0x02 ueber den Befehlskanal, nicht
// ueber die Vibrations-Characteristic - die nimmt nur rohe HD-Rumble-Daten.
//
// Der Tastendruck wird im NimBLE-Host-Task erkannt, geschrieben wird aber im
// Arduino-Task: derselbe Grund und dasselbe Muster wie bei
// pendingConnectAddress und hasPendingControlState. Ein GATT-Write aus dem
// Host-Task heraus kann den Stack blockieren, der auf genau diesen Task
// wartet.
static volatile bool         hasPendingVibration = false;
static Switch2ControllerType pendingVibrationType = Switch2ControllerType::Unknown;

static void requestVibration(Switch2ControllerType type)
{
  pendingVibrationType = type;
  hasPendingVibration  = true;
}

static void sendPendingVibration()
{
  if (!hasPendingVibration)
  {
    return;
  }
  const Switch2ControllerType type = pendingVibrationType;
  hasPendingVibration             = false;

  ControllerSession *session = findSessionByType(type);
  if (session == nullptr || session->commandCharacteristic == nullptr)
  {
    Serial.println("[VIB]   Kein Befehlskanal fuer diesen Controller - nichts gesendet.");
    return;
  }

  // Sample-ID plus drei ungenutzte Bytes - exakt das Datenfeld, das die
  // Konsole im Mitschnitt schickt.
  const uint8_t vibrationData[VIBRATION_SAMPLE_DATA_LENGTH] = {
      VIBRATION_SAMPLE_FOR_BUTTON_FEEDBACK, 0x00, 0x00, 0x00};

  const bool didWrite = sendControllerCommand(session,
                                              COMMAND_ID_VIBRATION,
                                              VIBRATION_SUBCOMMAND_PLAY_SAMPLE,
                                              vibrationData,
                                              sizeof(vibrationData));

  // Der Name steht im Tracker, sobald der Report abonniert ist. Vor dem Abo
  // gibt es keinen - dann reicht die neutrale Bezeichnung.
  const char *controllerName = session->specificReportTracker.controllerName != nullptr
                                   ? session->specificReportTracker.controllerName
                                   : "Controller";

  Serial.printf("[VIB]   Muster 0x%02X an %s: %s\n",
                VIBRATION_SAMPLE_FOR_BUTTON_FEEDBACK,
                controllerName,
                didWrite ? "gesendet" : "FEHLGESCHLAGEN");
}

// --- Gespeicherte Kopplungen ------------------------------------------------
//
// Der LTK muss einen Neustart ueberleben, sonst waere das Pairing nach jedem
// Reset wertlos. Abgelegt wird er unter der Adresse des Controllers, damit
// beide Joy-Cons nebeneinander passen.

static Preferences pairingStorage;

static constexpr const char *PAIRING_STORAGE_NAMESPACE = "switch2";

// NVS-Schluessel duerfen hoechstens 15 Zeichen lang sein. Die Adresse als
// Hexkette ohne Trenner braucht genau 12.
static void buildStorageKey(const NimBLEAddress &address, char *keyBuffer, size_t keyBufferLength)
{
  const uint8_t *addressBytes = address.getVal();
  snprintf(keyBuffer, keyBufferLength, "%02x%02x%02x%02x%02x%02x",
           addressBytes[5], addressBytes[4], addressBytes[3],
           addressBytes[2], addressBytes[1], addressBytes[0]);
}

static void storeLongTermKey(const NimBLEAddress &address, const uint8_t *longTermKey)
{
  char storageKey[16];
  buildStorageKey(address, storageKey, sizeof(storageKey));

  pairingStorage.begin(PAIRING_STORAGE_NAMESPACE, false);
  const size_t written = pairingStorage.putBytes(storageKey, longTermKey, PAIRING_KEY_LENGTH);
  pairingStorage.end();

  Serial.printf("[PAIR]  LTK unter '%s' gespeichert (%u Byte).\n",
                storageKey, static_cast<unsigned>(written));
}

static bool loadLongTermKey(const NimBLEAddress &address, uint8_t *longTermKey)
{
  char storageKey[16];
  buildStorageKey(address, storageKey, sizeof(storageKey));

  pairingStorage.begin(PAIRING_STORAGE_NAMESPACE, true);
  const size_t read = pairingStorage.getBytes(storageKey, longTermKey, PAIRING_KEY_LENGTH);
  pairingStorage.end();

  return read == PAIRING_KEY_LENGTH;
}

// --- Gespeicherte Kopplung nutzen -------------------------------------------
//
// ACHTUNG, das ist der einzige Teil dieser Firmware, der noch nicht am Geraet
// bestaetigt ist. Belegt ist nur: die Reconnect-Verbindung IST verschluesselt.
// In btle_joycon2_reconnect_encrypted.pcapng sind von 33 ATT-Zugriffen nur 16
// und von 709 Notifications nur 9 lesbar - der Rest liegt unter der
// Link-Layer-Verschluesselung.
//
// Nintendo handelt den Schluessel ueber die eigene Befehlsschnittstelle aus,
// nicht ueber SMP. NimBLE kennt diesen Weg nicht, also legen wir das Ergebnis
// von Hand in seinen Schluesselspeicher und stossen die Verschluesselung an.
//
// Nicht gepruefte Annahme: EDIV und Rand sind Null und das Verfahren zaehlt als
// Secure Connections. Das ist die uebliche Kodierung fuer einen Schluessel
// ohne Legacy-Aushandlung. Passt sie nicht, lehnt der Controller die
// Verschluesselung ab - sichtbar als sofortige Trennung nach dem Verbinden.
static void applyStoredPairing(NimBLEClient *client, const NimBLEAddress &address)
{
  uint8_t longTermKey[PAIRING_KEY_LENGTH];
  if (!loadLongTermKey(address, longTermKey))
  {
    return;  // Nie gekoppelt - wie bisher unverschluesselt weiterarbeiten.
  }

  struct ble_store_value_sec securityValue;
  memset(&securityValue, 0, sizeof(securityValue));
  securityValue.peer_addr   = *address.getBase();
  securityValue.key_size    = PAIRING_KEY_LENGTH;
  securityValue.ediv        = 0;
  securityValue.rand_num    = 0;
  memcpy(securityValue.ltk, longTermKey, PAIRING_KEY_LENGTH);
  securityValue.ltk_present = 1;
  securityValue.sc          = 1;
  securityValue.authenticated = 0;

  // Beide Richtungen: der Host braucht den Schluessel als eigenen und als den
  // der Gegenstelle, sonst findet er ihn beim Verbinden nicht wieder.
  const int peerStatus = ble_store_write_peer_sec(&securityValue);
  const int ownStatus  = ble_store_write_our_sec(&securityValue);
  if (peerStatus != 0 || ownStatus != 0)
  {
    Serial.printf("[PAIR]  LTK konnte nicht hinterlegt werden (peer=%d, own=%d).\n",
                  peerStatus, ownStatus);
    return;
  }

  const int securityStatus = ble_gap_security_initiate(client->getConnHandle());
  Serial.printf("[PAIR]  Gespeicherte Kopplung genutzt, Verschluesselung angestossen (status=%d).\n",
                securityStatus);
}

// --- Pairing-Antrieb --------------------------------------------------------
//
// Das Protokoll selbst steckt in switch2_pairing.cpp. Hier wird nur getaktet:
// Anfrage senden, Antwort abholen, naechsten Schritt anstossen. Alles im
// Arduino-Task, weil AES und NVS im BLE-Callback nichts zu suchen haben.

static void sendNextPairingRequest(ControllerSession *session)
{
  uint8_t subcommandId = 0;
  uint8_t data[PAIRING_MAXIMUM_REQUEST_DATA_LENGTH] = {0};
  size_t  dataLength = 0;

  if (!buildPairingRequest(session->pairing, subcommandId, data, dataLength))
  {
    return;
  }

  if (!sendControllerCommand(session, COMMAND_ID_PAIRING, subcommandId, data, dataLength))
  {
    Serial.println("[PAIR]  Anfrage konnte nicht gesendet werden - Abbruch.");
    session->pairing.step = PairingStep::Failed;
  }
}

static void startPairingForConnectedControllers()
{
  size_t startedCount = 0;
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    ControllerSession *session = &controllerSessions[i];
    if (!session->isInUse || session->commandCharacteristic == nullptr)
    {
      continue;
    }

    startPairing(session->pairing, NimBLEDevice::getAddress().getVal());
    sendNextPairingRequest(session);
    startedCount++;
  }

  if (startedCount == 0)
  {
    Serial.println("[PAIR]  Kein verbundener Controller mit Befehlskanal - nichts zu tun.");
  }
  else
  {
    Serial.printf("[PAIR]  Handshake fuer %u Controller gestartet.\n",
                  static_cast<unsigned>(startedCount));
  }
}

static void advancePairingSessions()
{
  const uint32_t nowMs = millis();

  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    ControllerSession *session = &controllerSessions[i];
    if (!session->isInUse)
    {
      continue;
    }

    if (session->hasCommandResponse)
    {
      session->hasCommandResponse = false;

      const uint8_t *response       = session->commandResponse;
      const size_t   responseLength = session->commandResponseLength;

      // Nur Pairing-Antworten treiben den Handshake. Alles andere ist bereits
      // als [CMD-RSP] protokolliert und hier nicht von Belang.
      if (responseLength > COMMAND_HEADER_LENGTH &&
          response[0] == COMMAND_ID_PAIRING &&
          response[1] == COMMAND_DIRECTION_DEVICE_TO_HOST)
      {
        const bool hasNextStep = handlePairingResponse(session->pairing,
                                                       response[3],
                                                       &response[COMMAND_HEADER_LENGTH],
                                                       responseLength - COMMAND_HEADER_LENGTH);
        if (hasNextStep)
        {
          sendNextPairingRequest(session);
        }
        else if (session->pairing.step == PairingStep::Completed)
        {
          storeLongTermKey(session->address, session->pairing.longTermKey);
          Serial.println("[PAIR]  Fertig. Der Controller sollte sich nach einem Neustart");
          Serial.println("[PAIR]  von selbst wieder melden - ohne Sync-Taste.");
        }
      }
    }

    checkPairingTimeout(session->pairing, nowMs);
  }
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

// Die drei Bytes eines Sticks gehoeren nicht in die Diff-Ausgabe: sie werden
// separat als [STICK] dekodiert. Der Rauschfilter allein reicht dafuer nicht,
// weil das dritte Byte (obere Y-Bits) im Ruhezustand stabil bleibt und sich
// erst bei Bewegung aendert - es sahe sonst wie ein Tastendruck aus.
static bool isPartOfKnownStickField(const ReportTracker &tracker, size_t byteIndex)
{
  if (tracker.firstStickOffset != NO_STICK_OFFSET &&
      byteIndex >= tracker.firstStickOffset &&
      byteIndex < static_cast<size_t>(tracker.firstStickOffset) + STICK_PACKED_BYTE_COUNT)
  {
    return true;
  }
  if (tracker.secondStickOffset != NO_STICK_OFFSET &&
      byteIndex >= tracker.secondStickOffset &&
      byteIndex < static_cast<size_t>(tracker.secondStickOffset) + STICK_PACKED_BYTE_COUNT)
  {
    return true;
  }
  return false;
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
  if (!shouldPrintStickValues)
  {
    return;
  }

  StickDeflection deflection;
  if (!readStickDeflection(report, length, trackerStickCalibration(tracker), deflection))
  {
    return;  // kein Stick bekannt, noch nicht kalibriert, oder Report zu kurz
  }

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

  // Testvibration bei steigender Flanke der A-Taste. Bewusst hier oben und
  // nicht in der Diff-Auswertung: die laeuft erst nach der Lernphase und
  // wuerde die Rueckmeldung die ersten hundert Reports lang verschlucken.
  //
  // Der gemeinsame Report 0x05 wird mit Typ Unknown abonniert und faellt damit
  // von selbst heraus - sein Tastenlayout ist ein anderes.
  if (tracker.controllerType == Switch2ControllerType::JoyConRight &&
      usableLength > JOYCON_RIGHT_A_BUTTON_BYTE &&
      tracker.previousReportLength > JOYCON_RIGHT_A_BUTTON_BYTE)
  {
    const bool wasAButtonPressed =
        (tracker.previousReport[JOYCON_RIGHT_A_BUTTON_BYTE] & JOYCON_RIGHT_A_BUTTON_MASK) != 0;
    const bool isAButtonPressed =
        (report[JOYCON_RIGHT_A_BUTTON_BYTE] & JOYCON_RIGHT_A_BUTTON_MASK) != 0;

    if (isAButtonPressed && !wasAButtonPressed)
    {
      requestVibration(Switch2ControllerType::JoyConRight);
    }
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
        static_cast<size_t>(tracker.firstStickOffset) + STICK_PACKED_BYTE_COUNT <=
            usableLength)
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

  // Beide Joy-Cons steuern die Maschine. Hier wird der Zustand lediglich
  // hinterlegt; publiziert wird im loop(), weil PubSubClient nicht threadsicher
  // ist und dieser Callback im NimBLE-Host-Task laeuft.
  storeControlState(tracker.controllerType,
                    readControlStateFromReport(report, usableLength,
                                               trackerStickCalibration(tracker)));

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
      // Die Characteristics gehoeren dem Client, der sich gleich selbst
      // loescht. Nur die Zeiger fallen lassen, nichts freigeben.
      session->commandCharacteristic   = nullptr;
      session->vibrationCharacteristic = nullptr;
      session->isInUse                 = false;
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
    const bool isWakeAdvertisement = data[MANUFACTURER_DATA_WAKE_FLAG_OFFSET] == MANUFACTURER_DATA_WAKE_FLAG_VALUE;
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
