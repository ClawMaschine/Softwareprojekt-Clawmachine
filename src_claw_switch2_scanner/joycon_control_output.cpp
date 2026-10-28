#include "joycon_control_output.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "claw_mqtt_connection.h"
#include "endstop_feedback.h"
#include "firmware_config.h"

static constexpr const char *JOYCON_CONTROL_TOPIC = "clawmachine/player_input/joycon";

// Client-ID dieses Boards. Sie steckt auch im Metadaten-Topic, deshalb eine
// Konstante statt zweier Literale, die auseinanderlaufen koennen.
static constexpr const char *CLAW_SWITCH2_SCANNER_CLIENT_ID = "claw_switch2_scanner";

// Reine Wertaenderungen innerhalb einer Bewegung werden erst ab dieser
// Aenderung (gegenueber dem zuletzt gesendeten Wert) gesendet, damit
// Stickrauschen keine Nachrichten und keine Geschwindigkeitsspruenge am Motor
// erzeugt.
static constexpr uint8_t JOYCON_MINIMUM_PERCENT_CHANGE = 3;

// Aenderungen innerhalb der Bewegung (Wert bleibt > 0) werden hoechstens so
// oft gesendet. Start und Stopp einer Richtung gehen immer sofort raus.
static constexpr unsigned long JOYCON_MINIMUM_PUBLISH_INTERVAL_MILLISECONDS = 100;

static ClawMqttConnection mqttConnection(
    CLAW_CLIENT_WIFI_SSID,
    CLAW_CLIENT_WIFI_PASSWORD,
    CLAW_MQTT_BROKER_HOST,
    CLAW_MQTT_BROKER_PORT,
    CLAW_SWITCH2_SCANNER_CLIENT_ID,
    CLAW_MQTT_USER_USERNAME,
    CLAW_MQTT_USER_PASSWORD,
    CLAW_CONNECTION_RETRY_INTERVAL_MS);

// --- Offline-Modus ----------------------------------------------------------
//
// Zum Testen ohne Broker. Zwei Gruende, warum das mehr ist als Bequemlichkeit:
//
// Erstens blockiert ein Verbindungsversuch ins Leere. ensureWifiConnected()
// in der gemeinsamen Library wartet in einer Schleife bis zu 10 Sekunden auf
// WiFi - und das in jedem Wiederholungsintervall.
//
// Zweitens teilen sich WiFi und BLE eine Antenne. Ist WiFi ganz aus, hat die
// BLE-Verbindung die Funkstrecke fuer sich; genau das braucht man, um einen
// Verbindungsabbruch von einem Koexistenzproblem zu unterscheiden.
//
// Ausgewertet wird trotzdem alles weiter: der Steuerzustand wird gemischt,
// begrenzt und auf der Konsole als [OFFLINE] ausgegeben, statt gesendet zu
// werden. Damit laesst sich die ganze Kette ausser dem letzten Schritt pruefen.
static bool isNetworkOutputOn = true;

// Je GERAET ein eigener Zustand - zusammengefuehrt wird erst beim Publizieren.
// Schluessel ist der Sitzungsplatz und NICHT der Controllertyp: zwei linke
// Joy-Cons gleichzeitig sind moeglich und haben sich nach Typ geschluesselt
// gegenseitig ueberschrieben.
static JoyConControlState controlStates[MAXIMUM_CONTROLLER_SESSIONS];
static volatile bool      hasPendingControlState = false;

// Rueckkanal: nur weiterreichen, ausgewertet wird in endstop_feedback. Laeuft
// im Arduino-Task (aufgerufen aus maintainConnection), nicht im BLE-Task.
static void onMqttMessage(char *topic, uint8_t *payload, unsigned int length)
{
  if (strcmp(topic, endstopStateTopic()) == 0)
  {
    handleEndstopStateMessage(payload, length);
  }
}

void beginControlOutput()
{
  Serial.printf("[MQTT]  Broker %s:%d, Topic %s\n",
                CLAW_MQTT_BROKER_HOST, CLAW_MQTT_BROKER_PORT, JOYCON_CONTROL_TOPIC);
  mqttConnection.setMessageCallback(onMqttMessage);
  mqttConnection.begin();
  mqttConnection.subscribe(endstopStateTopic());
}

void maintainControlOutput()
{
  if (!isNetworkOutputOn)
  {
    return;
  }
  mqttConnection.maintainConnection();
}

bool isNetworkOutputEnabled()
{
  return isNetworkOutputOn;
}

void setNetworkOutputEnabled(bool isEnabled)
{
  if (isEnabled == isNetworkOutputOn)
  {
    return;
  }
  isNetworkOutputOn = isEnabled;

  if (isEnabled)
  {
    WiFi.mode(WIFI_STA);
    Serial.println("[MQTT]  Netzwerk an - WiFi und MQTT werden wieder aufgebaut.");
    return;
  }

  // Funkstrecke wirklich freigeben, nicht nur das Senden unterlassen.
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[MQTT]  Netzwerk AUS. Steuerbefehle erscheinen nur noch als");
  Serial.println("[MQTT]  [OFFLINE] auf der Konsole. BLE hat die Antenne fuer sich.");
}

