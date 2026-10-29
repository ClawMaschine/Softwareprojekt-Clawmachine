#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Protokollkonstanten der Switch-2-Controller.
//
// Diese Datei enthaelt AUSSCHLIESSLICH Konstanten - kein Verhalten, keine
// Abhaengigkeiten. Alles hier ist aus der Reverse-Engineering-Doku oder aus
// Mitschnitten der echten Konsole uebernommen, nicht geraten.
//
// Quellen:
// - https://github.com/ndeadly/switch2_controller_research
//   (bluetooth_interface.md, hid_reports.md, commands.md)
// - https://gist.github.com/ndeadly/7d27aa63e2f653a902a2474dbcbc08b3
// - https://qiita.com/Tsukusim/items/5a88b76a2e8e0e69e412
//   (funktionierender NimBLE-Nachbau auf einem normalen ESP32)
//
// WICHTIG: Es darf niemals SMP-Pairing initiiert werden. Laut Doku trennt der
// Controller die Verbindung sofort, wenn der Host das versucht. Deshalb
// setSecurityAuth(false, false, false) und kein secureConnection().
// ============================================================================

// --- Geraeteerkennung im Advertisement (bluetooth_interface.md) -------------

static constexpr uint16_t NINTENDO_MANUFACTURER_ID  = 0x0553;
static constexpr uint16_t NINTENDO_VENDOR_ID        = 0x057E;
static constexpr uint16_t SWITCH2_LOWEST_PRODUCT_ID = 0x2060;

static constexpr size_t MANUFACTURER_DATA_MINIMUM_LENGTH      = 0x13;
static constexpr size_t MANUFACTURER_DATA_VENDOR_OFFSET       = 0x05;
static constexpr size_t MANUFACTURER_DATA_PRODUCT_OFFSET      = 0x07;
static constexpr size_t MANUFACTURER_DATA_WAKE_FLAG_OFFSET    = 0x0B;
static constexpr size_t MANUFACTURER_DATA_HOST_ADDRESS_OFFSET = 0x0C;
static constexpr size_t MANUFACTURER_DATA_HOST_ADDRESS_LENGTH = 6;

// Wake-Advertisement: der Controller meldet sich nur kurz fuer seinen bereits
// gekoppelten Host, nicht zum Koppeln.
static constexpr uint8_t MANUFACTURER_DATA_WAKE_FLAG_VALUE = 0x81;

// --- GATT-Struktur ----------------------------------------------------------

// Service, unter dem alle Input-Reports haengen (GATT-Handles 0x0008-0x002a).
static constexpr const char *SWITCH2_INPUT_SERVICE_UUID =
    "ab7de9be-89fe-49ad-828f-118f09df7fd0";

// Input Report 0x05 auf Handle 0x000A - bei ALLEN Controllertypen vorhanden.
static constexpr const char *COMMON_INPUT_REPORT_UUID =
    "ab7de9be-89fe-49ad-828f-118f09df7fd2";

// Handle 0x001A, NOTIFY: Antwort im selben Headerformat, Richtungsbyte 0x01.
// Diese Characteristic ist bei allen Controllertypen dieselbe.
static constexpr const char *SWITCH2_COMMAND_RESPONSE_UUID =
    "c765a961-d9d8-4d36-a20a-5315b111836a";

// Report 0x05 fuehrt beide Sticks in einem gemeinsamen Feld (hid_reports.md).
static constexpr uint8_t COMMON_REPORT_LEFT_STICK_OFFSET  = 0x0A;
static constexpr uint8_t COMMON_REPORT_RIGHT_STICK_OFFSET = 0x0D;

// Laengster beobachteter Report ist 63 Byte. Gilt auch fuer Befehlsantworten,
// die im selben Puffermuster abgelegt werden.
static constexpr size_t MAXIMUM_REPORT_LENGTH = 64;

// --- Befehlskanal -----------------------------------------------------------
//
// Befehle gehen an Handle 0x0016 ("Rumble + Command"), NICHT an das
// naheliegendere Handle 0x0014 ("Command"). Das ist am Mitschnitt der echten
// Konsole abgelesen: in btle_joycon2_pairing_decrypted.pcapng gehen alle 24
// Befehle - Vibration, LEDs, Firmware-Abfrage, Pairing - an 0x0016, und 0x0014
// wird kein einziges Mal benutzt. Deshalb nehmen wir den belegten Weg.
//
// Vor dem Befehlsheader stehen dabei 17 Byte: eine Report-ID und 16 Byte
// HD-Rumble-Daten. Wer nur einen Befehl schicken will, laesst sie auf Null.
//
// Die UUID von 0x0016 ist wie die Input-UUID pro Controllertyp verschieden und
// steht deshalb in CONTROLLER_SPECIFIC_REPORTS (switch2_controller_types.h).

