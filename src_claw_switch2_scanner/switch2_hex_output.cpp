#include "switch2_hex_output.h"

#include <Arduino.h>

void printHexBytes(const uint8_t *bytes, size_t length)
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
