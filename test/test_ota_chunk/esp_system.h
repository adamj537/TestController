/* esp_system.h — native test stub */
#pragma once
extern int g_mock_esp_restart_called;
static inline void esp_restart(void) { g_mock_esp_restart_called++; }
