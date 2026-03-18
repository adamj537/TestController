#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the DUT presence detection background task.
 * Polls U8 ch3 → MUX1 ch12 → ADC128 CH1 at ~1 Hz.
 * Publishes dut_present DDATA on state change (debounced). */
void dut_detect_start(void);

/* Stop the detection task. */
void dut_detect_stop(void);

/* Current DUT presence state (valid after first sample). */
bool dut_detect_present(void);

/* Last raw ADC reading in mV (for diagnostics). */
int dut_detect_last_mv(void);

/* Register "dut" console command (dut detect start|stop|status|sample). */
void register_dut_commands(void);

#ifdef __cplusplus
}
#endif
