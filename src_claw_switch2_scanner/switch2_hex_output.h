#pragma once

#include <stddef.h>
#include <stdint.h>

// Hexausgabe eines Bytepuffers, alle 8 Byte ein Trennzeichen. Eigene Datei,
// weil drei Module sie brauchen: die Report-Verfolgung, die Befehlsantwort und
// die Advertisement-Ausgabe im Scan. Keines davon ist dem anderen
// untergeordnet, also gehoert sie in keines von ihnen.
void printHexBytes(const uint8_t *bytes, size_t length);
