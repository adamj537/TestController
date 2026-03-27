/* tc_mqtt.h — native stub for test_recipe_engine.
 * Shadows common/src/tc_mqtt.h.  Provides tc_mqtt_check_t and publisher
 * declarations used by cmd_selftest.h and recipe_engine.c. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *id;
    bool        pass;
    bool        has_value;
    int         value;
} tc_mqtt_check_t;

void tc_mqtt_publish_step_result(int step_index, const char *step_id,
                                  const char *step_status, bool passed,
                                  uint32_t duration_ms);

void tc_mqtt_publish_test_progress(const char *event_type, bool in_progress,
                                    int step_index, int step_total,
                                    const char *step_name, const char *step_status);
