#include "switch2_connection.h"

#include <Arduino.h>
#include <string.h>

#include "joycon_control_output.h"
#include "joycon_power_status.h"
#include "switch2_command_channel.h"
#include "switch2_hex_output.h"
#include "switch2_input_router.h"
#include "switch2_pairing_manager.h"
#include "switch2_protocol.h"
#include "switch2_report_parser.h"
#include "switch2_report_tracker.h"

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
//
// Wichtig: ein gelungenes connect() ist noch kein Erfolg. Eine Verbindung, die
// nach zwei Sekunden wieder stirbt, laesst denselben Schaden entstehen wie ein
// Fehlversuch - sie wird nur nicht als einer erkannt, wenn man den Zaehler
// schon beim Verbinden zurueckstellt. Als gelungen gilt deshalb erst eine
// Verbindung, die CONNECTION_CONSIDERED_GOOD_AFTER_MS gehalten hat. Die
// 10 Sekunden sind genau das Kriterium, mit dem der Projektbericht die
// Verbindung als stabil abgenommen hat (Abschnitt 8, Kriterium 2).

static constexpr size_t   MAXIMUM_TRACKED_DEVICES          = 8;
static constexpr uint32_t CONNECT_RETRY_BASE_DELAY_MS      = 5000;
static constexpr uint32_t CONNECT_RETRY_MAXIMUM_DELAY_MS   = 30000;
static constexpr uint32_t SCAN_LOG_INTERVAL_MS             = 5000;
static constexpr uint32_t CONNECTION_CONSIDERED_GOOD_AFTER_MS = 10000;

// Nach einem Verbindungsabriss nicht sofort neu verbinden. Der Controller
// bewirbt sich zwar binnen Millisekunden wieder, aber sein Link-Layer ist
// gerade erst aufgeraeumt, und ein Versuch in dieses Fenster hinein laeuft in
// eine Zeitueberschreitung (connect() mit lastError=520). Genau das war am
// Geraet zu sehen: der eine Controller verband sich nach dem Abriss wieder,
// der andere nicht - der Unterschied war schlicht, wie viel Zeit dazwischen
// lag. Zwei Sekunden merkt niemand, machen den Unterschied aber systematisch.
static constexpr uint32_t RECONNECT_SETTLE_DELAY_MS = 2000;

// --- Unberuehrte Verbindungen trennen ---------------------------------------
//
// Ein daliegender Controller schickt weiter rund 30 Reports pro Sekunde. Das
// kostet seinen Akku, belegt einen Sitzungsplatz und teilt sich die Antenne
// mit WiFi. Nach dieser Zeit ohne Bedienung wird die Verbindung deshalb
// absichtlich getrennt; ein Tastendruck holt den Controller zurueck.
static constexpr uint32_t IDLE_DISCONNECT_AFTER_MS = 180000;

// Danach eine Weile nicht von selbst neu verbinden. Ohne das wuerde der
// Controller, der sich nach dem Trennen noch kurz bewirbt, sofort wieder
// eingefangen - und das Trennen waere sinnlos. Nach dieser Zeit schlaeft er,
// und nur ein echter Tastendruck weckt ihn.
static constexpr uint32_t IDLE_RECONNECT_SUPPRESSION_MS = 15000;

// Welcher Art ein Advertisement ist. Wechselt die Art, wird das sofort
// gemeldet und nicht von der Drosselung verschluckt: genau daran laesst sich
// ablesen, ob ein Sync-Druck gewirkt hat.
enum class AdvertisementKind : uint8_t
{
  Unknown,
  PairingMode,   // sucht einen neuen Host - wir duerfen ran
  AddressedToUs, // gekoppelt an uns - ein Tastendruck genuegt
  ForeignHost    // gekoppelt an jemand anderen - Sync lang druecken
};

static const char *describeAdvertisementKind(AdvertisementKind kind)
{
  switch (kind)
  {
    case AdvertisementKind::PairingMode:   return "Pairing-Modus";
    case AdvertisementKind::AddressedToUs: return "gekoppelt an UNS";
    case AdvertisementKind::ForeignHost:   return "gekoppelt an fremden Host";
    default:                               return "unbekannt";
  }
}

struct DeviceRecord
{
  bool          isInUse;
  NimBLEAddress address;
  uint8_t       failedConnectCount;
  uint32_t      nextConnectAllowedAtMs;
  uint32_t      lastScanLogAtMs;
  bool          hasLoggedOnce;

  // Wann wurde das Geraet zuletzt gesehen bzw. verbunden? Das erste fuer die
  // Platzverwaltung, das zweite fuer die Beurteilung der Standzeit.
  uint32_t      lastSeenAtMs;
  uint32_t      connectedAtMs;
  bool          isConnected;

