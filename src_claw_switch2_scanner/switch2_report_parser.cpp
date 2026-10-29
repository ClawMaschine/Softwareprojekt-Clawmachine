#include "switch2_report_parser.h"

JoyConPowerState readPowerStateFromReport(const uint8_t *report, size_t length)
{
  JoyConPowerState state;
  if (length <= JOYCON_POWER_INFO_BYTE)
  {
    return state;
  }

  const uint8_t powerInfo = report[JOYCON_POWER_INFO_BYTE];

  uint8_t batteryLevel =
      (powerInfo >> JOYCON_POWER_BATTERY_LEVEL_SHIFT) & JOYCON_POWER_BATTERY_LEVEL_MASK;
  // Das Feld ist vier Bit breit, dokumentiert sind nur 0-9. Ein hoeherer Wert
  // waere nicht gemessen, sondern geraten - deshalb begrenzen statt rechnen.
  if (batteryLevel > JOYCON_POWER_HIGHEST_LEVEL)
  {
    batteryLevel = JOYCON_POWER_HIGHEST_LEVEL;
  }

  state.hasExternalPower = (powerInfo & JOYCON_POWER_MASK_EXTERNAL_POWER) != 0;
  state.isCharging       = (powerInfo & JOYCON_POWER_MASK_CHARGING) != 0;
  state.batteryPercent   = static_cast<uint8_t>(batteryLevel * JOYCON_POWER_PERCENT_PER_LEVEL);
  state.isKnown          = true;
  return state;
}

StickPosition unpackPackedStickValues(const uint8_t *bytes)
{
  StickPosition stick;
  stick.x = static_cast<uint16_t>(bytes[0] | ((bytes[1] & 0x0F) << 8));
  stick.y = static_cast<uint16_t>((bytes[1] >> 4) | (bytes[2] << 4));
  return stick;
}

int8_t convertAxisToPercent(uint16_t rawValue, uint16_t centerValue)
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

bool isInsideDeadzone(int8_t percent)
{
  return percent > -STICK_DEADZONE_PERCENT && percent < STICK_DEADZONE_PERCENT;
}

bool readStickDeflection(const uint8_t          *report,
                         size_t                  length,
                         const StickCalibration &stick,
                         StickDeflection        &deflection)
{
  if (!stick.hasCenter || stick.offset == NO_STICK_OFFSET)
  {
    return false;
  }
  if (static_cast<size_t>(stick.offset) + STICK_PACKED_BYTE_COUNT > length)
  {
    return false;
  }

  const StickPosition raw = unpackPackedStickValues(&report[stick.offset]);
  deflection.horizontalPercent = convertAxisToPercent(raw.x, stick.center.x);
  deflection.verticalPercent   = convertAxisToPercent(raw.y, stick.center.y);
  return true;
}

// Bewusst eigene Helfer statt max() und abs() aus Arduino.h: dieses Modul
// bleibt frei von Arduino-Abhaengigkeiten, damit es sich ohne Hardware
// nachrechnen laesst.
static uint8_t largerPercent(uint8_t firstPercent, uint8_t secondPercent)
{
  return firstPercent > secondPercent ? firstPercent : secondPercent;
}

static uint8_t percentDifference(uint8_t firstPercent, uint8_t secondPercent)
{
  return firstPercent > secondPercent ? static_cast<uint8_t>(firstPercent - secondPercent)
                                      : static_cast<uint8_t>(secondPercent - firstPercent);
}

uint8_t JoyConControlState::largestChangeComparedTo(const JoyConControlState &other) const
{
  const uint8_t changes[] = {
      percentDifference(upPercent, other.upPercent),
      percentDifference(downPercent, other.downPercent),
      percentDifference(leftPercent, other.leftPercent),
      percentDifference(rightPercent, other.rightPercent),
      percentDifference(frontPercent, other.frontPercent),
      percentDifference(backPercent, other.backPercent),
      percentDifference(clawOpenPercent, other.clawOpenPercent),
      percentDifference(clawClosePercent, other.clawClosePercent)};

  uint8_t largestChange = 0;
  for (uint8_t change : changes)
  {
    largestChange = largerPercent(largestChange, change);
  }
  return largestChange;
}