void publishControllerMetadata(const char *metadataName, const char *payload)
{
  // Gleiches Schema wie die Uptime der anderen Boards:
  // clawmachine/<name>/metadata/... (siehe python_server/clawmachine/claw_machine.py).
  char topic[128];
  snprintf(topic, sizeof(topic), "clawmachine/%s/metadata/%s",
           CLAW_SWITCH2_SCANNER_CLIENT_ID, metadataName);

  if (!isNetworkOutputOn)
  {
    Serial.printf("[OFFLINE] %s -> %s\n", topic, payload);
    return;
  }

  mqttConnection.publish(topic, payload);
  Serial.printf("[MQTT]  %s -> %s\n", topic, payload);
}

void storeControlState(size_t sessionIndex, const JoyConControlState &state)
{
  if (sessionIndex >= MAXIMUM_CONTROLLER_SESSIONS)
  {
    return;
  }
  controlStates[sessionIndex] = state;
  hasPendingControlState      = true;
}

void clearControlState(size_t sessionIndex)
{
  if (sessionIndex >= MAXIMUM_CONTROLLER_SESSIONS)
  {
    return;
  }
  controlStates[sessionIndex] = JoyConControlState();
  hasPendingControlState      = true;
}

JoyConControlState controlStateFor(size_t sessionIndex)
{
  if (sessionIndex >= MAXIMUM_CONTROLLER_SESSIONS)
  {
    return JoyConControlState();
  }
  return controlStates[sessionIndex];
}

// ALLE Controller zusammenfuehren: pro Richtung gilt der hoechste Wert. So
// funktioniert gleichzeitiges Bedienen sinnvoll, egal wie viele und welche
// Controller verbunden sind.
static JoyConControlState mergeControlStates()
{
  JoyConControlState state;
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    const JoyConControlState &other = controlStates[i];
    state.upPercent    = max(state.upPercent,    other.upPercent);
    state.downPercent  = max(state.downPercent,  other.downPercent);
    state.leftPercent  = max(state.leftPercent,  other.leftPercent);
    state.rightPercent = max(state.rightPercent, other.rightPercent);
    state.frontPercent = max(state.frontPercent, other.frontPercent);
    state.backPercent  = max(state.backPercent,  other.backPercent);
    state.clawOpenPercent  = max(state.clawOpenPercent,  other.clawOpenPercent);
    state.clawClosePercent = max(state.clawClosePercent, other.clawClosePercent);
  }

  // Beide Klauen-Tasten gleichzeitig: Klaue nicht bewegen, statt den ganzen
  // Zustand zu verwerfen - die Achsen sollen weiter fahren.
  if (state.clawOpenPercent > 0 && state.clawClosePercent > 0)
  {
    state.clawOpenPercent  = 0;
    state.clawClosePercent = 0;
  }

  return state;
}

void publishPendingControlState()
{
  if (!hasPendingControlState)
  {
    return;
  }

  hasPendingControlState = false;

  const JoyConControlState state = mergeControlStates();

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

  // Reine Wertaenderung innerhalb einer Bewegung: nicht oefter als im
  // Mindestabstand senden. Der Zustand bleibt vorgemerkt und geht beim
  // naechsten loop()-Durchlauf nach Ablauf des Abstands raus; der letzte Wert
  // kommt so immer an.
  static unsigned long lastPublishedAtMilliseconds = 0;
  const unsigned long  nowMilliseconds             = millis();
  if (hasPublishedOnce && !state.hasStartOrStopComparedTo(lastPublishedState))
  {
    // Kleine Schwankungen ignorieren (Stickrauschen); sie wuerden sich am
    // Motor nur als Geschwindigkeitssprung bemerkbar machen. Der Vollanschlag
    // ist davon ausgenommen, damit er sicher ankommt.
    const uint8_t largestChange = state.largestChangeComparedTo(lastPublishedState);
    const bool    isFullRange   = state.leftPercent == 100 || state.rightPercent == 100 ||
                                  state.frontPercent == 100 || state.backPercent == 100 ||
                                  state.upPercent == 100 || state.downPercent == 100;
    if (largestChange < JOYCON_MINIMUM_PERCENT_CHANGE && !isFullRange)
    {
      return;
    }
    if (nowMilliseconds - lastPublishedAtMilliseconds < JOYCON_MINIMUM_PUBLISH_INTERVAL_MILLISECONDS)
    {
      hasPendingControlState = true;
      return;
    }
  }
  lastPublishedAtMilliseconds = nowMilliseconds;

  char payload[200];
  snprintf(payload, sizeof(payload),
           "{\"up\":%d,\"down\":%d,\"left\":%d,\"right\":%d,\"front\":%d,\"back\":%d,"
           "\"clawOpen\":%d,\"clawClose\":%d}",
           state.upPercent, state.downPercent,
           state.leftPercent, state.rightPercent,
           state.frontPercent, state.backPercent,
           state.clawOpenPercent, state.clawClosePercent);

  if (isNetworkOutputOn)
  {
    mqttConnection.publish(JOYCON_CONTROL_TOPIC, payload);
    Serial.printf("[MQTT]  %s -> %s\n", JOYCON_CONTROL_TOPIC, payload);
  }
  else
  {
    Serial.printf("[OFFLINE] %s -> %s\n", JOYCON_CONTROL_TOPIC, payload);
  }

  lastPublishedState = state;
  hasPublishedOnce   = true;
}
