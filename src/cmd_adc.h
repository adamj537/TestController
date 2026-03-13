#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void register_adc_commands(void);

/* Selftest API — reads one ADC1 channel; mv_out=-1 if calibration unavailable */
bool adc_selftest_read(int channel, int *raw_out, int *mv_out);

#ifdef __cplusplus
}
#endif
