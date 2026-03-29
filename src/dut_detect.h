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

/* Block until the detection task has fully exited (mux released).
 * Call after dut_detect_stop() before taking mux ownership. */
void dut_detect_wait_stopped(void);

/* Suppress auto-start trigger without stopping detection (for manual testing).
 * dut_detect_resume() re-enables auto-start. */
void dut_detect_pause(void);
void dut_detect_resume(void);

/* Threshold: ADC mV below which a DUT is considered present.
 * G3 Rev1: no-DUT ≈ 2560 mV, DUT-seated ≈ 1666 mV — 2000 mV gives margin.
 * Used by vdac guards and vdac char safety check (F-17). */
#define DUT_DETECT_THRESHOLD_MV  2000

/* Current DUT presence state (valid after first sample). */
bool dut_detect_present(void);

/* True once the first background sample has completed.
 * Safety guards must treat not-ready as PRESENT (fail-safe) (F-19). */
bool dut_detect_is_ready(void);

/* Last raw ADC reading in mV (for diagnostics). */
int dut_detect_last_mv(void);

/* Take a single DUT presence measurement (blocking, ~200ms).
 * Returns true on successful ADC read; *mv_out receives raw mV.
 * Below DUT_DETECT_THRESHOLD_MV = DUT present, above = absent. */
bool dut_detect_sample(int *mv_out);

/* Register "dut" console command (dut detect start|stop|status|sample). */
void register_dut_commands(void);

#ifdef __cplusplus
}
#endif