  // Kam die letzte Verbindung zustande und starb gleich wieder? Dann darf ein
  // Advertisement im Pairing-Modus die Wartezeit NICHT aufheben - sonst wirkt
  // sie bei einem Controller, der minutenlang im Pairing-Modus liegt, nie.
  bool          wasLastFailureShortConnection;

  AdvertisementKind lastAdvertisementKind;

  // Der GATT-Baum aendert sich zwischen zwei Verbindungen desselben Geraets
  // nicht. Bei jedem Reconnect erneut ausgegeben sind es rund 20 Zeilen, die
  // bei 115200 Baud den UART-Puffer ueberlaufen lassen - sichtbar als
  // verstuemmelte Zeilen. Deshalb nur beim ersten Mal.
  bool          hasPrintedGattTree;

  // Haben WIR die Verbindung getrennt, weil der Controller unberuehrt war?
  // Dann gilt die laengere Sperre statt der kurzen Beruhigungspause.
  bool          wasDisconnectedForIdleness;
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

static void initializeDeviceRecord(DeviceRecord &record, const NimBLEAddress &address)
{
  record.isInUse                = true;
  record.address                = address;
  record.failedConnectCount     = 0;
  record.nextConnectAllowedAtMs = 0;
  record.lastScanLogAtMs        = 0;
  record.hasLoggedOnce          = false;
  record.lastSeenAtMs           = millis();
  record.connectedAtMs          = 0;
  record.isConnected            = false;
  record.wasLastFailureShortConnection = false;
  record.lastAdvertisementKind         = AdvertisementKind::Unknown;
  record.hasPrintedGattTree            = false;
  record.wasDisconnectedForIdleness    = false;
}

// Die Plaetze wurden nie wieder frei, und dieser Datensatz entsteht VOR der
// Vendor- und Produktpruefung: jedes Nintendo-Geraet in Funkreichweite hat
// einen belegt, auch eine fremde Switch. War das Feld voll, gab es nullptr
// zurueck und der Scan verliess die Funktion stillschweigend - ein neuer
// Controller wurde dann nie wieder betrachtet, ohne eine Zeile im Log.
//
// Deshalb wird jetzt der am laengsten nicht gesehene Platz verdraengt und das
// auch gemeldet. Der verdraengte Datensatz verliert seine Wartezeit; das ist
// allemal besser, als neue Geraete zu ignorieren.
static DeviceRecord *findOrCreateDeviceRecord(const NimBLEAddress &address)
{
  for (size_t i = 0; i < MAXIMUM_TRACKED_DEVICES; i++)
  {
    if (deviceRecords[i].isInUse && deviceRecords[i].address == address)
    {
      deviceRecords[i].lastSeenAtMs = millis();
      return &deviceRecords[i];
    }
  }

  for (size_t i = 0; i < MAXIMUM_TRACKED_DEVICES; i++)
  {
    if (!deviceRecords[i].isInUse)
    {
      initializeDeviceRecord(deviceRecords[i], address);
      return &deviceRecords[i];
    }
  }

  // Alles belegt: aeltesten Eintrag suchen, aber keinen, der gerade verbunden
  // ist - dessen Standzeit wird noch gebraucht.
  DeviceRecord *oldestRecord = nullptr;
  for (size_t i = 0; i < MAXIMUM_TRACKED_DEVICES; i++)
  {
    if (deviceRecords[i].isConnected)
    {
      continue;
    }
    if (oldestRecord == nullptr ||
        static_cast<int32_t>(deviceRecords[i].lastSeenAtMs - oldestRecord->lastSeenAtMs) < 0)
    {
      oldestRecord = &deviceRecords[i];
    }
  }

  if (oldestRecord == nullptr)
  {
    return nullptr;  // alle Plaetze verbunden - kann bei drei Sessions nicht sein
  }

  Serial.printf("[SCAN]  Geraeteliste voll, verdraenge %s fuer %s.\n",
                oldestRecord->address.toString().c_str(), address.toString().c_str());
  initializeDeviceRecord(*oldestRecord, address);
  return oldestRecord;
}

// --- Unberuehrte Verbindungen trennen ---------------------------------------

static bool isIdleDisconnectOn = true;

void setIdleDisconnectEnabled(bool isEnabled)
{
  isIdleDisconnectOn = isEnabled;
  Serial.printf("[CONN]  Trennen unberuehrter Controller: %s (nach %u s).\n",
                isEnabled ? "an" : "aus",
                static_cast<unsigned>(IDLE_DISCONNECT_AFTER_MS / 1000));
}

bool isIdleDisconnectEnabled()
{
  return isIdleDisconnectOn;
}

void disconnectIdleControllers()
{
  if (!isIdleDisconnectOn)
  {
    return;
  }

  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    ControllerSession &session = controllerSessions[i];
    if (!session.isInUse)
    {
      continue;
    }

    const ReportTracker &tracker = session.specificReportTracker;
    if (static_cast<int32_t>(millis() - tracker.lastActivityAtMs) <
        static_cast<int32_t>(IDLE_DISCONNECT_AFTER_MS))
    {
      continue;
    }

    NimBLEClient *client = NimBLEDevice::getClientByPeerAddress(session.address);
    if (client == nullptr || !client->isConnected())
    {
      continue;
    }

    // Deutlich als Absicht kennzeichnen: sonst sieht diese Trennung im Log aus
    // wie ein Verbindungsabriss, und man sucht einen Fehler, den es nicht gibt.
    Serial.printf("[CONN]  %s war %u s unberuehrt - Verbindung wird ABSICHTLICH getrennt.\n",
                  tracker.controllerName != nullptr ? tracker.controllerName : "Controller",
                  static_cast<unsigned>((millis() - tracker.lastActivityAtMs) / 1000));
    Serial.println("[CONN]  Ein Tastendruck am Controller holt ihn zurueck ('i' schaltet das ab).");

    DeviceRecord *record = findOrCreateDeviceRecord(session.address);
    if (record != nullptr)
    {
      record->wasDisconnectedForIdleness = true;
    }
    client->disconnect();
  }
}

