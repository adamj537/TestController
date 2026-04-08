#pragma once
#include <stdint.h>
#include <stdbool.h>

/* meas_log.h stub — minimal declarations for native test builds.
 * Provides the types and function signatures used by recipe_engine.c.
 * Implementations are provided as inline stubs in test_recipe_engine.c. */

typedef struct {
    char  name[32];
    char  net_id[32];
    float measured;
    char  unit[8];
    float limit_min;
    float limit_max;
    bool  verdict;
} meas_entry_t;

void                 meas_log_reset(void);
int                  meas_log_count(void);
const meas_entry_t  *meas_log_get_entry(int index);
