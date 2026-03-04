#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start TCP console server on the given port (typically 4242).
 * All stdout/stderr output is tee'd to the connected TCP client via linker
 * wrap on _write_r.  Single client at a time; next accept() queues on close.
 * Must be called after wifi_init() (requires esp_netif to be up). */
void net_console_start(uint16_t port);

void net_console_stop(void);

#ifdef __cplusplus
}
#endif