void forgetTrackedDevices()
{
  size_t forgottenCount = 0;
  for (size_t i = 0; i < MAXIMUM_TRACKED_DEVICES; i++)
  {
    if (deviceRecords[i].isInUse)
    {
      forgottenCount++;
    }
    deviceRecords[i].isInUse = false;
  }
  Serial.printf("[SCAN]  %u gemerkte Geraete vergessen, Wartezeiten aufgehoben.\n",
                static_cast<unsigned>(forgottenCount));
}

// --- Beobachtungsfenster ----------------------------------------------------
//
// Normalerweise wird je Geraet nur alle SCAN_LOG_INTERVAL_MS eine Zeile
// ausgegeben, sonst laeuft der UART ueber. Zum Nachsehen, ob ein Sync-Druck
// ueberhaupt etwas am Advertisement aendert, ist das aber zu grob: man drueckt
// und sieht nichts.
//
// Deshalb ein befristetes Fenster, in dem JEDES Advertisement ausgegeben wird.
// Befristet und nicht dauerhaft, weil genau diese Flut laut Projektbericht
// (Abschnitt 7) den UART-Puffer ueberlaufen laesst.
static constexpr uint32_t VERBOSE_SCAN_WINDOW_MS = 20000;

static uint32_t verboseScanUntilMs = 0;

// --- Erzwungener Verbindungsversuch -----------------------------------------
//
// Normalerweise wird ein Controller uebersprungen, dessen Advertisement eine
// fremde Host-Adresse traegt: laut Bericht 5.4 weist er uns ab (reason=574),
// und jeder Fehlversuch zaehlt auf den Cooldown ein.
//
// Diese Annahme ist aber ins Wackeln geraten. Der linke Joy-Con ist
// nachweislich mit UNS gekoppelt - die verschluesselte Wiederverbindung
// funktioniert - und sendet trotzdem eine Host-Adresse aus Nullen. "Nullen"
// bedeutet also nicht "im Pairing-Modus", und damit ist auch die Umkehrung
// fraglich: eine belegte Host-Adresse muss nicht heissen, dass er uns abweist.
//
// Statt weiter zu raten: einmal trotzdem versuchen. Verbindet er, war der
// Filter das Problem. Kommt reason=574, ist die Annahme bestaetigt. Einmalig
// und nur auf ausdruecklichen Wunsch, damit daraus kein Haemmern wird.
static bool shouldForceNextConnect = false;

void forceNextConnectAttempt()
{
  shouldForceNextConnect = true;
  Serial.println("[SCAN]  Naechster gefundener Switch-2-Controller wird EINMAL versucht,");
  Serial.println("[SCAN]  auch wenn er auf einen fremden Host zeigt.");
}

void startVerboseScanWindow()
{
  verboseScanUntilMs = millis() + VERBOSE_SCAN_WINDOW_MS;
  Serial.printf("[SCAN]  Beobachtung an fuer %u s: jedes Advertisement wird ausgegeben.\n",
                static_cast<unsigned>(VERBOSE_SCAN_WINDOW_MS / 1000));
  Serial.println("[SCAN]  Jetzt die Sync-Taste LANG halten. Im Pairing-Modus wird die");
  Serial.println("[SCAN]  Host-Adresse im MfgData zu Nullen - genau darauf achten.");
}

static bool isVerboseScanActive()
{
  if (verboseScanUntilMs == 0)
  {
    return false;
  }
  if (static_cast<int32_t>(millis() - verboseScanUntilMs) < 0)
  {
    return true;
  }
  verboseScanUntilMs = 0;
  Serial.println("[SCAN]  Beobachtung beendet, Ausgabe wieder gedrosselt ('w' erneut).");
  return false;
}

