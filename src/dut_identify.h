#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Capture DUT UART boot output and extract a unique identifier.
 *
 * Installs UART1 (RX on GPIO44) for up to timeout_ms, scans for the
 * longest run of hex characters (0-9, A-F, a-f) of at least MIN_HEX_RUN
 * bytes, and copies it into out[].
 *
 * Returns: length of the identifier written, or 0 if nothing useful found.
 *
 * UART driver is installed and deleted within this call.
 * Caller must ensure UART1 driver is not already installed. */
int dut_identify_uart(char *out, size_t out_sz, int timeout_ms);

#ifdef __cplusplus
}
#endif
