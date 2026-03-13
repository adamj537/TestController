#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void register_swd_commands(void);

/* Connect SWD, read STM32 UID96 (0x1FFF7590), format as 24-char uppercase hex.
 * out must be at least 25 bytes.  Returns length written (24) or 0 on failure.
 * DUT must be powered and SWD lines connected before calling. */
int swd_read_dut_uid(char *out, size_t out_sz);

/* Download firmware binary from url and store it in the dut_fw partition.
 * Spawns a FreeRTOS task and returns immediately (call from DCMD handlers).
 * On success the image is available for "swd flash local". */
void dut_fw_store_from_url(const char *url);

#ifdef __cplusplus
}
#endif
