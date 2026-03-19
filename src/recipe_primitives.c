/* recipe_primitives.c — String-to-function dispatch table for test primitives.
 *
 * Maps recipe step IDs (strings) to the C functions that implement them.
 * All functions are defined in cmd_selftest.c (non-static).
 */

#include "recipe_primitives.h"
#include "cJSON.h"
#include <string.h>
#include <stddef.h>

/* Forward declarations — implemented in cmd_selftest.c */
extern void run_i2c(const cJSON *params);
extern void run_adc(const cJSON *params);
extern void run_wifi(const cJSON *params);
extern void run_ota(const cJSON *params);
extern void run_vdut(const cJSON *params);
extern void run_mux_scan(const cJSON *params);
extern void run_dut_heartbeat(const cJSON *params);
extern void run_dut_enter_test(const cJSON *params);
extern void run_dut_version(const cJSON *params);
extern void run_dut_hw_rev(const cJSON *params);
extern void run_dut_vref(const cJSON *params);
extern void run_dut_3v_rail(const cJSON *params);
extern void run_dut_flash_test(const cJSON *params);
extern void run_dut_rtc_read(const cJSON *params);
extern void run_dut_exit_test(const cJSON *params);

/* Forward declarations — implemented in g3_primitives.c */
extern void run_power_check(const cJSON *params);
extern void run_dut_program(const cJSON *params);
extern void run_dut_read_id(const cJSON *params);

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
    /* G3 MB recipe primitives */
    { "power_check",         run_power_check },
    { "dut_program",         run_dut_program },
    { "dut_read_id",         run_dut_read_id },
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
