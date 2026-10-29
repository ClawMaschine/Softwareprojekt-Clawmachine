#include <Arduino.h>

#include "joycon_control_output.h"
#include "joycon_power_status.h"
#include "switch2_command_channel.h"
#include "switch2_controller_types.h"
#include "switch2_connection.h"
#include "switch2_pairing_manager.h"
#include "switch2_report_tracker.h"
#include "switch2_session.h"

// ============================================================================
// Switch-2-Controller als Eingabegeraet fuer die Clawmachine.
//
// Diese Firmware trifft BEWUSST KEINE ANNAHME darueber, welches Bit zu welcher
// Taste gehoert. Sie verbindet sich mit dem Controller, abonniert dessen
// Input-Reports und gibt aus, WELCHE BYTES UND BITS sich aendern. Die
// Zuordnung "Bit -> physische Taste" entsteht durch Druecken und Ablesen und
// ist in docs/mapping.txt festgehalten.
//
// Zusaetzlich sendet sie die Steuerbefehle BEIDER Joy-Cons per MQTT an den
// Python-Server - im selben Format, das der Server vom Control-Panel kennt,
// sodass dort keine neue Auswertelogik noetig ist. Das ist bewusst dieselbe
// Rolle, die src_claw_panel_test beim Panel hatte: eine Testfirmware, die auf
// das echte Topic sendet. Der Umbau nach src_claw_player_input kommt spaeter.
//
// --- Wo liegt was? ----------------------------------------------------------
//
// Diese Datei enthaelt nur den Programmeinstieg und die serielle Konsole.
// Die Abhaengigkeiten laufen strikt von oben nach unten - kein Modul kennt
// eines, das darueber steht:
//
//   main.cpp                     setup/loop, serielle Kommandos
//   switch2_connection.*         scannen, verbinden, abonnieren (alles BLE)
//   switch2_input_router.*       was ein eintreffender Report nach sich zieht
//   switch2_pairing_manager.*    Handshake ausfuehren, LTK in NVS behalten
//   switch2_pairing.*            das Pairing-Protokoll, ohne Bluetooth
//   joycon_control_output.*      MQTT an den Python-Server
//   endstop_feedback.*           Vibration bei ausgeloestem Endstop
//   joycon_power_status.*        Akkustand melden, wenn er sich aendert
//   switch2_command_channel.*    Befehle senden, Vibration, Player-LEDs
//   switch2_session.*            was wir ueber eine offene Verbindung wissen
//   switch2_report_tracker.*     Rauschlernen, Kalibrierung, Diff-Ausgabe
//   switch2_report_parser.*      Bytes -> Stickwerte und Tastenzustaende
//   switch2_controller_types.*   Controllertypen, UUIDs, Tastennamen
//   switch2_protocol.h           Protokollkonstanten, reine Werte
//   switch2_hex_output.*         Hexausgabe, von dreien gebraucht
//
// Wer wissen will, was ein Tastendruck ausloest, liest switch2_input_router.*
// - das ist die einzige Stelle, an der Diagnose, Auswertung, Vibration und
// MQTT zusammentreffen.
//
// --- Grundlagen -------------------------------------------------------------
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
// Der Befehlskanal (Handle 0x0016, siehe switch2_protocol.h) wird beim
// Verbinden mit aufgeloest. Darueber laeuft die Testvibration: ein Druck auf A
// am rechten Joy-Con laesst ihn vibrieren. Das ist zugleich der Nachweis, dass
// ein ungekoppelter Host Schreibbefehle setzen darf - Voraussetzung fuer den
// Pairing-Handshake.
//
// Ist ein Controller einmal gekoppelt, genuegt danach ein Tastendruck an ihm:
// er sucht dann uns als Host, und der Scan laesst ihn durch (isAddressedToUs in
// switch2_connection.cpp). Der lange Sync-Druck ist nur fuer das erste Koppeln
// noetig.
//
// Das Player-LED-Muster, das das Pairing-Lauflicht beenden soll, wird derzeit
// NICHT von selbst gesetzt: seit es drin war, brach die Verbindung nach 2-3 s
// mit reason=520 ab, obwohl der Projektbericht sie vorher als "stabil ueber
// 10 s" abgenommen hatte. Mit 'l' von Hand setzen, mit 'L' die Automatik
// einschalten - siehe switch2_command_channel.h.
//
// Serielle Kommandos zur Laufzeit (kein Neuflashen noetig):
//   b = Tastennamen / rohe Bitmasken
//   d = kompletten Hexdump jedes Reports an/aus
//   r = Referenzwerte und Rauschliste zuruecksetzen (Controller ruhig halten)
//   s = Stick-Dekodierung an/aus
//   v = Testvibration am rechten Joy-Con ausloesen
//   a = Akkustand beider Controller ausgeben
//   l = Player-LEDs setzen (LED-Belegung pruefen)
//   o = alle vier LEDs an (Leuchten zurueckholen)
//   L = LED-Muster beim Verbinden an/aus (Standard aus, Begruendung oben)
//   p = Pairing-Handshake starten (siehe switch2_pairing.h)
//   k = gespeicherte Kopplungen und NimBLE-Bonds auflisten
//   x = alles austragen: unser Speicher, NimBLE-Bonds, Scan-Wartezeiten
//       und der Befehl an den Controller, uns zu vergessen
//   m = Netzwerk an/aus - testen ohne Broker, WiFi wird dabei abgeschaltet
//   w = 20 s lang jedes Advertisement ausgeben (wirkt der Sync-Druck?)
//   f = einmal trotz fremder Host-Adresse verbinden (blockt der Filter?)
//   i = Trennen unberuehrter Controller an/aus (Standard an, nach 3 Minuten)
//   h = Hilfe
// ============================================================================

