/* tc_mqtt.h — native test stub for test_ota_chunk.
 * Functions are declared here; definitions are provided in test_ota_chunk.c
 * via TC_MQTT_STUB_IMPL so the linker finds them as non-static symbols. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void tc_mqtt_publish_ota_progress(const char *target, const char *status,
                                   int pct, const char *code,
                                   const char *msg, bool rollback);
void tc_mqtt_publish_ndeath(void);
bool tc_mqtt_lbb_enabled(void);
