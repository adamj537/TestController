#pragma once

/* tc_config.h — Device configuration: load, access, mutate, persist.
 *
 * Config is a JSON blob stored in STORAGE_DOMAIN_CONFIGURATION under the key
 * "device".  It is loaded once at startup (tc_config_load) and held in a
 * cJSON tree in RAM for fast read access.  Mutations are staged in the tree
 * and written back to storage only on tc_config_save() or "config save".
 *
 * Key paths use dot notation: "vdut.slope_mv_per_pct", "fixture_id".
 * All accessors accept a default that is returned when the key is absent.
 *
 * VDUT voltage model
 * ------------------
 * The MP2315 regulator exhibits a roughly linear (inverting) duty-to-voltage
 * curve at the DUT connector.  Two calibration points in the config define it:
 *
 *   V_mv = vdut.slope_mv_per_pct × duty_pct + vdut.intercept_mv
 *
 * Invert to get the duty for a target voltage:
 *
 *   duty_pct = (target_mv - vdut.intercept_mv) / vdut.slope_mv_per_pct
 *
 * Calibrate by running "selftest vdut" without a DUT connected and recording
 * two INA219 V_BUS readings at known duty cycles, then solving for slope and
 * intercept.  Store with:
 *
 *   config set vdut.slope_mv_per_pct -87
 *   config set vdut.intercept_mv 11200
 *   config save
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

/* Load config from storage into the in-RAM cJSON tree.
 * If no stored config exists, the factory defaults are used in RAM
 * (nothing is written to storage until tc_config_save is called).
 * Safe to call multiple times — reloads from storage each time. */
int tc_config_load(void);

/* Write the current in-RAM tree to storage. */
int tc_config_save(void);

/* Reset the in-RAM tree to factory defaults (does NOT save automatically). */
void tc_config_reset_defaults(void);

/* ── Read accessors (dot-path) ───────────────────────────────────────────── */

int         tc_config_get_int(const char *path, int def);
float       tc_config_get_float(const char *path, float def);
const char *tc_config_get_str(const char *path, const char *def);

/* ── Write accessors (stage in RAM — call tc_config_save to persist) ─────── */

void tc_config_set_int(const char *path, int val);
void tc_config_set_float(const char *path, float val);
void tc_config_set_str(const char *path, const char *val);

/* ── VDUT helpers ────────────────────────────────────────────────────────── */

/* Compute duty% for target_mv using calibrated slope/intercept.
 * Clamps result to [1, 99].  Returns -1 if calibration data is missing
 * (slope == 0) — caller must treat as an error and NOT enable VDUT. */
int tc_config_vdut_duty_for_mv(int target_mv);

/* ── Console ─────────────────────────────────────────────────────────────── */

void register_config_commands(void);

#ifdef __cplusplus
}
#endif
