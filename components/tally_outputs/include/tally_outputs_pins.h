#pragma once

#include "driver/gpio.h"

// =====================================================
// TALLY OUTPUT PIN CONFIG - EDIT HERE
// =====================================================
// Sem sahat při návrhu PCB nebo při přeházení výstupů.
// Pořadí odpovídá ATEM vstupům 1..8.
//
// Program tally:
//   PGM 1 -> první pin v TALLY_OUTPUT_PROGRAM_PINS
//   PGM 2 -> druhý pin ...
//
// Preview tally:
//   PVW 1 -> první pin v TALLY_OUTPUT_PREVIEW_PINS
//   PVW 2 -> druhý pin ...
//
// Když bude budoucí HW aktivní v nule, změň jen:
//   #define TALLY_OUTPUT_ACTIVE_LEVEL 0
//
// TALLY_OUTPUT_SHOW_WITHOUT_ATEM:
//   1 = tally výstupy sledují app_state i bez ATEM spojení
//       (vhodné pro domácí test přes fake cut GPIO46)
//   0 = při ztrátě ATEM spojení se všechny tally výstupy zhasnou
//       (přísnější ostrý provoz)
// =====================================================

#define TALLY_OUTPUT_INPUT_COUNT 8

#define TALLY_OUTPUT_ACTIVE_LEVEL   1
#define TALLY_OUTPUT_INACTIVE_LEVEL 0

#define TALLY_OUTPUT_SHOW_WITHOUT_ATEM 1

#define TALLY_OUTPUT_PROGRAM_PINS { \
    GPIO_NUM_6,  /* PGM 1 */ \
    GPIO_NUM_14, /* PGM 2 */ \
    GPIO_NUM_15, /* PGM 3 */ \
    GPIO_NUM_16, /* PGM 4 */ \
    GPIO_NUM_17, /* PGM 5 */ \
    GPIO_NUM_18, /* PGM 6 */ \
    GPIO_NUM_19, /* PGM 7 */ \
    GPIO_NUM_54  /* PGM 8 */ \
}

#define TALLY_OUTPUT_PREVIEW_PINS { \
    GPIO_NUM_33, /* PVW 1 */ \
    GPIO_NUM_32, /* PVW 2 */ \
    GPIO_NUM_27, /* PVW 3 */ \
    GPIO_NUM_26, /* PVW 4 */ \
    GPIO_NUM_23, /* PVW 5 */ \
    GPIO_NUM_22, /* PVW 6 */ \
    GPIO_NUM_21, /* PVW 7 */ \
    GPIO_NUM_20  /* PVW 8 */ \
}
