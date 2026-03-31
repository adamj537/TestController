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
extern void run_swd_probe(const cJSON *params);
extern void run_mux_read(const cJSON *params);
extern void run_dut_gpio_set(const cJSON *params);
extern void run_dut_gpio_clear(const cJSON *params);
extern void run_dut_pin_read(const cJSON *params);
extern void run_dut_uc_adc_read(const cJSON *params);
extern void run_dut_peripheral_adc_read(const cJSON *params);
extern void run_dut_pwr_enable(const cJSON *params);
extern void run_dut_pwr_disable(const cJSON *params);
extern void run_dut_i2c_scan(const cJSON *params);
extern void run_branch_test(const cJSON *params);
extern void run_short_detect(const cJSON *params);
extern void run_sig_inject(const cJSON *params);
extern void run_sig_release(const cJSON *params);
extern void run_button_test(const cJSON *params);
/* Phase 3: snapshot-based cross-contamination detection */
extern void run_mux_snapshot(const cJSON *params);
extern void run_mux_compare_snapshot(const cJSON *params);
extern void run_ltc2498_snapshot(const cJSON *params);
extern void run_ltc2498_compare_snapshot(const cJSON *params);
extern void run_ina_read(const cJSON *params);
extern void run_delay_ms(const cJSON *params);
extern void run_dut_read_product_ver(const cJSON *params);
extern void run_vdut_off(const cJSON *params);

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
    { "swd_probe",           run_swd_probe },
    /* Phase 2: coverage matrix primitives */
    { "mux_read",            run_mux_read },
    { "dut_gpio_set",        run_dut_gpio_set },
    { "dut_gpio_clear",      run_dut_gpio_clear },
    { "dut_pin_read",        run_dut_pin_read },
    { "dut_uc_adc_read",     run_dut_uc_adc_read },
    { "dut_peripheral_adc_read", run_dut_peripheral_adc_read },
    { "dut_pwr_enable",      run_dut_pwr_enable },
    { "dut_pwr_disable",     run_dut_pwr_disable },
    { "dut_i2c_scan",        run_dut_i2c_scan },
    { "branch_test",         run_branch_test },
    { "short_detect",        run_short_detect },
    { "sig_inject",          run_sig_inject },
    { "sig_release",         run_sig_release },
    { "button_test",         run_button_test },
    /* Phase 3: snapshot-based cross-contamination detection */
    { "mux_snapshot",            run_mux_snapshot },
    { "mux_compare_snapshot",    run_mux_compare_snapshot },
    { "ltc2498_snapshot",        run_ltc2498_snapshot },
    { "ltc2498_compare_snapshot", run_ltc2498_compare_snapshot },
    /* Utility */
    { "ina_read",                run_ina_read },
    { "delay_ms",                run_delay_ms },
    /* Post-program verification */
    { "dut_read_product_ver",    run_dut_read_product_ver },
    { "vdut_off",                run_vdut_off },
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