// Wird im NimBLE-Host-Task gesetzt und im Arduino-Task ausgewertet. Ein
// einzelner Slot genuegt: Controller senden ihre Advertisements wiederholt,
// ein verpasster Fund kommt im naechsten Intervall erneut.
static NimBLEAddress   pendingConnectAddress;
static volatile bool   hasPendingConnectAddress = false;

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

// Steht weiter unten, wird aber schon im Trennen-Callback gebraucht.
static void registerFailedConnectAttempt(const NimBLEAddress &address,
                                         bool                 wasShortLivedConnection);

// Eine Verbindung, die nicht lange genug gehalten hat, zaehlt wie ein
// Fehlversuch. Ohne das lief die Firmware in genau die Schleife, die der
// Projektbericht in Abschnitt 7 als behoben beschreibt: der Zaehler wurde beim
// Verbinden zurueckgestellt, die Verbindung starb zwei Sekunden spaeter, und es
// ging ungebremst von vorn los - bis der Controller in seinen Cooldown ging und
// minutenlang gar nicht mehr reagierte.
static void evaluateConnectionLifetime(const NimBLEAddress &address)
{
  DeviceRecord *record = findOrCreateDeviceRecord(address);
  if (record == nullptr)
  {
    return;
  }

  record->isConnected = false;

  if (record->connectedAtMs == 0)
  {
    return;  // nie als verbunden vermerkt, nichts zu beurteilen
  }

  const uint32_t connectionLifetimeMs = millis() - record->connectedAtMs;
  record->connectedAtMs              = 0;

  Serial.printf("[CONN]  Verbindung hielt %u ms.\n",
                static_cast<unsigned>(connectionLifetimeMs));

  if (connectionLifetimeMs >= CONNECTION_CONSIDERED_GOOD_AFTER_MS)
  {
    // Die Verbindung war in Ordnung, also keine Strafe - aber die kurze
    // Beruhigungspause gilt trotzdem. Haben wir selbst wegen Untaetigkeit
    // getrennt, gilt die laengere Sperre.
    const uint32_t delayMs = record->wasDisconnectedForIdleness
                                 ? IDLE_RECONNECT_SUPPRESSION_MS
                                 : RECONNECT_SETTLE_DELAY_MS;
    record->failedConnectCount            = 0;
    record->nextConnectAllowedAtMs        = millis() + delayMs;
    record->wasLastFailureShortConnection = false;
    record->wasDisconnectedForIdleness    = false;
    return;
  }

  Serial.printf("[CONN]  Unter %u ms - zaehlt als Fehlversuch, sonst landet der\n",
                static_cast<unsigned>(CONNECTION_CONSIDERED_GOOD_AFTER_MS));
  Serial.println("[CONN]  Controller durch dauerndes Neuverbinden in seinem Cooldown.");
  registerFailedConnectAttempt(address, true);
}

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
      // Wie viele Reports kamen an, bevor die Verbindung starb? Bei einer
      // Supervision-Timeout (reason=520) ist das die entscheidende Frage:
      // wenige Reports heissen, der Controller ging sofort still; viele
      // heissen, er lief und verstummte erst spaeter.
      Serial.printf("[CONN]  %u Reports empfangen, Lernphase %s.\n",
                    static_cast<unsigned>(session->specificReportTracker.receivedReportCount),
                    session->specificReportTracker.isNoiseLearned ? "fertig" : "unfertig");

      // Steuerung stillsetzen: ohne das bliebe eine gehaltene Richtung stehen
      // und die Maschine fuehre nach dem Abriss weiter.
      // Je GERAET aufraeumen, nicht je Typ - sonst loescht ein getrennter
      // linker Joy-Con den Zustand eines zweiten, noch verbundenen.
      const size_t sessionIndex = sessionIndexOf(session);
      clearControlState(sessionIndex);
      clearPowerStatus(sessionIndex);
      releaseSession(*session);
    }

    evaluateConnectionLifetime(client->getPeerAddress());

    NimBLEDevice::getScan()->start(0, false, true);
  }

  // Die Verschluesselung mit dem gespeicherten LTK ist der einzige Teil dieser
  // Firmware, der nicht am Geraet bestaetigt ist - deshalb wird ihr Ergebnis
  // ausdruecklich protokolliert. Ohne das waere ein Fehlschlag nur als
  // raetselhafte Trennung sichtbar.
  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override
  {
    Serial.printf("[PAIR]  Sicherheitszustand: verschluesselt=%s authentifiziert=%s gebondet=%s\n",
                  connInfo.isEncrypted() ? "ja" : "nein",
                  connInfo.isAuthenticated() ? "ja" : "nein",
                  connInfo.isBonded() ? "ja" : "nein");
  }
};

// Ein wiederverbundener Controller erwartet, dass der Host mit dem
// gespeicherten Schluessel verschluesselt (Bericht 5.9). Deshalb erst
// weitermachen, wenn die Verschluesselung steht - ein GATT-Abruf davor
// bekommt entweder nichts oder fuehrt zur Trennung.
//
// Laeuft im Arduino-Task (ueber connectPendingController), hier darf also
// gewartet werden.
static constexpr uint32_t ENCRYPTION_TIMEOUT_MS      = 2000;
static constexpr uint32_t ENCRYPTION_POLL_INTERVAL_MS = 50;

