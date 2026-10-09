#include "switch2_report_parser.h"

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

JoyConControlState readControlStateFromReport(const uint8_t          *report,
                                              size_t                  length,
                                              const StickCalibration &stick)
{
  JoyConControlState state;
  if (length <= JOYCON_BUTTON_BYTE)
  {
    return state;
  }

  const uint8_t buttons = report[JOYCON_BUTTON_BYTE];

  state.leftButton  = (buttons & JOYCON_MASK_DIRECTION_LEFT) != 0;
  state.rightButton = (buttons & JOYCON_MASK_DIRECTION_RIGHT) != 0;
  state.frontButton = (buttons & JOYCON_MASK_DIRECTION_DOWN) != 0;
  state.backButton  = (buttons & JOYCON_MASK_DIRECTION_UP) != 0;
  state.upButton    = (buttons & JOYCON_MASK_SHOULDER_UPPER) != 0;
  state.downButton  = (buttons & JOYCON_MASK_SHOULDER_LOWER) != 0;

  StickDeflection deflection;
  if (!readStickDeflection(report, length, stick, deflection))
  {
    return state;
  }

  if (deflection.horizontalPercent >= JOYCON_STICK_DIRECTION_THRESHOLD_PERCENT)
  {
    state.rightButton = true;  // Stick nach rechts
  }
  else if (deflection.horizontalPercent <= -JOYCON_STICK_DIRECTION_THRESHOLD_PERCENT)
  {
    state.leftButton = true;   // Stick nach links
  }

  if (deflection.verticalPercent >= JOYCON_STICK_DIRECTION_THRESHOLD_PERCENT)
  {
    state.backButton = true;   // Stick nach oben
  }
  else if (deflection.verticalPercent <= -JOYCON_STICK_DIRECTION_THRESHOLD_PERCENT)
  {
    state.frontButton = true;  // Stick nach unten
  }

  return state;
}
