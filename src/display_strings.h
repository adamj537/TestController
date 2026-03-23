#pragma once

/* ── Operator display string constants — 16×2 LCD ────────────────────────── *
 *
 * All strings are ≤ 16 characters to fit a standard 16×2 character display.
 * Line 2 entries marked [runtime] are formatted at display time with DUT ref,
 * step ID, or connectivity state substituted in.
 *
 * Approved by g3-planning 2026-03-14.
 * Hold times are defaults; override via recipe pass_hold_s / fail_hold_s.
 */

/* ── Hold times (milliseconds) ── */
#define DISP_HOLD_PASS_MS    3000   /* PASS display hold — recipe: pass_hold_s */
#define DISP_HOLD_FAIL_MS    5000   /* FAIL display hold — recipe: fail_hold_s */
#define DISP_HOLD_ABORT_MS      0   /* ABORTED — held until next start DCMD */
#define DISP_HOLD_ESTOP_MS      0   /* E-STOP  — held until operator resets */
#define DISP_HOLD_SFAIL_MS      0   /* SELFTEST FAIL — held until operator action */

/* ── Line 1 strings ── */
#define DISP_L1_IDLE          "READY"
#define DISP_L1_PRECHECK      "CHECKING..."
#define DISP_L1_PRE_GATE      "PRE-TEST..."
#define DISP_L1_FLASH_DUT     "FLASHING DUT..."
#define DISP_L1_TESTING       "TESTING..."
#define DISP_L1_PASS          "**** PASS ****"
#define DISP_L1_FAIL          "**** FAIL ****"
#define DISP_L1_FLASH_FAIL    "FLASH FAIL"
#define DISP_L1_SELFTEST_FAIL "SELFTEST FAIL"
#define DISP_L1_NO_BROKER     "NO BROKER"
#define DISP_L1_ABORTED       "ABORTED"
#define DISP_L1_ESTOP         "** E-STOP **"

/* ── Line 2 fixed strings ── */
#define DISP_L2_IDLE_CONN     "INSERT DUT"       /* broker connected */
#define DISP_L2_IDLE_SOLO     "STANDALONE OK"    /* broker offline */
#define DISP_L2_FLASH_FAIL    "CHECK DUT PWR"
#define DISP_L2_SELFTEST_FAIL "CHECK FIXTURE"
#define DISP_L2_NO_BROKER     "STANDALONE OK"
#define DISP_L2_ABORTED       "REMOVE DUT"
#define DISP_L2_ESTOP         "RESET TO CLEAR"
#define DISP_L2_BLANK         ""

/* ── Line 2 format strings (runtime substitution required) ── */
/* DUT reference (CRC32 of UID96): format with snprintf(buf, 17, DISP_L2_FMT_DUT, dut_ref)
 * dut_ref is "XXXX-XXXX" (9 chars); total "DUT: XXXX-XXXX" = 14 chars. */
#define DISP_L2_FMT_DUT       "DUT: %s"

/* Failing step ID: truncate step_id to 16 chars before display.
 * snprintf(buf, 17, DISP_L2_FMT_STEP, step_id) */
#define DISP_L2_FMT_STEP      "%.16s"
