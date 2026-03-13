#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void register_ota_commands(void);

/* Trigger OTA from a DCMD handler — spawns a FreeRTOS task and returns
 * immediately.  url must be http:// or https://.  Reboots on success. */
void ota_start_from_url(const char *url);

#ifdef __cplusplus
}
#endif
