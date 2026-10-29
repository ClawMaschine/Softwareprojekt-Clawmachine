#include "joycon_power_status.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "joycon_control_output.h"
#include "switch2_session.h"

// Ein Platz je Sitzung - also je GERAET. Der Controllertyp taugt nicht als
// Schluessel, siehe Kopfkommentar im Header.
struct PowerStatusSlot
{
  bool             isInUse;
  char             displayName[48];  // Name und Adresse, fuer die Ausgabe kopiert
  char             metadataName[48]; // MQTT-Suffix, aus der Adresse gebildet

  JoyConPowerState latestState;
  JoyConPowerState reportedState;
  bool             hasReportedOnce;
};

static PowerStatusSlot powerStatusSlots[MAXIMUM_CONTROLLER_SESSIONS];

void updatePowerStatus(size_t      sessionIndex,
                       const char *controllerName,
                       const char *address,
                       const JoyConPowerState &state)
{
  if (sessionIndex >= MAXIMUM_CONTROLLER_SESSIONS || !state.isKnown)
  {
    return;
  }

  PowerStatusSlot &slot = powerStatusSlots[sessionIndex];

  if (!slot.isInUse)
  {
    // Beschriftung einmal je Verbindung festhalten. Die Adresse gehoert dazu:
    // "Joy-Con 2 (L)" allein ist nicht eindeutig, wenn zwei linke verbunden
    // sind.
    snprintf(slot.displayName, sizeof(slot.displayName), "%s %s",
             controllerName != nullptr ? controllerName : "Controller",
             address != nullptr ? address : "");

    // MQTT-Suffix aus der Adresse ohne Doppelpunkte - eindeutig je Geraet.
    size_t written = 0;
    written += snprintf(slot.metadataName, sizeof(slot.metadataName), "battery/");
    for (const char *c = address; c != nullptr && *c != '\0'; c++)
    {
      if (*c == ':' || written + 1 >= sizeof(slot.metadataName))
      {
        continue;
      }
      slot.metadataName[written++] = *c;
    }
    slot.metadataName[written] = '\0';

    slot.isInUse = true;
  }

  // Nur hinterlegen. Verglichen, ausgegeben und publiziert wird im loop().
  slot.latestState = state;
}

static void printPowerStatus(const PowerStatusSlot &slot)
{
  if (!slot.isInUse || !slot.latestState.isKnown)
  {
    return;
  }

  Serial.printf("[AKKU]  %s: %u %%%s%s\n",
                slot.displayName,
                static_cast<unsigned>(slot.latestState.batteryPercent),
                slot.latestState.isCharging ? ", laedt" : "",
                slot.latestState.hasExternalPower ? ", am Netzteil" : "");
}

void publishPendingPowerStatus()
{
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    PowerStatusSlot &slot = powerStatusSlots[i];
    if (!slot.isInUse || !slot.latestState.isKnown)
    {
      continue;
    }
    if (slot.hasReportedOnce && slot.latestState.equals(slot.reportedState))
    {
      continue;
    }

    printPowerStatus(slot);

    char payload[96];
    snprintf(payload, sizeof(payload),
             "{\"percent\":%u,\"charging\":%d,\"externalPower\":%d}",
             static_cast<unsigned>(slot.latestState.batteryPercent),
             slot.latestState.isCharging ? 1 : 0,
             slot.latestState.hasExternalPower ? 1 : 0);
    publishControllerMetadata(slot.metadataName, payload);

    slot.reportedState   = slot.latestState;
    slot.hasReportedOnce = true;
  }
}

void printPowerStatusNow()
{
  bool hasAnySlot = false;
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    if (powerStatusSlots[i].isInUse)
    {
      printPowerStatus(powerStatusSlots[i]);
      hasAnySlot = true;
    }
  }

  if (!hasAnySlot)
  {
    Serial.println("[AKKU]  Kein verbundener Controller - kein Stand bekannt.");
    return;
  }
  Serial.printf("[AKKU]  Der Controller meldet Zehntelstufen, %u %% ist das Maximum.\n",
                static_cast<unsigned>(JOYCON_POWER_HIGHEST_LEVEL * JOYCON_POWER_PERCENT_PER_LEVEL));
}

void clearPowerStatus(size_t sessionIndex)
{
  if (sessionIndex >= MAXIMUM_CONTROLLER_SESSIONS)
  {
    return;
  }
  powerStatusSlots[sessionIndex] = PowerStatusSlot{};
}
