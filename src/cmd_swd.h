#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void register_swd_commands(void);

/* Error codes returned by swd_flash_dut_url(), matching PRD-test-execution-system §flash_dut. */
typedef enum {
    SWD_FLASH_OK = 0,
    SWD_FLASH_ERR_DOWNLOAD,     /* HTTP GET failed, timed out, or buffer overflow */
    SWD_FLASH_ERR_SWD_CONNECT,  /* Could not establish SWD connection (DUT absent/unpowered) */
    SWD_FLASH_ERR_ERASE,        /* Flash erase failed */
    SWD_FLASH_ERR_PROGRAM,      /* Flash program failed */
    SWD_FLASH_ERR_VERIFY,       /* Read-back mismatch after programming */
    SWD_FLASH_ERR_TIMEOUT,      /* Total operation exceeded timeout_s */
} swd_flash_err_t;

/* Human-readable string for DDATA error_code field (e.g. "DOWNLOAD_FAIL"). */
const char *swd_flash_err_str(swd_flash_err_t err);

/* Firmware partition target — selects between PFW (test firmware) and
 * production firmware slots.  Both use the same dut_fw_hdr_t format. */
typedef enum {
    SWD_FW_TARGET_PFW  = 0,  /* dut_fw  partition — test-mode firmware */
    SWD_FW_TARGET_PROD = 1,  /* prod_fw partition — agency-approved firmware */
} swd_fw_target_t;

/* Download firmware from url and program DUT via SWD. Synchronous — blocks
 * until complete. verify and timeout_s are accepted for API compatibility
 * (verify read-back not yet implemented; HTTP timeout is fixed at 15 s).
 * fw_size_out receives bytes programmed, or 0 on failure (may be NULL).
 * target selects which stored partition to load from (SWD_FW_TARGET_PFW default).
 * Returns SWD_FLASH_OK on success. */
swd_flash_err_t swd_flash_dut_url(const char *url, bool verify,
                                   uint32_t timeout_s, uint32_t *fw_size_out,
                                   swd_fw_target_t target);

/* Program DUT from previously stored partition image (no HTTP download).
 * Synchronous — blocks until complete. target selects pfw or prod partition. */
swd_flash_err_t swd_flash_dut_local(bool verify, uint32_t timeout_s,
                                     uint32_t *fw_size_out, swd_fw_target_t target);

/* Connect SWD, read STM32 UID96 (0x1FFF7590), format as 24-char uppercase hex.
 * out must be at least 25 bytes.  Returns length written (24) or 0 on failure.
 * DUT must be powered and SWD lines connected before calling. */
int swd_read_dut_uid(char *out, size_t out_sz);

/* Download firmware binary from url and store it in the specified partition.
 * Spawns a FreeRTOS task and returns immediately (call from DCMD handlers).
 * On success the image is available for "swd flash local --target pfw|prod". */
void dut_fw_store_from_url(const char *url, swd_fw_target_t target);

#ifdef __cplusplus
}
#endif
