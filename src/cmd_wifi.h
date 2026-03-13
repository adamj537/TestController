#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize WiFi driver and netif stack.
 * Must be called once before register_wifi_commands() or net_console_start().
 * Automatically attempts to connect if NVS credentials are present. */
void wifi_init(void);

void register_wifi_commands(void);

#ifdef __cplusplus
}
#endif
