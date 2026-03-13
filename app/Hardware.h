/**
 * @file Hardware.h
 * @brief Hardware interface for G3 Test Controller
 *
 * Provides measurement and control functions for voltage, current, and signals.
 * Uses HAL abstraction layer for platform independence.
 */

#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
namespace Hardware {
extern "C" {
#endif

/* ==================== Initialization ==================== */

/**
 * @brief Initialize hardware subsystem
 *
 * Configures all GPIO, ADC, DAC, and UART peripherals.
 * Must be called once at startup.
 *
 * @return 0 on success, -1 on error
 */
int begin(void);

/* ==================== Voltage Control ==================== */

/**
 * @brief Set DUT voltage via adjustable regulator
 *
 * Adjusts the DAC which controls the regulator input voltage.
 *
 * @param voltage_mv Desired voltage in millivolts (e.g., 3300 for 3.3V)
 *
 * @return 0 on success, -1 on error
 */
int adjustVoltage(int32_t voltage_mv);

/**
 * @brief Read DUT output voltage (via feedback ADC)
 *
 * Reads the voltage at the regulator output for validation.
 *
 * @return Voltage in millivolts, or -1 on error
 */
int32_t readVoltage(void);

/* ==================== Current Measurement ==================== */

/**
 * @brief Read DUT supply current
 *
 * Reads the sense resistor ADC to measure current drawn by DUT.
 *
 * @return Current in milliamps, or -1 on error
 */
int32_t readCurrent(void);

/* ==================== Signal Stimulus & Measurement ==================== */

/**
 * @brief Set stimulus signal via DAC
 *
 * Outputs a test signal for the DUT to measure (Scenario D testing).
 *
 * @param signal_mv Signal level in millivolts
 *
 * @return 0 on success, -1 on error
 */
int setStimulusSignal(int32_t signal_mv);

/**
 * @brief Read DUT output signal via ADC
 *
 * Measures the DUT's response to a stimulus signal.
 *
 * @return Signal level in millivolts, or -1 on error
 */
int32_t readResponseSignal(void);

/* ==================== Branch Control ==================== */

/**
 * @brief Enable specific branch for measurement
 *
 * Activates relay to connect branch to test interface.
 *
 * @param branch_num Branch number (1-4)
 *
 * @return 0 on success, -1 on error
 */
int selectBranch(uint8_t branch_num);

/**
 * @brief Disable all branches
 *
 * Disconnects all test interface connections.
 *
 * @return 0 on success, -1 on error
 */
int deselectAllBranches(void);

/* ==================== Power Management ==================== */

/**
 * @brief Enable power rail
 *
 * Activates a specific power domain.
 *
 * @param rail Power rail identifier (from bsp_g3_tc.h)
 *
 * @return 0 on success, -1 on error
 */
int powerRailEnable(int rail);

/**
 * @brief Disable power rail
 *
 * Deactivates a specific power domain.
 *
 * @param rail Power rail identifier (from bsp_g3_tc.h)
 *
 * @return 0 on success, -1 on error
 */
int powerRailDisable(int rail);

/**
 * @brief Check if power rail is enabled
 *
 * @param rail Power rail identifier
 *
 * @return 1 if enabled, 0 if disabled, -1 on error
 */
int powerRailIsEnabled(int rail);

/* ==================== Status & Diagnostics ==================== */

/**
 * @brief Toggle status LED (heartbeat indicator)
 *
 * Used to indicate firmware is running.
 *
 * @return 0 on success, -1 on error
 */
int heartbeat(void);

/**
 * @brief Set error LED state
 *
 * Indicates error condition.
 *
 * @param on 1 to turn on, 0 to turn off
 *
 * @return 0 on success, -1 on error
 */
int setErrorLed(bool on);

#ifdef __cplusplus
}
}  /* namespace Hardware */
#endif

#endif /* HARDWARE_H */
