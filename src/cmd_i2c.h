#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void register_i2c_commands(void);

/* Selftest API — initializes bus with defaults if not already initialized */
bool i2c_ensure_initialized(void);
bool i2c_probe(uint8_t addr);
bool i2c_reinit(int sda, int scl);  /* tear down + re-init with new pins */
bool i2c_write_raw(uint8_t addr, const uint8_t *buf, int len);  /* single-byte devices e.g. PCF8574 */
bool i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val);
bool i2c_write_reg16(uint8_t addr, uint8_t reg, uint16_t val);  /* big-endian 16-bit write */
bool i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *buf, int len);

#ifdef __cplusplus
}
#endif