static bool waitForEncryption(NimBLEClient *client)
{
  const uint32_t startedAtMs = millis();

  while ((millis() - startedAtMs) < ENCRYPTION_TIMEOUT_MS)
  {
    if (!client->isConnected())
    {
      Serial.println("[PAIR]  Getrennt, waehrend auf die Verschluesselung gewartet wurde.");
      return false;
    }
    if (client->getConnInfo().isEncrypted())
    {
      Serial.printf("[PAIR]  Verschluesselt nach %u ms.\n",
                    static_cast<unsigned>(millis() - startedAtMs));
      return true;
    }
    delay(ENCRYPTION_POLL_INTERVAL_MS);
  }

  Serial.println("[PAIR]  Verschluesselung kam nicht zustande. Der gespeicherte LTK passt");
  Serial.println("[PAIR]  nicht - mit 'x' loeschen und neu koppeln ('p').");
  return false;
}

static Switch2ClientCallbacks clientCallbacks;

// Fehlversuch vermerken und den naechsten Versuch verzoegern. Ohne das wuerde
// nach jedem Fehlschlag sofort neu verbunden - und genau das loest laut
// Praxisberichten den Cooldown aus, der den Controller minutenlang gar nicht
// mehr reagieren laesst.
static void registerFailedConnectAttempt(const NimBLEAddress &address,
                                         bool                 wasShortLivedConnection)
{
  DeviceRecord *record = findOrCreateDeviceRecord(address);
  if (record == nullptr)
  {
    return;
  }

  if (record->failedConnectCount < 255)
  {
    record->failedConnectCount++;
  }
  record->wasLastFailureShortConnection = wasShortLivedConnection;

  uint32_t retryDelayMs = CONNECT_RETRY_BASE_DELAY_MS * record->failedConnectCount;
  if (retryDelayMs > CONNECT_RETRY_MAXIMUM_DELAY_MS)
  {
    retryDelayMs = CONNECT_RETRY_MAXIMUM_DELAY_MS;
  }
  record->nextConnectAllowedAtMs = millis() + retryDelayMs;

  Serial.printf("[CONN]  Naechster Versuch fruehestens in %u s (Fehlversuch %u).\n",
                static_cast<unsigned>(retryDelayMs / 1000),
                static_cast<unsigned>(record->failedConnectCount));

  if (wasShortLivedConnection)
  {
    // Hier hilft der Sync-Druck ausdruecklich NICHT: der Controller ist
    // erreichbar, das Haemmern ist das Problem. Die Wartezeit bleibt also
    // stehen, auch wenn er weiter im Pairing-Modus sendet.
    Serial.println("[CONN]  Wartezeit bleibt stehen - 'x' hebt sie auf, wenn es sein muss.");
  }
  else
  {
    Serial.println("[CONN]  Sync-Taste erneut lang druecken hebt die Wartezeit sofort auf.");
  }
}

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

    registerFailedConnectAttempt(address, false);

    NimBLEDevice::getScan()->start(0, false, true);
    return;
  }

  Serial.printf("[CONN]  Verbunden. MTU=%u\n", client->getMTU());

  // Nur wenn zu dieser Adresse ein LTK vorliegt, passiert hier etwas. Ohne
  // gespeicherte Kopplung bleibt die Verbindung offen wie bisher - ein
  // Verschluesselungsversuch auf gut Glueck wuerde die Trennung ausloesen.
  if (applyStoredPairing(client, address) && !waitForEncryption(client))
  {
    // Halb verbunden weitermachen hilft niemandem: sauber trennen. Der
    // Fehlversuch zaehlt mit, sonst wuerde im Zweisekundentakt neu verbunden -
    // und der Controller zieht sich in seinen Cooldown zurueck.
    session->isInUse = false;
    registerFailedConnectAttempt(address, false);
    client->disconnect();
    return;
  }

  // Standzeit mitschreiben, aber den Fehlerzaehler BEWUSST noch nicht
  // zuruecksetzen: ob diese Verbindung etwas wert war, entscheidet sich erst
  // beim Trennen (siehe evaluateConnectionLifetime). Wurde hier zurueckgesetzt,
  // lief jede kurzlebige Verbindung ungebremst in die naechste.
  DeviceRecord *record = findOrCreateDeviceRecord(address);
  if (record != nullptr)
  {
    record->connectedAtMs = millis();
    record->isConnected   = true;
  }

  if (record == nullptr || !record->hasPrintedGattTree)
  {
    printGattTree(client);
    if (record != nullptr)
    {
      record->hasPrintedGattTree = true;
    }
  }
  else
  {
    Serial.println("[GATT]  Baum unveraendert, nicht erneut ausgegeben ('x' zeigt ihn wieder).");
  }

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

  // Verbunden ist nicht gekoppelt: im Pairing-Modus laesst der Controller
  // jeden ran, merkt sich aber niemanden. Erst der Handshake ('p') schreibt
  // uns in den Controller und legt den LTK ab. Ohne diesen Hinweis haelt man
  // eine stehende Verbindung leicht faelschlich fuer eine Kopplung.
  if (!hasStoredPairing(address))
  {
    Serial.println("[PAIR]  Verbunden, aber NICHT gekoppelt. Mit 'p' koppeln, damit er sich");
    Serial.println("[PAIR]  diesen ESP32 merkt - sonst ist nach dem Trennen wieder Sync noetig.");
  }

  // Festes Player-LED-Muster statt des Pairing-Lauflichts: der Controller
  // wartet nicht mehr auf Kopplung, er wird bedient. Links leuchtet die erste
  // LED, rechts die zweite.
  if (session->commandCharacteristic != nullptr && isAutomaticPlayerLedEnabled())
  {
    setPlayerLedPattern(session, playerLedPatternForType(session->type));
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

    // Im Beobachtungsfenster ('w') ALLES melden, auch was gleich darunter
    // ausgefiltert wird. Sonst bleibt die wichtigste Frage unbeantwortbar:
    // sendet ein Controller ueberhaupt etwas, wenn man eine Taste druckt?
    // Ein Wake-Advertisement muss nicht dasselbe Format haben wie das
    // Advertisement im Pairing-Modus - faellt es hier heraus, sieht es aus,
    // als waere das Geraet still.
    if (isVerboseScanActive())
    {
      Serial.printf("[SCAN*] %s  RSSI=%d  MfgData %u Byte: ",
                    advertisedDevice->getAddress().toString().c_str(),
                    advertisedDevice->getRSSI(),
                    static_cast<unsigned>(manufacturerData.size()));
      if (manufacturerData.empty())
      {
        Serial.print("(keine)");
      }
      else
      {
        printHexBytes(reinterpret_cast<const uint8_t *>(manufacturerData.data()),
                      manufacturerData.size());
      }
      Serial.println();
    }

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
    // Gilt nur fuer den Fall unten, dass die Daten zu kurz sind. Das richtige
    // Advertisement hat weiter unten seine eigene Drosselung, die ein
    // Wechseln der Art durchlaesst.
    const bool shouldLogShortData =
        !record->hasLoggedOnce || (nowMs - record->lastScanLogAtMs) >= SCAN_LOG_INTERVAL_MS;

    if (manufacturerData.size() < MANUFACTURER_DATA_MINIMUM_LENGTH)
    {
      if (shouldLogShortData)
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
    // Das Flag 0x81 ist laut bluetooth_interface.md die Richtung "Konsole
    // weckt Controller". Ein gekoppelter Controller, der nach einem
    // Tastendruck seinen Host sucht, hat hier 0x00 und traegt die Host-Adresse
    // auf 0x0C - er ist also an der belegten Adresse zu erkennen, nicht am
    // Flag. Die Pruefung bleibt trotzdem stehen: taucht das Flag doch einmal
    // auf, soll das Advertisement nicht als Pairing-Modus durchgehen.
    const bool isWakeAdvertisement =
        data[MANUFACTURER_DATA_WAKE_FLAG_OFFSET] == MANUFACTURER_DATA_WAKE_FLAG_VALUE;

    // Steht keine Host-Adresse im Advertisement, gehoert der Controller
    // niemandem - dann darf verbunden werden, und es gibt auch keinen, der uns
    // abweisen koennte.
    //
    // Vorher stand hier zusaetzlich "&& !isWakeAdvertisement". Damit konnte das
    // Flag 0x81 einen Controller OHNE Eigentuemer veto-artig blockieren, und
    // zwar stillschweigend - ein moeglicher Grund dafuer, dass sich ein frisch
    // in den Pairing-Modus gebrachter Controller nicht verbinden liess. Das
    // Flag bedeutet laut bluetooth_interface.md ohnehin die andere Richtung
    // (Konsole weckt Controller) und bleibt deshalb nur Anzeigeinformation.
    const bool isPairingMode = !hasHostAddress;

    // Steht in der Host-Adresse unsere eigene, dann sucht der Controller uns
    // und nicht seine alte Konsole. Genau dieser Fall macht den Sync-
    // Tastendruck ueberfluessig: ein Tastendruck am Controller genuegt.
    // Die Adresse steht dort in umgekehrter Bytefolge, deshalb prueft
    // isHostAddressOurs() beide Richtungen.
    const bool isAddressedToUs =
        hasHostAddress && isHostAddressOurs(&data[MANUFACTURER_DATA_HOST_ADDRESS_OFFSET]);

    // Wechselt die Art des Advertisements, geht das sofort raus - sonst
    // verschluckt die Drosselung genau den Moment, in dem ein Sync-Druck
    // gewirkt hat, und man haelt die Taste ratlos noch einmal.
    const AdvertisementKind advertisementKind =
        isAddressedToUs ? AdvertisementKind::AddressedToUs
                        : (isPairingMode ? AdvertisementKind::PairingMode
                                         : AdvertisementKind::ForeignHost);

    if (advertisementKind != record->lastAdvertisementKind)
    {
      if (record->lastAdvertisementKind != AdvertisementKind::Unknown)
      {
        Serial.printf("[SCAN]  %s wechselt: %s -> %s\n",
                      address.toString().c_str(),
                      describeAdvertisementKind(record->lastAdvertisementKind),
                      describeAdvertisementKind(advertisementKind));
      }
      record->lastAdvertisementKind = advertisementKind;
      record->lastScanLogAtMs       = 0;
      record->hasLoggedOnce         = false;
    }

    const bool shouldLogAdvertisement =
        isVerboseScanActive() || !record->hasLoggedOnce ||
        (nowMs - record->lastScanLogAtMs) >= SCAN_LOG_INTERVAL_MS;

    if (shouldLogAdvertisement)
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
        if (!isAddressedToUs)
        {
          // Die eigene Adresse daneben, damit der Vergleich nachpruefbar ist -
          // sie steht sonst nur einmal beim Start und ist langst weggescrollt.
          Serial.printf("[SCAN]    Wir sind %s\n",
                        NimBLEDevice::getAddress().toString().c_str());
        }
      }

      if (isAddressedToUs)
      {
        Serial.println("[SCAN]    -> Wir sind der gekoppelte Host, wir duerfen ran.");
      }
      else if (!isPairingMode)
      {
        // Zeigt das Advertisement auf einen fremden Host, obwohl wir zu diesem
        // Controller einen Schluessel haben, dann ist er anderswo neu
        // gekoppelt worden. Unsere gespeicherte Kopplung ist damit wertlos -
        // ohne diesen Hinweis sucht man den Fehler bei sich.
        if (hasStoredPairing(address))
        {
          Serial.println("[SCAN]    -> ACHTUNG: wir haben eine Kopplung zu diesem Controller,");
          Serial.println("[SCAN]       er zeigt aber auf einen fremden Host. Er wurde an der");
          Serial.println("[SCAN]       Switch neu gekoppelt - mit 'x' loeschen und neu 'p'.");
        }
        Serial.println("[SCAN]    -> Sync-Taste LANG gedrueckt halten, bis die LEDs laufen.");
        Serial.println("[SCAN]       Kurzer Tastendruck weckt ihn nur fuer seinen alten Host.");
      }
    }

    if (vendorId != NINTENDO_VENDOR_ID || productId < SWITCH2_LOWEST_PRODUCT_ID)
    {
      // Nicht stillschweigend: genau hier faellt ein Controller heraus, der
      // kein Switch-2-Geraet ist - und das sieht von aussen aus wie "er wird
      // einfach ignoriert".
      if (shouldLogAdvertisement)
      {
        Serial.printf("[SCAN]    -> kein Switch-2-Geraet (Vendor 0x%04X, Product 0x%04X),\n",
                      vendorId, productId);
        Serial.printf("[SCAN]       erwartet Vendor 0x%04X und Product ab 0x%04X.\n",
                      NINTENDO_VENDOR_ID, SWITCH2_LOWEST_PRODUCT_ID);
      }
      return;
    }

    // Verbunden wird in genau zwei Faellen: der Controller ist im Pairing-Modus
    // (sucht einen neuen Host), oder sein Advertisement nennt UNSERE Adresse -
    // dann hat er uns gespeichert und ein Tastendruck an ihm genuegt. Genau das
    // ist der Weg, der den Sync-Druck nach dem ersten Koppeln erspart.
    //
    // Zeigt das Advertisement auf einen fremden Host, wird nicht verbunden: der
    // Controller wuerde uns abweisen, und jeder solche Fehlversuch zaehlt auf
    // den dokumentierten Cooldown ein, der ihn danach minutenlang gar nicht
    // mehr reagieren laesst.
    if (!isPairingMode && !isAddressedToUs)
    {
      if (!shouldForceNextConnect)
      {
        return;
      }

      // Einmalig: Filter und Wartezeit beiseite, weil es eine ausdrueckliche
      // Nutzeraktion ist - wie ein frischer Sync-Druck.
      shouldForceNextConnect         = false;
      record->failedConnectCount     = 0;
      record->nextConnectAllowedAtMs = 0;
      Serial.println("[SCAN]    -> 'f': Versuche trotz fremder Host-Adresse.");
      Serial.println("[SCAN]       reason=574 hiesse, er weist uns wirklich ab.");
    }

    // Frischer Sync-Tastendruck ist eine bewusste Nutzeraktion: Backoff aus
    // frueheren Fehlversuchen verfaellt damit. Zwei Einschraenkungen, beide
    // schmerzhaft gelernt:
    //
    // Nur im Pairing-Modus - ein Reconnection-Advertisement kommt mehrmals pro
    // Sekunde von selbst und wuerde die Wartezeit sonst dauernd aufheben.
    //
    // Und nicht, wenn die letzte Verbindung zustande kam und gleich wieder
    // starb. Ein Controller bleibt nach dem Sync-Druck minutenlang im
    // Pairing-Modus und sendet dabei ununterbrochen; jedes dieser
    // Advertisements wuerde die Wartezeit aufheben, und das Haemmern, vor dem
    // sie schuetzt, ginge genau so weiter. Ein gescheiterter
    // VerbindungsAUFBAU ist etwas anderes: da hilft ein frischer Sync-Druck
    // wirklich, also darf er die Wartezeit dort auch aufheben.
    if (isPairingMode && record->failedConnectCount > 0 &&
        !record->wasLastFailureShortConnection)
    {
      record->failedConnectCount     = 0;
      record->nextConnectAllowedAtMs = 0;
      Serial.println("[SCAN]    -> Pairing-Modus erkannt, Wartezeit aufgehoben.");
    }

    if (findSessionByAddress(address) != nullptr)
    {
      return;  // schon verbunden, nichts zu tun und nichts zu melden
    }
    if (hasPendingConnectAddress)
    {
      // Es laeuft schon ein Verbindungsversuch mit einem anderen Geraet. Das
      // ist normal und loest sich im naechsten Advertisement - nur eben nicht
      // offensichtlich, wenn man daneben sitzt und wartet.
      if (shouldLogAdvertisement)
      {
        Serial.println("[SCAN]    -> ein anderer Verbindungsversuch laeuft noch, gleich wieder.");
      }
      return;
    }
    if (record->nextConnectAllowedAtMs != 0 &&
        static_cast<int32_t>(nowMs - record->nextConnectAllowedAtMs) < 0)
    {
      // Nicht stillschweigend aussteigen: sonst sieht es aus, als wuerde der
      // Controller ignoriert, und man sucht den Fehler an der falschen Stelle.
      // Gedrosselt wie die uebrige Scan-Ausgabe.
      if (shouldLogAdvertisement)
      {
        Serial.printf("[SCAN]    -> noch %u s Wartezeit (Fehlversuch %u), kein Versuch.\n",
                      static_cast<unsigned>((record->nextConnectAllowedAtMs - nowMs) / 1000 + 1),
                      static_cast<unsigned>(record->failedConnectCount));
      }
      return;
    }

    pendingConnectAddress    = address;
    hasPendingConnectAddress = true;
    NimBLEDevice::getScan()->stop();
  }
};