// Report-ID (1 Byte) + HD-Rumble (16 Byte) vor dem Befehlsheader.
static constexpr size_t COMMAND_RUMBLE_PREFIX_LENGTH = 17;

// --- Befehlsformat (commands.md) --------------------------------------------
//
// Der Header ist 8 Byte lang, danach folgen die Befehlsdaten:
//
//   0x0  Command ID        z.B. 0x0A = Vibration
//   0x1  Richtung          0x91 = Host -> Geraet, 0x01 = Geraet -> Host
//   0x2  Transport         0x01 = Bluetooth
//   0x3  Subcommand ID
//   0x4  unbekannt         0x00
//   0x5  Datenlaenge       Anzahl der Bytes nach dem Header
//   0x6  reserviert        0x00
//   0x7  reserviert        0x00

static constexpr size_t  COMMAND_HEADER_LENGTH             = 8;
static constexpr uint8_t COMMAND_DIRECTION_HOST_TO_DEVICE  = 0x91;
static constexpr uint8_t COMMAND_DIRECTION_DEVICE_TO_HOST  = 0x01;
static constexpr uint8_t COMMAND_TRANSPORT_BLUETOOTH       = 0x01;

// Groesster bisher benoetigter Befehl ist der Pairing-Schluesseltausch mit
// 17 Datenbytes. 32 laesst Luft, ohne den Stack unnoetig zu belasten.
static constexpr size_t MAXIMUM_COMMAND_DATA_LENGTH = 32;

// --- Vibration --------------------------------------------------------------

static constexpr uint8_t COMMAND_ID_VIBRATION             = 0x0A;
static constexpr uint8_t VIBRATION_SUBCOMMAND_PLAY_SAMPLE = 0x02;

// Vordefinierte Muster im Controller. 0x00 stoppt eine laufende Ausgabe.
static constexpr uint8_t VIBRATION_SAMPLE_SILENCE      = 0x00;
static constexpr uint8_t VIBRATION_SAMPLE_SOFT_CLICK   = 0x03;
static constexpr uint8_t VIBRATION_SAMPLE_STRONG_CLICK = 0x05;

// Kurz und deutlich spuerbar - als Rueckmeldung auf einen Tastendruck genau
// richtig, ohne den Controller sekundenlang brummen zu lassen. Die Konsole
// selbst schickt an dieser Stelle 0x03; falls 0x05 am Geraet nicht ankommt,
// ist der Wechsel auf VIBRATION_SAMPLE_SOFT_CLICK der erste Versuch, denn
// dieses Muster ist im Mitschnitt byteweise belegt.
static constexpr uint8_t VIBRATION_SAMPLE_FOR_BUTTON_FEEDBACK = VIBRATION_SAMPLE_STRONG_CLICK;

// Sample-ID plus drei ungenutzte Bytes.
static constexpr size_t VIBRATION_SAMPLE_DATA_LENGTH = 4;

// --- Player-LEDs ------------------------------------------------------------
//
// Im Pairing-Modus laeuft das Licht ueber die vier LEDs. Setzt der Host ein
// festes Muster, hoert das auf - das ist die sichtbare Bestaetigung, dass der
// Controller nicht mehr auf Kopplung wartet, sondern bedient wird.

static constexpr uint8_t COMMAND_ID_PLAYER_LED = 0x09;

// Subcommand 0x07 nimmt ein Bitmuster (Bits 0-3 = die vier LEDs), 0x08
// schaltet das Blinken des aktuellen Musters ein oder aus.
static constexpr uint8_t PLAYER_LED_SUBCOMMAND_SET_PATTERN = 0x07;
static constexpr uint8_t PLAYER_LED_SUBCOMMAND_FLASH       = 0x08;

// Je ein Byte: das Muster bzw. der Ein/Aus-Wert.
static constexpr size_t PLAYER_LED_DATA_LENGTH = 1;

// Links die erste LED, rechts die zweite - so sind die beiden Joy-Cons am
// Geraet auseinanderzuhalten.
static constexpr uint8_t PLAYER_LED_PATTERN_JOYCON_LEFT  = 0x01;
static constexpr uint8_t PLAYER_LED_PATTERN_JOYCON_RIGHT = 0x02;

// Alle vier an - nicht zu uebersehen und damit der Nachweis, dass der Befehl
// ueberhaupt greift.
static constexpr uint8_t PLAYER_LED_PATTERN_ALL = 0x0F;

// --- Kopplungsspeicher des Controllers --------------------------------------
//
// Command 0x03 ist die Initialisierungsgruppe. Subcommand 0x08 verwirft die im
// Controller gespeicherte Kopplung - das Gegenstueck zum Loeschen auf unserer
// Seite, damit der Controller nicht weiter einen Host sucht, der ihn vergessen
// hat.
static constexpr uint8_t COMMAND_ID_INITIALISATION                     = 0x03;
static constexpr uint8_t INITIALISATION_SUBCOMMAND_CLEAR_PAIRING_INFO  = 0x08;