// --- Serielle Kommandos -----------------------------------------------------

static void printHelp()
{
  Serial.println("[HELP]  b = Tastennamen / rohe Bitmasken  | d = Vollhexdump an/aus");
  Serial.println("[HELP]  s = Stick-Ausgabe an/aus          | r = Referenz+Kalibrierung neu");
  Serial.println("[HELP]  v = Testvibration am rechten Joy-Con (wie ein Druck auf A)");
  Serial.println("[HELP]  a = Akkustand beider Controller   | l = Player-LEDs setzen");
  Serial.println("[HELP]  o = alle LEDs an (zurueckholen)   | L = LED beim Verbinden an/aus");
  Serial.println("[HELP]  p = Pairing starten (Controller merkt sich diesen ESP32!)");
  Serial.println("[HELP]  k = Kopplungen + NimBLE-Bonds zeigen | x = alles austragen");
  Serial.println("[HELP]  m = Netzwerk an/aus (ohne Broker testen, BLE allein auf der Antenne)");
  Serial.println("[HELP]  w = 20 s jedes Advertisement zeigen (Sync-Druck pruefen)");
  Serial.println("[HELP]  f = einmal trotz fremder Host-Adresse verbinden (Filter pruefen)");
  Serial.println("[HELP]  i = unberuehrte Controller nach 3 min trennen an/aus");
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
      case 'a':
        printPowerStatusNow();
        break;
      case 'l':
        // Zum Nachpruefen der LED-Belegung am Geraet. Beim Verbinden passiert
        // das ohnehin von selbst.
        setPlayerLedsForConnectedControllers();
        break;
      case 'L':
        // Zum Eingrenzen von Verbindungsabbruechen: das LED-Muster ist der
        // einzige Befehl, der unaufgefordert direkt nach dem Verbinden geht.
        setAutomaticPlayerLedEnabled(!isAutomaticPlayerLedEnabled());
        Serial.printf("[CMD]   LED-Muster beim Verbinden: %s\n",
                      isAutomaticPlayerLedEnabled() ? "an" : "aus");
        break;
      case 'p':
        // Bewusst nur auf ausdruecklichen Wunsch: ein Joy-Con hat genau einen
        // Host. Nach dem Pairing verbindet er sich nicht mehr mit der Switch,
        // bis er dort neu gekoppelt wird.
        startPairingForConnectedControllers();
        break;
      case 'k':
        printStoredPairings();
        break;
      case 'f':
        // Einmal trotz fremder Host-Adresse verbinden - der entscheidende
        // Versuch, ob der Filter oder der Controller blockt.
        forceNextConnectAttempt();
        break;
      case 'w':
        // Beobachtungsfenster: jedes Advertisement wird ausgegeben. Zum
        // Nachsehen, ob ein Sync-Druck etwas aendert.
        startVerboseScanWindow();
        break;
      case 'i':
        setIdleDisconnectEnabled(!isIdleDisconnectEnabled());
        break;
      case 'm':
        // Ohne Broker testen: WiFi ganz aus, Steuerbefehle nur noch als
        // [OFFLINE] auf der Konsole. Auch das Mittel, um einen
        // Verbindungsabbruch von einem Antennenproblem zu trennen.
        setNetworkOutputEnabled(!isNetworkOutputEnabled());
        break;
      case 'o':
        // Holt die Leuchten zurueck, wenn ein gesetztes Muster haengt, und
        // zeigt, ob der LED-Befehl ueberhaupt ankommt.
        turnOnAllPlayerLeds();
        break;
      case 'x':
        // Vollstaendig austragen. Die Scan-Wartezeiten gehoeren dazu, werden
        // aber hier aufgehoben und nicht im Pairing-Modul: das liegt unter
        // switch2_connection und darf es nicht kennen. main.cpp ist die
        // Verdrahtungsebene.
        clearStoredPairings();
        forgetTrackedDevices();
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

  beginControlOutput();

  beginControllerConnections();

  // Muss vor jedem Lesen des Kopplungsspeichers kommen.
  beginPairingStorage();

  // Sagt gleich beim Start, ob ein Controller sich von selbst melden kann oder
  // ob erst noch gekoppelt werden muss.
  printStoredPairings();
}

void loop()
{
  handleSerialCommands();

  // Zuerst verbinden, erst danach Netzwerk: ein vorgemerkter Controller ist
  // zeitkritisch, MQTT ist es nicht.
  //
  // Ein gekoppelter Controller bewirbt sich nach einem Tastendruck nur in
  // einem kurzen Fenster und hoert auch nur kurz auf eine Verbindung. Stand
  // connectPendingController() am Ende der Schleife, lag davor
  // maintainControlOutput() - und das kann Sekunden blockieren:
  // ensureWifiConnected() in der gemeinsamen Library wartet in einer Schleife
  // bis zu 10 s auf WiFi, und jeder Broker-Versuch ist ein blockierender
  // TCP-Connect. Bis wir den Verbindungsaufbau endlich starteten, war das
  // Fenster zu, und connect() lief in eine Zeitueberschreitung
  // (lastError=520).
  //
  // Mit gedrueckter Sync-Taste fiel das nie auf: im Pairing-Modus bewirbt sich
  // der Controller minutenlang, da ist eine Verzoegerung von Sekunden
  // gleichgueltig. Nur der Tastendruck-Reconnect ist darauf angewiesen.
  connectPendingController();

  maintainControlOutput();
  publishPendingControlState();
  publishPendingPowerStatus();
  sendPendingVibration();
  advancePairingSessions();
  disconnectIdleControllers();

  delay(10);
}
