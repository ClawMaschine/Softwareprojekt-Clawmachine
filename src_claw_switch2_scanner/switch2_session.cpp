#include "switch2_session.h"

ControllerSession controllerSessions[MAXIMUM_CONTROLLER_SESSIONS];

ControllerSession *findSessionByAddress(const NimBLEAddress &address)
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

ControllerSession *findSessionByTracker(const ReportTracker &tracker)
{
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    if (&controllerSessions[i].specificReportTracker == &tracker ||
        &controllerSessions[i].commonReportTracker == &tracker)
    {
      return &controllerSessions[i];
    }
  }
  return nullptr;
}

size_t sessionIndexOf(const ControllerSession *session)
{
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    if (&controllerSessions[i] == session)
    {
      return i;
    }
  }
  return MAXIMUM_CONTROLLER_SESSIONS;
}

// ACHTUNG: mit zwei gleichartigen Controllern (zwei linke Joy-Cons) ist der Typ
// kein eindeutiger Schluessel - hier gewinnt der erste belegte Platz. Fuer die
// Vibration ist das hinnehmbar, fuer Zustaende nicht: die laufen deshalb ueber
// findSessionByTracker.
ControllerSession *findSessionByType(Switch2ControllerType type)
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

ControllerSession *claimFreeSession(const NimBLEAddress &address)
{
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    if (controllerSessions[i].isInUse)
    {
      continue;
    }

    ControllerSession &session = controllerSessions[i];
    session.isInUse                 = true;
    session.address                 = address;
    session.type                    = Switch2ControllerType::Unknown;
    session.commandCharacteristic   = nullptr;
    session.vibrationCharacteristic = nullptr;
    session.commandResponseLength   = 0;
    session.hasCommandResponse      = false;
    session.pairing.step            = PairingStep::Idle;
    resetReportTracker(session.specificReportTracker);
    resetReportTracker(session.commonReportTracker);
    return &session;
  }
  return nullptr;
}

void releaseSession(ControllerSession &session)
{
  // Die Characteristics gehoeren dem Client, der sich gleich selbst loescht.
  // Nur die Zeiger fallen lassen, nichts freigeben.
  session.commandCharacteristic   = nullptr;
  session.vibrationCharacteristic = nullptr;
  session.isInUse                 = false;
}

void resetAllSessionTrackers()
{
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    if (controllerSessions[i].isInUse)
    {
      resetReportTracker(controllerSessions[i].specificReportTracker);
      resetReportTracker(controllerSessions[i].commonReportTracker);
    }
  }
}
