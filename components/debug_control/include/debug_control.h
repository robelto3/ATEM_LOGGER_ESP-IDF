#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Společný runtime přepínač diagnostických výpisů.
// Výchozí stav po startu je vypnuto.
// Zapíná/vypíná se přes UART konzoli příkazy: debug, debug on, debug off.

void debug_control_init(void);
void debug_control_set_enabled(bool enabled);
bool debug_control_is_enabled(void);

#ifdef __cplusplus
}
#endif
