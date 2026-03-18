/* recipe_primitives.c — String-to-function dispatch table for test primitives.
 *
 * Maps recipe step IDs (strings) to the C functions that implement them.
 * All functions are defined in cmd_selftest.c (non-static).
 */

#include "recipe_primitives.h"
#include <string.h>
#include <stddef.h>

/* Forward declarations — implemented in cmd_selftest.c */
extern void run_i2c(void);
extern void run_adc(void);
extern void run_wifi(void);
extern void run_ota(void);
extern void run_vdut(void);
extern void run_mux_scan(void);
extern void run_dut_heartbeat(void);
extern void run_dut_enter_test(void);
extern void run_dut_version(void);
extern void run_dut_hw_rev(void);
extern void run_dut_vref(void);
extern void run_dut_3v_rail(void);
extern void run_dut_flash_test(void);
extern void run_dut_rtc_read(void);
extern void run_dut_exit_test(void);

static const primitive_entry_t s_primitives[] = {
    /* TCC/TIE carrier checks */
    { "i2c",                 run_i2c },
    { "adc",                 run_adc },
    { "wifi",                run_wifi },
    { "ota",                 run_ota },
    { "vdut",                run_vdut },
    { "mux_scan",            run_mux_scan },
    /* DUT pogo-dependent */
    { "dut_heartbeat",       run_dut_heartbeat },
    { "dut_enter_test",      run_dut_enter_test },
    { "dut_version",         run_dut_version },
    { "dut_hw_rev",          run_dut_hw_rev },
    { "dut_uc_adc_vref",     run_dut_vref },
    { "dut_uc_adc_3v_rail",  run_dut_3v_rail },
    { "dut_flash_test",      run_dut_flash_test },
    { "dut_rtc_read",        run_dut_rtc_read },
    { "dut_exit_test",       run_dut_exit_test },
};

#define PRIMITIVE_COUNT  (sizeof(s_primitives) / sizeof(s_primitives[0]))

primitive_fn_t recipe_primitives_lookup(const char *id)
{
    if (!id) return NULL;
    for (int i = 0; i < (int)PRIMITIVE_COUNT; i++) {
        if (strcmp(s_primitives[i].id, id) == 0)
            return s_primitives[i].fn;
    }
    return NULL;
}

const primitive_entry_t *recipe_primitives_table(int *count_out)
{
    if (count_out) *count_out = (int)PRIMITIVE_COUNT;
    return s_primitives;
}
