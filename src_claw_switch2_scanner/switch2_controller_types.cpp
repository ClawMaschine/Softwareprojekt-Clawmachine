#include "switch2_controller_types.h"

// ============================================================================
// Tastenbelegung der Joy-Cons.
//
// Die Belegung des RECHTEN Joy-Con ist vollstaendig selbst gemessen (siehe
// docs/mapping.txt): alle zwoelf Bits, jedes einzelne deckungsgleich mit
// ndeadlys hid_reports.md.
//
// Die Belegung des LINKEN Joy-Con stammt bisher nur aus der Dokumentation und
// ist noch nicht am Geraet nachgeprueft. Sie ist deshalb in der Tabelle unten
// als unbestaetigt markiert (isButtonMappingVerified = false) und wird beim
// Verbinden entsprechend gemeldet.
// ============================================================================

#define ARRAY_ELEMENT_COUNT(array) (sizeof(array) / sizeof((array)[0]))

// Report 0x08 - gemessen am Geraet
static const Switch2ButtonMapping JOYCON_RIGHT_BUTTON_MAPPINGS[] = {
    {0x02, 0x01, "B"},
    {0x02, 0x02, "A"},
    {0x02, 0x04, "Y"},
    {0x02, 0x08, "X"},
    {0x02, 0x10, "R"},
    {0x02, 0x20, "ZR"},
    {0x02, 0x40, "Plus"},
    {0x02, 0x80, "Stick-Klick"},
    {0x03, 0x01, "Home"},
    {0x03, 0x10, "C"},
    {0x03, 0x40, "SR"},
    {0x03, 0x80, "SL"},
};

// Report 0x07 - aus hid_reports.md, noch nicht am Geraet geprueft
static const Switch2ButtonMapping JOYCON_LEFT_BUTTON_MAPPINGS[] = {
    {0x02, 0x01, "Unten"},
    {0x02, 0x02, "Rechts"},
    {0x02, 0x04, "Links"},
    {0x02, 0x08, "Oben"},
    {0x02, 0x10, "L"},
    {0x02, 0x20, "ZL"},
    {0x02, 0x40, "Minus"},
    {0x02, 0x80, "Stick-Klick"},
    {0x03, 0x01, "Capture"},
    {0x03, 0x40, "SR"},
    {0x03, 0x80, "SL"},
};

const Switch2ReportDescription CONTROLLER_SPECIFIC_REPORTS[] = {
    {Switch2ControllerType::JoyConLeft, "cc1bbbb5-7354-4d32-a716-a81cb241a32a",
     "ce49a830-dced-48ae-931e-c8cf88aadbea", "289326cb-a471-485d-a8f4-240c14f18241",
     "Joy-Con 2 (L)", "Report 0x07", 0x05, NO_STICK_OFFSET,
     JOYCON_LEFT_BUTTON_MAPPINGS, ARRAY_ELEMENT_COUNT(JOYCON_LEFT_BUTTON_MAPPINGS), false},
    {Switch2ControllerType::JoyConRight, "d5a9e01e-2ffc-4cca-b20c-8b67142bf442",
     "65a724b3-f1e7-4a61-8078-a342376b27ff", "fa19b0fb-cd1f-46a7-84a1-bbb09e00c149",
     "Joy-Con 2 (R)", "Report 0x08", 0x05, NO_STICK_OFFSET,
     JOYCON_RIGHT_BUTTON_MAPPINGS, ARRAY_ELEMENT_COUNT(JOYCON_RIGHT_BUTTON_MAPPINGS), true},
    {Switch2ControllerType::ProController, "7492866c-ec3e-4619-8258-32755ffcc0f8",
     "3dacbc7e-6955-40b5-8eaf-6f9809e8b379", "cc483f51-9258-427d-a939-630c31f72b05",
     "Pro Controller 2", "Report 0x09", 0x05, 0x08,
     nullptr, 0, false},
    {Switch2ControllerType::GameCube, "8261cba1-9435-420c-84d6-f0c75a2c8e4d",
     "af95885e-44b3-4a24-9cf0-483cc129469a", "3f8fb670-ab25-45bf-b540-38c72834d064",
     "NSO GameCube Controller", "Report 0x0A", 0x05, NO_STICK_OFFSET,
     nullptr, 0, false},
};

const size_t CONTROLLER_SPECIFIC_REPORT_COUNT =
    ARRAY_ELEMENT_COUNT(CONTROLLER_SPECIFIC_REPORTS);