static Switch2ScanCallbacks scanCallbacks;

// --- Start und Takt ---------------------------------------------------------

void beginControllerConnections()
{
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
  // Scan-Fenster und -Intervall in 0,625-ms-Schritten. Vorher 80 von 100,
  // also 80 % Funkzeit nur fuer den Scan. Das war richtig, solange nichts
  // anderes funkte - mit einer stehenden Controllerverbindung UND WiFi bleibt
  // fuer den Aufbau einer ZWEITEN Verbindung aber zu wenig uebrig: der Versuch
  // lief in eine Zeitueberschreitung (connect() mit lastError=520), waehrend
  // der erste Controller schon verbunden war.
  //
  // 48 von 160 sind 30 %: der Scan findet die wiederholten Advertisements
  // weiterhin zuverlaessig, laesst dem Verbindungsaufbau und den bestehenden
  // Verbindungen aber den Grossteil der Funkstrecke.
  scan->setInterval(160);
  scan->setWindow(48);
  scan->start(0, false, true);

  Serial.println("[SCAN]  Scanne... Joy-Con mit gedrueckter Sync-Taste in Pairing-Modus bringen.");
}

void connectPendingController()
{
  if (!hasPendingConnectAddress)
  {
    return;
  }

  const NimBLEAddress address = pendingConnectAddress;
  hasPendingConnectAddress    = false;
  connectToController(address);

  // Der Scan-Callback stoppt den Scan, bevor er die Adresse vormerkt.
  // connectToController hat aber mehrere fruehe Ausstiege (Session schon
  // vorhanden, kein freier Platz, createClient fehlgeschlagen), die ihn nicht
  // wieder angeworfen haben - danach wurde nie wieder ein Geraet gefunden, und
  // es sah aus, als liesse sich nur ein Controller verbinden. Deshalb hier an
  // einer Stelle sicherstellen, dass gescannt wird, statt es an jedem Ausstieg
  // einzeln zu versuchen.
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (!scan->isScanning())
  {
    Serial.println("[SCAN]  Scan war gestoppt - laeuft wieder.");
    scan->start(0, false, true);
  }
}
