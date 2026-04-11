#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Register the "tie" console command group.
 * Must be called after tie_mux_gpio_init() and tie_adc128_init() are available
 * (both are idempotent — cmd_tie.c calls tie_adc128_init() lazily on each read). */
void register_tie_commands(void);

#ifdef __cplusplus
}
#endif