// Stick-Auslenkung hinter der Schwelle auf 0-100 % umrechnen und auf
// JOYCON_PERCENT_STEP runden. Unter der Schwelle 0.
//
// previousPercent ist der zuletzt ermittelte Wert dieser Richtung: war sie
// schon aktiv, gilt die niedrigere Freigabeschwelle (Hysterese).
static uint8_t convertStickDeflectionToPercent(int8_t  signedPercent,
                                               bool    isPositiveDirection,
                                               uint8_t previousPercent)
{
  const int16_t directedPercent = isPositiveDirection ? signedPercent : -signedPercent;
  const int16_t activeThresholdPercent = previousPercent > 0
                                             ? JOYCON_STICK_RELEASE_THRESHOLD_PERCENT
                                             : JOYCON_STICK_DIRECTION_THRESHOLD_PERCENT;
  if (directedPercent < activeThresholdPercent)
  {
    return 0;
  }

  // Oberhalb der Freigabeschwelle weiter nach der Aktivierungsschwelle
  // skalieren, damit die Geschwindigkeit bei 30 % Auslenkung bei 0 % beginnt.
  int16_t scaledPercent = (directedPercent - JOYCON_STICK_DIRECTION_THRESHOLD_PERCENT) * 100 /
                          (100 - JOYCON_STICK_DIRECTION_THRESHOLD_PERCENT);
  if (scaledPercent < 0)
  {
    scaledPercent = 0;
  }

  int16_t roundedPercent =
      (scaledPercent + JOYCON_PERCENT_STEP / 2) / JOYCON_PERCENT_STEP * JOYCON_PERCENT_STEP;
  if (roundedPercent > 100)
  {
    roundedPercent = 100;
  }

  // Zwischen Freigabe- und Aktivierungsschwelle ergibt die Skalierung 0 - eine
  // schon aktive Richtung wuerde dort trotz Hysterese auf "losgelassen"
  // zurueckfallen und an der Schwelle staendig zwischen Start und Stopp
  // wechseln. Sie bleibt deshalb auf dem kleinsten Schritt stehen, bis die
  // Auslenkung wirklich unter die Freigabeschwelle faellt (dann greift der
  // fruehe Ausstieg oben).
  if (roundedPercent == 0 && previousPercent > 0)
  {
    roundedPercent = JOYCON_PERCENT_STEP;
  }

  return static_cast<uint8_t>(roundedPercent);
}

JoyConControlState readControlStateFromReport(Switch2ControllerType     controllerType,
                                              const uint8_t            *report,
                                              size_t                    length,
                                              const StickCalibration   &stick,
                                              const JoyConControlState &previousState)
{
  JoyConControlState state;
  if (length <= JOYCON_BUTTON_BYTE)
  {
    return state;
  }

  const bool isLeftJoyCon  = controllerType == Switch2ControllerType::JoyConLeft;
  const bool isRightJoyCon = controllerType == Switch2ControllerType::JoyConRight;

  const uint8_t buttons = report[JOYCON_BUTTON_BYTE];

  // Schultertasten beider Reihen: links oeffnet, rechts schliesst die Klaue.
  const bool isShoulderPressed =
      (buttons & (JOYCON_MASK_SHOULDER_UPPER | JOYCON_MASK_SHOULDER_LOWER)) != 0;
  if (isShoulderPressed)
  {
    if (isLeftJoyCon)
    {
      state.clawOpenPercent = JOYCON_BUTTON_PRESSED_PERCENT;
    }
    else if (isRightJoyCon)
    {
      state.clawClosePercent = JOYCON_BUTTON_PRESSED_PERCENT;
    }
  }

  // Nur der linke Joy-Con hat ein Steuerkreuz. Am rechten liegen auf denselben
  // Masken A/B/X/Y - die bleiben bewusst unbelegt.
  if (isLeftJoyCon)
  {
    state.leftPercent  = (buttons & JOYCON_MASK_DIRECTION_RIGHT) != 0 ? JOYCON_BUTTON_PRESSED_PERCENT : 0;
    state.rightPercent = (buttons & JOYCON_MASK_DIRECTION_LEFT)  != 0 ? JOYCON_BUTTON_PRESSED_PERCENT : 0;
    state.frontPercent = (buttons & JOYCON_MASK_DIRECTION_DOWN)  != 0 ? JOYCON_BUTTON_PRESSED_PERCENT : 0;
    state.backPercent  = (buttons & JOYCON_MASK_DIRECTION_UP)    != 0 ? JOYCON_BUTTON_PRESSED_PERCENT : 0;
  }

  StickDeflection deflection;
  if (!readStickDeflection(report, length, stick, deflection))
  {
    return state;
  }

  if (isLeftJoyCon)
  {
    // Stick und Steuerkreuz wirken zusammen: es gilt der hoehere Wert.
    state.leftPercent = largerPercent(
        state.leftPercent,
        convertStickDeflectionToPercent(deflection.horizontalPercent, true, previousState.leftPercent));
    state.rightPercent = largerPercent(
        state.rightPercent,
        convertStickDeflectionToPercent(deflection.horizontalPercent, false, previousState.rightPercent));
    state.backPercent = largerPercent(
        state.backPercent,
        convertStickDeflectionToPercent(deflection.verticalPercent, true, previousState.backPercent));
    state.frontPercent = largerPercent(
        state.frontPercent,
        convertStickDeflectionToPercent(deflection.verticalPercent, false, previousState.frontPercent));
  }
  else if (isRightJoyCon)
  {
    state.downPercent =
        convertStickDeflectionToPercent(deflection.verticalPercent, true, previousState.downPercent);
    state.upPercent =
        convertStickDeflectionToPercent(deflection.verticalPercent, false, previousState.upPercent);
  }

  return state;
}
